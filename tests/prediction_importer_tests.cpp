// hy3_algotrace — PredictionImporter unit tests (Phase 2B)
//
// These tests are SYNTHETIC_TEST_FIXTURE: they use hand-written JSON strings
// and in-memory run directories. They do NOT represent real Hy3 output and are
// never written into a real experiments run. They exercise the strict
// parse_status discrimination, schema/semantic validation, gold isolation,
// byte-exact raw hashing, and overwrite refusal.

#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/prompt_exporter.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace hy3;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            std::cerr << "FAIL: " << (msg) << " [" << __FILE__ << ":"        \
                      << __LINE__ << "]\n";                                   \
        }                                                                     \
    } while (0)

namespace {

// Build a minimal run dir with one prompt file so importResponse can load the
// prompt hash. Returns the run dir path.
fs::path makeRunDir(const std::string& base, const std::string& traceId,
                    bool withCandidate) {
    std::error_code ec;
    fs::path run = fs::path(base) / ("run_" + traceId);
    fs::create_directories(run / "prompts", ec);
    fs::create_directories(run / "raw-responses", ec);
    fs::create_directories(run / "predictions", ec);
    nlohmann::json manifest;
    manifest["evaluation_schema_version"] = "0.1.0";
    manifest["run_id"] = "run_x";
    manifest["model_provider"] = "tencent-hunyuan";
    manifest["model_name"] = "hy3";
    manifest["model_version"] = nullptr;
    manifest["prompt_template_id"] = "hy3-evaluator-v1";
    manifest["input_mode"] = "reference_assisted";
    std::ofstream manifestFile(run / "run-manifest.json", std::ios::binary);
    manifestFile << manifest.dump(2);
    // Match the frozen template's fifth fenced JSON input block.
    std::string prompt = "#### 5. candidate_solution\n\n```json\n";
    prompt += withCandidate ? "{\"id\":\"sol1\",\"trace_id\":\"" + traceId + "\"}" : "null";
    prompt += "\n```\n";
    std::ofstream ofs(run / "prompts" / (traceId + ".txt"), std::ios::binary);
    ofs << prompt;
    return run;
}

void writeRawFile(const fs::path& p, const std::string& content) {
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

} // namespace

int main(int argc, char** argv) {
    // Use a temp base dir.
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path() /
                   ("hy3_pi_tests_" + std::to_string(std::hash<std::string>{}(std::string(argv[0]))));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    const std::string TID = "cf_160A_t1";

    // ---- 1. empty response -> empty_response, null prediction ----
    {
        auto run = makeRunDir(tmp.string(), TID, false);
        writeRawFile(tmp / "raw.txt", "");
        ImporterResult r = importResponse(run.string(), TID, (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "empty_response import ok");
        // read wrapper
        std::ifstream wf(run / "predictions" / (TID + ".json"));
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "empty_response", "parse_status empty_response");
        CHECK(w.at("prediction").is_null(), "prediction null on empty");
        CHECK(w.at("raw_response_sha256").is_string(), "raw hash present (empty file)");
        CHECK(w.at("prompt_sha256").is_string(), "prompt hash present");
        CHECK(!w.contains("diagnoses"), "no gold leakage");
    }

    // ---- 2. whitespace-only response -> empty_response ----
    {
        auto run = makeRunDir(tmp.string(), "t_ws", false);
        writeRawFile(tmp / "raw.txt", "   \n  \t ");
        ImporterResult r = importResponse(run.string(), "t_ws", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "whitespace import ok");
        std::ifstream wf(run / "predictions" / "t_ws.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "empty_response", "whitespace -> empty_response");
    }

    // ---- 3. non-JSON -> invalid_json ----
    {
        auto run = makeRunDir(tmp.string(), "t_nj", false);
        writeRawFile(tmp / "raw.txt", "This is plain text, not JSON at all.");
        ImporterResult r = importResponse(run.string(), "t_nj", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "invalid_json import ok");
        std::ifstream wf(run / "predictions" / "t_nj.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "invalid_json", "parse_status invalid_json");
        CHECK(w.at("prediction").is_null(), "prediction null on invalid_json");
    }

    // ---- 4. Markdown fenced JSON -> invalid_json (no silent strip) ----
    {
        auto run = makeRunDir(tmp.string(), "t_md", false);
        std::string fenced = "```json\n{\"trace_id\":\"" + std::string("t_md") +
            "\",\"status\":\"correct\",\"primary_category\":null,\"findings\":[],"
            "\"confidence\":null,\"confidence_method\":null,\"calibration_version\":null}\n```";
        writeRawFile(tmp / "raw.txt", fenced);
        ImporterResult r = importResponse(run.string(), "t_md", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "fenced import ok");
        std::ifstream wf(run / "predictions" / "t_md.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "invalid_json",
              "markdown fence NOT stripped -> invalid_json");
    }

    // ---- 5. valid JSON then extra text -> invalid_json (no silent trim) ----
    {
        auto run = makeRunDir(tmp.string(), "t_et", false);
        std::string s = "{\"trace_id\":\"t_et\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null} and some trailing commentary";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_et", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "extra text import ok");
        std::ifstream wf(run / "predictions" / "t_et.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "invalid_json",
              "trailing text -> invalid_json (no repair)");
    }

    // ---- 6. missing key -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_mk", false);
        std::string s = "{\"trace_id\":\"t_mk\",\"status\":\"correct\"}"; // missing primary_category etc
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_mk", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "missing key import ok");
        std::ifstream wf(run / "predictions" / "t_mk.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "missing key -> schema_invalid");
        CHECK(!w.at("errors").empty(), "errors populated");
    }

    // ---- 7. wrong type -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_wt", false);
        std::string s = "{\"trace_id\":\"t_wt\",\"status\":123,\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_wt", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "wrong type import ok");
        std::ifstream wf(run / "predictions" / "t_wt.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "wrong type -> schema_invalid");
    }

    // ---- 8. extra top-level key -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_ek", false);
        std::string s = "{\"trace_id\":\"t_ek\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null,\"bonus\":\"leak\"}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ek", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "extra key import ok");
        std::ifstream wf(run / "predictions" / "t_ek.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "extra key -> schema_invalid");
    }

    // ---- 9. illegal enum status -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_ie", false);
        std::string s = "{\"trace_id\":\"t_ie\",\"status\":\"maybe\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ie", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "illegal enum import ok");
        std::ifstream wf(run / "predictions" / "t_ie.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "illegal enum -> schema_invalid");
    }

