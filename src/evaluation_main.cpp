#include "hy3_algotrace/evaluation.hpp"
#include "hy3_algotrace/sha256.hpp"
#include "hy3_algotrace/hy3_model_client.hpp"
#include "hy3_algotrace/production_http_transport.hpp"
#include "hy3_algotrace/model_runner.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
using namespace hy3::evaluation;
namespace fs=std::filesystem;
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("file unavailable");return {std::istreambuf_iterator<char>(f),{}};}
std::string readTemplate(const fs::path& p){auto raw=read(p);std::vector<std::uint8_t> out;std::string error;
    if(!hy3::normalizeUtf8({raw.begin(),raw.end()},out,error))throw std::runtime_error("template encoding");
    return {out.begin(),out.end()};}
int main(int argc,char**argv){
 try{
    if(argc<3){std::cout<<"hy3_evaluate validate DATASET | export[-v2] DATASET ROOT | import[-v2] DATASET SAMPLE RAW OUT | report[-v2] DATASET RECORDS OUT [EVIDENCE] | jobs DATASET OUT\n"
        <<"Paid, authorization required: call[-v2] DATASET SAMPLE CAMPAIGN_ROOT ACCOUNT_CONFIRMATION\n";return 1;}
    std::string cmd=argv[1];
    const bool v2=cmd.size()>3&&cmd.substr(cmd.size()-3)=="-v2";
    if(v2)cmd.resize(cmd.size()-3);
    const std::string schema=v2?version2:version;
    const std::string templatePath=v2?"prompts/hy3-greedy-evaluation-v2.md":"prompts/hy3-greedy-evaluation-v1.md";
    auto makePrompt=[&](const hy3::InteractiveDiagnosisRequest& r){return v2?renderV2(r,readTemplate(templatePath)):
        render(r,readTemplate("prompts/hy3-interactive-diagnosis-v2.md"),readTemplate(templatePath));};
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
