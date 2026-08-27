#pragma once
#include "hy3_algotrace/interactive_diagnosis.hpp"
#include <filesystem>
namespace hy3::evaluation {
using json = nlohmann::json;
inline constexpr const char* version = "greedy-evaluation-v1";
inline constexpr const char* version2 = "greedy-evaluation-v2";
json load(const std::filesystem::path&);
void saveNew(const std::filesystem::path&, const json&);
void validateDataset(const json&);
InteractiveDiagnosisRequest requestFor(const json&, const std::string& sampleId);
std::string render(const InteractiveDiagnosisRequest&, const std::string& v2Template,
                   const std::string& extension);
std::string renderV2(const InteractiveDiagnosisRequest&, const std::string& standaloneTemplate);
json parse(const std::string& raw, const InteractiveDiagnosisRequest&, const std::string& expectedVersion = version);
json report(const json& dataset, const json& records, bool synthetic);
json attachAnswerEvidence(const json& dataset, const json& records, const json& evidence);
std::string normalizeOutput(std::string);
json compareOutput(const std::string& actual, const std::string& expected);
// Append-only reservation/completion files; a stale lock fails closed.
// An unanswered reservation is never automatically repeated or released.
class Budget {
public:
    explicit Budget(std::filesystem::path root);
    json summary() const;
    void reserve(const std::string& id, std::uint64_t upper);
    void reconcile(const std::string& id, const std::optional<ModelTokenUsage>&);
private:
    std::filesystem::path root_;
};
}
