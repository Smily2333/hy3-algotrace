// hy3-algotrace -- loopback-only HTTP surface for the interactive demo.

#pragma once

#include "hy3_algotrace/interactive_diagnosis.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace hy3 {

struct InteractiveHttpReply {
    int status = 500;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

struct InteractiveServerConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::filesystem::path web_root = "web";
    std::filesystem::path artifacts_root = "experiments/interactive/runs";
    std::size_t request_body_limit_bytes = 256U * 1024U;
};

// HTTP-independent application handler. Tests exercise this class directly,
// so FakeModelClient coverage never opens a socket.
class InteractiveHttpApplication {
public:
    InteractiveHttpApplication(IModelClient& client,
                               std::string promptTemplateText,
                               std::string artifactsRoot,
                               bool tokenHubConfigured);

    InteractiveHttpReply health() const;
    InteractiveHttpReply diagnose(const std::string& contentType,
                                  const std::string& body);

private:
    IModelClient& client_;
    std::string prompt_template_text_;
    std::string artifacts_root_;
    bool token_hub_configured_ = false;
    std::atomic_flag call_active_ = ATOMIC_FLAG_INIT;
};

// Starts a blocking cpp-httplib server. Only 127.0.0.1 is accepted in this
// milestone. Returns false after a bind/listen failure or invalid static root.
bool serveInteractiveDemo(const InteractiveServerConfig& config,
                          InteractiveHttpApplication& application,
                          std::string& safeError);

} // namespace hy3
