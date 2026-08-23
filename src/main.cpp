// hy3_algotrace — command line interface.
//
// Usage:
//   hy3_algotrace validate <data_dir>   validate a dataset directory
//   hy3_algotrace --help | help         print usage
//
// Exit codes:
//   0  dataset is valid (result: PASS)
//   1  dataset is invalid (result: FAIL; diagnostics printed)
//   2  bad command-line usage or an internal error

#include "hy3_algotrace/validator.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cout
        << "hy3_algotrace — Phase 1B dataset contract validator\n"
        << "\n"
        << "USAGE:\n"
        << "  hy3_algotrace validate <data_dir>\n"
        << "      Validate data/manifest.json and data/problems/*.json.\n"
        << "      Prints a deterministic summary report. Exit 0 on PASS, 1 on FAIL.\n"
        << "\n"
        << "  hy3_algotrace --help | help\n"
        << "      Print this help and exit 0.\n"
        << "\n"
        << "EXIT CODES: 0 = valid, 1 = invalid data, 2 = usage/internal error.\n";
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
