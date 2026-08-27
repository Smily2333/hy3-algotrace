#include "hy3_algotrace/interactive_diagnosis.hpp"

#include "hy3_algotrace/model_runner.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <ctime>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#endif

namespace hy3 {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::set<std::string> kCategories{
    "problem_misunderstanding", "wrong_greedy_choice",
    "invalid_greedy_proof", "complexity_error", "boundary_omission",
    "implementation_mismatch", "code_logic_error"};

// Unicode White_Space (+ BOM) detection is separate from normalization: never
// trim source code merely to determine whether it contains meaningful input.
bool blank(const std::string& value) {
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char lead = static_cast<unsigned char>(value[i++]);
        std::uint32_t cp = lead;
        int continuation = 0;
        if (lead >= 0xF0) { cp = lead & 7U; continuation = 3; }
        else if (lead >= 0xE0) { cp = lead & 15U; continuation = 2; }
        else if (lead >= 0xC0) { cp = lead & 31U; continuation = 1; }
        while (continuation-- > 0 && i < value.size()) {
            cp = (cp << 6U) | (static_cast<unsigned char>(value[i++]) & 63U);
        }
        if (!((cp >= 9 && cp <= 13) || cp == 32 || cp == 0x85 ||
              cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
              cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
              cp == 0x3000 || cp == 0xFEFF)) return false;
    }
    return true;
}

std::string utcNow() {
    const std::time_t value = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

bool exactKeys(const json& value, const std::set<std::string>& keys) {
    if (!value.is_object() || value.size() != keys.size()) return false;
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (keys.count(it.key()) == 0) return false;
    }
    return true;
}

bool allowedAndRequiredKeys(const json& value,
                            const std::set<std::string>& allowed,
                            const std::set<std::string>& required) {
    if (!value.is_object()) return false;
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (allowed.count(it.key()) == 0) return false;
    }
    for (const auto& key : required) {
        if (!value.contains(key)) return false;
    }
    return true;
}

bool normalizeField(const json& value, std::size_t limit, bool required,
                    const std::string& path, std::string& output,
                    std::vector<std::string>& errors) {
    if (!value.is_string()) {
        errors.push_back(path + " must be a string");
        return false;
    }
    const std::string raw = value.get<std::string>();
    std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    std::vector<std::uint8_t> normalized;
    std::string error;
    if (!normalizeUtf8(bytes, normalized, error)) {
        errors.push_back(path + " must be valid UTF-8 without NUL");
        return false;
    }
    if (normalized.size() > limit) {
        errors.push_back(path + " exceeds its length limit");
        return false;
    }
    output.assign(normalized.begin(), normalized.end());
    if (required && blank(output)) {
        errors.push_back(path + " must not be blank");
        return false;
    }
    return true;
}

bool safeRequestId(const std::string& value) {
    if (value.empty() || value.size() > interactive_limits::request_id) return false;
    for (unsigned char ch : value) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '_' ||
                             ch == '-' || ch == '.';
        if (!allowed) return false;
    }
    return value != "." && value != "..";
}

void optionalField(const json& input, const char* key, std::size_t limit,
                   std::optional<std::string>& output,
                   std::vector<std::string>& errors) {
    if (!input.contains(key) || input.at(key).is_null()) {
        output.reset();
        return;
    }
    std::string text;
    if (normalizeField(input.at(key), limit, false, key, text, errors) &&
        !blank(text)) {
        output = std::move(text);
    } else {
        output.reset();
    }
}

json requestJson(const InteractiveDiagnosisRequest& request) {
    json tests = json::array();
    for (const auto& test : request.test_cases) {
        tests.push_back({{"input", test.input},
                         {"expected_output", test.expected_output}});
    }
    return json{
        {"schema_version", request.schema_version},
        {"request_id", request.request_id},
        {"algorithm_type", request.algorithm_type},
        {"problem_statement", request.problem_statement},
        {"reasoning", request.reasoning ? json(*request.reasoning) : json(nullptr)},
        {"cpp_solution", request.cpp_solution},
        {"test_cases", std::move(tests)},
        {"user_notes", request.user_notes ? json(*request.user_notes) : json(nullptr)},
    };
}

