// hy3_algotrace — PredictionImporter implementation (Phase 2B).

#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hy3 {
namespace fs = std::filesystem;

namespace {

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

std::string parseStatusName(ParseStatus s) {
    switch (s) {
        case ParseStatus::ModelCallNotAttempted: return "model_call_not_attempted";
        case ParseStatus::EmptyResponse:         return "empty_response";
        case ParseStatus::InvalidJson:           return "invalid_json";
        case ParseStatus::SchemaInvalid:         return "schema_invalid";
        case ParseStatus::SemanticInvalid:       return "semantic_invalid";
        case ParseStatus::Parsed:                return "parsed";
    }
    return "unknown";
}

// Recursively validate that a JSON object has exactly the given set of top-level
// keys (no extras). Returns the first extra key or "".
std::string extraTopKey(const nlohmann::json& j,
                        const std::vector<std::string>& allowed) {
    if (!j.is_object()) return "<not-object>";
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool ok = false;
        for (const auto& a : allowed) if (a == it.key()) { ok = true; break; }
        if (!ok) return it.key();
    }
    return "";
}

// Read the fifth fenced JSON input block from the frozen prompt format.
// Missing/malformed blocks are treated conservatively as having no candidate.
bool promptHasCandidateSolution(const std::string& promptText) {
    const std::string sectionMarker = "#### 5.";
    const std::string candidateToken = "candidate_solution";
    const std::string fenceMarker = "```json";
    const size_t section = promptText.find(sectionMarker);
    if (section == std::string::npos) return false;
    const size_t fence = promptText.find(fenceMarker, section + sectionMarker.size());
    if (fence == std::string::npos) return false;
    const size_t token = promptText.find(candidateToken, section);
    if (token == std::string::npos || token >= fence) return false;
    const size_t contentStart = promptText.find('\n', fence + fenceMarker.size());
    if (contentStart == std::string::npos) return false;
    const size_t contentEnd = promptText.find("\n```", contentStart + 1);
    if (contentEnd == std::string::npos) return false;

    const std::string candidateText =
        promptText.substr(contentStart + 1, contentEnd - contentStart - 1);
    const nlohmann::json candidate =
        nlohmann::json::parse(candidateText, nullptr, false);
    return candidate.is_object();
}

// Validate a parsed finding's shape, types, and enums.
bool validateFindingSchema(const nlohmann::json& f,
                           std::vector<std::string>& errors) {
    bool ok = true;
    if (!f.is_object()) {
        errors.push_back("finding is not an object");
        return false;
    }
    // required keys for a finding
    for (const char* k : {"stage", "category", "locating", "evidence", "suggestion"}) {
        if (!f.contains(k)) {
            errors.push_back(std::string("finding missing key: ") + k);
            ok = false;
        }
    }
    // no extra keys
    std::string extra = extraTopKey(f, {"stage", "category", "locating",
                                        "evidence", "suggestion"});
    if (!extra.empty()) {
        errors.push_back("finding has extra key: " + extra);
        ok = false;
    }
    if (f.contains("stage")) {
        if (!f.at("stage").is_string() || !taxonomy::isStage(f.at("stage").get<std::string>())) {
            errors.push_back("finding.stage invalid enum: " +
                             (f.at("stage").is_string() ? f.at("stage").get<std::string>()
                                                        : std::string("<non-string>")));
            ok = false;
        }
    }
    if (f.contains("category")) {
        if (!f.at("category").is_string() ||
            !taxonomy::isCategory(f.at("category").get<std::string>())) {
            errors.push_back("finding.category invalid enum: " +
                             (f.at("category").is_string() ? f.at("category").get<std::string>()
                                                          : std::string("<non-string>")));
            ok = false;
        }
    }
    for (const char* k : {"locating", "evidence", "suggestion"}) {
        if (f.contains(k) && !f.at(k).is_string()) {
            errors.push_back(std::string("finding.") + k + " must be string");
            ok = false;
        }
    }
    return ok;
}

