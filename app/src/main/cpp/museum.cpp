#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include "json.hpp"
#include "phoenix.h"
#include "shaders.h"
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
constexpr float XM_PI=3.141592653589793f,XM_2PI=XM_PI*2,XM_PIDIV2=XM_PI/2;
struct XMFLOAT3{float x=0,y=0,z=0;};
struct XMFLOAT4{float x=0,y=0,z=0,w=0;};
struct Mat {float m[16]{};};
using XMMATRIX=Mat;
Mat operator*(const Mat&a,const Mat&b){Mat c;for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)c.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j];return c;}
Mat XMMatrixIdentity(){Mat r;for(int i=0;i<4;i++)r.m[i*4+i]=1;return r;}
Mat XMMatrixScaling(float x,float y,float z){auto m=XMMatrixIdentity();m.m[0]=x;m.m[5]=y;m.m[10]=z;return m;}
Mat XMMatrixTranslation(float x,float y,float z){auto m=XMMatrixIdentity();m.m[12]=x;m.m[13]=y;m.m[14]=z;return m;}
Mat XMMatrixRotationY(float a){auto m=XMMatrixIdentity();m.m[0]=m.m[10]=cosf(a);m.m[2]=-sinf(a);m.m[8]=sinf(a);return m;}
Mat XMMatrixRotationZ(float a){auto m=XMMatrixIdentity();m.m[0]=m.m[5]=cosf(a);m.m[1]=sinf(a);m.m[4]=-sinf(a);return m;}
Mat transpose(Mat a){Mat b;for(int i=0;i<4;i++)for(int j=0;j<4;j++)b.m[i*4+j]=a.m[j*4+i];return b;}
XMFLOAT3 operator-(XMFLOAT3 a,XMFLOAT3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot(XMFLOAT3 a,XMFLOAT3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
XMFLOAT3 cross(XMFLOAT3 a,XMFLOAT3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
XMFLOAT3 normalize(XMFLOAT3 a){float d=sqrtf(dot(a,a));return {a.x/d,a.y/d,a.z/d};}
Mat look(XMFLOAT3 eye,XMFLOAT3 target){auto z=normalize(eye-target),x=normalize(cross({0,1,0},z)),y=cross(z,x);Mat m=XMMatrixIdentity();m.m[0]=x.x;m.m[4]=x.y;m.m[8]=x.z;m.m[1]=y.x;m.m[5]=y.y;m.m[9]=y.z;m.m[2]=z.x;m.m[6]=z.y;m.m[10]=z.z;m.m[12]=-dot(eye,x);m.m[13]=-dot(eye,y);m.m[14]=-dot(eye,z);return m;}
Mat perspective(float aspect){Mat m;float f=1/tanf(.78f*.5f),n=.08f,z=80;m.m[0]=f/aspect;m.m[5]=f;m.m[10]=-(z+n)/(z-n);m.m[11]=-1;m.m[14]=-2*z*n/(z-n);return m;}
Mat ortho(float radius){auto m=XMMatrixIdentity();m.m[0]=m.m[5]=1/radius;m.m[10]=-2/60.f;m.m[14]=-1;return m;}
static void ensure(bool b,const char* text){if(!b)throw std::runtime_error(text);}
static AAssetManager* assetManager=nullptr;
static fs::path files,nativeDir;
std::vector<uint8_t> asset(const std::string& path){AAsset* f=AAssetManager_open(assetManager,path.c_str(),AASSET_MODE_BUFFER);ensure(f,"Missing museum asset");size_t n=AAsset_getLength64(f);std::vector<uint8_t> data(n);int64_t at=0;while(at<int64_t(n)){int read=AAsset_read(f,data.data()+at,n-at);if(read<=0){AAsset_close(f);throw std::runtime_error("Truncated museum asset");}at+=read;}AAsset_close(f);return data;}
Json assetJson(const std::string& path){auto b=asset(path);return Json::parse(b);}
struct Vertex{float p[3],n[3],uv[2],t[4];};
struct Skin{Vertex vertex;uint32_t ids[4];float weights[4];float blink[6],smile[6],jaw[6];};
static_assert(sizeof(Skin)==152);
struct Mesh{GLuint vao=0,vbo=0;unsigned count=0;float height=1.8f;};
struct Person{Json rig;std::vector<float> matrices,currentBones,previousBones;unsigned boneCount=0,lod=0;float stride=1;GLuint boneTexture=0;Mesh mesh[4];};
#include "catalogue.inc"
static Person people[ActorCount];
static bool facialAnimation=true;
#include "gallery_animation.inc"
struct GalleryObject{Mat world,previousWorld,baseWorld;XMFLOAT4 material;unsigned mesh=0,lowMesh=0,animationKind=0;};
static std::vector<GalleryObject> gallery;
unsigned assetMesh(unsigned asset,unsigned lod){return asset<4?6+asset*2+std::min(lod,1u):14+(asset-4)*4+lod;}
void add(unsigned mesh,unsigned low,Mat world,XMFLOAT4 material){gallery.push_back({world,world,world,material,mesh,low,0});}
#include "gallery_layout.inc"
static Mesh meshes[86];
static std::array<GLuint,3> textures[23];
GLuint shader(GLenum type,const char* source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log);}return s;}
GLuint program(const char* vs,const char* ps){GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,ps),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[8192]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log);}return p;}
void matrix(GLuint p,const char* name,const Mat& m){glUniformMatrix4fv(glGetUniformLocation(p,name),1,GL_FALSE,m.m);}
void integer(GLuint p,const char* name,int value){glUniform1i(glGetUniformLocation(p,name),value);}
void number(GLuint p,const char* name,float value){glUniform1f(glGetUniformLocation(p,name),value);}
void vector3(GLuint p,const char* name,XMFLOAT3 value){glUniform3f(glGetUniformLocation(p,name),value.x,value.y,value.z);}
void bind(GLuint p,const char* name,GLuint tex,int unit){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,tex);integer(p,name,unit);}
GLuint texture(int w,int h,GLenum internal,GLenum format,GLenum type,const void* data=nullptr,bool linear=false){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,type,data);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);return t;}
GLuint loadTexture(const std::string& path){auto b=asset("scene/"+path);unsigned w,h;memcpy(&w,b.data(),4);memcpy(&h,b.data()+4,4);ensure(b.size()==8+size_t(w)*h*4,"Texture dimensions");GLuint t=texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,b.data()+8,true);glGenerateMipmap(GL_TEXTURE_2D);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);return t;}
Mesh createMesh(const void* data,unsigned count,bool skin){Mesh m;m.count=count;unsigned stride=skin?152:48;glGenVertexArrays(1,&m.vao);glBindVertexArray(m.vao);glGenBuffers(1,&m.vbo);glBindBuffer(GL_ARRAY_BUFFER,m.vbo);glBufferData(GL_ARRAY_BUFFER,size_t(count)*stride,data,GL_STATIC_DRAW);for(int i=0;i<4;i++){int count[]={3,3,2,4},offset[]={0,12,24,32};glEnableVertexAttribArray(i);glVertexAttribPointer(i,count[i],GL_FLOAT,GL_FALSE,stride,(void*)(intptr_t)offset[i]);}if(skin){glEnableVertexAttribArray(4);glVertexAttribIPointer(4,4,GL_UNSIGNED_INT,stride,(void*)48);for(int i=5;i<=8;i++){int count=i==5?4:3;int offset=i==5?64:80+(i-6)*24;glEnableVertexAttribArray(i);glVertexAttribPointer(i,count,GL_FLOAT,GL_FALSE,stride,(void*)(intptr_t)offset);}}const auto* b=static_cast<const uint8_t*>(data);m.height=0;for(unsigned i=0;i<count;i++){float y;memcpy(&y,b+i*stride+4,4);m.height=std::max(m.height,y);}return m;}
Mesh loadMesh(const std::string& path){auto b=asset("scene/"+path);unsigned n;memcpy(&n,b.data(),4);ensure(b.size()==4+size_t(n)*48,"Mesh dimensions");return createMesh(b.data()+4,n,false);}
Mesh cube(){std::vector<Vertex> v;XMFLOAT3 ns[6]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}},ts[6]={{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}},bs[6]={{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};for(int f=0;f<6;f++)for(int id:{0,1,2,0,2,3}){float u=(id==1||id==2)?1:0,z=id>=2?1:0;Vertex a{};float* np=&ns[f].x;float* tp=&ts[f].x;float* bp=&bs[f].x;for(int i=0;i<3;i++){a.p[i]=np[i]*.5f+tp[i]*(u-.5f)+bp[i]*(z-.5f);a.n[i]=np[i];a.t[i]=tp[i];}a.uv[0]=u;a.uv[1]=1-z;a.t[3]=-1;v.push_back(a);}return createMesh(v.data(),v.size(),false);}
static GLuint sceneProgram=0,finishProgram=0,displayProgram=0,captureProgram=0,emptyVAO=0;
struct Target {GLuint fbo=0,color=0,motion=0,world=0,normal=0,depth=0,finishFbo=0,finish=0;};
static Target targets[2];static GLuint shadowFbo=0,shadowTex=0,captureFbo=0,captureColor=0,captureMotion=0;
static int captureW=0,captureH=0;
static int renderW=0,renderH=0;static float sceneTime=0,previousSceneTime=0,lightPhase=0;
static Mat previousVP=XMMatrixIdentity(),lastVP=XMMatrixIdentity();
static Clock::time_point lastRender=Clock::now();
struct Result{std::vector<uint8_t> input,output,fullInput;int w=0,h=0,fullW=0,fullH=0,side=0;uint64_t generation=0,serial=0;double ms=0;Clock::time_point captured,completed;Json runtime;};
struct Job{std::vector<uint8_t> input,fullInput;std::vector<float> motion;Json options;int w=0,h=0,fullW=0,fullH=0,side=0;uint64_t generation=0;Clock::time_point captured;};
static std::mutex workMutex;static std::condition_variable workWake;static Job pendingJob;static bool hasJob=false,busy=false;static std::atomic<bool> paused{false},captureRequested{false},saveInferenceRequested{false};static std::atomic<uint64_t> generation{1};
static Json report={{"state","idle"},{"frame_ms",0},{"inference_fps",0},{"age_seconds",0}};static Result completed[2];static uint64_t uploadSerial[2]{},serial=0;static GLuint processedTex[2]{},matchedTex[2]{},lowInputTex[2]{};static Clock::time_point latestCompletion{},latestCapture{},callStarted{};static std::string lastConfig;
static std::vector<Json> deviceList;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void state(const std::string& value,const std::string& error=""){std::lock_guard lock(workMutex);report["state"]=value;report["error"]=error;}
void saveBinary(const fs::path& path,const void* data,size_t bytes){std::ofstream out(path,std::ios::binary);out.write(static_cast<const char*>(data),bytes);}
void worker(){
 void* library=dlopen((nativeDir/"libphoenix_runtime_cpp.so").c_str(),RTLD_NOW|RTLD_LOCAL);
 auto create=library?reinterpret_cast<decltype(&phoenix_runtime_create)>(dlsym(library,"phoenix_runtime_create")):nullptr;
 auto frame=library?reinterpret_cast<decltype(&phoenix_runtime_frame)>(dlsym(library,"phoenix_runtime_frame")):nullptr;
 auto destroy=library?reinterpret_cast<decltype(&phoenix_runtime_destroy)>(dlsym(library,"phoenix_runtime_destroy")):nullptr;
 auto error=library?reinterpret_cast<decltype(&phoenix_runtime_error)>(dlsym(library,"phoenix_runtime_error")):nullptr;
 auto status=library?reinterpret_cast<decltype(&phoenix_runtime_status)>(dlsym(library,"phoenix_runtime_status")):nullptr;
 void* session=nullptr;std::string signature;int previousSide=-1;uint64_t previousGeneration=0,savedGeneration=0;Clock::time_point lastCompleted{};std::deque<double> intervals;uint64_t rateGeneration=0;
 while(true){
  Job job;{std::unique_lock lock(workMutex);workWake.wait(lock,[]{return hasJob&&!paused;});job=std::move(pendingJob);hasJob=false;busy=true;callStarted=Clock::now();report["active_backend"]=job.options.value("backend",std::string("cpu"));}
  try {
   ensure(create&&frame&&destroy&&error&&status,"Phoenix runtime could not load");
   std::string backend=job.options.value("backend",std::string("vulkan"));std::string device=backend=="cpu"?"cpu":job.options.value("device",std::string());
   if(backend=="vulkan"){ensure(!device.empty(),"No Vulkan device was found");auto it=std::find_if(deviceList.begin(),deviceList.end(),[&](auto& d){return d["id"]==device;});ensure(it!=deviceList.end(),"Selected Vulkan device is unavailable");bool compatibility=job.options.value("vulkan_workgroup_compatibility",false);ensure(it->value(compatibility?"compatibility_supported":"supported",false),it->value("compatibility_supported",false)?"This GPU needs the optional Vulkan compatibility mode. Enable it in the menu.":"This GPU is missing required Phoenix Vulkan compute features. Select CPU to test inference.");}
   Json cfg={{"backend",backend},{"device",device},{"runtime_root",(files/"runtime").string()},{"allow_unvalidated",true},{"cpu_threads",job.options.value("cpu_threads",4)},{"weight_source",{{"kind","nvidia_dll"},{"path",(files/"model/nvngx_dlssnr.dll").string()},{"sha256",assetJson("model.json")["sha256"]}}}};
   if(backend=="vulkan")cfg["vulkan_workgroup_compatibility"]=job.options.value("vulkan_workgroup_compatibility",false);
   std::string key=cfg.dump()+std::to_string(job.w)+"x"+std::to_string(job.h);
   if(key!=signature){state("loading");if(session){destroy(session);session=nullptr;}signature.clear();session=create(cfg.dump().c_str(),job.w,job.h);if(!session)throw std::runtime_error(error());signature=key;previousSide=-1;}
   state("processing");auto start=Clock::now();size_t pixels=size_t(job.w)*job.h;std::vector<float> input(pixels*3),output(pixels*3);
   for(size_t p=0;p<pixels;p++)for(int c=0;c<3;c++)input[p*3+c]=job.input[p*4+c]/255.f;
   bool reset=previousSide!=job.side||previousGeneration!=job.generation||!job.options.value("history",true);
   Json meta={{"reset",reset},{"has_motion",true},{"passes",job.options.value("passes",1)},{"repeats",job.options.value("repeats",Json::array({1,1,1,1,1,1,1,1}))},{"controls",{{"intensity",job.options.value("intensity",1.f)},{"local_tone",job.options.value("local_tone",1.f)},{"local_structure",job.options.value("local_structure",1.f)},{"skin_structure",job.options.value("skin_structure",-1.f)},{"auto_mask",job.options.value("auto_mask",true)},{"style",job.options.value("style",0)},{"seed",job.options.value("seed",0)}}}};
   if(frame(session,input.data(),job.motion.data(),nullptr,nullptr,meta.dump().c_str(),output.data()))throw std::runtime_error(error());
   double elapsed=ms(start);auto finished=Clock::now();if(rateGeneration!=job.generation){intervals.clear();lastCompleted={};rateGeneration=job.generation;}if(lastCompleted.time_since_epoch().count()){intervals.push_back(std::chrono::duration<double>(finished-lastCompleted).count());if(intervals.size()>16)intervals.pop_front();}lastCompleted=finished;double seconds=0;for(double dt:intervals)seconds+=dt;double throughput=seconds>0?intervals.size()/seconds:0;previousSide=job.side;previousGeneration=job.generation;
   std::vector<char> text(status(session,nullptr,0));status(session,text.data(),text.size());Json runtime=Json::parse(text.data());
   Result result;result.input=job.input;result.fullInput=std::move(job.fullInput);result.fullW=job.fullW;result.fullH=job.fullH;result.output.resize(pixels*4);result.w=job.w;result.h=job.h;result.side=job.side;result.generation=job.generation;result.captured=job.captured;result.completed=Clock::now();result.ms=elapsed;result.runtime=runtime;
   size_t changed=0;double difference=0;for(size_t p=0;p<pixels;p++){float mask=job.options.value(job.side?"right_phoenix":"left_phoenix",0)==1?job.input[p*4+3]/255.f:1.f;for(int c=0;c<3;c++){float v=output[p*3+c];ensure(std::isfinite(v),"Nonfinite Phoenix output");float native=input[p*3+c];difference+=fabs(v-native);changed+=fabs(v-native)>1/255.f;float display=native+(std::clamp(v,0.f,1.f)-native)*mask;result.output[p*4+c]=uint8_t(display*255+.5f);}result.output[p*4+3]=255;}
   {std::lock_guard lock(workMutex);result.serial=++serial;if(job.generation==generation){completed[job.side]=std::move(result);latestCompletion=Clock::now();latestCapture=job.captured;report["state"]="ready";report["error"]="";report["frame_ms"]=elapsed;report["inference_fps"]=throughput;report["call_capacity_fps"]=1000/elapsed;report["runtime"]=runtime;report["changed_channels"]=changed;report["mean_absolute_change"]=difference/output.size();report["processed_frames"]=serial;report["backend"]=backend;report["processing_width"]=job.w;report["processing_height"]=job.h;report["reset"]=reset;report["side"]=job.side;}else report["state"]="settings changed";}
   if((job.options.value("freeze",false) && savedGeneration!=job.generation)||saveInferenceRequested.exchange(false)){savedGeneration=job.generation;fs::path folder=files/"captures"/(backend+"_"+std::to_string(serial));fs::create_directories(folder);saveBinary(folder/"input.rgb32f",input.data(),input.size()*4);saveBinary(folder/"output.rgb32f",output.data(),output.size()*4);saveBinary(folder/"motion.rg32f",job.motion.data(),job.motion.size()*4);std::ofstream(folder/"frame.json")<<Json({{"config",cfg},{"metadata",meta},{"runtime",runtime},{"call_ms",elapsed},{"width",job.w},{"height",job.h},{"changed_channels",changed},{"mean_absolute_change",difference/output.size()}}).dump(2);}
  }catch(const std::exception& e){state("error",e.what());__android_log_print(ANDROID_LOG_ERROR,"PhoenixMuseum","Inference: %s",e.what());if(session){destroy(session);session=nullptr;}signature.clear();}
  {std::lock_guard lock(workMutex);busy=false;}
 }
}
void loadScene(){
 ensure(std::strstr(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),"GL_EXT_color_buffer_float")||std::strstr(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),"GL_EXT_color_buffer_half_float"),"Floating point render targets are unavailable");
 sceneProgram=program(sceneVertex,sceneFragment);finishProgram=program(fullVertex,finishFragment);displayProgram=program(fullVertex,displayFragment);captureProgram=program(fullVertex,captureFragment);glGenVertexArrays(1,&emptyVAO);
 meshes[4]=cube();meshes[2]=loadMesh("marble_high.mesh");meshes[3]=loadMesh("marble_low.mesh");meshes[5]=meshes[3];
 for(int i=0;i<22;i++){std::string name=AssetNames[i];for(int t=0;t<3;t++)textures[i][t]=loadTexture(name+std::array<const char*,3>{"_color.rgba","_normal.rgba","_rough.rgba"}[t]);if(i<4){for(int l=0;l<2;l++)meshes[assetMesh(i,l)]=loadMesh(name+(l?"_low.mesh":"_high.mesh"));}else{auto& p=people[i-4];p.rig=assetJson("scene/"+name+"_rig.json");p.boneCount=p.rig["bone_count"];p.stride=p.rig["stride"];auto bones=asset("scene/"+name+".bones");p.matrices.resize(bones.size()/4);memcpy(p.matrices.data(),bones.data(),bones.size());p.currentBones.resize(p.boneCount*16);p.previousBones.resize(p.boneCount*16);auto skin=asset("scene/"+name+".skin");for(int l=0;l<4;l++){unsigned count=p.rig["lod_counts"][l],offset=p.rig["lod_offsets"][l];ensure((size_t(offset)+count)*152<=skin.size(),"Skin dimensions");p.mesh[l]=createMesh(skin.data()+size_t(offset)*152,count,true);meshes[assetMesh(i,l)]=p.mesh[l];}p.boneTexture=texture(4,p.boneCount*2,GL_RGBA32F,GL_RGBA,GL_FLOAT);}}
 textures[22][0]=loadTexture("marble_color.rgba");textures[22][1]=loadTexture("marble_normal.rgba");textures[22][2]=loadTexture("marble_rough.rgba");gallery.clear();makeGallery();
 shadowTex=texture(1024,1024,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT);glGenFramebuffers(1,&shadowFbo);glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shadowTex,0);GLenum no=GL_NONE;glDrawBuffers(1,&no);glReadBuffer(GL_NONE);ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Shadow framebuffer");
 {std::lock_guard lock(workMutex);report["scene_objects"]=gallery.size();report["actors"]=18;report["gl_renderer"]=reinterpret_cast<const char*>(glGetString(GL_RENDERER));}
}
void destroyTarget(Target& t){GLuint tex[]={t.color,t.motion,t.world,t.normal,t.depth,t.finish};glDeleteTextures(6,tex);glDeleteFramebuffers(1,&t.fbo);glDeleteFramebuffers(1,&t.finishFbo);t={};}
void resizeTargets(int w,int h){if(w==renderW&&h==renderH)return;renderW=w;renderH=h;for(auto& t:targets){destroyTarget(t);t.color=texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);t.motion=texture(w,h,GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT);t.world=texture(w,h,GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT);t.normal=texture(w,h,GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT);t.depth=texture(w,h,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT);glGenFramebuffers(1,&t.fbo);glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);GLuint tx[]={t.color,t.motion,t.world,t.normal};for(int i=0;i<4;i++)glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,tx[i],0);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,t.depth,0);GLenum draw[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};glDrawBuffers(4,draw);ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Museum framebuffer");t.finish=texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,nullptr,true);glGenFramebuffers(1,&t.finishFbo);glBindFramebuffer(GL_FRAMEBUFFER,t.finishFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.finish,0);glDrawBuffers(1,draw);ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Finish framebuffer");}for(int i=0;i<2;i++){if(processedTex[i])glDeleteTextures(1,&processedTex[i]);if(matchedTex[i])glDeleteTextures(1,&matchedTex[i]);if(lowInputTex[i])glDeleteTextures(1,&lowInputTex[i]);lowInputTex[i]=texture(1,1,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,nullptr,true);processedTex[i]=texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,nullptr,true);matchedTex[i]=texture(w,h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,nullptr,true);uploadSerial[i]=0;}generation++;}
void pose(bool cold,bool animate){
 for(unsigned i=0;i<ActorCount;i++){auto& p=people[i];sampleBones(i,sceneTime,p.currentBones);sampleBones(i,cold?sceneTime:previousSceneTime,p.previousBones);std::vector<float> data=p.currentBones;data.insert(data.end(),p.previousBones.begin(),p.previousBones.end());glBindTexture(GL_TEXTURE_2D,p.boneTexture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,p.boneCount*2,GL_RGBA,GL_FLOAT,data.data());}
 for(unsigned i=0;i<gallery.size();i++){auto& o=gallery[i];unsigned kind=o.animationKind;if(kind>=10&&kind<10+ActorCount){auto s=visitorState(kind-10,sceneTime);o.world=XMMatrixRotationY(s.yaw)*XMMatrixTranslation(s.x,kind-10>=16?.26f:-.025f,s.z);}else if(kind>=100){unsigned person=kind%100;auto& p=people[person];auto s=visitorState(person,sceneTime);auto hand=p.rig["hands"]["Bip01 R Hand"];unsigned bone=hand["bone"];Mat rest,skin;for(int r=0;r<4;r++)for(int c=0;c<4;c++)rest.m[r*4+c]=hand["rest_matrix"][r][c];memcpy(skin.m,p.currentBones.data()+bone*16,64);float visible=std::max(.001f,1-s.walk);o.world=XMMatrixScaling(o.baseWorld.m[0]*visible,o.baseWorld.m[5]*visible,o.baseWorld.m[10]*visible)*XMMatrixTranslation(0,.075f,kind>=200?.045f:0)*transpose(rest)*transpose(skin)*XMMatrixRotationY(s.yaw)*XMMatrixTranslation(s.x,-.025f,s.z);}else if(kind){Mat base=o.baseWorld;float x=base.m[12],y=base.m[13],z=base.m[14];base.m[12]=base.m[13]=base.m[14]=0;Mat rotation=kind==1?XMMatrixRotationY(sceneTime*.18f):XMMatrixRotationY(sceneTime*.22f+i*.35f)*XMMatrixRotationZ(.22f*sinf(sceneTime*.8f+i));o.world=base*rotation*XMMatrixTranslation(x,y,z);}if(cold)o.previousWorld=o.world;}
}
void drawObjects(const Json& cfg,int side,const Mat& vp,const Mat& light,XMFLOAT3 eye,bool shadows){
 int quality=cfg.value(side?"right_quality":"left_quality",3),detail=cfg.value(side?"right_detail":"left_detail",1);glUseProgram(sceneProgram);matrix(sceneProgram,"vp",shadows?light:vp);matrix(sceneProgram,"previousVP",previousVP);matrix(sceneProgram,"lightVP",light);vector3(sceneProgram,"eye",eye);integer(sceneProgram,"quality",quality);integer(sceneProgram,"shadowPass",shadows?1:0);number(sceneProgram,"lightPhase",lightPhase);glUniform2f(glGetUniformLocation(sceneProgram,"extent"),renderW,renderH);bind(sceneProgram,"shadowMap",shadows?0:shadowTex,3);
 for(auto& o:gallery){unsigned index=quality==2?o.mesh:o.lowMesh;unsigned actor=o.animationKind>=10&&o.animationKind<10+ActorCount?o.animationKind-10:999;bool skin=actor<ActorCount;if(skin){auto s=visitorState(actor,sceneTime);float distance=hypotf(s.x-eye.x,s.z-eye.z);unsigned lod=detail==0?0:detail==2?2:cfg.value("focus",-1)==int(actor)?0:distance<3?0:distance<6?1:distance<9?2:3;index=assetMesh(actor+4,lod);auto& p=people[actor];bind(sceneProgram,"bones",p.boneTexture,4);integer(sceneProgram,"boneCount",p.boneCount);vector3(sceneProgram,"face",faceWeights(actor,sceneTime));vector3(sceneProgram,"previousFace",faceWeights(actor,previousSceneTime));}integer(sceneProgram,"skinned",skin?1:0);matrix(sceneProgram,"world",o.world);matrix(sceneProgram,"previousWorld",o.previousWorld);glUniform4f(glGetUniformLocation(sceneProgram,"material"),o.material.x,o.material.y,o.material.z,o.material.w);int mapped=o.material.w>=6?int(o.material.w)-6:22;mapped=std::clamp(mapped,0,22);bind(sceneProgram,"colorMap",textures[mapped][0],0);bind(sceneProgram,"normalMap",textures[mapped][1],1);bind(sceneProgram,"roughMap",textures[mapped][2],2);glBindVertexArray(meshes[index].vao);glDrawArrays(GL_TRIANGLES,0,meshes[index].count);}
}
void drawScene(const Json& cfg,int side,const Mat& vp,const Mat& light,XMFLOAT3 eye){
 int quality=cfg.value(side?"right_quality":"left_quality",3);
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
 if(quality==1||quality==2||quality==4||quality==5){glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo);glViewport(0,0,1024,1024);glClear(GL_DEPTH_BUFFER_BIT);drawObjects(cfg,side,vp,light,eye,true);}
 auto& t=targets[side];glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);glViewport(0,0,renderW,renderH);float zero[4]={};for(int i=0;i<4;i++)glClearBufferfv(GL_COLOR,i,zero);glClear(GL_DEPTH_BUFFER_BIT);drawObjects(cfg,side,vp,light,eye,false);
 glDisable(GL_DEPTH_TEST);glBindFramebuffer(GL_FRAMEBUFFER,t.finishFbo);glUseProgram(finishProgram);bind(finishProgram,"sourceImage",t.color,0);bind(finishProgram,"worldImage",t.world,1);bind(finishProgram,"normalImage",t.normal,2);matrix(finishProgram,"vp",vp);vector3(finishProgram,"eye",eye);integer(finishProgram,"quality",quality);glBindVertexArray(emptyVAO);glDrawArrays(GL_TRIANGLES,0,3);
}
Job capture(const Json& cfg,int side){
 Job job;job.options=cfg;job.side=side;job.w=cfg.value("resolution",320);job.h=std::max(1,int(std::round(float(job.w)*renderH/renderW)));job.fullW=renderW;job.fullH=renderH;job.generation=generation;job.captured=Clock::now();
 if(captureW!=job.w||captureH!=job.h){captureW=job.w;captureH=job.h;if(captureColor)glDeleteTextures(1,&captureColor);if(captureMotion)glDeleteTextures(1,&captureMotion);if(captureFbo)glDeleteFramebuffers(1,&captureFbo);captureColor=texture(job.w,job.h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);captureMotion=texture(job.w,job.h,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);glGenFramebuffers(1,&captureFbo);glBindFramebuffer(GL_FRAMEBUFFER,captureFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,captureColor,0);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,captureMotion,0);GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,outputs);ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Processing framebuffer");}
 std::vector<uint8_t> full(size_t(renderW)*renderH*4);job.fullInput.resize(full.size());glBindFramebuffer(GL_FRAMEBUFFER,targets[side].finishFbo);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,renderW,renderH,GL_RGBA,GL_UNSIGNED_BYTE,full.data());for(int y=0;y<renderH;y++)memcpy(job.fullInput.data()+size_t(y)*renderW*4,full.data()+size_t(renderH-1-y)*renderW*4,size_t(renderW)*4);
 glBindFramebuffer(GL_FRAMEBUFFER,captureFbo);glViewport(0,0,job.w,job.h);glUseProgram(captureProgram);bind(captureProgram,"colorImage",targets[side].finish,0);bind(captureProgram,"motionImage",targets[side].motion,1);glUniform2f(glGetUniformLocation(captureProgram,"scale"),float(job.w)/renderW,float(job.h)/renderH);glBindVertexArray(emptyVAO);glDrawArrays(GL_TRIANGLES,0,3);
 size_t pixels=size_t(job.w)*job.h;std::vector<uint8_t> raw(pixels*4),motion(pixels*4);job.input.resize(raw.size());job.motion.resize(pixels*2);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,job.w,job.h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());glReadBuffer(GL_COLOR_ATTACHMENT1);glReadPixels(0,0,job.w,job.h,GL_RGBA,GL_UNSIGNED_BYTE,motion.data());
 GLenum readError=glGetError();if(readError)throw std::runtime_error("Frame readback error "+std::to_string(readError));
 for(int y=0;y<job.h;y++)for(int x=0;x<job.w;x++){size_t a=size_t(y)*job.w+x,b=size_t(job.h-1-y)*job.w+x;memcpy(job.input.data()+a*4,raw.data()+b*4,4);job.motion[a*2]=(int(motion[b*4])+int(motion[b*4+1])*256-32768)/32.f;job.motion[a*2+1]=-(int(motion[b*4+2])+int(motion[b*4+3])*256-32768)/32.f;}return job;
}
void uploadResult(int side){std::lock_guard lock(workMutex);auto& r=completed[side];if(r.serial==uploadSerial[side]||r.generation!=generation||r.fullW!=renderW||r.fullH!=renderH)return;auto upload=[&](GLuint texture,const std::vector<uint8_t>& bytes,int width,int height){std::vector<uint8_t> pixels(bytes.size());for(int y=0;y<height;y++)memcpy(pixels.data()+size_t(y)*width*4,bytes.data()+size_t(height-1-y)*width*4,size_t(width)*4);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());};upload(processedTex[side],r.output,r.w,r.h);upload(matchedTex[side],r.fullInput,r.fullW,r.fullH);upload(lowInputTex[side],r.input,r.w,r.h);uploadSerial[side]=r.serial;}
void draw(const Json& cfg,float yaw,float pitch,float zoom,int w,int h){
 if(!sceneProgram)loadScene();
 int rw=w,rh=h;bool cold=rw!=renderW||rh!=renderH;
 Json inferenceSettings=cfg;for(const char* key:{"view","same_resolution","display_proof","test_run_id"})inferenceSettings.erase(key);std::string configText=inferenceSettings.dump();if(configText!=lastConfig){lastConfig=configText;generation++;uploadSerial[0]=uploadSerial[1]=0;cold=true;std::lock_guard lock(workMutex);report["error"]="";report["frame_ms"]=0;report["inference_fps"]=0;latestCapture={};report["state"]=busy?"finishing previous call":"idle";}
 resizeTargets(rw,rh);{std::lock_guard lock(workMutex);report["render_width"]=rw;report["render_height"]=rh;}auto now=Clock::now();float dt=std::min(.1f,std::chrono::duration<float>(now-lastRender).count());lastRender=now;if(cfg.value("animation",true)&&!cfg.value("freeze",false))sceneTime+=dt;if(cfg.value("lights",true)&&!cfg.value("freeze",false))lightPhase+=dt*.7f;facialAnimation=cfg.value("faces",true);if(cfg.value("orbit",false)&&!cfg.value("freeze",false))yaw+=sceneTime*.18f;
 XMFLOAT3 eye={sinf(yaw)*zoom,2.25f+pitch*4,cosf(yaw)*zoom},target={0,1.15f,0};int focus=cfg.value("focus",-1);
 if(focus>=0&&focus<18){auto s=visitorState(focus,sceneTime);int framing=cfg.value("framing",0);float height=focus<16?headAnchor(focus,sceneTime).y/.91f:people[focus].mesh[0].height;float fractions[]={.5f,.72f,.91f},distances[]={2.35f,1.3f,.6f};float distance=focus>=16?2.3f*zoom:distances[framing]*zoom;float y=height*(focus>=16?.55f:fractions[framing])+(focus>=16?.26f:-.025f);target={s.x,y,s.z};eye={s.x+sinf(s.yaw+yaw)*distance,y+.04f+pitch,s.z+cosf(s.yaw+yaw)*distance};}
 Mat vp=look(eye,target)*perspective(float(rw)/rh);if(cold)previousVP=vp;Mat light=look({-2.7f,12,3.5f},{0,0,0})*ortho(18);int view=cfg.value("view",1);bool active[2]={view!=2,view!=1};
 // A frozen comparison can reuse its scene targets between model calls. Camera,
 // settings, resize/context changes and motion-reference updates invalidate them.
 static std::array<float,3> cachedCamera{};static bool cachedScene[2]{};
 std::array<float,3> camera{yaw,pitch,zoom};bool frozen=cfg.value("freeze",false);
 if(cold||!frozen||camera!=cachedCamera)cachedScene[0]=cachedScene[1]=false;
 cachedCamera=camera;
 if((active[0]&&!cachedScene[0])||(active[1]&&!cachedScene[1]))pose(cold,cfg.value("animation",true));
 for(int side=0;side<2;side++)if(active[side]&&!cachedScene[side]){drawScene(cfg,side,vp,light,eye);cachedScene[side]=true;}
 int selected=-1;static int nextSide=0;{std::lock_guard lock(workMutex);if(!busy&&!hasJob&&report.value("state",std::string())!="error")for(int i=0;i<2;i++){int side=(nextSide+i)%2;if(active[side]&&cfg.value(side?"right_phoenix":"left_phoenix",0)!=0){selected=side;nextSide=1-side;break;}}}
 if(selected>=0&&!paused){Job job=capture(cfg,selected);{std::lock_guard lock(workMutex);pendingJob=std::move(job);hasJob=true;}workWake.notify_one();}
 for(int side=0;side<2;side++)uploadResult(side);
 GLuint images[2]={targets[0].finish,targets[1].finish};for(int side=0;side<2;side++)if(active[side]&&cfg.value(side?"right_phoenix":"left_phoenix",0)&&uploadSerial[side])images[side]=processedTex[side];
 // A neural/off comparison uses the exact source paired with the completed result.
 if(view==0||view==3||cfg.value("same_resolution",false)){if(cfg.value("left_phoenix",0)==0&&uploadSerial[1]&&cfg.value("left_quality",3)==cfg.value("right_quality",3)&&cfg.value("left_detail",1)==cfg.value("right_detail",1))images[0]=cfg.value("same_resolution",false)?lowInputTex[1]:matchedTex[1];if(cfg.value("right_phoenix",0)==0&&uploadSerial[0]&&cfg.value("left_quality",3)==cfg.value("right_quality",3)&&cfg.value("left_detail",1)==cfg.value("right_detail",1))images[1]=cfg.value("same_resolution",false)?lowInputTex[0]:matchedTex[0];}
 glBindFramebuffer(GL_FRAMEBUFFER,0);GLenum back=GL_BACK;glDrawBuffers(1,&back);glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_DEPTH_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(displayProgram);bind(displayProgram,"leftImage",images[0],0);bind(displayProgram,"rightImage",images[1],1);integer(displayProgram,"view",view);number(displayProgram,"divider",.5f);glBindVertexArray(emptyVAO);glDrawArrays(GL_TRIANGLES,0,3);

 {std::lock_guard lock(workMutex);Json sides=Json::array();for(int side=0;side<2;side++){bool processed=active[side]&&cfg.value(side?"right_phoenix":"left_phoenix",0)&&uploadSerial[side];bool paired=images[side]==matchedTex[1-side]||images[side]==lowInputTex[1-side];sides.push_back({{"visible",active[side]},{"content",processed?"phoenix":"original"},{"paired_source",paired},{"requested_phoenix",cfg.value(side?"right_phoenix":"left_phoenix",0)!=0},{"serial",processed?uploadSerial[side]:0},{"width",processed?completed[side].w:paired&&cfg.value("same_resolution",false)?completed[1-side].w:w},{"height",processed?completed[side].h:paired&&cfg.value("same_resolution",false)?completed[1-side].h:h}});}report["displayed_sides"]=sides;}
 static uint64_t proofSerial=0,proofGeneration=0;
 if(cfg.value("display_proof",false)&&view==0&&uploadSerial[1]&&(proofSerial!=uploadSerial[1]||proofGeneration!=generation)){
  std::lock_guard lock(workMutex);auto& r=completed[1];fs::path folder=files/"captures"/"display_proof";fs::create_directories(folder);std::vector<uint8_t> pixels(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());saveBinary(folder/"display.rgba",pixels.data(),pixels.size());saveBinary(folder/"original.rgba",r.input.data(),r.input.size());saveBinary(folder/"processed.rgba",r.output.data(),r.output.size());std::ofstream(folder/"proof.json")<<Json({{"width",w},{"height",h},{"input_width",r.w},{"input_height",r.h},{"bottom_up",true},{"view",view},{"same_resolution",cfg.value("same_resolution",false)},{"result_serial",r.serial},{"displayed_sides",report["displayed_sides"]}}).dump(2);proofSerial=uploadSerial[1];proofGeneration=generation;
 }
 static unsigned diagnosticFrame=0;
 if(diagnosticFrame++%30==0){std::array<unsigned char,4> px{};glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px.data());std::lock_guard lock(workMutex);report["display_center_rgba"]=px;}
 if(captureRequested.exchange(false)){fs::path folder=files/"captures"/("display_"+std::to_string(Clock::now().time_since_epoch().count()));fs::create_directories(folder);std::vector<uint8_t> pixels(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());saveBinary(folder/"display.rgba",pixels.data(),pixels.size());Json row;{std::lock_guard lock(workMutex);row=report;}row["width"]=w;row["height"]=h;row["bottom_up"]=true;row["config"]=cfg;std::ofstream(folder/"display.json")<<row.dump(2);}
 if(selected>=0){previousVP=vp;previousSceneTime=sceneTime;for(auto& o:gallery)o.previousWorld=o.world;cachedScene[0]=cachedScene[1]=false;}GLenum error=glGetError();if(error)throw std::runtime_error("OpenGL error "+std::to_string(error));
}
std::string string(JNIEnv* e,jstring value){const char* s=e->GetStringUTFChars(value,nullptr);std::string result=s;e->ReleaseStringUTFChars(value,s);return result;}
extern "C" JNIEXPORT void JNICALL Java_org_phoenix_museum_MainActivity_initialize(JNIEnv* e,jclass,jobject manager,jstring root,jstring libs){
 static std::once_flag initialized;std::call_once(initialized,[&]{assetManager=AAssetManager_fromJava(e,manager);files=string(e,root);nativeDir=string(e,libs);fs::create_directories(files/"runtime/vulkan");
 for(auto stem:{"cpu_cpp","runtime_cpp","vulkan_cpp"}){fs::path name="libphoenix_"+std::string(stem)+".so",dest=files/"runtime"/name;unlink(dest.c_str());symlink((nativeDir/name).c_str(),dest.c_str());}
 fs::path provider=files/"runtime/vulkan/libphoenix_vulkan.so";unlink(provider.c_str());symlink((nativeDir/"libphoenix_vulkan.so").c_str(),provider.c_str());
 void* module=dlopen((nativeDir/"libphoenix_vulkan.so").c_str(),RTLD_NOW|RTLD_LOCAL);if(module){auto count=reinterpret_cast<int(*)()>(dlsym(module,"phoenix_device_count"));auto info=reinterpret_cast<int(*)(unsigned,char*,char*,unsigned*,unsigned*)>(dlsym(module,"phoenix_device_info"));auto optional=reinterpret_cast<int(*)(unsigned,unsigned,char*,char*,unsigned*,unsigned*)>(dlsym(module,"phoenix_device_info_options"));if(count&&info)for(int i=0,n=count();i<n;i++){char uuid[33]{},name[256]{};unsigned vendor=0,supported=0,compatibility=0;if(!info(i,uuid,name,&vendor,&supported)){if(optional)optional(i,1u<<30,uuid,name,&vendor,&compatibility);deviceList.push_back({{"id","vulkan:"+std::string(uuid)},{"name",name},{"supported",bool(supported)},{"compatibility_supported",bool(compatibility)},{"vendor",vendor}});}}dlclose(module);}
 {std::lock_guard lock(workMutex);report["device_name"]=deviceList.empty()?"No Vulkan device":deviceList[0]["name"];report["devices"]=deviceList;}
 std::thread(worker).detach();});
}
extern "C" JNIEXPORT jstring JNICALL Java_org_phoenix_museum_MainActivity_devices(JNIEnv* e,jclass){Json list=deviceList;return e->NewStringUTF(list.dump().c_str());}
extern "C" JNIEXPORT jstring JNICALL Java_org_phoenix_museum_MainActivity_render(JNIEnv* e,jclass,jstring options,jfloat yaw,jfloat pitch,jfloat zoom,jint width,jint height){try{draw(Json::parse(string(e,options)),yaw,pitch,zoom,width,height);{std::lock_guard lock(workMutex);report["render_error"]="";}return e->NewStringUTF("");}catch(const std::exception& ex){{std::lock_guard lock(workMutex);report["render_error"]=ex.what();}__android_log_print(ANDROID_LOG_ERROR,"PhoenixMuseum","Render: %s",ex.what());return e->NewStringUTF(ex.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_phoenix_museum_MainActivity_status(JNIEnv* e,jclass){std::lock_guard lock(workMutex);Json r=report;r["processing_seconds"]=busy?std::chrono::duration<double>(Clock::now()-callStarted).count():0;if(latestCapture.time_since_epoch().count())r["age_seconds"]=std::chrono::duration<double>(Clock::now()-latestCapture).count();return e->NewStringUTF(r.dump().c_str());}
extern "C" JNIEXPORT void JNICALL Java_org_phoenix_museum_MainActivity_pauseProcessing(JNIEnv*,jclass,jboolean pause){paused=pause;workWake.notify_one();}
extern "C" JNIEXPORT void JNICALL Java_org_phoenix_museum_MainActivity_saveCapture(JNIEnv*,jclass){captureRequested=true;saveInferenceRequested=true;}

extern "C" JNIEXPORT void JNICALL Java_org_phoenix_museum_MainActivity_contextCreated(JNIEnv*,jclass){
 sceneProgram=finishProgram=displayProgram=captureProgram=emptyVAO=0;shadowFbo=shadowTex=captureFbo=captureColor=captureMotion=0;renderW=renderH=captureW=captureH=0;
 targets[0]={};targets[1]={};processedTex[0]=processedTex[1]=matchedTex[0]=matchedTex[1]=lowInputTex[0]=lowInputTex[1]=0;generation++;lastConfig.clear();
}
