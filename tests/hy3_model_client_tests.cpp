// hy3-algotrace — TokenHub Hy3 adapter tests (Phase 2C)
//
// FakeHttpTransport is the only transport used here. These tests have no
// network side effects and cannot invoke a paid model API.

#include "hy3_algotrace/hy3_model_client.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace hy3;

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (condition) {                                                      \
            ++g_pass;                                                         \
        } else {                                                              \
            ++g_fail;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (message) << "\n"; \
        }                                                                     \
    } while (0)

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

std::string text(const std::vector<std::uint8_t>& value) {
    return {value.begin(), value.end()};
}

std::optional<std::string> header(const HttpRequest& request,
                                  const std::string& name) {
    for (const auto& entry : request.headers) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return std::nullopt;
}

HttpResponse completed(int status, const nlohmann::json& body) {
    HttpResponse response;
    response.transport_status = HttpTransportStatus::Completed;
    response.status_code = status;
    response.body = bytes(body.dump());
    return response;
}

class FakeHttpTransport final : public IHttpTransport {
public:
    explicit FakeHttpTransport(HttpResponse scripted)
        : scripted_(std::move(scripted)) {}

    HttpResponse perform(const HttpRequest& request) override {
        ++call_count;
        last_request = request;
        return scripted_;
    }

    HttpResponse scripted_;
    int call_count = 0;
    std::optional<HttpRequest> last_request;
};

Hy3ModelClientConfig explicitConfig() {
    Hy3ModelClientConfig config;
    config.api_key = "synthetic-secret-for-test";
    config.api_key_env = "HY3_ALGOTRACE_TEST_UNUSED_ENV";
    config.connect_timeout_ms = 1000;
    config.read_timeout_ms = 2000;
    config.total_timeout_ms = 3000;
    return config;
}

ModelRequest modelRequest() {
    return {"trace-1", "Return this diagnostic as JSON.\r\n", "synthetic-sha"};
}

} // namespace

