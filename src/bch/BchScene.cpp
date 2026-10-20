#include "bch/BchScene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace promptemon::bch {
namespace {
using Bytes = std::vector<std::uint8_t>;

std::uint16_t u16(const Bytes& b, std::size_t o){ if(o+2>b.size()) throw std::runtime_error("BCH truncated"); return b[o]|(std::uint16_t(b[o+1])<<8); }
std::int16_t s16(const Bytes& b, std::size_t o){ return static_cast<std::int16_t>(u16(b,o)); }
std::uint32_t u32(const Bytes& b, std::size_t o){ if(o+4>b.size()) throw std::runtime_error("BCH truncated"); return std::uint32_t(b[o])|(std::uint32_t(b[o+1])<<8)|(std::uint32_t(b[o+2])<<16)|(std::uint32_t(b[o+3])<<24); }
void put32(Bytes& b,std::size_t o,std::uint32_t v){ if(o+4>b.size()) throw std::runtime_error("BCH relocation out of bounds"); for(int i=0;i<4;++i)b[o+i]=std::uint8_t(v>>(i*8)); }
float f32(const Bytes& b,std::size_t o){ auto v=u32(b,o); float f; std::memcpy(&f,&v,4); return f; }
std::string cstr(const Bytes& b,std::size_t o){ if(!o||o>=b.size()) return {}; std::size_t e=o; while(e<b.size()&&b[e])++e; return std::string(reinterpret_cast<const char*>(b.data()+o),e-o); }

struct Header { std::uint8_t bc=0; std::uint32_t main=0,str=0,gpu=0,data=0,dataExt=0,reloc=0,relocLen=0; };
Header relocate(Bytes& b){
    if(b.size()<0x40||b[0]!='B'||b[1]!='C'||b[2]!='H') throw std::runtime_error("Not BCH");
    Header h; h.bc=b[4]; h.main=u32(b,8); h.str=u32(b,12); h.gpu=u32(b,16); h.data=u32(b,20);
    if(h.bc>0x20){ h.dataExt=u32(b,24); h.reloc=u32(b,28); h.relocLen=u32(b,52); }
    else { h.reloc=u32(b,24); h.relocLen=u32(b,44); }
    for(std::uint32_t o=h.reloc;o+4<=h.reloc+h.relocLen && o+4<=b.size();o+=4){
        const auto v=u32(b,o); const auto off=v&0x1ffffffu; const auto fl=v>>25;
        auto patch=[&](std::uint64_t p,std::uint32_t base,std::uint32_t hi=0){ if(p+4<=b.size()){ auto x=u32(b,p)+base; x=(x&0x7fffffffu)|hi; put32(b,p,x); } };
        if(fl==0) patch(std::uint64_t(off)*4+h.main,h.main);
        else if(fl==1) patch(std::uint64_t(off)+h.main,h.str);
        else if(fl==2) patch(std::uint64_t(off)*4+h.main,h.gpu);
        else if(fl==7||fl==0xc) patch(std::uint64_t(off)*4+h.main,h.data);
        const std::uint64_t p=std::uint64_t(off)*4+h.gpu;
        if(h.bc>=0x21){
            if(fl==0x25||fl==0x26) patch(p,h.data);
            else if(fl==0x27) patch(p,h.data,0x80000000u);
            else if(fl==0x28) patch(p,h.data);
            else if(fl==0x2b) patch(p,h.dataExt);
            else if(fl==0x2c) patch(p,h.dataExt,0x80000000u);
            else if(fl==0x2d) patch(p,h.dataExt);
        }
    }
    return h;
}

Mat4 identity(){ Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
Mat4 mul(const Mat4&a,const Mat4&b){ Mat4 r{}; for(int c=0;c<4;c++)for(int rr=0;rr<4;rr++)for(int k=0;k<4;k++)r.m[c*4+rr]+=a.m[k*4+rr]*b.m[c*4+k]; return r; }
Mat4 translation(Vec3 v){ auto r=identity(); r.m[12]=v.x;r.m[13]=v.y;r.m[14]=v.z;return r; }
Mat4 scale(Vec3 v){ auto r=identity();r.m[0]=v.x;r.m[5]=v.y;r.m[10]=v.z;return r; }
Quat normalize(Quat q){ float l=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w); if(l<1e-8f)return {}; q.x/=l;q.y/=l;q.z/=l;q.w/=l;return q; }
Mat4 qmat(Quat q){ q=normalize(q); float x=q.x,y=q.y,z=q.z,w=q.w; Mat4 r=identity();
    r.m[0]=1-2*y*y-2*z*z; r.m[4]=2*x*y-2*z*w; r.m[8]=2*x*z+2*y*w;
    r.m[1]=2*x*y+2*z*w; r.m[5]=1-2*x*x-2*z*z; r.m[9]=2*y*z-2*x*w;
    r.m[2]=2*x*z-2*y*w; r.m[6]=2*y*z+2*x*w; r.m[10]=1-2*x*x-2*y*y; return r; }
Quat euler(Vec3 r){ float cx=std::cos(r.x*.5f),sx=std::sin(r.x*.5f),cy=std::cos(r.y*.5f),sy=std::sin(r.y*.5f),cz=std::cos(r.z*.5f),sz=std::sin(r.z*.5f); return normalize({sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz}); }
Mat4 trs(Vec3 t,Quat r,Vec3 s){ return mul(translation(t),mul(qmat(r),scale(s))); }
Vec3 transformPoint(const Mat4&m,Vec3 v){ return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z+m.m[12],m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z+m.m[13],m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]}; }
Mat4 inverseAffine(const Mat4&m){ const float a00=m.m[0],a01=m.m[4],a02=m.m[8],a10=m.m[1],a11=m.m[5],a12=m.m[9],a20=m.m[2],a21=m.m[6],a22=m.m[10]; float det=a00*(a11*a22-a12*a21)-a01*(a10*a22-a12*a20)+a02*(a10*a21-a11*a20); if(std::fabs(det)<1e-10f)return identity(); float id=1.0f/det; Mat4 r=identity(); r.m[0]=(a11*a22-a12*a21)*id; r.m[4]=(a02*a21-a01*a22)*id; r.m[8]=(a01*a12-a02*a11)*id; r.m[1]=(a12*a20-a10*a22)*id; r.m[5]=(a00*a22-a02*a20)*id; r.m[9]=(a02*a10-a00*a12)*id; r.m[2]=(a10*a21-a11*a20)*id; r.m[6]=(a01*a20-a00*a21)*id; r.m[10]=(a00*a11-a01*a10)*id; Vec3 t{m.m[12],m.m[13],m.m[14]}; Vec3 nt=transformPoint(r,{-t.x,-t.y,-t.z}); r.m[12]=nt.x;r.m[13]=nt.y;r.m[14]=nt.z; return r; }

