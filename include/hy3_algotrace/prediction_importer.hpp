// hy3_algotrace — PredictionImporter (Phase 2B)
//
// Offline ingestion of a model's raw response into a deterministic, auditable
// prediction wrapper. This module implements ONLY the import half of the
// offline evaluation pipeline (see docs/phase-02-protocol.md); it does NOT
// call any model, does NOT compute metrics, and does NOT read gold labels.
//
// Responsibilities (kept as separate functions/classes):
//   * save raw response byte-for-byte (raw_response_sha256 over raw bytes)
//   * load + verify the corresponding prompt (prompt_sha256)
//   * strict parse_status discrimination (6 states)
//   * no silent stripping of fences / preamble / trailing text
//   * no JSON repair
//   * schema validation (required keys, enum, types)
//   * semantic validation (status vs primary_category/findings)
//   * generate predictions/<trace_id>.json wrapper
//   * null prediction when parse_status != parsed
//   * gold diagnosis never enters the wrapper
//   * internal sentinel never written to file
//   * trace_id consistency checks
//   * refuse to overwrite existing raw/prediction
//   * explicit "not attempted" marker (must be explicit, not inferred)
//
// All public entry points return a Result carrying either a value or a stable
// error code (importer_errc::*). They never throw across the CLI boundary.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hy3_algotrace/diagnostic.hpp"

