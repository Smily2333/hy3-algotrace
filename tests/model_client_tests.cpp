// hy3_algotrace — ModelClient / ModelRunner tests (Phase 2C)
//
// All calls use FakeModelClient.  No network, credentials, model API, or
// candidate code is involved.  The tests exercise the narrow vertical path:
// saved prompt -> injected client -> strict PredictionImporter.

#include "hy3_algotrace/model_client.hpp"
#include "hy3_algotrace/model_runner.hpp"
#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/sha256.hpp"

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
int g_temp_counter = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (condition) {                                                      \
            ++g_pass;                                                         \
        } else {                                                              \
            ++g_fail;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (message) << "\n"; \
        }                                                                     \
    } while (0)

fs::path makeRun(const std::string& tag, bool writePrompt = true) {
    const fs::path run = fs::temp_directory_path() /
                         ("hy3_model_client_test_" + tag + "_" +
                          std::to_string(++g_temp_counter));
    std::error_code ec;
    fs::remove_all(run, ec);
    fs::create_directories(run / "prompts", ec);
    fs::create_directories(run / "raw-responses", ec);
    fs::create_directories(run / "predictions", ec);
    nlohmann::json manifest;
    manifest["evaluation_schema_version"] = "0.1.0";
    manifest["run_id"] = "synthetic-model-run";
    manifest["dataset_version"] = "synthetic-dataset";
    manifest["dataset_commit"] = "synthetic-commit";
    manifest["taxonomy_version"] = "1.0.0";
    manifest["model_provider"] = "synthetic-test";
    manifest["model_name"] = "fake-hy3";
    manifest["model_version"] = "test-v1";
    manifest["pipeline_commit"] = "synthetic-pipeline";
    manifest["prompt_template_id"] = "hy3-evaluator-v1";
    manifest["prompt_template_sha256"] = sha256_hex("synthetic-template");
    manifest["input_mode"] = "reference_assisted";
    manifest["started_at"] = "2026-08-24T00:00:00Z";
    manifest["completed_at"] = nullptr;
    manifest["trace_ids"] = {"trace_1"};
    manifest["total_traces"] = 1;
    manifest["notes"] = "synthetic test fixture";
    std::ofstream manifestFile(run / "run-manifest.json", std::ios::binary);
    manifestFile << manifest.dump(2);
    if (writePrompt) {
        // Deliberately CRLF: ModelRunner must send the normalized prompt while
        // Importer keeps raw model bytes distinct from prompt normalization.
        std::ofstream prompt(run / "prompts" / "trace_1.txt", std::ios::binary);
        prompt << "Synthetic prompt\r\n";
    }
    return run;
}

std::vector<std::uint8_t> toBytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

ModelCallResult success(const std::vector<std::uint8_t>& raw) {
    ModelCallResult result;
    result.status = ModelCallStatus::Succeeded;
    result.raw_response = raw;
    result.provider = "synthetic-test";
    result.model_name = "fake-hy3";
    result.model_version = "test-v1";
    result.started_at = "2026-08-24T00:00:00Z";
    result.finished_at = "2026-08-24T00:00:01Z";
    result.duration_ms = 1;
    return result;
}

nlohmann::json readWrapper(const fs::path& run) {
    std::ifstream input(run / "predictions" / "trace_1.json");
    nlohmann::json wrapper;
    input >> wrapper;
    return wrapper;
}

nlohmann::json readSidecar(const fs::path& run) {
    std::ifstream input(run / "model-calls" / "trace_1.json");
    nlohmann::json sidecar;
    input >> sidecar;
    return sidecar;
}

ModelCallAuditConfig auditConfig() {
    ModelCallAuditConfig config;
    config.service = "tokenhub";
    config.endpoint_origin = "https://tokenhub.tencentmaas.com";
    config.timeout_seconds = 30;
    return config;
}

class SidecarInspectingClient final : public IModelClient {
public:
    SidecarInspectingClient(fs::path run, ModelCallResult scripted)
        : run_(std::move(run)), scripted_(std::move(scripted)) {}

    ModelCallResult invoke(const ModelRequest&) noexcept override {
        ++call_count;
        try {
            const nlohmann::json sidecar = readSidecar(run_);
            observed_attempting = sidecar.at("outcome") == "attempting" &&
                                  sidecar.at("completed_at").is_null();
        } catch (...) {
            observed_attempting = false;
        }
        return scripted_;
    }

    fs::path run_;
    ModelCallResult scripted_;
    int call_count = 0;
    bool observed_attempting = false;
};

bool noRunArtifacts(const fs::path& run) {
    return !fs::exists(run / "raw-responses" / "trace_1.txt") &&
           !fs::exists(run / "predictions" / "trace_1.json");
}

