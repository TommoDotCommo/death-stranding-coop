#include "enemies.h"
#include <cstring>
#include <stdexcept>
namespace mod::enemies {
uint8_t condition(uint32_t state,uint32_t flags){
    if(state==3)return Dead;
    if(state==4){
        (void)flags;
#ifndef MUTANT_ENEMY_KIND
        if(flags&(1u<<20))return Fainted;
        if(flags&(1u<<21))return Asleep;
#endif
        return Knocked;
    }
    return Up;   // 1, 2 standing; 5 removed from the world is not a defeat
}
uint64_t key(const std::array<uint8_t,16>& uuid){
    uint64_t v=0;std::memcpy(&v,uuid.data(),8);return v&((uint64_t(1)<<52)-1);
}
std::vector<Mule> readMules(const discovery::Sampler& s,uint64_t base){
    std::vector<Mule> out;uint64_t manager=0,units=0;int32_t count=0;
    if(!s.read(base+0x7bbb8c0,&manager,8)||!manager||!s.read(manager+0x440,&count,4)||!s.read(manager+0x448,&units,8)||!units||count<=0||count>256)return out;
    for(int32_t i=0;i<count;++i){
        uint64_t unit=0,vtable=0,array=0;uint32_t first=0,n=0;
        if(!s.read(units+8*uint64_t(i),&unit,8)||!unit||!s.read(unit,&vtable,8)||vtable!=base+0x3db71b8)continue;   // the MULE unit
        if(!s.read(unit+0x30,&first,4)||!s.read(unit+0x40,&n,4)||!s.read(unit+0x48,&array,8)||!array||n>1024)continue;
        std::vector<uint64_t> actors(n);if(n&&!s.read(array,actors.data(),size_t(n)*8))continue;
        for(uint32_t j=0;j<n;++j){
            uint8_t head[0x70];uint32_t life[2];uint64_t locator=0;
            if(!actors[j]||!s.read(actors[j],head,sizeof head)||!head[0x20]||!s.read(actors[j]+0xea0,life,8))continue;
            Mule m;m.address=actors[j];m.id=(uint32_t(i)<<13)|(first+j);m.state=life[0];m.flags=life[1];
            std::memcpy(&m.lod,head+0x24,4);std::memcpy(m.position.data(),head+0x28,24);std::memcpy(&locator,head+0x18,8);
            if(!locator||!s.read(locator+8,m.uuid.data(),16))continue;   // reinforcements have no locator: not shared
            out.push_back(m);
        }
    }
    return out;
}
std::vector<Camp> readCamps(const discovery::Sampler& s,uint64_t base){
    std::vector<Camp> out;uint64_t manager=0,units=0;int32_t count=0;
    if(!s.read(base+0x7bbb8c0,&manager,8)||!manager||!s.read(manager+0x440,&count,4)||!s.read(manager+0x448,&units,8)||!units||count<=0||count>256)return out;
    for(int32_t i=0;i<count;++i){
        uint64_t unit=0,vtable=0,array=0;uint32_t n=0;
        if(!s.read(units+8*uint64_t(i),&unit,8)||!unit||!s.read(unit,&vtable,8)||vtable!=base+0x3dbe8d0)continue;   // the camp unit
        if(!s.read(unit+0x40,&n,4)||!s.read(unit+0x48,&array,8)||!array||n>256)continue;
        std::vector<uint64_t> actors(n);if(n&&!s.read(array,actors.data(),size_t(n)*8))continue;
        for(uint32_t j=0;j<n;++j){
            uint8_t head[0x28];uint64_t locator=0;Camp c;
            if(!actors[j]||!s.read(actors[j],head,sizeof head)||!head[0x20]||!s.read(actors[j]+0x214,&c.phase,4))continue;
            std::memcpy(&locator,head+0x18,8);if(!locator||!s.read(locator+8,c.uuid.data(),16))continue;
            c.address=actors[j];out.push_back(c);
        }
    }
    return out;
}
std::vector<uint64_t> CampSync::observe(const std::vector<Camp>& camps){
    std::vector<uint64_t> out;std::map<uint64_t,uint32_t> next;
    for(auto& c:camps){
        uint64_t k=key(c.uuid);next[k]=c.phase;
        // What was alerted here for the partner is not news; the mark goes when the camp has been seen alarmed and is
        // calm again (the game needs a moment to act on the request, so a calm look right after it does not count).
        auto mark=applied_.find(k);
        if(!alarmed(c.phase)){if(mark!=applied_.end()&&mark->second)applied_.erase(mark);continue;}
        auto old=seen_.find(k);
#ifndef MUTANT_ENEMY_CAMP_ECHO
        if(mark!=applied_.end()){mark->second=true;continue;}
#endif
        if(!primed_||old==seen_.end()||alarmed(old->second))continue;
        out.push_back(k);
    }
    seen_.swap(next);primed_=true;
    return out;
}
const Camp* CampSync::wanted(const std::vector<Camp>& camps,uint64_t k) const {
    for(auto& c:camps)if(key(c.uuid)==k){
#ifndef MUTANT_ENEMY_CAMP_TWICE
        if(alarmed(c.phase))return nullptr;
#endif
        return &c;
    }
    return nullptr;
}
std::vector<Down> Sync::observe(const std::vector<Mule>& mules){
    std::vector<Down> out;std::map<uint64_t,uint8_t> next;
    for(auto& m:mules){
        uint64_t k=key(m.uuid);uint8_t now=condition(m.state,m.flags);next[k]=now;
        auto old=seen_.find(k);
        auto mark=applied_.find(k);
        if(now==Up){pending_.erase(k);if(mark!=applied_.end()&&mark->second)applied_.erase(mark);continue;}
#ifndef MUTANT_ENEMY_ECHO
        if(mark!=applied_.end()){mark->second=true;continue;}           // put down here for the partner: not news
#endif
        // Reported at the second look in a row that shows it down the same way after it was seen standing: the game
        // passes a MULE through a down state for a moment when it brings its body in.
        auto wait=pending_.find(k);
        if(wait!=pending_.end()){
            if(wait->second==now)out.push_back({k,now});
            pending_.erase(wait);continue;
        }
        if(!primed_||old==seen_.end()||old->second!=Up)continue;      // already down, or not seen standing before
#ifndef MUTANT_ENEMY_HASTY
        pending_[k]=now;
#else
        out.push_back({k,now});
#endif
    }
    seen_.swap(next);primed_=true;
    return out;
}
const Mule* Sync::wanted(const std::vector<Mule>& mules,const Down& d) const {
    for(auto& m:mules)if(key(m.uuid)==d.key){
#ifndef MUTANT_ENEMY_TWICE
        if(condition(m.state,m.flags)!=Up)return nullptr;
#endif
        if(m.state==5)return nullptr;
        return &m;
    }
    return nullptr;
}
std::function<void(const std::array<uint8_t,16>&)> nativeAlert(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native enemy API requires Windows");
#else
    const uint8_t want[16]={0x40,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x05,0x80,0xc6,0x3f,0x02};uint8_t got[16]{};
    if(!s.read(s.base+0x2b649a0,got,16)||std::memcmp(got,want,16))throw std::runtime_error("camp phase function differs");
    auto f=reinterpret_cast<void(*)(const uint8_t*,int)>(s.base+0x2b649a0);
    return [f](const std::array<uint8_t,16>& uuid){alignas(16) uint8_t u[16];std::memcpy(u,uuid.data(),16);f(u,int(AlertPhase));};
#endif
}
std::function<void(uint64_t,uint8_t)> nativeDefeat(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native enemy API requires Windows");
#else
    const uint8_t want[16]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x70,0x48,0x8b,0x05,0x1f,0x90,0x4b};uint8_t got[16]{};
    if(!s.read(s.base+0x2aa8000,got,16)||std::memcmp(got,want,16))throw std::runtime_error("MULE defeat function differs");
    auto f=reinterpret_cast<void(*)(void*,int)>(s.base+0x2aa8000);
    return [f](uint64_t actor,uint8_t kind){if(actor&&kind<=Dead)f(reinterpret_cast<void*>(actor),int(kind));};
#endif
}
}
