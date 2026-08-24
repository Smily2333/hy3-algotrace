#include "hy3_algotrace/model_runner.hpp"

#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/prediction_importer.hpp"
#include "hy3_algotrace/sha256.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hy3 {
namespace fs = std::filesystem;
namespace {

using json = nlohmann::json;

std::string utcNow() {
    const std::time_t value = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

bool writeTempJson(const fs::path& finalPath, const json& value,
                   fs::path& tempPath, std::string& error) {
    static std::atomic<unsigned long long> sequence{0};
    const unsigned long long processId =
#ifdef _WIN32
        static_cast<unsigned long long>(GetCurrentProcessId());
#else
        static_cast<unsigned long long>(::getpid());
#endif
    tempPath = finalPath;
    tempPath += ".tmp-" + std::to_string(processId) + "-" +
                std::to_string(++sequence) + "-" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count());
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create temporary sidecar";
        return false;
    }
    output << value.dump(2) << '\n';
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        error = "cannot write temporary sidecar";
        return false;
    }
    output.close();
    return true;
}

bool publishNewSidecar(const fs::path& finalPath, const json& value,
                       std::string& error) {
    fs::path tempPath;
    if (!writeTempJson(finalPath, value, tempPath, error)) {
        return false;
    }
    bool published = false;
#ifdef _WIN32
    published = MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                            MOVEFILE_WRITE_THROUGH) != 0;
#else
    published = ::link(tempPath.c_str(), finalPath.c_str()) == 0;
    ::unlink(tempPath.c_str());
#endif
    if (!published) {
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        error = "sidecar already exists or cannot be published";
    }
    return published;
}

bool replaceSidecar(const fs::path& finalPath, const json& value,
                    std::string& error) {
    fs::path tempPath;
    if (!writeTempJson(finalPath, value, tempPath, error)) {
        return false;
    }
    bool replaced = false;
#ifdef _WIN32
    replaced = MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                           MOVEFILE_REPLACE_EXISTING |
                               MOVEFILE_WRITE_THROUGH) != 0;
#else
    replaced = ::rename(tempPath.c_str(), finalPath.c_str()) == 0;
#endif
    if (!replaced) {
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        error = "cannot atomically replace sidecar";
    }
    return replaced;
}

json nullableString(const std::optional<std::string>& value) {
    return value ? json(*value) : json(nullptr);
}

json nullableInteger(const std::optional<int>& value) {
    return value ? json(*value) : json(nullptr);
}

std::string auditedOutcome(const ModelRunResult& result) {
    switch (result.call_result.status) {
        case ModelCallStatus::Succeeded:
            if (!result.ok) return "postprocess_error";
            return result.call_result.raw_response.empty()
                       ? "empty_response"
                       : "success";
        case ModelCallStatus::AuthenticationError: return "authentication_error";
        case ModelCallStatus::RateLimited: return "rate_limited";
        case ModelCallStatus::Timeout: return "timeout";
        case ModelCallStatus::TransportError: return "transport_error";
        case ModelCallStatus::Cancelled: return "cancelled";
        case ModelCallStatus::ProviderError:
            if (result.call_result.http_status &&
                (*result.call_result.http_status < 200 ||
                 *result.call_result.http_status >= 300)) {
                return "http_error";
            }
            return "provider_error";
        case ModelCallStatus::ConfigurationError: return "configuration_error";
    }
    return "transport_error";
}

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

