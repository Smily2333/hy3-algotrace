#include "hy3_algotrace/hy3_model_client.hpp"
#include "hy3_algotrace/interactive_server.hpp"
#include "hy3_algotrace/production_http_transport.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

void printUsage() {
    std::cout
        << "hy3_algotrace_demo --host 127.0.0.1 --port 8080\n"
        << "  [--web-root web]\n"
        << "  [--prompt-template prompts/hy3-interactive-diagnosis-v2.md]\n"
        << "  [--artifacts-root experiments/interactive/runs/v2]\n\n"
        << "The server is loopback-only. TOKENHUB_API_KEY is read from the\n"
        << "server process environment and is never sent to the browser.\n";
}

bool parsePort(const std::string& value, std::uint16_t& port) {
    try {
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed, 10);
        if (consumed != value.size() || parsed == 0 ||
            parsed > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        port = static_cast<std::uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool readTextFile(const fs::path& path, std::string& text) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec ||
        fs::file_size(path, ec) > 1024U * 1024U || ec) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    return !input.bad();
}

bool apiKeyConfigured() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    const bool configured =
        _dupenv_s(&value, &length, "TOKENHUB_API_KEY") == 0 &&
        value != nullptr && value[0] != '\0';
    std::free(value);
    return configured;
#else
    const char* value = std::getenv("TOKENHUB_API_KEY");
    return value != nullptr && value[0] != '\0';
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        hy3::InteractiveServerConfig serverConfig;
        fs::path promptPath = "prompts/hy3-interactive-diagnosis-v2.md";

        const std::vector<std::string> args(argv, argv + argc);
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--help" || args[i] == "-h") {
                printUsage();
                return 0;
            }
            if (i + 1 >= args.size()) {
                std::cerr << "E_BAD_ARGUMENT: option missing value\n";
                return 2;
            }
            const std::string value = args[++i];
            if (args[i - 1] == "--host") {
                serverConfig.host = value;
            } else if (args[i - 1] == "--port") {
                if (!parsePort(value, serverConfig.port)) {
                    std::cerr << "E_BAD_ARGUMENT: invalid port\n";
                    return 2;
                }
            } else if (args[i - 1] == "--web-root") {
                serverConfig.web_root = value;
            } else if (args[i - 1] == "--prompt-template") {
                promptPath = value;
            } else if (args[i - 1] == "--artifacts-root") {
                serverConfig.artifacts_root = value;
            } else {
                std::cerr << "E_BAD_ARGUMENT: unknown option " << args[i - 1]
                          << "\n";
                return 2;
            }
        }

        std::string promptTemplate;
        if (!readTextFile(promptPath, promptTemplate)) {
            std::cerr << "E_FILE_READ: cannot load the interactive prompt template\n";
            return 1;
        }
        if (!hy3::validInteractivePromptTemplate(promptTemplate)) {
            std::cerr << "E_TEMPLATE_INVALID: expected interactive v2 header and marker\n";
            return 1;
        }

        hy3::ProductionHttpTransport transport;
        hy3::Hy3ModelClientConfig modelConfig;
        modelConfig.model = "hy3";
        modelConfig.connect_timeout_ms = 10'000;
        modelConfig.read_timeout_ms = 90'000;
        modelConfig.total_timeout_ms = 90'000;
        hy3::Hy3ModelClient modelClient(transport, modelConfig);

        const bool configured = apiKeyConfigured();
        hy3::InteractiveHttpApplication application(
            modelClient, std::move(promptTemplate),
            serverConfig.artifacts_root.string(), configured);

        std::cout << "hy3-algotrace interactive demo\n"
                  << "URL: http://" << serverConfig.host << ':'
                  << serverConfig.port << "/\n"
                  << "Scope: greedy only; model static review; no code execution\n"
                  << "TokenHub: " << (configured ? "configured" : "not configured")
                  << "\nPress Ctrl+C to stop.\n";
        std::string safeError;
        if (!hy3::serveInteractiveDemo(serverConfig, application, safeError)) {
            std::cerr << "E_DEMO_SERVER: " << safeError << "\n";
            return 1;
        }
        return 0;
    } catch (const std::exception&) {
        std::cerr << "E_INTERNAL: local demo startup failed\n";
        return 2;
    }
}
