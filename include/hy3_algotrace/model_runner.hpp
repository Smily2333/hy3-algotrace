// hy3-algotrace — one-trace model orchestration (Phase 2C)
//
// ModelRunner joins a saved prompt, an injected IModelClient, and the strict
// PredictionImporter. It never reads gold labels and never turns a failed
// transport call into model_call_not_attempted.

#pragma once

#include <cstdint>
#include <string>

#include "hy3_algotrace/model_client.hpp"

namespace hy3 {

namespace model_runner_errc {
    inline constexpr const char* E_PROMPT_LOAD = "E_MODEL_PROMPT_LOAD";
    inline constexpr const char* E_RUN_CONTEXT = "E_MODEL_RUN_CONTEXT";
    inline constexpr const char* E_IMPORT_PRECHECK = "E_MODEL_IMPORT_PRECHECK";
    inline constexpr const char* E_MODEL_CONFIGURATION = "E_MODEL_CONFIGURATION";
    inline constexpr const char* E_MODEL_AUTHENTICATION = "E_MODEL_AUTHENTICATION";
    inline constexpr const char* E_MODEL_RATE_LIMITED = "E_MODEL_RATE_LIMITED";
    inline constexpr const char* E_MODEL_TIMEOUT = "E_MODEL_TIMEOUT";
    inline constexpr const char* E_MODEL_TRANSPORT = "E_MODEL_TRANSPORT";
    inline constexpr const char* E_MODEL_PROVIDER = "E_MODEL_PROVIDER";
    inline constexpr const char* E_MODEL_CANCELLED = "E_MODEL_CANCELLED";
    inline constexpr const char* E_MODEL_RESULT_INVALID = "E_MODEL_RESULT_INVALID";
    inline constexpr const char* E_IMPORT_FAILED = "E_MODEL_IMPORT_FAILED";
    inline constexpr const char* E_AUDIT_PRECHECK = "E_MODEL_AUDIT_PRECHECK";
    inline constexpr const char* E_AUDIT_WRITE = "E_MODEL_AUDIT_WRITE";
} // namespace model_runner_errc

struct ModelRunResult {
    bool ok = false;
    std::string error_code;
    std::string message;
    ModelCallResult call_result;
};

struct ModelCallAuditConfig {
    std::string schema_version = "0.1.0";
    std::string service;
    std::string endpoint_origin;
    std::uint64_t timeout_seconds = 0;
};

// Loads and hashes <runDir>/prompts/<traceId>.txt, sends exactly that
// normalized prompt to `client`, and on transport success passes the returned
// bytes unchanged to PredictionImporter::importResponseBytes.
//
// A transport failure returns ok=false and does not call the importer, write
// any file, or mark the trace as not attempted. A successful zero-byte model
// response is imported normally and becomes parse_status=empty_response.
ModelRunResult runModelForTrace(const std::string& runDir,
                                const std::string& traceId,
                                const std::string& runId,
                                const std::string& generatedAt,
                                IModelClient& client);

// Production/audited variant. Before client.invoke() it atomically creates
// model-calls/<traceId>.json with outcome=attempting. Any pre-existing sidecar
// refuses the call, including a stranded attempted record from a prior crash.
// Completion atomically replaces the sidecar with safe transport/import data.
ModelRunResult runRecordedModelForTrace(const std::string& runDir,
                                        const std::string& traceId,
                                        const std::string& runId,
                                        const std::string& generatedAt,
                                        const ModelCallAuditConfig& audit,
                                        IModelClient& client);

} // namespace hy3