// Validate finding rules that require run context or cross-field meaning.
bool validateFindingSemantics(const nlohmann::json& f,
                              std::vector<std::string>& errors,
                              bool hasCandidateSolution) {
    bool ok = true;
    // implementation_consistency requires an associated candidate solution.
    if (f.contains("stage") && f.at("stage").is_string() &&
        f.at("stage").get<std::string>() == "implementation_consistency") {
        if (!hasCandidateSolution) {
            errors.push_back("implementation_consistency finding without candidate_solution");
            ok = false;
        }
        if (f.contains("category") && f.at("category").is_string() &&
            f.at("category").get<std::string>() != "implementation_mismatch") {
            errors.push_back("implementation_consistency stage must pair with "
                             "implementation_mismatch category");
            ok = false;
        }
    }
    return ok;
}

} // namespace

// --- Raw response saving ---------------------------------------------------

ImporterResult saveRawResponse(const std::string& runDir,
                               const std::string& traceId,
                               const std::vector<uint8_t>& rawBytes,
                               std::string& rawResponseSha256) {
    ImporterResult res;
    if (!isSafeId(traceId)) {
        res.error_code = importer_errc::E_UNSAFE_ID;
        res.message = "unsafe trace id: " + traceId;
        return res;
    }
    fs::path outDir = fs::path(runDir) / "raw-responses";
    fs::path outPath = outDir / (traceId + ".txt");
    std::error_code ec;
    if (fs::exists(outPath, ec)) {
        res.error_code = importer_errc::E_RAW_EXISTS;
        res.message = "raw response already exists (refuse overwrite): " +
                      outPath.string();
        return res;
    }
    if (!fs::exists(outDir, ec)) {
        fs::create_directories(outDir, ec);
        if (ec) {
            res.error_code = importer_errc::E_WRITE_FAILED;
            res.message = "cannot create raw-responses dir: " + ec.message();
            return res;
        }
    }
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "cannot open raw response file: " + outPath.string();
        return res;
    }
    // Write verbatim, no translation.
    ofs.write(reinterpret_cast<const char*>(rawBytes.data()),
              static_cast<std::streamsize>(rawBytes.size()));
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "write failed: " + outPath.string();
        return res;
    }
    rawResponseSha256 = sha256_hex(rawBytes);
    res.ok = true;
    return res;
}

// --- Prompt loading + hash -------------------------------------------------

ImporterResult loadPromptSha(const std::string& runDir,
                             const std::string& traceId,
                             std::string& promptSha256,
                             std::string& normalizedPromptText) {
    ImporterResult res;
    if (!isSafeId(traceId)) {
        res.error_code = importer_errc::E_UNSAFE_ID;
        res.message = "unsafe trace id: " + traceId;
        return res;
    }
    fs::path p = fs::path(runDir) / "prompts" / (traceId + ".txt");
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        res.error_code = importer_errc::E_PROMPT_MISSING;
        res.message = "prompt not found for trace (export first): " + p.string();
        return res;
    }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
    std::vector<uint8_t> norm;
    std::string normErr;
    if (!normalizeUtf8(raw, norm, normErr)) {
        res.error_code = importer_errc::E_INVALID_RAW_BYTES;
        res.message = "prompt file not valid UTF-8: " + normErr;
        return res;
    }
    normalizedPromptText.assign(norm.begin(), norm.end());
    promptSha256 = sha256_hex(norm);
    res.ok = true;
    return res;
}

// --- Parse + validate ------------------------------------------------------

