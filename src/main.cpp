// hy3_algotrace — command line interface.
//
// Usage:
//   hy3_algotrace validate <data_dir>
//       Validate a dataset directory (Phase 1B contract validator).
//   hy3_algotrace export-prompts <data_dir> <template_file> <run_dir>
//       --run-id <run_id> --pipeline-commit <commit> --started-at <ISO-8601>
//       Export evaluation prompts for every trace (Phase 2B PromptExporter).
//   hy3_algotrace import-response <run_dir> <trace_id> <raw_file>
//       --run-id <run_id> --generated-at <ISO-8601>
//       Import a model's raw response into a prediction wrapper (PredictionImporter).
//   hy3_algotrace mark-not-attempted <run_dir> <trace_id>
//       --run-id <run_id> --generated-at <ISO-8601>
//       Explicitly mark a trace as model_call_not_attempted (no inference).
//   hy3_algotrace report <run_dir> <data_dir>
//       --completed-at <ISO-8601|null> --generated-at <ISO-8601>
//       Generate report.json + report.md (Reporter). No model/API/OJ.
//   hy3_algotrace --help | help
//       Print usage.
//
// Exit codes:
//   0  success (validate PASS, export/import/report completed)
//   1  dataset invalid / business failure (diagnostics or stable error code printed)
//   2  bad command-line usage or an internal error

#include "hy3_algotrace/prompt_exporter.hpp"
#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/reporter.hpp"
#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/validator.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cout
        << "hy3_algotrace — Phase 1B dataset contract validator + Phase 2B offline pipeline\n"
        << "\n"
        << "USAGE:\n"
        << "  hy3_algotrace validate <data_dir>\n"
        << "      Validate data/manifest.json and data/problems/*.json.\n"
        << "      Prints a deterministic summary report. Exit 0 on PASS, 1 on FAIL.\n"
        << "\n"
        << "  hy3_algotrace export-prompts <data_dir> <template_file> <run_dir>\n"
        << "      --run-id <run_id> --pipeline-commit <commit> --started-at <ISO-8601>\n"
        << "      Export evaluation prompts for every reasoning trace (Phase 2B).\n"
        << "      <run_dir> must not already exist. Prints run_id/total_traces/\n"
        << "      prompt_template_sha256/output_dir/result. Exit 0 on success, 1 on failure.\n"
        << "\n"
        << "  hy3_algotrace import-response <run_dir> <trace_id> <raw_file>\n"
        << "      --run-id <run_id> --generated-at <ISO-8601>\n"
        << "      Import a model raw response (verbatim) into a prediction wrapper.\n"
        << "      Refuses to overwrite an existing raw/prediction. Exit 0/1/2.\n"
        << "\n"
        << "  hy3_algotrace mark-not-attempted <run_dir> <trace_id>\n"
        << "      --run-id <run_id> --generated-at <ISO-8601>\n"
        << "      Explicitly mark a trace as model_call_not_attempted (never inferred).\n"
        << "      Refuses to overwrite an existing wrapper. Exit 0/1/2.\n"
        << "\n"
        << "  hy3_algotrace report <run_dir> <data_dir>\n"
        << "      --completed-at <ISO-8601|null> --generated-at <ISO-8601>\n"
        << "      Generate report.json + report.md per docs/phase-02-metrics.md.\n"
        << "      Updates run-manifest.completed_at only when run is complete. Exit 0/1/2.\n"
        << "\n"
        << "  hy3_algotrace --help | help\n"
        << "      Print this help and exit 0.\n"
        << "\n"
        << "EXIT CODES: 0 = success, 1 = data/business failure, 2 = usage/internal error.\n"
        << "No model API, OJ, or candidate code is ever invoked by these commands.\n";
}

