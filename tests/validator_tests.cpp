// hy3_algotrace — validator self-tests (Phase 1B).
//
// A tiny, dependency-free test harness (no Catch2 / GTest / Python). Each test
// builds a dataset in a throwaway temp directory, runs the real validator, and
// asserts on the stable error CODE (not just "it failed"). The canonical
// Phase 1A dataset is also validated when its path is supplied (argv[1], or
// ../data, or data relative to CWD).

#include "hy3_algotrace/validator.hpp"
#include "hy3_algotrace/diagnostic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string g_realDataDir;  // optional; set from argv[1]
static int g_counter = 0;

// ----- dataset builders -----------------------------------------------------
static json validProblem(const std::string& pid) {
    json p;
    p["meta"] = json::object({
        {"schema_version", "0.3.0"}, {"taxonomy_version", "1.0.0"},
        {"source_reference", "x"}, {"dataset_version", "phase1a-pilot-001"},
        {"created_at", "2026-08-23"}});
    p["problem"] = json::object({
        {"id", pid}, {"source", "codeforces"}, {"title", "t"},
        {"statement", "s"}, {"constraints", json::object()},
        {"algorithm_type", "greedy"}, {"reference_tags", json::array({"greedy"})}});
    p["reference_verdict"] = json::object({
        {"problem_id", pid}, {"expected_choice", "c"}, {"expected_proof", "p"},
        {"expected_complexity", "O(1)"}, {"expected_boundaries", json::array({"b"})},
        {"common_wrong_strategy_counterexample", "none"}});
    p["test_cases"] = json::array({json::object({
        {"id", pid + "_c1"}, {"problem_id", pid}, {"input", "1\n"},
        {"expected_output", "1\n"}, {"origin", "official_sample"},
        {"purpose", "normal"}, {"notes", "n"}})});
    json trace = json::object();
    trace["id"] = pid + "_t1";
    trace["problem_id"] = pid;
    trace["author"] = "hy3";
    trace["trace_origin"] = "model_generated";
    trace["generator_model"] = "hy3";
    trace["annotator"] = "hy3_draft";
    trace["review_status"] = "pending_planner_review";
    trace["reviewer"] = nullptr;
    trace["reviewed_at"] = nullptr;
    json step = json::object();
    step["stage"] = "problem_understanding";
    step["text"] = "t";
    step["relies_on"] = json::array();
    trace["steps"] = json::array({step});
    trace["intended_outcome"] = "o";
    p["reasoning_traces"] = json::array({trace});
    p["candidate_solutions"] = json::array();
    p["diagnoses"] = json::array({json::object({
        {"id", pid + "_t1_d"}, {"trace_id", pid + "_t1"}, {"status", "correct"},
        {"primary_category", nullptr}, {"findings", json::array()},
        {"confidence", nullptr}, {"confidence_method", nullptr},
        {"calibration_version", nullptr}})});
    p["verification_results"] = json::array();
    return p;
}

static json validManifest(const std::string& pid) {
    json m;
    m["schema_version"] = "0.3.0";
    m["taxonomy_version"] = "1.0.0";
    m["dataset_version"] = "phase1a-pilot-001";
    m["problem_count"] = 1;
    m["trace_count"] = 1;
    m["problem_ids"] = json::array({pid});
    m["category_counts"] = json::object({
        {"problem_misunderstanding", 0}, {"wrong_greedy_choice", 0},
        {"missing_greedy_proof", 0}, {"invalid_greedy_proof", 0},
        {"complexity_error", 0}, {"boundary_omission", 0},
        {"implementation_mismatch", 0}});
    m["status_counts"] = json::object({
        {"correct", 1}, {"incorrect", 0}, {"undetermined", 0}});
    m["test_origin_counts"] = json::object({
        {"official_sample", 1}, {"manually_designed", 0},
        {"counterexample", 0}, {"generated", 0}});
    m["review_status"] = "pending_planner_review";
    m["generated_by"] = "hy3";
    m["annotator"] = "hy3_draft";
    m["created_at"] = "2026-08-23";
    m["reviewer"] = nullptr;
    m["reviewed_at"] = nullptr;
    m["notes"] = "test";
    return m;
}

