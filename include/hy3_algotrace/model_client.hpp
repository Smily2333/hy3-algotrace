// hy3-algotrace — transport-neutral model client (Phase 2C)
//
// A ModelClient transports one already-rendered prompt to a model endpoint (or
// a deterministic fake). It does not parse diagnostic JSON, read gold labels,
// or write run artifacts. Those responsibilities remain in
// PredictionImporter and Reporter.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hy3 {

struct ModelRequest {
    std::string trace_id;
    std::string normalized_prompt;
    std::string prompt_sha256;
};

enum class ModelCallStatus {
    Succeeded,
    ConfigurationError,
    AuthenticationError,
    RateLimited,
    Timeout,
    TransportError,
    ProviderError,
    Cancelled
};

struct ModelTokenUsage {
    std::optional<std::uint64_t> prompt_tokens;
    std::optional<std::uint64_t> completion_tokens;
    std::optional<std::uint64_t> total_tokens;
    // Subsets of prompt/completion, never added again to the total.
    std::optional<std::uint64_t> cached_tokens;
    std::optional<std::uint64_t> reasoning_tokens;
};

// Stable lower-case spelling for diagnostics, tests, and future audit records.
const char* modelCallStatusName(ModelCallStatus status) noexcept;

struct ModelCallResult {
    ModelCallStatus status = ModelCallStatus::ConfigurationError;

    // Exact model-content bytes. They are never normalized or repaired. This
    // field may be empty for a successful empty response, but must be empty for
    // every failure status.
    std::vector<std::uint8_t> raw_response;

    std::string provider;
    std::string model_name;
    std::optional<std::string> model_version;

    // Safe provider metadata retained when the response exposes it. Missing
    // values remain null; adapters must never invent them.
    std::optional<int> http_status;
    std::optional<std::string> request_id;
    std::optional<ModelTokenUsage> token_usage;
    std::optional<std::string> finish_reason; // Allowlisted provider enum, not arbitrary text.

    // UTC wall-clock timestamps describe when the call started and finished.
    // duration_ms is measured independently with a monotonic clock by a real
    // adapter; it must not be derived by subtracting wall-clock strings.
    std::string started_at;
    std::string finished_at;
    std::uint64_t duration_ms = 0;

    // Stable adapter error code plus human-readable, credential-free detail.
    // Both are normally empty on success.
    std::string error_code;
    std::string message;
};

class IModelClient {
public:
    virtual ~IModelClient() = default;
    virtual ModelCallResult invoke(const ModelRequest& request) noexcept = 0;
};

// Deterministic, zero-cost client for unit/integration tests. It returns the
// configured result verbatim and records the most recent request. Test data
// must use an explicit synthetic provider/model identity; the fake never
// claims to be a real Hy3 call on its own.
class FakeModelClient final : public IModelClient {
public:
    explicit FakeModelClient(ModelCallResult scriptedResult);

    ModelCallResult invoke(const ModelRequest& request) noexcept override;

    std::size_t callCount() const noexcept;
    const std::optional<ModelRequest>& lastRequest() const noexcept;

private:
    ModelCallResult scripted_result_;
    std::size_t call_count_ = 0;
    std::optional<ModelRequest> last_request_;
};

} // namespace hy3
