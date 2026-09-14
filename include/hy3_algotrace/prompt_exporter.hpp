// hy3_algotrace — PromptExporter (Phase 2B-1)
//
// Deterministic, auditable, gold-label-free export of evaluation prompts from
// the frozen dataset. This module implements ONLY the export half of the
// offline evaluation pipeline (see docs/phase-02-protocol.md); it does NOT
// call any model, does NOT produce predictions, and does NOT compute metrics.
//
// Responsibilities (kept as separate functions/classes):
//   * text normalization + SHA-256          (sha256.hpp)
//   * template boundary extraction         (extractTemplateBody)
//   * explicit allowlist JSON projection    (projectTraceInput)
//   * structural leakage audit             (auditStructuralLeakage)
//   * prompt rendering                      (renderPrompt)
//   * run directory + manifest writing      (exportPrompts)
//
// All public entry points return a Result carrying either a value or a
// stable error code (errc::E_* from diagnostic.hpp plus exporter-specific
// codes declared in exporter_errc). They never throw across the CLI boundary.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hy3_algotrace/diagnostic.hpp"

namespace hy3 {

// Exporter-specific stable error codes (in addition to errc::* from
// diagnostic.hpp). Used both as observable failure signals and in tests.
namespace exporter_errc {
    inline constexpr const char* E_TEMPLATE_MARKER   = "E_TEMPLATE_MARKER";   // missing/dup/reversed BEGIN|END
    inline constexpr const char* E_TEMPLATE_PLACEHOLDER = "E_TEMPLATE_PLACEHOLDER"; // missing/dup/unknown/residual {{}}
    inline constexpr const char* E_LEAKAGE_AUDIT     = "E_LEAKAGE_AUDIT";      // forbidden key found in payload
    inline constexpr const char* E_RUN_DIR_EXISTS    = "E_RUN_DIR_EXISTS";    // refuse to overwrite
    inline constexpr const char* E_UNSAFE_ID         = "E_UNSAFE_ID";         // run_id/trace_id has path sep or ".."
    inline constexpr const char* E_WRITE_FAILED      = "E_WRITE_FAILED";      // filesystem write error
    inline constexpr const char* E_BAD_ARGUMENT      = "E_BAD_ARGUMENT";      // missing required CLI arg
    inline constexpr const char* E_DATA_LOAD         = "E_DATA_LOAD";         // cannot load/parse dataset
    inline constexpr const char* E_FOREIGN_KEY       = "E_FOREIGN_KEY";       // trace/solution FK mismatch
} // namespace exporter_errc

struct ExporterResult {
    bool ok = false;
    std::string error_code;   // stable code (exporter_errc::* or errc::*)
    std::string message;      // human-readable detail
};

// --- Template boundary extraction ------------------------------------------
//
// Extracts the text strictly between the standalone marker lines
//   <!-- HY3_PROMPT_BEGIN -->  and  <!-- HY3_PROMPT_END -->
// Inline mentions in surrounding design notes are ignored. The markers
// themselves are NOT included. Leading/trailing whitespace of the captured
// text is preserved verbatim (no trim). `body` is set only on success.
ExporterResult extractTemplateBody(const std::string& templateText,
                                   std::string& body);

// --- Allowlist projection --------------------------------------------------
//
// Builds a fresh JSON object containing ONLY the allowed fields for a single
// trace (`traceJson`), drawing from its owning problem (`problemJson`) and the
// problem's candidate_solutions array. The `traceId` and `problemId` are used
// to locate the associated candidate solution (by `trace_id`).
//
// `intendedOutcomeExists` controls whether `intended_outcome` is copied (only
// when the source trace actually has it).
//
// Returns a JSON object with exactly the keys permitted by the protocol:
//   problem, reference_verdict, test_cases (no `notes`), reasoning_trace,
//   candidate_solution (or JSON null when none associated).
ExporterResult projectTraceInput(const nlohmann::json& problemJson,
                                 const nlohmann::json& traceJson,
                                 nlohmann::json& out);

// --- Structural leakage audit ---------------------------------------------
//
// Recursively scans JSON keys for any forbidden key. Forbidden set:
//   diagnoses, review_status, reviewer, reviewed_at,
//   trace_origin, generator_model, annotator
// Only keys are checked (not string values), per protocol §3.4. Returns ok
// when clean; on failure `offendingKey` holds the first forbidden key found.
ExporterResult auditStructuralLeakage(const nlohmann::json& payload,
                                      std::string& offendingKey);

// --- Prompt rendering ------------------------------------------------------
//
// Replaces the five placeholders in `body` with the JSON *text* of the five
// projected sections. Each placeholder must appear exactly once and none may
// remain after substitution. `out` is the fully rendered prompt (UTF-8, LF).
// `placeholderJson` must contain keys: problem_json, reference_verdict_json,
// test_cases_json, reasoning_trace_json, candidate_solution_json_or_null.
ExporterResult renderPrompt(const std::string& body,
                            const nlohmann::json& placeholderJson,
                            std::string& out);

// JSON serialization policy for projected payloads and the run-manifest.
// Stable across runs: 2-space indent, no trailing spaces, keys in the order
// they were inserted (nlohmann preserves insertion order for object keys).
std::string serializeStable(const nlohmann::json& j);

// --- Run manifest ----------------------------------------------------------
struct RunManifest {
    std::string evaluation_schema_version = "0.1.0";
    std::string run_id;
    std::string dataset_version;
    std::string dataset_commit = "fb40cb2f8f93967a93a376508c5a0d9c3f3f4df9";
    std::string taxonomy_version = "1.0.0";
    std::string model_provider = "tencent-hunyuan";
    std::string model_name = "hy3";
    std::string model_version = "null"; // written as JSON null in manifest
    std::string pipeline_commit;
    std::string prompt_template_id = "hy3-evaluator-v1";
    std::string prompt_template_sha256;
    std::string input_mode = "reference_assisted";
    std::string started_at;
    std::string completed_at = "null"; // written as JSON null
    std::vector<std::string> trace_ids; // sorted lexicographically
    int total_traces = 0;
    std::string notes;
};

// Build the manifest JSON object from the struct (with proper null handling).
nlohmann::json buildManifestJson(const RunManifest& m);

// --- Top-level export ------------------------------------------------------
//
// Exports prompts for every trace in `dataDir` using `templateText` raw file
// bytes. The exporter canonicalizes UTF-8/BOM/newlines before extraction,
// hashing, and rendering. Results are written under `runDir`
// (which MUST NOT already exist). `runDir` is created only after a temp stage
// succeeds, then renamed into place (atomic-ish, failure-safe).
//
// On success: runDir/prompts/<trace_id>.txt (9 files, lexicographic order),
// runDir/raw-responses/ (empty), runDir/predictions/ (empty),
// runDir/run-manifest.json. Returns ok and fills `promptTemplateSha256`.
ExporterResult exportPrompts(const std::string& dataDir,
                             const std::string& templateText,
                             const std::string& runDir,
                             const RunManifest& manifestInput,
                             std::string& promptTemplateSha256);

} // namespace hy3
