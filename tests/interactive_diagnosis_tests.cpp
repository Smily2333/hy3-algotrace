#include "hy3_algotrace/interactive_diagnosis.hpp"
#include "hy3_algotrace/model_client.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
           ("hy3_interactive_diagnosis_" + tag + "_" +
            std::to_string(++gSequence));
}

void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::vector<std::uint8_t> bytes(const std::string& value) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

std::string readText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

InteractiveDiagnosisRequest requestWithCode(const std::string& id) {
    InteractiveDiagnosisRequest request;
    request.request_id = id;
    request.algorithm_type = "greedy";
    request.problem.title = "Twins";
    request.problem.statement = "选取最少硬币，使所选和严格大于剩余和。";
    request.problem.input_format = "n 和 n 个硬币面值";
    request.problem.output_format = "最少硬币数";
    request.problem.constraints = "1 <= n <= 100";
    request.reasoning = "降序取硬币，直到所选和严格大于剩余和。";
    request.cpp_solution = "if (sum >= rest) break;";
    request.test_cases = {{"2\n5 5\n", "2\n"}};
    return request;
}

json diagnosisFor(const std::string& id) {
    return json{
        {"schema_version", "interactive-diagnosis-v1"},
        {"request_id", id},
        {"status", "incorrect"},
        {"primary_category", "implementation_mismatch"},
        {"findings",
         json::array({{{"stage", "implementation_consistency"},
                       {"category", "implementation_mismatch"},
                       {"evidence", "思路要求严格大于，但代码使用了 >=。"},
                       {"input_excerpt", "if (sum >= rest) break;"},
                       {"suggestion", "将停止条件改为严格大于。"}}})},
        {"assessments",
         {{"complexity", {{"status", "ok"}, {"summary", "排序复杂度合理。"}}},
          {"boundary_conditions",
           {{"status", "issue"}, {"summary", "相等边界处理错误。"}}},
          {"implementation_consistency",
           {{"status", "issue"}, {"summary", "代码与思路不一致。"}}}}},
        {"short_suggestion", "修正比较条件并复查相等边界。"},
    };
}

ModelCallResult success(const json& diagnosis) {
    ModelCallResult result;
    result.status = ModelCallStatus::Succeeded;
    result.raw_response = bytes(diagnosis.dump());
    result.provider = "synthetic-test";
    result.model_name = "fake-hy3";
    result.model_version = "test-v1";
    result.http_status = 200;
    result.request_id = "provider-safe-id";
    result.token_usage = ModelTokenUsage{100, 40, 140};
    result.started_at = "2026-08-24T00:00:00Z";
    result.finished_at = "2026-08-24T00:00:01Z";
    result.duration_ms = 17;
    return result;
}

} // namespace

