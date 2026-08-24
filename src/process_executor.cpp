#include "hy3_algotrace/process_executor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace hy3 {
namespace {

using Clock = std::chrono::steady_clock;

bool validRequest(const ProcessRequest& request, ProcessResult& result) {
    if (!request.executable.is_absolute() ||
        !request.working_directory.is_absolute()) {
        result.error_message = "executable and working directory must be absolute";
        return false;
    }
    if (request.limits.startup_timeout_ms == 0 ||
        request.limits.wall_timeout_ms == 0) {
        result.error_message = "process timeouts must be nonzero";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(request.working_directory, ec) || ec) {
        result.error_message = "working directory is not available";
        return false;
    }
    if (!std::filesystem::is_regular_file(request.executable, ec) || ec) {
        result.error_message = "executable is not an available file";
        return false;
    }
    return true;
}

bool appendCapped(std::string& destination, bool& truncated,
                  const char* bytes, std::size_t count, std::size_t limit) {
    const std::size_t room = destination.size() < limit ? limit - destination.size() : 0;
    const std::size_t copied = std::min(room, count);
    destination.append(bytes, copied);
    if (copied != count) {
        truncated = true;
        return true;
    }
    return false;
}

std::uint64_t elapsedMs(Clock::time_point began) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began)
            .count());
}

#ifdef _WIN32

class Handle final {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    HANDLE get() const { return value_; }
    HANDLE release() {
        HANDLE value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr) {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }
private:
    HANDLE value_ = nullptr;
};

bool utf8ToWide(const std::string& value, std::wstring& converted) {
    if (value.empty()) {
        converted.clear();
        return true;
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) return false;
    converted.resize(static_cast<std::size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), converted.data(), size) == size;
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring quoted(L"\"");
    std::size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(character);
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool createPipe(Handle& readEnd, Handle& writeEnd, bool parentKeepsRead) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &attributes, 0)) return false;
    readEnd.reset(readHandle);
    writeEnd.reset(writeHandle);
    HANDLE parentHandle = parentKeepsRead ? readEnd.get() : writeEnd.get();
    return SetHandleInformation(parentHandle, HANDLE_FLAG_INHERIT, 0) != FALSE;
}

bool drainWindowsPipe(HANDLE pipe, std::string& destination, bool& truncated,
                      std::size_t limit) {
    bool exceeded = false;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) break;
        std::array<char, 8192> buffer{};
        DWORD read = 0;
        const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(pipe, buffer.data(), wanted, &read, nullptr) || read == 0) break;
        exceeded = appendCapped(destination, truncated, buffer.data(), read, limit) || exceeded;
    }
    return exceeded;
}

void drainWindowsAfterExit(HANDLE pipe, std::string& destination, bool& truncated,
                           std::size_t limit) {
    (void)drainWindowsPipe(pipe, destination, truncated, limit);
}

#else

class Fd final {
public:
    Fd() = default;
    explicit Fd(int value) : value_(value) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    int get() const { return value_; }
    int release() { const int value = value_; value_ = -1; return value; }
    void reset(int value = -1) {
        if (value_ >= 0) close(value_);
        value_ = value;
    }
private:
    int value_ = -1;
};

bool nonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool closeOnExec(int fd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool drainUnixFd(int fd, std::string& destination, bool& truncated,
                 std::size_t limit) {
    bool exceeded = false;
    std::array<char, 8192> buffer{};
    for (;;) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            exceeded = appendCapped(destination, truncated, buffer.data(),
                                    static_cast<std::size_t>(count), limit) || exceeded;
        } else if (count == 0 || (count < 0 && errno != EINTR)) {
            break;
        }
    }
    return exceeded;
}

