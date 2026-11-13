#include "wheels.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::wheels {
std::vector<uint16_t> chain(const std::vector<int32_t>& wheels,const std::vector<int16_t>& parents){
    std::set<uint16_t> out;
    for(auto w:wheels){
        if(w<=0||size_t(w)>=parents.size())continue;
        int j=w;
#ifndef MUTANT_WHEELS_ROOT
        for(unsigned up=0;up<=Ancestors&&j>0&&size_t(j)<parents.size();++up){out.insert(uint16_t(j));j=parents[size_t(j)];}
#else
        for(unsigned up=0;up<=Ancestors&&j>=0&&size_t(j)<parents.size();++up){out.insert(uint16_t(j));j=parents[size_t(j)];}
#endif
    }
    std::vector<uint16_t> v(out.begin(),out.end());
    if(v.size()>ds::AnimationJoints)v.resize(ds::AnimationJoints);
    return v;
}
ds::VehicleJoint toWire(const Joint& j){
    ds::VehicleJoint w;w.index=j.index;
    for(int k=0;k<4;++k)w.rotation[k]=int16_t(std::lround(std::clamp(j.rotation[k],-1.f,1.f)*32767));
    for(int k=0;k<3;++k)w.position[k]=int16_t(std::clamp<long>(std::lround(double(j.position[k])*1000),-32767,32767));
    return w;
}
Joint fromWire(const ds::VehicleJoint& w){
    Joint j;j.index=w.index;double n=0;
    for(int k=0;k<4;++k){j.rotation[k]=w.rotation[k]/32767.f;n+=double(j.rotation[k])*j.rotation[k];}
    if(n>1e-6){float s=float(1/std::sqrt(n));for(auto& c:j.rotation)c*=s;}else j.rotation={0,0,0,1};
    for(int k=0;k<3;++k)j.position[k]=w.position[k]/1000.f;
    return j;
}
std::vector<Joint> blend(const std::vector<Joint>& from,const std::vector<Joint>& to,float f){
    f=std::clamp(f,0.f,1.f);auto out=from;
    for(auto& a:out){
        auto z=std::find_if(to.begin(),to.end(),[&](const Joint& j){return j.index==a.index;});if(z==to.end())continue;
        float dot=0;for(int k=0;k<4;++k)dot+=a.rotation[k]*z->rotation[k];
#ifndef MUTANT_WHEELS_LONG_WAY
        float sign=dot<0?-1.f:1.f;
#else
        float sign=1.f;
#endif
        double n=0;for(int k=0;k<4;++k){a.rotation[k]=a.rotation[k]+f*(sign*z->rotation[k]-a.rotation[k]);n+=double(a.rotation[k])*a.rotation[k];}
        if(n>1e-9){float s=float(1/std::sqrt(n));for(auto& c:a.rotation)c*=s;}
        for(int k=0;k<3;++k)a.position[k]+=f*(z->position[k]-a.position[k]);
    }
    return out;
}
#ifdef _WIN32
namespace {
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
struct Record {uint64_t joints=0,table=0;int32_t count=0;};
std::optional<Record> record(const discovery::Sampler& s,uint64_t vehicle){
    uint64_t model=0,skeleton=0;int32_t k=0;Record r;
    if(!q(s,vehicle+Model,model)||!model||!s.read(model+RecordIndex,&k,4)||(k!=0&&k!=1))return std::nullopt;
    uint64_t rec=model+Records+RecordStride*uint64_t(k);
    if(!q(s,rec+RecordSkeleton,skeleton)||!skeleton||!q(s,rec+RecordJoints,r.joints)||!r.joints)return std::nullopt;
    if(!s.read(skeleton+SkeletonCount,&r.count,4)||r.count<=0||r.count>int32_t(MaxJoints)||!q(s,skeleton+SkeletonTable,r.table)||!r.table)return std::nullopt;
    return r;
}
}
std::optional<Read> read(const discovery::Sampler& s,uint64_t vehicle,uint8_t kind){
    if(kind!=ds::Motorbike&&kind!=ds::Truck)return std::nullopt;
    auto r=record(s,vehicle);if(!r)return std::nullopt;
    unsigned n=kind==ds::Motorbike?BikeWheelCount:TruckWheelCount;std::vector<int32_t> wheels(n);
    if(!s.read(vehicle+(kind==ds::Motorbike?BikeWheels:TruckWheels),wheels.data(),4*n))return std::nullopt;
    // The handle joint from the resource, when the resource is the one the wheel indices came from.
    {uint64_t h=0,o=0,res=0;int32_t first=0,handle=-1;
     if(q(s,vehicle+ResourceHandle,h)&&q(s,h,o)&&q(s,o+ResourceObject,res)&&s.read(res+ResourceFirstWheel,&first,4)&&first==wheels[0]
        &&s.read(res+(kind==ds::Motorbike?BikeHandle:TruckHandle),&handle,4)&&handle>0)wheels.push_back(handle);}
    std::vector<uint8_t> table(size_t(r->count)*SkeletonStride);
    if(!s.read(r->table,table.data(),table.size()))return std::nullopt;
    std::vector<int16_t> parents(size_t(r->count));
    for(size_t j=0;j<parents.size();++j)std::memcpy(&parents[j],table.data()+j*SkeletonStride+SkeletonParent,2);
    // A model small enough goes whole (every joint but the root): the bike's pose routine also leans its body and moves
    // the front pair's arms and the handlebar linkage, not only the wheel chains (live: 16 of 40 joints matched).
    std::vector<uint16_t> picked;
    if(size_t(r->count)-1<=ds::AnimationJoints)for(int32_t j=1;j<r->count;++j)picked.push_back(uint16_t(j));
    else picked=chain(wheels,parents);
    if(picked.empty())return std::nullopt;
    std::vector<uint8_t> pose(size_t(picked.back()+1)*JointStride);
    if(!s.read(r->joints,pose.data(),pose.size()))return std::nullopt;
    Read out;out.skeleton=uint16_t(r->count);
    for(auto j:picked){Joint x;x.index=j;std::memcpy(x.rotation.data(),pose.data()+j*JointStride,16);std::memcpy(x.position.data(),pose.data()+j*JointStride+0x10,12);
        bool sane=true;for(auto c:x.rotation)sane=sane&&std::isfinite(c)&&std::fabs(c)<=1.01f;for(auto c:x.position)sane=sane&&std::isfinite(c)&&std::fabs(c)<30;
        if(sane)out.joints.push_back(x);}
    if(out.joints.empty())return std::nullopt;
    return out;
}
bool write(const discovery::Sampler& s,uint64_t vehicle,uint16_t skeleton,const std::vector<Joint>& joints){
    auto r=record(s,vehicle);if(!r||r->count!=int32_t(skeleton)||joints.empty())return false;
    for(auto& j:joints)if(j.index>=skeleton)return false;
    uint64_t first=r->joints,last=r->joints+uint64_t(skeleton)*JointStride;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<void*>(first),&info,sizeof info)!=sizeof info||info.State!=MEM_COMMIT||info.Protect!=PAGE_READWRITE
       ||last>reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize)return false;
    for(auto& j:joints){auto at=reinterpret_cast<uint8_t*>(first+uint64_t(j.index)*JointStride);
        std::memcpy(at,j.rotation.data(),16);std::memcpy(at+0x10,j.position.data(),12);}
    InterlockedOr64(reinterpret_cast<volatile LONG64*>(vehicle+0x88),1);   // changed: the bit the game's transform writer sets
    return true;
}
#endif
}
