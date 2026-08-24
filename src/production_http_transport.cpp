#include "hy3_algotrace/production_http_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#include <mutex>
#endif

namespace hy3 {
namespace {

constexpr std::uint64_t kDefaultConnectTimeoutMs = 10U * 1000U;
constexpr std::uint64_t kDefaultTotalTimeoutMs = 60U * 1000U;

HttpResponse failedResponse(const char* message) {
    HttpResponse response;
    response.transport_status = HttpTransportStatus::Failed;
    response.transport_message = message;
    return response;
}

HttpResponse timeoutResponse() {
    HttpResponse response;
    response.transport_status = HttpTransportStatus::Timeout;
    response.transport_message = "production HTTP transport timed out";
    return response;
}

bool containsControl(const std::string& text) noexcept {
    for (const unsigned char value : text) {
        if (value < 0x20U || value == 0x7fU) {
            return true;
        }
    }
    return false;
}

bool isHttpsUrl(const std::string& url) noexcept {
    constexpr const char* prefix = "https://";
    if (url.rfind(prefix, 0) != 0 || containsControl(url)) {
        return false;
    }
    const std::size_t authorityBegin = 8;
    const std::size_t authorityEnd = url.find_first_of("/?#", authorityBegin);
    const std::string authority = url.substr(
        authorityBegin,
        authorityEnd == std::string::npos ? std::string::npos
                                           : authorityEnd - authorityBegin);
    // Credentials in a URL are both unsafe and unnecessary: authorization is
    // supplied only as an in-memory request header by the Hy3 adapter.
    return !authority.empty() && authority.find('@') == std::string::npos &&
           authority.find_first_of(" \t\r\n") == std::string::npos &&
           url.find('#') == std::string::npos;
}

bool safeRequestHeader(const std::pair<std::string, std::string>& header) noexcept {
    return !header.first.empty() && !containsControl(header.first) &&
           !containsControl(header.second) && header.first.find(':') == std::string::npos;
}

std::uint64_t connectTimeout(const HttpRequest& request) noexcept {
    return request.connect_timeout_ms == 0 ? kDefaultConnectTimeoutMs
                                           : request.connect_timeout_ms;
}

std::uint64_t totalTimeout(const HttpRequest& request) noexcept {
    if (request.total_timeout_ms != 0) {
        return request.total_timeout_ms;
    }
    return request.read_timeout_ms != 0 ? request.read_timeout_ms
                                        : kDefaultTotalTimeoutMs;
}

std::uint64_t remainingTimeout(
    std::chrono::steady_clock::time_point started,
    std::uint64_t totalMs) noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsed < 0 || static_cast<std::uint64_t>(elapsed) >= totalMs) {
        return 0;
    }
    return totalMs - static_cast<std::uint64_t>(elapsed);
}

bool isOfficialTokenHubUrl(const std::string& url) noexcept {
    constexpr const char* origin = "https://tokenhub.tencentmaas.com";
    const std::string prefix(origin);
    return url == prefix ||
           (url.size() > prefix.size() && url.rfind(prefix, 0) == 0 &&
            url[prefix.size()] == '/');
}

bool safeResponseHeaderValue(const std::string& value) noexcept {
    return !value.empty() && value.size() <= 128U && !containsControl(value);
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    return value;
}

void saveRequestIdHeader(HttpResponse& response,
                         std::string name,
                         std::string value) {
    name = lowerAscii(std::move(name));
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    value.erase(0, first);
    if ((name == "request-id" || name == "x-request-id" ||
         name == "x-tc-requestid") &&
        safeResponseHeaderValue(value)) {
        response.headers.emplace_back(std::move(name), std::move(value));
    }
}

#ifdef _WIN32

int boundedTimeout(std::uint64_t value) noexcept {
    return value > static_cast<std::uint64_t>(INT_MAX)
               ? INT_MAX
               : static_cast<int>(value);
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), converted.data(),
                            length) != length) {
        return {};
    }
    return converted;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string converted(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), converted.data(),
                            length, nullptr, nullptr) != length) {
        return {};
    }
    return converted;
}

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET value = nullptr) noexcept : value_(value) {}
    ~WinHttpHandle() { if (value_ != nullptr) { WinHttpCloseHandle(value_); } }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    HINTERNET get() const noexcept { return value_; }
