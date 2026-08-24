// No-network checks for ProductionHttpTransport request safety gates.

#include "hy3_algotrace/production_http_transport.hpp"

#include <iostream>

using namespace hy3;

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (condition) {                                                      \
            ++passed;                                                         \
        } else {                                                              \
            ++failed;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << message << "\n"; \
        }                                                                     \
    } while (0)

HttpRequest validRequest() {
    HttpRequest request;
    request.method = "POST";
    request.url = "https://tokenhub.tencentmaas.com/v1/chat/completions";
    request.headers = {{"Authorization", "Bearer synthetic-test-key"},
                       {"Content-Type", "application/json"}};
    request.body = {'{', '}'};
    return request;
}

} // namespace

int main() {
    const HttpRequest valid = validRequest();
    CHECK(ProductionHttpTransport::isValidHttpsPostRequest(valid),
          "well-formed HTTPS POST is accepted without I/O");

    HttpRequest nonHttps = valid;
    nonHttps.url = "http://tokenhub.tencentmaas.com/v1/chat/completions";
    CHECK(!ProductionHttpTransport::isValidHttpsPostRequest(nonHttps),
          "plain HTTP is rejected before I/O");

    HttpRequest wrongMethod = valid;
    wrongMethod.method = "GET";
    CHECK(!ProductionHttpTransport::isValidHttpsPostRequest(wrongMethod),
          "non-POST request is rejected before I/O");

    HttpRequest urlCredentials = valid;
    urlCredentials.url = "https://secret@tokenhub.tencentmaas.com/v1";
    CHECK(!ProductionHttpTransport::isValidHttpsPostRequest(urlCredentials),
          "URL credentials are rejected before I/O");

    HttpRequest otherOrigin = valid;
    otherOrigin.url = "https://example.invalid/v1/chat/completions";
    CHECK(!ProductionHttpTransport::isValidHttpsPostRequest(otherOrigin),
          "non-TokenHub origin is rejected before I/O");

    HttpRequest injectedHeader = valid;
    injectedHeader.headers[0].second = "Bearer x\r\nX-Injected: yes";
    CHECK(!ProductionHttpTransport::isValidHttpsPostRequest(injectedHeader),
          "control characters in request headers are rejected before I/O");

    std::cout << "ProductionHttpTransport tests: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
