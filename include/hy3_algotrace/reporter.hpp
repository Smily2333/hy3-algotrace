// hy3_algotrace — Reporter (Phase 2B)
//
// Loads a run-manifest + all prediction wrappers + frozen gold diagnoses, then
// produces report.json and report.md strictly per docs/phase-02-metrics.md.
// Only the Reporter reads gold; gold is never written into prediction files.
//
// Key invariants (from protocol §7 + metrics §0):
//   * missing wrapper -> run reported incomplete (never defaulted to correct)
//   * parse_status != parsed -> prediction counted as a failure
//   * __parse_failed__ sentinel used ONLY in memory for micro metrics
//   * per-trace same-category dedup, same (stage,category) pair dedup
//   * zero-denominator rules; N/A vs numeric 0 distinct
//   * no confidence comparison (Phase 4)
//   * report.json deterministic; report.md numbers match JSON
//   * non-deterministic fields (time) injected via parameters
//
// All metrics are computed from: predictions/<tid>.json (prediction field, or
// null when not parsed) and data/problems/<id>.json (diagnoses[] gold).
//
// Public entry points return ReporterResult with stable error codes.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hy3_algotrace/diagnostic.hpp"

namespace hy3 {

namespace reporter_errc {
    inline constexpr const char* E_RUN_DIR_MISSING    = "E_RUN_DIR_MISSING";
    inline constexpr const char* E_MANIFEST_READ      = "E_MANIFEST_READ";
    inline constexpr const char* E_DATA_LOAD          = "E_DATA_LOAD";
    inline constexpr const char* E_WRAPPER_MISSING    = "E_WRAPPER_MISSING"; // run incomplete
    inline constexpr const char* E_WRAPPER_PARSE      = "E_WRAPPER_PARSE";
    inline constexpr const char* E_GOLD_NOT_FOUND     = "E_GOLD_NOT_FOUND";
    inline constexpr const char* E_WRITE_FAILED       = "E_WRITE_FAILED";
    inline constexpr const char* E_BAD_ARGUMENT       = "E_BAD_ARGUMENT";
} // namespace reporter_errc

struct ReporterResult {
    bool ok = false;
    bool run_complete = false;     // false when some wrappers missing
    std::string error_code;
    std::string message;
};

// Load gold diagnosis for a (problem_id, trace_id) pair from the dataset.
// Returns the diagnosis JSON (with status / primary_category / findings) or an
// error if not found.
ReporterResult loadGoldDiagnosis(const std::string& dataDir,
                                 const std::string& problemId,
                                 const std::string& traceId,
                                 nlohmann::json& goldOut);

// Build the full report JSON. `completedAt` is injected (ISO-8601 or "null");
// `generatedAt` is injected for the report metadata. `dataDir` is used to load
// gold. On success `reportJson` holds the machine-readable report.
// `runComplete` is false if any trace in the manifest lacks a wrapper.
ReporterResult buildReport(const std::string& runDir,
                           const std::string& dataDir,
                           const std::string& completedAt,
                           const std::string& reportGeneratedAt,
                           nlohmann::json& reportJson,
                           bool& runComplete);

// Write report.json + report.md under runDir. `reportJson` must come from
// buildReport. Returns ok on success.
ReporterResult writeReport(const std::string& runDir,
                           const nlohmann::json& reportJson);

// Top-level: build + write. Also, if runComplete and completedAt is a valid
// ISO-8601 string, update run-manifest.completed_at. Refuses to update when
// not complete.
ReporterResult generateReport(const std::string& runDir,
                              const std::string& dataDir,
                              const std::string& completedAt,
                              const std::string& reportGeneratedAt);

// Render the markdown report (deterministic; numbers mirror reportJson).
std::string renderReportMarkdown(const nlohmann::json& reportJson);

} // namespace hy3
