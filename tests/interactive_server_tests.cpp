#include "hy3_algotrace/interactive_server.hpp"
#include "hy3_algotrace/model_client.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace hy3;

int gPassed = 0;
int gFailed = 0;
int gSequence = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) {                                                       \
            ++gPassed;                                                         \
        } else {                                                               \
            ++gFailed;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (message) << '\n'; \
        }                                                                      \
    } while (0)

fs::path tempRoot(const std::string& tag) {
    return fs::temp_directory_path() /
           ("hy3_interactive_http_" + tag + "_" +
            std::to_string(++gSequence));
}

void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::vector<std::uint8_t> bytes(const std::string& value) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

ModelCallResult successfulCall(const std::string& requestId) {
    const json diagnosis{
        {"schema_version", "interactive-diagnosis-v1"},
        {"request_id", requestId},
        {"status", "incorrect"},
        {"primary_category", "implementation_mismatch"},
        {"findings",
         json::array({{{"stage", "implementation_consistency"},
                       {"category", "implementation_mismatch"},
                       {"evidence", "思路要求严格大于，但代码使用 >=。"},
                       {"input_excerpt", "if (sum >= rest)"},
                       {"suggestion", "将停止条件改为严格大于。"}}})},
        {"assessments",
         {{"complexity", {{"status", "ok"}, {"summary", "排序复杂度正确。"}}},
          {"boundary_conditions",
           {{"status", "issue"}, {"summary", "相等边界处理错误。"}}},
          {"implementation_consistency",
           {{"status", "issue"}, {"summary", "代码条件与思路不一致。"}}}}},
        {"short_suggestion", "修正比较条件并复查相等边界。"},
    };
    ModelCallResult result;
    result.status = ModelCallStatus::Succeeded;
    result.raw_response = bytes(diagnosis.dump());
    result.provider = "synthetic-test";
    result.model_name = "fake-hy3";
    result.model_version = "test-v1";
    result.http_status = 200;
    result.request_id = "provider-request-1";
    result.token_usage = ModelTokenUsage{100, 50, 150};
    result.started_at = "2026-08-24T00:00:00Z";
    result.finished_at = "2026-08-24T00:00:01Z";
    result.duration_ms = 12;
    return result;
}

json validRequest(const std::string& requestId) {
    return json{
        {"request_id", requestId},
        {"algorithm_type", "greedy"},
        {"problem",
         {{"title", "Twins"},
          {"statement", "选取最少硬币，使所选和严格大于剩余和。"},
          {"input_format", "n 和 n 个硬币面值"},
          {"output_format", "最少硬币数"},
          {"constraints", "1 <= n <= 100"}}},
        {"reasoning", "降序取硬币，直到所选和严格大于剩余和。"},
        {"cpp_solution", "if (sum >= rest) break;"},
        {"test_cases", json::array()},
        {"user_notes", nullptr},
    };
}

} // namespace

int main() {
    const std::string prompt =
        "interactive greedy diagnosis\n{{interactive_request_json}}\n";

    {
        const fs::path root = tempRoot("success");
        cleanup(root);
        FakeModelClient client(successfulCall("request-http-1"));
        InteractiveHttpApplication app(client, prompt, root.string(), true);

        const InteractiveHttpReply health = app.health();
        const json healthJson = json::parse(health.body);
        CHECK(health.status == 200 && healthJson.at("ok") == true,
              "health endpoint is available without a model call");
        CHECK(healthJson.at("tokenhub_status") == "configured" &&
                  client.callCount() == 0,
              "health reports configuration without probing TokenHub");

        const InteractiveHttpReply media = app.diagnose("text/plain", "{}");
        CHECK(media.status == 415 && client.callCount() == 0,
              "non-JSON content type is rejected before invocation");
        const InteractiveHttpReply malformed =
            app.diagnose("application/json", "{");
        CHECK(malformed.status == 400 && client.callCount() == 0,
              "malformed JSON is rejected before invocation");

        const std::string requestBody = validRequest("request-http-1").dump();
        const InteractiveHttpReply diagnosed =
            app.diagnose("application/json; charset=utf-8", requestBody);
        const json browser = json::parse(diagnosed.body);
        CHECK(diagnosed.status == 200 && browser.at("ok") == true,
              "valid request returns a successful browser response");
        CHECK(browser.at("diagnosis").at("primary_category") ==
                  "implementation_mismatch",
              "strict diagnosis is exposed to the UI");
        CHECK(client.callCount() == 1,
              "one accepted HTTP request invokes the model once");

        const InteractiveHttpReply duplicate =
            app.diagnose("application/json", requestBody);
        CHECK(duplicate.status == 409 && client.callCount() == 1,
              "duplicate request id is latched without another model call");
        cleanup(root);
    }

    {
        const fs::path root = tempRoot("auth");
        cleanup(root);
        ModelCallResult failure;
        failure.status = ModelCallStatus::AuthenticationError;
        failure.provider = "synthetic-test";
        failure.model_name = "fake-hy3";
        failure.message = "Authorization: Bearer synthetic-secret";
        failure.error_code = "provider-secret-detail";
        FakeModelClient client(failure);
        InteractiveHttpApplication app(client, prompt, root.string(), true);
        const InteractiveHttpReply reply = app.diagnose(
            "application/json", validRequest("request-http-auth").dump());
        CHECK(reply.status == 502 && client.callCount() == 1,
              "provider authentication failure stays separate from diagnosis");
        CHECK(reply.body.find("synthetic-secret") == std::string::npos &&
                  reply.body.find("Bearer") == std::string::npos,
              "browser error never includes unsafe provider detail");
        cleanup(root);
    }

    std::cout << "interactive_server_tests: " << gPassed << " passed, "
              << gFailed << " failed\n";
    return gFailed == 0 ? 0 : 1;
}