InteractiveDiagnosisResult failed(const std::string& requestId,
                                  const std::string& outcome,
                                  const std::string& code,
                                  const std::string& message) {
    InteractiveDiagnosisResult result;
    result.request_id = requestId;
    result.outcome = outcome;
    result.error_code = code;
    result.message = message;
    return result;
}

std::string transportCode(ModelCallStatus status) {
    switch (status) {
        case ModelCallStatus::ConfigurationError:
            return interactive_errc::E_MODEL_CONFIGURATION;
        case ModelCallStatus::AuthenticationError:
            return interactive_errc::E_MODEL_AUTHENTICATION;
        case ModelCallStatus::RateLimited:
            return interactive_errc::E_MODEL_RATE_LIMITED;
        case ModelCallStatus::Timeout: return interactive_errc::E_MODEL_TIMEOUT;
        case ModelCallStatus::TransportError:
            return interactive_errc::E_MODEL_TRANSPORT;
        case ModelCallStatus::ProviderError:
            return interactive_errc::E_MODEL_PROVIDER;
        case ModelCallStatus::Cancelled:
            return interactive_errc::E_MODEL_CANCELLED;
        case ModelCallStatus::Succeeded: break;
    }
    return interactive_errc::E_MODEL_PROVIDER;
}

std::string transportMessage(ModelCallStatus status) {
    switch (status) {
        case ModelCallStatus::ConfigurationError:
            return "TokenHub is not configured on the local server";
        case ModelCallStatus::AuthenticationError:
            return "TokenHub authentication failed";
        case ModelCallStatus::RateLimited: return "TokenHub rate limit was reached";
        case ModelCallStatus::Timeout: return "the model call timed out";
        case ModelCallStatus::TransportError: return "the model transport failed";
        case ModelCallStatus::ProviderError: return "the model provider returned an error";
        case ModelCallStatus::Cancelled: return "the model call was cancelled";
        case ModelCallStatus::Succeeded: break;
    }
    return "the model call failed";
}

json optionalString(const std::optional<std::string>& value) {
    return value ? json(*value) : json(nullptr);
}

json optionalInteger(const std::optional<int>& value) {
    return value ? json(*value) : json(nullptr);
}

json tokenUsageJson(const std::optional<ModelTokenUsage>& usage) {
    if (!usage) return nullptr;
    const auto number = [](const std::optional<std::uint64_t>& value) -> json {
        return value ? json(*value) : json(nullptr);
    };
    return json{{"prompt_tokens", number(usage->prompt_tokens)},
                {"completion_tokens", number(usage->completion_tokens)},
                {"total_tokens", number(usage->total_tokens)}};
}

void copyMetadata(const ModelCallResult& call, InteractiveCallMetadata& metadata) {
    metadata.provider = call.provider;
    metadata.model_name = call.model_name;
    metadata.model_version = call.model_version;
    metadata.http_status = call.http_status;
    metadata.provider_request_id = call.request_id;
    metadata.token_usage = call.token_usage;
    metadata.duration_ms = call.duration_ms;
}

json sidecar(const InteractiveDiagnosisResult& result,
             const ModelCallResult* call,
             const std::string& startedAt,
             const std::string& finishedAt) {
    json value{
        {"schema_version", "interactive-model-call-v2"},
        {"request_schema_version", kInteractiveRequestVersion},
        {"response_schema_version", kInteractiveResponseVersion},
        {"source_code_sha256", result.metadata.source_code_sha256},
        {"code_location_basis", "lf_1_based_inclusive"},
        {"request_id", result.request_id},
        {"outcome", result.outcome},
        {"parse_status", result.parse_status},
        {"prompt_template_id", result.metadata.prompt_template_id},
        {"prompt_template_sha256", result.metadata.prompt_template_sha256},
        {"prompt_sha256", result.metadata.prompt_sha256},
        {"raw_response_sha256", optionalString(result.metadata.raw_response_sha256)},
        {"provider", result.metadata.provider},
        {"model_name", result.metadata.model_name},
        {"model_version", optionalString(result.metadata.model_version)},
        {"http_status", optionalInteger(result.metadata.http_status)},
        {"provider_request_id", optionalString(result.metadata.provider_request_id)},
        {"token_usage", tokenUsageJson(result.metadata.token_usage)},
        {"duration_ms", result.metadata.duration_ms},
        {"started_at", startedAt},
        {"finished_at", finishedAt.empty() ? json(nullptr) : json(finishedAt)},
        {"response_saved", result.metadata.raw_response_sha256.has_value()},
        {"diagnosis_saved", result.ok},
        {"error_code", result.error_code.empty() ? json(nullptr) : json(result.error_code)},
    };
    if (call == nullptr) {
        value["provider"] = nullptr;
        value["model_name"] = "hy3";
    }
    return value;
}

