// hy3_algotrace — Phase 2C single-trace vertical smoke test.
//
// This uses the frozen dataset and prompt template, but only a deterministic
// FakeModelClient response labelled synthetic-test/fake-hy3.  It never calls a
// network API or executes candidate code and must not be mistaken for a real
// Hy3 experiment.

#include "hy3_algotrace/model_client.hpp"
#include "hy3_algotrace/model_runner.hpp"
#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/prompt_exporter.hpp"
#include "hy3_algotrace/reporter.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace hy3;

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (condition) {                                                      \
            ++g_pass;                                                         \
        } else {                                                              \
            ++g_fail;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (message) << "\n"; \
        }                                                                     \
    } while (0)

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> bytesOf(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: phase2c_vertical_tests <project_root>\n";
        return 2;
    }

    const fs::path root(argv[1]);
    const fs::path dataDir = root / "data";
    const fs::path templatePath = root / "prompts" / "hy3-evaluator-v1.md";
    const std::string targetTraceId = "cf_160A_t1";
    const std::string runId = "phase2c-synthetic-vertical";
    const std::string generatedAt = "2026-08-24T01:00:00Z";
    const fs::path run = fs::temp_directory_path() /
                         ("hy3_phase2c_vertical_" +
                          std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()));
    cleanup(run);

    // First establish a real Phase 2B run from frozen inputs, with a clearly
    // synthetic model identity.  This must create all nine prompt artifacts.
    RunManifest manifest;
    manifest.run_id = runId;
    manifest.pipeline_commit = "phase2c-vertical-test";
    manifest.model_provider = "synthetic-test";
    manifest.model_name = "fake-hy3";
    manifest.model_version = "test-v1";
    manifest.started_at = "2026-08-24T00:00:00Z";
    manifest.notes = "SYNTHETIC_TEST_FIXTURE: FakeModelClient vertical smoke only.";
    std::string templateSha;
    const ExporterResult exported = exportPrompts(
        dataDir.string(), readFile(templatePath), run.string(), manifest, templateSha);
    if (!exported.ok) {
        std::cerr << "exportPrompts failed: " << exported.error_code << ": "
                  << exported.message << "\n";
        cleanup(run);
        return 1;
    }

    int promptCount = 0;
    for (const auto& entry : fs::directory_iterator(run / "prompts")) {
        if (entry.is_regular_file()) ++promptCount;
    }
    CHECK(promptCount == 9, "frozen dataset exports nine prompts");
    CHECK(fs::exists(run / "prompts" / (targetTraceId + ".txt")),
          "known target trace prompt exists");

    nlohmann::json initialManifest;
    {
        std::ifstream input(run / "run-manifest.json");
        input >> initialManifest;
    }
    CHECK(initialManifest.at("model_provider") == "synthetic-test",
          "manifest retains synthetic provider identity");
    CHECK(initialManifest.at("model_name") == "fake-hy3",
          "manifest retains synthetic model identity");
    CHECK(initialManifest.at("model_version") == "test-v1",
          "manifest retains synthetic model version");
    CHECK(initialManifest.at("trace_ids").size() == 9,
          "manifest enumerates all nine traces");

    // A single response is deliberately valid JSON for the known trace.  Its
    // bytes include CRLF to prove raw-response hashing is independent of the
    // prompt's UTF-8/LF normalization boundary.
    const std::string rawText =
        "{\"trace_id\":\"cf_160A_t1\",\"status\":\"correct\","
        "\"primary_category\":null,\"findings\":[],"
        "\"confidence\":null,\"confidence_method\":null,"
        "\"calibration_version\":null}\r\n";
    const std::vector<std::uint8_t> rawBytes = bytesOf(rawText);
    ModelCallResult scripted;
    scripted.status = ModelCallStatus::Succeeded;
    scripted.raw_response = rawBytes;
    scripted.provider = "synthetic-test";
    scripted.model_name = "fake-hy3";
    scripted.model_version = "test-v1";
    scripted.started_at = "2026-08-24T00:00:10Z";
    scripted.finished_at = "2026-08-24T00:00:11Z";
    scripted.duration_ms = 1;
    FakeModelClient client(scripted);

    const ModelRunResult called = runModelForTrace(
        run.string(), targetTraceId, runId, generatedAt, client);
    CHECK(called.ok, "FakeModelClient response completes ModelRunner path");
    CHECK(client.callCount() == 1, "one fake model call for target trace");
    CHECK(client.lastRequest().has_value(), "fake client captured target request");
    const std::string savedPrompt = readFile(run / "prompts" / (targetTraceId + ".txt"));
    if (client.lastRequest()) {
        CHECK(client.lastRequest()->trace_id == targetTraceId,
              "fake request uses target trace id");
        CHECK(client.lastRequest()->normalized_prompt == savedPrompt,
              "fake request content equals persisted normalized prompt");
        CHECK(client.lastRequest()->prompt_sha256 == sha256_hex(savedPrompt),
              "fake request prompt SHA matches persisted prompt");
    }

    const fs::path rawPath = run / "raw-responses" / (targetTraceId + ".txt");
    const fs::path wrapperPath = run / "predictions" / (targetTraceId + ".json");
    CHECK(readFile(rawPath) == rawText, "target raw response is byte-for-byte retained");
    nlohmann::json wrapper;
    {
        std::ifstream input(wrapperPath);
        input >> wrapper;
    }
    CHECK(wrapper.at("parse_status") == "parsed", "target wrapper is parsed");
    CHECK(wrapper.at("model_name") == "fake-hy3",
          "wrapper model_name comes from run manifest");
    CHECK(wrapper.at("prompt_sha256") == sha256_hex(savedPrompt),
          "wrapper stores prompt SHA");
    CHECK(wrapper.at("raw_response_sha256") == sha256_hex(rawBytes),
          "wrapper stores unnormalized raw-byte SHA");

    // A complete report requires one explicit wrapper for every remaining
    // trace.  These are intentionally not model calls and must carry the
    // protocol's explicit not-attempted status.
    const std::vector<std::string> traceIds =
        initialManifest.at("trace_ids").get<std::vector<std::string>>();
    int marked = 0;
    for (const std::string& traceId : traceIds) {
        if (traceId == targetTraceId) continue;
        const ImporterResult markedResult = markNotAttempted(
            run.string(), traceId, runId, "2026-08-24T00:02:00Z");
        CHECK(markedResult.ok, "remaining trace explicitly marked not attempted");
        if (markedResult.ok) ++marked;
    }
    CHECK(marked == 8, "eight remaining traces explicitly marked");

    const ReporterResult reported = generateReport(
        run.string(), dataDir.string(), "2026-08-24T00:03:00Z",
        "2026-08-24T00:03:00Z");
    CHECK(reported.ok, "Reporter generates synthetic vertical report");
    CHECK(reported.run_complete, "all nine wrappers make run complete");
    nlohmann::json report;
    {
        std::ifstream input(run / "report.json");
        input >> report;
    }
    const double parseSuccess = report.at("metrics").at("parse_success_rate").get<double>();
    CHECK(std::abs(parseSuccess - (1.0 / 9.0)) < 1e-12,
          "report parse_success_rate is exactly one ninth");

    // Prediction wrappers must remain gold-free and must never serialize the
    // Reporter's in-memory parse-failure sentinel.
    bool predictionLeaksGold = false;
    bool predictionLeaksSentinel = false;
    for (const std::string& traceId : traceIds) {
        const std::string text = readFile(run / "predictions" / (traceId + ".json"));
        if (text.find("\"diagnoses\"") != std::string::npos) predictionLeaksGold = true;
        if (text.find("__parse_failed__") != std::string::npos) {
            predictionLeaksSentinel = true;
        }
    }
    CHECK(!predictionLeaksGold, "prediction wrappers contain no gold diagnoses");
    CHECK(!predictionLeaksSentinel, "prediction wrappers contain no parse sentinel");

    nlohmann::json finalManifest;
    {
        std::ifstream input(run / "run-manifest.json");
        input >> finalManifest;
    }
    CHECK(finalManifest.at("model_version") == "test-v1",
          "report completion preserves manifest model version");
    CHECK(finalManifest.at("completed_at") == "2026-08-24T00:03:00Z",
          "complete report updates manifest completion time");

    cleanup(run);
    std::cout << "phase2c_vertical_tests: " << g_pass << " passed, " << g_fail
              << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
