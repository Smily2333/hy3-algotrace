// hy3-algotrace — Tencent TokenHub Hy3 adapter (Phase 2C)
//
// This adapter only constructs and interprets OpenAI-compatible HTTP messages.
// A production network implementation is deliberately out of scope: callers
// must inject an IHttpTransport. Prediction JSON parsing remains the
// responsibility of PredictionImporter.

#pragma once

#include "hy3_algotrace/model_client.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hy3 {

struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::uint8_t> body;
    // Zero means the injected transport's explicitly documented default.
    // The adapter does not invent provider timeout values absent from the
    // official API documentation.
    std::uint64_t connect_timeout_ms = 0;
    std::uint64_t read_timeout_ms = 0;
    std::uint64_t total_timeout_ms = 0;
};

enum class HttpTransportStatus {
    Completed,
    Timeout,
    Failed,
    Cancelled
};

struct HttpResponse {
    HttpTransportStatus transport_status = HttpTransportStatus::Failed;
    int status_code = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::uint8_t> body;

    // Adapter implementations must not copy this field into ModelCallResult:
    // transport diagnostics can accidentally contain request headers.
    std::string transport_message;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse perform(const HttpRequest& request) = 0;
};

struct Hy3ModelClientConfig {
    std::string base_url = "https://tokenhub.tencentmaas.com/v1";
    std::string model = "hy3";
    // Opt-in for bounded evaluation; unset preserves historical requests.
    std::optional<std::uint64_t> max_tokens;

    // Explicit configuration takes precedence. If empty, the adapter reads
    // api_key_env when invoke() starts. Neither value is ever included in an
    // adapter diagnostic.
    std::string api_key;
    std::string api_key_env = "TOKENHUB_API_KEY";

    // Credentials are pinned to the official TokenHub origin by default.
    // A caller may explicitly opt into another HTTPS-compatible gateway, but
    // non-HTTPS URLs are always rejected by this cloud adapter.
    bool allow_custom_base_url = false;

    // Passed through to IHttpTransport. TokenHub publishes no recommended
    // client-side values, so the project does not guess defaults here.
    std::uint64_t connect_timeout_ms = 0;
    std::uint64_t read_timeout_ms = 0;
    std::uint64_t total_timeout_ms = 0;
};

class Hy3ModelClient final : public IModelClient {
public:
    explicit Hy3ModelClient(IHttpTransport& transport,
                            Hy3ModelClientConfig config = {});

    ModelCallResult invoke(const ModelRequest& request) noexcept override;

private:
    IHttpTransport& transport_;
    Hy3ModelClientConfig config_;
};

} // namespace hy3