private:
    HINTERNET value_;
};

bool wasTimeout() noexcept {
    return GetLastError() == ERROR_WINHTTP_TIMEOUT;
}

void captureHeader(HINTERNET request, const wchar_t* name, HttpResponse& response) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, nullptr, &bytes,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return;
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1U, L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(),
                             &bytes, WINHTTP_NO_HEADER_INDEX)) {
        return;
    }
    std::wstring raw(value.data());
    const std::string converted = wideToUtf8(raw);
    if (!converted.empty()) {
        saveRequestIdHeader(response, wideToUtf8(name), converted);
    }
}

HttpResponse performWindows(const HttpRequest& request, std::size_t maxBytes) {
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t timeoutMs = totalTimeout(request);
    if (request.url.size() >
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        return failedResponse("production HTTP transport rejected oversized URL");
    }
    const std::wstring url = utf8ToWide(request.url);
    if (url.empty()) {
        return failedResponse("production HTTP transport rejected invalid UTF-8 URL");
    }
    std::vector<wchar_t> host(256U);
    std::vector<wchar_t> path(4096U);
    std::vector<wchar_t> extra(4096U);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = extra.data();
    components.dwExtraInfoLength = static_cast<DWORD>(extra.size());
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        return failedResponse("production HTTP transport rejected malformed HTTPS URL");
    }

    const std::wstring hostName(host.data(), components.dwHostNameLength);
    std::wstring objectName(path.data(), components.dwUrlPathLength);
    objectName.append(extra.data(), components.dwExtraInfoLength);
    if (objectName.empty()) {
        objectName = L"/";
    }

    WinHttpHandle session(WinHttpOpen(L"hy3-algotrace/phase2c",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.get() == nullptr) {
        return failedResponse("production HTTP transport could not initialize WinHTTP");
    }
    const int connectMs = boundedTimeout(
        std::min(connectTimeout(request), timeoutMs));
    int remainingMs = boundedTimeout(remainingTimeout(started, timeoutMs));
    if (remainingMs == 0) {
        return timeoutResponse();
    }
    if (!WinHttpSetTimeouts(session.get(), connectMs, connectMs, remainingMs,
                            remainingMs)) {
        return failedResponse("production HTTP transport could not configure timeouts");
    }
    WinHttpHandle connection(WinHttpConnect(session.get(), hostName.c_str(),
                                            components.nPort, 0));
    if (connection.get() == nullptr) {
        return wasTimeout() ? timeoutResponse()
                            : failedResponse("production HTTP transport could not connect");
    }
    WinHttpHandle handle(WinHttpOpenRequest(connection.get(), L"POST",
                                             objectName.c_str(), nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE));
    if (handle.get() == nullptr) {
        return failedResponse("production HTTP transport could not create request");
    }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(handle.get(), WINHTTP_OPTION_DISABLE_FEATURE,
                          &disabled, sizeof(disabled))) {
        return failedResponse("production HTTP transport could not disable redirects");
    }

    std::string headerText;
    for (const auto& header : request.headers) {
        headerText += header.first;
        headerText += ": ";
        headerText += header.second;
        headerText += "\r\n";
    }
    const std::wstring headers = utf8ToWide(headerText);
    if (!headerText.empty() && headers.empty()) {
        return failedResponse("production HTTP transport rejected invalid UTF-8 headers");
    }
    if (request.body.size() >
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        return failedResponse("production HTTP transport rejected oversized request body");
    }
    const void* body = request.body.empty() ? nullptr : request.body.data();
    const DWORD bodyLength = static_cast<DWORD>(request.body.size());
    remainingMs = boundedTimeout(remainingTimeout(started, timeoutMs));
    if (remainingMs == 0) {
        return timeoutResponse();
    }
    if (!WinHttpSetTimeouts(handle.get(), connectMs, connectMs, remainingMs,
                            remainingMs)) {
        return failedResponse("production HTTP transport could not update timeout");
    }
    if (!WinHttpSendRequest(handle.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                           : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
                            const_cast<void*>(body), bodyLength, bodyLength, 0)) {
        return wasTimeout() ? timeoutResponse()
                            : failedResponse("production HTTP transport request failed");
    }
    remainingMs = boundedTimeout(remainingTimeout(started, timeoutMs));
    if (remainingMs == 0) {
        return timeoutResponse();
    }
    if (!WinHttpSetTimeouts(handle.get(), connectMs, connectMs, remainingMs,
                            remainingMs)) {
        return failedResponse("production HTTP transport could not update timeout");
    }
    if (!WinHttpReceiveResponse(handle.get(), nullptr)) {
        return wasTimeout() ? timeoutResponse()
                            : failedResponse("production HTTP transport request failed");
    }

    HttpResponse response;
    response.transport_status = HttpTransportStatus::Completed;
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE |
                             WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        return failedResponse("production HTTP transport returned no HTTP status");
    }
    response.status_code = static_cast<int>(status);
    captureHeader(handle.get(), L"x-request-id", response);
    captureHeader(handle.get(), L"request-id", response);
    captureHeader(handle.get(), L"x-tc-requestid", response);

    for (;;) {
        const std::uint64_t remaining = remainingTimeout(started, timeoutMs);
        if (remaining == 0) {
            return timeoutResponse();
        }
        if (!WinHttpSetTimeouts(handle.get(), connectMs, connectMs, connectMs,
                                boundedTimeout(remaining))) {
            return failedResponse("production HTTP transport could not update timeout");
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle.get(), &available)) {
            return wasTimeout() ? timeoutResponse()
                                : failedResponse("production HTTP transport read failed");
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() > maxBytes ||
            static_cast<std::size_t>(available) > maxBytes - response.body.size()) {
            return failedResponse("production HTTP transport response exceeds safety limit");
        }
        std::vector<std::uint8_t> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(handle.get(), chunk.data(), available, &read)) {
            return wasTimeout() ? timeoutResponse()
                                : failedResponse("production HTTP transport read failed");
        }
        response.body.insert(response.body.end(), chunk.begin(), chunk.begin() + read);
    }
    return response;
}

