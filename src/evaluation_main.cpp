#include "hy3_algotrace/evaluation.hpp"
#include "hy3_algotrace/sha256.hpp"
#include "hy3_algotrace/hy3_model_client.hpp"
#include "hy3_algotrace/production_http_transport.hpp"
#include "hy3_algotrace/model_runner.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>
using namespace hy3::evaluation;
namespace fs=std::filesystem;
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("file unavailable");return {std::istreambuf_iterator<char>(f),{}};}
std::string readTemplate(const fs::path& p){auto raw=read(p);std::vector<std::uint8_t> out;std::string error;
    if(!hy3::normalizeUtf8({raw.begin(),raw.end()},out,error))throw std::runtime_error("template encoding");
    return {out.begin(),out.end()};}
bool safeRepositoryPath(const fs::path& p){
    if(p.empty()||p.is_absolute())return false;
    for(const auto& part:p)if(part=="..")return false;
    return true;
}
bool hasTokenHubKey(){
#ifdef _WIN32
    char* value=nullptr;std::size_t size=0;
    if(_dupenv_s(&value,&size,"TOKENHUB_API_KEY")!=0)return false;
    const bool present=value&&*value;std::free(value);return present;
#else
    const char* value=std::getenv("TOKENHUB_API_KEY");return value&&*value;
#endif
}
json formalDataset(const json& manifest){
    std::map<std::string,json> problems,samples;
    for(const auto& source:manifest.at("source_datasets")){
        const fs::path path=source.at("path").get<std::string>();
        if(!safeRepositoryPath(path))throw std::runtime_error("unsafe formal source path");
        auto d=load(path);validateDataset(d);
        if(hy3::sha256_hex(d.dump())!=source.at("canonical_sha256"))throw std::runtime_error("formal source hash mismatch");
        for(const auto& p:d.at("problems"))problems.emplace(p.at("id").get<std::string>(),p);
        for(const auto& s:d.at("samples"))samples.emplace(s.at("id").get<std::string>(),s);
    }
    json out={{"schema_version","greedy-dataset-v1"},{"version",manifest.at("dataset_version")},
        {"frozen",true},{"provenance",{{"kind","formal_projection"},{"experiment_id",manifest.at("experiment_id")}}},
        {"problems",json::array()},{"samples",json::array()}};
    std::set<std::string> addedProblems,addedSamples;
    for(const auto& value:manifest.at("sample_ids")){
        const auto id=value.get<std::string>();
        if(!addedSamples.insert(id).second||!samples.count(id))throw std::runtime_error("duplicate or unknown formal sample");
        const auto problemId=samples.at(id).at("problem_id").get<std::string>();
        if(!problems.count(problemId))throw std::runtime_error("formal problem unavailable");
        if(addedProblems.insert(problemId).second)out["problems"].push_back(problems.at(problemId));
        out["samples"].push_back(samples.at(id));
    }
    validateDataset(out);return out;
}
json formalAnswerEvidence(const json& manifest,const json& dataset){
    json evidence={{"schema_version","fixed-answer-results-v1"},{"dataset_sha256",hy3::sha256_hex(dataset.dump())},{"results",json::array()}};
    std::map<std::string,json> evidenceResults;
    for(const auto& source:manifest.at("answer_evidence_sources")){
        const fs::path path=source.at("path").get<std::string>();if(!safeRepositoryPath(path))throw std::runtime_error("unsafe formal answer evidence path");
        const auto part=load(path);if(hy3::sha256_hex(part.dump())!=source.at("canonical_sha256"))throw std::runtime_error("formal answer evidence hash mismatch");
        for(const auto& r:part.at("results"))evidenceResults.emplace(r.at("id").get<std::string>(),r);
    }
    for(const auto& s:dataset.at("samples")){
        const auto id=s.at("id").get<std::string>();if(!evidenceResults.count(id))throw std::runtime_error("formal answer evidence missing sample");
        evidence["results"].push_back(evidenceResults.at(id));
    }
    return evidence;
}
void validateFormalFreeze(const json& manifest,const json& dataset,const std::string& prompt){
    validateFormalIdentity(manifest,dataset,prompt);
    const fs::path reviewPath=manifest.at("material_review_path").get<std::string>();
    if(!safeRepositoryPath(reviewPath))throw std::runtime_error("unsafe formal review path");
    const auto review=load(reviewPath);
    if(hy3::sha256_hex(review.dump())!=manifest.at("material_review_sha256"))
        throw std::runtime_error("formal review hash mismatch");
    if(review.at("reviewer").get<std::string>().empty()||review.at("reviewed_at").is_null())
        throw std::runtime_error("formal material review is not human-complete");
    std::set<std::string> confirmed;
    for(const auto& e:review.at("entries"))if(e.at("decision")=="gold_confirmed")confirmed.insert(e.at("sample_id"));
    for(const auto& s:dataset.at("samples"))if(!confirmed.count(s.at("id")))
        throw std::runtime_error("formal sample lacks human gold confirmation");
    const auto evidence=formalAnswerEvidence(manifest,dataset);
    json pending=json::array();for(const auto& s:dataset.at("samples"))pending.push_back({{"sample_id",s.at("id")},{"parse_status","not_attempted"}});
    const auto attached=attachAnswerEvidence(dataset,pending,evidence);
    std::set<std::string> processBadAnswerPassed;
    for(const auto& r:attached)for(const auto& s:dataset.at("samples"))if(r.at("sample_id")==s.at("id")&&
        r.at("candidate_answer_status")=="passed"&&s.at("gold").at("process_status")=="incorrect")
        processBadAnswerPassed.insert(s.at("id"));
    if(processBadAnswerPassed.size()<2)throw std::runtime_error("formal answer-correct/process-bad coverage incomplete");
}
json makeFormalPlan(const json& manifest,const json& dataset,const std::string& promptTemplate,const Budget& budget){
    json requests=json::array();std::vector<std::pair<std::string,std::uint64_t>> attempts;std::uint64_t total=0;
    for(const auto& idValue:manifest.at("sample_ids")){
        const auto sample=idValue.get<std::string>();auto req=requestFor(dataset,sample);auto prompt=renderV2(req,promptTemplate);
        const auto upper=conservativeRequestUpper(prompt,manifest.at("output_token_limit"),manifest.at("input_envelope_margin"));
        const auto attempt=manifest.at("attempt_prefix").get<std::string>()+sample;
        attempts.push_back({attempt,upper});total+=upper;
        requests.push_back({{"sample_id",sample},{"attempt_id",attempt},{"prompt_sha256",hy3::sha256_hex(prompt)},
            {"prompt_utf8_bytes",prompt.size()},{"output_token_limit",manifest.at("output_token_limit")},
            {"input_envelope_margin",manifest.at("input_envelope_margin")},{"reserved_upper",upper}});
    }
    budget.requireBatchCapacity(attempts);const auto before=budget.summary();
    return {{"schema_version","formal-batch-plan-v1"},{"experiment_id",manifest.at("experiment_id")},
        {"freeze_manifest_sha256",hy3::sha256_hex(manifest.dump())},{"budget_before",before},{"requests",requests},
        {"batch_reserved_upper",total},{"worst_case_campaign_tokens",before.at("actual").get<std::uint64_t>()+total},
        {"fits_token_and_call_limits",true}};
}
int main(int argc,char**argv){
 try{
    if(argc<3){std::cout<<"hy3_evaluate validate DATASET | export[-v2] DATASET ROOT | import[-v2] DATASET SAMPLE RAW OUT | report[-v2] DATASET RECORDS OUT [EVIDENCE] | jobs DATASET OUT\n"
        <<"Paid, authorization required: call[-v2] DATASET SAMPLE CAMPAIGN_ROOT ACCOUNT_CONFIRMATION\n"
        <<"Formal: formal-plan FREEZE_MANIFEST CAMPAIGN_ROOT | formal-call FREEZE_MANIFEST SAMPLE CAMPAIGN_ROOT ACCOUNT_CONFIRMATION\n"
        <<"Offline formal: formal-bundle FREEZE_MANIFEST CAMPAIGN_ROOT OUT | formal-report FREEZE_MANIFEST BUNDLE OUT [SOLUTION_EVIDENCE]\n"
        <<"Approved execution jobs: formal-solution-jobs FREEZE_MANIFEST BUNDLE STATIC_REVIEW OUT\n";return 1;}
    std::string cmd=argv[1];
    const bool v2=cmd.size()>3&&cmd.substr(cmd.size()-3)=="-v2";
    if(v2)cmd.resize(cmd.size()-3);
    const std::string schema=v2?version2:version;
    const std::string templatePath=v2?"prompts/hy3-greedy-evaluation-v2.md":"prompts/hy3-greedy-evaluation-v1.md";
    auto makePrompt=[&](const hy3::InteractiveDiagnosisRequest& r){return v2?renderV2(r,readTemplate(templatePath)):
        render(r,readTemplate("prompts/hy3-interactive-diagnosis-v2.md"),readTemplate(templatePath));};
    if(cmd=="fingerprint-json"&&argc==3){std::cout<<hy3::sha256_hex(load(argv[2]).dump())<<"\n";return 0;}
    if(cmd=="formal-dataset-fingerprint"&&argc==3){std::cout<<hy3::sha256_hex(formalDataset(load(argv[2])).dump())<<"\n";return 0;}
    if(cmd=="formal-estimate"&&argc==4){
        auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);const auto prompt=readTemplate("prompts/hy3-greedy-evaluation-v2.md");
        auto identity=manifest;identity["status"]="frozen";identity["selection_frozen_before_model_output"]=true;
        validateFormalIdentity(identity,dataset,prompt);
        const fs::path root=argv[3];if(fs::weakly_canonical(root)!=fs::canonical("build/m3-development-20260827"))
            throw std::runtime_error("formal estimate must use original campaign budget");
        std::cout<<makeFormalPlan(manifest,dataset,prompt,Budget(root/"budget")).dump(2)<<"\n";return 0;
    }
    if(cmd=="formal-plan"&&argc==4){
        const auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);
        const auto prompt=readTemplate("prompts/hy3-greedy-evaluation-v2.md");validateFormalFreeze(manifest,dataset,prompt);
        const fs::path root=argv[3];
        if(fs::weakly_canonical(root)!=fs::canonical("build/m3-development-20260827"))
            throw std::runtime_error("formal calls must reuse the original campaign budget");
        Budget budget(root/"budget");auto plan=makeFormalPlan(manifest,dataset,prompt,budget);
        const fs::path formalRoot=root/"formal"/manifest.at("experiment_id").get<std::string>();
        fs::create_directories(formalRoot);saveNew(formalRoot/"batch-plan.json",plan);
        std::cout<<plan.dump(2)<<"\n";return 0;
    }
    if(cmd=="formal-bundle"&&argc==5){
        const auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);
        const auto promptTemplate=readTemplate("prompts/hy3-greedy-evaluation-v2.md");validateFormalFreeze(manifest,dataset,promptTemplate);
        const fs::path root=argv[3];if(fs::weakly_canonical(root)!=fs::canonical("build/m3-development-20260827"))
            throw std::runtime_error("formal bundle must use original campaign root");
        const fs::path formalRoot=root/"formal"/manifest.at("experiment_id").get<std::string>();json records=json::array();
        for(const auto& idValue:manifest.at("sample_ids")){
            const auto sample=idValue.get<std::string>();auto record=load(formalRoot/sample/"record.json");
            if(record.at("sample_id")!=sample||record.at("freeze_manifest_sha256")!=hy3::sha256_hex(manifest.dump())||
               record.at("formal_dataset_sha256")!=hy3::sha256_hex(dataset.dump())||record.at("prompt_template_sha256")!=hy3::sha256_hex(promptTemplate))
                throw std::runtime_error("formal record identity mismatch");
            if(record.value("parse_status","")=="parsed"){
                const auto checked=parse(record.at("response").dump(),requestFor(dataset,sample),version2);
                if(checked.at("parse_status")!="parsed")throw std::runtime_error("formal parsed record failed replay validation");
            }
            record.erase("provider_request_id");record["candidate_answer_status"]="unverified";
            record["solution_answer_status"]="unverified";record["solution_process_status"]="unreviewed";
            records.push_back(std::move(record));
        }
        saveNew(argv[4],{{"schema_version","formal-record-bundle-v1"},{"data_kind","real"},
            {"experiment_id",manifest.at("experiment_id")},{"freeze_manifest_sha256",hy3::sha256_hex(manifest.dump())},
            {"records",records}});return 0;
    }
    if(cmd=="formal-solution-jobs"&&argc==6){
        const auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);
        const auto promptTemplate=readTemplate("prompts/hy3-greedy-evaluation-v2.md");validateFormalFreeze(manifest,dataset,promptTemplate);
        const auto bundle=load(argv[3]),approval=load(argv[4]);
        if(bundle.at("schema_version")!="formal-record-bundle-v1"||bundle.at("experiment_id")!=manifest.at("experiment_id")||
           approval.at("schema_version")!="formal-solution-static-review-v1"||approval.at("experiment_id")!=manifest.at("experiment_id")||
           approval.at("reviewer").get<std::string>().empty()||approval.at("reviewed_at").is_null())
            throw std::runtime_error("formal solution review identity missing");
        std::map<std::string,json> approved;
        for(const auto& entry:approval.at("entries"))if(entry.at("decision")=="approved_restricted_execution")
            approved.emplace(entry.at("sample_id").get<std::string>(),entry);
        json jobs=json::array();
        for(const auto& record:bundle.at("records"))if(record.value("parse_status","")=="parsed"&&
            record.at("response").at("solution_code").at("availability")=="provided"){
            const auto sample=record.at("sample_id").get<std::string>();const auto source=record.at("response").at("solution_code").at("source_code").get<std::string>();
            if(!approved.count(sample)||approved.at(sample).at("source_sha256")!=hy3::sha256_hex(source))
                throw std::runtime_error("model solution lacks matching static execution approval");
            const json* problem=nullptr;for(const auto& s:dataset.at("samples"))if(s.at("id")==sample)
                for(const auto& p:dataset.at("problems"))if(p.at("id")==s.at("problem_id"))problem=&p;
            if(!problem)throw std::runtime_error("formal solution problem unavailable");
            jobs.push_back({{"id",sample+"_solution"},{"problem_id",problem->at("id")},{"source",source},{"tests",problem->at("test_cases")}});
        }
        saveNew(argv[5],{{"schema_version","fixed-answer-jobs-v1"},{"dataset_sha256",hy3::sha256_hex(dataset.dump())},
            {"provenance","Planner-approved model solutions only; execution remains external and isolated"},{"jobs",jobs}});return 0;
    }
    if(cmd=="formal-report"&&(argc==5||argc==6)){
        const auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);
        const auto promptTemplate=readTemplate("prompts/hy3-greedy-evaluation-v2.md");validateFormalFreeze(manifest,dataset,promptTemplate);
        const auto bundle=load(argv[3]);
        if(bundle.at("schema_version")!="formal-record-bundle-v1"||bundle.at("data_kind")!="real"||
           bundle.at("experiment_id")!=manifest.at("experiment_id")||
           bundle.at("freeze_manifest_sha256")!=hy3::sha256_hex(manifest.dump())||
           bundle.at("records").size()!=dataset.at("samples").size())throw std::runtime_error("formal bundle identity mismatch");
        auto evidence=formalAnswerEvidence(manifest,dataset);
        if(argc==6){const auto solutionEvidence=load(argv[5]);
            if(solutionEvidence.at("dataset_sha256")!=hy3::sha256_hex(dataset.dump()))throw std::runtime_error("formal solution evidence dataset mismatch");
            for(const auto& result:solutionEvidence.at("results"))evidence["results"].push_back(result);
        }
        auto records=attachAnswerEvidence(dataset,bundle.at("records"),evidence);
        auto summary=report(dataset,records,false);summary["evaluation_version"]=version2;
        summary["experiment_id"]=manifest.at("experiment_id");summary["freeze_manifest_sha256"]=hy3::sha256_hex(manifest.dump());
        saveNew(argv[4],summary);return 0;
    }
    if(cmd=="formal-call"&&argc==6){
        const auto manifest=load(argv[2]);const auto dataset=formalDataset(manifest);
        const auto promptTemplate=readTemplate("prompts/hy3-greedy-evaluation-v2.md");validateFormalFreeze(manifest,dataset,promptTemplate);
        auto approval=load(argv[5]);
        if(approval.at("service")!="https://tokenhub.tencentmaas.com/v1"||
           approval.at("no_out_of_allowance_charge")!=true||
           approval.at("confirmed_available_tokens").get<std::uint64_t>()<300000||
           approval.at("verified_by").get<std::string>().empty())
            throw std::runtime_error("account allowance confirmation missing");
        if(!hasTokenHubKey())throw std::runtime_error("TOKENHUB_API_KEY missing");
        const fs::path root=argv[4];
        if(fs::weakly_canonical(root)!=fs::canonical("build/m3-development-20260827"))
            throw std::runtime_error("formal calls must reuse the original campaign budget");
        const auto sample=std::string(argv[3]);
        if(std::find(manifest.at("sample_ids").begin(),manifest.at("sample_ids").end(),sample)==manifest.at("sample_ids").end())
            throw std::runtime_error("sample is not in frozen formal cohort");
        const fs::path formalRoot=root/"formal"/manifest.at("experiment_id").get<std::string>();
        const auto plan=load(formalRoot/"batch-plan.json");
        if(plan.at("freeze_manifest_sha256")!=hy3::sha256_hex(manifest.dump()))throw std::runtime_error("formal plan/freeze mismatch");
        Budget budget(root/"budget");std::vector<std::pair<std::string,std::uint64_t>> pending;
        const json* selected=nullptr;
        for(const auto& p:plan.at("requests")){
            const auto attempt=p.at("attempt_id").get<std::string>();
            if(p.at("sample_id")==sample)selected=&p;
            if(!fs::exists(root/"budget"/(attempt+".reserve"))&&!fs::exists(root/"budget"/(attempt+".done")))
                pending.push_back({attempt,p.at("reserved_upper").get<std::uint64_t>()});
        }
        if(!selected)throw std::runtime_error("sample absent from durable formal plan");
        const auto attempt=selected->at("attempt_id").get<std::string>();
        if(fs::exists(root/"budget"/(attempt+".reserve"))||fs::exists(root/"budget"/(attempt+".done")))
            throw std::runtime_error("formal attempt already exists: no resend");
        budget.requireBatchCapacity(pending);
        auto req=requestFor(dataset,sample);auto prompt=renderV2(req,promptTemplate);
        if(selected->at("prompt_sha256")!=hy3::sha256_hex(prompt))throw std::runtime_error("formal prompt differs from durable plan");
        const fs::path attemptRoot=formalRoot/sample;
        if(!fs::create_directory(attemptRoot))throw std::runtime_error("formal artifact directory already exists");
        auto halt=[&](const std::string& reason){if(!fs::exists(formalRoot/"halt.json"))
            saveNew(formalRoot/"halt.json",{{"reason",reason},{"sample_id",sample}});};
        try{saveNew(attemptRoot/"request.json",{{"request",hy3::interactiveDiagnosisRequestJson(req)},{"prompt",prompt},
                {"prompt_sha256",hy3::sha256_hex(prompt)},{"formal_dataset_sha256",hy3::sha256_hex(dataset.dump())},
                {"freeze_manifest_sha256",hy3::sha256_hex(manifest.dump())},{"output_limit",manifest.at("output_token_limit")},
                {"reserved_upper",selected->at("reserved_upper")}});
            budget.reserve(attempt,selected->at("reserved_upper"));
        }catch(...){halt("formal request artifact or reservation write failed");throw;}
        hy3::ProductionHttpTransport transport;hy3::Hy3ModelClientConfig cfg;
        cfg.max_tokens=manifest.at("output_token_limit").get<std::uint64_t>();
        cfg.connect_timeout_ms=10000;cfg.read_timeout_ms=120000;cfg.total_timeout_ms=180000;
        hy3::Hy3ModelClient client(transport,cfg);auto result=client.invoke({attempt,prompt,hy3::sha256_hex(prompt)});
        const std::string raw(result.raw_response.begin(),result.raw_response.end());
        std::ofstream rawFile(attemptRoot/"raw-response.txt",std::ios::binary);rawFile<<raw;rawFile.close();
        if(!rawFile){halt("raw write failed; reservation unresolved");throw std::runtime_error("raw write failed; no retry");}
        try{budget.reconcile(attempt,result.token_usage);}catch(...){halt("formal usage reconciliation failed");throw;}
        auto record=parse(raw,req,version2);record["sample_id"]=sample;record["attempt_id"]=attempt;
        record["raw_sha256"]=hy3::sha256_hex(raw);record["prompt_sha256"]=hy3::sha256_hex(prompt);
        record["freeze_manifest_sha256"]=hy3::sha256_hex(manifest.dump());record["formal_dataset_sha256"]=hy3::sha256_hex(dataset.dump());
        if(result.status!=hy3::ModelCallStatus::Succeeded)record["parse_status"]="not_attempted";
        record["outcome"]=hy3::modelCallStatusName(result.status);record["duration_ms"]=result.duration_ms;
        record["model"]="hy3";record["model_version"]=nullptr;record["data_kind"]="formal_real";
        record["http_status"]=result.http_status?json(*result.http_status):json(nullptr);
        record["provider_request_id"]=result.request_id?json(*result.request_id):json(nullptr);
        record["started_at"]=result.started_at;record["finished_at"]=result.finished_at;
        record["prompt_template_id"]="hy3-greedy-evaluation-v2";
        record["prompt_template_sha256"]=hy3::sha256_hex(promptTemplate);
        record["finish_reason"]=result.finish_reason?json(*result.finish_reason):json(nullptr);
        record["usage_details"]={{"cached_tokens",result.token_usage&&result.token_usage->cached_tokens?json(*result.token_usage->cached_tokens):json(nullptr)},
            {"reasoning_tokens",result.token_usage&&result.token_usage->reasoning_tokens?json(*result.token_usage->reasoning_tokens):json(nullptr)}};
        record["token_usage"]=result.token_usage?json{{"prompt_tokens",result.token_usage->prompt_tokens?json(*result.token_usage->prompt_tokens):json(nullptr)},
            {"completion_tokens",result.token_usage->completion_tokens?json(*result.token_usage->completion_tokens):json(nullptr)},
            {"total_tokens",result.token_usage->total_tokens?json(*result.token_usage->total_tokens):json(nullptr)}}:json(nullptr);
        try{saveNew(attemptRoot/"record.json",record);}catch(...){halt("formal record write failed");throw;}
        if(result.status!=hy3::ModelCallStatus::Succeeded||budget.summary().at("halt")==true){
            halt("formal infrastructure or accounting requires review");
        }
        std::cout<<budget.summary().dump()<<"\n";return result.status==hy3::ModelCallStatus::Succeeded?0:1;
    }
    auto d=load(argv[2]);validateDataset(d);
    if(cmd=="validate"){std::cout<<d["problems"].size()<<" problems, "<<d["samples"].size()<<" samples: valid; execution unverified\n";return 0;}
    if(cmd=="call"&&argc==6){
        // Explicit external account check, never inferred from API-key presence.
        auto approval=load(argv[5]);
        if(approval.at("service")!="https://tokenhub.tencentmaas.com/v1"||
           approval.at("no_out_of_allowance_charge")!=true||
           approval.at("confirmed_available_tokens").get<std::uint64_t>()<300000||
           approval.at("verified_by").get<std::string>().empty())
            throw std::runtime_error("account allowance confirmation missing");
        auto req=requestFor(d,argv[3]);
        // Development only until material execution and freeze are complete.
        bool dev=false;for(const auto&s:d["samples"])if(s["id"]==argv[3])for(const auto&p:d["problems"])
            if(p["id"]==s["problem_id"])dev=p["split"]=="development";
        if(!dev)throw std::runtime_error("formal calls require finalized freeze gate");
        auto prompt=makePrompt(req);
        fs::path root=argv[4];
        if(fs::weakly_canonical(root)!=fs::canonical("build/m3-development-20260827"))
            throw std::runtime_error("development calls must reuse the original campaign root and budget");
        if(fs::exists(root/"halt.json"))throw std::runtime_error("pilot halted; inspect evidence before new authorization");
        Budget budget(root/"budget");
        const std::string sampleId=argv[3];
        const std::string id=(v2?"v2-":"")+sampleId;
        // Official maximum input 192k; use binary-k and margin rather than a
        // guessed text/token conversion. Very conservative, may stop early.
        constexpr std::uint64_t upper=196608+16384+1024;
        const auto budgetState=budget.summary();
        if(v2) {
            if(sampleId!="s001"&&sampleId!="s002"&&sampleId!="s003")throw std::runtime_error("v2 validation sample not approved");
            if(budgetState["calls"].get<unsigned>()<3||budgetState["actual"].get<unsigned>()<36420)
                throw std::runtime_error("original pilot accounting missing");
        }
        if(budgetState["calls"].get<unsigned>()>=(v2?6U:3U))throw std::runtime_error("development validation call limit");
        if(!fs::create_directory(root/id))throw std::runtime_error("attempt already exists: no resend");
        saveNew(root/id/"request.json",{{"request",hy3::interactiveDiagnosisRequestJson(req)},{"prompt",prompt},{"prompt_sha256",hy3::sha256_hex(prompt)},
            {"dataset_sha256",hy3::sha256_hex(d.dump())},{"output_limit",16384},{"reserved_upper",upper}});
        budget.reserve(id,upper); // Durable before invoking; interrupted state is unknown, never zero.
        hy3::ProductionHttpTransport transport;hy3::Hy3ModelClientConfig cfg;cfg.max_tokens=16384;
        cfg.connect_timeout_ms=10000;cfg.read_timeout_ms=120000;cfg.total_timeout_ms=180000;
        hy3::Hy3ModelClient client(transport,cfg);
        auto result=client.invoke({id,prompt,hy3::sha256_hex(prompt)});
        budget.reconcile(id,result.token_usage);
        if(result.status!=hy3::ModelCallStatus::Succeeded||budget.summary().at("halt")==true||
           !result.token_usage||!result.token_usage->total_tokens)
            saveNew(root/"halt.json",{{"reason","infrastructure or accounting requires review"}});
        const std::string raw(result.raw_response.begin(),result.raw_response.end());
        std::ofstream rawFile(root/id/"raw-response.txt",std::ios::binary);rawFile<<raw;rawFile.close();
        if(!rawFile)throw std::runtime_error("raw write failed; no retry");
        auto record=parse(raw,req,schema);record["sample_id"]=sampleId;record["attempt_id"]=id;record["raw_sha256"]=hy3::sha256_hex(raw);
        if(result.status!=hy3::ModelCallStatus::Succeeded)record["parse_status"]="not_attempted";
        record["outcome"]=hy3::modelCallStatusName(result.status);record["duration_ms"]=result.duration_ms;
        record["model"]="hy3";record["model_version"]=nullptr;record["data_kind"]="development_real";
        record["http_status"]=result.http_status?json(*result.http_status):json(nullptr);
        record["provider_request_id"]=result.request_id?json(*result.request_id):json(nullptr);
        record["started_at"]=result.started_at;record["finished_at"]=result.finished_at;
        record["prompt_sha256"]=hy3::sha256_hex(prompt);record["dataset_sha256"]=hy3::sha256_hex(d.dump());
        record["prompt_template_id"]=v2?"hy3-greedy-evaluation-v2":"hy3-greedy-evaluation-v1";
        record["prompt_template_sha256"]=hy3::sha256_hex(readTemplate(templatePath));
        record["base_template_sha256"]=v2?json(nullptr):json(hy3::sha256_hex(readTemplate("prompts/hy3-interactive-diagnosis-v2.md")));
        record["extension_template_sha256"]=v2?json(nullptr):json(hy3::sha256_hex(readTemplate(templatePath)));
        record["finish_reason"]=result.finish_reason?json(*result.finish_reason):json(nullptr);
        record["usage_details"]={{"cached_tokens",result.token_usage&&result.token_usage->cached_tokens?json(*result.token_usage->cached_tokens):json(nullptr)},
            {"reasoning_tokens",result.token_usage&&result.token_usage->reasoning_tokens?json(*result.token_usage->reasoning_tokens):json(nullptr)}};
        record["token_usage"]=result.token_usage?json{{"prompt_tokens",result.token_usage->prompt_tokens?json(*result.token_usage->prompt_tokens):json(nullptr)},
            {"completion_tokens",result.token_usage->completion_tokens?json(*result.token_usage->completion_tokens):json(nullptr)},
            {"total_tokens",result.token_usage->total_tokens?json(*result.token_usage->total_tokens):json(nullptr)}}:json(nullptr);
        saveNew(root/id/"record.json",record);std::cout<<budget.summary().dump()<<"\n";return result.status==hy3::ModelCallStatus::Succeeded?0:1;
    }
    if(cmd=="export"&&argc==4){
        fs::path root=argv[3];if(!fs::create_directories(root))throw std::runtime_error("export directory already exists");
        json manifest={{"schema_version",schema},{"data_sha256",hy3::sha256_hex(d.dump())},
            {"prompt_template_sha256",hy3::sha256_hex(readTemplate(templatePath))},{"requests",json::array()}};
        if(!v2){manifest["base_template_sha256"]=hy3::sha256_hex(readTemplate("prompts/hy3-interactive-diagnosis-v2.md"));
            manifest["extension_sha256"]=manifest["prompt_template_sha256"];}
        for(const auto& s:d["samples"]){
            auto r=requestFor(d,s["id"]);
            auto prompt=makePrompt(r);
            saveNew(root/(s["id"].get<std::string>()+".json"),{{"request",hy3::interactiveDiagnosisRequestJson(r)},{"prompt",prompt},{"prompt_sha256",hy3::sha256_hex(prompt)}});
            manifest["requests"].push_back({{"request_id",r.request_id},{"sample_id",s["id"]},{"prompt_sha256",hy3::sha256_hex(prompt)}});
        }
        saveNew(root/"manifest.json",manifest);return 0;
    }
    if(cmd=="import"&&argc==6){
        auto r=requestFor(d,argv[3]);
        auto raw=read(argv[4]);auto result=parse(raw,r,schema);
        result["sample_id"]=argv[3];result["raw_sha256"]=hy3::sha256_hex(raw);
        result["candidate_answer_status"]="unverified";result["solution_answer_status"]="unverified";result["solution_process_status"]="unreviewed";
        saveNew(argv[5],result);return 0;
    }
    if(cmd=="report"&&(argc==5||argc==6)){
        auto bundle=load(argv[3]);
        if(bundle.at("data_kind")!="synthetic"&&bundle.at("data_kind")!="real")throw std::runtime_error("record provenance required");
        for(auto& r:bundle.at("records")){
            if(r.value("expected_schema_version",std::string(version))!=schema)
                throw std::runtime_error("record evaluation version mismatch; never mix campaigns");
            if(r.value("parse_status","")=="parsed"){
                auto req=requestFor(d,r.at("sample_id"));auto checked=parse(r.at("response").dump(),req,schema);
                if(checked.at("parse_status")!="parsed")throw std::runtime_error("record response failed strict validation");
            }
            for(auto key:{"candidate_answer_status","solution_answer_status"})
                if(r.value(key,"unverified")!="unverified")throw std::runtime_error("verified status requires evidence integration");
            if(r.value("solution_process_status","unreviewed")!="unreviewed")throw std::runtime_error("human review requires evidence integration");
        }
        auto records=bundle.at("records");
        if(argc==6)records=attachAnswerEvidence(d,records,load(argv[5]));
        auto summary=report(d,records,bundle.at("data_kind")=="synthetic");
        summary["evaluation_version"]=schema;
        saveNew(argv[4],summary);return 0;
    }
    if(cmd=="jobs"&&argc==4){
        json jobs=json::array();
        for(const auto& p:d["problems"])jobs.push_back({{"id",p["id"].get<std::string>()+"_reference"},{"problem_id",p["id"]},{"source",p["reference_code"]},{"tests",p["test_cases"]}});
        for(const auto& s:d["samples"])for(const auto& p:d["problems"])if(p["id"]==s["problem_id"])
            jobs.push_back({{"id",s["id"]},{"problem_id",p["id"]},{"source",s["code"]},{"tests",p["test_cases"]}});
        saveNew(argv[3],{{"schema_version","fixed-answer-jobs-v1"},{"dataset_sha256",hy3::sha256_hex(d.dump())},{"jobs",jobs}});
        return 0;
    }
    throw std::runtime_error("invalid command or arguments");
 }catch(const std::exception& e){std::cerr<<"E_EVALUATION: "<<e.what()<<"\n";return 1;}
}
