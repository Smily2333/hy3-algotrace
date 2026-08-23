// hy3_algotrace — DatasetValidator implementation.
//
// Implements every executable rule from docs/data-contract.md (schema 0.3.0)
// and docs/error-taxonomy.md. See the header for the overall contract.
//
// Phase 1B-R2 fix round (2026-08-23, planning-party review):
//   - two-phase validation: phase 1 loads every problem file and builds the
//     dataset-wide ID / ownership / reference index; phase 2 resolves all
//     foreign keys, associations, cardinality and semantic rules against that
//     complete index (NO file-order dependency);
//   - CMake include propagation fixed (third_party as SYSTEM PUBLIC on core);
//   - candidate_solution.execution_status fixed to "not_run" for the
//     phase1a-pilot-001 dataset;
//   - diagnosis.confidence / confidence_method / calibration_version all
//     required and must be null before Phase 4 calibration.

#include "hy3_algotrace/validator.hpp"
#include "hy3_algotrace/json_loader.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace hy3 {
namespace {

// ----- canonical whitelists ------------------------------------------------
const std::set<std::string> kStages = {
    "problem_understanding", "greedy_choice", "greedy_proof",
    "complexity", "boundary", "implementation_consistency"};
const std::set<std::string> kCategories = {
    "problem_misunderstanding", "wrong_greedy_choice", "missing_greedy_proof",
    "invalid_greedy_proof", "complexity_error", "boundary_omission",
    "implementation_mismatch"};
const std::set<std::string> kStatuses = {"correct", "incorrect", "undetermined"};
const std::set<std::string> kTestOrigins = {
    "official_sample", "manually_designed", "counterexample", "generated"};
const std::set<std::string> kTestPurposes = {
    "normal", "boundary", "complexity", "counterexample"};
const std::set<std::string> kExecStatus = {"not_run", "passed", "failed", "error"};
const std::set<std::string> kVerdicts = {
    "not_run", "pass", "fail", "compile_error", "runtime_error", "timeout"};
const std::set<std::string> kReviewStatuses = {
    "pending_planner_review", "planner_reviewed"};
const std::vector<std::string> kManifestRequiredKeys = {
    "schema_version", "taxonomy_version", "dataset_version", "problem_count",
    "trace_count", "problem_ids", "category_counts", "status_counts",
    "test_origin_counts", "review_status", "reviewer", "reviewed_at"};

// Fixed values required by the Phase 1A/1B data contract (schema 0.3.0).
const std::string kFixedProblemSource = "codeforces";
const std::string kFixedAlgorithmType = "greedy";
const std::string kFixedTraceOrigin = "model_generated";
const std::string kFixedGeneratorModel = "hy3";
const std::string kFixedAnnotator = "hy3_draft";
const std::string kFixedSolutionLanguage = "cpp";
const std::string kFixedSolutionStandard = "c++17";
// The phase1a-pilot-001 dataset does NOT execute candidate code, so every
// candidate_solution.execution_status must be "not_run". (passed/failed/error
// remain valid in the general contract for later phases, but are rejected here.)
const std::string kFixedSolutionExecStatus = "not_run";

bool member(const std::set<std::string>& s, const std::string& v) {
    return s.find(v) != s.end();
}

void emit(std::vector<Diagnostic>& out, const char* code, const std::string& file,
          const std::string& oid, const std::string& msg) {
    out.push_back(Diagnostic{Severity::ERROR, code, file, oid, msg});
}

// ----- small typed extractors (emit on failure) ----------------------------
bool reqString(const json& obj, const std::string& key, const std::string& file,
               const std::string& oid, std::string& out,
               std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_string()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be a string");
        return false;
    }
    out = v.get<std::string>();
    return true;
}

std::string nonEmptyString(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return "";
    const json& v = obj[key];
    if (!v.is_string()) return "";
    const std::string s = v.get<std::string>();
    return s.empty() ? "" : s;
}

bool reqInt(const json& obj, const std::string& key, const std::string& file,
            const std::string& oid, int& out, std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_number_integer()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be an integer");
        return false;
    }
    out = v.get<int>();
    return true;
}

bool reqArray(const json& obj, const std::string& key, const std::string& file,
              const std::string& oid, const json*& out,
              std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_array()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be an array");
        return false;
    }
    out = &v;
    return true;
}

bool reqObject(const json& obj, const std::string& key, const std::string& file,
               const std::string& oid, const json*& out,
               std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_object()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be an object");
        return false;
    }
    out = &v;
    return true;
}

bool reqFixed(const json& obj, const std::string& key, const std::string& expect,
              const std::string& file, const std::string& oid,
              std::vector<Diagnostic>& diags) {
    std::string v;
    if (!reqString(obj, key, file, oid, v, diags)) return false;
    if (v != expect) {
        emit(diags, errc::E_INVALID_ENUM, file, oid,
             "key '" + key + "' must equal '" + expect + "' (got '" + v + "')");
        return false;
    }
    return true;
}

// Require a fixed string value but allow only a specific allow-list
// (the general contract enum); for the pilot dataset, only the expected value
// is accepted and any other enum value is rejected with E_INVALID_ENUM.
bool reqFixedFromEnum(const json& obj, const std::string& key,
                      const std::string& expect, const std::set<std::string>& en,
                      const std::string& file, const std::string& oid,
                      std::vector<Diagnostic>& diags) {
    std::string v;
    if (!reqString(obj, key, file, oid, v, diags)) return false;
    if (!member(en, v))
        emit(diags, errc::E_INVALID_ENUM, file, oid,
             "key '" + key + "' value '" + v + "' is not a valid enum");
    else if (v != expect)
        emit(diags, errc::E_INVALID_ENUM, file, oid,
             "key '" + key + "' must equal '" + expect + "' for the phase1a-pilot-001 dataset (got '" + v + "')");
    return true;
}

