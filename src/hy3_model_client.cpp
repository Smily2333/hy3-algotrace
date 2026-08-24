#include "hy3_algotrace/hy3_model_client.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace hy3 {
namespace {

using json = nlohmann::json;
using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

std::string utcTimestamp(SystemClock::time_point point) {
    const std::time_t value = SystemClock::to_time_t(point);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string endpointFor(std::string baseUrl) {
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    return baseUrl + "/chat/completions";
}

std::string normalizedBaseUrl(std::string baseUrl) {
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    return baseUrl;
}

std::string resolveApiKey(const Hy3ModelClientConfig& config) {
    if (!config.api_key.empty()) {
        return config.api_key;
    }
    if (config.api_key_env.empty()) {
        return {};
    }
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, config.api_key_env.c_str()) != 0 ||
        value == nullptr) {
        return {};
    }
    const std::string resolved(value);
    std::free(value);
    return resolved;
#else
    const char* value = std::getenv(config.api_key_env.c_str());
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::string safeRequestId(const json& document) {
    const json* value = nullptr;
    if (document.is_object() && document.contains("request_id")) {
        value = &document.at("request_id");
    } else if (document.is_object() && document.contains("error") &&
               document.at("error").is_object() &&
               document.at("error").contains("request_id")) {
        value = &document.at("error").at("request_id");
    }
    if (value == nullptr || !value->is_string()) {
        return {};
    }

    const std::string candidate = value->get<std::string>();
    if (candidate.empty() || candidate.size() > 128) {
        return {};
    }
    for (const unsigned char ch : candidate) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') ||
                             ch == '_' || ch == '-' || ch == '.';
        if (!allowed) {
            return {};
        }
    }
    return candidate;
}

std::string providerErrorCode(const json& document, int httpStatus) {
    if (document.is_object() && document.contains("error") &&
        document.at("error").is_object() &&
        document.at("error").contains("code")) {
        const json& code = document.at("error").at("code");
        if (code.is_string()) {
            const std::string candidate = code.get<std::string>();
            bool sixDigits = candidate.size() == 6;
            for (const unsigned char ch : candidate) {
                sixDigits = sixDigits && ch >= '0' && ch <= '9';
            }
            if (sixDigits) {
                return candidate;
            }
        }
        if (code.is_number_unsigned()) {
            const std::uint64_t candidate = code.get<std::uint64_t>();
            if (candidate >= 100000 && candidate <= 999999) {
                return std::to_string(candidate);
            }
        }
        if (code.is_number_integer()) {
            const std::int64_t candidate = code.get<std::int64_t>();
            if (candidate >= 100000 && candidate <= 999999) {
                return std::to_string(candidate);
            }
        }
    }
    return "E_HY3_HTTP_" + std::to_string(httpStatus);
}

ModelCallStatus statusForHttp(int status) {
    if (status == 401) {
        return ModelCallStatus::AuthenticationError;
    }
    if (status == 429) {
        return ModelCallStatus::RateLimited;
    }
    if (status == 499) {
        return ModelCallStatus::Cancelled;
    }
    if (status == 504) {
        return ModelCallStatus::Timeout;
    }
    return ModelCallStatus::ProviderError;
}

void finishTiming(ModelCallResult& result,
                  SystemClock::time_point wallStart,
                  SteadyClock::time_point steadyStart) {
    result.started_at = utcTimestamp(wallStart);
    result.finished_at = utcTimestamp(SystemClock::now());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        SteadyClock::now() - steadyStart);
    result.duration_ms = elapsed.count() < 0
                             ? 0
                             : static_cast<std::uint64_t>(elapsed.count());
}

ModelCallResult baseResult(const Hy3ModelClientConfig& config) {
    ModelCallResult result;
    // Keep the frozen evaluation protocol's canonical provider identity;
    // TokenHub is the transport/service surface, not a different model owner.
    result.provider = "tencent-hunyuan";
    result.model_name = config.model;
    return result;
}

void setSafeHttpFailure(ModelCallResult& result,
                        int status,
                        const std::vector<std::uint8_t>& body) {
    result.status = statusForHttp(status);

    json document;
    try {
        document = json::parse(body.begin(), body.end());
    } catch (...) {
        document = json();
    }
    result.error_code = providerErrorCode(document, status);
    result.message = "TokenHub request failed with HTTP " +
                     std::to_string(status);
    const std::string requestId = safeRequestId(document);
    if (!requestId.empty()) {
        result.message += " (request_id=" + requestId + ")";
    }
}

} // namespace

Hy3ModelClient::Hy3ModelClient(IHttpTransport& transport,
                               Hy3ModelClientConfig config)
    : transport_(transport), config_(std::move(config)) {}