bool writeUnixInput(int fd, const std::string& input, std::size_t& offset) {
    while (offset < input.size()) {
        const ssize_t count = write(fd, input.data() + offset,
                                    std::min<std::size_t>(8192, input.size() - offset));
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

#endif

} // namespace

ProcessResult ProcessExecutor::execute(const ProcessRequest& request) {
    ProcessResult result;
    if (!validRequest(request, result)) return result;

    const auto launchBegan = Clock::now();

#ifdef _WIN32
    Handle stdinRead;
    Handle stdinWrite;
    Handle stdoutRead;
    Handle stdoutWrite;
    Handle stderrRead;
    Handle stderrWrite;
    if (!createPipe(stdinRead, stdinWrite, false) ||
        !createPipe(stdoutRead, stdoutWrite, true) ||
        !createPipe(stderrRead, stderrWrite, true)) {
        result.error_message = "could not create process pipes";
        return result;
    }

    std::vector<std::wstring> wideArgs;
    wideArgs.reserve(request.arguments.size() + 1);
    wideArgs.push_back(request.executable.wstring());
    for (const auto& argument : request.arguments) {
        std::wstring converted;
        if (!utf8ToWide(argument, converted)) {
            result.error_message = "argument is not valid UTF-8";
            return result;
        }
        wideArgs.push_back(std::move(converted));
    }
    std::wstring commandLine;
    for (const auto& argument : wideArgs) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += quoteWindowsArgument(argument);
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.get()) {
        result.error_message = "could not create process job";
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                 &jobInfo, sizeof(jobInfo))) {
        result.error_message = "could not configure process job";
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead.get();
    startup.hStdOutput = stdoutWrite.get();
    startup.hStdError = stderrWrite.get();
    PROCESS_INFORMATION process{};
    const std::wstring executable = request.executable.wstring();
    const std::wstring workingDirectory = request.working_directory.wstring();
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(),
                        &startup, &process)) {
        result.error_message = "could not start process";
        return result;
    }
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    if (!AssignProcessToJobObject(job.get(), processHandle.get())) {
        TerminateProcess(processHandle.get(), 1);
        WaitForSingleObject(processHandle.get(), INFINITE);
        result.error_message = "could not assign process job";
        return result;
    }
    stdinRead.reset();
    stdoutWrite.reset();
    stderrWrite.reset();
    result.started = true;

    // A writer thread prevents a large stdin stream from blocking pipe drains.
    std::thread inputWriter([pipe = stdinWrite.release(), input = request.stdin_bytes]() {
        const Handle owned(pipe);
        std::size_t offset = 0;
        while (offset < input.size()) {
            DWORD written = 0;
            const DWORD wanted = static_cast<DWORD>(
                std::min<std::size_t>(8192, input.size() - offset));
            if (!WriteFile(owned.get(), input.data() + offset, wanted, &written, nullptr) ||
                written == 0) break;
            offset += written;
        }
    });

    ProcessTermination termination = ProcessTermination::Exited;
    bool ended = false;
    while (!ended) {
        const bool outputExceeded =
            drainWindowsPipe(stdoutRead.get(), result.stdout_bytes, result.stdout_truncated,
                             request.limits.stdout_limit_bytes) ||
            drainWindowsPipe(stderrRead.get(), result.stderr_bytes, result.stderr_truncated,
                             request.limits.stderr_limit_bytes);
        if (outputExceeded) {
            termination = ProcessTermination::OutputLimitExceeded;
            TerminateJobObject(job.get(), 1);
            WaitForSingleObject(processHandle.get(), INFINITE);
            ended = true;
        } else if (elapsedMs(launchBegan) > request.limits.wall_timeout_ms) {
            termination = ProcessTermination::TimedOut;
            TerminateJobObject(job.get(), 1);
            WaitForSingleObject(processHandle.get(), INFINITE);
            ended = true;
        } else if (WaitForSingleObject(processHandle.get(), 0) == WAIT_OBJECT_0) {
            ended = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    inputWriter.join();
    drainWindowsAfterExit(stdoutRead.get(), result.stdout_bytes, result.stdout_truncated,
                          request.limits.stdout_limit_bytes);
    drainWindowsAfterExit(stderrRead.get(), result.stderr_bytes, result.stderr_truncated,
                          request.limits.stderr_limit_bytes);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle.get(), &exitCode)) {
        result.exit_code = static_cast<int>(exitCode);
    }
    result.runtime_ms = elapsedMs(launchBegan);
    result.termination = termination;