void printSummary(const hy3::ValidationSummary& s, bool pass,
                  int errorCount) {
    std::cout << "schema: " << s.schema_version << "\n";
    std::cout << "taxonomy: " << s.taxonomy_version << "\n";
    std::cout << "dataset: " << s.dataset_version << "\n";
    std::cout << "problems: " << s.problems << "\n";
    std::cout << "traces: " << s.traces << "\n";
    std::cout << "diagnoses: " << s.diagnoses << "\n";
    std::cout << "tests: " << s.tests << "\n";
    std::cout << "candidate_solutions: " << s.candidate_solutions << "\n";
    std::cout << "verification_results: " << s.verification_results << "\n";
    if (!pass) {
        std::cout << "errors: " << errorCount << "\n";
    }
    std::cout << "result: " << (pass ? "PASS" : "FAIL") << "\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args(argv, argv + argc);

        if (args.size() >= 2 &&
            (args[1] == "--help" || args[1] == "help" || args[1] == "-h")) {
            printUsage();
            return 0;
        }

        if (args.size() == 3 && args[1] == "validate") {
            const std::string dataDir = args[2];
            std::vector<hy3::Diagnostic> diags;
            hy3::ValidationSummary summary;
            bool pass = hy3::validateDataset(dataDir, diags, summary);
            hy3::sortDiagnostics(diags);
            int errors = 0;
            for (const auto& d : diags) {
                if (d.severity == hy3::Severity::ERROR) errors++;
                std::cout << hy3::formatDiagnostic(d) << "\n";
            }
            printSummary(summary, pass, errors);
            return pass ? 0 : 1;
        }

        // export-prompts <data_dir> <template_file> <run_dir>
        //   --run-id <id> --pipeline-commit <commit> --started-at <iso>
        if (args.size() >= 5 && args[1] == "export-prompts") {
            const std::string dataDir = args[2];
            const std::string templateFile = args[3];
            const std::string runDir = args[4];

            std::string runId, pipelineCommit, startedAt;
            if ((args.size() - 5) % 2 != 0) {
                std::cerr << "E_BAD_ARGUMENT: option missing value\n";
                return 2;
            }
            for (size_t i = 5; i + 1 < args.size(); i += 2) {
                if (args[i] == "--run-id") runId = args[i + 1];
                else if (args[i] == "--pipeline-commit") pipelineCommit = args[i + 1];
                else if (args[i] == "--started-at") startedAt = args[i + 1];
                else {
                    std::cerr << "E_BAD_ARGUMENT: unknown option " << args[i] << "\n";
                    return 2;
                }
            }
            if (runId.empty() || pipelineCommit.empty() || startedAt.empty()) {
                std::cerr << "E_BAD_ARGUMENT: --run-id, --pipeline-commit and "
                          << "--started-at are all required\n";
                return 2;
            }

            // Read + normalize the template file.
            std::ifstream tf(templateFile, std::ios::binary);
            if (!tf) {
                std::cerr << "E_FILE_READ: cannot open template: " << templateFile << "\n";
                return 1;
            }
            std::vector<uint8_t> rawTf((std::istreambuf_iterator<char>(tf)),
                                       std::istreambuf_iterator<char>());
            std::vector<uint8_t> normTf;
            std::string normErr;
            if (!hy3::normalizeUtf8(rawTf, normTf, normErr)) {
                std::cerr << normErr << ": template file is not valid UTF-8 or contains NUL\n";
                return 1;
            }
            std::string templateText(normTf.begin(), normTf.end());

            hy3::RunManifest m;
            m.run_id = runId;
            m.pipeline_commit = pipelineCommit;
            m.started_at = startedAt;
            m.notes =
                "Phase 2B-1 PromptExporter smoke export. No model call, no prediction, "
                "no metrics. reference_assisted mode.";

            std::string promptTemplateSha256;
            hy3::ExporterResult er =
                hy3::exportPrompts(dataDir, templateText, runDir, m, promptTemplateSha256);
            if (!er.ok) {
                std::cerr << er.error_code << ": " << er.message << "\n";
                return 1;
            }

            hy3::LoadResult manifestResult =
                hy3::loadJsonFile("run-manifest.json", runDir);
            if (!manifestResult.ok) {
                std::cerr << manifestResult.error_code << ": "
                          << manifestResult.error_message << "\n";
                return 2;
            }
            std::cout << "run_id: "
                      << manifestResult.doc.value("run_id", runId) << "\n";
            std::cout << "total_traces: "
                      << manifestResult.doc.value("total_traces", 0) << "\n";
            std::cout << "prompt_template_sha256: " << promptTemplateSha256 << "\n";
            std::cout << "output_dir: " << runDir << "\n";
            std::cout << "result: PASS\n";
            return 0;
        }

        // import-response <run_dir> <trace_id> <raw_file>
        //   --run-id <id> --generated-at <iso>
        if (args.size() >= 5 && args[1] == "import-response") {
            const std::string runDir = args[2];
            const std::string traceId = args[3];
            const std::string rawFile = args[4];
            std::string runId, generatedAt;
            if ((args.size() - 5) % 2 != 0) {
                std::cerr << "E_BAD_ARGUMENT: option missing value\n";
                return 2;
            }
            for (size_t i = 5; i + 1 < args.size(); i += 2) {
                if (args[i] == "--run-id") runId = args[i + 1];
                else if (args[i] == "--generated-at") generatedAt = args[i + 1];
                else {
                    std::cerr << "E_BAD_ARGUMENT: unknown option " << args[i] << "\n";
                    return 2;
                }
            }
            if (runId.empty() || generatedAt.empty()) {
                std::cerr << "E_BAD_ARGUMENT: --run-id and --generated-at are required\n";
                return 2;
            }
            hy3::ImporterResult ir = hy3::importResponse(runDir, traceId, rawFile,
                                                        runId, generatedAt);
            if (!ir.ok) {
                std::cerr << ir.error_code << ": " << ir.message << "\n";
                return 1;
            }
            std::cout << "trace_id: " << traceId << "\n";
            std::cout << "run_dir: " << runDir << "\n";
            std::cout << "result: PASS\n";
            return 0;
        }

        // mark-not-attempted <run_dir> <trace_id>
        //   --run-id <id> --generated-at <iso>
        if (args.size() >= 4 && args[1] == "mark-not-attempted") {
            const std::string runDir = args[2];
            const std::string traceId = args[3];
            std::string runId, generatedAt;
            if ((args.size() - 4) % 2 != 0) {
                std::cerr << "E_BAD_ARGUMENT: option missing value\n";
                return 2;
            }
            for (size_t i = 4; i + 1 < args.size(); i += 2) {
                if (args[i] == "--run-id") runId = args[i + 1];
                else if (args[i] == "--generated-at") generatedAt = args[i + 1];
                else {
                    std::cerr << "E_BAD_ARGUMENT: unknown option " << args[i] << "\n";
                    return 2;
                }
            }
            if (runId.empty() || generatedAt.empty()) {
                std::cerr << "E_BAD_ARGUMENT: --run-id and --generated-at are required\n";
                return 2;
            }
            hy3::ImporterResult ir = hy3::markNotAttempted(runDir, traceId, runId, generatedAt);
            if (!ir.ok) {
                std::cerr << ir.error_code << ": " << ir.message << "\n";
                return 1;
            }
            std::cout << "trace_id: " << traceId << "\n";
            std::cout << "parse_status: model_call_not_attempted\n";
            std::cout << "result: PASS\n";
            return 0;
        }

        // report <run_dir> <data_dir>
        //   --completed-at <iso|null> --generated-at <iso>
        if (args.size() >= 4 && args[1] == "report") {
            const std::string runDir = args[2];
            const std::string dataDir = args[3];
            std::string completedAt, generatedAt;
            if ((args.size() - 4) % 2 != 0) {
                std::cerr << "E_BAD_ARGUMENT: option missing value\n";
                return 2;
            }
            for (size_t i = 4; i + 1 < args.size(); i += 2) {
                if (args[i] == "--completed-at") completedAt = args[i + 1];
                else if (args[i] == "--generated-at") generatedAt = args[i + 1];
                else {
                    std::cerr << "E_BAD_ARGUMENT: unknown option " << args[i] << "\n";
                    return 2;
                }
            }
            if (completedAt.empty() || generatedAt.empty()) {
                std::cerr << "E_BAD_ARGUMENT: --completed-at and --generated-at are required\n";
                return 2;
            }
            hy3::ReporterResult rr = hy3::generateReport(runDir, dataDir,
                                                        completedAt, generatedAt);
            if (!rr.ok) {
                std::cerr << rr.error_code << ": " << rr.message << "\n";
                return 1;
            }
            hy3::LoadResult reportResult = hy3::loadJsonFile("report.json", runDir);
            if (!reportResult.ok) {
                std::cerr << reportResult.error_code << ": "
                          << reportResult.error_message << "\n";
                return 2;
            }
            std::cout << "run_id: " << reportResult.doc.value("run_id", "") << "\n";
            std::cout << "run_complete: " << (rr.run_complete ? "true" : "false") << "\n";
            std::cout << "output_dir: " << runDir << "\n";
            std::cout << "result: PASS\n";
            return 0;
        }

        // Any other invocation is a usage error.
        std::cerr << "E_USAGE: invalid arguments\n";
        printUsage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "E_USAGE: internal error: " << e.what() << "\n";
        return 2;
    } catch (...) {
        std::cerr << "E_USAGE: unknown internal error\n";
        return 2;
    }
}
