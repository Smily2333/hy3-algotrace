#include "hy3_algotrace/evaluation.hpp"
#include "hy3_algotrace/hy3_model_client.hpp"
#include "interactive_v2_fixture.hpp"
#include <iostream>
using namespace hy3;
using namespace hy3::evaluation;
int main(int argc,char**argv){
 int passed=0,failed=0;
 auto check=[&](bool ok,const char* msg){if(ok)++passed;else{++failed;std::cerr<<msg<<"\n";}};
 auto throws=[&](auto f){try{f();return false;}catch(...){return true;}};
 try{
 auto d=load(std::filesystem::path(argc>1?argv[1]:".")/"evaluation/materials/dataset.json");
 validateDataset(d);check(d["problems"].size()==8&&d["samples"].size()==25,"material count");
 InteractiveDiagnosisRequest r;
 check(parseInteractiveDiagnosisRequest(interactive_fixture::request("input"),r).ok,"minimal request");
 auto base=interactive_fixture::readText(std::filesystem::path(argc>1?argv[1]:".")/"prompts/hy3-interactive-diagnosis-v2.md");
 check(render(r,base,"# hy3-greedy-evaluation-v1\r\nJSON\r\n").find('\r')==std::string::npos,"CRLF extension normalized");
 json response={{"schema_version",version},{"diagnosis",interactive_fixture::diagnosis("input")},
    {"solution_code",{{"availability","provided"},{"language","cpp"},{"standard","c++17"},{"source_code",interactive_fixture::code(true)},{"unavailable_reason",nullptr}}}};
 check(parse(response.dump(),r)["parse_status"]=="parsed","complete response");
 check(parse(std::string(3,char(96))+"json\n"+response.dump(),r)["parse_status"]=="invalid_json","no fence repair");
 check(parse("",r)["parse_status"]=="empty_response","empty");
 auto bad=response;bad["solution_code"]["source_code"]=" \n";
 check(parse(bad.dump(),r)["parse_status"]=="schema_invalid","blank code");
 bad=response;bad["diagnosis"]["first_error"]["step_id"]="absent";
 check(parse(bad.dump(),r)["parse_status"]=="schema_invalid","bad reference");
 bad=response;bad.erase("solution_code");
 check(parse(bad.dump(),r)["parse_status"]=="schema_invalid","missing code object");
 bad=response;bad["solution_code"]={{"availability","unavailable"},{"language","cpp"},{"standard","c++17"},{"source_code",nullptr},{"unavailable_reason","insufficient"}};
 check(parse(bad.dump(),r)["parse_status"]=="parsed","unavailable explicit");
 check(compareOutput("2\r\n","2\n")["verdict"]=="passed","CRLF");
 check(compareOutput("2 \n","2\n")["verdict"]=="wrong_answer","no whitespace repair");
 check(compareOutput("2\n\n","2\n")["first_difference_byte"]==1,"extra newline");
 interactive_fixture::OwnedRoot root;
 Budget b(root.path/"budget");b.reserve("one",210000);
 check(throws([&]{b.reserve("one",1);}),"duplicate no resend");
 check(throws([&]{b.reserve("two",100000);}),"unknown reserves full upper");
 ModelTokenUsage usage;usage.prompt_tokens=100;usage.completion_tokens=200;usage.total_tokens=300;
 b.reconcile("one",usage);check(b.summary()["actual"]==300,"usage reconcile");
 check(throws([&]{b.reconcile("one",usage);}),"double reconcile");
 b.reserve("two",200000);b.reconcile("two",std::nullopt);
 Budget recovered(root.path/"budget");check(recovered.summary()["unknown_reserved"]==200000,"unknown survives recovery");
 check(throws([&]{recovered.reserve("three",100000);}),"remaining protected");
 check(throws([&]{b.reserve("../bad",1);}),"safe id");
 Budget count(root.path/"count");for(int i=0;i<38;++i)count.reserve("r"+std::to_string(i),1);
 check(throws([&]{count.reserve("extra",1);}),"38 call cap");
 Budget over(root.path/"over");over.reserve("r",100);over.reconcile("r",usage);
 check(over.summary()["halt"]==true,"underestimated bound halts");
 Budget partial(root.path/"partial");partial.reserve("r",100);ModelTokenUsage inconsistent;inconsistent.total_tokens=500000;
 partial.reconcile("r",inconsistent);check(partial.summary()["halt"]==true,"inconsistent overage stops calls");
 auto rep=report(d,json::array(),true);
 check(rep["failures"]==25&&rep["solution_answer_accuracy"]["numerator"]==0,"missing retained");
 check(rep["first_error_localization"]["value"].is_null(),"zero denominator");
 check(rep["data_kind"]=="SYNTHETIC_NOT_MODEL_EVIDENCE","synthetic labeled");
 auto small=d;small["samples"]=json::array({d["samples"][1]});
 small["samples"][0]["gold"]["first_error"]={{"start_line",16},{"end_line",16},{"logical_error","comparison"}};
 json goodRecord={{"sample_id","s002"},{"parse_status","parsed"},{"response",response},{"candidate_answer_status","wrong_answer"},
     {"solution_answer_status","passed"},{"solution_process_status","unreviewed"}};
 auto matched=report(small,json::array({goodRecord}),true);
 check(matched["first_error_localization"]["numerator"]==1,"line/category matches independent gold");
 goodRecord["response"]["diagnosis"]["first_error"]["step_id"]=nullptr;
 check(report(small,json::array({goodRecord}),true)["first_error_localization"]["numerator"]==0,"no location no hit");
 check(throws([&]{attachAnswerEvidence(d,json::array(),{{"schema_version","fixed-answer-results-v1"},{"dataset_sha256","wrong"}});}),"mismatched evidence rejected");
 auto evidence=load(std::filesystem::path(argc>1?argv[1]:".")/"evaluation/results/fixed-answer-evidence.json");
 json pending=json::array();for(const auto&s:d["samples"])pending.push_back({{"sample_id",s["id"]},{"parse_status","not_attempted"}});
 auto attached=attachAnswerEvidence(d,pending,evidence);int answerPass=0,answerWrong=0;
 for(const auto&x:attached){answerPass+=x["candidate_answer_status"]=="passed";answerWrong+=x["candidate_answer_status"]=="wrong_answer";}
 check(answerPass==10&&answerWrong==15,"actual isolated evidence integration");
 auto synthetic=report(d,attached,true);check(synthetic["first_error_localization"]["denominator"]==15,"location denominator from actual wrong answers");
 auto changed=evidence;changed["results"][8]["source_sha256"]="tampered";
 check(throws([&]{attachAnswerEvidence(d,pending,changed);}),"tampered source hash rejected");
 auto dup=json::array({{{"sample_id","s001"}},{{"sample_id","s001"}}});
 check(throws([&]{report(d,dup,true);}),"duplicate records");
 auto empty=d;empty["samples"]=json::array();auto zero=report(empty,json::array(),true);
 check(zero["diagnosis_agreement"]["value"].is_null(),"empty denominator");
 auto req=requestFor(d,"s001");auto j=interactiveDiagnosisRequestJson(req);
 check(!j.contains("gold")&&!j.contains("difficulty")&&req.request_id=="input"&&req.test_cases.empty(),"projection");
 struct Transport:IHttpTransport{HttpRequest last;int calls=0;HttpResponse perform(const HttpRequest&r)override{last=r;++calls;return {};}} t;
 Hy3ModelClientConfig cfg;cfg.api_key="synthetic";cfg.max_tokens=4096;
 Hy3ModelClient client(t,cfg);client.invoke({"x","test","hash"});
 auto body=json::parse(t.last.body);check(body["max_tokens"]==4096,"output cap transmitted");
 cfg.max_tokens=0;Hy3ModelClient invalid(t,cfg);invalid.invoke({"x","test","hash"});check(t.calls==1,"invalid cap pre-call");
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";++failed;}
 std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
