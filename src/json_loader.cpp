// hy3_algotrace — JsonLoader implementation.

#include "hy3_algotrace/json_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace hy3 {
namespace fs = std::filesystem;

LoadResult loadJsonFile(const std::string& relativePath, const std::string& baseDir) {
    LoadResult res;
    const bool absolute = fs::path(relativePath).is_absolute();
    const std::string full =
        (baseDir.empty() || absolute) ? relativePath : (baseDir + "/" + relativePath);

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        res.error_code = errc::E_FILE_READ;
        res.error_message = "cannot open file: " + full;
        return res;
    }

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (in.bad()) {
        res.error_code = errc::E_FILE_READ;
        res.error_message = "error while reading file: " + full;
        return res;
    }
    // A completely empty file is not valid JSON.
    if (content.empty()) {
        res.error_code = errc::E_JSON_PARSE;
        res.error_message = "file is empty (not valid JSON): " + full;
        return res;
    }

    try {
        res.doc = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        res.error_code = errc::E_JSON_PARSE;
        std::ostringstream oss;
        oss << "JSON parse error at byte " << e.byte << " in " << full << ": "
            << e.what();
        res.error_message = oss.str();
        return res;
    } catch (const std::exception& e) {
        res.error_code = errc::E_FILE_READ;
        res.error_message = std::string("error processing file ") + full + ": " + e.what();
        return res;
    }

    res.ok = true;
    return res;
}

} // namespace hy3
