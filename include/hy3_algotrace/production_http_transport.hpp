// hy3-algotrace -- concrete HTTPS transport for one production model call.
//
// This transport deliberately has no retry policy.  A caller that uses it for
// a paid model invocation is responsible for recording the attempt before
// calling perform().

#pragma once

#include "hy3_algotrace/hy3_model_client.hpp"

#include <cstddef>

namespace hy3 {

class ProductionHttpTransport final : public IHttpTransport {
public:
    // The limit prevents an unexpected server/proxy response from exhausting
    // memory.  It applies to the HTTP envelope, before a model adapter extracts
    // its content field.
    explicit ProductionHttpTransport(
        std::size_t max_response_bytes = 4U * 1024U * 1024U) noexcept;

    HttpResponse perform(const HttpRequest& request) override;

    // Exposed for zero-network tests and for callers which want to reject an
    // unsafe request before entering a paid-call critical section.
    static bool isValidHttpsPostRequest(const HttpRequest& request) noexcept;

private:
    std::size_t max_response_bytes_;
};

} // namespace hy3