bool writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    return static_cast<bool>(output);
}

bool writeText(const fs::path& path, const std::string& text) {
    return writeBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool replaceJson(const fs::path& path, const json& value) {
    static std::atomic<unsigned long long> sequence{0};
    fs::path temporary = path;
    temporary += ".tmp-" + std::to_string(++sequence) + "-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch().count());
    if (!writeText(temporary, value.dump(2) + "\n")) return false;
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
#else
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
#endif
    return true;
}

bool boundedString(const json& value, std::size_t limit, bool allowBlank = false) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    return text.size() <= limit && (allowBlank || !blank(text)) &&
           text.find('\0') == std::string::npos && text.find('\r') == std::string::npos;
}

std::vector<std::string> sourceLines(const std::string& source) {
    // The terminal LF is a separator, not an extra source line. Interior and
    // leading empty lines are real lines and must be included in snippets.
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < source.size()) {
        const auto next = source.find('\n', pos);
        lines.push_back(source.substr(pos, next == std::string::npos
                                              ? std::string::npos : next - pos));
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return lines;
}

bool validLocation(const json& location, const std::vector<std::string>& lines) {
    if (!exactKeys(location, {"start_line", "end_line", "snippet"}) ||
        !location.at("start_line").is_number_integer() ||
        !location.at("end_line").is_number_integer() ||
        !boundedString(location.at("snippet"), interactive_limits::cpp_solution)) {
        return false;
    }
    // Check JSON values before converting (including huge unsigned integers).
    if (location.at("start_line") < 1 ||
        location.at("end_line") < location.at("start_line") ||
        location.at("end_line") > lines.size()) return false;
    const auto first = location.at("start_line").get<std::size_t>();
    const auto last = location.at("end_line").get<std::size_t>();
    std::string expected;
    for (std::size_t i = first; i <= last; ++i) {
        if (i != first) expected += '\n';
        expected += lines[i - 1];
    }
    // Exact whole-line match at the declared range, not global substring match.
    return location.at("snippet") == expected;
}

bool validId(const json& value) {
    if (!boundedString(value, 64)) return false;
    const auto& id = value.get_ref<const std::string&>();
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
    });
}