#else

long boundedCurlTimeout(std::uint64_t value) noexcept {
    return value > static_cast<std::uint64_t>(LONG_MAX)
               ? LONG_MAX
               : static_cast<long>(value);
}

struct CurlResponseContext {
    HttpResponse response;
    std::size_t max_bytes = 0;
    bool response_too_large = false;
    bool allocation_failed = false;
};

size_t curlWrite(char* contents, size_t size, size_t count, void* opaque) {
    CurlResponseContext& context = *static_cast<CurlResponseContext*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        context.response_too_large = true;
        return 0;
    }
    const std::size_t bytes = size * count;
    if (context.response.body.size() > context.max_bytes ||
        bytes > context.max_bytes - context.response.body.size()) {
        context.response_too_large = true;
        return 0;
    }
    try {
        context.response.body.insert(context.response.body.end(),
                                     reinterpret_cast<std::uint8_t*>(contents),
                                     reinterpret_cast<std::uint8_t*>(contents) + bytes);
        return bytes;
    } catch (...) {
        context.allocation_failed = true;
        return 0;
    }
}

size_t curlHeader(char* contents, size_t size, size_t count, void* opaque) {
    CurlResponseContext& context = *static_cast<CurlResponseContext*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return 0;
    }
    const std::size_t bytes = size * count;
    try {
        const std::string line(contents, bytes);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            saveRequestIdHeader(context.response, line.substr(0, colon),
                                line.substr(colon + 1));
        }
    } catch (...) {
        // Header capture is optional audit metadata.  Do not convert a valid
        // response into a failed paid call merely because it cannot be stored.
    }
    return bytes;
}