int main() {
    const std::string templateLf =
        "Interactive greedy request:\n{{interactive_request_json}}\n";
    const std::string templateCrlf =
        "Interactive greedy request:\r\n{{interactive_request_json}}\r\n";

    {
        const json input{
            {"request_id", "parse-1"},
            {"algorithm_type", "greedy"},
            {"problem",
             {{"title", "T"}, {"statement", "S\r\nS2"},
              {"input_format", "I"}, {"output_format", "O"},
              {"constraints", "C"}}},
            {"reasoning", "R"},
        };
        InteractiveDiagnosisRequest parsed;
        const auto validation = parseInteractiveDiagnosisRequest(input, parsed);
        CHECK(validation.ok && parsed.problem.statement == "S\nS2",
              "strict parser accepts omitted optionals and normalizes CRLF");
        CHECK(!parsed.cpp_solution && parsed.test_cases.empty() && !parsed.user_notes,
              "omitted optional fields normalize to empty optionals");

        json unknown = input;
        unknown["gold"] = "implementation_mismatch";
        CHECK(!parseInteractiveDiagnosisRequest(unknown, parsed).ok,
              "unknown fields including gold are rejected");
        json wrongType = input;
        wrongType["algorithm_type"] = "dp";
        CHECK(!parseInteractiveDiagnosisRequest(wrongType, parsed).ok,
              "non-greedy requests are rejected before a call");
    }

    {
        const fs::path root = tempRoot("success");
        cleanup(root);
        const auto request = requestWithCode("success-1");
        const std::string raw = diagnosisFor(request.request_id).dump();
        FakeModelClient client(success(diagnosisFor(request.request_id)));
        const auto result = runInteractiveDiagnosis(
            request, templateCrlf, root.string(), client);
        CHECK(result.ok && result.parse_status == "parsed",
              "valid fake response passes strict interactive validation");
        CHECK(client.callCount() == 1,
              "accepted interactive request invokes FakeModelClient once");
        CHECK(client.lastRequest() &&
                  client.lastRequest()->normalized_prompt.find("\r") == std::string::npos,
              "template and user content use normalized prompt bytes");
        CHECK(result.metadata.prompt_template_sha256 == sha256_hex(templateLf),
              "CRLF and LF templates have the same canonical hash");
        const fs::path run = root / request.request_id;
        CHECK(readText(run / "raw-response.txt") == raw,
              "successful model content is saved verbatim for local audit");
        const json sidecar = json::parse(readText(run / "model-call.json"));
        CHECK(sidecar.at("parse_status") == "parsed" &&
                  sidecar.at("raw_response_sha256") == sha256_hex(raw),
              "safe sidecar links the raw response by SHA-256");
        const json browser = interactiveDiagnosisBrowserJson(result);
        CHECK(browser.at("diagnosis").at("status") == "incorrect" &&
                  !browser.contains("raw_response") &&
                  browser.at("metadata").at("response_sha256") == sha256_hex(raw),
              "browser output exposes strict diagnosis and hash, not a raw field");

        const auto duplicate = runInteractiveDiagnosis(
            request, templateLf, root.string(), client);
        CHECK(!duplicate.ok &&
                  duplicate.error_code == interactive_errc::E_REQUEST_EXISTS &&
                  client.callCount() == 1,
              "duplicate request directory prevents a second model invocation");
        cleanup(root);
    }

    {
        const fs::path root = tempRoot("fenced");
        cleanup(root);
        const auto request = requestWithCode("fenced-1");
        ModelCallResult call = success(diagnosisFor(request.request_id));
        const std::string fenced = "```json\n" + diagnosisFor(request.request_id).dump() +
                                   "\n```";
        call.raw_response = bytes(fenced);
        FakeModelClient client(call);
        const auto result = runInteractiveDiagnosis(
            request, templateLf, root.string(), client);
        CHECK(!result.ok && result.parse_status == "invalid_json",
              "Markdown fences are not stripped or repaired");
        CHECK(readText(root / request.request_id / "raw-response.txt") == fenced,
              "invalid raw response remains verbatim in local audit");
        cleanup(root);
    }

    {
        const fs::path root = tempRoot("no_code");
        cleanup(root);
        auto request = requestWithCode("no-code-1");
        request.cpp_solution.reset();
        FakeModelClient client(success(diagnosisFor(request.request_id)));
        const auto result = runInteractiveDiagnosis(
            request, templateLf, root.string(), client);
        CHECK(!result.ok && result.parse_status == "semantic_invalid",
              "implementation finding is rejected when no code was supplied");
        cleanup(root);
    }

    {
        const fs::path root = tempRoot("undetermined");
        cleanup(root);
        auto request = requestWithCode("undetermined-1");
        request.cpp_solution.reset();
        const json diagnosis{
            {"schema_version", "interactive-diagnosis-v1"},
            {"request_id", request.request_id},
            {"status", "undetermined"},
            {"primary_category", nullptr},
            {"findings", json::array()},
            {"assessments",
             {{"complexity", {{"status", "not_assessed"}, {"summary", "信息不足。"}}},
              {"boundary_conditions", {{"status", "not_assessed"}, {"summary", "信息不足。"}}},
              {"implementation_consistency", {{"status", "not_assessed"}, {"summary", "未提供代码。"}}}}},
            {"short_suggestion", "请补充足以确认贪心正确性的证明。"},
        };
        FakeModelClient client(success(diagnosis));
        const auto result = runInteractiveDiagnosis(
            request, templateLf, root.string(), client);
        CHECK(result.ok && result.diagnosis.at("status") == "undetermined",
              "undetermined is accepted for an inconclusive greedy diagnosis");
        cleanup(root);
    }

    {
        const fs::path root = tempRoot("auth");
        cleanup(root);
        const auto request = requestWithCode("auth-1");
        ModelCallResult call;
        call.status = ModelCallStatus::AuthenticationError;
        call.provider = "synthetic-test";
        call.model_name = "fake-hy3";
        call.error_code = "unsafe-provider-code";
        call.message = "Authorization: Bearer synthetic-secret";
        call.started_at = "2026-08-24T00:00:00Z";
        call.finished_at = "2026-08-24T00:00:01Z";
        FakeModelClient client(call);
        const auto result = runInteractiveDiagnosis(
            request, templateLf, root.string(), client);
        const std::string browser = interactiveDiagnosisBrowserJson(result).dump();
        const std::string sidecar = readText(root / request.request_id /
                                             "model-call.json");
        CHECK(!result.ok &&
                  result.error_code == interactive_errc::E_MODEL_AUTHENTICATION,
              "authentication error remains a transport outcome");
        CHECK(browser.find("synthetic-secret") == std::string::npos &&
                  sidecar.find("synthetic-secret") == std::string::npos &&
                  sidecar.find("Bearer") == std::string::npos,
              "unsafe client diagnostics never reach browser or sidecar");
        cleanup(root);
    }

    std::cout << "interactive_diagnosis_tests: " << gPassed << " passed, "
              << gFailed << " failed\n";
    return gFailed == 0 ? 0 : 1;
}