struct Pica {
    std::vector<std::uint32_t> cmd=std::vector<std::uint32_t>(0x10000); std::array<std::vector<float>,96> uni{};
    Pica(const Bytes&b,std::size_t p,std::uint32_t words){ std::uint32_t read=0,cur=0; while(read<words&&p+8<=b.size()){ auto param=u32(b,p), head=u32(b,p+4);p+=8;read+=2; auto id=std::uint16_t(head); auto mask=(head>>16)&0xf; auto extra=(head>>20)&0x7ff; bool cons=head&0x80000000u; std::vector<float> temp; auto write=[&](std::uint16_t i,std::uint32_t v){cmd[i]=(cmd[i]&((~mask)&0xf))|(v&(0xfffffff0u|mask));}; write(id,param); if(id==0x23d)break; if(id==0x2c0)cur=param&0x7fffffff; else if(id==0x2c1){float f;auto x=cmd[id];std::memcpy(&f,&x,4);temp.push_back(f);} for(std::uint32_t i=0;i<extra&&p+4<=b.size();++i){if(cons)++id;write(id,u32(b,p));p+=4;++read;if(id>0x2c0&&id<0x2c9){float f;auto x=cmd[id];std::memcpy(&f,&x,4);temp.push_back(f);}} if(cur<96&&!temp.empty())uni[cur].insert(uni[cur].end(),temp.begin(),temp.end()); p=(p+7)&~std::size_t(7); } }
    std::uint32_t param(int i)const{return cmd[i];}
    std::array<int,23> mainPerm()const{std::uint64_t v=param(0x2bb)|(std::uint64_t(param(0x2bc))<<32);std::array<int,23>r{};for(int i=0;i<23;i++)r[i]=(v>>(i*4))&0xf;return r;}
    std::array<int,23> bufPerm()const{std::uint64_t v=param(0x204)|(std::uint64_t(param(0x205)&0xffff)<<32);std::array<int,23>r{};for(int i=0;i<23;i++)r[i]=(v>>(i*4))&0xf;return r;}
    std::array<std::pair<int,int>,23> formats()const{std::uint64_t v=param(0x201)|(std::uint64_t(param(0x202))<<32);std::array<std::pair<int,int>,23>r{};for(int i=0;i<23;i++){int x=(v>>(i*4))&0xf;r[i]={x&3,x>>2};}return r;}
    int stride()const{return (param(0x205)>>16)&0xff;} int total()const{return param(0x205)>>28;} std::uint32_t addr()const{return param(0x203);} std::uint32_t idxAddr()const{return param(0x227)&0x7fffffff;} bool idx16()const{return param(0x227)>>31;} std::uint32_t idxCount()const{return param(0x228);}
};

