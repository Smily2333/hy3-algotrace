#include "hy3_algotrace/interactive_server.hpp"

#include "hy3_algotrace/sha256.hpp"

#include <cpp-httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace hy3 {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr std::size_t kApplicationBodyLimit = 256U * 1024U;
constexpr std::size_t kStaticAssetLimit = 2U * 1024U * 1024U;

json errorDocument(const std::string& outcome,
                   const std::string& kind,
                   const std::string& code,
                   const std::string& message) {
    return json{{"ok", false},
                {"outcome", outcome},
                {"parse_status", "not_attempted"},
                {"error", {{"kind", kind}, {"code", code}, {"message", message}}}};
}

InteractiveHttpReply jsonReply(int status, const json& document) {
    InteractiveHttpReply reply;
    reply.status = status;
    reply.body = document.dump();
    return reply;
}

bool contentTypeIsJson(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(ch >= 'A' && ch <= 'Z'
                                                    ? ch - 'A' + 'a'
                                                    : ch);
                   });
    const std::size_t semicolon = lowered.find(';');
    const std::string mediaType = lowered.substr(0, semicolon);
    return mediaType == "application/json";
}

int statusFor(const InteractiveDiagnosisResult& result) {
    if (result.ok) return 200;
    if (result.error_code == interactive_errc::E_REQUEST_SCHEMA ||
        result.error_code == interactive_errc::E_REQUEST_INVALID) {
        return 400;
    }
    if (result.error_code == interactive_errc::E_REQUEST_EXISTS) return 409;
    if (result.error_code == interactive_errc::E_MODEL_RATE_LIMITED) return 429;
    if (result.error_code == interactive_errc::E_MODEL_TIMEOUT) return 504;
    if (result.error_code == interactive_errc::E_ARTIFACTS_ROOT ||
        result.error_code == interactive_errc::E_ARTIFACT_WRITE ||
        result.error_code == interactive_errc::E_TEMPLATE_INVALID) {
        return 500;
    }
    return 502;
}

bool readStaticAsset(const fs::path& path, std::string& bytes) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec ||
        fs::file_size(path, ec) > kStaticAssetLimit || ec) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return !input.bad();
}

bool validHostHeader(const std::string& host, std::uint16_t port) {
    const std::string suffix = ":" + std::to_string(port);
    return host == "127.0.0.1" || host == "127.0.0.1" + suffix ||
           host == "localhost" || host == "localhost" + suffix;
}

void setReply(httplib::Response& response, const InteractiveHttpReply& reply) {
    response.status = reply.status;
    response.set_content(reply.body, reply.content_type);
}

void setStatic(httplib::Response& response, const std::string& body,
               const char* contentType) {
    response.status = 200;
    response.set_content(body, contentType);
}

} // namespace

InteractiveHttpApplication::InteractiveHttpApplication(
    IModelClient& client, std::string promptTemplateText,
    std::string artifactsRoot, bool tokenHubConfigured)
    : client_(client),
      prompt_template_text_(std::move(promptTemplateText)),
      artifacts_root_(std::move(artifactsRoot)),
      token_hub_configured_(tokenHubConfigured) {}

InteractiveHttpReply InteractiveHttpApplication::health() const {
    std::vector<std::uint8_t> input(prompt_template_text_.begin(),
                                    prompt_template_text_.end());
    std::vector<std::uint8_t> normalized;
    std::string normalizationError;
    const bool templateValid = normalizeUtf8(input, normalized, normalizationError);
    const std::string normalizedText(normalized.begin(), normalized.end());
    const json document{
        {"ok", true},
        {"service", "hy3-algotrace-interactive-demo"},
        {"model_name", "hy3"},
        {"algorithm_scope", "greedy"},
        {"tokenhub_status", token_hub_configured_ ? "configured" : "not_configured"},
        {"prompt_template_id", "hy3-interactive-diagnosis-v1"},
        {"prompt_template_sha256",
         templateValid ? json(sha256_hex(normalizedText)) : json(nullptr)},
        {"code_execution", false},
        {"online_judge", false},
    };
    return jsonReply(200, document);
}