// Write a dataset to a fresh temp dir and validate it.
static bool validateInTemp(const json& manifest,
                           const std::vector<json>& problems,
                           std::vector<hy3::Diagnostic>& out,
                           hy3::ValidationSummary& sum) {
    fs::path base = fs::temp_directory_path() /
                    ("hy3algo_test_" + std::to_string(++g_counter));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "problems", ec);
    {
        std::ofstream o(base / "manifest.json", std::ios::binary);
        o << manifest.dump(2);
    }
    for (const auto& p : problems) {
        std::string pid = p.at("problem").at("id").get<std::string>();
        std::ofstream o(base / "problems" / (pid + ".json"), std::ios::binary);
        o << p.dump(2);
    }
    bool ok = hy3::validateDataset(base.string(), out, sum);
    fs::remove_all(base, ec);
    return ok;
}

// Write a dataset where the first problem file has raw (possibly invalid) text.
static bool validateRawProblem(const json& manifest,
                               const std::string& rawProblem,
                               std::vector<hy3::Diagnostic>& out,
                               hy3::ValidationSummary& sum) {
    fs::path base = fs::temp_directory_path() /
                    ("hy3algo_test_" + std::to_string(++g_counter));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "problems", ec);
    {
        std::ofstream o(base / "manifest.json", std::ios::binary);
        o << manifest.dump(2);
    }
    {
        std::ofstream o(base / "problems" / "p1.json", std::ios::binary);
        o << rawProblem;
    }
    bool ok = hy3::validateDataset(base.string(), out, sum);
    fs::remove_all(base, ec);
    return ok;
}

static bool hasCode(const std::vector<hy3::Diagnostic>& d, const std::string& code) {
    for (const auto& x : d)
        if (x.code == code) return true;
    return false;
}

// ----- test helpers ----------------------------------------------------------
static int g_pass = 0, g_fail = 0;
static void check(const std::string& name, bool cond) {
    if (cond) {
        g_pass++;
        std::cout << "[PASS] " << name << "\n";
    } else {
        g_fail++;
        std::cout << "[FAIL] " << name << "\n";
    }
}

// negative test: expects validation to FAIL and the given code to be present.
static bool neg(const json& manifest, const std::vector<json>& problems,
                const std::string& expect) {
    std::vector<hy3::Diagnostic> out;
    hy3::ValidationSummary sum;
    bool ok = validateInTemp(manifest, problems, out, sum);
    return (!ok) && hasCode(out, expect);
}