std::string validateDiagnosis(const json& d, const InteractiveDiagnosisRequest& request) {
    if (!exactKeys(d, {"schema_version", "request_id", "status", "summary",
                      "limitations", "algorithm_overview", "steps", "first_error",
                      "primary_category", "findings", "counterexample",
                      "reference_solution"})) return "top-level schema is invalid";
    if (d.at("schema_version") != kInteractiveResponseVersion ||
        d.at("request_id") != request.request_id) return "identity fields are invalid";
    if (!d.at("status").is_string() || !boundedString(d.at("summary"), 4000))
        return "status/summary schema is invalid";
    const std::string status = d.at("status").get<std::string>();
    if (status != "correct" && status != "incorrect" && status != "undetermined")
        return "status is invalid";
    if (!d.at("limitations").is_array() || d.at("limitations").empty() ||
        d.at("limitations").size() > 10) return "limitations schema is invalid";
    for (const auto& item : d.at("limitations"))
        if (!boundedString(item, 2000)) return "limitation schema is invalid";
    const auto& overview = d.at("algorithm_overview");
    if (!exactKeys(overview, {"origin", "summary"}) ||
        overview.at("origin") != "model_code_interpretation" ||
        !boundedString(overview.at("summary"), 4000)) return "overview schema is invalid";

    const auto lines = sourceLines(request.cpp_solution);
    const auto& steps = d.at("steps");
    if (!steps.is_array() || steps.size() > 30 ||
        (status != "undetermined" && steps.empty())) return "steps schema is invalid";
    std::map<std::string, std::size_t> stepIndexes;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps.at(i);
        if (!exactKeys(step, {"id", "summary", "code_location"}) ||
            !validId(step.at("id")) || !boundedString(step.at("summary"), 2000))
            return "step schema is invalid";
        if (!stepIndexes.emplace(step.at("id").get<std::string>(), i).second)
            return "step IDs must be unique";
        if (!validLocation(step.at("code_location"), lines))
            return "step source location is invalid";
    }
    const auto& first = d.at("first_error");
    if (!exactKeys(first, {"step_id", "explanation"}) ||
        (!first.at("step_id").is_null() && !validId(first.at("step_id"))) ||
        !boundedString(first.at("explanation"), 4000)) return "first error schema is invalid";

    std::optional<std::string> primary;
    if (d.at("primary_category").is_string()) {
        primary = d.at("primary_category").get<std::string>();
        if (kCategories.count(*primary) == 0) return "primary category is invalid";
    } else if (!d.at("primary_category").is_null()) return "primary category type is invalid";
    const auto& findings = d.at("findings");
    if (!findings.is_array() || findings.size() > 20) return "findings schema is invalid";
    bool primarySeen = false;
    bool unlocated = false;
    std::size_t earliest = steps.size();
    std::set<std::string> findingIds;
    for (const auto& f : findings) {
        if (!exactKeys(f, {"id", "step_id", "category", "reason", "input_evidence",
                          "code_location", "location_reason", "suggestion"}) ||
            !validId(f.at("id")) || !boundedString(f.at("category"), 64) ||
            !boundedString(f.at("reason"), 8000) ||
            !boundedString(f.at("suggestion"), 8000))
            return "finding schema is invalid";
        if (!findingIds.insert(f.at("id").get<std::string>()).second)
            return "finding IDs must be unique";
        const std::string category = f.at("category").get<std::string>();
        if (kCategories.count(category) == 0) return "finding category is invalid";
        const bool reasoningOnly = category == "implementation_mismatch" ||
                                   category == "invalid_greedy_proof";
        if (reasoningOnly && !request.reasoning)
            return "finding requires explicit user reasoning";
        primarySeen = primarySeen || (primary && category == *primary);
        const auto& location = f.at("code_location");
        if (location.is_null()) {
            if (!f.at("step_id").is_null() || !boundedString(f.at("location_reason"), 2000))
                return "unlocated finding requires a reason and null step";
        } else if (!validLocation(location, lines) || !f.at("location_reason").is_null()) {
            return "finding source location is invalid";
        }
        if (f.at("step_id").is_null()) {
            unlocated = true;
            if (!location.is_null()) return "located finding requires a step";
        } else {
            if (!validId(f.at("step_id"))) return "step reference type is invalid";
            const auto it = stepIndexes.find(f.at("step_id").get<std::string>());
            if (it == stepIndexes.end()) return "finding step reference does not exist";
            const auto& scope = steps.at(it->second).at("code_location");
            if (location.at("start_line") < scope.at("start_line") ||
                location.at("end_line") > scope.at("end_line"))
                return "finding location is outside its referenced step";
            earliest = std::min(earliest, it->second);
        }
        const auto& evidence = f.at("input_evidence");
        if (!exactKeys(evidence, {"source", "excerpt"}) ||
            !boundedString(evidence.at("source"), 64) ||
            !boundedString(evidence.at("excerpt"), 8000))
            return "input evidence schema is invalid";
        const auto evidenceSource = evidence.at("source").get<std::string>();
        const auto excerpt = evidence.at("excerpt").get<std::string>();
        std::string input;
        if (evidenceSource == "problem_statement") input = request.problem_statement;
        else if (evidenceSource == "reasoning" && request.reasoning) input = *request.reasoning;
        else if (evidenceSource == "user_notes" && request.user_notes) input = *request.user_notes;
        else if (evidenceSource == "cpp_solution" && !location.is_null())
            input = location.at("snippet").get<std::string>();
        else return "evidence source is unavailable";
        if (input.find(excerpt) == std::string::npos)
            return "evidence excerpt does not match its declared input";
        if (reasoningOnly && evidenceSource != "reasoning")
            return "reasoning finding must quote actual user reasoning";
    }
    if (status == "incorrect") {
        if (!primary || findings.empty() || !primarySeen)
            return "incorrect diagnosis semantics are invalid";
        if (!first.at("step_id").is_null()) {
            const auto it = stepIndexes.find(first.at("step_id").get<std::string>());
            if (unlocated || it == stepIndexes.end() || it->second != earliest)
                return "first error must reference the first located finding in logical step order";
        }
    } else if (primary || !findings.empty() || !first.at("step_id").is_null()) {
        return "correct/undetermined diagnosis semantics are invalid";
    }

    const auto& counter = d.at("counterexample");
    if (!exactKeys(counter, {"availability", "input", "expected_output",
                             "predicted_candidate_output", "candidate_output_basis",
                             "explanation", "provenance"}) ||
        counter.at("provenance") != "model_proposed_not_executed" ||
        !boundedString(counter.at("explanation"), 8000))
        return "counterexample schema is invalid";
    if (counter.at("availability") == "provided") {
        if (status != "incorrect" ||
            !boundedString(counter.at("input"), 20000, true) ||
            !boundedString(counter.at("expected_output"), 20000, true))
            return "counterexample contradicts diagnosis";
        if (counter.at("predicted_candidate_output").is_null()) {
            if (!counter.at("candidate_output_basis").is_null())
                return "absent candidate output must have null basis";
        } else if (!boundedString(counter.at("predicted_candidate_output"), 20000, true) ||
                   counter.at("candidate_output_basis") != "static_inference")
            return "candidate output must be marked as static inference";
    } else if (counter.at("availability") == "unavailable") {
        for (const char* key : {"input", "expected_output", "predicted_candidate_output",
                                "candidate_output_basis"})
            if (!counter.at(key).is_null()) return "unavailable counterexample must be null";
    } else return "counterexample availability is invalid";

    const auto& solution = d.at("reference_solution");
    if (!exactKeys(solution, {"availability", "strategy", "correctness",
                              "complexity", "boundaries", "unavailable_reason", "provenance"}) ||
        solution.at("provenance") != "model_generated_unverified")
        return "reference solution schema is invalid";
    if (solution.at("availability") == "provided") {
        if (status == "undetermined" || !solution.at("unavailable_reason").is_null())
            return "reference solution contradicts uncertainty";
        for (const char* key : {"strategy", "correctness", "complexity", "boundaries"})
            if (!boundedString(solution.at(key), 16000)) return "complete solution schema is invalid";
    } else if (solution.at("availability") == "unavailable") {
        if (!boundedString(solution.at("unavailable_reason"), 4000))
            return "unavailable solution requires explanation";
        for (const char* key : {"strategy", "correctness", "complexity", "boundaries"})
            if (!solution.at(key).is_null()) return "unavailable solution must not invent content";
    } else return "reference solution availability is invalid";
    return {};
}

