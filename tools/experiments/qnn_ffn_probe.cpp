// Entire block-0 feed-forward residual island on HTP, with linear W8A16
// quantization replacing the reference's E4M3/half publication grids.
// Calibration uses this same private fixture: this is a feasibility prototype.
#include "qnn_probe_common.h"
#include <deque>
#include <array>
#include <map>

struct TensorStorage {
    std::string name;std::array<uint32_t,2> dims{};
    std::vector<uint16_t> u16;std::vector<uint8_t> u8;Qnn_Tensor_t value{};
};
struct Builder {
    QNN_INTERFACE_VER_TYPE& api;Qnn_BackendHandle_t backend;Qnn_GraphHandle_t graph;
    std::deque<TensorStorage> tensors;std::map<std::string,size_t> ids;
    Qnn_Tensor_t get(const std::string& name){return tensors.at(ids.at(name)).value;}
    Qnn_Tensor_t add(std::string name,unsigned m,unsigned n,Qnn_TensorType_t type,
                     float scale,const std::vector<float>& values={},bool weight=false) {
        tensors.emplace_back();auto& s=tensors.back();s.name=name;s.dims={m,n};
        s.value=tensor(s.name.c_str(),type,s.dims.data(),nullptr,0);
        quantization(s.value,weight?QNN_DATATYPE_UFIXED_POINT_8:QNN_DATATYPE_UFIXED_POINT_16,
                     scale,weight?-128:-32768);
        if(type==QNN_TENSOR_TYPE_STATIC){
            if(values.size()!=size_t(m)*n)throw std::runtime_error("Static tensor shape mismatch");
            if(weight){s.u8.resize(values.size());for(size_t i=0;i<values.size();++i)s.u8[i]=uint8_t(std::clamp(std::nearbyint(values[i]/scale)+128.0f,0.0f,255.0f));s.value.v1.clientBuf={s.u8.data(),uint32_t(s.u8.size())};}
            else{s.u16.resize(values.size());for(size_t i=0;i<values.size();++i)s.u16[i]=uint16_t(std::clamp(std::nearbyint(values[i]/scale)+32768.0f,0.0f,65535.0f));s.value.v1.clientBuf={s.u16.data(),uint32_t(s.u16.size()*2)};}
        }
        check(api.tensorCreateGraphTensor(graph,&s.value),name.c_str());ids[name]=tensors.size()-1;return s.value;
    }
    void node(const std::string& name,const char* type,const std::vector<std::string>& inputs,
              const std::string& output,std::vector<Qnn_Param_t> params={}){
        std::vector<Qnn_Tensor_t> in;for(auto& n:inputs)in.push_back(get(n));auto out=get(output);
        Qnn_OpConfig_t op=QNN_OPCONFIG_INIT;op.v1.name=name.c_str();op.v1.packageName=QNN_OP_PACKAGE_NAME_QTI_AISW;op.v1.typeName=type;
        op.v1.numOfParams=params.size();op.v1.params=params.data();op.v1.numOfInputs=in.size();op.v1.inputTensors=in.data();op.v1.numOfOutputs=1;op.v1.outputTensors=&out;
        check(api.backendValidateOpConfig(backend,op),name.c_str());check(api.graphAddNode(graph,op),name.c_str());
    }
};
float scale(const std::vector<float>& values,unsigned levels=32767){float peak=0;for(float v:values){if(!std::isfinite(v))throw std::runtime_error("Nonfinite calibration");peak=std::max(peak,std::abs(v));}return std::max(peak/levels,1e-8f);}
std::vector<float> floats(const std::vector<uint16_t>& values){std::vector<float> v(values.size());for(size_t i=0;i<v.size();++i)v[i]=half_value(values[i]);return v;}
Qnn_Param_t scalar(const char* name,float value){Qnn_Param_t p=QNN_PARAM_INIT;p.name=name;p.paramType=QNN_PARAMTYPE_SCALAR;p.scalarParam.dataType=QNN_DATATYPE_FLOAT_32;p.scalarParam.floatValue=value;return p;}