bool ensureCurlGlobalInit() {
    static std::once_flag initialized;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(initialized, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
}

HttpResponse performCurl(const HttpRequest& request, std::size_t maxBytes) {
    if (!ensureCurlGlobalInit()) {
        return failedResponse("production HTTP transport could not initialize libcurl");
    }
    CURL* raw = curl_easy_init();
    if (raw == nullptr) {
        return failedResponse("production HTTP transport could not initialize request");
    }
    struct CurlGuard {
        CURL* handle;
        ~CurlGuard() { curl_easy_cleanup(handle); }
    } guard{raw};
    curl_slist* headerList = nullptr;
    struct HeaderGuard {
        curl_slist*& list;
        ~HeaderGuard() { if (list != nullptr) { curl_slist_free_all(list); } }
    } headerGuard{headerList};
    for (const auto& header : request.headers) {
        const std::string line = header.first + ": " + header.second;
        curl_slist* appended = curl_slist_append(headerList, line.c_str());
        if (appended == nullptr) {
            return failedResponse("production HTTP transport could not allocate headers");
        }
        headerList = appended;
    }

    CurlResponseContext context;
    context.max_bytes = maxBytes;
    context.response.transport_status = HttpTransportStatus::Completed;
    const std::uint64_t totalMs = totalTimeout(request);
    const std::uint64_t connectMs = connectTimeout(request);
    const auto setopt = [raw](CURLoption option, auto value) {
        return curl_easy_setopt(raw, option, value) == CURLE_OK;
    };
    if (!setopt(CURLOPT_URL, request.url.c_str()) ||
        !setopt(CURLOPT_POST, 1L) ||
        !setopt(CURLOPT_HTTPHEADER, headerList) ||
        !setopt(CURLOPT_POSTFIELDS, request.body.empty() ? nullptr : request.body.data()) ||
        !setopt(CURLOPT_POSTFIELDSIZE_LARGE,
                static_cast<curl_off_t>(request.body.size())) ||
        !setopt(CURLOPT_CONNECTTIMEOUT_MS, boundedCurlTimeout(connectMs)) ||
        !setopt(CURLOPT_TIMEOUT_MS, boundedCurlTimeout(totalMs)) ||
        !setopt(CURLOPT_SSL_VERIFYPEER, 1L) ||
        !setopt(CURLOPT_SSL_VERIFYHOST, 2L) ||
        !setopt(CURLOPT_FOLLOWLOCATION, 0L) ||
        !setopt(CURLOPT_NOSIGNAL, 1L) ||
        !setopt(CURLOPT_PROTOCOLS, CURLPROTO_HTTPS) ||
        !setopt(CURLOPT_WRITEFUNCTION, curlWrite) ||
        !setopt(CURLOPT_WRITEDATA, &context) ||
        !setopt(CURLOPT_HEADERFUNCTION, curlHeader) ||
        !setopt(CURLOPT_HEADERDATA, &context)) {
        return failedResponse("production HTTP transport could not configure request");
    }
    const CURLcode code = curl_easy_perform(raw);
    if (code == CURLE_OPERATION_TIMEDOUT) {
        return timeoutResponse();
    }
    if (code != CURLE_OK) {
        if (context.response_too_large) {
            return failedResponse("production HTTP transport response exceeds safety limit");
        }
        if (context.allocation_failed) {
            return failedResponse("production HTTP transport could not store response");
        }
        return failedResponse("production HTTP transport request failed");
    }
    long status = 0;
    if (curl_easy_getinfo(raw, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
        status <= 0 || status > INT_MAX) {
        return failedResponse("production HTTP transport returned no HTTP status");
    }
    context.response.status_code = static_cast<int>(status);
    return context.response;
}

#endif

} // namespace

ProductionHttpTransport::ProductionHttpTransport(std::size_t maxResponseBytes) noexcept
    : max_response_bytes_(maxResponseBytes) {}

bool ProductionHttpTransport::isValidHttpsPostRequest(const HttpRequest& request) noexcept {
    if (request.method != "POST" || !isHttpsUrl(request.url) ||
        !isOfficialTokenHubUrl(request.url)) {
        return false;
    }
    for (const auto& header : request.headers) {
        if (!safeRequestHeader(header)) {
            return false;
        }
    }
    return true;
}

HttpResponse ProductionHttpTransport::perform(const HttpRequest& request) {
    try {
        if (!isValidHttpsPostRequest(request)) {
            return failedResponse("production HTTP transport rejected unsafe request");
        }
        if (max_response_bytes_ == 0) {
            return failedResponse("production HTTP transport has zero response limit");
        }
#ifdef _WIN32
        return performWindows(request, max_response_bytes_);
#else
        return performCurl(request, max_response_bytes_);
#endif
    } catch (...) {
        return failedResponse("production HTTP transport internal failure");
    }
}

} // namespace hy3