// ----- three-layer typed extractors (Phase 1B-R2.1) -------------------------
// Layer 1: existence. Layer 2: declared type. Layer 3: semantic rules are
// applied by the caller AFTER the type is known. These helpers never silently
// treat a missing key or a wrong-typed value as null/empty.

// Required field, declared type string|null. Returns true iff the key exists
// and is a string or null (type layer passes). `out` carries the string ("" for
// null). A missing key -> E_MISSING_KEY; a wrong type -> E_TYPE_MISMATCH.
bool reqNullableString(const json& obj, const std::string& key,
                       const std::string& file, const std::string& oid,
                       std::string& out, std::vector<Diagnostic>& diags) {
    out.clear();
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_string() && !v.is_null()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be a string or null");
        return false;
    }
    if (v.is_string()) out = v.get<std::string>();
    return true;
}

// Required field, declared type number|null. Returns true iff the key exists
// and is a number or null. `out` carries the number (0 for null).
bool reqNullableNumber(const json& obj, const std::string& key,
                       const std::string& file, const std::string& oid,
                       double& out, std::vector<Diagnostic>& diags) {
    out = 0.0;
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_number() && !v.is_null()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be a number or null");
        return false;
    }
    if (v.is_number()) out = v.get<double>();
    return true;
}

// Required array<string>: each element must be a string. A missing key ->
// E_MISSING_KEY; a non-array -> E_TYPE_MISMATCH; a non-string element ->
// E_TYPE_MISMATCH (reported once per offending array).
bool reqStringArray(const json& obj, const std::string& key,
                    const std::string& file, const std::string& oid,
                    const json*& out, std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, oid, "missing required key: " + key);
        return false;
    }
    const json& v = obj[key];
    if (!v.is_array()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "key '" + key + "' must be an array");
        return false;
    }
    for (const auto& e : v) {
        if (!e.is_string()) {
            emit(diags, errc::E_TYPE_MISMATCH, file, oid,
                 "every element of '" + key + "' must be a string");
            break;
        }
    }
    out = &v;
    return true;
}

// Optional field, declared type string. Missing -> returns false (not an error).
// Present but non-string -> E_TYPE_MISMATCH (reported, returns true so caller
// knows it existed). Used ONLY for fields the contract explicitly marks optional.
bool optString(const json& obj, const std::string& key, const std::string& file,
               const std::string& oid, std::string& out,
               std::vector<Diagnostic>& diags) {
    out.clear();
    if (!obj.contains(key)) return false;
    const json& v = obj[key];
    if (!v.is_string()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, oid,
             "optional key '" + key + "' must be a string when present");
        return true;  // present, but wrong type
    }
    out = v.get<std::string>();
    return true;
}

// ----- running accumulators across the whole dataset -----------------------
// Phase-1 record of one reasoning_trace (indexed globally, resolved in phase 2).
struct TraceRec {
    std::string id;
    std::string problemId;
    std::string file;
    std::string review_status;
    std::string reviewer;
    std::string reviewed_at;
    bool hasImplStep = false;
};
struct SolRec {
    std::string id;
    std::string trace_id;
    std::string file;
    std::string execution_status;
};
struct DiagRec {
    std::string id;
    std::string trace_id;
    std::string file;
    std::string status;
    std::string primary;          // "" when null
    std::set<std::string> cats;    // categories collected from findings
    bool findingsOk = false;
    bool primaryInFindings = false;
    bool confidenceNull = true;
    bool confidenceMethodNull = true;
    bool calibrationVersionNull = true;
};
struct TestRec {
    std::string id;
    std::string problemId;
    std::string file;
    std::string origin;
};
struct VerifRec {
    std::string file;
    std::string solution_id;
    std::string test_id;
    std::string verdict;
};

struct Accum {
    // global ID namespaces (dataset-wide uniqueness)
    std::set<std::string> problemIds;
    std::set<std::string> traceIds;
    std::set<std::string> solIds;
    std::set<std::string> tcIds;
    std::set<std::string> dgIds;
    // global FK resolution maps (target may live in another file)
    std::map<std::string, std::string> traceToProblem; // trace.id -> problem.id
    std::map<std::string, std::string> solToTrace;      // solution.id -> trace.id
    std::map<std::string, std::string> diagToTrace;     // diagnosis.id -> trace.id
    std::map<std::string, std::string> testToProblem;   // test.id -> problem.id

    // phase-1 records for phase-2 resolution (order-independent)
    std::vector<TraceRec> traces;
    std::vector<SolRec> solutions;
    std::vector<DiagRec> diagnoses;
    std::vector<TestRec> tests;
    std::vector<VerifRec> verifs;

    int traceCount = 0, diagCount = 0, testCount = 0, solCount = 0, verifCount = 0;
    std::map<std::string, int> statusCounts;
    std::map<std::string, int> catCounts;       // traces touching each category
    std::map<std::string, int> testOriginCounts;
    std::vector<std::string> traceReviewStatuses;
};

struct ManifestView {
    bool ok = false;
    std::string schema;
    std::string taxonomy;
    std::string dataset;
    std::string review_status;
    std::string reviewer;
    std::string reviewed_at;
    json raw;
};