ImporterResult classifyResponse(const std::string& rawText,
                                const std::string& expectedTraceId,
                                bool hasCandidateSolution,
                                ParseStatus& parseStatus,
                                nlohmann::json& prediction,
                                std::vector<std::string>& errors) {
    ImporterResult res;
    errors.clear();
    prediction = nlohmann::json::value_t::null;

    // Trim check for emptiness (only whitespace / nothing).
    bool allWhitespace = true;
    for (char c : rawText) {
        if (!std::isspace(static_cast<unsigned char>(c))) { allWhitespace = false; break; }
    }
    if (rawText.empty() || allWhitespace) {
        parseStatus = ParseStatus::EmptyResponse;
        res.ok = true; // status discriminated, no prediction
        return res;
    }

    // Parse JSON. No fence stripping, no repair.
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(rawText);
    } catch (...) {
        parseStatus = ParseStatus::InvalidJson;
        res.ok = true;
        return res;
    }

    if (!doc.is_object()) {
        parseStatus = ParseStatus::SchemaInvalid;
        errors.push_back("top-level JSON is not an object");
        res.ok = true;
        return res;
    }

    // --- Schema validation ---
    bool schemaOk = true;
    // Required top-level keys.
    for (const char* k : {"trace_id", "status", "primary_category", "findings",
                          "confidence", "confidence_method", "calibration_version"}) {
        if (!doc.contains(k)) {
            errors.push_back(std::string("missing key: ") + k);
            schemaOk = false;
        }
    }
    // No extra top-level keys.
    std::string extra = extraTopKey(doc, {"trace_id", "status", "primary_category",
                                          "findings", "confidence", "confidence_method",
                                          "calibration_version"});
    if (!extra.empty()) {
        errors.push_back("extra top-level key: " + extra);
        schemaOk = false;
    }
    // status enum
    if (doc.contains("status")) {
        if (!doc.at("status").is_string() ||
            !taxonomy::isStatus(doc.at("status").get<std::string>())) {
            errors.push_back("status invalid enum");
            schemaOk = false;
        }
    }
    // primary_category: string|null
    if (doc.contains("primary_category")) {
        const auto& pc = doc.at("primary_category");
        if (!(pc.is_string() || pc.is_null())) {
            errors.push_back("primary_category must be string or null");
            schemaOk = false;
        } else if (pc.is_string() &&
                   !taxonomy::isCategory(pc.get<std::string>())) {
            errors.push_back("primary_category invalid enum");
            schemaOk = false;
        }
    }
    // findings: array
    if (doc.contains("findings")) {
        if (!doc.at("findings").is_array()) {
            errors.push_back("findings must be array");
            schemaOk = false;
        } else {
            for (const auto& f : doc.at("findings")) {
                if (!validateFindingSchema(f, errors)) {
                    schemaOk = false;
                }
            }
        }
    }
    // confidence fields must be null (Phase 4)
    for (const char* k : {"confidence", "confidence_method", "calibration_version"}) {
        if (doc.contains(k) && !doc.at(k).is_null()) {
            errors.push_back(std::string(k) + " must be null in Phase 4");
            schemaOk = false;
        }
    }
    if (!schemaOk) {
        parseStatus = ParseStatus::SchemaInvalid;
        res.ok = true;
        return res;
    }

    // trace_id consistency
    std::string gotTraceId = doc.at("trace_id").is_string()
                                 ? doc.at("trace_id").get<std::string>() : std::string();
    if (gotTraceId != expectedTraceId) {
        parseStatus = ParseStatus::SchemaInvalid;
        errors.push_back("trace_id mismatch: expected " + expectedTraceId +
                         " got " + gotTraceId);
        res.ok = true;
        return res;
    }

    // --- Semantic validation ---
    std::string status = doc.at("status").get<std::string>();
    const auto& pc = doc.at("primary_category");
    const auto& findings = doc.at("findings");

    // Validate contextual and cross-field finding rules after schema passes.
    bool semanticOk = true;
    for (const auto& f : findings) {
        if (!validateFindingSemantics(f, errors, hasCandidateSolution)) {
            semanticOk = false;
        }
    }

    if (status == "correct") {
        if (!pc.is_null()) {
            errors.push_back("correct requires primary_category=null");
            semanticOk = false;
        }
        if (!findings.empty()) {
            errors.push_back("correct requires findings=[]");
            semanticOk = false;
        }
    } else if (status == "incorrect") {
        if (!pc.is_string()) {
            errors.push_back("incorrect requires primary_category non-null");
            semanticOk = false;
        } else {
            bool found = false;
            for (const auto& f : findings) {
                if (f.contains("category") && f.at("category").is_string() &&
                    f.at("category").get<std::string>() == pc.get<std::string>()) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                errors.push_back("incorrect requires primary_category in findings.category");
                semanticOk = false;
            }
        }
        if (findings.empty()) {
            errors.push_back("incorrect requires findings non-empty");
            semanticOk = false;
        }
    } else if (status == "undetermined") {
        if (!pc.is_null()) {
            errors.push_back("undetermined requires primary_category=null");
            semanticOk = false;
        }
        if (!findings.empty()) {
            errors.push_back("undetermined requires findings=[]");
            semanticOk = false;
        }
    }

    if (!semanticOk) {
        parseStatus = ParseStatus::SemanticInvalid;
        res.ok = true;
        return res;
    }

    // All good: prediction = the validated structure verbatim (no gold).
    parseStatus = ParseStatus::Parsed;
    prediction = doc;
    res.ok = true;
    return res;
}