    // ---- 10. confidence non-null -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_cn", false);
        std::string s = "{\"trace_id\":\"t_cn\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":0.9,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_cn", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "confidence non-null import ok");
        std::ifstream wf(run / "predictions" / "t_cn.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "confidence non-null -> schema_invalid");
    }

    // ---- 11. correct: primary non-null -> semantic_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_cpc", false);
        std::string s = "{\"trace_id\":\"t_cpc\",\"status\":\"correct\",\"primary_category\":\"boundary_omission\","
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_cpc", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "correct w/ primary import ok");
        std::ifstream wf(run / "predictions" / "t_cpc.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "semantic_invalid", "correct+primary -> semantic_invalid");
    }

    // ---- 12. primary_category illegal enum -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_bad_primary", false);
        std::string s = "{\"trace_id\":\"t_bad_primary\",\"status\":\"incorrect\","
            "\"primary_category\":\"not_a_category\",\"findings\":[],"
            "\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_bad_primary",
                                          (tmp / "raw.txt").string(), "run_x",
                                          "2026-08-24T00:00:00Z");
        CHECK(r.ok, "illegal primary category import ok");
        std::ifstream wf(run / "predictions" / "t_bad_primary.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid",
              "illegal primary category -> schema_invalid");
    }

    // ---- 13. correct: findings non-empty -> semantic_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_cf", false);
        std::string s = "{\"trace_id\":\"t_cf\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[{\"stage\":\"boundary\",\"category\":\"boundary_omission\","
            "\"locating\":\"x\",\"evidence\":\"y\",\"suggestion\":\"z\"}],"
            "\"confidence\":null,\"confidence_method\":null,\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_cf", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "correct w/ findings import ok");
        std::ifstream wf(run / "predictions" / "t_cf.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "semantic_invalid", "correct+findings -> semantic_invalid");
    }

    // ---- 13. incorrect: primary not in findings -> semantic_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_ip", false);
        std::string s = "{\"trace_id\":\"t_ip\",\"status\":\"incorrect\",\"primary_category\":\"boundary_omission\","
            "\"findings\":[{\"stage\":\"complexity\",\"category\":\"complexity_error\","
            "\"locating\":\"x\",\"evidence\":\"y\",\"suggestion\":\"z\"}],"
            "\"confidence\":null,\"confidence_method\":null,\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ip", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "incorrect primary mismatch import ok");
        std::ifstream wf(run / "predictions" / "t_ip.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "semantic_invalid", "primary not in findings -> semantic_invalid");
    }

    // ---- 14. undetermined w/ findings -> semantic_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_uf", false);
        std::string s = "{\"trace_id\":\"t_uf\",\"status\":\"undetermined\",\"primary_category\":null,"
            "\"findings\":[{\"stage\":\"boundary\",\"category\":\"boundary_omission\","
            "\"locating\":\"x\",\"evidence\":\"y\",\"suggestion\":\"z\"}],"
            "\"confidence\":null,\"confidence_method\":null,\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_uf", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "undetermined w/ findings import ok");
        std::ifstream wf(run / "predictions" / "t_uf.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "semantic_invalid", "undetermined+findings -> semantic_invalid");
    }

    // ---- 15. perfect 'correct' parse -> parsed, prediction present, no gold ----
    {
        auto run = makeRunDir(tmp.string(), "t_ok_c", false);
        std::string s = "{\"trace_id\":\"t_ok_c\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ok_c", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "correct parsed import ok");
        std::ifstream wf(run / "predictions" / "t_ok_c.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "parsed", "correct -> parsed");
        CHECK(w.at("prediction").is_object(), "prediction object on parsed");
        CHECK(!w.contains("diagnoses") && !w.at("prediction").contains("diagnoses"),
              "no gold leakage in wrapper");
        CHECK(w.at("prediction").at("status") == "correct", "prediction status correct");
    }

    // ---- 16. perfect 'incorrect' parse -> parsed ----
    {
        auto run = makeRunDir(tmp.string(), "t_ok_i", false);
        std::string s = "{\"trace_id\":\"t_ok_i\",\"status\":\"incorrect\","
            "\"primary_category\":\"boundary_omission\","
            "\"findings\":[{\"stage\":\"boundary\",\"category\":\"boundary_omission\","
            "\"locating\":\"step 3\",\"evidence\":\"missing edge x=0\",\"suggestion\":\"add check\"}],"
            "\"confidence\":null,\"confidence_method\":null,\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ok_i", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "incorrect parsed import ok");
        std::ifstream wf(run / "predictions" / "t_ok_i.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "parsed", "incorrect -> parsed");
        CHECK(w.at("prediction").at("primary_category") == "boundary_omission",
              "primary category preserved");
    }

    // ---- 17. implementation_consistency without candidate -> semantic_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_ics", false); // no candidate
        std::string s = "{\"trace_id\":\"t_ics\",\"status\":\"incorrect\","
            "\"primary_category\":\"implementation_mismatch\","
            "\"findings\":[{\"stage\":\"implementation_consistency\","
            "\"category\":\"implementation_mismatch\",\"locating\":\"x\",\"evidence\":\"y\","
            "\"suggestion\":\"z\"}],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ics", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "ics-no-candidate import ok");
        std::ifstream wf(run / "predictions" / "t_ics.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "semantic_invalid",
              "implementation_consistency without candidate -> semantic_invalid");
    }

    // ---- 18. finding schema failures -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_finding_schema", false);
        std::string s = "{\"trace_id\":\"t_finding_schema\",\"status\":\"incorrect\","
            "\"primary_category\":\"boundary_omission\","
            "\"findings\":[{\"stage\":\"boundary\",\"category\":\"boundary_omission\","
            "\"locating\":\"x\",\"evidence\":\"y\"}],\"confidence\":null,"
            "\"confidence_method\":null,\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_finding_schema",
                                          (tmp / "raw.txt").string(), "run_x",
                                          "2026-08-24T00:00:00Z");
        CHECK(r.ok, "finding-schema import ok");
        std::ifstream wf(run / "predictions" / "t_finding_schema.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid",
              "finding missing required key -> schema_invalid");
    }

    // ---- 19. implementation_consistency WITH candidate -> parsed ----
    {
        auto run = makeRunDir(tmp.string(), "t_ics2", true); // with candidate
        std::string s = "{\"trace_id\":\"t_ics2\",\"status\":\"incorrect\","
            "\"primary_category\":\"implementation_mismatch\","
            "\"findings\":[{\"stage\":\"implementation_consistency\","
            "\"category\":\"implementation_mismatch\",\"locating\":\"x\",\"evidence\":\"y\","
            "\"suggestion\":\"z\"}],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_ics2", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "ics-with-candidate import ok");
        std::ifstream wf(run / "predictions" / "t_ics2.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "parsed", "ics with candidate -> parsed");
    }

    // ---- 20. exporter/importer candidate association integration ----
    if (argc >= 2) {
        const fs::path dataDir = fs::path(argv[1]);
        const fs::path templatePath = dataDir.parent_path() / "prompts" /
                                      "hy3-evaluator-v1.md";
        std::ifstream templateFile(templatePath, std::ios::binary);
        std::string templateText((std::istreambuf_iterator<char>(templateFile)),
                                 std::istreambuf_iterator<char>());
        const fs::path run = tmp / "run_exporter_integration";
        RunManifest manifest;
        manifest.run_id = "importer-integration";
        manifest.pipeline_commit = "test";
        manifest.started_at = "2026-08-24T00:00:00Z";
        std::string templateSha;
        ExporterResult exported = exportPrompts(dataDir.string(), templateText,
                                                 run.string(), manifest, templateSha);
        CHECK(exported.ok, "real exporter prompt integration setup");
        if (exported.ok) {
            auto implementationResponse = [](const std::string& traceId) {
                return "{\"trace_id\":\"" + traceId +
                    "\",\"status\":\"incorrect\","
                    "\"primary_category\":\"implementation_mismatch\","
                    "\"findings\":[{\"stage\":\"implementation_consistency\","
                    "\"category\":\"implementation_mismatch\",\"locating\":\"x\","
                    "\"evidence\":\"y\",\"suggestion\":\"z\"}],"
                    "\"confidence\":null,\"confidence_method\":null,"
                    "\"calibration_version\":null}";
            };

            writeRawFile(tmp / "no_candidate.raw",
                         implementationResponse("cf_160A_t1"));
            ImporterResult noCandidate = importResponse(
                run.string(), "cf_160A_t1", (tmp / "no_candidate.raw").string(),
                "importer-integration", "2026-08-24T00:01:00Z");
            CHECK(noCandidate.ok, "import real prompt without candidate");
            std::ifstream noCandidateFile(run / "predictions" /
                                          "cf_160A_t1.json");
            nlohmann::json noCandidateWrapper; noCandidateFile >> noCandidateWrapper;
            CHECK(noCandidateWrapper.at("parse_status") == "semantic_invalid",
                  "real null candidate rejects implementation finding");

            writeRawFile(tmp / "with_candidate.raw",
                         implementationResponse("cf_160A_t3"));
            ImporterResult withCandidate = importResponse(
                run.string(), "cf_160A_t3", (tmp / "with_candidate.raw").string(),
                "importer-integration", "2026-08-24T00:01:00Z");
            CHECK(withCandidate.ok, "import real prompt with candidate");
            std::ifstream withCandidateFile(run / "predictions" /
                                            "cf_160A_t3.json");
            nlohmann::json withCandidateWrapper; withCandidateFile >> withCandidateWrapper;
            CHECK(withCandidateWrapper.at("parse_status") == "parsed",
                  "real candidate object permits implementation finding");
        }
    }

    // ---- 21. trace_id mismatch -> schema_invalid ----
    {
        auto run = makeRunDir(tmp.string(), "t_tm", false);
        std::string s = "{\"trace_id\":\"DIFFERENT\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r = importResponse(run.string(), "t_tm", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "trace_id mismatch import ok");
        std::ifstream wf(run / "predictions" / "t_tm.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "schema_invalid", "trace_id mismatch -> schema_invalid");
    }

    // ---- 20. raw response byte hash determinism (no normalization) ----
    {
        std::string content = "{\"trace_id\":\"x\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}\r\n"; // has CRLF
        std::vector<uint8_t> bytes(content.begin(), content.end());
        std::string h1 = sha256_hex(bytes);
        std::string h2 = sha256_hex(bytes);
        CHECK(h1 == h2, "raw hash deterministic");
        // NIST-style known vector sanity: empty
        CHECK(sha256_hex(std::vector<uint8_t>{}) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256 empty vector");
        // The raw file saved must reproduce the same hash.
        auto run = makeRunDir(tmp.string(), "t_rh", false);
        writeRawFile(tmp / "raw.txt", content);
        ImporterResult r = importResponse(run.string(), "t_rh", (tmp / "raw.txt").string(),
                                          "run_x", "2026-08-24T00:00:00Z");
        CHECK(r.ok, "raw hash import ok");
        std::ifstream wf(run / "predictions" / "t_rh.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("raw_response_sha256") == h1, "raw_response_sha256 matches verbatim bytes");
    }

    // ---- 21. refuse overwrite of raw / prediction ----
    {
        auto run = makeRunDir(tmp.string(), "t_ow", false);
        std::string s = "{\"trace_id\":\"t_ow\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        ImporterResult r1 = importResponse(run.string(), "t_ow", (tmp / "raw.txt").string(),
                                           "run_x", "2026-08-24T00:00:00Z");
        CHECK(r1.ok, "first import ok");
        // With both artifacts present, prediction preflight wins before any
        // raw mutation is attempted.
        ImporterResult r2 = importResponse(run.string(), "t_ow", (tmp / "raw.txt").string(),
                                           "run_x", "2026-08-24T00:00:00Z");
        CHECK(!r2.ok && r2.error_code == importer_errc::E_PREDICTION_EXISTS,
              "refuse overwrite when prediction already exists");

        auto rawOnlyRun = makeRunDir(tmp.string(), "t_raw_only", false);
        writeRawFile(rawOnlyRun / "raw-responses" / "t_raw_only.txt", "existing");
        ImporterResult rawOnly = importResponse(
            rawOnlyRun.string(), "t_raw_only", (tmp / "raw.txt").string(),
            "run_x", "2026-08-24T00:00:00Z");
        CHECK(!rawOnly.ok && rawOnly.error_code == importer_errc::E_RAW_EXISTS,
              "refuse overwrite when only raw response exists");
    }

    // ---- 22. explicit mark-not-attempted ----
    {
        auto run = makeRunDir(tmp.string(), "t_na", false);
        ImporterResult r = markNotAttempted(run.string(), "t_na", "run_x",
                                            "2026-08-24T00:00:00Z");
        CHECK(r.ok, "markNotAttempted ok");
        std::ifstream wf(run / "predictions" / "t_na.json");
        nlohmann::json w; wf >> w;
        CHECK(w.at("parse_status") == "model_call_not_attempted", "not_attempted status");
        CHECK(w.at("raw_response_sha256").is_null(), "raw hash null when not attempted");
        CHECK(w.at("prediction").is_null(), "prediction null");
    }

    // ---- 23. absence of raw file is NOT silently inferred as not_attempted ----
    {
        auto run = makeRunDir(tmp.string(), "t_abs", false);
        // No import, no mark. Prediction file must not exist.
        CHECK(!fs::exists(run / "predictions" / "t_abs.json"),
              "missing file not auto-created as not_attempted");
    }

    // ---- 24. output determinism: two identical imports produce identical wrapper ----
    {
        // Same trace_id + identical response, but in two distinct parent dirs
        // (re-importing the same trace is refused to avoid overwrite).
        fs::path baseA = tmp / "detA";
        fs::path baseB = tmp / "detB";
        fs::create_directories(baseA, ec);
        fs::create_directories(baseB, ec);
        auto runA = makeRunDir(baseA.string(), "t_det", false);
        auto runB = makeRunDir(baseB.string(), "t_det", false);
        std::string s = "{\"trace_id\":\"t_det\",\"status\":\"correct\",\"primary_category\":null,"
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "rawA.txt", s);
        writeRawFile(tmp / "rawB.txt", s);
        importResponse(runA.string(), "t_det", (tmp / "rawA.txt").string(),
                       "run_x", "2026-08-24T00:00:00Z");
        importResponse(runB.string(), "t_det", (tmp / "rawB.txt").string(),
                       "run_x", "2026-08-24T00:00:00Z");
        std::ifstream a(runA / "predictions" / "t_det.json");
        std::ifstream b(runB / "predictions" / "t_det.json");
        nlohmann::json wa, wb; a >> wa; b >> wb;
        CHECK(wa.dump() == wb.dump(), "deterministic wrapper output");
    }

    // ---- 25. sentinel never written to file ----
    {
        // Internally __parse_failed__ is only in reporter memory; check a
        // semantic_invalid wrapper doesn't contain it.
        auto run = makeRunDir(tmp.string(), "t_snt", false);
        std::string s = "{\"trace_id\":\"t_snt\",\"status\":\"correct\",\"primary_category\":\"x\","
            "\"findings\":[],\"confidence\":null,\"confidence_method\":null,"
            "\"calibration_version\":null}";
        writeRawFile(tmp / "raw.txt", s);
        importResponse(run.string(), "t_snt", (tmp / "raw.txt").string(),
                       "run_x", "2026-08-24T00:00:00Z");
        std::ifstream wf(run / "predictions" / "t_snt.json");
        std::string content((std::istreambuf_iterator<char>(wf)),
                            std::istreambuf_iterator<char>());
        CHECK(content.find("__parse_failed__") == std::string::npos,
              "sentinel absent from file");
    }

    fs::remove_all(tmp, ec);

    std::cout << "prediction_importer_tests: " << g_pass << " passed, "
              << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