int main() {
    // Default endpoint, authentication, non-streaming mode, and prompt body.
    {
        const std::string modelText = "{\"status\":\"correct\"}\r\n";
        FakeHttpTransport transport(completed(200, {
            {"choices", nlohmann::json::array({
                {{"message", {{"role", "assistant"}, {"content", modelText}}}}
            })}
        }));
        Hy3ModelClient client(transport, explicitConfig());

        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::Succeeded,
              "valid TokenHub envelope succeeds");
        CHECK(text(result.raw_response) == modelText,
              "message content bytes are returned without normalization");
        CHECK(result.provider == "tencent-hunyuan", "provider is canonical");
        CHECK(result.model_name == "hy3", "default model is hy3");
        CHECK(transport.call_count == 1, "transport called exactly once");
        CHECK(transport.last_request.has_value(), "request was recorded");
        if (transport.last_request) {
            const HttpRequest& request = *transport.last_request;
            CHECK(request.method == "POST", "request method is POST");
            CHECK(request.url ==
                      "https://tokenhub.tencentmaas.com/v1/chat/completions",
                  "default TokenHub Chat endpoint is used");
            CHECK(header(request, "Authorization") ==
                      "Bearer synthetic-secret-for-test",
                  "Bearer authentication is sent to injected transport");
            CHECK(header(request, "Content-Type") == "application/json",
                  "JSON content type is sent");
            CHECK(request.connect_timeout_ms == 1000 &&
                      request.read_timeout_ms == 2000 &&
                      request.total_timeout_ms == 3000,
                  "configured timeouts are passed to the transport");
            const nlohmann::json body = nlohmann::json::parse(request.body);
            CHECK(body.at("model") == "hy3", "request uses formal Hy3 model id");
            CHECK(body.at("stream") == false, "stream is explicitly disabled");
            CHECK(body.at("response_format").at("type") == "json_object",
                  "request asks for JSON object mode without guessing schema");
            CHECK(body.at("messages").size() == 1,
                  "request sends one rendered prompt message");
            CHECK(body.at("messages").at(0).at("role") == "user",
                  "rendered prompt is a user message");
            CHECK(body.at("messages").at(0).at("content") ==
                      modelRequest().normalized_prompt,
                  "prompt bytes reach JSON body unchanged");
        }
    }

    // Empty string content is still a successful call. Importer classifies it.
    {
        FakeHttpTransport transport(completed(200, {
            {"choices", nlohmann::json::array({{{"message", {{"content", ""}}}}})}
        }));
        Hy3ModelClient client(transport, explicitConfig());
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::Succeeded,
              "empty model content remains a successful call");
        CHECK(result.raw_response.empty(), "empty model content stays empty");
    }

    // A successful HTTP response without string content is a provider failure,
    // not an empty prediction.
    {
        FakeHttpTransport transport(completed(200, {
            {"choices", nlohmann::json::array({{{"message", {{"content", nullptr}}}}})}
        }));
        Hy3ModelClient client(transport, explicitConfig());
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::ProviderError,
              "null content is a malformed provider envelope");
        CHECK(result.error_code == "E_HY3_RESPONSE_SCHEMA",
              "malformed success envelope has stable error code");
        CHECK(result.raw_response.empty(), "provider failure has no raw response");
    }

    // Official TokenHub mappings. String and integer error.code values must be
    // accepted, and only a constrained request_id enters the diagnostic.
    struct HttpCase {
        int http_status;
        ModelCallStatus expected_status;
        nlohmann::json code;
        std::string expected_code;
    };
    const HttpCase cases[] = {
        {401, ModelCallStatus::AuthenticationError, "401002", "401002"},
        {429, ModelCallStatus::RateLimited, 429001, "429001"},
        {499, ModelCallStatus::Cancelled, "499001", "499001"},
        {500, ModelCallStatus::ProviderError, "500001", "500001"},
        {502, ModelCallStatus::ProviderError, "502001", "502001"},
        {503, ModelCallStatus::ProviderError, "503001", "503001"},
        {504, ModelCallStatus::Timeout, "504001", "504001"},
    };
    for (const HttpCase& test : cases) {
        FakeHttpTransport transport(completed(test.http_status, {
            {"error", {
                {"code", test.code},
                {"message", "must not be copied; synthetic-secret-for-test"},
                {"request_id", "req-safe_123"}
            }}
        }));
        Hy3ModelClient client(transport, explicitConfig());
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == test.expected_status,
              "HTTP status maps to ModelCallStatus");
        CHECK(result.error_code == test.expected_code,
              "provider code accepts string or integer form");
        CHECK(result.message.find("req-safe_123") != std::string::npos,
              "safe request id is retained for support");
        CHECK(result.message.find("synthetic-secret-for-test") == std::string::npos,
              "provider message cannot leak a credential");
        CHECK(result.raw_response.empty(), "HTTP failure has no model content");
    }

    // Only the documented six-digit business-code shape is surfaced. An
    // untrusted error envelope cannot move arbitrary text into diagnostics.
    {
        FakeHttpTransport transport(completed(401, {
            {"error", {
                {"code", "synthetic-secret-for-test"},
                {"request_id", "request id with unsafe whitespace"}
            }}
        }));
        Hy3ModelClient client(transport, explicitConfig());
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.error_code == "E_HY3_HTTP_401",
              "unsafe provider error code falls back to adapter code");
        CHECK(result.message.find("synthetic-secret-for-test") == std::string::npos,
              "unsafe provider code cannot leak arbitrary text");
        CHECK(result.message.find("request id") == std::string::npos,
              "unsafe request id is omitted");
    }

    // Transport timeout is distinct from generic transport failure, and raw
    // transport diagnostics are never copied to the public result.
    {
        HttpResponse response;
        response.transport_status = HttpTransportStatus::Timeout;
        response.transport_message =
            "Authorization: Bearer synthetic-secret-for-test";
        FakeHttpTransport transport(response);
        Hy3ModelClient client(transport, explicitConfig());
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::Timeout,
              "transport timeout maps to Timeout");
        CHECK(result.error_code == "E_HY3_TRANSPORT_TIMEOUT",
              "transport timeout has stable adapter code");
        CHECK(result.message.find("synthetic-secret-for-test") == std::string::npos,
              "transport diagnostic cannot leak a credential");
    }

    // No explicit key and a deliberately nonexistent environment variable
    // fail before the transport can observe any request.
    {
        HttpResponse response;
        response.transport_status = HttpTransportStatus::Completed;
        response.status_code = 200;
        FakeHttpTransport transport(response);
        Hy3ModelClientConfig config;
        config.api_key_env =
            "HY3_ALGOTRACE_TEST_MISSING_TOKENHUB_KEY_8C15467C";
        Hy3ModelClient client(transport, config);
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::ConfigurationError,
              "missing key is a configuration failure");
        CHECK(result.error_code == "E_HY3_API_KEY_MISSING",
              "missing key has stable adapter code");
        CHECK(transport.call_count == 0, "missing key never calls transport");
        CHECK(result.message.find(config.api_key_env) == std::string::npos,
              "missing-key diagnostic does not expose environment details");
    }

    // Validate the destination before resolving or transmitting credentials.
    for (const std::string unsafeBase : {
             std::string("http://tokenhub.tencentmaas.com/v1"),
             std::string("https://example.invalid/v1")}) {
        HttpResponse response;
        response.transport_status = HttpTransportStatus::Completed;
        FakeHttpTransport transport(response);
        Hy3ModelClientConfig config = explicitConfig();
        config.base_url = unsafeBase;
        Hy3ModelClient client(transport, config);
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::ConfigurationError,
              "unsafe or unapproved base URL is rejected");
        CHECK(transport.call_count == 0,
              "rejected base URL never reaches transport");
    }

    // Environment-based key loading is supported without storing the key in
    // adapter diagnostics. Use a process-local synthetic variable, never the
    // user's real TOKENHUB_API_KEY.
    {
        const char* variable = "HY3_ALGOTRACE_TEST_TOKENHUB_KEY_8C15467C";
#ifdef _WIN32
        _putenv_s(variable, "synthetic-env-key");
#else
        setenv(variable, "synthetic-env-key", 1);
#endif
        FakeHttpTransport transport(completed(200, {
            {"choices", nlohmann::json::array({{{"message", {{"content", "{}"}}}}})}
        }));
        Hy3ModelClientConfig config;
        config.api_key_env = variable;
        Hy3ModelClient client(transport, config);
        const ModelCallResult result = client.invoke(modelRequest());
        CHECK(result.status == ModelCallStatus::Succeeded,
              "API key can be loaded from configured environment variable");
        CHECK(transport.last_request.has_value(),
              "environment-key request reaches injected transport");
        if (transport.last_request) {
            CHECK(header(*transport.last_request, "Authorization") ==
                      "Bearer synthetic-env-key",
                  "environment key is used in Bearer header");
        }
#ifdef _WIN32
        _putenv_s(variable, "");
#else
        unsetenv(variable);
#endif
    }

    std::cout << "hy3_model_client_tests: " << g_pass << " passed, "
              << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
