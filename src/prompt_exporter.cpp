// hy3_algotrace — PromptExporter implementation (Phase 2B-1).

#include "hy3_algotrace/prompt_exporter.hpp"
#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hy3 {
namespace fs = std::filesystem;

namespace {

// Forbidden keys for structural leakage audit (protocol §3.4).
const std::vector<std::string>& forbiddenKeys() {
    static const std::vector<std::string> keys = {
        "diagnoses", "review_status", "reviewer", "reviewed_at",
        "trace_origin", "generator_model", "annotator"};
    return keys;
}

bool hasForbiddenKey(const std::string& k) {
    for (const auto& fk : forbiddenKeys()) {
        if (fk == k) return true;
    }
    return false;
}

// Recursively check JSON keys.
bool containsForbiddenKey(const nlohmann::json& j, std::string& found) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (hasForbiddenKey(it.key())) {
                found = it.key();
                return true;
            }
            if (containsForbiddenKey(it.value(), found)) return true;
        }
    } else if (j.is_array()) {
        for (const auto& el : j) {
            if (containsForbiddenKey(el, found)) return true;
        }
    }
    return false;
}

// Copy a field only if present and of the expected kind. We copy the value
// verbatim (already validated by the Phase 1B DatasetValidator), so we do not
// re-validate here.
void copyIfPresent(const nlohmann::json& src, const std::string& key,
                   nlohmann::json& dst) {
    if (src.contains(key)) {
        dst[key] = src.at(key);
    }
}

// Reject path separators, ".", control chars, spaces, or anything not a safe
// identifier component. Safe set: [A-Za-z0-9_.-] but no "..", no leading dot
// is allowed actually (we keep it strict: alnum, hyphen, underscore).
bool isSafeId(const std::string& id) {
    if (id.empty()) return false;
    if (id == "." || id == "..") return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Marker mentions in the surrounding design notes are not prompt boundaries.
// Only a marker that occupies an entire line is structural.
std::vector<size_t> standaloneMarkerPositions(const std::string& text,
                                              const std::string& marker) {
    std::vector<size_t> positions;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        size_t contentEnd = lineEnd == std::string::npos ? text.size() : lineEnd;
        if (contentEnd > lineStart && text[contentEnd - 1] == '\r') {
            --contentEnd;
        }
        if (contentEnd - lineStart == marker.size() &&
            text.compare(lineStart, marker.size(), marker) == 0) {
            positions.push_back(lineStart);
        }
        if (lineEnd == std::string::npos) break;
        lineStart = lineEnd + 1;
    }
    return positions;
}

} // namespace

// --- Template boundary extraction ------------------------------------------

ExporterResult extractTemplateBody(const std::string& text,
                                   std::string& body) {
    const std::string beginMark = "<!-- HY3_PROMPT_BEGIN -->";
    const std::string endMark = "<!-- HY3_PROMPT_END -->";
    ExporterResult res;

    const auto begins = standaloneMarkerPositions(text, beginMark);
    const auto ends = standaloneMarkerPositions(text, endMark);
    if (begins.empty()) {
        res.error_code = exporter_errc::E_TEMPLATE_MARKER;
        res.message = "missing BEGIN marker: " + beginMark;
        return res;
    }
    if (begins.size() != 1) {
        res.error_code = exporter_errc::E_TEMPLATE_MARKER;
        res.message = "duplicate BEGIN marker";
        return res;
    }
    if (ends.empty()) {
        res.error_code = exporter_errc::E_TEMPLATE_MARKER;
        res.message = "missing END marker: " + endMark;
        return res;
    }
    if (ends.size() != 1) {
        res.error_code = exporter_errc::E_TEMPLATE_MARKER;
        res.message = "duplicate END marker";
        return res;
    }
    const size_t b = begins.front();
    const size_t e = ends.front();
    if (e < b + beginMark.size()) {
        res.error_code = exporter_errc::E_TEMPLATE_MARKER;
        res.message = "END marker appears before BEGIN marker";
        return res;
    }

    body = text.substr(b + beginMark.size(), e - (b + beginMark.size()));
    res.ok = true;
    return res;
}

// --- Allowlist projection --------------------------------------------------

