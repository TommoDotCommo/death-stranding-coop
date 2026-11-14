#include "orderload.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace mod::orderload {
namespace {
float life(float v){return std::isfinite(v)&&v>=0&&v<=100000.f?v:-1.f;}
}
std::optional<Tag> tagOf(const cargo::Placement& p){
    if(!p.mission||p.mission>0xffffffffull||!p.item)return std::nullopt;        // static orders have small ids
    if((p.flags&0x100000)||p.type!=TypeStandard||p.unit>63)return std::nullopt; // a main or sub order's cargo is never shared
    Tag t;t.mission=p.mission;t.unit=p.unit;t.type=p.type;t.item=p.item;t.link=uint16_t(p.id);
    t.flags=uint8_t(((p.flags&0x200000)?FlagStandard:0)|((p.flags2&2)?FlagResourceA:0)|((p.flags2&0x1000)?FlagResourceB:0));
    t.life[0]=life(p.life[0]);t.life[1]=life(p.life[1]);
    return t;
}
Wire encode(const Tag& t){
    Wire w;w.item=t.item;w.ref=(t.mission&0xffffffffull)|uint64_t(t.link)<<32;
    w.position={double(unsigned(t.unit&63)|unsigned(t.flags&15)<<6|unsigned(t.type&63)<<10),double(life(t.life[0])),double(life(t.life[1]))};
    return w;
}
bool belongs(const Tag& t,uint64_t id){
#ifdef MUTANT_LOAD_LINK
    (void)t;(void)id;return true;
#else
    return t.link==uint16_t(id);
#endif
}
std::optional<Tag> decode(const Wire& w){
    Tag t;t.mission=w.ref&0xffffffffull;t.link=uint16_t(w.ref>>32);t.item=w.item;
    double packed=w.position[0];
#ifndef MUTANT_LOAD_DECODE_RANGE
    if(w.ref>>48||!t.mission||!t.item)return std::nullopt;
    if(!(packed>=0&&packed<65536)||packed!=std::floor(packed))return std::nullopt;
    for(int i=1;i<3;++i)if(!std::isfinite(w.position[i])||(w.position[i]!=-1&&(w.position[i]<0||w.position[i]>100000)))return std::nullopt;
#else
    if(!(packed>=0&&packed<65536))packed=0;
#endif
    unsigned v=unsigned(packed);t.unit=uint8_t(v&63);t.flags=uint8_t(v>>6&15);t.type=uint8_t(v>>10&63);
#ifndef MUTANT_LOAD_DECODE_RANGE
    if(t.type!=TypeStandard||(t.flags&FlagStory))return std::nullopt;
#endif
    t.life[0]=float(w.position[1]);t.life[1]=float(w.position[2]);
    return t;
}
void Loads::underWay(const std::vector<uint64_t>& missions,uint64_t now){
#ifndef MUTANT_LOAD_STALE
    std::vector<uint64_t> over;
    for(auto& [mission,since]:mirrored_)if(now-since>Grace&&std::find(missions.begin(),missions.end(),mission)==missions.end())over.push_back(mission);
    for(auto mission:over)ended(mission);
#else
    (void)missions;(void)now;
#endif
}
void Loads::started(uint64_t mission,bool forPartner,uint64_t now){
    for(auto it=shared_.begin();it!=shared_.end();)if((*it&~UnitMask)==mission)it=shared_.erase(it);else ++it;
    if(forPartner)mirrored_[mission]=now;else mirrored_.erase(mission);
}
void Loads::ended(uint64_t mission){
#ifndef MUTANT_LOAD_ENDED
    mirrored_.erase(mission);
#endif
    for(auto it=shared_.begin();it!=shared_.end();)if((*it&~UnitMask)==mission)it=shared_.erase(it);else ++it;
    for(auto it=asked_.begin();it!=asked_.end();)if((it->first&~UnitMask)==mission)it=asked_.erase(it);else ++it;
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](auto& p){return p.c.tag.mission==mission;}),pending_.end());
}
bool Loads::own(const cargo::Record& r) const {
    if(!r.mission)return false;
#ifndef MUTANT_LOAD_STRIP_TAKER
    if(!mirrored_.count(r.mission))return false;
#endif
#ifndef MUTANT_LOAD_STRIP_SHARED
    if(shared_.count(handle(r.mission,r.unit)))return false;
#endif
    return true;
}
std::vector<uint32_t> Loads::strip(const std::vector<cargo::Record>& records,uint64_t now,unsigned* left){
    std::vector<uint32_t> out;if(left)*left=0;
    for(auto& r:records){
        if(!own(r))continue;
#ifndef MUTANT_LOAD_STRIP_CARRIED
        if(cargo::location(r)!=cargo::InWorld){if(left)++*left;continue;}
#endif
        uint64_t h=handle(r.mission,r.unit);auto a=asked_.find(h);
        if(a!=asked_.end()&&now-a->second<AskAgain)continue;
        asked_[h]=now;out.push_back(r.index);
    }
    return out;
}
void Loads::want(const Case& c,uint64_t now){
    uint64_t h=handle(c.tag.mission,c.tag.unit);
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](auto& p){return handle(p.c.tag.mission,p.c.tag.unit)==h;}),pending_.end());
    pending_.push_back({c,now,now});
}
bool Loads::cancel(uint64_t id){
#ifdef MUTANT_LOAD_CANCEL
    (void)id;return false;
#else
    auto before=pending_.size();
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](auto& p){return p.c.id==id;}),pending_.end());
    return pending_.size()!=before;