void cleanup(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

} // namespace

int main() {
    const std::string kRunId = "synthetic-model-run";
    const std::string kGeneratedAt = "2026-08-24T00:02:00Z";

    // The audited production path creates its durable one-shot record before
    // invoke(), then atomically finalizes it after strict import.
    {
        const fs::path run = makeRun("recorded_success");
        const std::string rawText =
            "{\"trace_id\":\"trace_1\",\"status\":\"correct\","
            "\"primary_category\":null,\"findings\":[],"
            "\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        ModelCallResult scripted = success(toBytes(rawText));
        scripted.http_status = 200;
        scripted.request_id = "req-synthetic-1";
        scripted.token_usage = ModelTokenUsage{11, 7, 18};
        SidecarInspectingClient client(run, scripted);

        const ModelRunResult result = runRecordedModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, auditConfig(), client);
        CHECK(result.ok, "recorded fake response succeeds");
        CHECK(client.call_count == 1, "recorded path invokes exactly once");
        CHECK(client.observed_attempting,
              "attempting sidecar is present before model invocation");
        const nlohmann::json sidecar = readSidecar(run);
        CHECK(sidecar.at("schema_version") == "0.1.0",
              "sidecar has independent schema version");
        CHECK(sidecar.at("outcome") == "success",
              "successful imported response finalizes as success");
        CHECK(sidecar.at("http_status") == 200 &&
                  sidecar.at("request_id") == "req-synthetic-1",
              "sidecar retains safe HTTP metadata");
        CHECK(sidecar.at("token_usage").at("total_tokens") == 18,
              "sidecar retains available token usage");
        CHECK(sidecar.at("raw_response_sha256") == sha256_hex(rawText),
              "sidecar links the saved raw response by hash");
        CHECK(sidecar.at("response_saved") == true &&
                  sidecar.at("prediction_imported") == true &&
                  sidecar.at("parse_status") == "parsed",
              "sidecar records importer completion without copying content");
        cleanup(run);
    }

    // Even a failed attempt leaves a sidecar that blocks a second request.
    {
        const fs::path run = makeRun("recorded_failure_latch");
        ModelCallResult timedOut;
        timedOut.status = ModelCallStatus::Timeout;
        timedOut.provider = "synthetic-test";
        timedOut.model_name = "fake-hy3";
        timedOut.started_at = "2026-08-24T00:00:00Z";
        timedOut.finished_at = "2026-08-24T00:00:30Z";
        timedOut.duration_ms = 30000;
        timedOut.message = "Authorization: Bearer synthetic-secret-must-not-leak";
        FakeModelClient first(timedOut);
        const ModelRunResult failed = runRecordedModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, auditConfig(), first);
        CHECK(!failed.ok && readSidecar(run).at("outcome") == "timeout",
              "failed attempt is finalized as timeout");
        const std::vector<std::uint8_t> sidecarBytes = readBytes(
            run / "model-calls" / "trace_1.json");
        CHECK(std::string(sidecarBytes.begin(), sidecarBytes.end()).find(
                  "synthetic-secret-must-not-leak") == std::string::npos,
              "sidecar never copies unsafe client error text");

        FakeModelClient second(success(toBytes("{}")));
        const ModelRunResult repeated = runRecordedModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, auditConfig(), second);
        CHECK(!repeated.ok &&
                  repeated.error_code == model_runner_errc::E_AUDIT_PRECHECK,
              "existing failed sidecar refuses a repeated call");
        CHECK(second.callCount() == 0,
              "failed sidecar latch prevents another billable invocation");
        cleanup(run);
    }

    // A completed run is immutable and must fail before creating an attempt
    // record or invoking the model client.
    {
        const fs::path run = makeRun("recorded_completed_manifest");
        nlohmann::json manifest;
        {
            std::ifstream input(run / "run-manifest.json");
            input >> manifest;
        }
        manifest["completed_at"] = "2026-08-24T00:03:00Z";
        std::ofstream(run / "run-manifest.json", std::ios::binary | std::ios::trunc)
            << manifest.dump(2);
        FakeModelClient client(success(toBytes("{}")));
        const ModelRunResult result = runRecordedModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, auditConfig(), client);
        CHECK(!result.ok && result.error_code == model_runner_errc::E_RUN_CONTEXT,
              "completed manifest is a hard pre-call failure");
        CHECK(client.callCount() == 0 && !fs::exists(run / "model-calls"),
              "completed manifest creates no sidecar and performs no call");
        cleanup(run);
    }

    // A valid fake response reaches the same byte-preserving Importer path as
    // an offline raw-response file and creates a parsed prediction wrapper.
    {
        const fs::path run = makeRun("valid");
        const std::string rawText =
            "{\"trace_id\":\"trace_1\",\"status\":\"correct\","
            "\"primary_category\":null,\"findings\":[],"
            "\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}\r\n";
        const std::vector<std::uint8_t> raw = toBytes(rawText);
        FakeModelClient client(success(raw));

        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(result.ok, "valid fake response succeeds");
        CHECK(client.callCount() == 1, "valid fake invoked exactly once");
        CHECK(client.lastRequest().has_value(), "fake records request");
        if (client.lastRequest()) {
            CHECK(client.lastRequest()->trace_id == "trace_1", "trace id forwarded");
            CHECK(client.lastRequest()->normalized_prompt == "Synthetic prompt\n",
                  "prompt is normalized before client invocation");
            CHECK(client.lastRequest()->prompt_sha256 ==
                      sha256_hex("Synthetic prompt\n"),
                  "request prompt SHA is normalized prompt SHA");
        }
        CHECK(readBytes(run / "raw-responses" / "trace_1.txt") == raw,
              "fake response bytes saved verbatim");
        const nlohmann::json wrapper = readWrapper(run);
        CHECK(wrapper.at("parse_status") == "parsed", "valid fake -> parsed");
        CHECK(wrapper.at("model_name") == "fake-hy3",
              "wrapper model identity comes from run manifest");
        CHECK(wrapper.at("raw_response_sha256") == sha256_hex(raw),
              "wrapper hash is over unnormalized raw bytes");
        CHECK(wrapper.at("prediction").at("status") == "correct",
              "parsed wrapper preserves diagnostic prediction");
        cleanup(run);
    }

    // An empty successful call is a real call, not an unattempted call.  It is
    // saved as an empty file and classified by PredictionImporter.
    {
        const fs::path run = makeRun("empty");
        FakeModelClient client(success({}));
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(result.ok, "empty successful fake call imports successfully");
        CHECK(client.callCount() == 1, "empty fake was invoked");
        CHECK(fs::exists(run / "raw-responses" / "trace_1.txt"),
              "empty response still has a raw artifact");
        const nlohmann::json wrapper = readWrapper(run);
        CHECK(wrapper.at("parse_status") == "empty_response",
              "empty fake -> empty_response");
        CHECK(wrapper.at("raw_response_sha256") == sha256_hex(std::vector<std::uint8_t>{}),
              "empty raw artifact has empty-byte SHA");
        CHECK(wrapper.at("prediction").is_null(), "empty response has null prediction");
        cleanup(run);
    }

    // Non-JSON model content is retained verbatim but cannot become a parsed
    // prediction; Importer owns this classification rather than ModelClient.
    {
        const fs::path run = makeRun("invalid_json");
        const std::vector<std::uint8_t> raw = toBytes("not JSON\n");
        FakeModelClient client(success(raw));
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(result.ok, "invalid JSON is an imported response, not a call failure");
        CHECK(readBytes(run / "raw-responses" / "trace_1.txt") == raw,
              "invalid JSON raw bytes are retained");
        const nlohmann::json wrapper = readWrapper(run);
        CHECK(wrapper.at("parse_status") == "invalid_json",
              "invalid raw text -> invalid_json");
        CHECK(wrapper.at("prediction").is_null(), "invalid JSON has null prediction");
        cleanup(run);
    }

    // Every transport/provider failure is explicit and leaves no importer
    // artifacts.  In particular, none is silently changed to not_attempted.
    struct FailureCase {
        ModelCallStatus status;
        const char* expectedCode;
    };
    const FailureCase failures[] = {
        {ModelCallStatus::Timeout, model_runner_errc::E_MODEL_TIMEOUT},
        {ModelCallStatus::AuthenticationError, model_runner_errc::E_MODEL_AUTHENTICATION},
        {ModelCallStatus::RateLimited, model_runner_errc::E_MODEL_RATE_LIMITED},
        {ModelCallStatus::TransportError, model_runner_errc::E_MODEL_TRANSPORT},
        {ModelCallStatus::ProviderError, model_runner_errc::E_MODEL_PROVIDER},
    };
    for (const FailureCase& failure : failures) {
        const fs::path run = makeRun(failure.expectedCode);
        ModelCallResult scripted;
        scripted.status = failure.status;
        scripted.provider = "synthetic-test";
        scripted.model_name = "fake-hy3";
        scripted.message = "synthetic failure";
        FakeModelClient client(scripted);
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(!result.ok, std::string(failure.expectedCode) + " fails model run");
        CHECK(result.error_code == failure.expectedCode,
              std::string(failure.expectedCode) + " is stable default code");
        CHECK(result.call_result.status == failure.status,
              std::string(failure.expectedCode) + " preserves call status");
        CHECK(client.callCount() == 1,
              std::string(failure.expectedCode) + " invokes fake once");
        CHECK(noRunArtifacts(run),
              std::string(failure.expectedCode) +
                  " creates neither raw nor prediction");
        cleanup(run);
    }

    // A missing prompt is detected before transport.  The client must not be
    // invoked and no missing-input state may be rewritten as not_attempted.
    {
        const fs::path run = makeRun("missing_prompt", false);
        FakeModelClient client(success(toBytes("{}")));
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(!result.ok, "missing prompt fails before model call");
        CHECK(result.error_code == model_runner_errc::E_PROMPT_LOAD,
              "missing prompt uses ModelRunner prompt-load code");
        CHECK(result.message.find(importer_errc::E_PROMPT_MISSING) != std::string::npos,
              "missing prompt retains Importer stable cause");
        CHECK(client.callCount() == 0, "missing prompt never invokes client");
        CHECK(noRunArtifacts(run), "missing prompt creates no run artifacts");
        cleanup(run);
    }

    // A successful transport cannot silently write predictions under a model
    // identity different from the run manifest.
    {
        const fs::path run = makeRun("identity_mismatch");
        ModelCallResult scripted = success(toBytes("{}"));
        scripted.provider = "wrong-provider";
        FakeModelClient client(scripted);
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(!result.ok, "model identity mismatch is rejected");
        CHECK(result.error_code == model_runner_errc::E_MODEL_RESULT_INVALID,
              "identity mismatch uses stable invalid-result code");
        CHECK(noRunArtifacts(run), "identity mismatch creates no run artifacts");
        cleanup(run);
    }

    {
        const fs::path run = makeRun("version_mismatch");
        ModelCallResult scripted = success(toBytes("{}"));
        scripted.model_version = "wrong-version";
        FakeModelClient client(scripted);
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(!result.ok, "model version mismatch is rejected");
        CHECK(result.error_code == model_runner_errc::E_MODEL_RESULT_INVALID,
              "version mismatch uses stable invalid-result code");
        CHECK(noRunArtifacts(run), "version mismatch creates no run artifacts");
        cleanup(run);
    }

    // Existing artifacts are detected before a second, potentially billable
    // invocation. The original raw response remains untouched.
    {
        const fs::path run = makeRun("rerun_preflight");
        const std::string valid =
            "{\"trace_id\":\"trace_1\",\"status\":\"correct\","
            "\"primary_category\":null,\"findings\":[],"
            "\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        const std::vector<std::uint8_t> original = toBytes(valid);
        FakeModelClient first(success(original));
        CHECK(runModelForTrace(run.string(), "trace_1", kRunId,
                               kGeneratedAt, first).ok,
              "first call creates importer artifacts");

        FakeModelClient second(success(toBytes("different")));
        const ModelRunResult repeated = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, second);
        CHECK(!repeated.ok, "rerun with existing artifacts is rejected");
        CHECK(repeated.error_code == model_runner_errc::E_IMPORT_PRECHECK,
              "rerun fails at import precheck");
        CHECK(second.callCount() == 0, "rerun does not invoke model client");
        CHECK(readBytes(run / "raw-responses" / "trace_1.txt") == original,
              "rerun preserves original raw bytes");
        cleanup(run);
    }

    // A prediction-only collision must not create a raw half-artifact.
    {
        const fs::path run = makeRun("prediction_collision");
        std::ofstream(run / "predictions" / "trace_1.json") << "{}";
        FakeModelClient client(success(toBytes("{}")));
        const ModelRunResult result = runModelForTrace(
            run.string(), "trace_1", kRunId, kGeneratedAt, client);
        CHECK(!result.ok, "prediction-only collision is rejected");
        CHECK(client.callCount() == 0,
              "prediction-only collision never invokes model client");
        CHECK(!fs::exists(run / "raw-responses" / "trace_1.txt"),
              "prediction-only collision creates no raw half-artifact");
        cleanup(run);
    }

    // A manifest that exists but is corrupt cannot silently fall back to a
    // default Hy3 identity in the direct in-memory importer path.
    {
        const fs::path run = makeRun("corrupt_manifest");
        std::ofstream(run / "run-manifest.json", std::ios::binary) << "{bad";
        const ImporterResult result = importResponseBytes(
            run.string(), "trace_1", toBytes("{}"), kRunId, kGeneratedAt);
        CHECK(!result.ok, "corrupt existing manifest is rejected");
        CHECK(result.error_code == importer_errc::E_RUN_CONTEXT,
              "corrupt manifest uses stable run-context code");
        CHECK(noRunArtifacts(run), "corrupt manifest creates no artifacts");
        cleanup(run);
    }

    std::cout << "model_client_tests: " << g_pass << " passed, " << g_fail
              << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
