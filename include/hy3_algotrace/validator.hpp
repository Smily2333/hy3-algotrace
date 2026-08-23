// hy3_algotrace — DatasetValidator
//
// Validates the on-disk dataset (manifest + problem files) against the
// executable rules derived from docs/data-contract.md (schema 0.3.0) and
// docs/error-taxonomy.md. It:
//   * loads the manifest and every problem file (via JsonLoader),
//   * checks file/JSON structure, version & problem consistency,
//   * validates IDs and foreign keys,
//   * validates diagnosis rules, reasoning traces and candidate solutions,
//   * recomputes the manifest summary counts and compares them,
//   * accumulates as MANY diagnostics as possible (does not stop at first),
//   * fills a ValidationSummary used by the CLI report.
//
// No business rule returns early; all findings are collected so a single run
// reports everything that is wrong.

#pragma once

#include <string>
#include <vector>

#include "hy3_algotrace/diagnostic.hpp"

namespace hy3 {

// Aggregated, report-friendly counters produced while validating.
struct ValidationSummary {
    std::string schema_version;
    std::string taxonomy_version;
    std::string dataset_version;
    int problems = 0;
    int traces = 0;
    int diagnoses = 0;
    int tests = 0;
    int candidate_solutions = 0;
    int verification_results = 0;
};

// Validate the dataset rooted at `dataDir` (a filesystem path). All discovered
// diagnostics (ERROR or WARNING) are appended to `out`. `summary` is always
// populated with whatever could be computed. Returns true iff no ERROR-level
// diagnostic was emitted.
bool validateDataset(const std::string& dataDir,
                     std::vector<Diagnostic>& out,
                     ValidationSummary& summary);

// Stable ordering so repeated runs produce identical output (deterministic CI).
void sortDiagnostics(std::vector<Diagnostic>& diags);

} // namespace hy3
