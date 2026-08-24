// hy3_algotrace — Reporter unit tests (Phase 2B)
//
// SYNTHETIC_TEST_FIXTURE: hand-built run directories + a tiny synthetic dataset.
// Exercises metric computation per docs/phase-02-metrics.md, parse-failure
// scoring, dedup, zero-denominator rules, gold isolation, missing-wrapper
// incomplete reporting, and report.json/report.md consistency.

#include "hy3_algotrace/reporter.hpp"
#include "hy3_algotrace/prediction_importer.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace hy3;

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (cond) g_pass++;                                                   \
        else { g_fail++;                                                      \
               std::cerr << "FAIL: " << (msg) << " [" << __FILE__ << ":"     \
                         << __LINE__ << "]\n"; }                              \
    } while (0)

namespace {

// Create a synthetic dataset: one problem with two traces, one correct + one
// incorrect (with two findings of same category to test dedup).
fs::path makeDataset(const fs::path& base) {
    fs::path data = base / "data";
    fs::create_directories(data / "problems", std::error_code{});
    // manifest
    nlohmann::json man;
    man["dataset_version"] = "syn";
    man["problem_ids"] = nlohmann::json::array({"p1"});
    std::ofstream(base / "data" / "manifest.json") << man.dump(2);
    // problem with gold diagnoses for t_c (correct) and t_i (incorrect)
    nlohmann::json prob;
    prob["problem"] = {{"id", "p1"}};
    prob["reasoning_traces"] = nlohmann::json::array({
        {{"id", "t_c"}, {"problem_id", "p1"}},
        {{"id", "t_i"}, {"problem_id", "p1"}}});
    prob["diagnoses"] = nlohmann::json::array({
        {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr}, {"findings", nlohmann::json::array()}},
        {{"trace_id", "t_i"}, {"status", "incorrect"}, {"primary_category", "boundary_omission"},
         {"findings", nlohmann::json::array({
             {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "x"}, {"evidence", "y"}, {"suggestion", "z"}},
             {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "a"}, {"evidence", "b"}, {"suggestion", "c"}} // dup category
         })}}});
    std::ofstream(data / "problems" / "p1.json") << prob.dump(2);
    return data;
}

// Create a run dir with manifest + predictions. predictions is a map tid->wrapper json.
fs::path makeRun(const fs::path& base, const nlohmann::json& manifestExtra,
                 const std::map<std::string, nlohmann::json>& wrappers) {
    fs::path run = base / "run";
    fs::create_directories(run / "predictions", std::error_code{});
    nlohmann::json man;
    man["evaluation_schema_version"] = "0.1.0";
    man["run_id"] = "syn_run";
    man["dataset_version"] = "syn";
    man["dataset_commit"] = "deadbeef";
    man["taxonomy_version"] = "1.0.0";
    man["model_provider"] = "tencent-hunyuan";
    man["model_name"] = "hy3";
    man["model_version"] = nullptr;
    man["pipeline_commit"] = "abc";
    man["prompt_template_id"] = "hy3-evaluator-v1";
    man["prompt_template_sha256"] = "x";
    man["input_mode"] = "reference_assisted";
    man["started_at"] = "2026-08-24T00:00:00Z";
    man["completed_at"] = nullptr;
    man["trace_ids"] = nlohmann::json::array({"t_c", "t_i"});
    man["total_traces"] = 2;
    man["notes"] = "synthetic";
    for (auto it = manifestExtra.begin(); it != manifestExtra.end(); ++it)
        man[it.key()] = it.value();
    std::ofstream(run / "run-manifest.json") << man.dump(2);
    for (const auto& kv : wrappers) {
        std::ofstream(run / "predictions" / (kv.first + ".json")) << kv.second.dump(2);
    }
    return run;
}

nlohmann::json wrapper(ParseStatus ps, const nlohmann::json& pred,
                       const std::string& rawSha = "null") {
    nlohmann::json w;
    w["evaluation_schema_version"] = "0.1.0";
    w["run_id"] = "syn_run";
    w["trace_id"] = "";
    w["model_name"] = "hy3";
    w["prompt_template_id"] = "hy3-evaluator-v1";
    w["input_mode"] = "reference_assisted";
    w["prompt_sha256"] = "x";
    w["raw_response_sha256"] = rawSha;
    std::string name = "parsed";
    switch (ps) {
        case ParseStatus::ModelCallNotAttempted: name = "model_call_not_attempted"; break;
        case ParseStatus::EmptyResponse: name = "empty_response"; break;
        case ParseStatus::InvalidJson: name = "invalid_json"; break;
        case ParseStatus::SchemaInvalid: name = "schema_invalid"; break;
        case ParseStatus::SemanticInvalid: name = "semantic_invalid"; break;
        case ParseStatus::Parsed: name = "parsed"; break;
    }
    w["parse_status"] = name;
    if (ps == ParseStatus::Parsed) w["prediction"] = pred;
    else w["prediction"] = nullptr;
    w["errors"] = nlohmann::json::array();
    w["generated_at"] = nullptr;
    return w;
}

} // namespace

