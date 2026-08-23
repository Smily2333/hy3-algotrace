// hy3_algotrace — Diagnostic
//
// A single, structured validation finding. Every diagnostic is machine-readable
// (stable `code`) and human-readable (`message`), so the CLI and tests can both
// assert on the `code` rather than free text.
//
// This header is header-only (no separate .cpp) on purpose: it is tiny and is
// included by both the validator and the CLI.

#pragma once

#include <string>
#include <vector>

namespace hy3 {

enum class Severity { ERROR, WARNING };

struct Diagnostic {
    Severity severity = Severity::ERROR;
    std::string code;       // stable machine-readable error code, e.g. "E_BAD_TRACE_FK"
    std::string file;       // relative path to the offending file (may be empty)
    std::string object_id;  // object id that locates the issue (may be empty)
    std::string message;    // human-readable explanation
};

// Stable error codes. These must remain stable across releases; the test suite
// and documentation reference them by name. See docs/data-contract.md (appendix)
// for the full mapping.
namespace errc {
    inline constexpr const char* E_USAGE                          = "E_USAGE";
    inline constexpr const char* E_DATA_DIR_NOT_FOUND            = "E_DATA_DIR_NOT_FOUND";
    inline constexpr const char* E_MANIFEST_NOT_FOUND            = "E_MANIFEST_NOT_FOUND";
    inline constexpr const char* E_PROBLEMS_DIR_NOT_FOUND        = "E_PROBLEMS_DIR_NOT_FOUND";
    inline constexpr const char* E_FILE_READ                     = "E_FILE_READ";
    inline constexpr const char* E_JSON_PARSE                    = "E_JSON_PARSE";
    inline constexpr const char* E_MISSING_KEY                  = "E_MISSING_KEY";
    inline constexpr const char* E_TYPE_MISMATCH                = "E_TYPE_MISMATCH";
    inline constexpr const char* E_VERSION_MISMATCH             = "E_VERSION_MISMATCH";
    inline constexpr const char* E_DUPLICATE_ID                 = "E_DUPLICATE_ID";
    inline constexpr const char* E_BAD_PROBLEM_FK               = "E_BAD_PROBLEM_FK";
    inline constexpr const char* E_BAD_TRACE_FK                 = "E_BAD_TRACE_FK";
    inline constexpr const char* E_BAD_SOLUTION_FK              = "E_BAD_SOLUTION_FK";
    inline constexpr const char* E_BAD_TEST_FK                  = "E_BAD_TEST_FK";
    inline constexpr const char* E_DIAGNOSIS_CARDINALITY        = "E_DIAGNOSIS_CARDINALITY";
    inline constexpr const char* E_INVALID_ENUM                 = "E_INVALID_ENUM";
    inline constexpr const char* E_CORRECT_WITH_FINDINGS        = "E_CORRECT_WITH_FINDINGS";
    inline constexpr const char* E_INCORRECT_WITHOUT_FINDINGS   = "E_INCORRECT_WITHOUT_FINDINGS";
    inline constexpr const char* E_PRIMARY_NOT_IN_FINDINGS      = "E_PRIMARY_NOT_IN_FINDINGS";
    inline constexpr const char* E_IMPLEMENTATION_WITHOUT_SOLUTION = "E_IMPLEMENTATION_WITHOUT_SOLUTION";
    inline constexpr const char* E_MANIFEST_COUNT_MISMATCH     = "E_MANIFEST_COUNT_MISMATCH";
    inline constexpr const char* E_UNCALIBRATED_CONFIDENCE      = "E_UNCALIBRATED_CONFIDENCE";
    inline constexpr const char* E_UNEXPECTED_VERIFICATION_RESULT = "E_UNEXPECTED_VERIFICATION_RESULT";

    // Extra codes used for clarity (allowed: spec says "at least cover" the list above).
    inline constexpr const char* E_PROBLEM_ID_FILE_MISMATCH     = "E_PROBLEM_ID_FILE_MISMATCH";
    inline constexpr const char* E_REVIEW_STATUS_SEMANTIC       = "E_REVIEW_STATUS_SEMANTIC";
    inline constexpr const char* E_STATUS_PRIMARY_MISMATCH      = "E_STATUS_PRIMARY_MISMATCH";
} // namespace errc

// Format one diagnostic as a single, log/CI-friendly line:
//   ERROR [E_BAD_TRACE_FK] data/problems/cf_160A.json (cf_160A_t3_d): message
inline std::string formatDiagnostic(const Diagnostic& d) {
    std::string sev = (d.severity == Severity::ERROR) ? "ERROR" : "WARNING";
    std::string s = sev;
    s += " [";
    s += d.code;
    s += "]";
    if (!d.file.empty()) {
        s += " ";
        s += d.file;
    }
    if (!d.object_id.empty()) {
        s += " (";
        s += d.object_id;
        s += ")";
    }
    s += ": ";
    s += d.message;
    return s;
}

} // namespace hy3
