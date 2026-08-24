// hy3-algotrace — interactive greedy diagnosis business layer.
//
// This module is deliberately independent from the frozen evaluation runs. It
// accepts ephemeral user input, renders the versioned interactive prompt, and
// invokes an injected IModelClient at most once. It never reads gold data,
// executes submitted code, or exposes raw transport details to the browser.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "hy3_algotrace/model_client.hpp"

namespace hy3 {

namespace interactive_errc {
inline constexpr const char* E_REQUEST_SCHEMA = "E_INTERACTIVE_REQUEST_SCHEMA";
inline constexpr const char* E_REQUEST_INVALID = "E_INTERACTIVE_REQUEST_INVALID";
inline constexpr const char* E_TEMPLATE_INVALID = "E_INTERACTIVE_TEMPLATE_INVALID";
inline constexpr const char* E_ARTIFACTS_ROOT = "E_INTERACTIVE_ARTIFACTS_ROOT";
inline constexpr const char* E_REQUEST_EXISTS = "E_INTERACTIVE_REQUEST_EXISTS";
inline constexpr const char* E_ARTIFACT_WRITE = "E_INTERACTIVE_ARTIFACT_WRITE";
inline constexpr const char* E_MODEL_CONFIGURATION = "E_INTERACTIVE_MODEL_CONFIGURATION";
inline constexpr const char* E_MODEL_AUTHENTICATION = "E_INTERACTIVE_MODEL_AUTHENTICATION";
inline constexpr const char* E_MODEL_RATE_LIMITED = "E_INTERACTIVE_MODEL_RATE_LIMITED";
inline constexpr const char* E_MODEL_TIMEOUT = "E_INTERACTIVE_MODEL_TIMEOUT";
inline constexpr const char* E_MODEL_TRANSPORT = "E_INTERACTIVE_MODEL_TRANSPORT";
inline constexpr const char* E_MODEL_PROVIDER = "E_INTERACTIVE_MODEL_PROVIDER";
inline constexpr const char* E_MODEL_CANCELLED = "E_INTERACTIVE_MODEL_CANCELLED";
inline constexpr const char* E_RESPONSE_ENCODING = "E_INTERACTIVE_RESPONSE_ENCODING";
inline constexpr const char* E_RESPONSE_INVALID = "E_INTERACTIVE_RESPONSE_INVALID";
} // namespace interactive_errc

namespace interactive_limits {
inline constexpr std::size_t request_id = 128;
inline constexpr std::size_t title = 256;
inline constexpr std::size_t statement = 30000;
inline constexpr std::size_t input_format = 10000;
inline constexpr std::size_t output_format = 10000;
inline constexpr std::size_t constraints = 10000;
inline constexpr std::size_t reasoning = 30000;
inline constexpr std::size_t cpp_solution = 120000;
inline constexpr std::size_t user_notes = 10000;
inline constexpr std::size_t test_text = 20000;
inline constexpr std::size_t test_cases = 20;
inline constexpr std::size_t rendered_prompt = 300000;
inline constexpr std::size_t model_response = 1000000;
} // namespace interactive_limits

struct InteractiveProblem {
    std::string title;
    std::string statement;
    std::string input_format;
    std::string output_format;
    std::string constraints;
};

struct InteractiveTestCase {
    std::string input;
    std::string expected_output;
};

struct InteractiveDiagnosisRequest {
    std::string request_id;
    std::string algorithm_type = "greedy";
    InteractiveProblem problem;
    std::string reasoning;
    std::optional<std::string> cpp_solution;
    std::vector<InteractiveTestCase> test_cases;
    std::optional<std::string> user_notes;
};

struct InteractiveRequestValidation {
    bool ok = false;
    std::string error_code;
    std::vector<std::string> errors;
};

// Strictly parses the HTTP JSON body. Unknown keys, wrong JSON types, invalid
// UTF-8/NUL, over-limit fields, and algorithm_type != "greedy" are rejected.
// Optional strings may be absent or null; empty optional strings normalize to
// null. Line endings in accepted user text normalize to LF before hashing.
InteractiveRequestValidation parseInteractiveDiagnosisRequest(
    const nlohmann::json& input,
    InteractiveDiagnosisRequest& request) noexcept;

struct InteractiveCallMetadata {
    std::string prompt_template_id = "hy3-interactive-diagnosis-v1";
    std::string prompt_template_sha256;
    std::string prompt_sha256;
    std::optional<std::string> raw_response_sha256;
    std::string provider;
    std::string model_name;
    std::optional<std::string> model_version;
    std::optional<int> http_status;
    std::optional<std::string> provider_request_id;
    std::optional<ModelTokenUsage> token_usage;
    std::uint64_t duration_ms = 0;
};

struct InteractiveDiagnosisResult {
    bool ok = false;
    std::string request_id;
    // diagnosed | invalid_request | duplicate_request | model_call_failed |
    // invalid_model_response | artifact_error
    std::string outcome;
    // not_attempted | empty_response | invalid_utf8 | invalid_json |
    // schema_invalid | semantic_invalid | parsed
    std::string parse_status = "not_attempted";
    std::string error_code;
    std::string message; // fixed browser-safe text; never provider error text
    std::vector<std::string> validation_errors;
    nlohmann::json diagnosis = nullptr;
    InteractiveCallMetadata metadata;
};

// Renders {{interactive_request_json}} in the supplied versioned template and
// performs at most one model invocation. artifactsRoot/request_id is an
// immutable one-shot run directory: creation latches the request before the
// call, and any pre-existing directory rejects the request without invoking.
// Successful raw bytes are saved verbatim for local audit; browser JSON never
// includes them. No automatic retry is performed for any outcome.
InteractiveDiagnosisResult runInteractiveDiagnosis(
    const InteractiveDiagnosisRequest& request,
    const std::string& promptTemplateText,
    const std::string& artifactsRoot,
    IModelClient& client) noexcept;

// Only browser-safe fields are serialized. In particular this never includes
// raw response bytes, prompt/user content, provider error messages, headers,
// credentials, or local filesystem paths.
nlohmann::json interactiveDiagnosisBrowserJson(
    const InteractiveDiagnosisResult& result);

} // namespace hy3