ExporterResult projectTraceInput(const nlohmann::json& problemJson,
                                 const nlohmann::json& traceJson,
                                 nlohmann::json& out) {
    ExporterResult res;
    out = nlohmann::json::object();

    // Problem (allowlist only; explicitly NOT problem.notes). Dataset files
    // store these fields under the top-level "problem" object.
    const nlohmann::json& problemFields =
        problemJson.contains("problem") && problemJson.at("problem").is_object()
            ? problemJson.at("problem")
            : problemJson;
    nlohmann::json problem;
    copyIfPresent(problemFields, "id", problem);
    copyIfPresent(problemFields, "source", problem);
    copyIfPresent(problemFields, "title", problem);
    copyIfPresent(problemFields, "statement", problem);
    copyIfPresent(problemFields, "constraints", problem);
    copyIfPresent(problemFields, "algorithm_type", problem);
    copyIfPresent(problemFields, "reference_tags", problem);
    out["problem"] = problem;

    // Reference verdict (allowlist only).
    nlohmann::json rv;
    copyIfPresent(problemJson, "expected_choice", rv);
    copyIfPresent(problemJson, "expected_proof", rv);
    copyIfPresent(problemJson, "expected_complexity", rv);
    copyIfPresent(problemJson, "expected_boundaries", rv);
    copyIfPresent(problemJson, "common_wrong_strategy_counterexample", rv);
    // Note: reference_verdict lives under problemJson["reference_verdict"].
    if (problemJson.contains("reference_verdict") &&
        problemJson.at("reference_verdict").is_object()) {
        const auto& src = problemJson.at("reference_verdict");
        copyIfPresent(src, "expected_choice", rv);
        copyIfPresent(src, "expected_proof", rv);
        copyIfPresent(src, "expected_complexity", rv);
        copyIfPresent(src, "expected_boundaries", rv);
        copyIfPresent(src, "common_wrong_strategy_counterexample", rv);
    }
    out["reference_verdict"] = rv;

    // Test cases (allowlist only; explicitly NOT test_cases.notes).
    nlohmann::json testCases = nlohmann::json::array();
    if (problemJson.contains("test_cases") && problemJson.at("test_cases").is_array()) {
        for (const auto& tc : problemJson.at("test_cases")) {
            nlohmann::json t;
            copyIfPresent(tc, "id", t);
            copyIfPresent(tc, "input", t);
            copyIfPresent(tc, "expected_output", t);
            copyIfPresent(tc, "origin", t);
            copyIfPresent(tc, "purpose", t);
            testCases.push_back(t);
        }
    }
    out["test_cases"] = testCases;

    // Reasoning trace (allowlist only).
    nlohmann::json trace;
    copyIfPresent(traceJson, "id", trace);
    copyIfPresent(traceJson, "problem_id", trace);
    copyIfPresent(traceJson, "author", trace);
    copyIfPresent(traceJson, "steps", trace);
    if (traceJson.contains("intended_outcome")) {
        trace["intended_outcome"] = traceJson.at("intended_outcome");
    }
    out["reasoning_trace"] = trace;

    // Candidate solution: only the one whose trace_id matches this trace.
    nlohmann::json candidate = nlohmann::json::value_t::null;
    if (traceJson.contains("id") && traceJson.at("id").is_string() &&
        problemJson.contains("candidate_solutions") &&
        problemJson.at("candidate_solutions").is_array()) {
        std::string tid = traceJson.at("id").get<std::string>();
        for (const auto& sol : problemJson.at("candidate_solutions")) {
            if (sol.contains("trace_id") && sol.at("trace_id").is_string() &&
                sol.at("trace_id").get<std::string>() == tid) {
                nlohmann::json c;
                copyIfPresent(sol, "id", c);
                copyIfPresent(sol, "trace_id", c);
                copyIfPresent(sol, "language", c);
                copyIfPresent(sol, "standard", c);
                copyIfPresent(sol, "source_code", c);
                copyIfPresent(sol, "execution_status", c);
                candidate = c;
                break;
            }
        }
    }
    out["candidate_solution"] = candidate;

    res.ok = true;
    return res;
}

// --- Structural leakage audit ---------------------------------------------

ExporterResult auditStructuralLeakage(const nlohmann::json& payload,
                                      std::string& offendingKey) {
    ExporterResult res;
    if (containsForbiddenKey(payload, offendingKey)) {
        res.error_code = exporter_errc::E_LEAKAGE_AUDIT;
        res.message = "forbidden key found in input payload: " + offendingKey;
        return res;
    }
    res.ok = true;
    return res;
}

// --- Prompt rendering ------------------------------------------------------

