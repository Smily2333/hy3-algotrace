// hy3_algotrace — JsonLoader
//
// Reads a UTF-8 JSON file from disk and parses it. It performs NO business
// rules: on success it returns the parsed document; on failure it returns a
// structured result carrying a stable error code (E_FILE_READ / E_JSON_PARSE)
// and the underlying error text (which is never swallowed). The caller turns
// that into a Diagnostic.

#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "hy3_algotrace/diagnostic.hpp"

namespace hy3 {

struct LoadResult {
    bool ok = false;
    nlohmann::json doc;          // valid only when ok == true
    std::string error_code;      // errc::E_FILE_READ or errc::E_JSON_PARSE
    std::string error_message;  // human-readable, includes the underlying cause
};

// Load `relativePath` resolved against `baseDir`.
//   full = baseDir.empty() ? relativePath : baseDir + "/" + relativePath
// On any error `ok` is false and `error_code`/`error_message` are populated.
LoadResult loadJsonFile(const std::string& relativePath, const std::string& baseDir);

} // namespace hy3
