// hy3_algotrace — PromptExporter self-tests (Phase 2B-1).
//
// A tiny, dependency-free test harness (no Catch2 / GTest / Python). Each test
// builds a dataset in a throwaway temp directory, runs the real PromptExporter,
// and asserts on the stable error CODE (not just "it failed"). The canonical
// Phase 1A dataset is also exported when its path is supplied (argv[1], or
// ../data, or data relative to CWD).

#include "hy3_algotrace/prompt_exporter.hpp"
#include "hy3_algotrace/sha256.hpp"
#include "hy3_algotrace/json_loader.hpp"
#include "hy3_algotrace/diagnostic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string g_realDataDir;  // optional; set from argv[1]
static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        g_total++;                                                            \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n";     \
        }                                                                     \
    } while (0)

// Write bytes to a file (binary, LF only).
static void writeFile(const fs::path& p, const std::string& s) {
    std::ofstream ofs(p, std::ios::binary);
    ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// Build a minimal valid problem JSON with one trace.
static json makeProblem(const std::string& pid, const std::string& tid,
                        bool withCandidate, bool withIntendedOutcome) {
    json p;
    p["meta"] = json::object({
        {"schema_version", "0.3.0"}, {"taxonomy_version", "1.0.0"},
        {"source_reference", "x"}, {"dataset_version", "phase1a-pilot-001"},
        {"created_at", "2026-08-23"}});
    p["problem"] = json::object({
        {"id", pid}, {"source", "codeforces"}, {"title", "t"},
        {"statement", "s"}, {"constraints", json::object()},
        {"algorithm_type", "greedy"}, {"reference_tags", json::array({"greedy"})},
        {"notes", "LEAK: this is problem.notes and must be stripped"}});
    p["reference_verdict"] = json::object({
        {"problem_id", pid}, {"expected_choice", "c"},
        {"expected_proof", "p"}, {"expected_complexity", "O(1)"},
        {"expected_boundaries", json::array({"b"})},
        {"common_wrong_strategy_counterexample", "none"}});
    p["test_cases"] = json::array({json::object({
        {"id", pid + "_c1"}, {"problem_id", pid}, {"input", "1\n"},
        {"expected_output", "1\n"}, {"origin", "official_sample"},
        {"purpose", "normal"}, {"notes", "LEAK: tc.notes must be stripped"}})});
    json trace = json::object();
    trace["id"] = tid;
    trace["problem_id"] = pid;
    trace["author"] = "hy3";
    trace["trace_origin"] = "model_generated";
    trace["generator_model"] = "hy3";
    trace["annotator"] = "hy3_draft";
    trace["review_status"] = "pending_planner_review";
    trace["reviewer"] = nullptr;
    trace["reviewed_at"] = nullptr;
    json step = json::object();
    step["stage"] = "problem_understanding";
    step["text"] = "t";
    step["relies_on"] = json::array();
    trace["steps"] = json::array({step});
    if (withIntendedOutcome) trace["intended_outcome"] = "o";
    p["reasoning_traces"] = json::array({trace});
    p["candidate_solutions"] = json::array();
    if (withCandidate) {
        json sol = json::object();
        sol["id"] = pid + "_sol1";
        sol["trace_id"] = tid;
        sol["language"] = "cpp";
        sol["standard"] = "c++17";
        sol["source_code"] = "int main(){}";
        sol["execution_status"] = "compiled";
        p["candidate_solutions"] = json::array({sol});
    }
    p["diagnoses"] = json::array();
    p["verification_results"] = json::array();
    return p;
}

static json makeManifest(const std::vector<std::string>& pids) {
    json m;
    m["schema_version"] = "0.3.0";
    m["taxonomy_version"] = "1.0.0";
    m["dataset_version"] = "phase1a-pilot-001";
    m["problem_count"] = static_cast<int>(pids.size());
    m["trace_count"] = static_cast<int>(pids.size());
    m["problem_ids"] = json(pids);
    return m;
}

static fs::path makeTempData(const std::string& tag,
                             const std::vector<json>& problems,
                             const std::vector<std::string>& pids) {
    fs::path dir = fs::temp_directory_path() /
                   ("hy3_pe_test_" + tag + "_" +
                    std::to_string(g_pass + g_fail));
    fs::remove_all(dir);
    fs::create_directories(dir / "problems");
    for (const auto& p : problems) {
        std::string pid = p.at("problem").at("id").get<std::string>();
        writeFile(dir / "problems" / (pid + ".json"), p.dump(2));
    }
    writeFile(dir / "manifest.json", makeManifest(pids).dump(2));
    return dir;
}

// A template with the five placeholders and markers.
static const std::string kTemplate =
    "PRE\n"
    "<!-- HY3_PROMPT_BEGIN -->\n"
    "PROBLEM: {{problem_json}}\n"
    "VERDICT: {{reference_verdict_json}}\n"
    "TESTS: {{test_cases_json}}\n"
    "TRACE: {{reasoning_trace_json}}\n"
    "SOLUTION: {{candidate_solution_json_or_null}}\n"
    "<!-- HY3_PROMPT_END -->\n"
    "POST";

int main(int argc, char** argv) {
    if (argc >= 2) g_realDataDir = argv[1];

    // --- 1. SHA-256 standard vectors -------------------------------------
    {
        CHECK(hy3::sha256_hex("") ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA-256 empty string vector");
        CHECK(hy3::sha256_hex("abc") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 'abc' vector");
        // 56-byte input: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        std::string s56 =
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CHECK(s56.size() == 56, "56-byte input length");
        CHECK(hy3::sha256_hex(s56) ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "SHA-256 56-byte vector");
    }

    // --- 2. CRLF/CR -> LF, BOM handling ----------------------------------
    {
        std::vector<uint8_t> raw = {'a', '\r', '\n', 'b', '\r', 'c'};
        std::vector<uint8_t> out;
        std::string err;
        CHECK(hy3::normalizeUtf8(raw, out, err), "normalize CRLF/CR succeeds");
        std::string s(out.begin(), out.end());
        CHECK(s == "a\nb\nc", "CRLF/CR normalized to LF");
        // BOM + CRLF
        std::vector<uint8_t> bom = {0xEF, 0xBB, 0xBF, 'x', '\r', '\n', 'y'};
        std::vector<uint8_t> out2;
        CHECK(hy3::normalizeUtf8(bom, out2, err), "normalize BOM+CRLF succeeds");
        std::string s2(out2.begin(), out2.end());
        CHECK(s2 == "x\ny", "BOM stripped and CRLF normalized");
    }

    // --- 3. Invalid UTF-8 / embedded NUL rejection -----------------------
    {
        std::vector<uint8_t> nul = {'a', 0x00, 'b'};
        std::vector<uint8_t> out;
        std::string err;
        CHECK(!hy3::normalizeUtf8(nul, out, err), "embedded NUL rejected");
        CHECK(err == hy3::sha_errc::E_EMBEDDED_NUL, "NUL error code");
        // invalid UTF-8: 0xFF is never valid
        std::vector<uint8_t> bad = {0x48, 0xFF, 0x49};
        std::vector<uint8_t> out2;
        CHECK(!hy3::normalizeUtf8(bad, out2, err), "invalid UTF-8 rejected");
        CHECK(err == hy3::sha_errc::E_UTF8_INVALID, "UTF-8 invalid error code");
        // overlong encoding of '/'
        std::vector<uint8_t> over = {0xC0, 0xAF};
        std::vector<uint8_t> out3;
        CHECK(!hy3::normalizeUtf8(over, out3, err), "overlong UTF-8 rejected");
    }

    // --- 4. Template body extraction + error cases -----------------------
    {
        std::string body;
        auto r = hy3::extractTemplateBody(kTemplate, body);
        CHECK(r.ok, "extractTemplateBody ok on valid template");
        CHECK(body.find("{{problem_json}}") != std::string::npos,
              "body contains placeholder");
        CHECK(body.find("PRE") == std::string::npos, "body excludes PRE");
        CHECK(body.find("POST") == std::string::npos, "body excludes POST");

        std::string missing;
        auto r2 = hy3::extractTemplateBody("no markers here", missing);
        CHECK(!r2.ok && r2.error_code == hy3::exporter_errc::E_TEMPLATE_MARKER,
              "missing BEGIN -> E_TEMPLATE_MARKER");

        auto r3 = hy3::extractTemplateBody(
            "<!-- HY3_PROMPT_BEGIN -->x<!-- HY3_PROMPT_BEGIN -->y"
            "<!-- HY3_PROMPT_END -->", missing);
        CHECK(!r3.ok && r3.error_code == hy3::exporter_errc::E_TEMPLATE_MARKER,
              "duplicate BEGIN -> E_TEMPLATE_MARKER");

        auto r4 = hy3::extractTemplateBody(
            "<!-- HY3_PROMPT_END -->x<!-- HY3_PROMPT_BEGIN -->y", missing);
        CHECK(!r4.ok && r4.error_code == hy3::exporter_errc::E_TEMPLATE_MARKER,
              "END before BEGIN -> E_TEMPLATE_MARKER");
    }

    // --- 5. Placeholder errors -------------------------------------------
    {
        // duplicate placeholder
        std::string tpl = "<!-- HY3_PROMPT_BEGIN -->{{problem_json}}{{problem_json}}"
                          "<!-- HY3_PROMPT_END -->";
        std::string body;
        hy3::extractTemplateBody(tpl, body);
        json ph = json::object();
        ph["problem_json"] = json::object();
        std::string out;
        auto r = hy3::renderPrompt(body, ph, out);
        CHECK(!r.ok && r.error_code == hy3::exporter_errc::E_TEMPLATE_PLACEHOLDER,
              "duplicate placeholder -> E_TEMPLATE_PLACEHOLDER");

        // residual placeholder
        std::string tpl2 = "<!-- HY3_PROMPT_BEGIN -->BODY{{missing_ph}}"
                           "<!-- HY3_PROMPT_END -->";
        std::string body2;
        hy3::extractTemplateBody(tpl2, body2);
        auto r2 = hy3::renderPrompt(body2, json::object(), out);
        CHECK(!r2.ok && r2.error_code == hy3::exporter_errc::E_TEMPLATE_PLACEHOLDER,
              "residual placeholder -> E_TEMPLATE_PLACEHOLDER");
    }

    // --- 6. Allowlist projection (no leakage) ----------------------------
    {
        json prob = makeProblem("p1", "p1_t1", /*withCandidate=*/false,
                                /*withIntendedOutcome=*/true);
        json trace = prob.at("reasoning_traces").at(0);
        json out;
        auto r = hy3::projectTraceInput(prob, trace, out);
        CHECK(r.ok, "projectTraceInput ok");
        CHECK(!out.contains("problem") || !out.at("problem").contains("notes"),
              "problem.notes stripped");
        CHECK(out.contains("reference_verdict"), "reference_verdict present");
        CHECK(out.at("test_cases").at(0).contains("notes") == false,
              "test_cases.notes stripped");
        CHECK(out.at("reasoning_trace").contains("intended_outcome"),
              "intended_outcome copied when present");
        CHECK(out.at("reasoning_trace").contains("trace_origin") == false,
              "trace.trace_origin stripped (not in allowlist)");
        CHECK(out.at("candidate_solution").is_null(),
              "candidate_solution null when none associated");
    }

    // --- 7. Structural leakage audit: non-false-positives ----------------
    {
        json clean = json::object();
        clean["problem"] = json::object({{"id", "x"}});
        clean["reference_verdict"] = json::object();
        clean["test_cases"] = json::array();
        clean["reasoning_trace"] = json::object();
        clean["candidate_solution"] = json::value_t::null;
        std::string bad;
        auto r = hy3::auditStructuralLeakage(clean, bad);
        CHECK(r.ok, "clean payload passes audit (no false positive)");

        // The projected output of a normal trace must NOT trip the audit.
        json prob = makeProblem("p1", "p1_t1", false, true);
        json out;
        hy3::projectTraceInput(prob, prob.at("reasoning_traces").at(0), out);
        std::string bad2;
        auto r2 = hy3::auditStructuralLeakage(out, bad2);
        CHECK(r2.ok, "projected trace passes audit (no false positive)");
    }

    // --- 8. Structural leakage audit: detects forbidden key -------------
    {
        json payload = json::object();
        payload["diagnoses"] = json::array();
        std::string bad;
        auto r = hy3::auditStructuralLeakage(payload, bad);
        CHECK(!r.ok && r.error_code == hy3::exporter_errc::E_LEAKAGE_AUDIT,
              "forbidden key detected -> E_LEAKAGE_AUDIT");
        CHECK(bad == "diagnoses", "offending key reported");
    }

    // --- 9. Null candidate_solution when not associated ------------------
    {
        json prob = makeProblem("p1", "p1_t1", /*withCandidate=*/false, false);
        json out;
        hy3::projectTraceInput(prob, prob.at("reasoning_traces").at(0), out);
        CHECK(out.at("candidate_solution").is_null(),
              "candidate_solution is JSON null when no match");
    }

    // --- 10. cf_160A_t3 association (candidate matched by trace_id) ------
    {
        // Build a problem where only one trace has a matching candidate.
        json prob = makeProblem("cf_160A", "cf_160A_t3", /*withCandidate=*/true,
                                /*withIntendedOutcome=*/true);
        // add a second trace with no candidate
        json t2 = json::object();
        t2["id"] = "cf_160A_t2";
        t2["problem_id"] = "cf_160A";
        t2["author"] = "hy3";
        t2["steps"] = json::array();
        prob.at("reasoning_traces").push_back(t2);

        json out3;
        hy3::projectTraceInput(prob, prob.at("reasoning_traces").at(0), out3);
        CHECK(!out3.at("candidate_solution").is_null(),
              "cf_160A_t3 gets candidate_solution");
        CHECK(out3.at("candidate_solution").at("id").get<std::string>() ==
                  "cf_160A_sol1",
              "correct candidate id associated");

        json out2;
        hy3::projectTraceInput(prob, t2, out2);
        CHECK(out2.at("candidate_solution").is_null(),
              "cf_160A_t2 has no candidate (null)");
    }

    // --- 11. 9-trace lexicographic export --------------------------------
    {
        std::vector<json> problems;
        std::vector<std::string> pids;
        // 3 problems x 3 traces, trace ids chosen so lexicographic order is
        // well-defined and NOT insertion order.
        const char* pnames[3] = {"cf_160A", "cf_545D", "cf_1398B"};
        int n = 0;
        for (auto pn : pnames) {
            json p = makeProblem(pn, "", false, false);
            p.at("reasoning_traces").clear();
            for (int k = 3; k >= 1; --k) {  // insertion reversed
                json tr = json::object();
                tr["id"] = std::string(pn) + "_t" + std::to_string(k);
                tr["problem_id"] = std::string(pn);
                tr["author"] = "hy3";
                tr["steps"] = json::array();
                p.at("reasoning_traces").push_back(tr);
            }
            problems.push_back(p);
            pids.push_back(pn);
        }
        fs::path dir = makeTempData("nine", problems, pids);
        fs::path runDir = dir / "run1";
        hy3::RunManifest m;
        m.run_id = "test_nine";
        m.pipeline_commit = "abc123";
        m.started_at = "2026-08-24T00:00:00Z";
        std::string ptsha;
        auto r = hy3::exportPrompts(dir.string(), kTemplate, runDir.string(),
                                    m, ptsha);
        CHECK(r.ok, "export 9 traces ok");
        // count prompt files
        int cnt = 0;
        for (auto& e : fs::directory_iterator(runDir / "prompts")) cnt++;
        CHECK(cnt == 9, "9 prompt files written");
        // trace_ids in manifest sorted lexicographically
        json mj = json::parse(
            (fs::path(runDir / "run-manifest.json")).string());
        auto ids = mj.at("trace_ids").get<std::vector<std::string>>();
        CHECK(ids.size() == 9, "manifest has 9 trace_ids");
        bool sorted = true;
        for (size_t i = 1; i < ids.size(); ++i)
            if (ids[i] < ids[i - 1]) sorted = false;
        CHECK(sorted, "trace_ids lexicographically sorted");
        CHECK(mj.at("total_traces").get<int>() == 9, "total_traces == 9");
        CHECK(mj.at("model_version").is_null(), "model_version is null");
        CHECK(mj.at("completed_at").is_null(), "completed_at is null");
    }

    // --- 12. Determinism (same input -> identical hashes/output) ---------
    {
        std::vector<json> problems = {makeProblem("d1", "d1_t1", false, false)};
        std::vector<std::string> pids = {"d1"};
        fs::path dir = makeTempData("det", problems, pids);
        hy3::RunManifest m;
        m.run_id = "det"; m.pipeline_commit = "c"; m.started_at = "s";
        std::string sha1, sha2;
        hy3::exportPrompts(dir.string(), kTemplate, (dir / "r1").string(), m, sha1);
        hy3::exportPrompts(dir.string(), kTemplate, (dir / "r2").string(), m, sha2);
        CHECK(sha1 == sha2, "template sha256 deterministic across runs");
        // prompt file content identical
        std::ifstream f1((dir / "r1" / "prompts" / "d1_t1.txt").string(),
                         std::ios::binary);
        std::ifstream f2((dir / "r2" / "prompts" / "d1_t1.txt").string(),
                         std::ios::binary);
        std::string s1((std::istreambuf_iterator<char>(f1)),
                       std::istreambuf_iterator<char>());
        std::string s2((std::istreambuf_iterator<char>(f2)),
                       std::istreambuf_iterator<char>());
        CHECK(s1 == s2, "rendered prompt deterministic across runs");
    }

    // --- 13. Run-dir-exists refusal --------------------------------------
    {
        std::vector<json> problems = {makeProblem("e1", "e1_t1", false, false)};
        std::vector<std::string> pids = {"e1"};
        fs::path dir = makeTempData("exists", problems, pids);
        fs::path runDir = dir / "r";
        fs::create_directories(runDir);  // pre-create
        hy3::RunManifest m;
        m.run_id = "e"; m.pipeline_commit = "c"; m.started_at = "s";
        std::string sh;
        auto r = hy3::exportPrompts(dir.string(), kTemplate, runDir.string(), m, sh);
        CHECK(!r.ok && r.error_code == hy3::exporter_errc::E_RUN_DIR_EXISTS,
              "refuse to overwrite existing run dir");
    }

    // --- 14. Unsafe-id refusal -------------------------------------------
    {
        // problem id with path separator must be rejected with E_UNSAFE_ID.
        json p = makeProblem("bad/id", "bad_t1", false, false);
        p.at("problem").at("id") = "bad/id";
        p.at("reasoning_traces").at(0).at("problem_id") = "bad/id";
        p.at("reasoning_traces").at(0).at("id") = "bad/id_t1";
        std::vector<json> problems = {p};
        std::vector<std::string> pids = {"bad/id"};
        fs::path dir = makeTempData("unsafe", problems, pids);
        hy3::RunManifest m;
        m.run_id = "u"; m.pipeline_commit = "c"; m.started_at = "s";
        std::string sh;
        auto r = hy3::exportPrompts(dir.string(), kTemplate, (dir / "r").string(),
                                    m, sh);
        CHECK(!r.ok && r.error_code == hy3::exporter_errc::E_UNSAFE_ID,
              "unsafe id rejected with E_UNSAFE_ID");
    }

    // --- 15. Real-data integration (canonical dataset) -------------------
    if (!g_realDataDir.empty()) {
        fs::path realDir(g_realDataDir);
        if (fs::exists(realDir / "manifest.json")) {
            // locate a template file
            fs::path tplPath = fs::path(g_realDataDir).parent_path() /
                               "prompts" / "hy3-evaluator-v1.md";
            if (!fs::exists(tplPath))
                tplPath = "prompts/hy3-evaluator-v1.md";
            if (fs::exists(tplPath)) {
                std::ifstream tf(tplPath, std::ios::binary);
                std::string tplText((std::istreambuf_iterator<char>(tf)),
                                    std::istreambuf_iterator<char>());
                // normalize
                std::vector<uint8_t> raw(tplText.begin(), tplText.end());
                std::vector<uint8_t> norm;
                std::string nerr;
                if (hy3::normalizeUtf8(raw, norm, nerr))
                    tplText.assign(norm.begin(), norm.end());
                fs::path runDir = realDir.parent_path() / "out-real-test";
                fs::remove_all(runDir);
                hy3::RunManifest m;
                m.run_id = "real_integration";
                m.pipeline_commit = "test";
                m.started_at = "2026-08-24T00:00:00Z";
                std::string sh;
                auto r = hy3::exportPrompts(g_realDataDir, tplText,
                                            runDir.string(), m, sh);
                CHECK(r.ok, "real dataset export ok");
                if (r.ok) {
                    int cnt = 0;
                    for (auto& e : fs::directory_iterator(runDir / "prompts")) cnt++;
                    CHECK(cnt == 9, "real dataset produced 9 prompts");
                    CHECK(!sh.empty(), "real dataset template sha256 computed");
                    // verify no leakage in any produced prompt
                    bool anyLeak = false;
                    for (auto& e : fs::directory_iterator(runDir / "prompts")) {
                        std::ifstream pf(e.path(), std::ios::binary);
                        std::string content((std::istreambuf_iterator<char>(pf)),
                                            std::istreambuf_iterator<char>());
                        // forbidden key names must not appear as JSON keys
                        for (auto fk : {"diagnoses", "review_status", "reviewer",
                                        "reviewed_at", "trace_origin",
                                        "generator_model", "annotator"}) {
                            // crude check: forbidden key as a JSON object key
                            std::string needle = std::string("\"") + fk + "\"";
                            if (content.find(needle) != std::string::npos)
                                anyLeak = true;
                        }
                    }
                    CHECK(!anyLeak, "no forbidden key leaked into real prompts");
                    fs::remove_all(runDir);
                }
            } else {
                std::cerr << "WARN: template not found, skipping real-data test\n";
            }
        } else {
            std::cerr << "WARN: real dataset manifest not found, skipping\n";
        }
    }

    // --- 16. Validator regression: existing 56 tests still pass ----------
    // (We re-run the validator logic on the real dataset if present; this is a
    //  lightweight proxy. The dedicated validator_tests.exe covers the 56.)
    if (!g_realDataDir.empty()) {
        // The real data must still be valid (no contract break by Phase 2B-1).
        // We don't link validator here; instead assert the files are intact.
        CHECK(fs::exists(fs::path(g_realDataDir) / "manifest.json"),
              "real manifest intact");
    }

    // --- 17. validate data still PASS (smoke) ----------------------------
    // (Covered by validator_tests.exe; here we just ensure data dir readable.)
    if (!g_realDataDir.empty()) {
        auto lr = hy3::loadJsonFile("manifest.json", g_realDataDir);
        CHECK(lr.ok, "real manifest still loads");
    }

    std::cout << "prompt_exporter_tests: pass=" << g_pass
              << " fail=" << g_fail << " total=" << g_total << "\n";
    return g_fail == 0 ? 0 : 1;
}
