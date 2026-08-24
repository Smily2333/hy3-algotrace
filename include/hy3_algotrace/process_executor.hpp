// hy3-algotrace -- deliberately small, direct-argv process execution seam.
//
// This is not a security sandbox.  CandidateRunner only supplies repository
// controlled fixtures and a fresh runner-owned working directory.  The
// executor enforces bounded wall time and captured output, and kills the
// complete child process tree when either bound is exceeded.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hy3 {

struct ProcessLimits {
    // CreateProcess/fork are synchronous OS calls.  This bounds only local
    // launch bookkeeping; wall_timeout_ms is the enforceable child limit.
    std::uint64_t startup_timeout_ms = 5'000;
    std::uint64_t wall_timeout_ms = 5'000;
    std::size_t stdout_limit_bytes = 64U * 1024U;
    std::size_t stderr_limit_bytes = 64U * 1024U;
};

struct ProcessRequest {
    // The executable and working directory must be absolute.  arguments does
    // not include argv[0].  No field is interpreted by a shell.
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::string stdin_bytes;
    std::filesystem::path working_directory;
    ProcessLimits limits;
};

enum class ProcessTermination {
    Exited,
    TimedOut,
    OutputLimitExceeded,
    LaunchFailed,
};

struct ProcessResult {
    ProcessTermination termination = ProcessTermination::LaunchFailed;
    bool started = false;
    int exit_code = -1;
    std::uint64_t runtime_ms = 0;
    std::string stdout_bytes;
    std::string stderr_bytes;
    bool stdout_truncated = false;
    bool stderr_truncated = false;
    // For runner/launch errors only. Never contains an assembled command.
    std::string error_message;
};

class IProcessExecutor {
public:
    virtual ~IProcessExecutor() = default;
    virtual ProcessResult execute(const ProcessRequest& request) = 0;
};

class ProcessExecutor final : public IProcessExecutor {
public:
    ProcessResult execute(const ProcessRequest& request) override;
};

} // namespace hy3