// --- Wrapper writing -------------------------------------------------------

std::string serializeWrapper(const nlohmann::json& j) {
    return j.dump(2);
}

ImporterResult writePredictionWrapper(const std::string& runDir,
                                      const std::string& traceId,
                                      const std::string& runId,
                                      ParseStatus parseStatus,
                                      const std::string& promptSha256,
                                      const std::string& rawResponseSha256,
                                      const nlohmann::json& prediction,
                                      const std::vector<std::string>& errors,
                                      const std::string& generatedAt) {
    ImporterResult res;
    if (!isSafeId(traceId)) {
        res.error_code = importer_errc::E_UNSAFE_ID;
        res.message = "unsafe trace id: " + traceId;
        return res;
    }
    fs::path outDir = fs::path(runDir) / "predictions";
    fs::path outPath = outDir / (traceId + ".json");
    std::error_code ec;
    if (fs::exists(outPath, ec)) {
        res.error_code = importer_errc::E_PREDICTION_EXISTS;
        res.message = "prediction wrapper already exists (refuse overwrite): " +
                      outPath.string();
        return res;
    }
    if (!fs::exists(outDir, ec)) {
        fs::create_directories(outDir, ec);
        if (ec) {
            res.error_code = importer_errc::E_WRITE_FAILED;
            res.message = "cannot create predictions dir: " + ec.message();
            return res;
        }
    }
    nlohmann::json w;
    w["evaluation_schema_version"] = "0.1.0";
    w["run_id"] = runId;
    w["trace_id"] = traceId;
    w["model_name"] = "hy3";
    w["prompt_template_id"] = "hy3-evaluator-v1";
    w["input_mode"] = "reference_assisted";
    w["prompt_sha256"] = promptSha256;
    w["raw_response_sha256"] = rawResponseSha256; // null only when not_attempted
    w["parse_status"] = parseStatusName(parseStatus);
    // prediction must be null unless parsed.
    if (parseStatus == ParseStatus::Parsed) {
        w["prediction"] = prediction;
    } else {
        w["prediction"] = nlohmann::json::value_t::null;
    }
    w["errors"] = nlohmann::json(errors);
    w["generated_at"] = generatedAt.empty()
                            ? nlohmann::json()
                            : nlohmann::json(generatedAt);
    // Internal sentinel must never leak. Defensive guard:
    std::string text = serializeWrapper(w);
    if (text.find("__parse_failed__") != std::string::npos) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "internal sentinel would leak into wrapper (refused)";
        return res;
    }
    std::vector<uint8_t> raw(text.begin(), text.end());
    std::vector<uint8_t> norm;
    std::string normErr;
    if (!normalizeUtf8(raw, norm, normErr)) {
        res.error_code = importer_errc::E_INVALID_RAW_BYTES;
        res.message = normErr;
        return res;
    }
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "cannot open prediction wrapper: " + outPath.string();
        return res;
    }
    ofs.write(reinterpret_cast<const char*>(norm.data()),
              static_cast<std::streamsize>(norm.size()));
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "write failed: " + outPath.string();
        return res;
    }
    res.ok = true;
    return res;
}

// --- Top-level import ------------------------------------------------------

