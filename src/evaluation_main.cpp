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
    if(argc<3){std::cout<<"hy3_evaluate validate DATASET | export DATASET ROOT | import DATASET SAMPLE RAW OUT | report DATASET RECORDS OUT | jobs DATASET OUT\n";return 1;}
    std::string cmd=argv[1];auto d=load(argv[2]);validateDataset(d);
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
        auto prompt=render(req,readTemplate("prompts/hy3-interactive-diagnosis-v2.md"),readTemplate("prompts/hy3-greedy-evaluation-v1.md"));
        fs::path root=argv[4];fs::create_directories(root);
        if(fs::exists(root/"halt.json"))throw std::runtime_error("pilot halted; inspect evidence before new authorization");
        Budget budget(root/"budget");
        const std::string id=argv[3];
        // Official maximum input 192k; use binary-k and margin rather than a
        // guessed text/token conversion. Very conservative, may stop early.
        constexpr std::uint64_t upper=196608+16384+1024;
        if(budget.summary()["calls"].get<unsigned>()>=3)throw std::runtime_error("initial development pilot limit");
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
        auto record=parse(raw,req);record["sample_id"]=id;record["raw_sha256"]=hy3::sha256_hex(raw);
        if(result.status!=hy3::ModelCallStatus::Succeeded)record["parse_status"]="not_attempted";
        record["outcome"]=hy3::modelCallStatusName(result.status);record["duration_ms"]=result.duration_ms;
        record["model"]="hy3";record["model_version"]=nullptr;record["data_kind"]="development_real";
        record["http_status"]=result.http_status?json(*result.http_status):json(nullptr);
        record["provider_request_id"]=result.request_id?json(*result.request_id):json(nullptr);
        record["started_at"]=result.started_at;record["finished_at"]=result.finished_at;
        record["prompt_sha256"]=hy3::sha256_hex(prompt);record["dataset_sha256"]=hy3::sha256_hex(d.dump());
        record["prompt_template_id"]="hy3-greedy-evaluation-v1";
        record["base_template_sha256"]=hy3::sha256_hex(readTemplate("prompts/hy3-interactive-diagnosis-v2.md"));
        record["extension_template_sha256"]=hy3::sha256_hex(readTemplate("prompts/hy3-greedy-evaluation-v1.md"));
        record["token_usage"]=result.token_usage?json{{"prompt_tokens",result.token_usage->prompt_tokens?json(*result.token_usage->prompt_tokens):json(nullptr)},
            {"completion_tokens",result.token_usage->completion_tokens?json(*result.token_usage->completion_tokens):json(nullptr)},
            {"total_tokens",result.token_usage->total_tokens?json(*result.token_usage->total_tokens):json(nullptr)}}:json(nullptr);
        saveNew(root/id/"record.json",record);std::cout<<budget.summary().dump()<<"\n";return result.status==hy3::ModelCallStatus::Succeeded?0:1;
    }
    if(cmd=="export"&&argc==4){
        fs::path root=argv[3];if(!fs::create_directories(root))throw std::runtime_error("export directory already exists");
        auto base=readTemplate("prompts/hy3-interactive-diagnosis-v2.md"),ext=readTemplate("prompts/hy3-greedy-evaluation-v1.md");
        json manifest={{"schema_version",version},{"data_sha256",hy3::sha256_hex(d.dump())},{"base_template_sha256",hy3::sha256_hex(base)},{"extension_sha256",hy3::sha256_hex(ext)},{"requests",json::array()}};
        for(const auto& s:d["samples"]){
            auto r=requestFor(d,s["id"]);
            auto prompt=render(r,base,ext);
            saveNew(root/(s["id"].get<std::string>()+".json"),{{"request",hy3::interactiveDiagnosisRequestJson(r)},{"prompt",prompt},{"prompt_sha256",hy3::sha256_hex(prompt)}});
            manifest["requests"].push_back({{"request_id",r.request_id},{"sample_id",s["id"]},{"prompt_sha256",hy3::sha256_hex(prompt)}});
        }
        saveNew(root/"manifest.json",manifest);return 0;
    }
    if(cmd=="import"&&argc==6){
        auto r=requestFor(d,argv[3]);
        auto raw=read(argv[4]);auto result=parse(raw,r);
        result["sample_id"]=argv[3];result["raw_sha256"]=hy3::sha256_hex(raw);
        result["candidate_answer_status"]="unverified";result["solution_answer_status"]="unverified";result["solution_process_status"]="unreviewed";
        saveNew(argv[5],result);return 0;
    }
    if(cmd=="report"&&(argc==5||argc==6)){
        auto bundle=load(argv[3]);
        if(bundle.at("data_kind")!="synthetic"&&bundle.at("data_kind")!="real")throw std::runtime_error("record provenance required");
        for(auto& r:bundle.at("records")){
            if(r.value("parse_status","")=="parsed"){
                auto req=requestFor(d,r.at("sample_id"));auto checked=parse(r.at("response").dump(),req);
                if(checked.at("parse_status")!="parsed")throw std::runtime_error("record response failed strict validation");
            }
            for(auto key:{"candidate_answer_status","solution_answer_status"})
                if(r.value(key,"unverified")!="unverified")throw std::runtime_error("verified status requires evidence integration");
            if(r.value("solution_process_status","unreviewed")!="unreviewed")throw std::runtime_error("human review requires evidence integration");
        }
        auto records=bundle.at("records");
        if(argc==6)records=attachAnswerEvidence(d,records,load(argv[5]));
        saveNew(argv[4],report(d,records,bundle.at("data_kind")=="synthetic"));return 0;
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