// ----- PHASE 1: load a problem file, read structure, build global index ----
void loadProblem(const json& doc, const std::string& file,
                 const ManifestView& mv, Accum& acc,
                 std::vector<Diagnostic>& diags) {
    // A. required top-level keys (extra keys allowed).
    const std::vector<std::string> topKeys = {
        "meta", "problem", "reference_verdict", "test_cases", "reasoning_traces",
        "candidate_solutions", "diagnoses", "verification_results"};
    for (const auto& k : topKeys)
        if (!doc.contains(k))
            emit(diags, errc::E_MISSING_KEY, file, "",
                 "problem file missing required top-level key: " + k);

    // B. meta version consistency + required meta fields.
    {
        const json* meta = nullptr;
        if (reqObject(doc, "meta", file, "", meta, diags)) {
            std::string sv, tv, dv, sr, ca;
            if (reqString(*meta, "schema_version", file, "", sv, diags) &&
                mv.ok && !mv.schema.empty() && sv != mv.schema)
                emit(diags, errc::E_VERSION_MISMATCH, file, "",
                     "meta.schema_version '" + sv + "' != manifest '" + mv.schema + "'");
            if (reqString(*meta, "taxonomy_version", file, "", tv, diags) &&
                mv.ok && !mv.taxonomy.empty() && tv != mv.taxonomy)
                emit(diags, errc::E_VERSION_MISMATCH, file, "",
                     "meta.taxonomy_version '" + tv + "' != manifest '" + mv.taxonomy + "'");
            if (reqString(*meta, "dataset_version", file, "", dv, diags) &&
                mv.ok && !mv.dataset.empty() && dv != mv.dataset)
                emit(diags, errc::E_VERSION_MISMATCH, file, "",
                     "meta.dataset_version '" + dv + "' != manifest '" + mv.dataset + "'");
            reqString(*meta, "source_reference", file, "", sr, diags);
            reqString(*meta, "created_at", file, "", ca, diags);
        }
    }

    // problem.id + filename + required/fixed problem fields.
    std::string problemId;
    {
        const json* prob = nullptr;
        if (reqObject(doc, "problem", file, "", prob, diags)) {
            if (reqString(*prob, "id", file, "", problemId, diags)) {
                if (acc.problemIds.count(problemId))
                    emit(diags, errc::E_DUPLICATE_ID, file, problemId,
                         "duplicate problem.id '" + problemId + "' (across the dataset)");
                acc.problemIds.insert(problemId);
                std::string expected = problemId + ".json";
                if (fs::path(file).filename().string() != expected)
                    emit(diags, errc::E_PROBLEM_ID_FILE_MISMATCH, file, problemId,
                         "problem.id '" + problemId + "' does not match filename '" +
                         expected + "'");
            }
            std::string src, algo, title, stmt;
            reqString(*prob, "source", file, problemId, src, diags);
            reqString(*prob, "title", file, problemId, title, diags);
            reqString(*prob, "statement", file, problemId, stmt, diags);
            const json* c = nullptr;
            reqObject(*prob, "constraints", file, problemId, c, diags);
            const json* tags = nullptr;
            reqStringArray(*prob, "reference_tags", file, problemId, tags, diags);
            // notes is optional; present but non-string -> E_TYPE_MISMATCH.
            std::string probNotes;
            optString(*prob, "notes", file, problemId, probNotes, diags);
            if (reqString(*prob, "algorithm_type", file, problemId, algo, diags) &&
                algo != kFixedAlgorithmType)
                emit(diags, errc::E_INVALID_ENUM, file, problemId,
                     "problem.algorithm_type must be '" + kFixedAlgorithmType +
                     "' (got '" + algo + "')");
            if (!src.empty() && src != kFixedProblemSource)
                emit(diags, errc::E_INVALID_ENUM, file, problemId,
                     "problem.source must be '" + kFixedProblemSource + "' (got '" + src + "')");
        }
    }

    // reference_verdict required fields + FK.
    {
        const json* rv = nullptr;
        if (reqObject(doc, "reference_verdict", file, "", rv, diags)) {
            std::string rvp, ec, ep, ecm, ce;
            reqString(*rv, "problem_id", file, "", rvp, diags);
            reqString(*rv, "expected_choice", file, "", ec, diags);
            reqString(*rv, "expected_proof", file, "", ep, diags);
            reqString(*rv, "expected_complexity", file, "", ecm, diags);
            const json* eb = nullptr;
            reqStringArray(*rv, "expected_boundaries", file, "", eb, diags);
            reqString(*rv, "common_wrong_strategy_counterexample", file, "", ce, diags);
            if (!rvp.empty() && !problemId.empty() && rvp != problemId)
                emit(diags, errc::E_BAD_PROBLEM_FK, file, "",
                     "reference_verdict.problem_id '" + rvp +
                     "' does not match problem.id '" + problemId + "'");
        }
    }

    // reasoning_traces: read structure + build global trace index.
    {
        const json* traces = nullptr;
        if (reqArray(doc, "reasoning_traces", file, "", traces, diags)) {
            acc.traceCount += static_cast<int>(traces->size());
            for (const auto& t : *traces) {
                if (!t.is_object()) {
                    emit(diags, errc::E_TYPE_MISMATCH, file, "", "reasoning_trace entry must be an object");
                    continue;
                }
                TraceRec rec; rec.file = file;
                if (!reqString(t, "id", file, "", rec.id, diags)) continue;
                if (acc.traceIds.count(rec.id))
                    emit(diags, errc::E_DUPLICATE_ID, file, rec.id,
                         "duplicate reasoning_trace.id '" + rec.id + "' (across the dataset)");
                acc.traceIds.insert(rec.id);
                if (!problemId.empty()) acc.traceToProblem[rec.id] = problemId;
                rec.problemId = problemId;

                std::string tpid, author;
                if (reqString(t, "problem_id", file, rec.id, tpid, diags) &&
                    !problemId.empty() && tpid != problemId)
                    emit(diags, errc::E_BAD_PROBLEM_FK, file, rec.id,
                         "trace.problem_id '" + tpid + "' does not match problem.id '" + problemId + "'");
                reqFixed(t, "trace_origin", kFixedTraceOrigin, file, rec.id, diags);
                reqFixed(t, "generator_model", kFixedGeneratorModel, file, rec.id, diags);
                reqFixed(t, "annotator", kFixedAnnotator, file, rec.id, diags);
                reqString(t, "author", file, rec.id, author, diags);
                // reviewer / reviewed_at: required, string|null (three-layer).
                reqNullableString(t, "reviewer", file, rec.id, rec.reviewer, diags);
                reqNullableString(t, "reviewed_at", file, rec.id, rec.reviewed_at, diags);
                // intended_outcome is optional; present but non-string -> E_TYPE_MISMATCH.
                std::string intended;
                optString(t, "intended_outcome", file, rec.id, intended, diags);
                if (reqString(t, "review_status", file, rec.id, rec.review_status, diags)) {
                    acc.traceReviewStatuses.push_back(rec.review_status);
                    if (!member(kReviewStatuses, rec.review_status))
                        emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                             "review_status '" + rec.review_status + "' is not a valid value");
                    else if (rec.review_status == "planner_reviewed") {
                        if (rec.reviewer.empty() || rec.reviewed_at.empty())
                            emit(diags, errc::E_REVIEW_STATUS_SEMANTIC, file, rec.id,
                                 "review_status=planner_reviewed requires non-null reviewer and reviewed_at");
                    } else {
                        if (!rec.reviewer.empty() || !rec.reviewed_at.empty())
                            emit(diags, errc::E_REVIEW_STATUS_SEMANTIC, file, rec.id,
                                 "review_status=pending_planner_review requires reviewer=null and reviewed_at=null");
                    }
                }

                const json* steps = nullptr;
                if (reqArray(t, "steps", file, rec.id, steps, diags)) {
                    for (const auto& s : *steps) {
                        if (!s.is_object()) {
                            emit(diags, errc::E_TYPE_MISMATCH, file, rec.id, "step must be an object");
                            continue;
                        }
                        std::string stage, stext;
                        if (reqString(s, "stage", file, rec.id, stage, diags) &&
                            !member(kStages, stage))
                            emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                                 "step.stage '" + stage + "' is not a valid stage");
                        if (stage == "implementation_consistency") rec.hasImplStep = true;
                        if (reqString(s, "text", file, rec.id, stext, diags) && stext.empty())
                            emit(diags, errc::E_TYPE_MISMATCH, file, rec.id,
                                 "step.text must be a non-empty string");
                        const json* rel = nullptr;
                        reqStringArray(s, "relies_on", file, rec.id, rel, diags);
                    }
                }
                acc.traces.push_back(std::move(rec));
            }
        }
    }

    // candidate_solutions: read structure + build global solution index.
    {
        const json* sols = nullptr;
        if (reqArray(doc, "candidate_solutions", file, "", sols, diags)) {
            acc.solCount += static_cast<int>(sols->size());
            for (const auto& sol : *sols) {
                if (!sol.is_object()) {
                    emit(diags, errc::E_TYPE_MISMATCH, file, "", "candidate_solution entry must be an object");
                    continue;
                }
                SolRec rec; rec.file = file;
                if (!reqString(sol, "id", file, "", rec.id, diags)) continue;
                if (acc.solIds.count(rec.id))
                    emit(diags, errc::E_DUPLICATE_ID, file, rec.id,
                         "duplicate candidate_solution.id '" + rec.id + "' (across the dataset)");
                acc.solIds.insert(rec.id);
                if (reqString(sol, "trace_id", file, rec.id, rec.trace_id, diags))
                    acc.solToTrace[rec.id] = rec.trace_id;
                reqFixed(sol, "language", kFixedSolutionLanguage, file, rec.id, diags);
                reqFixed(sol, "standard", kFixedSolutionStandard, file, rec.id, diags);
                std::string src;
                reqString(sol, "source_code", file, rec.id, src, diags);
                // Phase 1A pilot dataset does not execute candidate code: the
                // only accepted value is "not_run"; any other enum value is
                // rejected with E_INVALID_ENUM (handled inside reqFixedFromEnum).
                reqFixedFromEnum(sol, "execution_status", kFixedSolutionExecStatus,
                                 kExecStatus, file, rec.id, diags);
                acc.solutions.push_back(std::move(rec));
            }
        }
    }

    // test_cases: read structure + build global test index.
    {
        const json* tcs = nullptr;
        if (reqArray(doc, "test_cases", file, "", tcs, diags)) {
            acc.testCount += static_cast<int>(tcs->size());
            for (const auto& tc : *tcs) {
                if (!tc.is_object()) {
                    emit(diags, errc::E_TYPE_MISMATCH, file, "", "test_case entry must be an object");
                    continue;
                }
                TestRec rec; rec.file = file;
                if (!reqString(tc, "id", file, "", rec.id, diags)) continue;
                if (acc.tcIds.count(rec.id))
                    emit(diags, errc::E_DUPLICATE_ID, file, rec.id,
                         "duplicate test_case.id '" + rec.id + "' (across the dataset)");
                acc.tcIds.insert(rec.id);
                if (!problemId.empty()) acc.testToProblem[rec.id] = problemId;
                rec.problemId = problemId;
                std::string tpid, input, output, purpose, origin;
                if (reqString(tc, "problem_id", file, rec.id, tpid, diags) &&
                    !problemId.empty() && tpid != problemId)
                    emit(diags, errc::E_BAD_TEST_FK, file, rec.id,
                         "test_case.problem_id '" + tpid + "' does not match problem.id '" + problemId + "'");
                if (reqString(tc, "input", file, rec.id, input, diags) && input.empty())
                    emit(diags, errc::E_TYPE_MISMATCH, file, rec.id,
                         "test_case.input must be a non-empty string");
                reqString(tc, "expected_output", file, rec.id, output, diags);
                if (reqString(tc, "origin", file, rec.id, rec.origin, diags)) {
                    if (!member(kTestOrigins, rec.origin))
                        emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                             "test_case.origin '" + rec.origin + "' is invalid");
                    else
                        acc.testOriginCounts[rec.origin]++;
                }
                if (reqString(tc, "purpose", file, rec.id, purpose, diags) &&
                    !member(kTestPurposes, purpose))
                    emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                         "test_case.purpose '" + purpose + "' is invalid");
                // notes is optional; present but non-string -> E_TYPE_MISMATCH.
                std::string tcNotes;
                optString(tc, "notes", file, rec.id, tcNotes, diags);
                acc.tests.push_back(std::move(rec));
            }
        }
    }

    // verification_results: read structure (Phase 1A invariant: must be empty).
    {
        const json* vrs = nullptr;
        if (reqArray(doc, "verification_results", file, "", vrs, diags)) {
            acc.verifCount += static_cast<int>(vrs->size());
            if (vrs->size() > 0) {
                emit(diags, errc::E_UNEXPECTED_VERIFICATION_RESULT, file, "",
                     "verification_results must be empty for the Phase 1A dataset");
                for (const auto& vr : *vrs) {
                    if (!vr.is_object()) continue;
                    VerifRec rec; rec.file = file;
                    // Even though the array is forbidden in Phase 1A, complete
                    // the per-record field checks so malformed records still
                    // surface their own errors.
                    reqString(vr, "solution_id", file, "", rec.solution_id, diags);
                    reqString(vr, "test_id", file, "", rec.test_id, diags);
                    std::string ao;
                    reqNullableString(vr, "actual_output", file, "", ao, diags);
                    std::string verdict;
                    if (reqString(vr, "verdict", file, "", verdict, diags)) {
                        rec.verdict = verdict;
                        if (!member(kVerdicts, verdict))
                            emit(diags, errc::E_INVALID_ENUM, file, "",
                                 "verification_result.verdict '" + verdict + "' is invalid");
                    }
                    double rt = 0;
                    reqNullableNumber(vr, "runtime_ms", file, "", rt, diags);
                    std::string fr;
                    optString(vr, "finding_ref", file, "", fr, diags);
                    acc.verifs.push_back(std::move(rec));
                }
            }
        }
    }

    // diagnoses: read structure + build global diagnosis index + per-trace cats.
    {
        const json* dgs = nullptr;
        if (reqArray(doc, "diagnoses", file, "", dgs, diags)) {
            acc.diagCount += static_cast<int>(dgs->size());
            for (const auto& dg : *dgs) {
                if (!dg.is_object()) {
                    emit(diags, errc::E_TYPE_MISMATCH, file, "", "diagnosis entry must be an object");
                    continue;
                }
                DiagRec rec; rec.file = file;
                if (!reqString(dg, "id", file, "", rec.id, diags)) continue;
                if (acc.dgIds.count(rec.id))
                    emit(diags, errc::E_DUPLICATE_ID, file, rec.id,
                         "duplicate diagnosis.id '" + rec.id + "' (across the dataset)");
                acc.dgIds.insert(rec.id);
                if (reqString(dg, "trace_id", file, rec.id, rec.trace_id, diags))
                    acc.diagToTrace[rec.id] = rec.trace_id;

                if (reqString(dg, "status", file, rec.id, rec.status, diags) &&
                    !member(kStatuses, rec.status))
                    emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                         "diagnosis.status '" + rec.status + "' is invalid");
                // primary_category: required, string|null, enum-or-null.
                // Layer 1 existence + Layer 2 type via reqNullableString.
                std::string pc;
                reqNullableString(dg, "primary_category", file, rec.id, pc, diags);
                // Layer 3 (enum) is applied in phase 2 once status is known AND
                // we know the set of finding categories. We record pc (""=null).
                rec.primary = pc;

                // three confidence fields: all required, declared types differ,
                // all must be null before Phase 4 calibration.
                // confidence: number|null ; confidence_method: string|null ;
                // calibration_version: string|null.
                const struct { const char* key; bool isNumber; } cfields[3] = {
                    {"confidence", true},
                    {"confidence_method", false},
                    {"calibration_version", false}};
                for (const auto& cf : cfields) {
                    if (!dg.contains(cf.key)) {
                        emit(diags, errc::E_MISSING_KEY, file, rec.id,
                             "diagnosis missing required key: " + std::string(cf.key));
                        continue;
                    }
                    const json& v = dg[cf.key];
                    bool typeOk;
                    if (cf.isNumber) {
                        // Layer 2: declared type number|null.
                        typeOk = v.is_number() || v.is_null();
                        if (!typeOk)
                            emit(diags, errc::E_TYPE_MISMATCH, file, rec.id,
                                 "diagnosis." + std::string(cf.key) +
                                 " must be a number or null");
                        if (!v.is_null()) {
                            // Layer 3: non-null before Phase 4 calibration.
                            emit(diags, errc::E_UNCALIBRATED_CONFIDENCE, file, rec.id,
                                 "diagnosis." + std::string(cf.key) +
                                 " must be null before Phase 4 calibration");
                        }
                    } else {
                        // Layer 2: declared type string|null.
                        typeOk = v.is_string() || v.is_null();
                        if (!typeOk)
                            emit(diags, errc::E_TYPE_MISMATCH, file, rec.id,
                                 "diagnosis." + std::string(cf.key) +
                                 " must be a string or null");
                        if (!v.is_null()) {
                            // Layer 3: non-null before Phase 4 calibration.
                            emit(diags, errc::E_UNCALIBRATED_CONFIDENCE, file, rec.id,
                                 "diagnosis." + std::string(cf.key) +
                                 " must be null before Phase 4 calibration");
                        }
                    }
                    if (std::string(cf.key) == "confidence")
                        rec.confidenceNull = v.is_null();
                    else if (std::string(cf.key) == "confidence_method")
                        rec.confidenceMethodNull = v.is_null();
                    else
                        rec.calibrationVersionNull = v.is_null();
                }

                const json* findings = nullptr;
                rec.findingsOk = reqArray(dg, "findings", file, rec.id, findings, diags);
                if (rec.findingsOk) {
                    for (const auto& f : *findings) {
                        if (!f.is_object()) {
                            emit(diags, errc::E_TYPE_MISMATCH, file, rec.id, "finding must be an object");
                            continue;
                        }
                        std::string fstage, fcat, floc, fev, fsug;
                        if (reqString(f, "stage", file, rec.id, fstage, diags) &&
                            !member(kStages, fstage))
                            emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                                 "finding.stage '" + fstage + "' is invalid");
                        if (reqString(f, "category", file, rec.id, fcat, diags)) {
                            if (!member(kCategories, fcat))
                                emit(diags, errc::E_INVALID_ENUM, file, rec.id,
                                     "finding.category '" + fcat + "' is not one of the 7 allowed");
                            else
                                rec.cats.insert(fcat);
                        }
                        reqString(f, "locating", file, rec.id, floc, diags);
                        reqString(f, "evidence", file, rec.id, fev, diags);
                        reqString(f, "suggestion", file, rec.id, fsug, diags);
                        if (fcat == rec.primary) rec.primaryInFindings = true;
                    }
                }
                // count this trace once per category it touches (dedup)
                for (const auto& c : rec.cats) acc.catCounts[c]++;
                if (member(kStatuses, rec.status)) acc.statusCounts[rec.status]++;
                acc.diagnoses.push_back(std::move(rec));
            }
        }
    }
}

