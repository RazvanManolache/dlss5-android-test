#pragma once
static const char* sceneVertex=R"GLSL(#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 tangent;
layout(location=4) in uvec4 boneIds;
layout(location=5) in vec4 boneWeights;
layout(location=6) in vec3 blinkDelta;
layout(location=7) in vec3 smileDelta;
layout(location=8) in vec3 jawDelta;
uniform mat4 world,previousWorld,vp,previousVP,lightVP;
uniform sampler2D bones;
uniform int boneCount,skinned;
uniform vec3 face,previousFace;
out vec3 p,n,t;
out float handedness;
out vec2 uv;
out vec4 currentClip,previousClip,shadowClip;
vec3 boneTransform(vec3 value,uint id,int old,float w){
 int y=int(id)+old*boneCount;
 vec4 v=vec4(value,w);
 return vec3(dot(texelFetch(bones,ivec2(0,y),0),v),dot(texelFetch(bones,ivec2(1,y),0),v),dot(texelFetch(bones,ivec2(2,y),0),v));
}
void main(){
 vec3 cp=position,op=position,cn=normal,ct=tangent.xyz;
 if(skinned==1){
  vec3 a=position+blinkDelta*face.x+smileDelta*face.y+jawDelta*face.z;
  vec3 b=position+blinkDelta*previousFace.x+smileDelta*previousFace.y+jawDelta*previousFace.z;
  cp=vec3(0);op=vec3(0);cn=vec3(0);ct=vec3(0);
  for(int k=0;k<4;k++){cp+=boneTransform(a,boneIds[k],0,1.0)*boneWeights[k];op+=boneTransform(b,boneIds[k],1,1.0)*boneWeights[k];cn+=boneTransform(normal,boneIds[k],0,0.0)*boneWeights[k];ct+=boneTransform(tangent.xyz,boneIds[k],0,0.0)*boneWeights[k];}
 }
 vec4 wp=world*vec4(cp,1);p=wp.xyz;n=normalize(mat3(world)*cn);t=normalize(mat3(world)*ct);handedness=tangent.w;uv=texcoord;
 currentClip=vp*wp;previousClip=previousVP*previousWorld*vec4(op,1);shadowClip=lightVP*wp;gl_Position=currentClip;
}
)GLSL";
static const char* sceneFragment=R"GLSL(#version 300 es
precision highp float;
in vec3 p,n,t;
in float handedness;
in vec2 uv;
in vec4 currentClip,previousClip,shadowClip;
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
layout(location=2) out vec4 worldPosition;
layout(location=3) out vec4 normalRough;
uniform sampler2D colorMap,normalMap,roughMap,shadowMap;
uniform vec4 material;
uniform vec3 eye;
uniform int quality,shadowPass;
uniform float lightPhase;
uniform vec2 extent;
vec3 linear(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 srgb(vec3 c){return mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),c));}
float shadow(){
 if(quality==0||quality==3||quality==6)return 1.0;
 vec3 q=shadowClip.xyz/shadowClip.w*.5+.5;if(any(lessThan(q,vec3(0)))||any(greaterThan(q,vec3(1))))return 1.0;
 float sum=0.0;int radius=quality==2?2:1;
 for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){if(abs(x)>radius||abs(y)>radius)continue;sum+=q.z-.0025<texture(shadowMap,q.xy+vec2(x,y)/1024.0).r?1.0:.15;}
 return sum/float((radius*2+1)*(radius*2+1));
}
void main(){
 int kind=int(material.w+.1);vec4 tex=texture(colorMap,uv);
 if(kind>=10&&tex.a<.5)discard;
 if(shadowPass==1){color=vec4(1);motion=vec4(0);worldPosition=vec4(0);normalRough=vec4(0);return;}
 vec3 albedo=material.rgb,N=normalize(n);float rough=.68;
 bool mapped=kind==1||kind>=6;
 bool faceSurface=kind>=10&&uv.x<.333333;
 if(mapped){albedo=linear(tex.rgb);rough=max(.22,texture(roughMap,uv).r);if(quality!=0){vec3 T=normalize(t-N*dot(t,N));vec3 B=cross(N,T)*handedness;vec3 nm=texture(normalMap,uv).xyz*2.0-1.0;nm=normalize(mix(vec3(0,0,1),nm,kind>=10?.55:1.0));N=normalize(T*nm.x+B*nm.y+N*nm.z);}}
 if(kind==3){vec2 joint=smoothstep(vec2(.485),vec2(.5),abs(fract(p.xz*.7)-.5));albedo*=mix(1.0,.65,max(joint.x,joint.y));rough=.24;}
 if(kind==4)rough=.26;
 vec3 V=normalize(eye-p);vec3 lighting=albedo*(.09+.11*max(N.y,0.0));
 const vec3 positions[8]=vec3[8](vec3(-2.7,4.2,3.5),vec3(3.2,3.6,2),vec3(-5,4.8,-4),vec3(5,4.8,-4),vec3(-4.5,5,-8),vec3(4.5,5,-8),vec3(-4.6,3.5,9.5),vec3(6,3.8,9.5));
 const vec3 colors[8]=vec3[8](vec3(23,17,11),vec3(9,13,19),vec3(18,10,5),vec3(7,12,15),vec3(13,9,6),vec3(8,8,13),vec3(22,27,32),vec3(30,22,15));
 int count=quality==0?2:8;
 for(int k=0;k<8;k++){if(k>=count)break;vec3 delta=positions[k]-p;float dist=dot(delta,delta);vec3 L=normalize(delta);float ndl=max(dot(N,L),0.0);vec3 H=normalize(L+V);float spec=quality==0?0.0:pow(max(dot(N,H),0.0),mix(120.0,5.0,rough))*.28;
 float pulse=1.0+.07*sin(lightPhase*1.3+float(k)*.9);float visibility=k==0?shadow():1.0;
 float wrap=(faceSurface&&quality>=3)?.08*max(dot(N,L)+.3,0.0):0.0;
 lighting+=(albedo*(ndl+wrap)+vec3(spec))*colors[k]*pulse/(dist+2.0)*visibility;
 }
 if(quality==5)lighting+=albedo*vec3(.10,.07,.04)*(1.0-max(N.y,0.0));
 if(kind==5)lighting=material.rgb;
 vec3 finish=lighting/(lighting+vec3(1));finish=srgb(finish);
 color=vec4(clamp(finish,0.0,1.0),faceSurface||kind==1?1.0:0.0);
 vec2 a=currentClip.xy/currentClip.w*.5+.5,b=previousClip.xy/previousClip.w*.5+.5;
 // glReadPixels uses bottom-up rows. Host capture flips both rows and Y motion.
 motion=vec4((b-a)*extent,0,1);worldPosition=vec4(p,float(kind+1));normalRough=vec4(N,rough);
}
)GLSL";
static const char* fullVertex=R"GLSL(#version 300 es
precision highp float;
out vec2 uv;
void main(){uv=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(uv*2.0-1.0,0,1);}
)GLSL";
static const char* finishFragment=R"GLSL(#version 300 es
precision highp float;
in vec2 uv;out vec4 result;
uniform sampler2D sourceImage,worldImage,normalImage;
uniform mat4 vp;
uniform vec3 eye;
uniform int quality;
void main(){
 vec4 base=texture(sourceImage,uv),P=texture(worldImage,uv),N=texture(normalImage,uv);vec3 c=base.rgb;
 if(P.w<.5){result=vec4(.055,.065,.08,0);return;}
 if(quality==1||quality==2||quality==5||quality==6){
  float occlusion=0.0;int taps=quality==2?16:8;vec2 pixel=1.0/vec2(textureSize(sourceImage,0));
  for(int k=0;k<16;k++){if(k>=taps)break;float a=float(k)*2.399963;vec2 q=uv+vec2(cos(a),sin(a))*pixel*(4.0+float(k));vec4 Q=texture(worldImage,q);vec3 d=Q.xyz-P.xyz;float l=length(d);if(Q.w>.5&&l<.65&&l>.015)occlusion+=max(dot(normalize(d),N.xyz)-.18,0.0)*(1.0-l/.65);}
  c*=1.0-min(.6,occlusion/float(taps)*2.0);
 }
 if(quality==1||quality==2||quality==6){
  vec3 direction=reflect(normalize(P.xyz-eye),normalize(N.xyz));vec3 reflected=vec3(.10,.13,.19);int steps=quality==2?24:8;
  for(int k=1;k<=24;k++){if(k>steps)break;vec3 q=P.xyz+normalize(N.xyz)*.04+direction*(float(k)*.28);vec4 clip=vp*vec4(q,1);vec2 st=clip.xy/clip.w*.5+.5;if(clip.w<=0.0||any(lessThan(st,vec2(0)))||any(greaterThan(st,vec2(1))))break;vec4 at=texture(worldImage,st);if(at.w>.5&&length(at.xyz-q)<.3){reflected=texture(sourceImage,st).rgb;break;}}
  float amount=(int(P.w+.1)==4||int(P.w+.1)==5)?.16:.035;c=mix(c,reflected,amount*(1.0-N.w));
 }
 if(quality==2&&base.a>.5){vec2 step=1.0/vec2(textureSize(sourceImage,0));vec3 soft=vec3(0);float weights=0.0;for(int k=-3;k<=3;k++){vec2 q=uv+vec2(k,0)*step*1.5;vec4 v=texture(sourceImage,q);if(v.a>.5){soft+=v.rgb;weights+=1.0;}}if(weights>0.0)c=mix(c,soft/weights,.24);}
 result=vec4(clamp(c,0.0,1.0),base.a);
}
)GLSL";
static const char* displayFragment=R"GLSL(#version 300 es
precision highp float;
in vec2 uv;out vec4 result;
uniform sampler2D leftImage,rightImage;
uniform int view;
uniform float divider;
void main(){vec4 a=texture(leftImage,uv),b=texture(rightImage,uv);vec3 c=view==1?a.rgb:view==2?b.rgb:view==3?abs(a.rgb-b.rgb)*6.0:uv.x<divider?a.rgb:b.rgb;if(view==0&&abs(uv.x-divider)<.001)c=vec3(.72,.42,1);result=vec4(c,1);}
)GLSL";
static const char* captureFragment=R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec2 uv;
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
uniform sampler2D colorImage,motionImage;
uniform vec2 scale;
void main(){color=texture(colorImage,uv);vec2 m=clamp(texture(motionImage,uv).xy*scale,vec2(-1024.0),vec2(1023.96875));uvec2 bits=uvec2(round(m*32.0+32768.0));motion=vec4(float(bits.x&255u),float(bits.x>>8),float(bits.y&255u),float(bits.y>>8))/255.0;}
)GLSL";