std::vector<float> readAttr(const Bytes&b,std::size_t& p,int type,int n){std::vector<float>v;for(int i=0;i<n;i++){if(type==0){v.push_back((std::int8_t)b.at(p++));}else if(type==1){v.push_back(b.at(p++));}else if(type==2){v.push_back(s16(b,p));p+=2;}else{v.push_back(f32(b,p));p+=4;}}return v;}
Vec3 lerp(Vec3 a,Vec3 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
Quat slerp(Quat a,Quat b,float t){ a=normalize(a);b=normalize(b);float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0){b={-b.x,-b.y,-b.z,-b.w};d=-d;}if(d>.9995f)return normalize({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});float th=std::acos(std::clamp(d,-1.0f,1.0f)),s=std::sin(th);float aa=std::sin((1-t)*th)/s,bb=std::sin(t*th)/s;return {a.x*aa+b.x*bb,a.y*aa+b.y*bb,a.z*aa+b.z*bb,a.w*aa+b.w*bb};}
template<class T,class F>T sample(const std::vector<T>&v,float start,float end,float frame,const T&fallback,F interp){if(v.empty())return fallback;if(v.size()==1)return v[0];float f=std::clamp(frame,start,end)-start;float span=std::max(1.0f,end-start);float pos=f/span*float(v.size()-1);auto i=std::min<std::size_t>(v.size()-2,std::size_t(std::floor(pos)));return interp(v[i],v[i+1],pos-float(i));}
} // namespace

const Animation* Scene::findAnimation(const std::string& name)const{for(auto& a:animations_)if(a.name==name)return &a;return nullptr;}