int main() {
    fs::path tmp = fs::temp_directory_path() / "hy3_rep_tests";
    fs::remove_all(tmp, std::error_code{});
    fs::create_directories(tmp, std::error_code{});
    fs::path data = makeDataset(tmp);

    // ---- Case A: perfect predictions for both traces ----
    {
        nlohmann::json wC = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr},
             {"findings", nlohmann::json::array()}, {"confidence", nullptr},
             {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wC["trace_id"] = "t_c";
        nlohmann::json wI = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_i"}, {"status", "incorrect"}, {"primary_category", "boundary_omission"},
             {"findings", nlohmann::json::array({
                 {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "x"},
                  {"evidence", "y"}, {"suggestion", "z"}}})},
             {"confidence", nullptr}, {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wI["trace_id"] = "t_i";
        fs::path run = makeRun(tmp, {}, {{"t_c", wC}, {"t_i", wI}});
        nlohmann::json rep;
        bool complete = false;
        ReporterResult rr = buildReport(run.string(), data.string(), "2026-08-24T01:00:00Z",
                                        "2026-08-24T01:00:00Z", rep, complete);
        CHECK(rr.ok, "caseA build ok");
        CHECK(complete, "caseA complete");
        const auto& m = rep.at("metrics");
        CHECK(m.value("parse_success_rate", 0.0) == 1.0, "caseA parse_success_rate=1");
        CHECK(m.value("status_accuracy", 0.0) == 1.0, "caseA status_accuracy=1");
        // primary_category_accuracy: 1 incorrect gold, predicted correctly -> 1
        CHECK(std::abs(m.at("primary_category_accuracy").get<double>() - 1.0) < 1e-9,
              "caseA primary_accuracy=1");
        // micro f1: t_c gold empty pred empty -> no contribution; t_i G={b_o}, P={b_o}
        // -> TP=1, FP=0, FN=0 -> P=1,R=1,F1=1. macro: t_c empty/empty=1, t_i=1 -> 1
        CHECK(std::abs(m.at("finding_category_macro_F1").get<double>() - 1.0) < 1e-9,
              "caseA macro_F1=1");
        CHECK(std::abs(m.at("finding_category_micro").at("f1").get<double>() - 1.0) < 1e-9,
              "caseA micro_f1=1");
        // gold never in prediction files (indirect: wrapper built by us, but check report
        // doesn't embed gold inside prediction field)
        bool predHasGold = false;
        for (const auto& t : rep.at("per_trace")) {
            if (t.contains("prediction") && t.at("prediction").is_object() &&
                t.at("prediction").contains("diagnoses")) predHasGold = true;
        }
        CHECK(!predHasGold, "caseA no gold in prediction field");

        // consistency: report.md numbers match JSON
        std::string md = renderReportMarkdown(rep);
        CHECK(md.find("parse_success_rate: 1.") != std::string::npos,
              "caseA md shows parse_success_rate");
        CHECK(md.find("status_accuracy: 1.") != std::string::npos,
              "caseA md shows status_accuracy");
    }

    // ---- Case B: t_i parse failure (invalid_json) -> counted as failure ----
    {
        nlohmann::json wC = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr},
             {"findings", nlohmann::json::array()}, {"confidence", nullptr},
             {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wC["trace_id"] = "t_c";
        nlohmann::json wI = wrapper(ParseStatus::InvalidJson, nullptr);
        wI["trace_id"] = "t_i";
        fs::path run = makeRun(tmp, {}, {{"t_c", wC}, {"t_i", wI}});
        nlohmann::json rep;
        bool complete = false;
        ReporterResult rr = buildReport(run.string(), data.string(), "2026-08-24T01:00:00Z",
                                        "2026-08-24T01:00:00Z", rep, complete);
        CHECK(rr.ok, "caseB build ok");
        const auto& m = rep.at("metrics");
        CHECK(m.value("parse_success_rate", 0.0) == 0.5, "caseB parse_success_rate=0.5");
        CHECK(m.value("status_accuracy", 0.0) == 0.5, "caseB status_accuracy=0.5 (t_i fail)");
        // macro F1: t_c=1, t_i parse failed=0 -> 0.5
        CHECK(std::abs(m.at("finding_category_macro_F1").get<double>() - 0.5) < 1e-9,
              "caseB macro_F1=0.5");
        // micro: t_i sentinel FP + FN(b_o) -> FP=1,FN=1,TP=0 -> P=0,R=0,F1=0
        CHECK(std::abs(m.at("finding_category_micro").at("f1").get<double>() - 0.0) < 1e-9,
              "caseB micro_f1=0");
    }

    // ---- Case C: missing wrapper -> run incomplete, not defaulted correct ----
    {
        nlohmann::json wC = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr},
             {"findings", nlohmann::json::array()}, {"confidence", nullptr},
             {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wC["trace_id"] = "t_c";
        // t_i wrapper absent
        fs::path run = makeRun(tmp, {}, {{"t_c", wC}});
        nlohmann::json rep;
        bool complete = true;
        ReporterResult rr = buildReport(run.string(), data.string(), "2026-08-24T01:00:00Z",
                                        "2026-08-24T01:00:00Z", rep, complete);
        CHECK(rr.ok, "caseC build ok");
        CHECK(!complete, "caseC run NOT complete (missing wrapper)");
        CHECK(rep.at("run_complete") == false, "caseC run_complete false");
        const auto& m = rep.at("metrics");
        // status_accuracy counts missing as failure
        CHECK(m.value("status_accuracy", 0.0) == 0.5, "caseC status_accuracy=0.5");
    }

    // ---- Case D: dedup — t_i predicted with same category twice ----
    {
        nlohmann::json wC = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr},
             {"findings", nlohmann::json::array()}, {"confidence", nullptr},
             {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wC["trace_id"] = "t_c";
        // predicted with boundary_omission TWICE (should dedup to 1)
        nlohmann::json wI = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_i"}, {"status", "incorrect"}, {"primary_category", "boundary_omission"},
             {"findings", nlohmann::json::array({
                 {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "x"},
                  {"evidence", "y"}, {"suggestion", "z"}},
                 {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "a"},
                  {"evidence", "b"}, {"suggestion", "c"}}})},
             {"confidence", nullptr}, {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wI["trace_id"] = "t_i";
        fs::path run = makeRun(tmp, {}, {{"t_c", wC}, {"t_i", wI}});
        nlohmann::json rep;
        bool complete = false;
        ReporterResult rr = buildReport(run.string(), data.string(), "2026-08-24T01:00:00Z",
                                        "2026-08-24T01:00:00Z", rep, complete);
        CHECK(rr.ok, "caseD build ok");
        const auto& m = rep.at("metrics");
        // dedup: G={b_o}, P={b_o} after dedup -> TP=1,FP=0,FN=0 -> F1=1
        CHECK(std::abs(m.at("finding_category_micro").at("f1").get<double>() - 1.0) < 1e-9,
              "caseD micro_f1=1 (dedup)");
    }

    // ---- Case E: completed_at update only when complete ----
    {
        nlohmann::json wC = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_c"}, {"status", "correct"}, {"primary_category", nullptr},
             {"findings", nlohmann::json::array()}, {"confidence", nullptr},
             {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wC["trace_id"] = "t_c";
        nlohmann::json wI = wrapper(ParseStatus::Parsed,
            {{"trace_id", "t_i"}, {"status", "incorrect"}, {"primary_category", "boundary_omission"},
             {"findings", nlohmann::json::array({
                 {{"stage", "boundary"}, {"category", "boundary_omission"}, {"locating", "x"},
                  {"evidence", "y"}, {"suggestion", "z"}}})},
             {"confidence", nullptr}, {"confidence_method", nullptr}, {"calibration_version", nullptr}});
        wI["trace_id"] = "t_i";
        fs::path run = makeRun(tmp, {}, {{"t_c", wC}, {"t_i", wI}});
        ReporterResult rr = generateReport(run.string(), data.string(),
                                           "2026-08-24T02:00:00Z", "2026-08-24T02:00:00Z");
        CHECK(rr.ok, "caseE generate ok");
        CHECK(rr.run_complete, "caseE complete");
        // read manifest completed_at
        std::ifstream mf(run / "run-manifest.json");
        nlohmann::json man; mf >> man;
        CHECK(man.value("completed_at", "") == "2026-08-24T02:00:00Z",
              "caseE completed_at updated");
    }

    fs::remove_all(tmp, std::error_code{});
    std::cout << "reporter_tests: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