std::string errorKind(const InteractiveDiagnosisResult& result) {
    if (result.error_code == interactive_errc::E_REQUEST_SCHEMA ||
        result.error_code == interactive_errc::E_REQUEST_INVALID) return "validation_error";
    if (result.error_code == interactive_errc::E_REQUEST_EXISTS) return "duplicate_request";
    if (result.error_code == interactive_errc::E_MODEL_CONFIGURATION) return "configuration_error";
    if (result.error_code == interactive_errc::E_MODEL_AUTHENTICATION) return "authentication_error";
    if (result.error_code == interactive_errc::E_MODEL_RATE_LIMITED) return "rate_limited";
    if (result.error_code == interactive_errc::E_MODEL_TIMEOUT) return "timeout";
    if (result.error_code == interactive_errc::E_MODEL_TRANSPORT) return "transport_error";
    if (result.error_code == interactive_errc::E_MODEL_PROVIDER) return "provider_error";
    if (result.error_code == interactive_errc::E_MODEL_CANCELLED) return "cancelled";
    if (result.error_code == interactive_errc::E_RESPONSE_ENCODING ||
        result.error_code == interactive_errc::E_RESPONSE_INVALID) return "invalid_model_response";
    return "artifact_error";
}

} // namespace

json interactiveDiagnosisRequestJson(const InteractiveDiagnosisRequest& request) {
    return requestJson(request);
}

bool validInteractivePromptTemplate(const std::string& text) {
    std::vector<std::uint8_t> normalized;
    std::string error;
    if (!normalizeUtf8(std::vector<std::uint8_t>(text.begin(), text.end()), normalized, error))
        return false;
    const std::string value(normalized.begin(), normalized.end());
    const std::string header = std::string("# ") + kInteractiveTemplateId + "\n";
    const std::string marker = "{{interactive_request_json}}";
    const auto pos = value.find(marker);
    return value.compare(0, header.size(), header) == 0 &&
           pos != std::string::npos &&
           value.find(marker, pos + marker.size()) == std::string::npos;
}