InteractiveHttpReply InteractiveHttpApplication::diagnose(
    const std::string& contentType, const std::string& body) {
    if (!contentTypeIsJson(contentType)) {
        return jsonReply(415, errorDocument(
            "invalid_request", "validation_error", "E_HTTP_CONTENT_TYPE",
            "Content-Type must be application/json"));
    }
    if (body.empty() || body.size() > kApplicationBodyLimit) {
        return jsonReply(body.empty() ? 400 : 413, errorDocument(
            "invalid_request", "validation_error", "E_HTTP_BODY_SIZE",
            body.empty() ? "request body is empty" : "request body is too large"));
    }

    const json input = json::parse(body, nullptr, false, false);
    if (input.is_discarded()) {
        return jsonReply(400, errorDocument(
            "invalid_request", "validation_error", "E_HTTP_JSON_PARSE",
            "request body is not valid JSON"));
    }

    InteractiveDiagnosisRequest request;
    const InteractiveRequestValidation validation =
        parseInteractiveDiagnosisRequest(input, request);
    if (!validation.ok) {
        json document = errorDocument(
            "invalid_request", "validation_error", validation.error_code,
            "interactive request validation failed");
        document["error"]["details"] = validation.errors;
        return jsonReply(400, document);
    }

    if (call_active_.test_and_set()) {
        return jsonReply(429, errorDocument(
            "model_call_failed", "busy", "E_INTERACTIVE_BUSY",
            "another diagnosis is currently running"));
    }
    struct ClearFlag final {
        std::atomic_flag& flag;
        ~ClearFlag() { flag.clear(); }
    } clear{call_active_};

    const InteractiveDiagnosisResult result = runInteractiveDiagnosis(
        request, prompt_template_text_, artifacts_root_, client_);
    return jsonReply(statusFor(result), interactiveDiagnosisBrowserJson(result));
}

bool serveInteractiveDemo(const InteractiveServerConfig& config,
                          InteractiveHttpApplication& application,
                          std::string& safeError) {
    safeError.clear();
    if (config.host != "127.0.0.1") {
        safeError = "only the 127.0.0.1 loopback host is permitted";
        return false;
    }
    if (config.port == 0 || config.request_body_limit_bytes == 0 ||
        config.request_body_limit_bytes > kApplicationBodyLimit) {
        safeError = "invalid server port or request body limit";
        return false;
    }

    std::string html;
    std::string css;
    std::string javascript;
    if (!readStaticAsset(config.web_root / "index.html", html) ||
        !readStaticAsset(config.web_root / "styles.css", css) ||
        !readStaticAsset(config.web_root / "app.js", javascript)) {
        safeError = "web root is missing a required static asset";
        return false;
    }

    httplib::Server server;
    server.set_payload_max_length(config.request_body_limit_bytes);
    server.set_read_timeout(15, 0);
    server.set_write_timeout(15, 0);
    server.set_idle_interval(0, 100000);
    server.set_default_headers({
        {"Cache-Control", "no-store"},
        {"Content-Security-Policy",
         "default-src 'self'; script-src 'self'; style-src 'self'; "
         "connect-src 'self'; img-src 'self' data:; object-src 'none'; "
         "base-uri 'none'; frame-ancestors 'none'"},
        {"Permissions-Policy", "camera=(), microphone=(), geolocation=()"},
        {"Referrer-Policy", "no-referrer"},
        {"X-Content-Type-Options", "nosniff"},
        {"X-Frame-Options", "DENY"},
    });

    const auto hostAllowed = [&config](const httplib::Request& request) {
        return validHostHeader(request.get_header_value("Host"), config.port);
    };
    const auto rejectHost = [](httplib::Response& response) {
        setReply(response, jsonReply(403, errorDocument(
            "invalid_request", "security_error", "E_HTTP_HOST",
            "request host is not permitted")));
    };

    server.Get("/", [&, html](const httplib::Request& request,
                              httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        setStatic(response, html, "text/html; charset=utf-8");
    });
    server.Get("/styles.css", [&, css](const httplib::Request& request,
                                       httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        setStatic(response, css, "text/css; charset=utf-8");
    });
    server.Get("/app.js", [&, javascript](const httplib::Request& request,
                                          httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        setStatic(response, javascript, "application/javascript; charset=utf-8");
    });
    server.Get("/favicon.ico", [&](const httplib::Request& request,
                                    httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        response.status = 204;
    });
    server.Get("/api/health", [&](const httplib::Request& request,
                                   httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        setReply(response, application.health());
    });
    server.Post("/api/diagnose", [&](const httplib::Request& request,
                                      httplib::Response& response) {
        if (!hostAllowed(request)) return rejectHost(response);
        setReply(response, application.diagnose(
                               request.get_header_value("Content-Type"),
                               request.body));
    });
    server.set_exception_handler([](const httplib::Request&,
                                    httplib::Response& response,
                                    std::exception_ptr) {
        setReply(response, jsonReply(500, errorDocument(
            "server_error", "server_error", "E_HTTP_INTERNAL",
            "local server encountered an internal error")));
    });

    if (!server.listen(config.host, static_cast<int>(config.port))) {
        safeError = "could not bind the local demo server";
        return false;
    }
    return true;
}

} // namespace hy3