Scene Scene::parse(const std::vector<std::uint8_t>& modelRaw,const std::vector<std::uint8_t>& animRaw){
    Scene out; Bytes b=modelRaw; auto h=relocate(b); auto modelsOff=u32(b,h.main),modelCount=u32(b,h.main+4); if(!modelCount)throw std::runtime_error("BCH has no model"); auto mh=u32(b,modelsOff); std::size_t p=mh+4+48; auto matsOff=u32(b,p),matsCount=u32(b,p+4); (void)matsOff;(void)matsCount;p+=12;auto objOff=u32(b,p),objCount=u32(b,p+4);p+=8;p+=(h.bc>6?0x28:0x20);auto skOff=u32(b,p),skCount=u32(b,p+4);p+=12;auto visOff=u32(b,p);(void)visOff;p+=8;out.modelName_=cstr(b,u32(b,p));p+=4;auto objNames=u32(b,p),objNamesOff=u32(b,p+4);(void)objNames;(void)objNamesOff;
    p=skOff; for(std::uint32_t i=0;i<skCount;i++,p+=100){Bone bone;bone.parent=s16(b,p+4);bone.bindScale={f32(b,p+8),f32(b,p+12),f32(b,p+16)};bone.bindRotation={f32(b,p+20),f32(b,p+24),f32(b,p+28)};bone.bindTranslation={f32(b,p+32),f32(b,p+36),f32(b,p+40)};bone.name=cstr(b,u32(b,p+92));out.boneIndex_[bone.name]=out.bones_.size();out.bones_.push_back(bone);} for(std::size_t i=0;i<out.bones_.size();++i){auto local=trs(out.bones_[i].bindTranslation,euler(out.bones_[i].bindRotation),out.bones_[i].bindScale);out.bones_[i].bindGlobal=out.bones_[i].parent>=0?mul(out.bones_[out.bones_[i].parent].bindGlobal,local):local;out.bones_[i].inverseBind=inverseAffine(out.bones_[i].bindGlobal);}
    struct Obj{std::uint16_t mat,fl,node,prio;std::uint32_t vc,vw,fo,fc;};std::vector<Obj>objects;for(std::uint32_t i=0;i<objCount;i++){auto q=objOff+i*0x38;objects.push_back({u16(b,q),u16(b,q+2),u16(b,q+4),u16(b,q+6),u32(b,q+8),u32(b,q+12),u32(b,q+16),u32(b,q+20)});} for(std::size_t oi=0;oi<objects.size();++oi){auto&o=objects[oi];if((o.fl&1)&&h.bc!=8)continue;Pica pc(b,o.vc,o.vw);auto mp=pc.mainPerm(); auto bp=pc.bufPerm(); auto fm=pc.formats();auto u6=pc.uni[6],u7=pc.uni[7];auto pop=[&](std::vector<float>&x){if(x.empty())return 0.0f;auto v=x.back();x.pop_back();return v;};Vec4 posOff{pop(u6),pop(u6),pop(u6),pop(u6)};float tex0=pop(u7),tex1=pop(u7),tex2=pop(u7),bwScale=pop(u7),posScale=pop(u7),normalScale=pop(u7),tanScale=pop(u7),colorScale=pop(u7);(void)tex1;(void)tex2;(void)tanScale;(void)colorScale;Mesh mesh;mesh.materialId=o.mat;mesh.name="mesh"+std::to_string(oi);
        for(std::uint32_t fi=0;fi<o.fc;fi++){auto fb=o.fo+fi*0x34;auto skin=u16(b,fb),nodeCount=u16(b,fb+2);std::vector<std::uint16_t> nodes;for(int n=0;n<nodeCount;n++)nodes.push_back(u16(b,fb+4+n*2));auto fcmd=u32(b,fb+0x2c),fwords=u32(b,fb+0x30);Pica ic(b,fcmd,fwords);for(std::uint32_t ii=0;ii<ic.idxCount();ii++){auto index=ic.idx16()?u16(b,ic.idxAddr()+ii*2):b.at(ic.idxAddr()+ii);std::size_t vp=pc.addr()+std::size_t(index)*pc.stride();Vertex v;std::vector<float> boneIdx,boneWeight;for(int a=0;a<pc.total();a++){auto slot=bp[a]; auto att=mp[slot]; auto fmt=fm[slot];if(att==8)fmt.first=1;auto x=readAttr(b,vp,fmt.first,fmt.second+1);if(att==0&&x.size()>=3)v.position={x[0]*posScale+posOff.x,x[1]*posScale+posOff.y,x[2]*posScale+posOff.z};else if(att==1&&x.size()>=3)v.normal={x[0]*normalScale,x[1]*normalScale,x[2]*normalScale};else if(att==4&&x.size()>=2)v.uv={x[0]*tex0,x[1]*tex0};else if(att==7)boneIdx=x;else if(att==8)boneWeight=x;}for(std::size_t k=0;k<boneIdx.size()&&k<4;k++){auto bi=std::size_t(boneIdx[k]);if(bi<nodes.size())v.bones[v.influenceCount++]=nodes[bi];}if(v.influenceCount==0&&nodes.size()<=4){for(auto n:nodes)v.bones[v.influenceCount++]=n;}for(std::size_t k=0;k<boneWeight.size()&&k<4;k++)v.weights[k]=boneWeight[k]*bwScale;if(v.influenceCount&&boneWeight.empty())v.weights[0]=1.0f;if(skin!=1&&v.influenceCount){v.position=transformPoint(out.bones_[v.bones[0]].bindGlobal,v.position);}mesh.vertices.push_back(v);}}
        out.meshes_.push_back(std::move(mesh)); }
    Bytes ab=animRaw;auto ah=relocate(ab);auto animTable=u32(ab,ah.main+24*4),animCount=u32(ab,ah.main+25*4);for(std::uint32_t ai=0;ai<animCount;ai++){auto ao=u32(ab,animTable+ai*4);if(!ao)continue;Animation a;a.name=cstr(ab,u32(ab,ao));a.loop=(u32(ab,ao+4)&1)!=0;a.frames=f32(ab,ao+8);auto bt=u32(ab,ao+12),bc=u32(ab,ao+16);for(std::uint32_t ti=0;ti<bc;ti++){auto bo=u32(ab,bt+ti*4);auto type=u32(ab,bo+4);auto fl=u32(ab,bo+8);if(((type>>16)&0xf)!=7)continue;BoneTrack t;t.boneName=cstr(ab,u32(ab,bo));auto so=u32(ab,bo+12),ro=u32(ab,bo+16),to=u32(ab,bo+20);auto read3=[&](std::uint32_t off,std::uint32_t absent,std::uint32_t constant,Vec3Track&tr){if(fl&absent)return;tr.present=true;if(fl&constant){tr.constant=true;tr.value={f32(ab,off),f32(ab,off+4),f32(ab,off+8)};}else{tr.start=f32(ab,off);tr.end=f32(ab,off+4);auto dataOff=u32(ab,off+12),entries=u32(ab,off+16);for(std::uint32_t k=0;k<entries;k++)tr.samples.push_back({f32(ab,dataOff+k*12),f32(ab,dataOff+k*12+4),f32(ab,dataOff+k*12+8)});}};read3(so,0x20,4,t.scale);read3(to,8,1,t.translation);if(!(fl&0x10)){t.rotation.present=true;if(fl&2){t.rotation.constant=true;t.rotation.value={f32(ab,ro),f32(ab,ro+4),f32(ab,ro+8),f32(ab,ro+12)};}else{t.rotation.start=f32(ab,ro);t.rotation.end=f32(ab,ro+4);auto dataOff=u32(ab,ro+12),entries=u32(ab,ro+16);for(std::uint32_t k=0;k<entries;k++)t.rotation.samples.push_back({f32(ab,dataOff+k*16),f32(ab,dataOff+k*16+4),f32(ab,dataOff+k*16+8),f32(ab,dataOff+k*16+12)});}}a.tracks.push_back(std::move(t));}out.animations_.push_back(std::move(a));}
    return out;
}

