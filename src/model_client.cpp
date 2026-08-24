#include "hy3_algotrace/model_client.hpp"

#include <utility>

namespace hy3 {

const char* modelCallStatusName(ModelCallStatus status) noexcept {
    switch (status) {
        case ModelCallStatus::Succeeded: return "succeeded";
        case ModelCallStatus::ConfigurationError: return "configuration_error";
        case ModelCallStatus::AuthenticationError: return "authentication_error";
        case ModelCallStatus::RateLimited: return "rate_limited";
        case ModelCallStatus::Timeout: return "timeout";
        case ModelCallStatus::TransportError: return "transport_error";
        case ModelCallStatus::ProviderError: return "provider_error";
        case ModelCallStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

FakeModelClient::FakeModelClient(ModelCallResult scriptedResult)
    : scripted_result_(std::move(scriptedResult)) {}

ModelCallResult FakeModelClient::invoke(const ModelRequest& request) noexcept {
    try {
        ++call_count_;
        last_request_ = request;
        return scripted_result_;
    } catch (...) {
        ModelCallResult failure;
        failure.status = ModelCallStatus::ProviderError;
        failure.provider = "synthetic-test";
        failure.model_name = "fake-model";
        failure.error_code = "E_FAKE_CLIENT_INTERNAL";
        failure.message = "fake client could not record or return scripted result";
        return failure;
    }
}

std::size_t FakeModelClient::callCount() const noexcept {
    return call_count_;
}

const std::optional<ModelRequest>& FakeModelClient::lastRequest() const noexcept {
    return last_request_;
}

} // namespace hy3