ImporterResult importResponse(const std::string& runDir,
                              const std::string& traceId,
                              const std::string& rawFilePath,
                              const std::string& runId,
                              const std::string& generatedAt) {
    ImporterResult res;
    if (!isSafeId(traceId)) {
        res.error_code = importer_errc::E_UNSAFE_ID;
        res.message = "unsafe trace id: " + traceId;
        return res;
    }
    std::error_code ec;
    if (!fs::exists(runDir, ec)) {
        res.error_code = importer_errc::E_RUN_DIR_MISSING;
        res.message = "run directory missing: " + runDir;
        return res;
    }
    // 1) read raw bytes verbatim
    std::ifstream ifs(rawFilePath, std::ios::binary);
    if (!ifs) {
        res.error_code = importer_errc::E_BAD_ARGUMENT;
        res.message = "cannot open raw response file: " + rawFilePath;
        return res;
    }
    std::vector<uint8_t> rawBytes((std::istreambuf_iterator<char>(ifs)),
                                  std::istreambuf_iterator<char>());

    // 2) save raw (refuse overwrite)
    std::string rawSha;
    ImporterResult sr = saveRawResponse(runDir, traceId, rawBytes, rawSha);
    if (!sr.ok) return sr;

    // 3) load prompt hash
    std::string promptSha, promptText;
    ImporterResult lp = loadPromptSha(runDir, traceId, promptSha, promptText);
    if (!lp.ok) return lp;

    // 4) classify
    std::string rawText(rawBytes.begin(), rawBytes.end());
    // Validate raw bytes are decodable as UTF-8 for classification (do not
    // normalize JSON content; only decode for parsing). If invalid UTF-8, the
    // response is treated as invalid_json (cannot parse).
    {
        std::vector<uint8_t> norm;
        std::string normErr;
        if (!normalizeUtf8(rawBytes, norm, normErr)) {
            ParseStatus ps = ParseStatus::InvalidJson;
            nlohmann::json pred = nlohmann::json::value_t::null;
            std::vector<std::string> errs = {"raw response not valid UTF-8"};
            return writePredictionWrapper(runDir, traceId, runId, ps, promptSha,
                                          rawSha, pred, errs, generatedAt);
        }
        rawText.assign(norm.begin(), norm.end());
    }

    ParseStatus ps;
    nlohmann::json pred;
    std::vector<std::string> errs;
    // The exporter renders candidate_solution as the fifth fenced JSON block:
    // JSON null when absent, or an object when associated.
    const bool hasCandidateSolution = promptHasCandidateSolution(promptText);
    ImporterResult cr = classifyResponse(rawText, traceId, hasCandidateSolution,
                                         ps, pred, errs);
    if (!cr.ok) return cr;

    // 5) write wrapper
    return writePredictionWrapper(runDir, traceId, runId, ps, promptSha, rawSha,
                                  pred, errs, generatedAt);
}

// --- Explicit not attempted ------------------------------------------------

ImporterResult markNotAttempted(const std::string& runDir,
                                const std::string& traceId,
                                const std::string& runId,
                                const std::string& generatedAt) {
    ImporterResult res;
    if (!isSafeId(traceId)) {
        res.error_code = importer_errc::E_UNSAFE_ID;
        res.message = "unsafe trace id: " + traceId;
        return res;
    }
    std::error_code ec;
    if (!fs::exists(runDir, ec)) {
        res.error_code = importer_errc::E_RUN_DIR_MISSING;
        res.message = "run directory missing: " + runDir;
        return res;
    }
    fs::path outPath = fs::path(runDir) / "predictions" / (traceId + ".json");
    if (fs::exists(outPath, ec)) {
        res.error_code = importer_errc::E_PREDICTION_EXISTS;
        res.message = "prediction wrapper already exists (refuse overwrite): " +
                      outPath.string();
        return res;
    }
    // Load prompt hash if prompt exists, else null.
    std::string promptSha = "null";
    {
        std::string pt;
        ImporterResult lp = loadPromptSha(runDir, traceId, promptSha, pt);
        if (!lp.ok) promptSha = "null";
    }
    nlohmann::json w;
    w["evaluation_schema_version"] = "0.1.0";
    w["run_id"] = runId;
    w["trace_id"] = traceId;
    w["model_name"] = "hy3";
    w["prompt_template_id"] = "hy3-evaluator-v1";
    w["input_mode"] = "reference_assisted";
    w["prompt_sha256"] = promptSha;
    w["raw_response_sha256"] = nlohmann::json::value_t::null;
    w["parse_status"] = "model_call_not_attempted";
    w["prediction"] = nlohmann::json::value_t::null;
    w["errors"] = nlohmann::json::array();
    w["generated_at"] = generatedAt.empty()
                            ? nlohmann::json()
                            : nlohmann::json(generatedAt);
    std::string text = serializeWrapper(w);
    fs::path outDir = fs::path(runDir) / "predictions";
    std::error_code ec2;
    if (!fs::exists(outDir, ec2)) fs::create_directories(outDir, ec2);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "cannot open prediction wrapper: " + outPath.string();
        return res;
    }
    ofs.write(text.c_str(), static_cast<std::streamsize>(text.size()));
    if (!ofs) {
        res.error_code = importer_errc::E_WRITE_FAILED;
        res.message = "write failed: " + outPath.string();
        return res;
    }
    res.ok = true;
    return res;
}

} // namespace hy3