#endif
}
std::vector<Case> Loads::due(const std::vector<cargo::Record>& records,uint64_t now){
    std::vector<Case> out;
    for(auto it=pending_.begin();it!=pending_.end();){
        if(now-it->since>GiveUp){gaveUp_.push_back(it->c);it=pending_.erase(it);continue;}
        bool exists=false;
#ifndef MUTANT_LOAD_DUPLICATE
        for(auto& r:records)exists=exists||(r.mission==it->c.tag.mission&&r.unit==it->c.tag.unit);
#else
        (void)records;
#endif
        if(!exists&&now>=it->next)out.push_back(it->c);
        ++it;
    }
    return out;
}
void Loads::tried(uint64_t id,bool accepted,uint64_t now){
    for(auto it=pending_.begin();it!=pending_.end();++it)if(it->c.id==id){
        if(accepted){shared_.insert(handle(it->c.tag.mission,it->c.tag.unit));pending_.erase(it);}
        else it->next=now+Retry;
        return;
    }
}
std::vector<Case> Loads::takeGivenUp(){std::vector<Case> out;out.swap(gaveUp_);return out;}
std::function<bool(const Case&)> nativeCreate(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native order cargo API requires Windows");
#else
    struct Site{uint64_t rva;uint8_t bytes[16];};
    const Site sites[]={{0x2c470e0,{0x4c,0x8b,0xdc,0x53,0x41,0x56,0x48,0x81,0xec,0x58,0x01,0x00,0x00,0x48,0x8b,0x05}},        // creator
                        {0x277fca0,{0x48,0x83,0xec,0x40,0x33,0xed,0x48,0x8b,0xd9,0xc5,0xf9,0x57,0xc0,0xc5,0xf8,0x11}},        // record constructor + 0x10
                        {0x34abff0,{0x48,0x8b,0xd1,0x48,0x8b,0x0d,0x86,0x17,0x77,0x04,0x48,0x85,0xc9,0x0f,0x85,0x8d}},        // its name member's release
                        {0x33a3a50,{0x48,0x89,0x5c,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x83,0xec,0x50,0x48,0x8b,0xe9}}};       // the mission system's own filler (layout reference)
    for(auto& site:sites){uint8_t got[16]{};if(!s.read(s.base+site.rva,got,16)||std::memcmp(got,site.bytes,16))throw std::runtime_error("order cargo creation function differs");}
    const uint64_t base=s.base;auto* sp=&s;
    return [base,sp](const Case& c)->bool{
        uint64_t manager=0;if(!sp->read(base+0x7bf4968,&manager,8)||!manager)return false;
        alignas(16) uint8_t record[0x118]{};
        reinterpret_cast<void*(*)(void*)>(base+0x277fc90)(record);
        auto put=[&](size_t at,const void* v,size_t n){std::memcpy(record+at,v,n);};
        uint64_t key=handle(c.tag.mission,c.tag.unit),mission=c.tag.mission;
        put(0,c.position.data(),24);put(0x18,c.rotation.data(),36);
        put(0x40,&key,8);put(0x48,&c.tag.item,4);record[0x4c]=c.tag.type;put(0x50,&mission,8);put(0x64,&c.name,4);
        record[0x6e]=1;                                              // into the manager's list of order cargo
        record[0x72]=c.tag.unit;record[0x75]=0;record[0x76]=(c.tag.flags&FlagStandard)?1:0;
        record[0x7a]=(c.tag.flags&FlagResourceA)?1:0;record[0x80]=(c.tag.flags&FlagResourceB)?1:0;record[0xc0]=0x13;
        if(c.tag.life[0]>=1){int32_t v=int32_t(c.tag.life[0]);put(0x110,&v,4);}
        if(c.tag.life[1]>=0)put(0x114,&c.tag.life[1],4);
        uint64_t made=reinterpret_cast<uint64_t(*)(uint64_t,void*)>(base+0x2c470e0)(manager,record);
        reinterpret_cast<void(*)(void*)>(base+0x34abff0)(record+0xc8);
        return made!=0;};
#endif
}
}