#else
    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    int errorPipe[2] = {-1, -1};
    if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0 ||
        pipe(errorPipe) != 0) {
        for (int fd : {stdinPipe[0], stdinPipe[1], stdoutPipe[0], stdoutPipe[1],
                       stderrPipe[0], stderrPipe[1], errorPipe[0], errorPipe[1]}) {
            if (fd >= 0) close(fd);
        }
        result.error_message = "could not create process pipes";
        return result;
    }
    Fd stdinRead(stdinPipe[0]);
    Fd stdinWrite(stdinPipe[1]);
    Fd stdoutRead(stdoutPipe[0]);
    Fd stdoutWrite(stdoutPipe[1]);
    Fd stderrRead(stderrPipe[0]);
    Fd stderrWrite(stderrPipe[1]);
    Fd errorRead(errorPipe[0]);
    Fd errorWrite(errorPipe[1]);
    if (!nonBlocking(stdinWrite.get()) || !nonBlocking(stdoutRead.get()) ||
        !nonBlocking(stderrRead.get()) || !nonBlocking(errorRead.get()) ||
        !closeOnExec(errorWrite.get())) {
        result.error_message = "could not configure process pipes";
        return result;
    }

    const pid_t child = fork();
    if (child < 0) {
        result.error_message = "could not fork process";
        return result;
    }
    if (child == 0) {
        const auto fail = [&]() {
            const int error = errno;
            (void)write(errorWrite.get(), &error, sizeof(error));
            _exit(127);
        };
        if (setpgid(0, 0) != 0 || chdir(request.working_directory.c_str()) != 0 ||
            dup2(stdinRead.get(), STDIN_FILENO) < 0 ||
            dup2(stdoutWrite.get(), STDOUT_FILENO) < 0 ||
            dup2(stderrWrite.get(), STDERR_FILENO) < 0) {
            fail();
        }
        stdinRead.release();
        stdinWrite.reset();
        stdoutRead.reset();
        stdoutWrite.release();
        stderrRead.reset();
        stderrWrite.release();
        errorRead.reset();
        errorWrite.release();
        std::vector<char*> argv;
        argv.reserve(request.arguments.size() + 2);
        const std::string executable = request.executable.string();
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& argument : request.arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        fail();
    }

    // Close child-only pipe ends and make the child a process-group leader.
    (void)setpgid(child, child);
    stdinRead.reset();
    stdoutWrite.reset();
    stderrWrite.reset();
    errorWrite.reset();
    result.started = true;

    sigset_t sigpipeSet{};
    sigemptyset(&sigpipeSet);
    sigaddset(&sigpipeSet, SIGPIPE);
    sigset_t oldSignalMask{};
    const bool signalMasked = pthread_sigmask(SIG_BLOCK, &sigpipeSet, &oldSignalMask) == 0;

    std::size_t inputOffset = 0;
    bool inputOpen = true;
    bool execFailed = false;
    ProcessTermination termination = ProcessTermination::Exited;
    int status = 0;
    bool ended = false;
    while (!ended) {
        bool outputExceeded =
            drainUnixFd(stdoutRead.get(), result.stdout_bytes, result.stdout_truncated,
                        request.limits.stdout_limit_bytes) ||
            drainUnixFd(stderrRead.get(), result.stderr_bytes, result.stderr_truncated,
                        request.limits.stderr_limit_bytes);
        int launchError = 0;
        const ssize_t errorBytes = read(errorRead.get(), &launchError, sizeof(launchError));
        if (errorBytes > 0) execFailed = true;

        if (inputOpen) {
            const bool wrote = writeUnixInput(stdinWrite.get(), request.stdin_bytes, inputOffset);
            if (!wrote || inputOffset == request.stdin_bytes.size()) {
                stdinWrite.reset();
                inputOpen = false;
            }
        }
        if (outputExceeded) {
            termination = ProcessTermination::OutputLimitExceeded;
            (void)kill(-child, SIGKILL);
        } else if (elapsedMs(launchBegan) > request.limits.wall_timeout_ms) {
            termination = ProcessTermination::TimedOut;
            (void)kill(-child, SIGKILL);
        }

        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            ended = true;
        } else if (waited < 0) {
            termination = ProcessTermination::LaunchFailed;
            result.error_message = "could not wait for process";
            ended = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    stdinWrite.reset();
    (void)drainUnixFd(stdoutRead.get(), result.stdout_bytes, result.stdout_truncated,
                      request.limits.stdout_limit_bytes);
    (void)drainUnixFd(stderrRead.get(), result.stderr_bytes, result.stderr_truncated,
                      request.limits.stderr_limit_bytes);
    int launchError = 0;
    if (read(errorRead.get(), &launchError, sizeof(launchError)) > 0) execFailed = true;
    if (signalMasked) {
        // A write to a pipe whose reader has exited raises SIGPIPE even when
        // masked.  Consume the pending signal before restoring the caller's
        // mask so an early-exiting controlled fixture cannot kill the runner.
        timespec noWait{};
        while (sigtimedwait(&sigpipeSet, nullptr, &noWait) == SIGPIPE) {}
        (void)pthread_sigmask(SIG_SETMASK, &oldSignalMask, nullptr);
    }

    if (execFailed && termination == ProcessTermination::Exited) {
        result.started = false;
        termination = ProcessTermination::LaunchFailed;
        result.error_message = "could not execute process";
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    result.runtime_ms = elapsedMs(launchBegan);
    result.termination = termination;
#endif

    return result;
}

} // namespace hy3
