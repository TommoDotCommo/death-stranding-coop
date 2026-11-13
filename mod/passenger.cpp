#include "passenger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace mod::passenger {
Do Rider::tick(const Look& l){
    if(!truck_){
#ifndef MUTANT_PASSENGER_FAR
        if(l.key&&l.permitted&&l.partnerInTruck&&l.truck&&l.distance<=Reach&&l.mover==Free){truck_=l.truck;heldSeen_=false;freeLooks_=0;return Do::GetOn;}
#else
        if(l.key&&l.permitted&&l.partnerInTruck&&l.truck){truck_=l.truck;heldSeen_=false;freeLooks_=0;return Do::GetOn;}
#endif
        return Do::Nothing;
    }
    auto off=[&](const char* why){why_=why;truck_=0;heldSeen_=false;return Do::GetOff;};
    if(l.key)return off("the passenger key");
    if(!l.permitted)return off("not permitted here now (game event, link, on a vehicle)");
#ifndef MUTANT_PASSENGER_STAY
    if(!l.partnerInTruck)return off("the partner got out");
    if(l.truck!=truck_)return off("the truck went");
#endif
    // The game can end the hold by itself (a fall, a hit): once it was held, two free looks in a row mean it is over.
    if(l.mover==Held)heldSeen_=true;
    if(heldSeen_&&l.mover==Free){if(++freeLooks_>=2){truck_=0;heldSeen_=false;why_="the game let go";return Do::Nothing;}}
    else freeLooks_=0;
    return Do::Nothing;
}
std::optional<std::array<double,3>> pushOut(const Transform& t,const std::array<double,3>& p,const Box& b){
    double d[3]={p[0]-t.position[0],p[1]-t.position[1],p[2]-t.position[2]};
    auto along=[&](int row){return d[0]*t.rotation[size_t(3*row)]+d[1]*t.rotation[size_t(3*row+1)]+d[2]*t.rotation[size_t(3*row+2)];};
    double r=along(0),f=along(1),u=along(2);
#ifndef MUTANT_PASSENGER_TALL
    if(u<-0.5||u>b.height)return std::nullopt;                         // above the roof or below the ground: not inside
#endif
    if(std::fabs(r)>=b.halfRight||std::fabs(f)>=b.halfForward)return std::nullopt;
    // Out through the nearest side.
    double outR=b.halfRight-std::fabs(r),outF=b.halfForward-std::fabs(f);
    double moveR=0,moveF=0;
    if(outR<=outF)moveR=(r>=0?1:-1)*(outR+b.margin);else moveF=(f>=0?1:-1)*(outF+b.margin);
    std::array<double,3> out=p;
    for(int k=0;k<3;++k)out[size_t(k)]+=moveR*t.rotation[size_t(k)]+moveF*t.rotation[size_t(3+k)];
    return out;
}
Api nativeApi(discovery::Sampler& s){
#ifdef _WIN32
    auto same=[&s](uint64_t rva,std::initializer_list<uint8_t> want){std::vector<uint8_t> got(want.size());return s.read(s.base+rva,got.data(),got.size())&&std::equal(want.begin(),want.end(),got.begin());};
    if(!same(0x26bcfa0,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x20})||!same(0x2696c50,{0xc5,0xfa,0x11,0x89,0x30,0x07,0x00,0x00})
       ||!same(0x190aa00,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9})||!same(0x190ad20,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x19})
       ||!same(0x191c790,{0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x8b,0x09}))throw std::runtime_error("passenger functions differ");
    uint64_t base=s.base;
    auto mover=[&s,base](uint64_t porter){uint64_t m=0,v=0;return porter&&s.read(porter+0xb0,&m,8)&&m&&s.read(m,&v,8)&&v==base+0x3d6f018?m:uint64_t(0);};
    Api a;
    a.state=[&s,mover](uint64_t porter){uint8_t st=0;uint64_t m=mover(porter);if(m)s.read(m+0x720,&st,1);return st;};
    a.attach=[&s,base,mover](uint64_t porter,uint64_t vehicle,const char* helper){
        uint64_t m=mover(porter),model=0;uint32_t joint=0;
        if(!m||!vehicle||!s.read(vehicle+0xb8,&model,8)||!model||!s.read(porter+0x934,&joint,4))return false;
        alignas(16) uint8_t name[32]{};alignas(16) uint64_t slot[4]{vehicle+0x20,0,0,0};
        alignas(16) float local[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        reinterpret_cast<void(*)(void*,const char*)>(base+0x190aa00)(name,helper);
        reinterpret_cast<void(*)(void*)>(base+0x191c790)(slot);                  // the attach releases this slot itself
        reinterpret_cast<void(*)(uint64_t,uint32_t,void*,void*,float*,float)>(base+0x26bcfa0)(m,joint,slot,name,local,0.25f);
        reinterpret_cast<void(*)(void*)>(base+0x190ad20)(name);
        return true;
    };
    a.release=[base,mover](uint64_t porter){uint64_t m=mover(porter);if(m)reinterpret_cast<void(*)(uint64_t,float)>(base+0x2696c50)(m,0.0f);};
    return a;
#else
    (void)s;throw std::runtime_error("passenger needs the game");
#endif
}
}
