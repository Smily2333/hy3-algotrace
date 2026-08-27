#include "hy3_algotrace/evaluation.hpp"
#include "hy3_algotrace/model_runner.hpp"
#include "hy3_algotrace/sha256.hpp"
#include <fstream>
#include <set>
#include <stdexcept>
#include <algorithm>
namespace hy3::evaluation {
namespace fs = std::filesystem;
namespace {
void need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool idOK(const std::string& id) {
    return !id.empty() && id.size() < 100 &&
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}
json ratio(std::size_t n, std::size_t d) {
    return {{"numerator",n},{"denominator",d},{"value", d ? json(double(n)/double(d)) : json(nullptr)}};
}
struct Lock {
    fs::path p;
    explicit Lock(const fs::path& root):p(root/"lock") {
        need(fs::create_directory(p), "budget locked: inspect interrupted run, never automatically retry");
    }
    ~Lock(){std::error_code ec; fs::remove(p,ec);}
};
}
json load(const fs::path& p) {
    std::ifstream f(p,std::ios::binary);
    need(bool(f),"cannot read JSON");
    return json::parse(f);
}
void saveNew(const fs::path& p,const json& j) {
    need(!fs::exists(p),"refusing overwrite");
    std::ofstream f(p,std::ios::binary);
    need(bool(f),"cannot create JSON");
    f << j.dump(2) << '\n'; f.flush(); need(bool(f),"JSON write failed");
}
void validateDataset(const json& d) {
    need(d.at("schema_version")=="greedy-dataset-v1","dataset version");
    need(d.at("problems").is_array() && d.at("samples").is_array(),"dataset arrays");
    std::set<std::string> ids, samples;
    for(const auto& p:d.at("problems")) {
        const auto id=p.at("id").get<std::string>();
        need(idOK(id)&&ids.insert(id).second,"duplicate/unsafe problem id");
        need(p.at("split")=="development"||p.at("split")=="holdout","split");
        need(p.at("difficulty")=="basic"||p.at("difficulty")=="medium"||p.at("difficulty")=="hard","difficulty");
        need(p.at("test_cases").is_array()&&!p.at("test_cases").empty(),"tests missing");
        std::set<std::string> tests;
        for(const auto& t:p.at("test_cases")) {
            need(t.at("input").is_string()&&t.at("expected_output").is_string(),"test text");
            need(tests.insert(t.at("id").get<std::string>()).second,"duplicate test");
        }
        need(p.at("reference_code").is_string()&&!p.at("reference_code").get<std::string>().empty(),"reference missing");
    }
    for(const auto& s:d.at("samples")) {
        auto id=s.at("id").get<std::string>();
        need(idOK(id)&&samples.insert(id).second,"duplicate/unsafe sample id");
        need(ids.count(s.at("problem_id").get<std::string>())==1,"unknown problem");
        auto r=requestFor(d,id);
        InteractiveDiagnosisRequest checked;
        need(parseInteractiveDiagnosisRequest(interactiveDiagnosisRequestJson(r),checked).ok,"sample request invalid");
        const auto& g=s.at("gold");
        need(g.at("process_status")=="correct"||g.at("process_status")=="incorrect","gold status");
        need(g.at("answer_status")=="unverified","source gold must not impersonate execution evidence");
        if(!g.at("first_error").is_null()) {
            auto first=g.at("first_error").at("start_line").get<int>();
            auto last=g.at("first_error").at("end_line").get<int>();
            const auto lines=std::count(r.cpp_solution.begin(),r.cpp_solution.end(),'\n')+
                (!r.cpp_solution.empty()&&r.cpp_solution.back()!='\n');
            need(first>0&&last>=first&&last<=lines,"gold range");
        }
    }
}
InteractiveDiagnosisRequest requestFor(const json& d,const std::string& id) {
    for(const auto& s:d.at("samples")) if(s.at("id")==id) {
        for(const auto& p:d.at("problems")) if(p.at("id")==s.at("problem_id")) {
            // Explicit allowlist: no sample IDs, difficulty, split, gold, tests or references.
            InteractiveDiagnosisRequest r;
            r.request_id="input"; r.problem_statement=p.at("statement").get<std::string>();
            r.cpp_solution=s.at("code").get<std::string>();
            if(!s.at("reasoning").is_null()) r.reasoning=s.at("reasoning").get<std::string>();
            return r;
        }
    }
    throw std::runtime_error("sample missing");
}
std::string render(const InteractiveDiagnosisRequest& r,const std::string& base,const std::string& extension) {
    need(validInteractivePromptTemplate(base),"v2 template invalid");
    std::vector<std::uint8_t> out;std::string error;
    need(normalizeUtf8({base.begin(),base.end()},out,error),"template encoding");
    std::string text(out.begin(),out.end());
    need(normalizeUtf8({extension.begin(),extension.end()},out,error),"extension encoding");
    const std::string normalizedExtension(out.begin(),out.end());
    need(normalizedExtension.rfind("# hy3-greedy-evaluation-v1\n",0)==0,"evaluation template invalid");
    const std::string marker="{{interactive_request_json}}";
    text.replace(text.find(marker),marker.size(),interactiveDiagnosisRequestJson(r).dump());
    text += "\n"+normalizedExtension;
    need(text.size()<=interactive_limits::rendered_prompt,"prompt too large");
    return text;
}
namespace {
struct ContractError {std::string path,message;};
void keys(const json& j,std::initializer_list<const char*> names,const std::string& path) {
    if(!j.is_object())throw ContractError{path,"expected object"};
    std::set<std::string> allowed;
    for(const auto name:names){allowed.insert(name);if(!j.contains(name))throw ContractError{path+"/"+name,"required field missing"};}
    for(auto i=j.begin();i!=j.end();++i)if(!allowed.count(i.key()))throw ContractError{path,"unexpected field"};
}
void locations(const json& j,const std::string& path) {
    keys(j,{"start_line","end_line","snippet"},path);
}
void structure(const json& j) {
    keys(j,{"schema_version","diagnosis","solution_code"},"");
    const auto& d=j.at("diagnosis");const std::string p="/diagnosis";
    keys(d,{"schema_version","request_id","status","summary","limitations","algorithm_overview","steps","first_error","primary_category","findings","counterexample","reference_solution"},p);
    keys(d.at("algorithm_overview"),{"origin","summary"},p+"/algorithm_overview");
    keys(d.at("first_error"),{"step_id","explanation"},p+"/first_error");
    for(const char* name:{"steps","findings"}) {
        if(!d.at(name).is_array())throw ContractError{p+"/"+name,"expected array"};
        for(std::size_t i=0;i<d.at(name).size();++i) {
            const auto& item=d.at(name).at(i);auto path=p+"/"+name+"/"+std::to_string(i);
            if(std::string(name)=="steps")keys(item,{"id","summary","code_location"},path);
            else {keys(item,{"id","step_id","category","reason","input_evidence","code_location","location_reason","suggestion"},path);
                keys(item.at("input_evidence"),{"source","excerpt"},path+"/input_evidence");}
            if(!item.at("code_location").is_null())locations(item.at("code_location"),path+"/code_location");
        }
    }
    keys(d.at("counterexample"),{"availability","input","expected_output","predicted_candidate_output","candidate_output_basis","explanation","provenance"},p+"/counterexample");
    keys(d.at("reference_solution"),{"availability","strategy","correctness","complexity","boundaries","unavailable_reason","provenance"},p+"/reference_solution");
    keys(j.at("solution_code"),{"availability","language","standard","source_code","unavailable_reason"},"/solution_code");
}
void locationPaths(const json& d,const InteractiveDiagnosisRequest& r) {
    InteractiveDiagnosisRequest checked;
    if(!parseInteractiveDiagnosisRequest(interactiveDiagnosisRequestJson(r),checked).ok)
        throw ContractError{"/request","invalid input"};
    std::vector<std::string> lines;
    const auto& source=checked.cpp_solution;std::size_t pos=0;
    while(pos<source.size()) {
        auto end=source.find('\n',pos);
        lines.push_back(source.substr(pos,end==std::string::npos?end:end-pos));
        if(end==std::string::npos)break;
        pos=end+1;
    }
    for(const char* group:{"steps","findings"})for(std::size_t i=0;i<d.at(group).size();++i) {
        const auto& item=d.at(group).at(i);const auto& loc=item.at("code_location");
        const std::string path=std::string("/diagnosis/")+group+"/"+std::to_string(i)+"/code_location";
        if(loc.is_null())continue;
        for(const char* key:{"start_line","end_line"})
            if(!loc.at(key).is_number_integer()||loc.at(key)<1||loc.at(key)>lines.size())
                throw ContractError{path+"/"+key,"line outside source"};
        const auto first=loc.at("start_line").get<std::size_t>(),last=loc.at("end_line").get<std::size_t>();
        if(first>last)throw ContractError{path,"inverted range"};
        std::string snippet;
        for(auto line=first;line<=last;++line){if(line>first)snippet+='\n';snippet+=lines.at(line-1);}
        if(loc.at("snippet")!=snippet)throw ContractError{path+"/snippet","decoded snippet differs from declared source lines"};
    }
}
}
std::string renderV2(const InteractiveDiagnosisRequest& r,const std::string& input) {
    InteractiveDiagnosisRequest checked;
    need(parseInteractiveDiagnosisRequest(interactiveDiagnosisRequestJson(r),checked).ok,"invalid request");
    std::vector<std::uint8_t> bytes;std::string error;
    need(normalizeUtf8({input.begin(),input.end()},bytes,error),"template encoding");
    std::string text(bytes.begin(),bytes.end());
    need(text.rfind("# hy3-greedy-evaluation-v2\n",0)==0,"standalone v2 template required");
    const std::string marker="{{evaluation_request_json}}";auto at=text.find(marker);
    need(at!=std::string::npos&&text.find(marker,at+marker.size())==std::string::npos,"exactly one request marker required");
    text.replace(at,marker.size(),interactiveDiagnosisRequestJson(checked).dump());
    need(text.size()<=interactive_limits::rendered_prompt,"prompt too large");
    return text;
}
json parse(const std::string& raw,const InteractiveDiagnosisRequest& r,const std::string& expectedVersion) {
    json result={{"parse_status","invalid_json"},{"response",nullptr},{"expected_schema_version",expectedVersion},{"validation_errors",json::array()}};
    if(raw.empty()){result["parse_status"]="empty_response";return result;}
    if(raw.size()>interactive_limits::model_response){result["parse_status"]="schema_invalid";
        result["validation_errors"].push_back({{"path",""},{"message","response exceeds byte limit"}});return result;}
    auto j=json::parse(raw,nullptr,false);
    if(j.is_discarded()){result["validation_errors"].push_back({{"path",""},{"message","invalid JSON syntax; not repaired"}});return result;}
    std::string path="";
    try {
        need(expectedVersion==version||expectedVersion==version2,"unsupported expected version");
        structure(j);
        path="/schema_version";
        need(j.at("schema_version")==expectedVersion,"response version mismatch");
        path="/diagnosis";
        locationPaths(j.at("diagnosis"),r);
        const auto diagnostic=validateInteractiveDiagnosis(j.at("diagnosis"),r);
        if(!diagnostic.empty())throw ContractError{path,diagnostic};
        path="/solution_code";
        const auto& c=j.at("solution_code");
        need(c.is_object()&&c.size()==5,"solution schema");
        need(c.at("language")=="cpp"&&c.at("standard")=="c++17","language");
        if(c.at("availability")=="provided") {
            path="/solution_code/source_code";
            need(c.at("source_code").is_string()&&c.at("unavailable_reason").is_null(),"code fields");
            InteractiveDiagnosisRequest code=r;
            code.cpp_solution=c.at("source_code").get<std::string>();
            InteractiveDiagnosisRequest checked;
            need(parseInteractiveDiagnosisRequest(interactiveDiagnosisRequestJson(code),checked).ok,"code missing/invalid");
            need(j.at("diagnosis").at("reference_solution").at("availability")=="provided","code without explanation");
        } else {
            need(c.at("availability")=="unavailable"&&c.at("source_code").is_null()&&
                 c.at("unavailable_reason").is_string()&&!c.at("unavailable_reason").get<std::string>().empty(),"unavailable");
        }
        result["parse_status"]="parsed";result["response"]=j;
    } catch(const ContractError& e) {result["parse_status"]="schema_invalid";result["validation_errors"].push_back({{"path",e.path},{"message",e.message}});
    } catch(const std::exception&) {result["parse_status"]="schema_invalid";result["validation_errors"].push_back({{"path",path},{"message","field type, value or condition invalid"}});}
    return result;
}
std::string normalizeOutput(std::string s) {
    std::string out;
    for(std::size_t i=0;i<s.size();++i) {
        if(s[i]=='\r'&&i+1<s.size()&&s[i+1]=='\n'){out+='\n';++i;}else out+=s[i];
    }
    if(!out.empty()&&out.back()=='\n')out.pop_back();
    return out; // No trailing-space or token repair; one terminal newline optional.
}
json compareOutput(const std::string& a,const std::string& e) {
    const auto x=normalizeOutput(a),y=normalizeOutput(e);
    std::size_t i=0;while(i<std::min(x.size(),y.size())&&x[i]==y[i])++i;
    return {{"verdict",x==y?"passed":"wrong_answer"},{"first_difference_byte",x==y?json(nullptr):json(i)},
            {"comparison","crlf_lf_optional_one_terminal_lf_v1"}};
}
json report(const json& dataset,const json& records,bool synthetic) {
    need(records.is_array(),"records array");
    std::set<std::string> seen;
    for(const auto& r:records) {
        auto id=r.at("sample_id").get<std::string>();
        need(seen.insert(id).second,"duplicate record");
        bool found=false;for(const auto& s:dataset.at("samples"))found=found||s.at("id")==id;
        need(found,"unknown record sample");
    }
    std::size_t agree=0, correct=0, fp=0, answers=0, verified=0, processes=0, reviewed=0;
    std::size_t locN=0,locD=0,failed=0,unknown=0,missing=0;
    json rows=json::array(),slices=json::object(),categories=json::object(),reviews=json::array();
    for(const auto& s:dataset.at("samples")) {
        const json* rec=nullptr;
        for(const auto& r:records)if(r.at("sample_id")==s.at("id"))rec=&r;
        const auto& g=s.at("gold");
        bool valid=rec&&rec->value("parse_status","")=="parsed"&&rec->contains("response")&&!rec->at("response").is_null();
        std::string pred=valid?rec->at("response").at("diagnosis").at("status").get<std::string>():"failed";
        bool matched=pred==g.at("process_status");
        agree+=matched;failed+=!valid;unknown+=pred=="undetermined";
        correct+=g.at("process_status")=="correct";
        bool falsePositive=g.at("process_status")=="correct"&&pred=="incorrect";fp+=falsePositive;
        const auto answer=rec?rec->value("solution_answer_status","unverified"):"unverified";
        const auto process=rec?rec->value("solution_process_status","unreviewed"):"unreviewed";
        verified+=answer!="unverified"; answers+=answer=="passed";
        reviewed+=process!="unreviewed";processes+=process=="correct";
        missing+=!valid||rec->at("response").at("solution_code").at("availability")!="provided";
        auto candidateAnswer=rec?rec->value("candidate_answer_status","unverified"):"unverified";
        bool eligible=candidateAnswer=="wrong_answer"&&!g.at("first_error").is_null();
        bool hit=false;
        if(eligible&&pred=="incorrect") {
            const auto& diag=rec->at("response").at("diagnosis");
            for(const auto& f:diag.at("findings")) {
                if(f.at("step_id")==diag.at("first_error").at("step_id")&&!f.at("code_location").is_null()) {
                    const auto& loc=f.at("code_location");
                    const auto& gold=g.at("first_error");
                    // Tight code range containment plus independent category mapping.
                    hit=hit||(f.at("category")==g.at("primary_category")&&
                         loc.at("start_line")>=gold.at("start_line")&&loc.at("end_line")<=gold.at("end_line"));
                }
            }
        }
        locD+=eligible;locN+=eligible&&hit;
        std::string split,difficulty;
        for(const auto& p:dataset.at("problems"))if(p.at("id")==s.at("problem_id")){
            split=p.at("split");difficulty=p.at("difficulty");
        }
        const std::string category=g.at("primary_category").is_null()?"none":g.at("primary_category").get<std::string>();
        json row={{"sample_id",s.at("id")},{"predicted_status",pred},{"agreement",matched},
                  {"candidate_answer_status",candidateAnswer},{"solution_answer_status",answer},
                  {"solution_process_status",process},{"location_eligible",eligible},{"location_hit",hit},
                  {"split",split},{"difficulty",difficulty},{"gold_category",category}};
        rows.push_back(row);
        for(const auto& key:{split,difficulty,category}) {
            if(!slices.contains(key))slices[key]={{"n",0},{"agreement",0},{"failed",0},{"undetermined",0},{"false_positive",0},
                {"process_correct",0},{"answer_verified",0},{"answer_passed",0},{"process_reviewed",0},{"process_passed",0},{"location_eligible",0},{"location_hit",0}};
            auto& x=slices[key];x["n"]=x["n"].get<int>()+1;
            x["agreement"]=x["agreement"].get<int>()+int(matched);
            x["failed"]=x["failed"].get<int>()+int(!valid);
            x["undetermined"]=x["undetermined"].get<int>()+int(pred=="undetermined");
            x["false_positive"]=x["false_positive"].get<int>()+int(falsePositive);
            x["process_correct"]=x["process_correct"].get<int>()+int(g.at("process_status")=="correct");
            x["answer_verified"]=x["answer_verified"].get<int>()+int(answer!="unverified");
            x["answer_passed"]=x["answer_passed"].get<int>()+int(answer=="passed");
            x["process_reviewed"]=x["process_reviewed"].get<int>()+int(process!="unreviewed");
            x["process_passed"]=x["process_passed"].get<int>()+int(process=="correct");
            x["location_eligible"]=x["location_eligible"].get<int>()+int(eligible);
            x["location_hit"]=x["location_hit"].get<int>()+int(eligible&&hit);
        }
        if(valid) {
            auto c=rec->at("response").at("diagnosis").at("primary_category");
            std::string key=c.is_null()?"none":c.get<std::string>();
            categories[key]=categories.value(key,0)+1;
        }
        if((candidateAnswer=="passed"&&pred=="incorrect")||!matched||difficulty=="hard"||g.at("process_status")=="correct")
            reviews.push_back({{"sample_id",s.at("id")},{"priority",candidateAnswer=="passed"&&pred=="incorrect"?"answer_correct_alert":"stratified_review"},
                {"reviewer",nullptr},{"date",nullptr},{"decision","pending"},{"evidence",nullptr}});
    }
    const auto n=dataset.at("samples").size();
    for(auto& x:slices) {
        x["diagnosis_agreement"]=ratio(x["agreement"],x["n"]);
        x["solution_answer_accuracy"]=ratio(x["answer_passed"],x["n"]);
        x["solution_process_accuracy"]=ratio(x["process_passed"],x["n"]);
        x["answer_verification_coverage"]=ratio(x["answer_verified"],x["n"]);
        x["process_review_coverage"]=ratio(x["process_reviewed"],x["n"]);
        x["first_error_localization"]=ratio(x["location_hit"],x["location_eligible"]);
        x["process_correct_false_positive_rate"]=ratio(x["false_positive"],x["process_correct"]);
    }
    return {{"schema_version","greedy-report-v1"},{"data_kind",synthetic?"SYNTHETIC_NOT_MODEL_EVIDENCE":"real"},{"experiment_started",!records.empty()},{"records_received",records.size()},
        {"included",n},{"diagnosis_agreement",ratio(agree,n)},{"solution_answer_accuracy",ratio(answers,n)},
        {"solution_process_accuracy",ratio(processes,n)},{"answer_verification_coverage",ratio(verified,n)},
        {"process_review_coverage",ratio(reviewed,n)},{"first_error_localization",ratio(locN,locD)},
        {"process_correct_false_positive_rate",ratio(fp,correct)},{"failures",failed},{"undetermined",unknown},
        {"missing_solution",missing},{"rows",rows},{"strata",slices},{"predicted_categories",categories},
        {"human_review_queue",reviews},{"limitations",json::array({"Finite tests are not proof; unreviewed labels are agent-authored.",
            "Location match is category plus tight code range, not proof of explanation correctness.",
            "Unverified outputs never count as verified correct; failures remain in denominators."})}};
}
json attachAnswerEvidence(const json& dataset,const json& records,const json& evidence) {
    need(evidence.at("schema_version")=="fixed-answer-results-v1"&&
         evidence.at("dataset_sha256")==sha256_hex(dataset.dump()),"answer evidence dataset mismatch");
    json out=records;
    std::set<std::string> ids;
    for(const auto& result:evidence.at("results"))need(ids.insert(result.at("id").get<std::string>()).second,"duplicate answer result");
    for(auto& record:out) {
        const auto id=record.at("sample_id").get<std::string>();
        const json* sample=nullptr;const json* problem=nullptr;
        for(const auto& s:dataset.at("samples"))if(s.at("id")==id)sample=&s;
        need(sample,"unknown evidence sample");
        for(const auto& p:dataset.at("problems"))if(p.at("id")==sample->at("problem_id"))problem=&p;
        need(problem,"unknown evidence problem");
        for(bool solution:{false,true}) {
            const std::string key=solution?"solution_answer_status":"candidate_answer_status";
            record[key]="unverified";
            if(solution&&(record.value("parse_status","")!="parsed"||record.at("response").at("solution_code").at("availability")!="provided"))continue;
            const auto source=solution?record.at("response").at("solution_code").at("source_code").get<std::string>():sample->at("code").get<std::string>();
            for(const auto& r:evidence.at("results"))if(r.at("id")==id+(solution?"_solution":"")) {
                need(r.at("source_sha256")==sha256_hex(source),"answer evidence code mismatch");
                if(r.at("compile").at("outcome")!="success"){record[key]="compile_error";continue;}
                need(r.at("tests").size()==problem->at("test_cases").size(),"incomplete test evidence");
                std::string verdict="passed";std::set<std::string> tests;
                for(const auto& t:r.at("tests")) {
                    need(tests.insert(t.at("test_id").get<std::string>()).second,"duplicate evidence test");
                    const json* expected=nullptr;
                    for(const auto& e:problem->at("test_cases"))if(e.at("id")==t.at("test_id"))expected=&e;
                    need(expected&&t.at("input_sha256")==sha256_hex(expected->at("input").get<std::string>())&&
                        t.at("expected_output")==expected->at("expected_output"),"test identity mismatch");
                    if(t.at("outcome")!="success")verdict=t.at("outcome");
                    else if(compareOutput(t.at("stdout"),expected->at("expected_output")).at("verdict")!="passed"&&verdict=="passed")verdict="wrong_answer";
                }
                record[key]=verdict;
            }
        }
    }
    return out;
}
Budget::Budget(fs::path root):root_(std::move(root)){fs::create_directories(root_);}
json Budget::summary() const {
    std::uint64_t spent=0,unknown=0,calls=0;bool over=false;
    for(const auto& f:fs::directory_iterator(root_)) if(f.path().extension()==".reserve") {
        auto j=load(f.path());auto upper=j.at("upper").get<std::uint64_t>();
        ++calls;auto done=f.path();done.replace_extension(".done");
        if(fs::exists(done)) {
            auto d=load(done);
            over=over||d.value("reported_upper_violation",false);
            if(d.at("actual").is_null())unknown+=upper;
            else {auto actual=d.at("actual").get<std::uint64_t>();spent+=actual;over=over||actual>upper;}
        }else unknown+=upper;
    }
    return {{"limit",300000},{"call_limit",38},{"calls",calls},{"actual",spent},{"unknown_reserved",unknown},
        {"remaining",spent+unknown<=300000?300000-spent-unknown:0},{"halt",over||spent+unknown>300000}};
}
void Budget::reserve(const std::string& id,std::uint64_t upper) {
    need(idOK(id),"unsafe attempt id");Lock lock(root_);
    auto s=summary();
    need(!s.at("halt").get<bool>()&&s.at("calls").get<unsigned>()<38,"budget halted");
    need(upper>0&&upper<=s.at("remaining").get<std::uint64_t>(),"budget insufficient");
    need(!fs::exists(root_/(id+".reserve"))&&!fs::exists(root_/(id+".done")),"attempt already exists");
    saveNew(root_/(id+".reserve"),{{"upper",upper},{"attempt_id",id}});
}
void Budget::reconcile(const std::string& id,const std::optional<ModelTokenUsage>& usage) {
    need(idOK(id),"unsafe attempt id");Lock lock(root_);
    need(fs::exists(root_/(id+".reserve")),"missing reservation");
    json actual=nullptr;
    const auto upper=load(root_/(id+".reserve")).at("upper").get<std::uint64_t>();
    bool violation=false;
    if(usage)for(const auto& n:{usage->prompt_tokens,usage->completion_tokens,usage->total_tokens})
        violation=violation||(n&&*n>upper);
    if(usage&&usage->prompt_tokens&&usage->completion_tokens&&usage->total_tokens&&
       *usage->prompt_tokens<=300000&&*usage->completion_tokens<=300000&&
       *usage->total_tokens==*usage->prompt_tokens+*usage->completion_tokens)
        actual=*usage->total_tokens;
    saveNew(root_/(id+".done"),{{"actual",actual},{"reported_upper_violation",violation}});
}
}
