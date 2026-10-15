#pragma once
// Shared QNN experiment utilities. Supply the official SDK separately.
// No SDK headers, binaries, model tensors or automatic CPU fallback are shipped.
#include "QnnInterface.h"
#include "QnnOpDef.h"
#include "HTP/QnnHtpGraph.h"
#include "HTP/QnnHtpCommon.h"
#include "HTP/QnnHtpDevice.h"
#include "json.hpp"
#include <dlfcn.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void check(Qnn_ErrorHandle_t code,const char* name){if(code!=QNN_SUCCESS)throw std::runtime_error(std::string(name)+" failed: "+std::to_string(code));}
void logger(const char* format,QnnLog_Level_t,uint64_t,va_list args){vfprintf(stderr,format,args);fputc('\n',stderr);}
struct Runtime {
    void* library=nullptr;QNN_INTERFACE_VER_TYPE api{};
    Qnn_LogHandle_t log=nullptr;Qnn_BackendHandle_t backend=nullptr;
    Qnn_DeviceHandle_t device=nullptr;Qnn_ContextHandle_t context=nullptr;
    QnnHtpDevice_PerfInfrastructure_t perf{};uint32_t power_id=0;bool burst=false;
    ~Runtime(){if(burst)perf.destroyPowerConfigId(power_id);if(context)api.contextFree(context,nullptr);if(device)api.deviceFree(device);if(backend)api.backendFree(backend);if(log)api.logFree(log);if(library)dlclose(library);}
    void initialize(unsigned soc,bool use_burst=false){
        library=dlopen("libQnnHtp.so",RTLD_NOW|RTLD_LOCAL);
        if(!library)throw std::runtime_error(dlerror());
        auto providers=reinterpret_cast<decltype(&QnnInterface_getProviders)>(dlsym(library,"QnnInterface_getProviders"));
        if(!providers)throw std::runtime_error("QNN provider interface missing");
        const QnnInterface_t** list=nullptr;uint32_t count=0;check(providers(&list,&count),"providers");
        bool found=false;
        for(unsigned i=0;i<count;++i)if(list[i]->backendId==QNN_BACKEND_ID_HTP&&list[i]->apiVersion.coreApiVersion.major==QNN_API_VERSION_MAJOR&&list[i]->apiVersion.coreApiVersion.minor>=QNN_API_VERSION_MINOR){api=list[i]->QNN_INTERFACE_VER_NAME;found=true;break;}
        if(!found)throw std::runtime_error("Compatible HTP interface unavailable");
        check(api.logCreate(logger,QNN_LOG_LEVEL_WARN,&log),"logCreate");
        check(api.backendCreate(log,nullptr,&backend),"backendCreate");
        QnnHtpDevice_CustomConfig_t target{};target.option=QNN_HTP_DEVICE_CONFIG_OPTION_SOC;target.socModel=soc;
        QnnDevice_Config_t config=QNN_DEVICE_CONFIG_INIT;config.option=QNN_DEVICE_CONFIG_OPTION_CUSTOM;config.customConfig=&target;
        const QnnDevice_Config_t* configs[]={&config,nullptr};
        check(api.deviceCreate(log,soc?configs:nullptr,&device),"deviceCreate");
        check(api.contextCreate(backend,device,nullptr,&context),"contextCreate");
        if(use_burst){
            QnnDevice_Infrastructure_t infrastructure=nullptr;
            check(api.deviceGetInfrastructure(&infrastructure),"deviceGetInfrastructure");
            if(!infrastructure||infrastructure->infraType!=QNN_HTP_DEVICE_INFRASTRUCTURE_TYPE_PERF)throw std::runtime_error("HTP performance infrastructure unavailable");
            perf=infrastructure->perfInfra;if(!perf.createPowerConfigId||!perf.destroyPowerConfigId||!perf.setPowerConfig)throw std::runtime_error("HTP power API unavailable");check(perf.createPowerConfigId(0,0,&power_id),"createPowerConfigId");burst=true;
            QnnHtpPerfInfrastructure_PowerConfig_t config=QNN_HTP_PERF_INFRASTRUCTURE_POWER_CONFIG_INIT;
            config.option=QNN_HTP_PERF_INFRASTRUCTURE_POWER_CONFIGOPTION_DCVS_V3;auto& p=config.dcvsV3Config;
            p.contextId=power_id;p.setDcvsEnable=1;p.dcvsEnable=0;p.powerMode=QNN_HTP_PERF_INFRASTRUCTURE_POWERMODE_PERFORMANCE_MODE;
            p.setSleepLatency=1;p.sleepLatency=40;p.setSleepDisable=1;p.sleepDisable=1;
            p.setBusParams=1;p.busVoltageCornerMin=p.busVoltageCornerTarget=p.busVoltageCornerMax=DCVS_VOLTAGE_VCORNER_MAX_VOLTAGE_CORNER;
            p.setCoreParams=1;p.coreVoltageCornerMin=p.coreVoltageCornerTarget=p.coreVoltageCornerMax=DCVS_VOLTAGE_VCORNER_MAX_VOLTAGE_CORNER;
            const QnnHtpPerfInfrastructure_PowerConfig_t* configs[]={&config,nullptr};check(perf.setPowerConfig(power_id,configs),"setPowerConfig");
        }
    }
};
template<class T>std::vector<T> read(const std::filesystem::path& p,size_t count){std::vector<T> v(count);std::ifstream f(p,std::ios::binary);if(!f.read(reinterpret_cast<char*>(v.data()),count*sizeof(T))||f.peek()!=EOF)throw std::runtime_error("Input size mismatch: "+p.string());return v;}
Qnn_Tensor_t tensor(const char* name,Qnn_TensorType_t type,uint32_t* dimensions,void* data,size_t bytes){
    if(bytes>UINT32_MAX)throw std::runtime_error("Tensor too large");
    Qnn_Tensor_t t=QNN_TENSOR_INIT;t.v1.name=name;t.v1.type=type;t.v1.dataType=QNN_DATATYPE_FLOAT_16;t.v1.rank=2;t.v1.dimensions=dimensions;t.v1.memType=QNN_TENSORMEMTYPE_RAW;t.v1.clientBuf={data,uint32_t(bytes)};return t;
}
float half_value(uint16_t bits){return float(std::bit_cast<_Float16>(bits));}
float range_scale(const std::vector<uint16_t>& data,unsigned maximum){float peak=0;for(auto h:data)peak=std::max(peak,std::abs(half_value(h)));return std::max(peak/maximum,1e-8f);}
void quantization(Qnn_Tensor_t& tensor,Qnn_DataType_t type,float scale,int offset){
    tensor.v1.dataType=type;auto& q=tensor.v1.quantizeParams;q.encodingDefinition=QNN_DEFINITION_DEFINED;q.quantizationEncoding=QNN_QUANTIZATION_ENCODING_SCALE_OFFSET;q.scaleOffsetEncoding={scale,offset};
}