// ----- PHASE 2: resolve FK / associations / cardinality / semantics ---------
void resolveForeignKeys(Accum& acc, std::vector<Diagnostic>& diags) {
    // candidate_solution.trace_id must resolve (order-independent).
    for (const auto& sol : acc.solutions) {
        if (!acc.traceIds.count(sol.trace_id))
            emit(diags, errc::E_BAD_TRACE_FK, sol.file, sol.id,
                 "candidate_solution.trace_id '" + sol.trace_id +
                 "' does not resolve to any reasoning_trace");
    }
    // diagnosis.trace_id must resolve (order-independent).
    for (const auto& dg : acc.diagnoses) {
        if (!acc.traceIds.count(dg.trace_id))
            emit(diags, errc::E_BAD_TRACE_FK, dg.file, dg.id,
                 "diagnosis.trace_id '" + dg.trace_id +
                 "' does not resolve to any reasoning_trace");
    }
    // verification_result FKs (order-independent).
    for (const auto& vr : acc.verifs) {
        if (!vr.solution_id.empty() && !acc.solIds.count(vr.solution_id))
            emit(diags, errc::E_BAD_SOLUTION_FK, vr.file, "",
                 "verification_result.solution_id '" + vr.solution_id + "' does not resolve");
        if (!vr.test_id.empty() && !acc.tcIds.count(vr.test_id))
            emit(diags, errc::E_BAD_TEST_FK, vr.file, "",
                 "verification_result.test_id '" + vr.test_id + "' does not resolve");
        if (!vr.verdict.empty() && !member(kVerdicts, vr.verdict))
            emit(diags, errc::E_INVALID_ENUM, vr.file, "",
                 "verification_result.verdict '" + vr.verdict + "' is invalid");
    }

    // implementation_consistency <-> candidate_solution association (dataset-wide)
    std::set<std::string> solTraces;
    for (const auto& sol : acc.solutions) solTraces.insert(sol.trace_id);
    for (const auto& tr : acc.traces) {
        if (tr.hasImplStep && !solTraces.count(tr.id))
            emit(diags, errc::E_IMPLEMENTATION_WITHOUT_SOLUTION, tr.file, tr.id,
                 "trace contains an 'implementation_consistency' step but has "
                 "no associated candidate_solution");
    }

    // each trace must have exactly one diagnosis (dataset-wide cardinality).
    std::map<std::string, int> diagByTrace;
    for (const auto& dg : acc.diagnoses) diagByTrace[dg.trace_id]++;
    for (const auto& tr : acc.traces) {
        int c = diagByTrace[tr.id];
        if (c != 1)
            emit(diags, errc::E_DIAGNOSIS_CARDINALITY, tr.file, tr.id,
                 "reasoning_trace '" + tr.id + "' has " + std::to_string(c) +
                 " diagnosis(es); exactly 1 is required");
    }

    // diagnosis status rules + primary_category semantics.
    for (const auto& dg : acc.diagnoses) {
        if (!member(kStatuses, dg.status)) continue;  // already reported
        // Layer 3 (enum): a non-null primary_category must be one of the 7.
        if (!dg.primary.empty() && !member(kCategories, dg.primary))
            emit(diags, errc::E_INVALID_ENUM, dg.file, dg.id,
                 "diagnosis.primary_category '" + dg.primary + "' is not one of the 7 allowed categories");
        if (dg.status == "correct") {
            if (!dg.findingsOk || !dg.cats.empty() || !dg.primary.empty())
                emit(diags, errc::E_CORRECT_WITH_FINDINGS, dg.file, dg.id,
                     "status=correct requires primary_category=null and empty findings");
        } else if (dg.status == "incorrect") {
            if (!dg.findingsOk || dg.cats.empty())
                emit(diags, errc::E_INCORRECT_WITHOUT_FINDINGS, dg.file, dg.id,
                     "status=incorrect requires non-empty findings");
            else if (dg.primary.empty() || !dg.primaryInFindings)
                emit(diags, errc::E_PRIMARY_NOT_IN_FINDINGS, dg.file, dg.id,
                     "status=incorrect requires primary_category to be non-null and "
                     "present in a finding.category");
        } else {  // undetermined
            if (!dg.primary.empty())
                emit(diags, errc::E_STATUS_PRIMARY_MISMATCH, dg.file, dg.id,
                     "status=undetermined requires primary_category=null");
        }
    }
}