ModelCallResult Hy3ModelClient::invoke(const ModelRequest& request) noexcept {
    const SystemClock::time_point wallStart = SystemClock::now();
    const SteadyClock::time_point steadyStart = SteadyClock::now();
    ModelCallResult result = baseResult(config_);

    try {
        if (config_.base_url.empty()) {
            result.status = ModelCallStatus::ConfigurationError;
            result.error_code = "E_HY3_BASE_URL_MISSING";
            result.message = "TokenHub base URL is not configured";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }
        if (config_.model.empty()) {
            result.status = ModelCallStatus::ConfigurationError;
            result.error_code = "E_HY3_MODEL_MISSING";
            result.message = "TokenHub model is not configured";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        const std::string baseUrl = normalizedBaseUrl(config_.base_url);
        if (baseUrl.rfind("https://", 0) != 0) {
            result.status = ModelCallStatus::ConfigurationError;
            result.error_code = "E_HY3_HTTPS_REQUIRED";
            result.message = "TokenHub base URL must use HTTPS";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }
        constexpr const char* officialBase =
            "https://tokenhub.tencentmaas.com/v1";
        if (!config_.allow_custom_base_url && baseUrl != officialBase) {
            result.status = ModelCallStatus::ConfigurationError;
            result.error_code = "E_HY3_BASE_URL_NOT_ALLOWED";
            result.message = "TokenHub base URL is not the pinned official origin";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        const std::string apiKey = resolveApiKey(config_);
        if (apiKey.empty()) {
            result.status = ModelCallStatus::ConfigurationError;
            result.error_code = "E_HY3_API_KEY_MISSING";
            result.message = "TokenHub API key is not configured";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        // The frozen diagnosis schema is not owned by this adapter. Until it
        // is explicitly injected, request only OpenAI JSON object mode rather
        // than guessing a JSON Schema structure here.
        json body;
        body["model"] = config_.model;
        body["messages"] = json::array({
            {{"role", "user"}, {"content", request.normalized_prompt}}
        });
        body["stream"] = false;
        body["response_format"] = {{"type", "json_object"}};
        const std::string bodyText = body.dump();

        HttpRequest httpRequest;
        httpRequest.method = "POST";
        httpRequest.url = endpointFor(baseUrl);
        httpRequest.headers = {
            {"Authorization", "Bearer " + apiKey},
            {"Content-Type", "application/json"}
        };
        httpRequest.body.assign(bodyText.begin(), bodyText.end());
        httpRequest.connect_timeout_ms = config_.connect_timeout_ms;
        httpRequest.read_timeout_ms = config_.read_timeout_ms;
        httpRequest.total_timeout_ms = config_.total_timeout_ms;

        HttpResponse response;
        try {
            response = transport_.perform(httpRequest);
        } catch (...) {
            result.status = ModelCallStatus::TransportError;
            result.error_code = "E_HY3_TRANSPORT_EXCEPTION";
            result.message = "TokenHub transport raised an exception";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }
        switch (response.transport_status) {
            case HttpTransportStatus::Timeout:
                result.status = ModelCallStatus::Timeout;
                result.error_code = "E_HY3_TRANSPORT_TIMEOUT";
                result.message = "TokenHub transport timed out";
                finishTiming(result, wallStart, steadyStart);
                return result;
            case HttpTransportStatus::Failed:
                result.status = ModelCallStatus::TransportError;
                result.error_code = "E_HY3_TRANSPORT";
                result.message = "TokenHub transport failed";
                finishTiming(result, wallStart, steadyStart);
                return result;
            case HttpTransportStatus::Cancelled:
                result.status = ModelCallStatus::Cancelled;
                result.error_code = "E_HY3_TRANSPORT_CANCELLED";
                result.message = "TokenHub transport was cancelled";
                finishTiming(result, wallStart, steadyStart);
                return result;
            case HttpTransportStatus::Completed:
                break;
        }

        if (response.status_code < 200 || response.status_code >= 300) {
            if (response.status_code <= 0) {
                result.status = ModelCallStatus::TransportError;
                result.error_code = "E_HY3_TRANSPORT_STATUS";
                result.message = "TokenHub transport returned no HTTP status";
                finishTiming(result, wallStart, steadyStart);
                return result;
            }
            setSafeHttpFailure(result, response.status_code, response.body);
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        json responseDocument;
        try {
            responseDocument = json::parse(response.body.begin(), response.body.end());
        } catch (...) {
            result.status = ModelCallStatus::ProviderError;
            result.error_code = "E_HY3_RESPONSE_INVALID_JSON";
            result.message = "TokenHub returned an invalid JSON response envelope";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        const bool validEnvelope = responseDocument.is_object() &&
            responseDocument.contains("choices") &&
            responseDocument.at("choices").is_array() &&
            !responseDocument.at("choices").empty() &&
            responseDocument.at("choices").at(0).is_object() &&
            responseDocument.at("choices").at(0).contains("message") &&
            responseDocument.at("choices").at(0).at("message").is_object() &&
            responseDocument.at("choices").at(0).at("message").contains("content") &&
            responseDocument.at("choices").at(0).at("message").at("content").is_string();
        if (!validEnvelope) {
            result.status = ModelCallStatus::ProviderError;
            result.error_code = "E_HY3_RESPONSE_SCHEMA";
            result.message = "TokenHub response envelope has no string message content";
            finishTiming(result, wallStart, steadyStart);
            return result;
        }

        const std::string& content = responseDocument.at("choices").at(0)
            .at("message").at("content").get_ref<const std::string&>();
        result.raw_response.assign(content.begin(), content.end());
        result.status = ModelCallStatus::Succeeded;
        finishTiming(result, wallStart, steadyStart);
        return result;
    } catch (...) {
        result.raw_response.clear();
        result.status = ModelCallStatus::ProviderError;
        result.error_code = "E_HY3_ADAPTER_INTERNAL";
        result.message = "Hy3 adapter could not construct or interpret the request";
        finishTiming(result, wallStart, steadyStart);
        return result;
    }
}

} // namespace hy3
