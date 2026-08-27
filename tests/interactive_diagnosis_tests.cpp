#include "interactive_v2_fixture.hpp"
#include "hy3_algotrace/sha256.hpp"
#include <functional>
#include <iostream>

namespace {
using namespace hy3;
using namespace interactive_fixture;
int passed=0, failed=0;
#define CHECK(c,m) do { if(c) ++passed; else { ++failed; std::cerr<<"FAIL "<<__LINE__<<": "<<m<<'\n'; } } while(0)
InteractiveDiagnosisResult run(const json& input,const json& d,const std::string& prompt) {
    OwnedRoot root; InteractiveDiagnosisRequest parsed;
    if(!parseInteractiveDiagnosisRequest(input,parsed).ok) throw std::runtime_error("bad fixture request");
    FakeModelClient client(success(d));
    return runInteractiveDiagnosis(parsed,prompt,root.path.string(),client);
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    try {
        const auto prompt=readText(fs::path(argv[1])/"prompts/hy3-interactive-diagnosis-v2.md");
        CHECK(validInteractivePromptTemplate(prompt),"real v2 template");
        CHECK(!validInteractivePromptTemplate(readText(fs::path(argv[1])/"prompts/hy3-interactive-diagnosis-v1.md")),"v1 template rejected");
        CHECK(!validInteractivePromptTemplate(prompt+"{{interactive_request_json}}"),"duplicate marker");
        const auto input=request("unit");
        InteractiveDiagnosisRequest parsed;
        CHECK(parseInteractiveDiagnosisRequest(input,parsed).ok && !parsed.reasoning && !parsed.user_notes && parsed.test_cases.empty(),"minimal request");
        CHECK(interactiveDiagnosisRequestJson(parsed)["problem_statement"]==input["problem_statement"],"v2 serialization");
        for(const char* field:{"problem_statement","cpp_solution"}) {
            for(const json& value:std::vector<json>{nullptr,""," \t\r\n","　 ",42,false,json::array(),json::object()}) {
                auto bad=input; bad[field]=value;
                CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"required blank/type rejected");
            }
            auto bad=input; bad.erase(field);
            CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"missing rejected");
        }
        for(const char* field:{"reasoning","user_notes"}) {
            for(const json& value:std::vector<json>{nullptr,""," \t\r\n","　"}) {
                auto optional=input; optional[field]=value;
                CHECK(parseInteractiveDiagnosisRequest(optional,parsed).ok && !parsed.reasoning && !parsed.user_notes,"optional empty rules");
            }
            auto bad=input; bad[field]=7;
            CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"optional wrong type");
        }
        for(const auto& limit:std::vector<std::pair<std::string,std::size_t>>{
            {"problem_statement",60000},{"cpp_solution",120000},{"reasoning",30000},{"user_notes",10000}}) {
            auto bad=input; bad[limit.first]=std::string(limit.second+1,'x');
            CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"byte limit");
        }
        auto bad=input; bad.erase("schema_version");
        CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"unversioned rejected");
        bad=input; bad["schema_version"]="interactive-request-v1";
        CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"old version rejected");
        bad=input; bad["problem"]=json::object();
        CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"unknown/v1 fields rejected");
        bad=input; bad["algorithm_type"]="dp";
        CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"not greedy");
        for(const auto& raw:std::vector<std::string>{std::string("x\0y",3),std::string(1,'\xFF')}) {
            bad=input; bad["cpp_solution"]=raw;
            CHECK(!parseInteractiveDiagnosisRequest(bad,parsed).ok,"invalid encoding");
        }
        auto tests=input; tests["test_cases"]=json::array({{{"input",""},{"expected_output",""}}});
        CHECK(parseInteractiveDiagnosisRequest(tests,parsed).ok && parsed.test_cases.size()==1,"empty test stdin/stdout valid");
        CHECK(parseInteractiveDiagnosisRequest(input,parsed).ok && parsed.test_cases.empty(),"reuse clears state");
        tests["test_cases"][0]["input"]=nullptr;
        CHECK(!parseInteractiveDiagnosisRequest(tests,parsed).ok,"test string cannot be null");
        tests["test_cases"]="";
        CHECK(!parseInteractiveDiagnosisRequest(tests,parsed).ok,"tests type");
        tests["test_cases"]=json::array();
        for(int i=0;i<21;++i) tests["test_cases"].push_back({{"input",""},{"expected_output",""}});
        CHECK(!parseInteractiveDiagnosisRequest(tests,parsed).ok,"test count");

        CHECK(run(input,diagnosis("unit"),prompt).ok,"CF >= fixture");
        CHECK(run(request("unit",true),diagnosis("unit","correct"),prompt).ok,"correct code no proof required");
        CHECK(run(input,diagnosis("unit","undetermined"),prompt).ok,"uncertain");
        const auto reject=[&](const std::function<void(json&)>& mutate,const char* label) {
            auto d=diagnosis("unit"); mutate(d); CHECK(!run(input,d,prompt).ok,label);
        };
        for(const char* category:{"missing_greedy_proof","implementation_mismatch","invalid_greedy_proof"})
            reject([&](json& d){d["primary_category"]=category; d["findings"][0]["category"]=category;},"no absent/imagined reasoning finding");
        auto withReasoning=input; withReasoning["reasoning"]="严格大于才停止";
        auto d=diagnosis("unit"); d["primary_category"]="implementation_mismatch"; d["findings"][0]["category"]="implementation_mismatch";
        CHECK(!run(withReasoning,d,prompt).ok,"mismatch must quote actual reasoning");
        d["findings"][0]["input_evidence"]={{"source","reasoning"},{"excerpt","严格大于"}};
        CHECK(run(withReasoning,d,prompt).ok,"explicit reasoning mismatch permitted");
        d=diagnosis("unit"); d["primary_category"]="code_logic_error"; d["findings"][0]["category"]="code_logic_error";
        CHECK(run(input,d,prompt).ok,"v2 code logic extension");

        // Distinct wrong greedy choice, entirely scripted, not production heuristics.
        auto ascending=input; std::string asc=code();
        asc.replace(asc.find("rbegin(), coins.rend()"),std::string("rbegin(), coins.rend()").size(),"begin(), coins.end()");
        ascending["cpp_solution"]=asc; d=diagnosis("unit");
        d["steps"][1]["code_location"]=location(asc,11,11);
        d["first_error"]={{"step_id","s2"},{"explanation","先选择最小硬币"}};
        d["primary_category"]="wrong_greedy_choice";
        d["findings"][0]["category"]="wrong_greedy_choice"; d["findings"][0]["step_id"]="s2";
        d["findings"][0]["code_location"]=location(asc,11,11);
        d["findings"][0]["reason"]="先取小硬币不能保证最少数量";
        d["findings"][0]["suggestion"]="从大到小选择";
        d["counterexample"]["input"]="3\n1 2 10\n"; d["counterexample"]["expected_output"]="1\n";
        d["counterexample"]["predicted_candidate_output"]="3\n";
        CHECK(run(ascending,d,prompt).ok,"other error type fixture");

        reject([](json& x){x["steps"][1]["id"]="s1";},"duplicate step");
        reject([](json& x){x["findings"][0]["step_id"]="absent";},"dangling step");
        reject([](json& x){x["first_error"]["step_id"]="s1";},"first must reference finding");
        reject([](json& x){x["steps"][0]["code_location"]["start_line"]=0;},"zero line");
        reject([](json& x){x["steps"][0]["code_location"]["end_line"]=999;},"out of range");
        reject([](json& x){x["steps"][0]["code_location"]["end_line"]=1;},"reversed range");
        reject([](json& x){x["steps"][0]["code_location"]["start_line"]=7.0;},"floating line");
        reject([](json& x){x["steps"][0]["code_location"]["end_line"]=UINT64_MAX;},"huge unsigned line");
        reject([](json& x){x["findings"][0]["code_location"]["snippet"]="if (taken * 2 >= total) break;";},"trimmed snippet");
        reject([](json& x){x["findings"][0]["step_id"]="s2";},"outside step");
        reject([](json& x){x["findings"][0]["input_evidence"]["excerpt"]="not in input";},"bad evidence");
        reject([](json& x){x["algorithm_overview"]["origin"]="user_reasoning";},"model overview origin");
        reject([](json& x){x["counterexample"]["candidate_output_basis"]="executed";},"static inference only");
        reject([](json& x){x["reference_solution"]["provenance"]="verified";},"solution unverified");
        reject([](json& x){x["reference_solution"]["correctness"]=" ";},"complete solution required");
        for(const char* status:{"correct","undetermined","AC"})
            reject([&](json& x){x["status"]=status;},"contradictory/invalid status");
        reject([](json& x){x["schema_version"]="interactive-diagnosis-v1";},"old response");
        reject([](json& x){x["unknown"]=true;},"unknown response key");
        reject([](json& x){x["first_error"]["explanation"]="";},"first explanation required");
        auto unknown=diagnosis("unit","undetermined");
        unknown["reference_solution"]=diagnosis("unit")["reference_solution"];
        CHECK(!run(input,unknown,prompt).ok,"no invented uncertain solution");
        auto unlocated=diagnosis("unit");
        unlocated["findings"][0]["step_id"]=nullptr; unlocated["findings"][0]["code_location"]=nullptr;
        unlocated["findings"][0]["location_reason"]="无法可靠对应"; unlocated["first_error"]["step_id"]=nullptr;
        CHECK(run(input,unlocated,prompt).ok,"unlocated with reason");
        unlocated["first_error"]["step_id"]="s3";
        CHECK(!run(input,unlocated,prompt).ok,"no first claim with unlocated finding");

        auto repeated=input; repeated["cpp_solution"]="\r\n  x();\r\n\r\n  x();\r\n";
        const std::string lf="\n  x();\n\n  x();\n";
        auto rd=diagnosis("unit");
        rd["steps"]=json::array({
            {{"id","later"},{"summary","逻辑先解释第四行"},{"code_location",location(lf,4,4)}},
            {{"id","earlier"},{"summary","逻辑再解释第二行"},{"code_location",location(lf,1,3)}}});
        rd["first_error"]["step_id"]="later"; rd["findings"][0]["step_id"]="later";
        rd["findings"][0]["code_location"]=location(lf,4,4);
        rd["findings"].push_back(rd["findings"][0]); rd["findings"][1]["id"]="f2";
        rd["findings"][1]["step_id"]="earlier"; rd["findings"][1]["code_location"]=location(lf,2,2);
        CHECK(run(repeated,rd,prompt).ok,"CRLF blanks and logical first not minimum line");
        rd["findings"][0]["code_location"]["start_line"]=3; rd["findings"][0]["code_location"]["end_line"]=3;
        CHECK(!run(repeated,rd,prompt).ok,"global repeated snippet cannot justify wrong line");
        CHECK(parseInteractiveDiagnosisRequest(repeated,parsed).ok && parsed.cpp_solution==lf,"no trim");

        OwnedRoot audit; CHECK(parseInteractiveDiagnosisRequest(input,parsed).ok,"audit request");
        auto full=diagnosis("unit"); FakeModelClient client(success(full));
        auto result=runInteractiveDiagnosis(parsed,prompt,audit.path.string(),client);
        auto browser=interactiveDiagnosisBrowserJson(result);
        CHECK(result.ok && browser["diagnosis"]==full,"all fields reach browser unchanged");
        auto side=json::parse(readText(audit.path/parsed.request_id/"model-call.json"));
        std::vector<std::uint8_t> normalized; std::string error;
        normalizeUtf8(std::vector<std::uint8_t>(prompt.begin(),prompt.end()),normalized,error);
        CHECK(side["schema_version"]=="interactive-model-call-v2" &&
              side["prompt_template_id"]==kInteractiveTemplateId &&
              side["prompt_template_sha256"]==sha256_hex(normalized) &&
              side["source_code_sha256"]==sha256_hex(code()),"v2 audit identities/hashes");
        CHECK(readText(audit.path/parsed.request_id/"raw-response.txt")==full.dump(),"raw exact");
        CHECK(!runInteractiveDiagnosis(parsed,prompt,audit.path.string(),client).ok && client.callCount()==1,"duplicate blocked");
        OwnedRoot invalidRoot; FakeModelClient invalidClient(success(full));
        auto typed=parsed; typed.cpp_solution=" \n";
        CHECK(!runInteractiveDiagnosis(typed,prompt,invalidRoot.path.string(),invalidClient).ok && invalidClient.callCount()==0,"typed caller validation");
        CHECK(!runInteractiveDiagnosis(parsed,"# v1\n{{interactive_request_json}}",invalidRoot.path.string(),invalidClient).ok && invalidClient.callCount()==0,"template before invocation");
        const std::string fence(3,static_cast<char>(96));
        for(const auto& raw:std::vector<std::string>{fence+"json\n"+full.dump()+"\n"+fence,"{","",std::string(1,'\xFF')}) {
            OwnedRoot root; auto call=success(full); call.raw_response.assign(raw.begin(),raw.end());
            FakeModelClient fake(call); auto r=runInteractiveDiagnosis(parsed,prompt,root.path.string(),fake);
            CHECK(!r.ok && fake.callCount()==1,"invalid raw not repaired/retried");
            CHECK(readText(root.path/parsed.request_id/"raw-response.txt")==raw,"invalid raw exact");
        }
        for(auto status:{ModelCallStatus::AuthenticationError,ModelCallStatus::Timeout,ModelCallStatus::RateLimited,ModelCallStatus::TransportError}) {
            OwnedRoot root; ModelCallResult call; call.status=status; call.message="Authorization: Bearer synthetic-secret";
            FakeModelClient fake(call); auto r=runInteractiveDiagnosis(parsed,prompt,root.path.string(),fake);
            auto b=interactiveDiagnosisBrowserJson(r);
            CHECK(!r.ok && r.outcome=="model_call_failed" && b["diagnosis"].is_null() && fake.callCount()==1,"failure not successful");
            CHECK(b.dump().find("synthetic-secret")==std::string::npos &&
                  readText(root.path/parsed.request_id/"model-call.json").find("Bearer")==std::string::npos,"provider detail excluded");
        }
    } catch(const std::exception& e) {++failed;std::cerr<<e.what()<<'\n';}
    std::cout<<"interactive_diagnosis_tests: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed==0?0:1;
}
