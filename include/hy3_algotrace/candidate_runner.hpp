// hy3-algotrace -- Phase 2D controlled C++17 candidate verification.
//
// CandidateRunner is a bounded process harness for repository-controlled
// fixtures. It is deliberately not a security sandbox and must not be used
// for arbitrary user-provided or downloaded code.

#pragma once

#include "hy3_algotrace/process_executor.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hy3 {

enum class CompilerKind {
    GnuLike,
    Msvc,
};

enum class CandidateVerdict {
    CompileError,
    Passed,
    WrongAnswer,
    RuntimeError,
    Timeout,
    OutputLimitExceeded,
    RunnerError,
};

struct CandidateTestCase {
    std::string id;
    std::string stdin_bytes;
    std::string expected_stdout;
};

struct CandidateSolution {
    std::string solution_id;
    std::string trace_id;
    std::string language;
    std::string standard;
    std::string source_code;
};

struct CandidateRunnerConfig {
    // Must be absolute and must not exist. CandidateRunner creates it and an
    // independently named temporary workspace below it.
    std::filesystem::path run_directory;
    std::filesystem::path compiler_path;
    CompilerKind compiler_kind = CompilerKind::GnuLike;
    std::string run_id;
    std::string started_at;
    std::uint64_t compile_timeout_ms = 30'000;
    std::uint64_t execution_timeout_ms = 2'000;
    std::size_t compile_stdout_limit_bytes = 256U * 1024U;
    std::size_t compile_stderr_limit_bytes = 256U * 1024U;
    std::size_t execution_stdout_limit_bytes = 64U * 1024U;
    std::size_t execution_stderr_limit_bytes = 64U * 1024U;
};

struct CandidateRunRequest {
    CandidateSolution solution;
    std::vector<CandidateTestCase> test_cases;
    std::optional<std::string> finding_ref;
};

struct OutputComparison {
    bool equal = false;
    std::string normalized_actual;
    std::string normalized_expected;
    std::string difference_summary;
};

struct CandidateRunResult {
    bool ok = false;
    std::string error_code;
    std::string message;
    CandidateVerdict overall_verdict = CandidateVerdict::RunnerError;
    nlohmann::json record;
};

// Comparison contract:
//  * CRLF and lone CR become LF;
//  * spaces and tabs at the end of each line are ignored;
//  * one optional final LF is ignored;
//  * leading/internal whitespace and additional blank lines remain semantic.
// A mismatch reports the deterministic first differing line and column.
OutputComparison compareCandidateOutput(const std::string& actual,
                                        const std::string& expected);

std::string candidateVerdictName(CandidateVerdict verdict);

class CandidateRunner {
public:
    explicit CandidateRunner(IProcessExecutor& executor) : executor_(executor) {}

    CandidateRunResult run(const CandidateRunnerConfig& config,
                           const CandidateRunRequest& request);

private:
    IProcessExecutor& executor_;
};

} // namespace hy3
