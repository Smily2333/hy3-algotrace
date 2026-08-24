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
    "missing_greedy_proof", "invalid_greedy_proof", "complexity_error",
    "boundary_omission", "implementation_mismatch"};
const std::set<std::string> kStages{
    "problem_understanding", "greedy_choice", "greedy_proof", "complexity",
    "boundary", "implementation_consistency"};
const std::set<std::string> kAssessmentStatuses{"ok", "issue", "not_assessed"};

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
    if (required && output.empty()) {
        errors.push_back(path + " must not be empty");
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
        !text.empty()) {
        output = std::move(text);
    } else if (text.empty()) {
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
        {"request_id", request.request_id},
        {"algorithm_type", "greedy"},
        {"problem",
         {{"title", request.problem.title},
          {"statement", request.problem.statement},
          {"input_format", request.problem.input_format},
          {"output_format", request.problem.output_format},
          {"constraints", request.problem.constraints}}},
        {"reasoning", request.reasoning},
        {"cpp_solution", request.cpp_solution ? json(*request.cpp_solution) : json(nullptr)},
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
        {"schema_version", "interactive-model-call-0.1.0"},
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

bool stageMatchesCategory(const std::string& stage, const std::string& category) {
    if (stage == "problem_understanding") return category == "problem_misunderstanding";
    if (stage == "greedy_choice") return category == "wrong_greedy_choice";
    if (stage == "greedy_proof") {
        return category == "missing_greedy_proof" || category == "invalid_greedy_proof";
    }
    if (stage == "complexity") return category == "complexity_error";
    if (stage == "boundary") return category == "boundary_omission";
    if (stage == "implementation_consistency") {
        return category == "implementation_mismatch";
    }
    return false;
}

bool nonemptyBoundedString(const json& value, std::size_t limit) {
    return value.is_string() && !value.get_ref<const std::string&>().empty() &&
           value.get_ref<const std::string&>().size() <= limit;
}

std::string searchableInput(const InteractiveDiagnosisRequest& request) {
    std::ostringstream value;
    value << request.problem.title << '\n' << request.problem.statement << '\n'
          << request.problem.input_format << '\n' << request.problem.output_format
          << '\n' << request.problem.constraints << '\n' << request.reasoning;
    if (request.cpp_solution) value << '\n' << *request.cpp_solution;
    for (const auto& test : request.test_cases) {
        value << '\n' << test.input << '\n' << test.expected_output;
    }
    if (request.user_notes) value << '\n' << *request.user_notes;
    return value.str();
}

bool validateAssessment(const json& value) {
    return exactKeys(value, {"status", "summary"}) &&
           value.at("status").is_string() &&
           kAssessmentStatuses.count(value.at("status").get<std::string>()) != 0 &&
           nonemptyBoundedString(value.at("summary"), 4000);
}

std::string validateDiagnosis(const json& diagnosis,
                              const InteractiveDiagnosisRequest& request) {
    const std::set<std::string> topKeys{
        "schema_version", "request_id", "status", "primary_category",
        "findings", "assessments", "short_suggestion"};
    if (!exactKeys(diagnosis, topKeys)) return "top-level schema is invalid";
    if (diagnosis.at("schema_version") != "interactive-diagnosis-v1" ||
        diagnosis.at("request_id") != request.request_id ||
        !diagnosis.at("status").is_string()) {
        return "identity fields are invalid";
    }
    const std::string status = diagnosis.at("status").get<std::string>();
    if (status != "correct" && status != "incorrect" && status != "undetermined") {
        return "status is invalid";
    }
    if (!diagnosis.at("findings").is_array() ||
        diagnosis.at("findings").size() > 50) {
        return "findings is invalid";
    }

    std::optional<std::string> primary;
    if (diagnosis.at("primary_category").is_string()) {
        primary = diagnosis.at("primary_category").get<std::string>();
        if (kCategories.count(*primary) == 0) return "primary category is invalid";
    } else if (!diagnosis.at("primary_category").is_null()) {
        return "primary category type is invalid";
    }

    bool primarySeen = false;
    bool anyIssue = false;
    const std::string haystack = searchableInput(request);
    for (const json& finding : diagnosis.at("findings")) {
        if (!exactKeys(finding, {"stage", "category", "evidence",
                                 "input_excerpt", "suggestion"}) ||
            !finding.at("stage").is_string() ||
            !finding.at("category").is_string() ||
            !nonemptyBoundedString(finding.at("evidence"), 8000) ||
            !nonemptyBoundedString(finding.at("input_excerpt"), 2000) ||
            !nonemptyBoundedString(finding.at("suggestion"), 4000)) {
            return "finding schema is invalid";
        }
        const std::string stage = finding.at("stage").get<std::string>();
        const std::string category = finding.at("category").get<std::string>();
        if (kStages.count(stage) == 0 || kCategories.count(category) == 0 ||
            !stageMatchesCategory(stage, category)) {
            return "finding taxonomy is invalid";
        }
        if ((!request.cpp_solution &&
             (stage == "implementation_consistency" ||
              category == "implementation_mismatch"))) {
            return "implementation finding requires source code";
        }
        const std::string excerpt = finding.at("input_excerpt").get<std::string>();
        if (haystack.find(excerpt) == std::string::npos) {
            return "finding excerpt is not present in user input";
        }
        primarySeen = primarySeen || (primary && category == *primary);
        anyIssue = true;
    }

    const json& assessments = diagnosis.at("assessments");
    if (!exactKeys(assessments, {"complexity", "boundary_conditions",
                                 "implementation_consistency"}) ||
        !validateAssessment(assessments.at("complexity")) ||
        !validateAssessment(assessments.at("boundary_conditions")) ||
        !validateAssessment(assessments.at("implementation_consistency")) ||
        !nonemptyBoundedString(diagnosis.at("short_suggestion"), 4000)) {
        return "assessment schema is invalid";
    }
    if (!request.cpp_solution &&
        assessments.at("implementation_consistency").at("status") !=
            "not_assessed") {
        return "implementation assessment requires source code";
    }

    if (status == "incorrect") {
        if (!primary || !anyIssue || !primarySeen) {
            return "incorrect diagnosis semantics are invalid";
        }
    } else if (primary || anyIssue) {
        return "correct/undetermined diagnosis semantics are invalid";
    }
    if (status == "correct") {
        for (const char* key : {"complexity", "boundary_conditions",
                                "implementation_consistency"}) {
            if (assessments.at(key).at("status") == "issue") {
                return "correct diagnosis cannot contain an issue assessment";
            }
        }
    }
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

InteractiveRequestValidation parseInteractiveDiagnosisRequest(
    const json& input, InteractiveDiagnosisRequest& request) noexcept {
    InteractiveRequestValidation result;
    result.error_code = interactive_errc::E_REQUEST_SCHEMA;
    try {
        if (!allowedAndRequiredKeys(
                input,
                {"request_id", "algorithm_type", "problem", "reasoning",
                 "cpp_solution", "test_cases", "user_notes"},
                {"request_id", "algorithm_type", "problem", "reasoning"})) {
            result.errors.push_back("request keys do not match the interactive contract");
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

        const json& problem = input.at("problem");
        if (!exactKeys(problem, {"title", "statement", "input_format",
                                 "output_format", "constraints"})) {
            result.errors.push_back("problem keys do not match the interactive contract");
        } else {
            normalizeField(problem.at("title"), interactive_limits::title, true,
                           "problem.title", request.problem.title, result.errors);
            normalizeField(problem.at("statement"), interactive_limits::statement,
                           true, "problem.statement", request.problem.statement,
                           result.errors);
            normalizeField(problem.at("input_format"), interactive_limits::input_format,
                           true, "problem.input_format", request.problem.input_format,
                           result.errors);
            normalizeField(problem.at("output_format"), interactive_limits::output_format,
                           true, "problem.output_format", request.problem.output_format,
                           result.errors);
            normalizeField(problem.at("constraints"), interactive_limits::constraints,
                           true, "problem.constraints", request.problem.constraints,
                           result.errors);
        }
        normalizeField(input.at("reasoning"), interactive_limits::reasoning,
                       true, "reasoning", request.reasoning, result.errors);
        optionalField(input, "cpp_solution", interactive_limits::cpp_solution,
                      request.cpp_solution, result.errors);
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
        const json canonicalRequest = requestJson(request);
        InteractiveDiagnosisRequest checked;
        const InteractiveRequestValidation validation =
            parseInteractiveDiagnosisRequest(canonicalRequest, checked);
        if (!validation.ok) {
            result = failed(request.request_id, "invalid_request",
                            interactive_errc::E_REQUEST_INVALID,
                            "interactive request validation failed");
            result.validation_errors = validation.errors;
            return result;
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
                    const std::string semanticError = validateDiagnosis(diagnosis, request);
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