ExporterResult renderPrompt(const std::string& body,
                            const nlohmann::json& placeholderJson,
                            std::string& out) {
    ExporterResult res;
    static const std::vector<std::string> placeholders = {
        "{{problem_json}}",
        "{{reference_verdict_json}}",
        "{{test_cases_json}}",
        "{{reasoning_trace_json}}",
        "{{candidate_solution_json_or_null}}"};

    // Each placeholder must appear exactly once in the body.
    for (const auto& ph : placeholders) {
        size_t c = 0, pos = body.find(ph);
        while (pos != std::string::npos) {
            c++;
            pos = body.find(ph, pos + ph.size());
        }
        if (c != 1) {
            res.error_code = exporter_errc::E_TEMPLATE_PLACEHOLDER;
            res.message = "placeholder must appear exactly once: " + ph +
                          " (found " + std::to_string(c) + ")";
            return res;
        }
    }

    // Build replacement text for each placeholder.
    std::string problemJson, rvJson, tcJson, traceJson, solJson;
    if (placeholderJson.contains("problem_json"))
        problemJson = serializeStable(placeholderJson.at("problem_json"));
    if (placeholderJson.contains("reference_verdict_json"))
        rvJson = serializeStable(placeholderJson.at("reference_verdict_json"));
    if (placeholderJson.contains("test_cases_json"))
        tcJson = serializeStable(placeholderJson.at("test_cases_json"));
    if (placeholderJson.contains("reasoning_trace_json"))
        traceJson = serializeStable(placeholderJson.at("reasoning_trace_json"));
    if (placeholderJson.contains("candidate_solution_json_or_null"))
        solJson = serializeStable(placeholderJson.at("candidate_solution_json_or_null"));

    std::string result = body;
    auto replaceOnce = [&](const std::string& ph, const std::string& rep) {
        size_t pos = result.find(ph);
        if (pos != std::string::npos) {
            result.replace(pos, ph.size(), rep);
        }
    };
    replaceOnce("{{problem_json}}", problemJson);
    replaceOnce("{{reference_verdict_json}}", rvJson);
    replaceOnce("{{test_cases_json}}", tcJson);
    replaceOnce("{{reasoning_trace_json}}", traceJson);
    replaceOnce("{{candidate_solution_json_or_null}}", solJson);

    // No residual placeholder allowed.
    for (const auto& ph : placeholders) {
        if (result.find(ph) != std::string::npos) {
            res.error_code = exporter_errc::E_TEMPLATE_PLACEHOLDER;
            res.message = "residual placeholder after substitution: " + ph;
            return res;
        }
    }

    out = result;
    res.ok = true;
    return res;
}

// --- Serialization ---------------------------------------------------------

std::string serializeStable(const nlohmann::json& j) {
    // 2-space indent; nlohmann preserves object key insertion order by default.
    return j.dump(2);
}

// --- Manifest --------------------------------------------------------------

nlohmann::json buildManifestJson(const RunManifest& m) {
    nlohmann::json j;
    j["evaluation_schema_version"] = m.evaluation_schema_version;
    j["run_id"] = m.run_id;
    j["dataset_version"] = m.dataset_version;
    j["dataset_commit"] = m.dataset_commit;
    j["taxonomy_version"] = m.taxonomy_version;
    j["model_provider"] = m.model_provider;
    j["model_name"] = m.model_name;
    j["model_version"] = nlohmann::json::value_t::null; // always null in v0.1.0
    j["pipeline_commit"] = m.pipeline_commit;
    j["prompt_template_id"] = m.prompt_template_id;
    j["prompt_template_sha256"] = m.prompt_template_sha256;
    j["input_mode"] = m.input_mode;
    j["started_at"] = m.started_at;
    j["completed_at"] = nlohmann::json::value_t::null;
    j["trace_ids"] = nlohmann::json(m.trace_ids); // already sorted
    j["total_traces"] = m.total_traces;
    j["notes"] = m.notes;
    return j;
}

// --- Top-level export ------------------------------------------------------