// Require a summary count entry: key must exist, be an integer; compare to want.
void checkSummaryInt(const json& obj, const std::string& key,
                     const std::string& file, int want,
                     std::vector<Diagnostic>& diags) {
    if (!obj.contains(key)) {
        emit(diags, errc::E_MISSING_KEY, file, "", "summary count missing key: " + key);
        return;
    }
    const json& v = obj[key];
    if (!v.is_number_integer()) {
        emit(diags, errc::E_TYPE_MISMATCH, file, "", "summary count '" + key + "' must be an integer");
        return;
    }
    int have = v.get<int>();
    if (have != want)
        emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
             key + "=" + std::to_string(have) + " but dataset has " + std::to_string(want));
}

void checkManifestSummary(const ManifestView& mv, Accum& acc,
                          std::vector<Diagnostic>& diags) {
    if (!mv.ok) return;
    const json& m = mv.raw;
    const std::string file = "manifest.json";

    // --- required string fields (real type check, not silent read) ---------
    std::string sv, tv, dv, rs;
    reqString(m, "schema_version", file, "", sv, diags);
    reqString(m, "taxonomy_version", file, "", tv, diags);
    reqString(m, "dataset_version", file, "", dv, diags);
    if (reqString(m, "review_status", file, "", rs, diags) &&
        !member(kReviewStatuses, rs))
        emit(diags, errc::E_INVALID_ENUM, file, "",
             "manifest.review_status '" + rs + "' is not a valid value");

    int pc = 0, tc = 0;
    if (reqInt(m, "problem_count", file, "", pc, diags) &&
        pc != static_cast<int>(acc.problemIds.size()))
        emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
             "problem_count=" + std::to_string(pc) + " but dataset has " +
             std::to_string(acc.problemIds.size()) + " problem file(s)");
    if (reqInt(m, "trace_count", file, "", tc, diags) && tc != acc.traceCount)
        emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
             "trace_count=" + std::to_string(tc) + " but dataset has " +
             std::to_string(acc.traceCount) + " trace(s)");

    {
        const json* cc = nullptr;
        if (reqObject(m, "category_counts", file, "", cc, diags)) {
            for (const auto& cat : kCategories)
                checkSummaryInt(*cc, cat, file, acc.catCounts[cat], diags);
            for (const auto& kv : cc->items())
                if (!member(kCategories, kv.key()))
                    emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
                         "category_counts has unexpected key '" + kv.key() + "'");
        }
    }
    {
        const json* sc = nullptr;
        if (reqObject(m, "status_counts", file, "", sc, diags)) {
            for (const auto& st : kStatuses)
                checkSummaryInt(*sc, st, file, acc.statusCounts[st], diags);
        }
    }
    {
        const json* oc = nullptr;
        if (reqObject(m, "test_origin_counts", file, "", oc, diags)) {
            for (const auto& o : kTestOrigins)
                checkSummaryInt(*oc, o, file, acc.testOriginCounts[o], diags);
        }
    }
    {
        const json* ids = nullptr;
        if (reqArray(m, "problem_ids", file, "", ids, diags)) {
            std::vector<std::string> mids;
            for (const auto& v : *ids) {
                if (!v.is_string()) {
                    emit(diags, errc::E_TYPE_MISMATCH, file, "",
                         "every element of manifest.problem_ids must be a string");
                    break;
                }
                mids.push_back(v.get<std::string>());
            }
            std::sort(mids.begin(), mids.end());
            std::vector<std::string> pids(acc.problemIds.begin(), acc.problemIds.end());
            std::sort(pids.begin(), pids.end());
            if (mids != pids)
                emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
                     "manifest.problem_ids does not match the set of problem.id found");
        }
    }
    if (!acc.traceReviewStatuses.empty()) {
        bool allSame = true;
        for (const auto& trs : acc.traceReviewStatuses)
            if (trs != mv.review_status) { allSame = false; break; }
        if (!allSame)
            emit(diags, errc::E_MANIFEST_COUNT_MISMATCH, file, "",
                 "manifest.review_status='" + mv.review_status +
                 "' is inconsistent with trace review_status values");
    }
    if (mv.review_status == "planner_reviewed") {
        if (mv.reviewer.empty() || mv.reviewed_at.empty())
            emit(diags, errc::E_REVIEW_STATUS_SEMANTIC, file, "",
                 "manifest.review_status=planner_reviewed requires non-null reviewer and reviewed_at");
    } else if (mv.review_status == "pending_planner_review") {
        if (!mv.reviewer.empty() || !mv.reviewed_at.empty())
            emit(diags, errc::E_REVIEW_STATUS_SEMANTIC, file, "",
                 "manifest.review_status=pending_planner_review requires reviewer=null and reviewed_at=null");
    }
    // reviewer / reviewed_at presence + type already checked at load time
    // (reqNullableString). Remaining: missing-key sweep for every required key.
    for (const auto& k : kManifestRequiredKeys)
        if (!m.contains(k))
            emit(diags, errc::E_MISSING_KEY, file, "", "manifest missing required key: " + k);
}

} // namespace