namespace hy3 {

// Importer-specific stable error codes.
namespace importer_errc {
    inline constexpr const char* E_RUN_DIR_MISSING   = "E_RUN_DIR_MISSING";
    inline constexpr const char* E_PROMPT_MISSING     = "E_PROMPT_MISSING";
    inline constexpr const char* E_RAW_EXISTS         = "E_RAW_EXISTS";     // refuse overwrite
    inline constexpr const char* E_PREDICTION_EXISTS  = "E_PREDICTION_EXISTS"; // refuse overwrite
    inline constexpr const char* E_UNSAFE_ID          = "E_UNSAFE_ID";
    inline constexpr const char* E_BAD_ARGUMENT       = "E_BAD_ARGUMENT";
    inline constexpr const char* E_WRITE_FAILED       = "E_WRITE_FAILED";
    inline constexpr const char* E_PROMPT_SHA_MISMATCH = "E_PROMPT_SHA_MISMATCH";
    inline constexpr const char* E_TRACE_ID_MISMATCH  = "E_TRACE_ID_MISMATCH";
    inline constexpr const char* E_INVALID_RAW_BYTES  = "E_INVALID_RAW_BYTES"; // invalid UTF-8 raw
} // namespace importer_errc

// The six canonical parse statuses from protocol §5.
enum class ParseStatus {
    ModelCallNotAttempted,
    EmptyResponse,
    InvalidJson,
    SchemaInvalid,
    SemanticInvalid,
    Parsed
};

// Frozen taxonomy enums (data contract / architecture §4 / prompt template).
namespace taxonomy {
    // status enum
    inline const std::vector<std::string>& statuses() {
        static const std::vector<std::string> v = {"correct", "incorrect", "undetermined"};
        return v;
    }
    // 7 error categories
    inline const std::vector<std::string>& categories() {
        static const std::vector<std::string> v = {
            "problem_misunderstanding", "wrong_greedy_choice", "missing_greedy_proof",
            "invalid_greedy_proof", "complexity_error", "boundary_omission",
            "implementation_mismatch"};
        return v;
    }
    // 6 reasoning stages (architecture §4)
    inline const std::vector<std::string>& stages() {
        static const std::vector<std::string> v = {
            "problem_understanding", "greedy_choice", "greedy_proof",
            "complexity", "boundary", "implementation_consistency"};
        return v;
    }
    inline bool isStatus(const std::string& s) {
        for (const auto& x : statuses()) if (x == s) return true;
        return false;
    }
    inline bool isCategory(const std::string& s) {
        for (const auto& x : categories()) if (x == s) return true;
        return false;
    }
    inline bool isStage(const std::string& s) {
        for (const auto& x : stages()) if (x == s) return true;
        return false;
    }
} // namespace taxonomy

struct ImporterResult {
    bool ok = false;
    std::string error_code;   // stable code (importer_errc::* or errc::*)
    std::string message;      // human-readable detail
};

// --- Raw response saving ---------------------------------------------------
//
// Saves `rawBytes` verbatim to <run_dir>/raw-responses/<trace_id>.txt (binary,
// no newline translation). Computes SHA-256 over the raw bytes. Refuses if the
// file already exists (overwrite not supported in v1). Verifies trace_id is a
// safe identifier.
ImporterResult saveRawResponse(const std::string& runDir,
                               const std::string& traceId,
                               const std::vector<uint8_t>& rawBytes,
                               std::string& rawResponseSha256);

// --- Prompt loading + hash -------------------------------------------------
//
// Loads <run_dir>/prompts/<trace_id>.txt, normalizes (UTF-8, LF, strip BOM),
// and computes prompt_sha256. Refuses if the prompt is absent.
ImporterResult loadPromptSha(const std::string& runDir,
                             const std::string& traceId,
                             std::string& promptSha256,
                             std::string& normalizedPromptText);

// --- Parse + validate a raw response --------------------------------------
//
// Parses `rawText` (UTF-8 assumed; the raw file bytes are decoded by caller).
// Discriminates the six parse statuses:
//   empty -> empty_response
//   non-JSON -> invalid_json
//   JSON but schema fail -> schema_invalid (errors filled)
//   JSON schema ok but semantic fail -> semantic_invalid (errors filled)
//   all pass -> parsed (prediction = validated structure)
// `expectedTraceId` from the run context is compared against the parsed
// `trace_id` (trace_id consistency). `hasCandidateSolution` indicates whether
// the originating trace had an associated candidate_solution (used to enforce
// the protocol rule that implementation_consistency findings require one).
// On success `parseStatus` and `prediction` (JSON null unless parsed) are set.
// `errors` carries schema/semantic failure reasons; empty on parsed success.
ImporterResult classifyResponse(const std::string& rawText,
                                const std::string& expectedTraceId,
                                bool hasCandidateSolution,
                                ParseStatus& parseStatus,
                                nlohmann::json& prediction,
                                std::vector<std::string>& errors);

// --- Wrapper writing -------------------------------------------------------
//
// Writes <run_dir>/predictions/<trace_id>.json with the wrapper structure from
// protocol §7. Refuses if the file already exists. `prediction` must be JSON
// null when parseStatus != parsed (caller enforces; this writes as given).
ImporterResult writePredictionWrapper(const std::string& runDir,
                                      const std::string& traceId,
                                      const std::string& runId,
                                      ParseStatus parseStatus,
                                      const std::string& promptSha256,
                                      const std::string& rawResponseSha256,
                                      const nlohmann::json& prediction,
                                      const std::vector<std::string>& errors,
                                      const std::string& generatedAt);

// --- Top-level import ------------------------------------------------------
//
// importResponse(runDir, traceId, rawFilePath, runId, generatedAt):
//   1) read raw file bytes verbatim
//   2) saveRawResponse (byte hash, refuse overwrite)
//   3) loadPromptSha
//   4) classifyResponse
//   5) writePredictionWrapper
// Returns ok on success. The gold label is never touched.
ImporterResult importResponse(const std::string& runDir,
                              const std::string& traceId,
                              const std::string& rawFilePath,
                              const std::string& runId,
                              const std::string& generatedAt);

// --- Explicit "not attempted" marker --------------------------------------
//
// Creates a model_call_not_attempted wrapper for `traceId`. This is EXPLICIT:
// it must be called by the operator; absence of a raw file is NEVER silently
// inferred as not_attempted. Refuses if a prediction wrapper already exists.
ImporterResult markNotAttempted(const std::string& runDir,
                                const std::string& traceId,
                                const std::string& runId,
                                const std::string& generatedAt);

// Helper: serialize wrapper with 2-space indent (stable).
std::string serializeWrapper(const nlohmann::json& j);

} // namespace hy3
