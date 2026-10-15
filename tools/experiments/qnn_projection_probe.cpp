#include "qnn_probe_common.h"
int main(int argc,char** argv){
    try{
        if(argc!=5)throw std::runtime_error("Usage: qnn_projection_probe CAPTURE_DIRECTORY NEW_OUTPUT_DIRECTORY QNN_SOC_MODEL fp16|w8a16");
        std::filesystem::path in=argv[1],out=argv[2];if(std::filesystem::exists(out))throw std::runtime_error("Fresh output directory required");
        Json spec;std::ifstream(in/"projection.json")>>spec;
        unsigned m=spec.at("m"),n=spec.at("n"),k=spec.at("k");if(!m||!n||!k||uint64_t(m)*n>100000000||uint64_t(m)*k>100000000||uint64_t(n)*k>100000000)throw std::runtime_error("Invalid projection extent");
        auto a=read<uint16_t>(in/spec.at("input").get<std::string>(),size_t(m)*k),b=read<uint16_t>(in/spec.at("weights").get<std::string>(),size_t(n)*k);
        std::string precision=argv[4];if(precision!="fp16"&&precision!="w8a16")throw std::runtime_error("Unknown precision");bool quantized=precision=="w8a16";
        std::vector<uint16_t> aq(a.size());std::vector<uint8_t> bq(b.size());float as=0,bs=0,cs=0;
        if(quantized){
            as=range_scale(a,32767);bs=range_scale(b,127);
            auto reference=read<float>(in/spec.at("reference").get<std::string>(),size_t(m)*n);for(float value:reference)cs=std::max(cs,std::abs(value));cs=std::max(cs/32767,1e-8f);
            for(size_t i=0;i<a.size();++i)aq[i]=uint16_t(std::clamp(std::nearbyint(half_value(a[i])/as)+32768.0f,0.0f,65535.0f));
            for(size_t i=0;i<b.size();++i)bq[i]=uint8_t(std::clamp(std::nearbyint(half_value(b[i])/bs)+128.0f,0.0f,255.0f));
        }
        std::vector<uint16_t> result(size_t(m)*n);
        unsigned soc=std::stoul(argv[3]);Runtime runtime;runtime.initialize(soc);auto& api=runtime.api;
        QnnHtpGraph_CustomConfig_t fp16=QNN_HTP_GRAPH_CUSTOM_CONFIG_INIT;fp16.option=QNN_HTP_GRAPH_CONFIG_OPTION_PRECISION;fp16.precision=QNN_PRECISION_FLOAT16;
        QnnGraph_Config_t config=QNN_GRAPH_CONFIG_INIT;config.option=QNN_GRAPH_CONFIG_OPTION_CUSTOM;config.customConfig=&fp16;const QnnGraph_Config_t* configs[]={&config,nullptr};
        Qnn_GraphHandle_t graph=nullptr;check(api.graphCreate(runtime.context,"phoenix_block0_projection",quantized?nullptr:configs,&graph),"graphCreate");
        uint32_t adims[]={m,k},bdims[]={n,k},cdims[]={m,n};
        auto input=tensor("activation",QNN_TENSOR_TYPE_APP_WRITE,adims,a.data(),a.size()*2);
        auto weight=tensor("weight",QNN_TENSOR_TYPE_STATIC,bdims,b.data(),b.size()*2);
        auto output=tensor("projection",QNN_TENSOR_TYPE_APP_READ,cdims,result.data(),result.size()*2);
        if(quantized){
            quantization(input,QNN_DATATYPE_UFIXED_POINT_16,as,-32768);quantization(weight,QNN_DATATYPE_UFIXED_POINT_8,bs,-128);quantization(output,QNN_DATATYPE_UFIXED_POINT_16,cs,-32768);
            weight.v1.clientBuf={bq.data(),uint32_t(bq.size())};
        }
        input.v1.clientBuf={nullptr,0};output.v1.clientBuf={nullptr,0};
        for(auto* t:{&input,&weight,&output})check(api.tensorCreateGraphTensor(graph,t),"tensorCreateGraphTensor");
        Qnn_Param_t transpose=QNN_PARAM_INIT;transpose.paramType=QNN_PARAMTYPE_SCALAR;transpose.name=QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN1;transpose.scalarParam.dataType=QNN_DATATYPE_BOOL_8;transpose.scalarParam.bool8Value=1;
        Qnn_Tensor_t inputs[]={input,weight};Qnn_OpConfig_t op=QNN_OPCONFIG_INIT;op.v1.name="block0_projection";op.v1.packageName=QNN_OP_PACKAGE_NAME_QTI_AISW;op.v1.typeName=QNN_OP_MAT_MUL;op.v1.numOfParams=1;op.v1.params=&transpose;op.v1.numOfInputs=2;op.v1.inputTensors=inputs;op.v1.numOfOutputs=1;op.v1.outputTensors=&output;
        check(api.backendValidateOpConfig(runtime.backend,op),"validate MatMul");check(api.graphAddNode(graph,op),"graphAddNode");
        auto start=Clock::now();check(api.graphFinalize(graph,nullptr,nullptr),"graphFinalize");double prepare=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        input.v1.clientBuf={quantized?aq.data():a.data(),uint32_t(a.size()*2)};
        output.v1.clientBuf={result.data(),uint32_t(result.size()*2)};
        std::vector<double> samples;
        for(unsigned i=0;i<22;++i){start=Clock::now();check(api.graphExecute(graph,&input,1,&output,1,nullptr,nullptr),"graphExecute");double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();if(i>=2)samples.push_back(ms);}
        const char* output_file=quantized?"output.u16":"output.f16";
        std::filesystem::create_directories(out);std::ofstream f(out/output_file,std::ios::binary);f.write(reinterpret_cast<const char*>(result.data()),result.size()*2);if(!f)throw std::runtime_error("Output write failed");
        auto sorted=samples;std::sort(sorted.begin(),sorted.end());Json report={{"backend","QNN HTP"},{"precision",precision},{"soc_model",soc},{"m",m},{"n",n},{"k",k},{"graph_finalize_ms",prepare},{"warm_call_ms",samples},{"median_call_ms",(sorted[9]+sorted[10])/2},{"cpu_fallback",false},{"scope","Actual Phoenix block-0 projection only; complete QNN graphExecute calls include host-plane I/O; not full-model or app FPS"}};
        if(quantized)report["quantization"]={{"input_scale",as},{"weight_scale",bs},{"output_scale",cs},{"output_zero_point",32768},{"calibration","Absolute maxima of this same projection fixture, including reference output; no independent quality evaluation"}};
        report["output_file"]=output_file;
        std::ofstream(out/"result.json")<<report.dump(2);std::cout<<report.dump()<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