ModelRunResult runModelForTraceImpl(const std::string& runDir,
                                    const std::string& traceId,
                                    const std::string& runId,
                                    const std::string& generatedAt,
                                    const ModelCallAuditConfig* audit,
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
    if (audit != nullptr) {
        const char* requiredManifestStrings[] = {
            "evaluation_schema_version", "dataset_version", "dataset_commit",
            "taxonomy_version", "pipeline_commit", "prompt_template_id",
            "prompt_template_sha256", "input_mode", "started_at", "notes"};
        for (const char* key : requiredManifestStrings) {
            if (!manifest.doc.contains(key) ||
                !manifest.doc.at(key).is_string() ||
                manifest.doc.at(key).get_ref<const std::string&>().empty()) {
                return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                        std::string("run-manifest field is missing or invalid: ") + key);
            }
        }
        if (!manifest.doc.contains("model_version") ||
            !(manifest.doc.at("model_version").is_null() ||
              (manifest.doc.at("model_version").is_string() &&
               !manifest.doc.at("model_version")
                    .get_ref<const std::string&>().empty())) ||
            !manifest.doc.contains("completed_at") ||
            !manifest.doc.at("completed_at").is_null() ||
            !manifest.doc.contains("trace_ids") ||
            !manifest.doc.at("trace_ids").is_array() ||
            !manifest.doc.contains("total_traces") ||
            !manifest.doc.at("total_traces").is_number_integer()) {
            return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                    "run-manifest typed fields are missing or invalid");
        }
        const json& traceIds = manifest.doc.at("trace_ids");
        bool traceListed = false;
        std::string previousTrace;
        for (const json& item : traceIds) {
            if (!item.is_string()) {
                return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                        "run-manifest trace_ids is invalid");
            }
            const std::string currentTrace = item.get<std::string>();
            if (currentTrace.empty() ||
                (!previousTrace.empty() && currentTrace <= previousTrace)) {
                return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                        "run-manifest trace_ids must be sorted and unique");
            }
            previousTrace = currentTrace;
            traceListed = traceListed || currentTrace == traceId;
        }
        if (!traceListed || manifest.doc.at("total_traces").get<int>() !=
                                static_cast<int>(traceIds.size())) {
            return failBeforeImport(model_runner_errc::E_RUN_CONTEXT,
                                    "run-manifest trace membership is invalid");
        }
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

    fs::path sidecarPath;
    json sidecar;
    bool auditStarted = false;
    if (audit != nullptr) {
        if (audit->schema_version != "0.1.0" ||
            audit->service != "tokenhub" ||
            audit->endpoint_origin != "https://tokenhub.tencentmaas.com" ||
            audit->timeout_seconds == 0) {
            return failBeforeImport(model_runner_errc::E_AUDIT_PRECHECK,
                                    "model call audit configuration is incomplete");
        }
        sidecarPath = fs::path(runDir) / "model-calls" /
                      (traceId + ".json");
        const bool sidecarExists = fs::exists(sidecarPath, pathError);
        if (pathError) {
            return failBeforeImport(model_runner_errc::E_AUDIT_PRECHECK,
                                    "cannot inspect model call sidecar path");
        }
        if (sidecarExists) {
            return failBeforeImport(model_runner_errc::E_AUDIT_PRECHECK,
                                    "model call sidecar already exists");
        }
        if (!fs::create_directories(sidecarPath.parent_path(), pathError) &&
            pathError) {
            return failBeforeImport(model_runner_errc::E_AUDIT_WRITE,
                                    "cannot create model-calls directory");
        }

        sidecar["schema_version"] = audit->schema_version;
        sidecar["run_id"] = runId;
        sidecar["trace_id"] = traceId;
        sidecar["provider"] = manifestProvider;
        sidecar["service"] = audit->service;
        sidecar["model_name"] = manifestModel;
        sidecar["model_version"] =
            manifest.doc.contains("model_version")
                ? manifest.doc.at("model_version")
                : json(nullptr);
        sidecar["endpoint_origin"] = audit->endpoint_origin;
        sidecar["attempted_at"] = utcNow();
        sidecar["completed_at"] = nullptr;
        sidecar["outcome"] = "attempting";
        sidecar["error_category"] = nullptr;
        sidecar["timeout_seconds"] = audit->timeout_seconds;
        sidecar["http_status"] = nullptr;
        sidecar["request_id"] = nullptr;
        sidecar["latency_ms"] = nullptr;
        sidecar["token_usage"] = nullptr;
        sidecar["prompt_sha256"] = promptSha256;
        sidecar["raw_response_sha256"] = nullptr;
        sidecar["response_saved"] = false;
        sidecar["prediction_imported"] = false;
        sidecar["parse_status"] = nullptr;

        std::string auditError;
        if (!publishNewSidecar(sidecarPath, sidecar, auditError)) {
            return failBeforeImport(model_runner_errc::E_AUDIT_WRITE,
                                    auditError);
        }
        auditStarted = true;
    }

    ModelRequest request;
    request.trace_id = traceId;
    request.normalized_prompt = normalizedPrompt;
    request.prompt_sha256 = promptSha256;

    ModelCallResult callResult = invokeModelOnce(request, client);

    const auto finishAudit = [&](ModelRunResult result) {
        if (!auditStarted) {
            return result;
        }
        sidecar["completed_at"] = result.call_result.finished_at.empty()
                                      ? utcNow()
                                      : result.call_result.finished_at;
        sidecar["outcome"] = auditedOutcome(result);
        sidecar["error_category"] = result.ok
                                         ? json(nullptr)
                                         : json(result.call_result.status ==
                                                        ModelCallStatus::Succeeded
                                                    ? "postprocess_error"
                                                    : modelCallStatusName(
                                                          result.call_result.status));
        sidecar["model_version"] = nullableString(result.call_result.model_version);
        sidecar["http_status"] = nullableInteger(result.call_result.http_status);
        sidecar["request_id"] = nullableString(result.call_result.request_id);
        sidecar["latency_ms"] = result.call_result.started_at.empty()
                                     ? json(nullptr)
                                     : json(result.call_result.duration_ms);
        if (result.call_result.token_usage) {
            const ModelTokenUsage& usage = *result.call_result.token_usage;
            sidecar["token_usage"] = {
                {"prompt_tokens", usage.prompt_tokens
                                      ? json(*usage.prompt_tokens)
                                      : json(nullptr)},
                {"completion_tokens", usage.completion_tokens
                                          ? json(*usage.completion_tokens)
                                          : json(nullptr)},
                {"total_tokens", usage.total_tokens
                                     ? json(*usage.total_tokens)
                                     : json(nullptr)}};
        }

        std::error_code artifactError;
        bool responseSaved = fs::exists(rawPath, artifactError) && !artifactError;
        std::vector<std::uint8_t> savedRaw;
        if (responseSaved) {
            std::ifstream input(rawPath, std::ios::binary);
            savedRaw.assign(std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>());
            responseSaved = !input.bad() &&
                            savedRaw == result.call_result.raw_response;
        }
        artifactError.clear();
        bool predictionImported = fs::exists(predictionPath, artifactError) &&
                                  !artifactError;
        sidecar["response_saved"] = responseSaved;
        sidecar["prediction_imported"] = predictionImported;
        if (responseSaved) {
            sidecar["raw_response_sha256"] =
                sha256_hex(savedRaw);
        }
        if (predictionImported) {
            const LoadResult wrapper = loadJsonFile(
                traceId + ".json", (fs::path(runDir) / "predictions").string());
            const std::string rawSha = responseSaved ? sha256_hex(savedRaw) : "";
            predictionImported = wrapper.ok && wrapper.doc.is_object() &&
                wrapper.doc.value("run_id", "") == runId &&
                wrapper.doc.value("trace_id", "") == traceId &&
                wrapper.doc.value("prompt_sha256", "") == promptSha256 &&
                responseSaved && wrapper.doc.value("raw_response_sha256", "") == rawSha;
            sidecar["prediction_imported"] = predictionImported;
            if (predictionImported &&
                wrapper.doc.contains("parse_status") &&
                wrapper.doc.at("parse_status").is_string()) {
                sidecar["parse_status"] = wrapper.doc.at("parse_status");
            }
        }

        std::string auditError;
        if (!replaceSidecar(sidecarPath, sidecar, auditError)) {
            result.ok = false;
            result.error_code = model_runner_errc::E_AUDIT_WRITE;
            result.message = auditError;
        }
        return result;
    };

    if (callResult.status != ModelCallStatus::Succeeded) {
        if (!callResult.raw_response.empty()) {
            return finishAudit(failBeforeImport(
                model_runner_errc::E_MODEL_RESULT_INVALID,
                "failed model call returned raw response bytes",
                std::move(callResult)));
        }

        const std::string adapterCode = callResult.error_code.empty()
                                            ? defaultCallErrorCode(callResult.status)
                                            : callResult.error_code;
        std::string detail = std::string(modelCallStatusName(callResult.status));
        if (!callResult.message.empty()) {
            detail += ": " + callResult.message;
        }
        return finishAudit(
            failBeforeImport(adapterCode, detail, std::move(callResult)));
    }

    if (callResult.provider.empty() || callResult.model_name.empty() ||
        callResult.started_at.empty() || callResult.finished_at.empty()) {
        return finishAudit(failBeforeImport(
            model_runner_errc::E_MODEL_RESULT_INVALID,
            "successful model call missing identity or time metadata",
            std::move(callResult)));
    }
    if (callResult.provider != manifestProvider ||
        callResult.model_name != manifestModel) {
        return finishAudit(failBeforeImport(
            model_runner_errc::E_MODEL_RESULT_INVALID,
            "model call identity does not match run-manifest",
            std::move(callResult)));
    }
    if (manifest.doc.contains("model_version") &&
        manifest.doc.at("model_version").is_string()) {
        const std::string expectedVersion =
            manifest.doc.at("model_version").get<std::string>();
        if (!callResult.model_version ||
            *callResult.model_version != expectedVersion) {
            return finishAudit(failBeforeImport(
                model_runner_errc::E_MODEL_RESULT_INVALID,
                "model version does not match run-manifest",
                std::move(callResult)));
        }
    }

    const ImporterResult importResult =
        importResponseBytes(runDir, traceId, callResult.raw_response, runId,
                            generatedAt);
    if (!importResult.ok) {
        return finishAudit(failBeforeImport(
            model_runner_errc::E_IMPORT_FAILED,
            importResult.error_code + ": " + importResult.message,
            std::move(callResult)));
    }

    ModelRunResult result;
    result.ok = true;
    result.call_result = std::move(callResult);
    return finishAudit(std::move(result));
}

} // namespace

ModelCallResult invokeModelOnce(const ModelRequest& request,
                                IModelClient& client) noexcept {
    try {
        return client.invoke(request);
    } catch (...) {
        // IModelClient is noexcept by contract. Keep a final defensive boundary
        // for a non-conforming future adapter without copying exception text.
        ModelCallResult result;
        result.status = ModelCallStatus::ProviderError;
        result.error_code = model_runner_errc::E_MODEL_PROVIDER;
        result.message = "model client escaped an exception";
        return result;
    }
}

ModelRunResult runModelForTrace(const std::string& runDir,
                                const std::string& traceId,
                                const std::string& runId,
                                const std::string& generatedAt,
                                IModelClient& client) {
    return runModelForTraceImpl(runDir, traceId, runId, generatedAt, nullptr,
                                client);
}

ModelRunResult runRecordedModelForTrace(const std::string& runDir,
                                        const std::string& traceId,
                                        const std::string& runId,
                                        const std::string& generatedAt,
                                        const ModelCallAuditConfig& audit,
                                        IModelClient& client) {
    return runModelForTraceImpl(runDir, traceId, runId, generatedAt, &audit,
                                client);
}

} // namespace hy3
