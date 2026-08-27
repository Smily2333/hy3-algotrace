// Fixed-material worker: run ONLY through run-isolated.sh, never on a normal host.
// No shell command construction. This is not an arbitrary-code security sandbox.
#include <nlohmann/json.hpp>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "independent_oracle.hpp"
#include "hy3_algotrace/sha256.hpp"
using json=nlohmann::json;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void write(const fs::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f<<s;f.flush();if(!f)throw std::runtime_error("write failed");}
json execute(const std::vector<std::string>& args,const std::string& input,int seconds){
 int pipes[2][2];if(pipe(pipes[0])||pipe(pipes[1]))throw std::runtime_error("pipe");
 auto start=Clock::now();pid_t pid=fork();
 if(pid<0)throw std::runtime_error("fork");
 if(pid==0){
    setpgid(0,0);
    rlimit cpu{static_cast<rlim_t>(seconds),static_cast<rlim_t>(seconds+1)},mem{512*1024*1024,512*1024*1024},size{2*1024*1024,2*1024*1024},core{0,0},procs{128,128};
    if(setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_AS,&mem)||setrlimit(RLIMIT_FSIZE,&size)||setrlimit(RLIMIT_CORE,&core)||setrlimit(RLIMIT_NPROC,&procs))_exit(125);
    int in=open(input.c_str(),O_RDONLY);if(in<0)_exit(125);
    dup2(in,0);dup2(pipes[0][1],1);dup2(pipes[1][1],2);close(in);
    for(auto&p:pipes){close(p[0]);close(p[1]);}
    std::vector<char*> argv;for(const auto&s:args)argv.push_back(const_cast<char*>(s.c_str()));argv.push_back(nullptr);
    char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C.UTF-8",tmp[]="TMPDIR=/work";
    char* env[]={path,lang,tmp,nullptr};execve(argv[0],argv.data(),env);_exit(126);
 }
 setpgid(pid,pid);
 close(pipes[0][1]);close(pipes[1][1]);
 for(auto&p:pipes)fcntl(p[0],F_SETFL,O_NONBLOCK);
 std::string out[2];bool openPipe[2]={true,true},done=false,limited=false,timed=false;int status=0;
 while(!done||openPipe[0]||openPipe[1]){
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
    if(ms>seconds*1000){timed=true;kill(-pid,SIGKILL);}
    for(int i=0;i<2;++i)if(openPipe[i]){
        char buf[4096];ssize_t n;
        while((n=read(pipes[i][0],buf,sizeof(buf)))>0){
            const auto room=65536-out[i].size();out[i].append(buf,std::min(room,static_cast<std::size_t>(n)));
            if(static_cast<std::size_t>(n)>room){limited=true;kill(-pid,SIGKILL);break;}
        }
        if(n==0||limited||timed){close(pipes[i][0]);openPipe[i]=false;}
    }
    if(!done&&waitpid(pid,&status,WNOHANG)==pid){done=true;kill(-pid,SIGKILL);}
    if(limited||timed){if(!done)waitpid(pid,&status,0);done=true;for(int i=0;i<2;++i)if(openPipe[i]){close(pipes[i][0]);openPipe[i]=false;}}
    if(!done||openPipe[0]||openPipe[1])poll(nullptr,0,2);
 }
 auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
 return {{"outcome",limited?"output_limit_exceeded":timed?"timeout":WIFEXITED(status)&&WEXITSTATUS(status)==0?"success":"execution_failed"},
 {"stdout",out[0]},{"stderr",out[1]},{"stdout_truncated",limited&&out[0].size()==65536},{"stderr_truncated",limited&&out[1].size()==65536},
 {"exit_code",WIFEXITED(status)?json(WEXITSTATUS(status)):json(nullptr)},{"signal",WIFSIGNALED(status)?json(WTERMSIG(status)):json(nullptr)},{"runtime_ms",ms}};
}
std::string norm(const std::string&s){std::string o;for(std::size_t i=0;i<s.size();++i){if(s[i]=='\r'&&i+1<s.size()&&s[i+1]=='\n'){o+='\n';++i;}else o+=s[i];}if(!o.empty()&&o.back()=='\n')o.pop_back();return o;}
int main(){
 try{
    // Fail closed outside the launcher mount namespace. This is a misuse guard,
    // not authentication; isolation is supplied by bubblewrap, not this marker.
    if(!fs::exists("/hy3-isolation-policy")||!fs::exists("/input.json")||fs::exists("/mnt/c")||fs::exists("/home"))
        throw std::runtime_error("approved isolated launcher required");
    fs::current_path("/work");write("empty","");
    json jobs;std::ifstream("/input.json")>>jobs;
    if(jobs.at("schema_version")!="fixed-answer-jobs-v1"||jobs.at("jobs").size()>100)throw std::runtime_error("jobs schema");
    auto identity=execute({"/usr/bin/g++","--version"},"empty",5);
    json report={{"schema_version","fixed-answer-results-v1"},{"dataset_sha256",jobs.at("dataset_sha256")},
        {"compiler",identity["stdout"]},{"compiler_path","/usr/bin/g++"},{"flags",json::array({"-std=c++17","-O2","-pipe"})},
        {"network","unshared"},{"environment","cleared"},{"results",json::array()}};
    for(const auto& j:jobs.at("jobs")){
        if(j.contains("problem_id"))for(const auto& t:j.at("tests")) {
            if(std::to_string(oracle(j.at("problem_id"),t.at("input")))!=norm(t.at("expected_output")))
                throw std::runtime_error("independent oracle disagrees with fixed expected output");
        }
        if(!j.at("source").is_string()||j.at("source").get<std::string>().size()>120000||j.at("tests").size()>100)throw std::runtime_error("job size");
        char name[]="/work/case-XXXXXX";char* p=mkdtemp(name);if(!p)throw std::runtime_error("temporary directory");
        const auto dir=fs::canonical(p);fs::current_path(dir);
        write("source.cpp",j.at("source"));write("empty","");
        auto compile=execute({"/usr/bin/g++","-std=c++17","-O2","-pipe","source.cpp","-o","candidate"},"empty",20);
        json row={{"id",j.at("id")},{"source_sha256",hy3::sha256_hex(j.at("source").get<std::string>())},{"independent_oracle_checked",j.contains("problem_id")},{"compile",compile},{"tests",json::array()},{"verdict","compile_error"}};
        if(compile["outcome"]=="success"){
            row["verdict"]="passed";
            for(const auto& t:j.at("tests")){
                if(t.at("input").get<std::string>().size()>1000000)throw std::runtime_error("input too large");
                write("stdin",t.at("input"));
                auto result=execute({(dir/"candidate").string()},"stdin",2);
                std::string verdict=result["outcome"]=="success"?(norm(result["stdout"])==norm(t["expected_output"])?"passed":"wrong_answer"):result["outcome"].get<std::string>();
                if(verdict!="passed"&&row["verdict"]=="passed")row["verdict"]=verdict;
                result["test_id"]=t["id"];result["verdict"]=verdict;result["expected_output"]=t["expected_output"];
                result["input_sha256"]=hy3::sha256_hex(t.at("input").get<std::string>());
                auto actual=norm(result["stdout"]),expected=norm(t["expected_output"]);std::size_t pos=0;
                while(pos<std::min(actual.size(),expected.size())&&actual[pos]==expected[pos])++pos;
                result["first_difference_byte"]=actual==expected?json(nullptr):json(pos);
                row["tests"].push_back(result);
            }
        }else if(compile["outcome"]=="timeout"||compile["outcome"]=="output_limit_exceeded")row["verdict"]=compile["outcome"];
        report["results"].push_back(row);
        fs::current_path("/work");
        if(dir.parent_path()!=fs::path("/work")||dir.filename().string().rfind("case-",0)!=0)throw std::runtime_error("cleanup containment");
        fs::remove_all(dir); // Only mkdtemp-owned directory inside ephemeral /work.
    }
    std::cout<<report.dump(2)<<"\n";
 }catch(const std::exception&e){std::cerr<<"runner_error: "<<e.what()<<"\n";return 1;}
}
