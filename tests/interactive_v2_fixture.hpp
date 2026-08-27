// SYNTHETIC_TEST_FIXTURE: scripted responses, NOT evidence of Hy3 quality.
#pragma once

#include "hy3_algotrace/interactive_diagnosis.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace interactive_fixture {
using json = nlohmann::json;
namespace fs = std::filesystem;

inline std::string readText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("fixture file unavailable");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct OwnedRoot {
    fs::path parent = fs::canonical(fs::temp_directory_path());
    fs::path path;
    OwnedRoot() {
        static unsigned sequence = 0;
        path = parent / ("hy3_m1_owned_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(++sequence));
        if (!fs::create_directory(path)) throw std::runtime_error("fixture root collision");
    }
    ~OwnedRoot() {
        std::error_code ec;
        const auto resolved = fs::canonical(path, ec);
        if (!ec && resolved.parent_path() == parent && resolved.filename() == path.filename())
            fs::remove_all(path, ec); // Only the unique directory this instance created.
    }
    OwnedRoot(const OwnedRoot&) = delete;
    OwnedRoot& operator=(const OwnedRoot&) = delete;
};

inline std::string code(bool correct = false) {
    return std::string(
        "#include <algorithm>\n#include <iostream>\n#include <vector>\n"
        "using namespace std;\n\nint main() {\n    int n; cin >> n;\n"
        "    vector<int> coins(n);\n    int total = 0;\n"
        "    for (int &x : coins) { cin >> x; total += x; }\n"
        "    sort(coins.rbegin(), coins.rend());\n"
        "    int taken = 0, count = 0;\n    for (int x : coins) {\n"
        "        taken += x;\n        ++count;\n        if (taken * 2 ") +
        (correct ? ">" : ">=") + " total) break;\n    }\n"
        "    cout << count << '\\n';\n}\n";
}

inline json request(const std::string& id, bool correct = false) {
    return {{"schema_version", hy3::kInteractiveRequestVersion}, {"request_id", id},
            {"algorithm_type", "greedy"},
            {"problem_statement", "拿走尽可能少的硬币，使拿走的总价值严格大于剩余价值。\n输入：n 和 n 个面值。输出最少硬币数。\n约束：1 <= n <= 100，1 <= a[i] <= 100。"},
            {"cpp_solution", code(correct)}};
}

inline json location(const std::string& source, int first, int last) {
    std::size_t begin = 0;
    for (int line = 1; line < first; ++line) begin = source.find('\n', begin) + 1;
    auto end = begin;
    for (int line = first; line <= last; ++line) {
        end = source.find('\n', end);
        if (end == std::string::npos) { end = source.size(); break; }
        if (line < last) ++end;
    }
    return {{"start_line", first}, {"end_line", last}, {"snippet", source.substr(begin, end - begin)}};
}

inline json diagnosis(const std::string& id, const std::string& status = "incorrect") {
    const auto source = code(status == "correct");
    const bool bad = status == "incorrect";
    const bool unknown = status == "undetermined";
    json d = {
        {"schema_version", hy3::kInteractiveResponseVersion}, {"request_id", id},
        {"status", status},
        {"summary", unknown ? "题面信息不足，无法判断。" : bad ? "相等边界会提前停止。" : "静态审查未发现明确错误。"},
        {"limitations", json::array({"Fake 预设响应，仅用于程序验收；未执行代码，不代表 Hy3 效果。"})},
        {"algorithm_overview", {{"origin", "model_code_interpretation"}, {"summary", "将硬币降序排列，累计选择并检查停止条件。"}}},
        {"steps", json::array({
            {{"id", "s1"}, {"summary", "读取硬币并计算总和"}, {"code_location", location(source, 7, 10)}},
            {{"id", "s2"}, {"summary", "从大到小排序"}, {"code_location", location(source, 11, 11)}},
            {{"id", "s3"}, {"summary", "累加硬币并判断是否停止"}, {"code_location", location(source, 12, 17)}},
            {{"id", "s4"}, {"summary", "输出选择数量"}, {"code_location", location(source, 18, 18)}}})},
        {"first_error", {{"step_id", bad ? json("s3") : json(nullptr)},
                          {"explanation", bad ? "排序之后，停止条件第一次允许了不符合题意的相等情况。" : "没有可靠的错误步骤可定位。"}}},
        {"primary_category", bad ? json("boundary_omission") : json(nullptr)},
        {"findings", json::array()},
        {"counterexample", {{"availability", "unavailable"}, {"input", nullptr},
            {"expected_output", nullptr}, {"predicted_candidate_output", nullptr},
            {"candidate_output_basis", nullptr}, {"explanation", "没有可靠反例。"},
            {"provenance", "model_proposed_not_executed"}}},
        {"reference_solution", {{"availability", "provided"},
            {"strategy", "降序排列硬币。维护总和与已取总和，依次取最大值，严格超过剩余总和时输出数量。"},
            {"correctness", "对任意数量 k，最大的 k 枚硬币总和不小于任何其他 k 枚；若它们仍未严格超过一半，任何 k 枚都不可行。首次可行的前缀因此数量最少。"},
            {"complexity", "排序 O(n log n)，遍历 O(n)，存储 O(n)。"},
            {"boundaries", "恰好一半不能停止；检查单枚硬币、相等面值及最大总和；给定范围内 int 足够。"},
            {"unavailable_reason", nullptr}, {"provenance", "model_generated_unverified"}}}
    };
    if (bad) {
        d["findings"].push_back({{"id", "f1"}, {"step_id", "s3"},
            {"category", "boundary_omission"}, {"reason", "代码在所取与剩余价值相等时停止，但题意要求严格大于。"},
            {"input_evidence", {{"source", "problem_statement"}, {"excerpt", "严格大于"}}},
            {"code_location", location(source, 16, 16)}, {"location_reason", nullptr},
            {"suggestion", "停止条件使用 taken * 2 > total。"}});
        d["counterexample"] = {{"availability", "provided"}, {"input", "2\n5 5\n"},
            {"expected_output", "2\n"}, {"predicted_candidate_output", "1\n"},
            {"candidate_output_basis", "static_inference"},
            {"explanation", "取一枚后两边均为 5，不满足严格大于。候选输出仅为静态推断。"},
            {"provenance", "model_proposed_not_executed"}};
    }
    if (unknown) {
        d["steps"] = json::array();
        d["algorithm_overview"]["summary"] = "可看出排序与累计，但缺少判定目标。";
        d["reference_solution"] = {{"availability", "unavailable"},
            {"strategy", nullptr}, {"correctness", nullptr}, {"complexity", nullptr},
            {"boundaries", nullptr}, {"unavailable_reason", "题意不足，不能编造解法。"},
            {"provenance", "model_generated_unverified"}};
    }
    return d;
}

inline hy3::ModelCallResult success(const json& d) {
    hy3::ModelCallResult call;
    call.status = hy3::ModelCallStatus::Succeeded;
    const auto raw = d.dump();
    call.raw_response.assign(raw.begin(), raw.end());
    call.provider = "synthetic-test";
    call.model_name = "fake-model";
    call.model_version = "fixture-v2";
    call.http_status = 200;
    call.duration_ms = 0;
    return call;
}
} // namespace interactive_fixture
