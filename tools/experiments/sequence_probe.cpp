// Repeat real frame calls with history enabled; raw receipts remain private.
#include "phoenix.h"
#include "platform.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
using Json=nlohmann::json;
using namespace phoenix_cpp;
std::vector<float> plane(const Json& job,const char* key,size_t count){
    if(!job.contains(key))return {};std::vector<float> v(count);std::ifstream f(job.at(key).get<std::string>(),std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(v.data()),count*4)||f.peek()!=EOF)throw std::runtime_error("Plane size mismatch");return v;
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: sequence_probe JOB.json NEW_OUTPUT_DIRECTORY");
    Json job;std::ifstream(argv[1])>>job;std::filesystem::path out=argv[2];if(std::filesystem::exists(out))throw std::runtime_error("Fresh output directory required");
    unsigned w=job.at("width"),h=job.at("height");if(!w||!h||w>8192||h>8192)throw std::runtime_error("Invalid extent");size_t pixels=size_t(w)*h;
    auto input=plane(job,"color",pixels*3),motion=plane(job,"motion",pixels*2);if(input.empty())throw std::runtime_error("Color required");std::vector<float> output(input.size());
    auto root=std::filesystem::path(job.at("config").at("runtime_root").get<std::string>());auto module=open_library(root/library_name("phoenix_runtime_cpp"));if(!module)throw std::runtime_error("Runtime missing");
    auto create=reinterpret_cast<decltype(&phoenix_runtime_create)>(library_symbol(module,"phoenix_runtime_create"));auto frame=reinterpret_cast<decltype(&phoenix_runtime_frame)>(library_symbol(module,"phoenix_runtime_frame"));
    auto destroy=reinterpret_cast<decltype(&phoenix_runtime_destroy)>(library_symbol(module,"phoenix_runtime_destroy"));auto error=reinterpret_cast<decltype(&phoenix_runtime_error)>(library_symbol(module,"phoenix_runtime_error"));auto status=reinterpret_cast<decltype(&phoenix_runtime_status)>(library_symbol(module,"phoenix_runtime_status"));
    if(!create||!frame||!destroy||!error||!status)throw std::runtime_error("Incomplete C API");
    auto config=job.at("config").dump();void* session=create(config.c_str(),w,h);if(!session)throw std::runtime_error(error());
    struct Cleanup{void* session;decltype(&phoenix_runtime_destroy) destroy;~Cleanup(){destroy(session);}} cleanup{session,destroy};
    std::filesystem::create_directories(out);
    for(unsigned i=0;i<5;++i){auto meta=job.at("metadata");meta["reset"]=i==0;meta["has_motion"]=!motion.empty();auto text=meta.dump();
        auto start=std::chrono::steady_clock::now();if(frame(session,input.data(),motion.empty()?nullptr:motion.data(),nullptr,nullptr,text.c_str(),output.data()))throw std::runtime_error(error());
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::vector<char> buffer(status(session,nullptr,0));status(session,buffer.data(),buffer.size());auto row=Json::parse(buffer.data());row["sequence_frame"]=i;row["call_ms"]=ms;
        std::cout<<row.dump()<<std::endl;std::ofstream f(out/(std::to_string(i)+".rgb32f"),std::ios::binary);if(!f.write(reinterpret_cast<char*>(output.data()),output.size()*4))throw std::runtime_error("Output write failed");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