InteractiveRequestValidation parseInteractiveDiagnosisRequest(
    const json& input, InteractiveDiagnosisRequest& request) noexcept {
    InteractiveRequestValidation result;
    result.error_code = interactive_errc::E_REQUEST_SCHEMA;
    try {
        request = InteractiveDiagnosisRequest{};
        if (!allowedAndRequiredKeys(
                input,
                {"schema_version", "request_id", "algorithm_type", "problem_statement",
                 "reasoning", "cpp_solution", "test_cases", "user_notes"},
                {"schema_version", "request_id", "algorithm_type", "problem_statement",
                 "cpp_solution"})) {
            result.errors.push_back("request keys must match interactive-request-v2; v1 is unsupported");
            return result;
        }
        if (input.at("schema_version") != kInteractiveRequestVersion) {
            result.errors.push_back("schema_version must be interactive-request-v2");
            return result;
        }
        normalizeField(input.at("request_id"), interactive_limits::request_id,
                       true, "request_id", request.request_id, result.errors);
        if (!safeRequestId(request.request_id)) {
            result.errors.push_back("request_id contains unsupported characters");
        }
        normalizeField(input.at("algorithm_type"), 32, true, "algorithm_type",
                       request.algorithm_type, result.errors);
        if (request.algorithm_type != "greedy") {
            result.errors.push_back("algorithm_type must be greedy");
        }
        normalizeField(input.at("problem_statement"), interactive_limits::statement,
                       true, "problem_statement", request.problem_statement, result.errors);
        normalizeField(input.at("cpp_solution"), interactive_limits::cpp_solution,
                       true, "cpp_solution", request.cpp_solution, result.errors);
        optionalField(input, "reasoning", interactive_limits::reasoning,
                      request.reasoning, result.errors);
        optionalField(input, "user_notes", interactive_limits::user_notes,
                      request.user_notes, result.errors);

        if (!input.contains("test_cases") || input.at("test_cases").is_null()) {
            request.test_cases.clear();
        } else if (!input.at("test_cases").is_array()) {
            result.errors.push_back("test_cases must be an array");
        } else if (input.at("test_cases").size() > interactive_limits::test_cases) {
            result.errors.push_back("test_cases exceeds its item limit");
        } else {
            for (std::size_t i = 0; i < input.at("test_cases").size(); ++i) {
                const json& item = input.at("test_cases").at(i);
                if (!exactKeys(item, {"input", "expected_output"})) {
                    result.errors.push_back("test_cases item keys are invalid");
                    continue;
                }
                InteractiveTestCase test;
                normalizeField(item.at("input"), interactive_limits::test_text,
                               false, "test_cases.input", test.input, result.errors);
                normalizeField(item.at("expected_output"),
                               interactive_limits::test_text, false,
                               "test_cases.expected_output", test.expected_output,
                               result.errors);
                request.test_cases.push_back(std::move(test));
            }
        }
        result.ok = result.errors.empty();
        if (!result.ok) result.error_code = interactive_errc::E_REQUEST_INVALID;
        return result;
    } catch (...) {
        result.errors.push_back("request JSON has a missing or invalid field");
        return result;
    }
}

