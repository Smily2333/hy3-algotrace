// hy3_algotrace — Reporter implementation (Phase 2B).

#include "hy3_algotrace/reporter.hpp"
#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/prediction_importer.hpp" // for ParseStatus + taxonomy

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace hy3 {
namespace fs = std::filesystem;

namespace {

// Decode parse_status string -> enum. Returns false if unknown.
bool parseStatusFromStr(const std::string& s, ParseStatus& out) {
    if (s == "model_call_not_attempted") out = ParseStatus::ModelCallNotAttempted;
    else if (s == "empty_response") out = ParseStatus::EmptyResponse;
    else if (s == "invalid_json") out = ParseStatus::InvalidJson;
    else if (s == "schema_invalid") out = ParseStatus::SchemaInvalid;
    else if (s == "semantic_invalid") out = ParseStatus::SemanticInvalid;
    else if (s == "parsed") out = ParseStatus::Parsed;
    else return false;
    return true;
}

// Extract the gold diagnosis for a trace from a problem doc.
nlohmann::json findGold(const nlohmann::json& problemDoc, const std::string& traceId) {
    if (!problemDoc.contains("diagnoses") || !problemDoc.at("diagnoses").is_array())
        return nlohmann::json();
    for (const auto& d : problemDoc.at("diagnoses")) {
        if (d.contains("trace_id") && d.at("trace_id").is_string() &&
            d.at("trace_id").get<std::string>() == traceId) {
            return d;
        }
    }
    // some datasets key diagnoses by trace directly; fall back to first
    return nlohmann::json();
}

// Build the deduped category set and (stage,category) pair set from a prediction
// or gold findings array.
std::set<std::string> categorySet(const nlohmann::json& findings) {
    std::set<std::string> s;
    if (findings.is_array()) {
        for (const auto& f : findings) {
            if (f.contains("category") && f.at("category").is_string())
                s.insert(f.at("category").get<std::string>());
        }
    }
    return s;
}
std::set<std::pair<std::string,std::string>> pairSet(const nlohmann::json& findings) {
    std::set<std::pair<std::string,std::string>> s;
    if (findings.is_array()) {
        for (const auto& f : findings) {
            std::string stage = f.contains("stage") && f.at("stage").is_string()
                                    ? f.at("stage").get<std::string>() : "";
            std::string cat = f.contains("category") && f.at("category").is_string()
                                  ? f.at("category").get<std::string>() : "";
            s.insert({stage, cat});
        }
    }
    return s;
}

// Safe division with zero-denominator rule from metrics §4/§6.
double safeDiv(int num, int den) {
    if (den == 0) return 1.0; // zero denominator -> precision/recall = 1
    return static_cast<double>(num) / static_cast<double>(den);
}

} // namespace

// --- Gold loading ----------------------------------------------------------

ReporterResult loadGoldDiagnosis(const std::string& dataDir,
                                 const std::string& problemId,
                                 const std::string& traceId,
                                 nlohmann::json& goldOut) {
    ReporterResult res;
    LoadResult lr = loadJsonFile("problems/" + problemId + ".json", dataDir);
    if (!lr.ok) {
        res.error_code = reporter_errc::E_DATA_LOAD;
        res.message = lr.error_message;
        return res;
    }
    goldOut = findGold(lr.doc, traceId);
    if (goldOut.is_null() || !goldOut.is_object()) {
        res.error_code = reporter_errc::E_GOLD_NOT_FOUND;
        res.message = "gold diagnosis not found for " + problemId + " / " + traceId;
        return res;
    }
    res.ok = true;
    return res;
}

// --- Build report ----------------------------------------------------------

ReporterResult buildReport(const std::string& runDir,
                           const std::string& dataDir,
                           const std::string& completedAt,
                           const std::string& reportGeneratedAt,
                           nlohmann::json& reportJson,
                           bool& runComplete) {
    ReporterResult res;
    std::error_code ec;
    if (!fs::exists(runDir, ec)) {
        res.error_code = reporter_errc::E_RUN_DIR_MISSING;
        res.message = "run directory missing: " + runDir;
        return res;
    }
    // Load manifest.
    LoadResult mlr = loadJsonFile("run-manifest.json", runDir);
    if (!mlr.ok) {
        res.error_code = reporter_errc::E_MANIFEST_READ;
        res.message = mlr.error_message;
        return res;
    }
    const nlohmann::json& manifest = mlr.doc;
    if (!manifest.contains("trace_ids") || !manifest.at("trace_ids").is_array()) {
        res.error_code = reporter_errc::E_MANIFEST_READ;
        res.message = "manifest missing trace_ids array";
        return res;
    }
    const auto& traceIds = manifest.at("trace_ids");
    int N = static_cast<int>(traceIds.size());

    // Load per-trace context from manifest? We need problem_id per trace to load
    // gold. The manifest does not store problem_id. We scan all problems in the
    // dataset and match trace_id -> problem_id.
    // Build a map trace_id -> problem_id from dataset.
    std::map<std::string, std::string> traceToProblem;
    {
        LoadResult dmlr = loadJsonFile("manifest.json", dataDir);
        if (!dmlr.ok) {
            res.error_code = reporter_errc::E_DATA_LOAD;
            res.message = dmlr.error_message;
            return res;
        }
        for (const auto& pidVal : dmlr.doc.at("problem_ids")) {
            if (!pidVal.is_string()) continue;
            std::string pid = pidVal.get<std::string>();
            LoadResult plr = loadJsonFile("problems/" + pid + ".json", dataDir);
            if (!plr.ok) continue;
            const auto& prob = plr.doc;
            auto collect = [&](const nlohmann::json& arr) {
                if (arr.is_array()) {
                    for (const auto& tr : arr) {
                        if (tr.contains("id") && tr.at("id").is_string())
                            traceToProblem[tr.at("id").get<std::string>()] = pid;
                    }
                }
            };
            if (prob.contains("reasoning_traces")) collect(prob.at("reasoning_traces"));
        }
    }

    // Per-trace comparison + parse status counts.
    std::map<std::string, int> parseStatusCounts;
    int parsedCount = 0;
    int statusCorrect = 0;                 // status_accuracy numerator
    int primaryCorrect = 0;                // primary_category_accuracy numerator
    int incorrectGoldCount = 0;           // |I|

    int microTP = 0, microFP = 0, microFN = 0;
    int pairTP = 0, pairFP = 0, pairFN = 0;
    double macroF1Sum = 0.0;

    // confusion matrices
    std::map<std::string, std::map<std::string, int>> statusConf; // gold x pred
    std::map<std::string, std::map<std::string, int>> primaryConf; // gold x pred (incorrect only)
    int undeterminedCount = 0;

    std::vector<nlohmann::json> perTrace;
    runComplete = true;

    for (const auto& tidVal : traceIds) {
        std::string tid = tidVal.is_string() ? tidVal.get<std::string>() : "";
        nlohmann::json entry;
        entry["trace_id"] = tid;

        fs::path wpath = fs::path(runDir) / "predictions" / (tid + ".json");
        if (!fs::exists(wpath, ec)) {
            // Missing wrapper => run incomplete. Record as parse_failed.
            runComplete = false;
            entry["wrapper_present"] = false;
            entry["parse_status"] = "wrapper_missing";
            entry["prediction"] = nullptr;
            entry["gold_status"] = nullptr;
            entry["status_match"] = false;
            parseStatusCounts["wrapper_missing"]++;
            // metrics treat missing like parse failure (sentinel).
            microFP++; // sentinel FP
            pairFP++;
            macroF1Sum += 0.0;
            statusConf["<missing>"]["<missing>"]++; // note; not in 3x3
            perTrace.push_back(entry);
            continue;
        }
        LoadResult wlr = loadJsonFile("predictions/" + tid + ".json", runDir);
        if (!wlr.ok) {
            res.error_code = reporter_errc::E_WRAPPER_PARSE;
            res.message = wlr.error_message;
            return res;
        }
        const nlohmann::json& w = wlr.doc;
        entry["wrapper_present"] = true;
        std::string psStr = w.contains("parse_status") && w.at("parse_status").is_string()
                                ? w.at("parse_status").get<std::string>() : "wrapper_missing";
        entry["parse_status"] = psStr;
        parseStatusCounts[psStr]++;
        if (psStr == "parsed") parsedCount++;

        // Load gold.
        std::string pid = traceToProblem.count(tid) ? traceToProblem.at(tid) : "";
        nlohmann::json gold;
        bool goldOk = false;
        if (!pid.empty()) {
            ReporterResult gr = loadGoldDiagnosis(dataDir, pid, tid, gold);
            goldOk = gr.ok;
        }
        entry["gold_status"] = goldOk ? gold.at("status") : nlohmann::json(nullptr);
        if (goldOk && gold.contains("primary_category"))
            entry["gold_primary_category"] = gold.at("primary_category");
        else
            entry["gold_primary_category"] = nullptr;

        bool isParsed = (psStr == "parsed");
        nlohmann::json pred; // prediction structure (or null)
        if (isParsed && w.contains("prediction") && w.at("prediction").is_object())
            pred = w.at("prediction");

        ParseStatus ps;
        parseStatusFromStr(psStr, ps);
        bool parseFailed = !isParsed; // includes not_attempted/empty/invalid/schema/semantic

        // ---- status accuracy ----
        bool statusMatch = false;
        if (isParsed && goldOk) {
            std::string predStatus = pred.contains("status") && pred.at("status").is_string()
                                         ? pred.at("status").get<std::string>() : "";
            std::string goldStatus = gold.at("status").is_string()
                                         ? gold.at("status").get<std::string>() : "";
            statusMatch = (predStatus == goldStatus);
            if (statusMatch) statusCorrect++;
            statusConf[goldStatus][predStatus]++;
        } else {
            // parse failure -> not a match
            std::string goldStatus = goldOk && gold.at("status").is_string()
                                         ? gold.at("status").get<std::string>() : "<unknown>";
            statusConf[goldStatus]["<parse_failed>"]++;
        }
        entry["status_match"] = statusMatch;

        // ---- gold findings sets ----
        std::set<std::string> Gt, Pt;
        std::set<std::pair<std::string,std::string>> SGt, SPt;
        if (goldOk) {
            bool goldIncorrect = gold.at("status").is_string() &&
                                 gold.at("status").get<std::string>() == "incorrect";
            if (goldIncorrect) {
                Gt = categorySet(gold.at("findings"));
                SGt = pairSet(gold.at("findings"));
            }
            if (gold.at("status").is_string() &&
                gold.at("status").get<std::string>() == "incorrect")
                incorrectGoldCount++;
        }
        if (isParsed) {
            Pt = categorySet(pred.at("findings"));
            SPt = pairSet(pred.at("findings"));
        }

        // ---- primary_category_accuracy ----
        if (goldOk && gold.at("status").is_string() &&
            gold.at("status").get<std::string>() == "incorrect") {
            bool pcMatch = false;
            if (isParsed && pred.contains("primary_category") &&
                pred.at("primary_category").is_string()) {
                pcMatch = (pred.at("primary_category").get<std::string>() ==
                           (gold.contains("primary_category") &&
                            gold.at("primary_category").is_string()
                                ? gold.at("primary_category").get<std::string>() : ""));
            }
            if (pcMatch) primaryCorrect++;
        }

        // ---- micro category ----
        if (parseFailed) {
            // sentinel: predict set = {__parse_failed__}
            microFP++; // always an FP vs gold (even if gold empty: FP of sentinel)
            if (goldOk && gold.at("status").is_string() &&
                gold.at("status").get<std::string>() == "incorrect") {
                // also FN for each missing gold category
                microFN += static_cast<int>(Gt.size());
            }
        } else {
            // intersection etc.
            for (const auto& c : Pt) {
                if (Gt.count(c)) microTP++;
                else microFP++;
            }
            for (const auto& c : Gt) {
                if (!Pt.count(c)) microFN++;
            }
        }

        // ---- micro stage-category pair ----
        if (parseFailed) {
            pairFP++;
            if (goldOk && gold.at("status").is_string() &&
                gold.at("status").get<std::string>() == "incorrect") {
                pairFN += static_cast<int>(SGt.size());
            }
        } else {
            for (const auto& p : SPt) {
                if (SGt.count(p)) pairTP++;
                else pairFP++;
            }
            for (const auto& p : SGt) {
                if (!SPt.count(p)) pairFN++;
            }
        }

        // ---- macro F1 per trace ----
        double f1t = 0.0;
        if (parseFailed) {
            f1t = 0.0;
        } else if (Pt.empty() && Gt.empty()) {
            f1t = 1.0;
        } else {
            int inter = 0;
            for (const auto& c : Pt) if (Gt.count(c)) inter++;
            int denom = static_cast<int>(Pt.size()) + static_cast<int>(Gt.size());
            if (denom > 0) f1t = 2.0 * static_cast<double>(inter) / static_cast<double>(denom);
        }
        macroF1Sum += f1t;
        entry["category_set_f1"] = f1t;

        // ---- primary confusion (incorrect only) ----
        if (goldOk && gold.at("status").is_string() &&
            gold.at("status").get<std::string>() == "incorrect") {
            std::string gpc = (gold.contains("primary_category") &&
                               gold.at("primary_category").is_string())
                                  ? gold.at("primary_category").get<std::string>() : "null";
            std::string ppc = "null";
            if (isParsed && pred.contains("primary_category") &&
                pred.at("primary_category").is_string())
                ppc = pred.at("primary_category").get<std::string>();
            else if (isParsed && pred.contains("primary_category") &&
                     pred.at("primary_category").is_null())
                ppc = "null";
            else if (parseFailed)
                ppc = "<parse_failed>";
            primaryConf[gpc][ppc]++;
        }

        // ---- undetermined count ----
        if (isParsed && pred.contains("status") && pred.at("status").is_string() &&
            pred.at("status").get<std::string>() == "undetermined")
            undeterminedCount++;

        entry["prediction"] = isParsed ? pred : nlohmann::json(nullptr);
        perTrace.push_back(entry);
    }

    // ---- Aggregate metrics ----
    double parseSuccessRate = safeDiv(parsedCount, N); // N never 0 here
    double statusAccuracy = safeDiv(statusCorrect, N);
    double primaryAccuracy = (incorrectGoldCount == 0)
                                 ? nlohmann::json::value_t::null // N/A when no incorrect gold
                                 : nlohmann::json(static_cast<double>(primaryCorrect) /
                                                  static_cast<double>(incorrectGoldCount));
    // micro
    double microP = safeDiv(microTP, microTP + microFP);
    double microR = safeDiv(microTP, microTP + microFN);
    double microF1 = (microP + microR == 0.0) ? 0.0 : (2.0 * microP * microR / (microP + microR));
    // micro pair
    double pairP = safeDiv(pairTP, pairTP + pairFP);
    double pairR = safeDiv(pairTP, pairTP + pairFN);
    double pairF1 = (pairP + pairR == 0.0) ? 0.0 : (2.0 * pairP * pairR / (pairP + pairR));
    // macro
    double macroF1 = (N == 0) ? 0.0 : (macroF1Sum / static_cast<double>(N));
    double undeterminedRate = safeDiv(undeterminedCount, N);

    nlohmann::json m;
    m["total_traces"] = N;
    m["parsed_count"] = parsedCount;
    m["parse_success_rate"] = parseSuccessRate;
    m["status_accuracy"] = statusAccuracy;
    m["primary_category_accuracy"] = primaryAccuracy;
    m["finding_category_micro"] = {{"precision", microP}, {"recall", microR}, {"f1", microF1}};
    m["finding_category_macro_F1"] = macroF1;
    m["stage_category_pair_micro"] = {{"precision", pairP}, {"recall", pairR}, {"f1", pairF1}};
    m["undetermined_rate"] = undeterminedRate;
    m["parse_status_counts"] = nlohmann::json(parseStatusCounts);
    m["status_confusion_matrix"] = nlohmann::json(statusConf);
    m["primary_category_confusion_matrix"] = nlohmann::json(primaryConf);
    m["incorrect_gold_count"] = incorrectGoldCount;

    reportJson = nlohmann::json::object();
    reportJson["evaluation_schema_version"] = "0.1.0";
    reportJson["run_id"] = manifest.contains("run_id") ? manifest.at("run_id") : "";
    reportJson["run_complete"] = runComplete;
    reportJson["generated_at"] = reportGeneratedAt.empty()
                                     ? nlohmann::json(nullptr) : nlohmann::json(reportGeneratedAt);
    reportJson["completed_at"] = completedAt.empty() ? nlohmann::json(nullptr)
                                                     : nlohmann::json(completedAt);
    reportJson["metrics"] = m;
    reportJson["per_trace"] = perTrace;

    res.ok = true;
    return res;
}

// --- Markdown rendering ----------------------------------------------------

std::string renderReportMarkdown(const nlohmann::json& reportJson) {
    auto strOr = [&](const char* key, const std::string& def) -> std::string {
        if (reportJson.contains(key) && reportJson.at(key).is_string())
            return reportJson.at(key).get<std::string>();
        return def;
    };
    std::ostringstream os;
    os << "# Phase 2B Evaluation Report\n\n";
    os << "- run_id: " << strOr("run_id", "") << "\n";
    os << "- run_complete: " << (reportJson.value("run_complete", false) ? "true" : "false") << "\n";
    os << "- generated_at: " << strOr("generated_at", "null") << "\n";
    os << "- completed_at: " << strOr("completed_at", "null") << "\n\n";

    const auto& m = reportJson.at("metrics");
    os << "## Metrics\n\n";
    os << "- total_traces: " << m.value("total_traces", 0) << "\n";
    os << "- parsed_count: " << m.value("parsed_count", 0) << "\n";
    os << "- parse_success_rate: " << m.value("parse_success_rate", 0.0) << "\n";
    os << "- status_accuracy: " << m.value("status_accuracy", 0.0) << "\n";
    auto pca = m.at("primary_category_accuracy");
    os << "- primary_category_accuracy: "
       << (pca.is_null() ? std::string("N/A") : std::to_string(pca.get<double>())) << "\n";
    const auto& mic = m.at("finding_category_micro");
    os << "- finding_category_micro: P=" << mic.value("precision", 0.0)
       << " R=" << mic.value("recall", 0.0)
       << " F1=" << mic.value("f1", 0.0) << "\n";
    os << "- finding_category_macro_F1: " << m.value("finding_category_macro_F1", 0.0) << "\n";
    const auto& smic = m.at("stage_category_pair_micro");
    os << "- stage_category_pair_micro: P=" << smic.value("precision", 0.0)
       << " R=" << smic.value("recall", 0.0)
       << " F1=" << smic.value("f1", 0.0) << "\n";
    os << "- undetermined_rate: " << m.value("undetermined_rate", 0.0) << "\n\n";

    os << "## Parse status counts\n\n";
    for (auto it = m.at("parse_status_counts").begin();
         it != m.at("parse_status_counts").end(); ++it) {
        os << "- " << it.key() << ": " << it.value().get<int>() << "\n";
    }
    os << "\n";

    os << "## Per-trace comparison\n\n";
    os << "| trace_id | parse_status | gold_status | pred_status | status_match |\n";
    os << "| --- | --- | --- | --- | --- |\n";
    for (const auto& t : reportJson.at("per_trace")) {
        std::string tid = t.value("trace_id", "");
        std::string ps = t.value("parse_status", "");
        std::string gs = "null";
        if (t.contains("gold_status") && t.at("gold_status").is_string())
            gs = t.at("gold_status").get<std::string>();
        std::string pst = "null";
        if (t.contains("prediction") && t.at("prediction").is_object() &&
            t.at("prediction").contains("status") && t.at("prediction").at("status").is_string())
            pst = t.at("prediction").at("status").get<std::string>();
        bool sm = t.value("status_match", false);
        os << "| " << tid << " | " << ps << " | " << gs << " | " << pst << " | "
           << (sm ? "true" : "false") << " |\n";
    }
    os << "\n";
    os << "> Note: gold diagnoses are shown only here, never inside prediction files.\n";
    os << "> parse failure counts as status mismatch; macro F1 = 0 for parse-failed traces.\n";
    return os.str();
}

// --- Write report ----------------------------------------------------------

ReporterResult writeReport(const std::string& runDir,
                           const nlohmann::json& reportJson) {
    ReporterResult res;
    fs::path jpath = fs::path(runDir) / "report.json";
    fs::path mpath = fs::path(runDir) / "report.md";
    std::string jtext = reportJson.dump(2);
    std::string mtext = renderReportMarkdown(reportJson);
    {
        std::ofstream ofs(jpath, std::ios::binary);
        if (!ofs) {
            res.error_code = reporter_errc::E_WRITE_FAILED;
            res.message = "cannot write report.json";
            return res;
        }
        ofs << jtext;
    }
    {
        std::ofstream ofs(mpath, std::ios::binary);
        if (!ofs) {
            res.error_code = reporter_errc::E_WRITE_FAILED;
            res.message = "cannot write report.md";
            return res;
        }
        ofs << mtext;
    }
    res.ok = true;
    return res;
}

// --- Top-level generate ----------------------------------------------------

ReporterResult generateReport(const std::string& runDir,
                              const std::string& dataDir,
                              const std::string& completedAt,
                              const std::string& reportGeneratedAt) {
    ReporterResult res;
    nlohmann::json report;
    bool runComplete = false;
    ReporterResult br = buildReport(runDir, dataDir, completedAt, reportGeneratedAt,
                                    report, runComplete);
    if (!br.ok) return br;
    res.run_complete = runComplete;
    ReporterResult wr = writeReport(runDir, report);
    if (!wr.ok) return wr;

    // Update run-manifest.completed_at ONLY when complete and completedAt valid.
    if (runComplete && !completedAt.empty() && completedAt != "null") {
        fs::path mp = fs::path(runDir) / "run-manifest.json";
        LoadResult mlr = loadJsonFile("run-manifest.json", runDir);
        if (mlr.ok) {
            nlohmann::json m = mlr.doc;
            m["completed_at"] = completedAt;
            std::ofstream ofs(mp, std::ios::binary);
            if (ofs) ofs << m.dump(2);
        }
    }
    res.ok = true;
    return res;
}

} // namespace hy3
