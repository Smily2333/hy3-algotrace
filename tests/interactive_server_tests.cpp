#include "hy3_algotrace/interactive_server.hpp"
#include "interactive_v2_fixture.hpp"
#include <iostream>

namespace {
using namespace hy3;
using namespace interactive_fixture;
int passed=0,failed=0;
#define CHECK(c,m) do { if(c) ++passed; else { ++failed; std::cerr<<"FAIL "<<__LINE__<<": "<<m<<'\n'; } } while(0)

// Only the explicitly invoked test harness uses this fixed script. No analysis,
// source-pattern matching, network client or API credential is involved.
class ScriptedBrowserFake final : public IModelClient {
    std::size_t sequence_=0;
public:
    ModelCallResult invoke(const ModelRequest& request) noexcept override {
        try {
            const auto scene=sequence_++ % 6;
            std::cout<<"Fake fixture invocation "<<sequence_<<std::endl;
            auto d=diagnosis(request.trace_id,scene==1?"correct":scene==2?"undetermined":"incorrect");
            if(scene==5) d["summary"]="<img src=x onerror=alert('unsafe')> Fake text must not become HTML.";
            auto call=success(d);
            if(scene==3) call.raw_response={'{'};
            if(scene==4) {call.status=ModelCallStatus::Timeout;call.raw_response.clear();call.http_status.reset();}
            FakeModelClient fake(call);
            return fake.invoke(request);
        } catch(...) {
            ModelCallResult call;call.status=ModelCallStatus::ProviderError;return call;
        }
    }
};
class RecordedReplay final : public IModelClient {
    json diagnosis_;
public:
    explicit RecordedReplay(json diagnosis):diagnosis_(std::move(diagnosis)) {}
    ModelCallResult invoke(const ModelRequest& request) noexcept override {
        auto diagnosis=diagnosis_;diagnosis["request_id"]=request.trace_id;
        const auto raw=diagnosis.dump();ModelCallResult result;
        result.raw_response.assign(raw.begin(),raw.end());result.status=ModelCallStatus::Succeeded;result.http_status=200;
        return result;
    }
};
}
int main(int argc,char** argv) {
    try {
        if(argc==6 && std::string(argv[1])=="--serve-replay") {
            const fs::path repo=argv[2];const auto port=std::stoul(argv[4]);
            if(port==0 || port>65535) return 2;
            const auto bundle=json::parse(readText(argv[5]));const json* selected=nullptr;
            for(const auto& record:bundle.at("records"))if(record.at("sample_id")=="s005"&&record.at("parse_status")=="parsed")selected=&record;
            if(!selected)throw std::runtime_error("s005 replay record unavailable");
            RecordedReplay replay(selected->at("response").at("diagnosis"));
            InteractiveServerConfig config;config.port=static_cast<std::uint16_t>(port);
            config.web_root=repo/"web";config.artifacts_root=argv[3];
            InteractiveHttpApplication app(replay,readText(repo/"prompts/hy3-interactive-diagnosis-v2.md"),
                                           config.artifacts_root.string(),false,false,true);
            std::cout<<"RECORDED HY3 RESULT REPLAY - no network or code execution\n"
                     <<"URL: http://127.0.0.1:"<<port<<"/\n"<<std::flush;
            std::string error;if(!serveInteractiveDemo(config,app,error)){std::cerr<<error<<'\n';return 1;}return 0;
        }
        if(argc==5 && std::string(argv[1])=="--serve-fake") {
            const fs::path repo=argv[2];
            const auto port=std::stoul(argv[4]);
            if(port==0 || port>65535) return 2;
            ScriptedBrowserFake fake;
            InteractiveServerConfig config;config.port=static_cast<std::uint16_t>(port);
            config.web_root=repo/"web";config.artifacts_root=argv[3];
            InteractiveHttpApplication app(fake,readText(repo/"prompts/hy3-interactive-diagnosis-v2.md"),
                                           config.artifacts_root.string(),false,true);
            std::cout<<"MOCK / FAKE ONLY - no model, credentials, or code execution\n"
                     <<"Scenes: incorrect / correct / undetermined / invalid JSON / timeout / HTML-as-text\n"
                     <<"URL: http://127.0.0.1:"<<port<<"/\n"<<std::flush;
            std::string error;
            if(!serveInteractiveDemo(config,app,error)){std::cerr<<error<<'\n';return 1;}
            return 0;
        }
        if(argc!=2){std::cerr<<"repo root required, or --serve-fake <repo> <local-artifacts-root> <port>\n";return 2;}
        const auto prompt=readText(fs::path(argv[1])/"prompts/hy3-interactive-diagnosis-v2.md");
        OwnedRoot root;FakeModelClient fake(success(diagnosis("http")));
        InteractiveHttpApplication app(fake,prompt,root.path.string(),false,true);
        const auto health=json::parse(app.health().body);
        CHECK(health["ok"]==true && health["model_mode"]=="mock_fixture" && fake.callCount()==0,"health honest/no call");
        CHECK(health["prompt_template_id"]==kInteractiveTemplateId &&
              health["request_schema_version"]==kInteractiveRequestVersion,"health v2 identity");
        OwnedRoot replayRoot;RecordedReplay replay(diagnosis("replay"));
        InteractiveHttpApplication replayApp(replay,prompt,replayRoot.path.string(),false,false,true);
        const auto replayHealth=json::parse(replayApp.health().body);
        CHECK(replayHealth["model_mode"]=="recorded_replay"&&replayHealth["model_name"]=="hy3-recorded","replay mode honest");
        CHECK(app.diagnose("text/plain","{}").status==415 && fake.callCount()==0,"content type");
        CHECK(app.diagnose("application/json","{").status==400 && fake.callCount()==0,"invalid JSON before call");
        CHECK(app.diagnose("application/json",std::string(256*1024+1,'x')).status==413 &&
              fake.callCount()==0,"body limit before call");
        for(const char* field:{"cpp_solution","problem_statement"}) {
            for(const json& value:std::vector<json>{nullptr,""," \n\t",json::array(),7}) {
                auto invalid=request("http");invalid[field]=value;
                CHECK(app.diagnose("application/json",invalid.dump()).status==400 && fake.callCount()==0,
                      "invalid input before model");
            }
            auto invalid=request("http");invalid.erase(field);
            CHECK(app.diagnose("application/json",invalid.dump()).status==400 && fake.callCount()==0,"missing before call");
            invalid=request("http");invalid[field]=std::string(120001,'x');
            CHECK(app.diagnose("application/json",invalid.dump()).status==400 && fake.callCount()==0,"field limit before call");
        }
        const auto reply=app.diagnose("application/json; charset=utf-8",request("http").dump());
        const auto body=json::parse(reply.body);
        CHECK(reply.status==200 && body["ok"]==true && fake.callCount()==1,"minimal HTTP Fake chain");
        CHECK(body["diagnosis"]==diagnosis("http"),"all new fields through browser JSON");
        CHECK(!body.contains("raw_response") && body["metadata"]["response_schema_version"]==kInteractiveResponseVersion,"safe wrapper v2");
        CHECK(app.diagnose("application/json",request("http").dump()).status==409 && fake.callCount()==1,"duplicate single call");
        for(const auto status:{ModelCallStatus::AuthenticationError,ModelCallStatus::Timeout,
                               ModelCallStatus::RateLimited,ModelCallStatus::TransportError}) {
            OwnedRoot errors;ModelCallResult call;call.status=status;
            call.message="Authorization: Bearer synthetic-secret";FakeModelClient f(call);
            InteractiveHttpApplication failing(f,prompt,errors.path.string(),true);
            const auto error=failing.diagnose("application/json",request("failure").dump());
            const auto e=json::parse(error.body);
            CHECK(error.status>=400 && !e["ok"].get<bool>() && e["diagnosis"].is_null() && f.callCount()==1,"failure never success");
            CHECK(error.body.find("synthetic-secret")==std::string::npos &&
                  error.body.find("Bearer")==std::string::npos,"secret detail not returned");
        }
        OwnedRoot invalidRoot;auto call=success(diagnosis("invalid"));call.raw_response={'{'};
        FakeModelClient invalid(call);InteractiveHttpApplication invalidApp(invalid,prompt,invalidRoot.path.string(),false);
        const auto error=json::parse(invalidApp.diagnose("application/json",request("invalid").dump()).body);
        CHECK(error["parse_status"]=="invalid_json" && invalid.callCount()==1,"invalid JSON unchanged/no retry");
        InteractiveHttpApplication v1(fake,"# hy3-interactive-diagnosis-v1\n{{interactive_request_json}}",root.path.string(),false);
        CHECK(json::parse(v1.health().body)["ok"]==false,"v1 template not healthy");
        InteractiveServerConfig config;config.host="0.0.0.0";std::string safe;
        CHECK(!serveInteractiveDemo(config,app,safe),"public bind prohibited before socket");
    } catch(const std::exception& e){++failed;std::cerr<<e.what()<<'\n';}
    std::cout<<"interactive_server_tests: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed==0?0:1;
}