InteractiveDiagnosisResult runInteractiveDiagnosis(
    const InteractiveDiagnosisRequest& request,
    const std::string& promptTemplateText,
    const std::string& artifactsRoot,
    IModelClient& client) noexcept {
    InteractiveDiagnosisResult result;
    result.request_id = request.request_id;
    result.outcome = "artifact_error";
    try {
        const json inputRequest = requestJson(request);
        InteractiveDiagnosisRequest checked;
        const InteractiveRequestValidation validation =
            parseInteractiveDiagnosisRequest(inputRequest, checked);
        if (!validation.ok) {
            result = failed(request.request_id, "invalid_request",
                            interactive_errc::E_REQUEST_INVALID,
                            "interactive request validation failed");
            result.validation_errors = validation.errors;
            return result;
        }

        const json canonicalRequest = requestJson(checked);
        result.metadata.source_code_sha256 = sha256_hex(checked.cpp_solution);
        if (!validInteractivePromptTemplate(promptTemplateText)) {
            return failed(request.request_id, "artifact_error",
                          interactive_errc::E_TEMPLATE_INVALID,
                          "interactive v2 prompt template header or marker is invalid");
        }
        std::vector<std::uint8_t> templateRaw(promptTemplateText.begin(),
                                              promptTemplateText.end());
        std::vector<std::uint8_t> templateNormalized;
        std::string normalizationError;
        if (!normalizeUtf8(templateRaw, templateNormalized, normalizationError)) {
            return failed(request.request_id, "artifact_error",
                          interactive_errc::E_TEMPLATE_INVALID,
                          "interactive prompt template is invalid");
        }
        std::string rendered(templateNormalized.begin(), templateNormalized.end());
        const std::string marker = "{{interactive_request_json}}";
        const std::size_t markerPosition = rendered.find(marker);
        if (markerPosition == std::string::npos ||
            rendered.find(marker, markerPosition + marker.size()) != std::string::npos) {
            return failed(request.request_id, "artifact_error",
                          interactive_errc::E_TEMPLATE_INVALID,
                          "interactive prompt template marker is invalid");
        }
        result.metadata.prompt_template_sha256 = sha256_hex(rendered);
        rendered.replace(markerPosition, marker.size(), canonicalRequest.dump(2));
        if (rendered.size() > interactive_limits::rendered_prompt) {
            return failed(request.request_id, "invalid_request",
                          interactive_errc::E_REQUEST_INVALID,
                          "rendered prompt exceeds the safety limit");
        }
        result.metadata.prompt_sha256 = sha256_hex(rendered);

        if (artifactsRoot.empty()) {
            return failed(request.request_id, "artifact_error",
                          interactive_errc::E_ARTIFACTS_ROOT,
                          "interactive artifacts root is unavailable");
        }
        const fs::path root(artifactsRoot);
        std::error_code ec;
        fs::create_directories(root, ec);
        if (ec || !fs::is_directory(root, ec) || ec) {
            return failed(request.request_id, "artifact_error",
                          interactive_errc::E_ARTIFACTS_ROOT,
                          "interactive artifacts root is unavailable");
        }
        const fs::path run = root / request.request_id;
        if (!fs::create_directory(run, ec) || ec) {
            return failed(request.request_id, "duplicate_request",
                          interactive_errc::E_REQUEST_EXISTS,
                          "request_id has already been attempted");
        }

        const fs::path sidecarPath = run / "model-call.json";
        result.outcome = "attempting";
        result.metadata.model_name = "hy3";
        const std::string promptBytes = rendered;
        if (!writeText(run / "prompt.txt", promptBytes) ||
            !replaceJson(sidecarPath, sidecar(result, nullptr, utcNow(), ""))) {
            result = failed(request.request_id, "artifact_error",
                            interactive_errc::E_ARTIFACT_WRITE,
                            "could not write the local audit record");
            return result;
        }

        ModelRequest modelRequest;
        modelRequest.trace_id = request.request_id;
        modelRequest.normalized_prompt = rendered;
        modelRequest.prompt_sha256 = result.metadata.prompt_sha256;
        ModelCallResult call = invokeModelOnce(modelRequest, client);
        copyMetadata(call, result.metadata);

        if (call.status != ModelCallStatus::Succeeded) {
            result.outcome = "model_call_failed";
            result.error_code = transportCode(call.status);
            result.message = transportMessage(call.status);
            (void)replaceJson(sidecarPath,
                              sidecar(result, &call, call.started_at, call.finished_at));
            return result;
        }

        result.metadata.raw_response_sha256 = sha256_hex(call.raw_response);
        if (!writeBytes(run / "raw-response.txt", call.raw_response)) {
            result.outcome = "artifact_error";
            result.error_code = interactive_errc::E_ARTIFACT_WRITE;
            result.message = "could not write the local audit record";
            (void)replaceJson(sidecarPath,
                              sidecar(result, &call, call.started_at, call.finished_at));
            return result;
        }
        if (call.raw_response.empty()) {
            result.outcome = "invalid_model_response";
            result.parse_status = "empty_response";
            result.error_code = interactive_errc::E_RESPONSE_INVALID;
            result.message = "model returned an empty response";
        } else if (call.raw_response.size() > interactive_limits::model_response) {
            result.outcome = "invalid_model_response";
            result.parse_status = "schema_invalid";
            result.error_code = interactive_errc::E_RESPONSE_INVALID;
            result.message = "model response exceeds the safety limit";
        } else {
            std::vector<std::uint8_t> normalizedRaw;
            std::string utf8Error;
            if (!normalizeUtf8(call.raw_response, normalizedRaw, utf8Error)) {
                result.outcome = "invalid_model_response";
                result.parse_status = "invalid_utf8";
                result.error_code = interactive_errc::E_RESPONSE_ENCODING;
                result.message = "model response is not valid UTF-8 JSON";
            } else {
                const json diagnosis = json::parse(normalizedRaw.begin(),
                                                   normalizedRaw.end(), nullptr,
                                                   false, false);
                if (diagnosis.is_discarded()) {
                    result.outcome = "invalid_model_response";
                    result.parse_status = "invalid_json";
                    result.error_code = interactive_errc::E_RESPONSE_INVALID;
                    result.message = "model response is not valid JSON";
                } else {
                    const std::string semanticError = validateDiagnosis(diagnosis, checked);
                    if (!semanticError.empty()) {
                        result.outcome = "invalid_model_response";
                        result.parse_status = semanticError.find("schema") != std::string::npos ||
                                                      semanticError.find("type") != std::string::npos ||
                                                      semanticError.find("identity") != std::string::npos
                                                  ? "schema_invalid"
                                                  : "semantic_invalid";
                        result.error_code = interactive_errc::E_RESPONSE_INVALID;
                        result.message = "model response failed strict validation";
                    } else {
                        result.ok = true;
                        result.outcome = "diagnosed";
                        result.parse_status = "parsed";
                        result.diagnosis = diagnosis;
                        result.message = "diagnosis completed";
                        if (!replaceJson(run / "diagnosis.json", diagnosis)) {
                            result.ok = false;
                            result.outcome = "artifact_error";
                            result.error_code = interactive_errc::E_ARTIFACT_WRITE;
                            result.message = "could not write the local audit record";
                        }
                    }
                }
            }
        }
        if (!replaceJson(sidecarPath,
                         sidecar(result, &call, call.started_at, call.finished_at))) {
            result.ok = false;
            result.outcome = "artifact_error";
            result.error_code = interactive_errc::E_ARTIFACT_WRITE;
            result.message = "could not finalize the local audit record";
        }
        return result;
    } catch (...) {
        return failed(request.request_id, "artifact_error",
                      interactive_errc::E_ARTIFACT_WRITE,
                      "interactive diagnosis failed safely");
    }
}