bool validateDataset(const std::string& dataDir, std::vector<Diagnostic>& out,
                     ValidationSummary& summary) {
    std::error_code ec;
    if (!fs::is_directory(dataDir, ec)) {
        emit(out, errc::E_DATA_DIR_NOT_FOUND, dataDir, "", "data directory not found: " + dataDir);
        return false;
    }
    std::string manifestRel = dataDir + "/manifest.json";
    if (!fs::exists(manifestRel, ec)) {
        emit(out, errc::E_MANIFEST_NOT_FOUND, manifestRel, "", "manifest.json not found at " + manifestRel);
        return false;
    }
    std::string problemsDir = dataDir + "/problems";
    if (!fs::is_directory(problemsDir, ec)) {
        emit(out, errc::E_PROBLEMS_DIR_NOT_FOUND, problemsDir, "", "problems/ directory not found at " + problemsDir);
        return false;
    }

    std::vector<std::string> probFiles;
    for (const auto& e : fs::directory_iterator(problemsDir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".json")
            probFiles.push_back(e.path().filename().string());
    }
    std::sort(probFiles.begin(), probFiles.end());
    if (probFiles.empty()) {
        emit(out, errc::E_PROBLEMS_DIR_NOT_FOUND, problemsDir, "", "problems/ contains no .json files");
        return false;
    }

    ManifestView mv;
    {
        LoadResult lr = loadJsonFile(manifestRel, ".");
        if (!lr.ok) {
            emit(out, lr.error_code.c_str(), manifestRel, "", lr.error_message);
        } else {
            mv.ok = true;
            mv.raw = lr.doc;
            // Read the version/review strings WITHOUT emitting: the real
            // existence + declared-type checks happen in checkManifestSummary
            // (which also compares counts). We only capture the string value
            // when the declared type is correct so downstream logic (meta
            // version consistency, trace review-status consistency) can use it.
            auto readStr = [](const json& j, const char* k) -> std::string {
                if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
                return "";
            };
            mv.schema = readStr(lr.doc, "schema_version");
            mv.taxonomy = readStr(lr.doc, "taxonomy_version");
            mv.dataset = readStr(lr.doc, "dataset_version");
            mv.review_status = readStr(lr.doc, "review_status");
            mv.reviewer = readStr(lr.doc, "reviewer");
            mv.reviewed_at = readStr(lr.doc, "reviewed_at");
            summary.schema_version = mv.schema;
            summary.taxonomy_version = mv.taxonomy;
            summary.dataset_version = mv.dataset;
        }
    }

    Accum acc;
    // PHASE 1: load every file, build the global index (order-independent).
    for (const auto& pf : probFiles) {
        std::string rel = dataDir + "/problems/" + pf;
        LoadResult lr = loadJsonFile(rel, ".");
        if (!lr.ok) {
            emit(out, lr.error_code.c_str(), rel, "", lr.error_message);
            continue;
        }
        if (!lr.doc.is_object()) {
            emit(out, errc::E_TYPE_MISMATCH, rel, "", "problem file top-level must be a JSON object");
            continue;
        }
        loadProblem(lr.doc, rel, mv, acc, out);
    }

    summary.problems = static_cast<int>(probFiles.size());
    summary.traces = acc.traceCount;
    summary.diagnoses = acc.diagCount;
    summary.tests = acc.testCount;
    summary.candidate_solutions = acc.solCount;
    summary.verification_results = acc.verifCount;

    // PHASE 2: resolve FK / association / cardinality / semantics on the
    // complete global index (no dependence on file traversal order).
    resolveForeignKeys(acc, out);

    checkManifestSummary(mv, acc, out);

    for (const auto& d : out)
        if (d.severity == Severity::ERROR) return false;
    return true;
}

void sortDiagnostics(std::vector<Diagnostic>& diags) {
    std::stable_sort(diags.begin(), diags.end(),
                     [](const Diagnostic& a, const Diagnostic& b) {
                         if (a.file != b.file) return a.file < b.file;
                         if (a.object_id != b.object_id) return a.object_id < b.object_id;
                         if (a.code != b.code) return a.code < b.code;
                         return a.message < b.message;
                     });
}

} // namespace hy3
