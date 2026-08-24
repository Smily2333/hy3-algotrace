#include "hy3_algotrace/model_runner.hpp"

#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/prediction_importer.hpp"

#include <filesystem>
#include <utility>

namespace hy3 {
namespace fs = std::filesystem;
namespace {

ModelRunResult failBeforeImport(const std::string& code,
                                const std::string& message,
                                ModelCallResult callResult = {}) {
    ModelRunResult result;
    result.error_code = code;
    result.message = message;
    result.call_result = std::move(callResult);
    return result;
}

const char* defaultCallErrorCode(ModelCallStatus status) noexcept {
    switch (status) {
        case ModelCallStatus::ConfigurationError:
            return model_runner_errc::E_MODEL_CONFIGURATION;
        case ModelCallStatus::AuthenticationError:
            return model_runner_errc::E_MODEL_AUTHENTICATION;
        case ModelCallStatus::RateLimited:
            return model_runner_errc::E_MODEL_RATE_LIMITED;
        case ModelCallStatus::Timeout:
            return model_runner_errc::E_MODEL_TIMEOUT;
        case ModelCallStatus::TransportError:
            return model_runner_errc::E_MODEL_TRANSPORT;
        case ModelCallStatus::ProviderError:
            return model_runner_errc::E_MODEL_PROVIDER;
        case ModelCallStatus::Cancelled:
            return model_runner_errc::E_MODEL_CANCELLED;
        case ModelCallStatus::Succeeded:
            return model_runner_errc::E_MODEL_RESULT_INVALID;
    }
    return model_runner_errc::E_MODEL_RESULT_INVALID;
}

} // namespace

ModelRunResult runModelForTrace(const std::string& runDir,
                                const std::string& traceId,
                                const std::string& runId,
                                const std::string& generatedAt,
                                IModelClient& client) {
    const LoadResult manifest = loadJsonFile("run-manifest.json", runDir);
    if (!manifest.ok || !manifest.doc.is_object()) {
        const std::string detail = manifest.ok
                                       ? "run-manifest is not an object"
                                       : manifest.error_code + ": " +
                                             manifest.error_message;
        return failBeforeImport(model_runner_errc::E_RUN_CONTEXT, detail);
    }
    const auto requiredString = [&](const char* key, std::string& value) {
        if (!manifest.doc.contains(key) || !manifest.doc.at(key).is_string()) {
            return false;
        }
        value = manifest.doc.at(key).get<std::string>();
        return !value.empty();
    };
    std::string manifestRunId;
    std::string manifestProvider;
    std::string manifestModel;
    if (!requiredString("run_id", manifestRunId) ||
        !requiredString("model_provider", manifestProvider) ||
        !requiredString("model_name", manifestModel)) {
        return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                "run-manifest model identity is missing or invalid");
    }
    if (manifestRunId != runId) {
        return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                "run_id does not match run-manifest");
    }

    std::string promptSha256;
    std::string normalizedPrompt;
    const ImporterResult promptResult =
        loadPromptSha(runDir, traceId, promptSha256, normalizedPrompt);
    if (!promptResult.ok) {
        return failBeforeImport(model_runner_errc::E_PROMPT_LOAD,
                                promptResult.error_code + ": " +
                                    promptResult.message);
    }

    // Refuse before a potentially billable call when this trace already has
    // importer output. The importer repeats the checks to protect direct
    // in-memory/manual callers.
    std::error_code pathError;
    const fs::path rawPath =
        fs::path(runDir) / "raw-responses" / (traceId + ".txt");
    const fs::path predictionPath =
        fs::path(runDir) / "predictions" / (traceId + ".json");
    const bool rawExists = fs::exists(rawPath, pathError);
    if (pathError) {
        return failBeforeImport(model_runner_errc::E_IMPORT_PRECHECK,
                                "cannot inspect raw response path: " +
                                    pathError.message());
    }
    const bool predictionExists = fs::exists(predictionPath, pathError);
    if (pathError) {
        return failBeforeImport(model_runner_errc::E_IMPORT_PRECHECK,
                                "cannot inspect prediction path: " +
                                    pathError.message());
    }
    if (rawExists || predictionExists) {
        return failBeforeImport(model_runner_errc::E_IMPORT_PRECHECK,
                                "raw response or prediction already exists");
    }

    ModelRequest request;
    request.trace_id = traceId;
    request.normalized_prompt = normalizedPrompt;
    request.prompt_sha256 = promptSha256;

    ModelCallResult callResult;
    try {
        callResult = client.invoke(request);
    } catch (...) {
        // The interface is noexcept, but retain a defensive boundary in case a
        // non-conforming adapter is introduced through ABI or future changes.
        callResult.status = ModelCallStatus::ProviderError;
        callResult.error_code = model_runner_errc::E_MODEL_PROVIDER;
        callResult.message = "model client escaped an exception";
    }

    if (callResult.status != ModelCallStatus::Succeeded) {
        if (!callResult.raw_response.empty()) {
            return failBeforeImport(
                model_runner_errc::E_MODEL_RESULT_INVALID,
                "failed model call returned raw response bytes",
                std::move(callResult));
        }

        const std::string adapterCode = callResult.error_code.empty()
                                            ? defaultCallErrorCode(callResult.status)
                                            : callResult.error_code;
        std::string detail = std::string(modelCallStatusName(callResult.status));
        if (!callResult.message.empty()) {
            detail += ": " + callResult.message;
        }
        return failBeforeImport(adapterCode, detail, std::move(callResult));
    }

    if (callResult.provider.empty() || callResult.model_name.empty() ||
        callResult.started_at.empty() || callResult.finished_at.empty()) {
        return failBeforeImport(model_runner_errc::E_MODEL_RESULT_INVALID,
                                "successful model call missing identity or time metadata",
                                std::move(callResult));
    }
    if (callResult.provider != manifestProvider ||
        callResult.model_name != manifestModel) {
        return failBeforeImport(model_runner_errc::E_MODEL_RESULT_INVALID,
                                "model call identity does not match run-manifest",
                                std::move(callResult));
    }
    if (manifest.doc.contains("model_version") &&
        manifest.doc.at("model_version").is_string()) {
        const std::string expectedVersion =
            manifest.doc.at("model_version").get<std::string>();
        if (!callResult.model_version ||
            *callResult.model_version != expectedVersion) {
            return failBeforeImport(model_runner_errc::E_MODEL_RESULT_INVALID,
                                    "model version does not match run-manifest",
                                    std::move(callResult));
        }
    }

    const ImporterResult importResult =
        importResponseBytes(runDir, traceId, callResult.raw_response, runId,
                            generatedAt);
    if (!importResult.ok) {
        return failBeforeImport(model_runner_errc::E_IMPORT_FAILED,
                                importResult.error_code + ": " +
                                    importResult.message,
                                std::move(callResult));
    }

    ModelRunResult result;
    result.ok = true;
    result.call_result = std::move(callResult);
    return result;
}

} // namespace hy3