json interactiveDiagnosisBrowserJson(const InteractiveDiagnosisResult& result) {
    json metadata{
        {"request_schema_version", kInteractiveRequestVersion},
        {"response_schema_version", kInteractiveResponseVersion},
        {"source_code_sha256", result.metadata.source_code_sha256},
        {"code_location_basis", "lf_1_based_inclusive"},
        {"prompt_template_id", result.metadata.prompt_template_id},
        {"prompt_template_sha256", result.metadata.prompt_template_sha256},
        {"prompt_sha256", result.metadata.prompt_sha256},
        {"response_sha256", optionalString(result.metadata.raw_response_sha256)},
        {"provider", result.metadata.provider},
        {"model_name", result.metadata.model_name},
        {"model_version", optionalString(result.metadata.model_version)},
        {"http_status", optionalInteger(result.metadata.http_status)},
        {"provider_request_id", optionalString(result.metadata.provider_request_id)},
        {"token_usage", tokenUsageJson(result.metadata.token_usage)},
        {"duration_ms", result.metadata.duration_ms},
    };
    json browser{
        {"ok", result.ok},
        {"request_id", result.request_id},
        {"outcome", result.outcome},
        {"parse_status", result.parse_status},
        {"diagnosis", result.ok ? result.diagnosis : json(nullptr)},
        {"metadata", std::move(metadata)},
        {"error", nullptr},
    };
    if (!result.ok) {
        browser["error"] = {{"kind", errorKind(result)},
                            {"code", result.error_code},
                            {"message", result.message},
                            {"details", result.validation_errors}};
    }
    return browser;
}

} // namespace hy3