int main(int argc,char** argv){try{
    if(argc<4||argc>6)throw std::runtime_error("Usage: qnn_ffn_probe CAPTURE_DIRECTORY NEW_OUTPUT_DIRECTORY QNN_SOC_MODEL [BATCH_ROWS] [burst]");
    std::filesystem::path folder=argv[1],out=argv[2];if(std::filesystem::exists(out))throw std::runtime_error("Fresh output directory required");
    Json spec;std::ifstream(folder/"projection.json")>>spec;unsigned m=spec.at("m"),n=spec.at("n"),k=spec.at("k");
    if(m!=32768||n!=128||k!=32)throw std::runtime_error("This first FFN prototype requires the captured block-0 32768x32x128 shape");
    unsigned batch_rows=argc>=5?std::stoul(argv[4]):m;if(!batch_rows||batch_rows>m||m%batch_rows)throw std::runtime_error("Batch rows must divide captured rows");
    auto ffn=spec.at("ffn");auto input=floats(read<uint16_t>(folder/spec.at("input").get<std::string>(),size_t(m)*k));
    auto skip=floats(read<uint16_t>(folder/ffn.at("skip").get<std::string>(),size_t(m)*k));
    auto w1=floats(read<uint16_t>(folder/spec.at("weights").get<std::string>(),size_t(n)*k));
    auto w2=floats(read<uint16_t>(folder/ffn.at("contract").get<std::string>(),size_t(k)*n));
    auto gains=read<float>(folder/ffn.at("gain").get<std::string>(),k);
    auto reference=read<float>(folder/ffn.at("reference").get<std::string>(),size_t(m)*k);
    auto projection=read<float>(folder/spec.at("reference").get<std::string>(),size_t(m)*n);
    bool burst=argc==6;if(burst&&std::string(argv[5])!="burst")throw std::runtime_error("Expected burst profile");
    unsigned soc=std::stoul(argv[3]);Runtime runtime;runtime.initialize(soc,burst);auto& api=runtime.api;
    Qnn_GraphHandle_t graph=nullptr;check(api.graphCreate(runtime.context,"phoenix_block0_ffn",nullptr,&graph),"graphCreate");Builder b{api,runtime.backend,graph};
    float input_scale=scale(input),skip_scale=scale(skip),output_scale=scale(reference);
    b.add("input",batch_rows,k,QNN_TENSOR_TYPE_APP_WRITE,input_scale);b.add("skip",batch_rows,k,QNN_TENSOR_TYPE_APP_WRITE,skip_scale);
    b.add("w1",n,k,QNN_TENSOR_TYPE_STATIC,scale(w1,127),w1,true);b.add("w2",k,n,QNN_TENSOR_TYPE_STATIC,scale(w2,127),w2,true);
    b.add("gains",1,k,QNN_TENSOR_TYPE_STATIC,scale(gains),gains);
    Qnn_Param_t transpose=QNN_PARAM_INIT;transpose.paramType=QNN_PARAMTYPE_SCALAR;transpose.name=QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN1;transpose.scalarParam.dataType=QNN_DATATYPE_BOOL_8;transpose.scalarParam.bool8Value=1;
    b.add("wide",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(projection));b.node("expand",QNN_OP_MAT_MUL,{"input","w1"},"wide",{transpose});
    auto current=projection;
    auto unary=[&](std::string name,const char* op,auto function,std::vector<Qnn_Param_t> params={}){for(float& v:current)v=function(v);b.add(name,batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node(name,op,{name=="clamped"?"wide":"clamped"},name,params);};
    unary("clamped",QNN_OP_RELU_MIN_MAX,[](float v){return std::clamp(v,-4.0f,4.0f);},{scalar(QNN_OP_RELU_MIN_MAX_PARAM_MIN_VALUE,-4),scalar(QNN_OP_RELU_MIN_MAX_PARAM_MAX_VALUE,4)});
    unary("absolute",QNN_OP_ELEMENT_WISE_ABS,[](float v){return std::abs(v);});
    auto constant=[&](const char* name,float value){b.add(name,1,1,QNN_TENSOR_TYPE_STATIC,std::max(std::abs(value)/32767,1e-8f),{value});};
    constant("a",-0.055908203125f);constant("b",0.447265625f);constant("c",0.89453125f);
    for(float& v:current)v*=-0.055908203125f;b.add("scaled",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node("scaled",QNN_OP_ELEMENT_WISE_MULTIPLY,{"absolute","a"},"scaled");
    for(float& v:current)v+=0.447265625f;b.add("linear",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node("linear",QNN_OP_ELEMENT_WISE_ADD,{"scaled","b"},"linear");
    for(size_t i=0;i<current.size();++i)current[i]*=std::clamp(projection[i],-4.0f,4.0f);b.add("product",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node("product",QNN_OP_ELEMENT_WISE_MULTIPLY,{"linear","clamped"},"product");
    for(float& v:current)v+=0.89453125f;b.add("factor",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node("factor",QNN_OP_ELEMENT_WISE_ADD,{"product","c"},"factor");
    for(size_t i=0;i<current.size();++i)current[i]*=projection[i];b.add("gated",batch_rows,n,QNN_TENSOR_TYPE_NATIVE,scale(current));b.node("gated",QNN_OP_ELEMENT_WISE_MULTIPLY,{"wide","factor"},"gated");
    // Conservative per-fixture calibration for the two final intermediates.
    std::vector<float> skip_gain(skip.size());for(size_t i=0;i<skip.size();++i)skip_gain[i]=skip[i]*gains[i%k];
    std::vector<float> contract(reference.size());for(size_t i=0;i<contract.size();++i)contract[i]=reference[i]-skip_gain[i];
    b.add("contract",batch_rows,k,QNN_TENSOR_TYPE_NATIVE,scale(contract));b.node("contract",QNN_OP_MAT_MUL,{"gated","w2"},"contract",{transpose});
    b.add("skip_gain",batch_rows,k,QNN_TENSOR_TYPE_NATIVE,scale(skip_gain));b.node("skip_gain",QNN_OP_ELEMENT_WISE_MULTIPLY,{"skip","gains"},"skip_gain");
    b.add("output",batch_rows,k,QNN_TENSOR_TYPE_APP_READ,output_scale);b.node("residual",QNN_OP_ELEMENT_WISE_ADD,{"contract","skip_gain"},"output");
    auto started=Clock::now();check(api.graphFinalize(graph,nullptr,nullptr),"graphFinalize");double finalize_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    auto encode=[](const std::vector<float>& data,float step){std::vector<uint16_t> q(data.size());for(size_t i=0;i<q.size();++i)q[i]=uint16_t(std::clamp(std::nearbyint(data[i]/step)+32768.0f,0.0f,65535.0f));return q;};
    auto iq=encode(input,input_scale),sq=encode(skip,skip_scale);std::vector<uint16_t> result(reference.size());
    auto in0=b.get("input"),in1=b.get("skip"),output=b.get("output");in0.v1.clientBuf={iq.data(),uint32_t(iq.size()*2)};in1.v1.clientBuf={sq.data(),uint32_t(sq.size()*2)};output.v1.clientBuf={result.data(),uint32_t(result.size()*2)};Qnn_Tensor_t inputs[]={in0,in1};
    std::vector<double> samples;for(unsigned i=0;i<22;++i){started=Clock::now();for(unsigned first=0;first<m;first+=batch_rows){inputs[0].v1.clientBuf={iq.data()+size_t(first)*k,uint32_t(batch_rows*k*2)};inputs[1].v1.clientBuf={sq.data()+size_t(first)*k,uint32_t(batch_rows*k*2)};output.v1.clientBuf={result.data()+size_t(first)*k,uint32_t(batch_rows*k*2)};check(api.graphExecute(graph,inputs,2,&output,1,nullptr,nullptr),"graphExecute");}if(i>=2)samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-started).count());}
    std::filesystem::create_directories(out);std::ofstream f(out/"output.u16",std::ios::binary);f.write(reinterpret_cast<const char*>(result.data()),result.size()*2);if(!f)throw std::runtime_error("Output write failed");
    auto sorted=samples;std::sort(sorted.begin(),sorted.end());Json report={{"backend","QNN HTP"},{"precision","w8a16"},{"soc_model",soc},{"m",m},{"hidden",n},{"channels",k},{"graph_finalize_ms",finalize_ms},{"warm_call_ms",samples},{"median_call_ms",(sorted[9]+sorted[10])/2},{"output_scale",output_scale},{"output_zero_point",32768},{"cpu_fallback",false},{"nodes",11},{"batch_rows",batch_rows},{"graph_calls_per_iteration",m/batch_rows},{"calibration","Same fixture, including reference output/intermediates; no independent quality evaluation"},{"scope","Complete block-0 feed-forward residual island on HTP; polynomial gate uses linear quantization instead of E4M3 publication; excludes attention, full model, external input quantization and app presentation"}};
    report["power_profile"]=burst?"scoped-burst":"default";
    std::ofstream(out/"result.json")<<report.dump(2);std::cout<<report.dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
