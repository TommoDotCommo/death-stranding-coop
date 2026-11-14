#include "voidout.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::voidout {
constexpr uint64_t Manager=0x7bee060,Active=0x280,Pending=0x290,Counter=0x2a8,Dirty=0x2ac,Resource=0x2a0,Stride=0x60;
Table read(const discovery::Sampler& s,uint64_t base){
    Table t;uint64_t c=0,list=0;int32_t n=0;
    if(!s.read(base+Manager,&c,8)||!c||!s.read(c+Active,&n,4)||!s.read(c+Active+8,&list,8)||!s.read(c+Counter,&t.counter,4))return t;
    if(n<0||n>512||(n&&!list))return t;
    std::vector<uint8_t> raw(size_t(n)*Stride);if(n&&!s.read(list,raw.data(),raw.size()))return t;
    for(int i=0;i<n;++i){
        const uint8_t* r=raw.data()+size_t(i)*Stride;Crater k;uint32_t flags=0;uint32_t area=0;
        std::memcpy(k.position.data(),r+0x18,24);std::memcpy(&k.radius,r+0x30,4);std::memcpy(&k.id,r+0x48,4);
        std::memcpy(&area,r+0x50,4);std::memcpy(&k.yaw,r+0x54,4);std::memcpy(&flags,r+0x58,4);
        k.area=area&0xffff;k.disabled=flags&1;k.active=r[0x3c]!=0;
        bool sane=std::isfinite(k.radius)&&k.radius>0&&k.radius<1000;for(auto v:k.position)sane=sane&&std::isfinite(v)&&std::fabs(v)<100000;
        if(sane&&!(flags&2))t.craters.push_back(k);
    }
    t.readable=true;return t;
}
ds::CargoEvent encode(const Crater& k){
    ds::CargoEvent e;e.type=WireType;e.item=uint32_t(std::lround(double(k.radius)*100));e.ref=(k.area&0xffff)|(uint64_t(k.disabled)<<16);
    e.position=k.position;e.yaw=std::remainder(double(k.yaw),2*3.14159265358979323846);return e;
}
std::optional<Crater> decode(const ds::CargoEvent& e){
    if(e.type!=WireType||!e.item||e.item>100000)return std::nullopt;
    Crater k;k.radius=float(e.item)/100.f;k.area=uint32_t(e.ref&0xffff);k.disabled=(e.ref>>16)&1;k.position=e.position;k.yaw=float(e.yaw);
    for(auto v:k.position)if(!std::isfinite(v))return std::nullopt;
    return k;
}
std::vector<Crater> Sync::observe(const Table& t){
    std::vector<Crater> out;if(!t.readable)return out;
#ifndef MUTANT_VOIDOUT_LOAD
    if(primed_&&t.counter<counter_)primed_=false;                     // a load rewrote the list and the counter
#endif
    if(!primed_){primed_=true;counter_=t.counter;return out;}
    for(auto& k:t.craters){
        if(k.id<=counter_)continue;
#ifndef MUTANT_VOIDOUT_ECHO
        if(mine_.count(k.id))continue;
#endif
        out.push_back(k);
    }
    counter_=std::max(counter_,t.counter);
    return out;
}
bool Sync::wanted(const Table& t,const Crater& k){
    if(!t.readable)return false;
#ifndef MUTANT_VOIDOUT_TWICE
    for(auto& c:t.craters){double d=0;for(int i=0;i<3;++i){double x=c.position[size_t(i)]-k.position[size_t(i)];d+=x*x;}if(d<25)return false;}
#endif
    return true;
}
std::function<uint32_t()> nativeArea(discovery::Sampler& s){
#ifdef _WIN32
    static const uint8_t want[12]={0x48,0x85,0xc0,0x74,0x0e,0x80,0xb8,0xa8,0x00,0x00,0x00,0x00};uint8_t got[12]{};
    if(!s.read(s.base+0x262a8c7,got,12)||std::memcmp(got,want,12))throw std::runtime_error("area function differs");
    uint64_t base=s.base;
    return [&s,base]()->uint32_t{uint64_t module=0;if(!s.read(base+0x7be6318,&module,8)||!module)return 0xffff;
        return uint32_t(reinterpret_cast<uint16_t(*)(uint64_t)>(base+0x262a8c0)(module));};
#else
    (void)s;throw std::runtime_error("voidouts need the game");
#endif
}
std::function<uint32_t(const Crater&)> craterAdder(discovery::Sampler& s,Writable writable,Grow grow,Log log){
    uint64_t base=s.base;
    return [&s,base,writable,grow,log,logged=false](const Crater& k) mutable ->uint32_t{
        auto refuse=[&](){if(!logged){logged=true;if(log)log("VOIDOUT refused: crater identity or writable memory changed\n");}return uint32_t(0);};
        uint64_t c=0,resource=0;
        if(!s.read(base+Manager,&c,8)||!c)return 0;
#ifndef MUTANT_VOIDOUT_CLASS
        uint64_t v=0;if(!s.read(c,&v,8)||v!=base+0x3daf258)return refuse(); // constructor, game_offsets.md "Detecting and reproducing a voidout"
#endif
#ifndef MUTANT_VOIDOUT_WRITABLE
        if(!writable(c,Dirty+1))return refuse();
#endif
        if(!s.read(c+Resource,&resource,8)||!resource)return 0;   // the crater resource is not loaded
        uint64_t arr=c+Pending;int32_t n=0,cap=0;uint64_t list=0;
        if(!s.read(arr,&n,4)||!s.read(arr+4,&cap,4)||!s.read(arr+8,&list,8)||n<0||n>64)return 0;
#ifndef MUTANT_VOIDOUT_WRITABLE
        if(n&&(!list||!writable(list,size_t(n)*Stride)))return refuse();
#endif
        if(n+1>cap||!list){
            auto grown=grow(arr,list,n+1+(n+1)/8+2);
            if(!grown)return 0;
#ifndef MUTANT_VOIDOUT_WRITABLE
            if(!writable(grown,size_t(n+1)*Stride))return refuse();
#endif
            *reinterpret_cast<uint64_t*>(arr+8)=grown;list=grown;
        }
#ifndef MUTANT_VOIDOUT_WRITABLE
        if(!writable(list,size_t(n+1)*Stride))return refuse();
#endif
        auto* r=reinterpret_cast<uint8_t*>(list+uint64_t(n)*Stride);std::memset(r,0,Stride);
        std::memcpy(r+0x18,k.position.data(),24);std::memcpy(r+0x30,&k.radius,4);
        uint32_t id=++*reinterpret_cast<volatile uint32_t*>(c+Counter);std::memcpy(r+0x48,&id,4);
        uint32_t area=k.area;std::memcpy(r+0x50,&area,4);std::memcpy(r+0x54,&k.yaw,4);
        uint32_t flags=k.disabled?1u:0u;std::memcpy(r+0x58,&flags,4);
        r[0x3c]=1;                                                          // already activated: the crater only, no blast
        *reinterpret_cast<volatile int32_t*>(arr)=n+1;*reinterpret_cast<volatile uint8_t*>(c+Dirty)=1;
        return id;
    };
}
std::function<uint32_t(const Crater&)> nativeAdd(discovery::Sampler& s,Log log){
#ifdef _WIN32
    static const uint8_t grow[16]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41};uint8_t got[16]{};
    if(!s.read(s.base+0x2a6a260,got,16)||std::memcmp(got,grow,16))throw std::runtime_error("crater list function differs");
    auto writable=[](uint64_t at,size_t n){MEMORY_BASIC_INFORMATION info{};
        return at&&at<0x800000000000ULL&&n<=0x800000000000ULL-at
            &&VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE
            &&n<=info.RegionSize&&at-reinterpret_cast<uint64_t>(info.BaseAddress)<=info.RegionSize-n;};
    return craterAdder(s,writable,reinterpret_cast<uint64_t(*)(uint64_t,uint64_t,int32_t)>(s.base+0x2a6a260),std::move(log));
#else
    (void)s;(void)log;throw std::runtime_error("voidouts need the game");
#endif
}
}