ExporterResult exportPrompts(const std::string& dataDir,
                             const std::string& templateText,
                             const std::string& runDir,
                             const RunManifest& manifestInput,
                             std::string& promptTemplateSha256) {
    ExporterResult res;

    // 1) Safety: run_dir must not already exist.
    if (fs::exists(runDir)) {
        res.error_code = exporter_errc::E_RUN_DIR_EXISTS;
        res.message = "run directory already exists (refuse to overwrite): " + runDir;
        return res;
    }

    // 2) Load dataset (reuse JsonLoader; no business rules here).
    auto loadFile = [&](const std::string& rel) -> ExporterResult {
        LoadResult lr = loadJsonFile(rel, dataDir);
        ExporterResult r;
        if (!lr.ok) {
            r.error_code = exporter_errc::E_DATA_LOAD;
            r.message = lr.error_message;
            return r;
        }
        r.ok = true;
        return r;
    };

    LoadResult manifestLr = loadJsonFile("manifest.json", dataDir);
    if (!manifestLr.ok) {
        res.error_code = exporter_errc::E_DATA_LOAD;
        res.message = manifestLr.error_message;
        return res;
    }
    const nlohmann::json& manifest = manifestLr.doc;
    if (!manifest.contains("problem_ids") || !manifest.at("problem_ids").is_array()) {
        res.error_code = exporter_errc::E_DATA_LOAD;
        res.message = "manifest.json missing problem_ids array";
        return res;
    }

    // 3) Canonicalize template bytes, then extract and hash its body.
    std::vector<uint8_t> rawTemplate(templateText.begin(), templateText.end());
    std::vector<uint8_t> normalizedTemplate;
    std::string normalizationError;
    if (!normalizeUtf8(rawTemplate, normalizedTemplate, normalizationError)) {
        res.error_code = normalizationError;
        res.message = "template normalization failed: " + normalizationError;
        return res;
    }
    const std::string canonicalTemplate(normalizedTemplate.begin(),
                                        normalizedTemplate.end());
    std::string body;
    ExporterResult extr = extractTemplateBody(canonicalTemplate, body);
    if (!extr.ok) return extr;
    promptTemplateSha256 = sha256_hex(body);

    // 4) Prepare a temp dir sibling to runDir; fail-safe.
    fs::path runPath(runDir);
    fs::path parent = runPath.parent_path();
    if (parent.empty()) parent = ".";
    fs::path tempDir = parent / (runPath.filename().string() + ".tmp-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));

    auto failCleanup = [&](const std::string& code, const std::string& msg) {
        std::error_code ec;
        fs::remove_all(tempDir, ec); // scoped temp only; never callers' dirs
        ExporterResult r;
        r.error_code = code;
        r.message = msg;
        return r;
    };

    std::error_code ec;
    if (!fs::create_directories(tempDir / "prompts", ec)) {
        return failCleanup(exporter_errc::E_WRITE_FAILED,
                           "cannot create temp prompts dir: " + ec.message());
    }
    fs::create_directories(tempDir / "raw-responses", ec);
    fs::create_directories(tempDir / "predictions", ec);

    // 5) Collect trace ids (lexicographic) across all problems.
    std::vector<std::string> allTraceIds;
    struct TraceRef { std::string problemId; nlohmann::json problem; nlohmann::json trace; };
    std::vector<TraceRef> refs;

    for (const auto& pidVal : manifest.at("problem_ids")) {
        if (!pidVal.is_string()) continue;
        std::string pid = pidVal.get<std::string>();
        if (!isSafeId(pid)) {
            return failCleanup(exporter_errc::E_UNSAFE_ID,
                               "unsafe problem id: " + pid);
        }
        std::string rel = "problems/" + pid + ".json";
        LoadResult plr = loadJsonFile(rel, dataDir);
        if (!plr.ok) {
            return failCleanup(exporter_errc::E_DATA_LOAD, plr.error_message);
        }
        const nlohmann::json& prob = plr.doc;
        if (!prob.contains("reasoning_traces") || !prob.at("reasoning_traces").is_array()) {
            return failCleanup(exporter_errc::E_DATA_LOAD,
                               "problem missing reasoning_traces: " + pid);
        }
        for (const auto& tr : prob.at("reasoning_traces")) {
            if (!tr.contains("id") || !tr.at("id").is_string()) {
                return failCleanup(exporter_errc::E_DATA_LOAD,
                                   "trace missing string id in " + pid);
            }
            std::string tid = tr.at("id").get<std::string>();
            if (!isSafeId(tid)) {
                return failCleanup(exporter_errc::E_UNSAFE_ID,
                                   "unsafe trace id: " + tid);
            }
            // FK sanity: trace.problem_id must match the file's problem.id.
            if (prob.contains("problem") && prob.at("problem").contains("id") &&
                prob.at("problem").at("id").is_string()) {
                if (tr.contains("problem_id") && tr.at("problem_id").is_string() &&
                    tr.at("problem_id").get<std::string>() !=
                        prob.at("problem").at("id").get<std::string>()) {
                    return failCleanup(exporter_errc::E_FOREIGN_KEY,
                                       "trace problem_id mismatch: " + tid);
                }
            }
            refs.push_back({pid, prob, tr});
            allTraceIds.push_back(tid);
        }
    }

    std::sort(allTraceIds.begin(), allTraceIds.end());
    // de-duplicate (shouldn't happen, but be safe)
    allTraceIds.erase(std::unique(allTraceIds.begin(), allTraceIds.end()),
                      allTraceIds.end());

    // 6) For each trace: project, audit, render, write prompt file.
    for (const auto& ref : refs) {
        nlohmann::json projected;
        ExporterResult pr = projectTraceInput(ref.problem, ref.trace, projected);
        if (!pr.ok) return failCleanup(pr.error_code, pr.message);

        // Leakage audit on the whole projected payload.
        std::string badKey;
        ExporterResult au = auditStructuralLeakage(projected, badKey);
        if (!au.ok) return failCleanup(au.error_code, au.message);

        // Render.
        nlohmann::json ph;
        ph["problem_json"] = projected.at("problem");
        ph["reference_verdict_json"] = projected.at("reference_verdict");
        ph["test_cases_json"] = projected.at("test_cases");
        ph["reasoning_trace_json"] = projected.at("reasoning_trace");
        ph["candidate_solution_json_or_null"] = projected.at("candidate_solution");
        std::string prompt;
        ExporterResult rn = renderPrompt(body, ph, prompt);
        if (!rn.ok) return failCleanup(rn.error_code, rn.message);

        // Normalize + write. Then hash the written bytes.
        std::vector<uint8_t> raw(prompt.begin(), prompt.end());
        std::vector<uint8_t> norm;
        std::string normErr;
        if (!normalizeUtf8(raw, norm, normErr)) {
            return failCleanup(sha_errc::E_UTF8_INVALID, normErr);
        }
        fs::path outPath = tempDir / "prompts" / (ref.trace.at("id").get<std::string>() + ".txt");
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs) {
            return failCleanup(exporter_errc::E_WRITE_FAILED,
                               "cannot open prompt file: " + outPath.string());
        }
        ofs.write(reinterpret_cast<const char*>(norm.data()),
                  static_cast<std::streamsize>(norm.size()));
        if (!ofs) {
            return failCleanup(exporter_errc::E_WRITE_FAILED,
                               "write failed: " + outPath.string());
        }
    }

    // 7) Manifest (with computed template hash + sorted trace_ids).
    RunManifest m = manifestInput;
    m.prompt_template_sha256 = promptTemplateSha256;
    m.trace_ids = allTraceIds;
    m.total_traces = static_cast<int>(allTraceIds.size());
    // dataset_version pulled from manifest if not provided.
    if (m.dataset_version.empty() && manifest.contains("dataset_version") &&
        manifest.at("dataset_version").is_string()) {
        m.dataset_version = manifest.at("dataset_version").get<std::string>();
    }
    nlohmann::json mj = buildManifestJson(m);
    std::string mjText = mj.dump(2);
    {
        std::vector<uint8_t> raw(mjText.begin(), mjText.end());
        std::vector<uint8_t> norm;
        std::string normErr;
        if (!normalizeUtf8(raw, norm, normErr)) {
            return failCleanup(sha_errc::E_UTF8_INVALID, normErr);
        }
        std::ofstream ofs(tempDir / "run-manifest.json", std::ios::binary);
        if (!ofs) {
            return failCleanup(exporter_errc::E_WRITE_FAILED,
                               "cannot open run-manifest.json");
        }
        ofs.write(reinterpret_cast<const char*>(norm.data()),
                  static_cast<std::streamsize>(norm.size()));
        if (!ofs) {
            return failCleanup(exporter_errc::E_WRITE_FAILED,
                               "write failed: run-manifest.json");
        }
    }

    // 8) Atomic-ish publish: rename temp -> runDir.
    std::error_code rec;
    fs::rename(tempDir, runDir, rec);
    if (rec) {
        // Partial: try to clean temp, leave runDir absent.
        fs::remove_all(tempDir, ec);
        return failCleanup(exporter_errc::E_WRITE_FAILED,
                           "rename temp->run failed: " + rec.message());
    }

    res.ok = true;
    return res;
}

} // namespace hy3
