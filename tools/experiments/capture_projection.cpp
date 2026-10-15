// Capture a real model projection for a private, reproducible accelerator probe.
// Weight/activation/output files belong in ignored evidence/, never in Git.
#include "graph.h"
#include "frame.h"
#include "platform.h"
#include "backend_options.h"
#include "fp8_weights.h"
#include <iostream>
using namespace phoenix_cpp;
template<class T> void save(const std::filesystem::path& p,const std::vector<T>& v) {
    std::ofstream f(p,std::ios::binary);
    demand(bool(f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T))),"Capture write failed");
}
int main(int argc,char** argv) {
    try {
        demand(argc==3,"Usage: capture_projection JOB.json NEW_OUTPUT_DIRECTORY");
        Json job;std::ifstream(argv[1])>>job;auto cfg=job.at("config");
        std::filesystem::path out=argv[2],root=cfg.at("runtime_root").get<std::string>();
        demand(!std::filesystem::exists(out),"Fresh output folder required");
        auto module=open_library(root/library_name("phoenix_vulkan_cpp"));demand(module,"Vulkan backend unavailable");
        auto create=reinterpret_cast<Backend*(*)(const char*,const char*,uint32_t,char*,size_t)>(library_symbol(module,"phoenix_backend_create_utf8"));
        auto destroy=reinterpret_cast<DestroyBackend>(library_symbol(module,"phoenix_backend_destroy"));
        demand(create&&destroy,"Backend factory missing");char error[2048]{};
        auto device=cfg.at("device").get<std::string>(),shaders=(root/"vulkan").string();
        std::unique_ptr<Backend,DestroyBackend> gpu(create(device.c_str(),shaders.c_str(),VulkanWorkgroupCompatibility|VulkanTiledFallback|VulkanCoalescedFallback,error,sizeof error),destroy);
        demand(bool(gpu),error);
        demand(job.at("width")==320&&job.at("height")==144,"320x144 fixture required");
        std::vector<float> color(320*144*3);std::ifstream in(job.at("color").get<std::string>(),std::ios::binary);
        demand(bool(in.read(reinterpret_cast<char*>(color.data()),color.size()*4)),"Input unavailable");
        FrameState frame(320,144,320,320,4);std::vector<float> prior,post;std::vector<uint8_t> valid,post_valid;
        frame.warp(nullptr,nullptr,nullptr,false,prior,post,valid,post_valid);
        auto input=frame.features(color.data(),Controls::from(job.at("metadata").at("controls")),prior,valid);
        Model model(phoenix_model::WeightSource::config(cfg.at("weight_source")));
        {
            Graph graph(*gpu,model,false,512,false,false);graph.build(320,320,std::array<unsigned,8>{1,1,1,1,1,1,1,1});model.discard_decoded();
            gpu->upload({graph.input.ptr(),input.size()*2,graph.input.memory->token},input.data(),input.size()*2);
            size_t index=0;
            for(;index<graph.commands.size();++index){auto& op=graph.commands[index].operation;
                if(graph.commands[index].block==0&&op.family==phx::MatrixMultiply&&(op.flags&phx::RightInputFp8)&&(op.flags&1u)&&op.batch_count<=1&&op.reduction_count>=32)break;}
            demand(index<graph.commands.size(),"No eligible block-0 projection");
            std::vector<Event> events;
            if(index)gpu->execute({graph.commands.begin(),graph.commands.begin()+index},events);
            auto cmd=graph.commands[index];auto op=cmd.operation;
            demand(index+2<graph.commands.size(),"FFN sequence truncated");
            const auto contract=graph.commands[index+1].operation,residual=graph.commands[index+2].operation;
            unsigned m=op.row_count,n=op.column_count,k=op.reduction_count;
            auto get=[&](void* address,size_t bytes,void* destination){gpu->download({address,bytes,uint64_t(uintptr_t(address)>>40)},destination,bytes);};
            unsigned lda=op.left_row_stride?op.left_row_stride:k,ldb=op.right_row_stride?op.right_row_stride:k;
            std::vector<uint16_t> input_strided(size_t(m)*lda),a(size_t(m)*k),weights(size_t(n)*k);
            std::vector<uint8_t> packed(size_t(n)*ldb);get(op.left_input,input_strided.size()*2,input_strided.data());get(op.right_input,packed.size(),packed.data());
            for(unsigned r=0;r<m;++r)std::copy_n(input_strided.data()+size_t(r)*lda,k,a.data()+size_t(r)*k);
            for(unsigned r=0;r<n;++r)for(unsigned c=0;c<k;++c)weights[size_t(r)*k+c]=fp8_weight_half(packed[size_t(r)*ldb+c]);
            demand(contract.family==phx::MatrixMultiply&&contract.reduction_count==n&&contract.column_count==k&&
                   residual.family==phx::Elementwise&&(residual.flags&255u)==phx::ScaledResidual&&
                   ((op.flags>>8u)&15u)==phx::PublishFusedGateFp8,"Expected block-0 expand/gate/contract/residual sequence");
            std::vector<uint8_t> contract_packed(size_t(k)*n);
            std::vector<uint16_t> contract_weights(contract_packed.size()),skip(size_t(m)*k);
            std::vector<float> gain(k);
            get(contract.right_input,contract_packed.size(),contract_packed.data());
            for(size_t i=0;i<contract_packed.size();++i)contract_weights[i]=fp8_weight_half(contract_packed[i]);
            demand(residual.flags&phx::RightInputHalf,"Expected half residual skip");
            get(residual.right_input,skip.size()*2,skip.data());get(residual.auxiliary,gain.size()*4,gain.data());
            // Raw matrix output deliberately excludes Phoenix publication, scales
            // and residuals: this isolates QNN MatMul support and numerical error.
            std::vector<float> reference(size_t(m)*n);auto c=gpu->allocate(reference.size()*4);
            op.output=c.address;op.flags=1u|phx::RightInputFp8;op.output_row_stride=n;op.output_batch_stride=m*n;op.auxiliary=nullptr;op.secondary_output=nullptr;
            cmd.operation=op;cmd.count=reference.size();std::vector<double> timings;
            for(unsigned i=0;i<9;++i){events.clear();gpu->execute({cmd},events);double ms=0;for(auto event:events)ms+=event.milliseconds;if(i>=2)timings.push_back(ms);}
            gpu->download(c,reference.data(),reference.size()*4);gpu->release(c);
            std::vector<double> ffn_timings;
            for(unsigned i=0;i<9;++i){
                // Scratch lifetimes allow later outputs to reuse earlier inputs.
                // Restore both operands before every isolated subgraph replay.
                auto original=graph.commands[index].operation;
                gpu->upload({original.left_input,input_strided.size()*2,uint64_t(uintptr_t(original.left_input)>>40)},input_strided.data(),input_strided.size()*2);
                gpu->upload({residual.right_input,skip.size()*2,uint64_t(uintptr_t(residual.right_input)>>40)},skip.data(),skip.size()*2);
                events.clear();gpu->execute({graph.commands.begin()+index,graph.commands.begin()+index+3},events);double ms=0;for(auto event:events)ms+=event.milliseconds;if(i>=2)ffn_timings.push_back(ms);}
            std::vector<float> ffn_reference(size_t(m)*k);get(residual.output,ffn_reference.size()*4,ffn_reference.data());
            std::filesystem::create_directories(out);save(out/"input.f16",a);save(out/"weights.f16",weights);save(out/"reference.f32",reference);
            Json info={{"block",0},{"command_index",index},{"m",m},{"n",n},{"k",k},{"operation","MatMul A * transpose(B)"},{"input","input.f16"},{"weights","weights.f16"},{"reference","reference.f32"},{"vulkan_gpu_ms",timings},{"scope","Actual block-0 projection operands; raw matrix only, not a full Phoenix block or end-to-end inference"}};
            save(out/"contract.f16",contract_weights);save(out/"skip.f16",skip);save(out/"gain.f32",gain);save(out/"ffn-reference.f32",ffn_reference);
            info["ffn"]={{"contract","contract.f16"},{"skip","skip.f16"},{"gain","gain.f32"},{"reference","ffn-reference.f32"},{"vulkan_gpu_ms",ffn_timings},{"scope","Block-0 feed-forward residual subgraph: expand + polynomial gate/FP8 publication + contract + learned residual; excludes window attention"}};
            std::ofstream(out/"projection.json")<<info.dump(2);std::cout<<info.dump()<<std::endl;
        }
        gpu.reset();close_library(module);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