int main(int argc, char** argv) {
    if (argc > 1) g_realDataDir = argv[1];
    else if (fs::is_directory("../data")) g_realDataDir = "../data";
    else if (fs::is_directory("data")) g_realDataDir = "data";

    const std::string PID = "tpl_p1";

    // 1. Phase 1A real data -> PASS
    if (!g_realDataDir.empty()) {
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = hy3::validateDataset(g_realDataDir, out, sum);
        check("1. real Phase 1A data validates PASS", ok && out.empty());
    } else {
        std::cout << "[SKIP] 1. real Phase 1A data (path not provided)\n";
    }

    // 2. JSON syntax corruption -> E_JSON_PARSE
    {
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateRawProblem(validManifest(PID),
                                     "{ this is : not valid json ,,, ", out, sum);
        check("2. corrupted JSON -> E_JSON_PARSE",
              (!ok) && hasCode(out, hy3::errc::E_JSON_PARSE));
    }

    // 3. missing required top-level key -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p.erase("diagnoses");
        check("3. missing top-level key -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 4. duplicate trace.id -> E_DUPLICATE_ID
    {
        json p = validProblem(PID);
        json t2 = p["reasoning_traces"][0];
        t2["id"] = PID + "_t1";  // same id as the existing trace
        p["reasoning_traces"].push_back(t2);
        check("4. duplicate trace.id -> E_DUPLICATE_ID",
              neg(validManifest(PID), {p}, hy3::errc::E_DUPLICATE_ID));
    }

    // 5. diagnosis.trace_id unresolved -> E_BAD_TRACE_FK
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["trace_id"] = "missing_trace";
        check("5. diagnosis.trace_id unresolved -> E_BAD_TRACE_FK",
              neg(validManifest(PID), {p}, hy3::errc::E_BAD_TRACE_FK));
    }

    // 6. one trace with two diagnoses -> E_DIAGNOSIS_CARDINALITY
    {
        json p = validProblem(PID);
        json d2 = p["diagnoses"][0];
        d2["id"] = PID + "_t1_d2";  // distinct id, same trace
        p["diagnoses"].push_back(d2);
        check("6. trace with 2 diagnoses -> E_DIAGNOSIS_CARDINALITY",
              neg(validManifest(PID), {p}, hy3::errc::E_DIAGNOSIS_CARDINALITY));
    }

    // 7. correct diagnosis with findings -> E_CORRECT_WITH_FINDINGS
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["findings"] = json::array({json::object({
            {"stage", "boundary"}, {"category", "boundary_omission"},
            {"locating", "x"}, {"evidence", "y"}, {"suggestion", "z"}})});
        check("7. correct with findings -> E_CORRECT_WITH_FINDINGS",
              neg(validManifest(PID), {p}, hy3::errc::E_CORRECT_WITH_FINDINGS));
    }

    // 8. incorrect without findings -> E_INCORRECT_WITHOUT_FINDINGS
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "incorrect";
        p["diagnoses"][0]["primary_category"] = "wrong_greedy_choice";
        p["diagnoses"][0]["findings"] = json::array();
        check("8. incorrect without findings -> E_INCORRECT_WITHOUT_FINDINGS",
              neg(validManifest(PID), {p}, hy3::errc::E_INCORRECT_WITHOUT_FINDINGS));
    }

    // 9. primary_category not in findings -> E_PRIMARY_NOT_IN_FINDINGS
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "incorrect";
        p["diagnoses"][0]["primary_category"] = "wrong_greedy_choice";
        p["diagnoses"][0]["findings"] = json::array({json::object({
            {"stage", "boundary"}, {"category", "boundary_omission"},
            {"locating", "x"}, {"evidence", "y"}, {"suggestion", "z"}})});
        check("9. primary not in findings -> E_PRIMARY_NOT_IN_FINDINGS",
              neg(validManifest(PID), {p}, hy3::errc::E_PRIMARY_NOT_IN_FINDINGS));
    }

    // 10. implementation_consistency without candidate_solution
    //     -> E_IMPLEMENTATION_WITHOUT_SOLUTION
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"].push_back(json::object({
            {"stage", "implementation_consistency"}, {"text", "x"},
            {"relies_on", json::array()}}));
        check("10. impl step w/o solution -> E_IMPLEMENTATION_WITHOUT_SOLUTION",
              neg(validManifest(PID), {p}, hy3::errc::E_IMPLEMENTATION_WITHOUT_SOLUTION));
    }

    // 11. invalid enum (status) -> E_INVALID_ENUM
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "bogus_status";
        check("11. invalid status enum -> E_INVALID_ENUM",
              neg(validManifest(PID), {p}, hy3::errc::E_INVALID_ENUM));
    }

    // 12. manifest count tampered -> E_MANIFEST_COUNT_MISMATCH
    {
        json m = validManifest(PID);
        m["trace_count"] = 999;
        check("12. manifest count mismatch -> E_MANIFEST_COUNT_MISMATCH",
              neg(m, {validProblem(PID)}, hy3::errc::E_MANIFEST_COUNT_MISMATCH));
    }

    // 13. confidence non-null -> E_UNCALIBRATED_CONFIDENCE
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["confidence"] = 0.9;
        check("13. non-null confidence -> E_UNCALIBRATED_CONFIDENCE",
              neg(validManifest(PID), {p}, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // 14. forged verification_result (Phase 1A) -> E_UNEXPECTED_VERIFICATION_RESULT
    {
        json p = validProblem(PID);
        p["verification_results"] = json::array({json::object({
            {"solution_id", "x"}, {"test_id", PID + "_c1"}, {"actual_output", nullptr},
            {"verdict", "pass"}, {"runtime_ms", nullptr},
            {"finding_ref", nullptr}})});
        check("14. forged verification_result -> E_UNEXPECTED_VERIFICATION_RESULT",
              neg(validManifest(PID), {p}, hy3::errc::E_UNEXPECTED_VERIFICATION_RESULT));
    }

    // 15. legal zero counts (generated:0, undetermined:0) must NOT false-positive
    {
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {validProblem(PID)}, out, sum);
        check("15. legal zero counts -> PASS (no false positive)", ok && out.empty());
    }

    // 16. a trace WITH a candidate solution + impl step must NOT false-positive
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"].push_back(json::object({
            {"stage", "implementation_consistency"}, {"text", "consistent"},
            {"relies_on", json::array()}}));
        p["candidate_solutions"] = json::array({json::object({
            {"id", PID + "_sol1"}, {"trace_id", PID + "_t1"}, {"language", "cpp"},
            {"standard", "c++17"}, {"source_code", "int main(){}"},
            {"execution_status", "not_run"}})});
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("16. solution+impl step -> PASS (no false positive)",
              ok && !hasCode(out, hy3::errc::E_IMPLEMENTATION_WITHOUT_SOLUTION));
    }

    // ----------------------------------------------------------------------
    // Fix round (2026-08-23, planning-party review) additions
    // ----------------------------------------------------------------------

    // 17. cross-file duplicate reasoning_trace.id -> E_DUPLICATE_ID
    {
        json p1 = validProblem("x_p1");
        json p2 = validProblem("x_p2");
        // force p2's trace id to equal p1's trace id (x_p1_t1)
        p2["reasoning_traces"][0]["id"] = "x_p1_t1";
        check("17. cross-file duplicate trace.id -> E_DUPLICATE_ID",
              neg(validManifest("x_p1"), {p1, p2}, hy3::errc::E_DUPLICATE_ID));
    }

    // 18. cross-file duplicate diagnosis.id -> E_DUPLICATE_ID
    {
        json p1 = validProblem("x_p1");
        json p2 = validProblem("x_p2");
        p2["diagnoses"][0]["id"] = "x_p1_t1_d";
        check("18. cross-file duplicate diagnosis.id -> E_DUPLICATE_ID",
              neg(validManifest("x_p1"), {p1, p2}, hy3::errc::E_DUPLICATE_ID));
    }

    // 19. cross-file duplicate test_case.id -> E_DUPLICATE_ID
    {
        json p1 = validProblem("x_p1");
        json p2 = validProblem("x_p2");
        p2["test_cases"][0]["id"] = "x_p1_c1";
        check("19. cross-file duplicate test_case.id -> E_DUPLICATE_ID",
              neg(validManifest("x_p1"), {p1, p2}, hy3::errc::E_DUPLICATE_ID));
    }

    // 20. cross-file duplicate candidate_solution.id -> E_DUPLICATE_ID
    {
        json p1 = validProblem("x_p1");
        json p2 = validProblem("x_p2");
        // p2 needs a solution to duplicate against; give x_p1 one too
        json sol = json::object({
            {"id", PID + "_sol1"}, {"trace_id", PID + "_t1"}, {"language", "cpp"},
            {"standard", "c++17"}, {"source_code", "int main(){}"},
            {"execution_status", "not_run"}});
        p1["candidate_solutions"] = json::array({sol});
        p2["candidate_solutions"] = json::array({sol});
        p2["reasoning_traces"][0]["steps"].push_back(json::object({
            {"stage", "implementation_consistency"}, {"text", "c"},
            {"relies_on", json::array()}}));
        check("20. cross-file duplicate candidate_solution.id -> E_DUPLICATE_ID",
              neg(validManifest("x_p1"), {p1, p2}, hy3::errc::E_DUPLICATE_ID));
    }

    // 21. algorithm_type = "dp" -> E_INVALID_ENUM
    {
        json p = validProblem(PID);
        p["problem"]["algorithm_type"] = "dp";
        check("21. algorithm_type=dp -> E_INVALID_ENUM",
              neg(validManifest(PID), {p}, hy3::errc::E_INVALID_ENUM));
    }

    // 22. reasoning_trace missing steps -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0].erase("steps");
        check("22. trace missing steps -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 23. test_case missing input -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["test_cases"][0].erase("input");
        check("23. test_case missing input -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 24. candidate_solution missing source_code -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"].push_back(json::object({
            {"stage", "implementation_consistency"}, {"text", "c"},
            {"relies_on", json::array()}}));
        p["candidate_solutions"] = json::array({json::object({
            {"id", PID + "_sol1"}, {"trace_id", PID + "_t1"}, {"language", "cpp"},
            {"standard", "c++17"}, {"execution_status", "not_run"}})});
        check("24. candidate_solution missing source_code -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 25. step missing text -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"][0].erase("text");
        check("25. step missing text -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 26. finding missing evidence -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "incorrect";
        p["diagnoses"][0]["primary_category"] = "wrong_greedy_choice";
        p["diagnoses"][0]["findings"] = json::array({json::object({
            {"stage", "boundary"}, {"category", "boundary_omission"},
            {"locating", "x"}, {"suggestion", "z"}})});  // no evidence
        check("26. finding missing evidence -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 27. meta missing schema_version -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["meta"].erase("schema_version");
        check("27. meta missing schema_version -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 28. steps is not an array (nested type error) -> E_TYPE_MISMATCH
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"] = "not-an-array";
        check("28. steps not array -> E_TYPE_MISMATCH",
              neg(validManifest(PID), {p}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 29. manifest delete test_origin_counts.generated -> E_MISSING_KEY
    {
        json m = validManifest(PID);
        m["test_origin_counts"].erase("generated");
        check("29. manifest missing generated key -> E_MISSING_KEY",
              neg(m, {validProblem(PID)}, hy3::errc::E_MISSING_KEY));
    }

    // 30. manifest delete status_counts.undetermined -> E_MISSING_KEY
    {
        json m = validManifest(PID);
        m["status_counts"].erase("undetermined");
        check("30. manifest missing undetermined key -> E_MISSING_KEY",
              neg(m, {validProblem(PID)}, hy3::errc::E_MISSING_KEY));
    }

    // 31. manifest zero written as string -> E_TYPE_MISMATCH
    {
        json m = validManifest(PID);
        m["status_counts"]["undetermined"] = "0";
        check("31. manifest count as string -> E_TYPE_MISMATCH",
              neg(m, {validProblem(PID)}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 32. same-trace duplicate category counts dedup -> manifest count 1 PASS
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "incorrect";
        p["diagnoses"][0]["primary_category"] = "wrong_greedy_choice";
        p["diagnoses"][0]["findings"] = json::array({
            json::object({{"stage", "boundary"}, {"category", "wrong_greedy_choice"},
                          {"locating", "x"}, {"evidence", "y"}, {"suggestion", "z"}}),
            json::object({{"stage", "boundary"}, {"category", "wrong_greedy_choice"},
                          {"locating", "x2"}, {"evidence", "y2"}, {"suggestion", "z2"}})});
        json m = validManifest(PID);
        m["status_counts"] = json::object({
            {"correct", 0}, {"incorrect", 1}, {"undetermined", 0}});
        m["category_counts"]["wrong_greedy_choice"] = 1;  // deduped: 1 trace
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(m, {p}, out, sum);
        check("32. duplicate category in one trace -> count 1 PASS",
              ok && out.empty());
    }

    // 33. CTest data path cannot be silently skipped: real data must still PASS
    //     (mirrors add_test COMMAND passing "${CMAKE_CURRENT_SOURCE_DIR}/data").
    if (!g_realDataDir.empty()) {
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = hy3::validateDataset(g_realDataDir, out, sum);
        check("33. real data (explicit path) -> PASS (no skip)",
              ok && out.empty());
    } else {
        std::cout << "[SKIP] 33. real data explicit path (path not provided)\n";
    }

    // ----------------------------------------------------------------------
    // Phase 1B-R2 fix round additions
    // ----------------------------------------------------------------------

    // Helper: two problems where `refPid` owns a trace `refPid_t1`, and
    // `solPid` owns a candidate_solution whose trace_id points to that trace
    // (a legal cross-file reference). Returns {solProblem, refProblem}.
    auto makeCrossRef = [](const std::string& solPid,
                           const std::string& refPid) {
        json solP = validProblem(solPid);
        json refP = validProblem(refPid);
        // solP gets an extra candidate_solution referencing refP's trace.
        solP["candidate_solutions"] = json::array({json::object({
            {"id", solPid + "_solX"}, {"trace_id", refPid + "_t1"},
            {"language", "cpp"}, {"standard", "c++17"},
            {"source_code", "int main(){}"}, {"execution_status", "not_run"}})});
        return std::make_pair(solP, refP);
    };

    // 34. legal cross-file BACKWARD reference (target file read later) -> PASS
    {
        auto pr = makeCrossRef("m_ref_a", "m_ref_b");  // m_ref_a before m_ref_b
        json m = validManifest("m_ref_a");
        m["problem_count"] = 2;
        m["trace_count"] = 2;
        m["problem_ids"] = json::array({"m_ref_a", "m_ref_b"});
        m["status_counts"] = json::object({{"correct", 2}, {"incorrect", 0}, {"undetermined", 0}});
        m["test_origin_counts"]["official_sample"] = 2;
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(m, {pr.first, pr.second}, out, sum);
        check("34. cross-file backward FK ref -> PASS (order-independent)",
              ok && out.empty());
    }

    // 35. same legal cross-file reference with roles/sort order swapped -> PASS
    {
        auto pr = makeCrossRef("z_ref_a", "a_ref_b");  // a_ref_b before z_ref_a
        json m = validManifest("a_ref_b");
        m["problem_count"] = 2;
        m["trace_count"] = 2;
        m["problem_ids"] = json::array({"a_ref_b", "z_ref_a"});
        m["status_counts"] = json::object({{"correct", 2}, {"incorrect", 0}, {"undetermined", 0}});
        m["test_origin_counts"]["official_sample"] = 2;
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(m, {pr.second, pr.first}, out, sum);
        check("35. cross-file FK ref (order swapped) -> PASS (order-independent)",
              ok && out.empty());
    }

    // 36. candidate_solution.execution_status = passed -> FAIL (pilot dataset)
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"].push_back(json::object({
            {"stage", "implementation_consistency"}, {"text", "c"},
            {"relies_on", json::array()}}));
        p["candidate_solutions"] = json::array({json::object({
            {"id", PID + "_sol1"}, {"trace_id", PID + "_t1"}, {"language", "cpp"},
            {"standard", "c++17"}, {"source_code", "int main(){}"},
            {"execution_status", "passed"}})});
        check("36. execution_status=passed -> E_INVALID_ENUM (pilot dataset)",
              neg(validManifest(PID), {p}, hy3::errc::E_INVALID_ENUM));
    }

    // 37. diagnosis missing confidence_method -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["diagnoses"][0].erase("confidence_method");
        check("37. diagnosis missing confidence_method -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 38. diagnosis missing calibration_version -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["diagnoses"][0].erase("calibration_version");
        check("38. diagnosis missing calibration_version -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 39. confidence_method non-null -> E_UNCALIBRATED_CONFIDENCE
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["confidence_method"] = "heuristic";
        check("39. confidence_method non-null -> E_UNCALIBRATED_CONFIDENCE",
              neg(validManifest(PID), {p}, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // 40. calibration_version non-null -> E_UNCALIBRATED_CONFIDENCE
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["calibration_version"] = "v1";
        check("40. calibration_version non-null -> E_UNCALIBRATED_CONFIDENCE",
              neg(validManifest(PID), {p}, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // ----------------------------------------------------------------------
    // Phase 1B-R2.1 schema conformance micro-fix additions (tests 41-56)
    // ----------------------------------------------------------------------

    // 41. diagnosis missing primary_category -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["diagnoses"][0].erase("primary_category");
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("41. diagnosis missing primary_category -> E_MISSING_KEY",
              (!ok) && hasCode(out, hy3::errc::E_MISSING_KEY));
    }

    // 42. primary_category invalid category -> E_INVALID_ENUM
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["status"] = "incorrect";
        p["diagnoses"][0]["primary_category"] = "not_a_real_category";
        p["diagnoses"][0]["findings"] = json::array({json::object({
            {"stage", "boundary"}, {"category", "boundary_omission"},
            {"locating", "x"}, {"evidence", "y"}, {"suggestion", "z"}})});
        check("42. primary_category invalid -> E_INVALID_ENUM",
              neg(validManifest(PID), {p}, hy3::errc::E_INVALID_ENUM));
    }

    // 43. manifest.schema_version numeric -> E_TYPE_MISMATCH
    {
        json m = validManifest(PID);
        m["schema_version"] = 3;
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(m, {validProblem(PID)}, out, sum);
        check("43. manifest.schema_version numeric -> E_TYPE_MISMATCH",
              (!ok) && hasCode(out, hy3::errc::E_TYPE_MISMATCH));
    }

    // 44. confidence is a string -> BOTH E_TYPE_MISMATCH and E_UNCALIBRATED_CONFIDENCE
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["confidence"] = "bad";
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("44. confidence=string -> E_TYPE_MISMATCH + E_UNCALIBRATED_CONFIDENCE",
              (!ok) && hasCode(out, hy3::errc::E_TYPE_MISMATCH) &&
              hasCode(out, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // 45. confidence_method numeric -> BOTH codes
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["confidence_method"] = 123;
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("45. confidence_method=number -> E_TYPE_MISMATCH + E_UNCALIBRATED_CONFIDENCE",
              (!ok) && hasCode(out, hy3::errc::E_TYPE_MISMATCH) &&
              hasCode(out, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // 46. calibration_version numeric -> BOTH codes
    {
        json p = validProblem(PID);
        p["diagnoses"][0]["calibration_version"] = 7;
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("46. calibration_version=number -> E_TYPE_MISMATCH + E_UNCALIBRATED_CONFIDENCE",
              (!ok) && hasCode(out, hy3::errc::E_TYPE_MISMATCH) &&
              hasCode(out, hy3::errc::E_UNCALIBRATED_CONFIDENCE));
    }

    // 47. trace missing reviewer -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0].erase("reviewer");
        check("47. trace missing reviewer -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 48. trace missing reviewed_at -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0].erase("reviewed_at");
        check("48. trace missing reviewed_at -> E_MISSING_KEY",
              neg(validManifest(PID), {p}, hy3::errc::E_MISSING_KEY));
    }

    // 49. trace reviewer numeric -> E_TYPE_MISMATCH
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["reviewer"] = 5;
        check("49. trace reviewer numeric -> E_TYPE_MISMATCH",
              neg(validManifest(PID), {p}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 50. manifest missing reviewer -> E_MISSING_KEY
    {
        json m = validManifest(PID);
        m.erase("reviewer");
        check("50. manifest missing reviewer -> E_MISSING_KEY",
              neg(m, {validProblem(PID)}, hy3::errc::E_MISSING_KEY));
    }

    // 51. manifest.review_status invalid -> E_INVALID_ENUM
    {
        json m = validManifest(PID);
        m["review_status"] = "bogus_status";
        check("51. manifest.review_status invalid -> E_INVALID_ENUM",
              neg(m, {validProblem(PID)}, hy3::errc::E_INVALID_ENUM));
    }

    // 52. reference_tags contains non-string -> E_TYPE_MISMATCH
    {
        json p = validProblem(PID);
        p["problem"]["reference_tags"] = json::array({"greedy", 42});
        check("52. reference_tags non-string element -> E_TYPE_MISMATCH",
              neg(validManifest(PID), {p}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 53. step.relies_on contains non-string -> E_TYPE_MISMATCH
    {
        json p = validProblem(PID);
        p["reasoning_traces"][0]["steps"][0]["relies_on"] = json::array({123});
        check("53. relies_on non-string element -> E_TYPE_MISMATCH",
              neg(validManifest(PID), {p}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 54. manifest.problem_ids contains non-string -> E_TYPE_MISMATCH
    {
        json m = validManifest(PID);
        m["problem_ids"] = json::array({PID, 999});
        check("54. problem_ids non-string element -> E_TYPE_MISMATCH",
              neg(m, {validProblem(PID)}, hy3::errc::E_TYPE_MISMATCH));
    }

    // 55. verification_result missing actual_output/runtime_ms -> E_MISSING_KEY
    {
        json p = validProblem(PID);
        p["verification_results"] = json::array({json::object({
            {"solution_id", "x"}, {"test_id", PID + "_c1"},
            {"verdict", "pass"}
            // actual_output and runtime_ms intentionally omitted
            })});
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {p}, out, sum);
        check("55. verification_result missing actual_output/runtime_ms -> E_MISSING_KEY",
              (!ok) && hasCode(out, hy3::errc::E_UNEXPECTED_VERIFICATION_RESULT) &&
              hasCode(out, hy3::errc::E_MISSING_KEY));
    }

    // 56. legal original (canonical) builder data still PASS
    {
        std::vector<hy3::Diagnostic> out;
        hy3::ValidationSummary sum;
        bool ok = validateInTemp(validManifest(PID), {validProblem(PID)}, out, sum);
        check("56. legal original data -> PASS (no false positive)", ok && out.empty());
    }

    std::cout << "\nvalidator_tests: " << g_pass << " passed, " << g_fail
              << " failed\n";
    return g_fail == 0 ? 0 : 1;
}