std::vector<Mat4> Scene::samplePose(const Animation* a,float frame)const{std::vector<Vec3>s,t;std::vector<Quat>r;for(auto&b:bones_){s.push_back(b.bindScale);t.push_back(b.bindTranslation);r.push_back(euler(b.bindRotation));}if(a){for(auto&tr:a->tracks){auto it=boneIndex_.find(tr.boneName);if(it==boneIndex_.end())continue;auto i=it->second;if(tr.scale.present)s[i]=tr.scale.constant?tr.scale.value:sample(tr.scale.samples,tr.scale.start,tr.scale.end,frame,s[i],[](Vec3 a,Vec3 b,float t){return lerp(a,b,t);});if(tr.translation.present)t[i]=tr.translation.constant?tr.translation.value:sample(tr.translation.samples,tr.translation.start,tr.translation.end,frame,t[i],[](Vec3 a,Vec3 b,float t){return lerp(a,b,t);});if(tr.rotation.present)r[i]=tr.rotation.constant?tr.rotation.value:sample(tr.rotation.samples,tr.rotation.start,tr.rotation.end,frame,r[i],[](Quat a,Quat b,float t){return slerp(a,b,t);});}}
    std::vector<Mat4>global(bones_.size()),skin(bones_.size());for(std::size_t i=0;i<bones_.size();++i){auto local=trs(t[i],r[i],s[i]);global[i]=bones_[i].parent>=0?mul(global[bones_[i].parent],local):local;skin[i]=mul(global[i],bones_[i].inverseBind);}return skin;}
Vec3 Scene::skin(const Vertex&v,const std::vector<Mat4>&m)const{if(!v.influenceCount)return v.position;Vec3 out{};float sum=0;for(int i=0;i<v.influenceCount;i++){float w=v.weights[i];if(w==0&&i==0)w=1;auto bi=v.bones[i];if(bi>=m.size())continue;auto p=transformPoint(m[bi],v.position);out.x+=p.x*w;out.y+=p.y*w;out.z+=p.z*w;sum+=w;}if(sum>1e-5f&&std::fabs(sum-1)>1e-3f){out.x/=sum;out.y/=sum;out.z/=sum;}return out;}
} // namespace promptemon::bch
