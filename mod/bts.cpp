#include "bts.h"
#include <cstring>
#include <stdexcept>
namespace mod::bts {
uint64_t key(const std::array<uint8_t,16>& uuid){
    uint64_t v=0;std::memcpy(&v,uuid.data(),8);return v&((uint64_t(1)<<52)-1);
}
std::vector<Gazer> readGazers(const discovery::Sampler& s,uint64_t base){
    std::vector<Gazer> out;uint64_t manager=0,units=0;int32_t count=0;
    if(!s.read(base+0x7bbb8c0,&manager,8)||!manager||!s.read(manager+0x440,&count,4)||!s.read(manager+0x448,&units,8)||!units||count<=0||count>256)return out;
    // The locators' UUIDs are remembered for a while (they do not change in a world; forgotten every 120 looks).
    static std::map<uint64_t,std::array<uint8_t,16>> uuids;static uint64_t uuidsFor=0;static unsigned looks=0;
    if(uuidsFor!=manager||++looks>=120){uuids.clear();uuidsFor=manager;looks=0;}
    for(int32_t i=0;i<count;++i){
        uint64_t unit=0,vtable=0,array=0;uint32_t n=0;
        if(!s.read(units+8*uint64_t(i),&unit,8)||!unit||!s.read(unit,&vtable,8)||vtable!=base+0x3da58a8)continue;   // the gazer unit
        if(!s.read(unit+0x40,&n,4)||!s.read(unit+0x48,&array,8)||!array||n>1024)continue;
        std::vector<uint64_t> actors(n);if(n&&!s.read(array,actors.data(),size_t(n)*8))continue;
        // One copy per actor and the locator's UUID remembered (as for the MULEs, enemies.cpp).
        uint8_t block[0x1b8];
        for(uint32_t j=0;j<n;++j){
            uint64_t locator=0;Gazer g;
            if(!actors[j]||!s.readOnce(actors[j],block,sizeof block)||!block[0x20])continue;
            std::memcpy(&g.life,block+0x180,4);std::memcpy(&g.flags,block+0x1b4,4);std::memcpy(&g.lod,block+0x24,4);std::memcpy(&locator,block+0x18,8);
            if(!locator)continue;
            auto known=uuids.find(locator);
            if(known==uuids.end()){std::array<uint8_t,16> u{};if(!s.read(locator+8,u.data(),16))continue;known=uuids.emplace(locator,u).first;}
            g.uuid=known->second;g.address=actors[j];out.push_back(g);
        }
    }
    return out;
}
std::vector<uint64_t> GazerSync::observe(const std::vector<Gazer>& gazers){
    std::vector<uint64_t> out;std::map<uint64_t,bool> next;
    for(auto& g:gazers){
        uint64_t k=key(g.uuid);bool alive=g.life>0;next[k]=alive;
        auto old=alive_.find(k);
#ifndef MUTANT_BT_UNSEEN
        if(!primed_||old==alive_.end()||!old->second)continue;
#endif
        (void)old;
        if(alive)continue;
#ifndef MUTANT_BT_ECHO
        if(applied_.erase(k))continue;                                  // destroyed here for the partner: not news
#endif
#ifndef MUTANT_BT_DYNAMIC
        if(dynamic(g.flags))continue;
#endif
        out.push_back(k);
    }
    alive_.swap(next);primed_=true;
#ifndef MUTANT_BT_MASS
    if(out.size()>MassDeath)out.clear();
#endif
    return out;
}
std::vector<uint64_t> GazerSync::everything(const std::vector<Gazer>& gazers){
    std::vector<uint64_t> out;
    for(auto& g:gazers){
#ifndef MUTANT_BT_ALL_ALIVE
        if(g.life>0||dynamic(g.flags))continue;
#endif
        out.push_back(key(g.uuid));
    }
    return out;
}
const Gazer* GazerSync::wanted(const std::vector<Gazer>& gazers,uint64_t k){
    for(auto& g:gazers)if(key(g.uuid)==k)return g.life>0?&g:nullptr;
    return nullptr;
}
Fights readFights(const discovery::Sampler& s,uint64_t base){
    Fights f;uint64_t manager=0,records=0;int32_t n=0;
    if(!s.read(base+0x7bee060,&manager,8)||!manager||!s.read(manager+0x88,&n,4)||!s.read(manager+0x90,&records,8)||!s.read(manager+0xa8,&f.cleared,4))return f;
    if(n<0||n>64||f.cleared<0||f.cleared>100000)return f;
    for(int32_t i=0;i<n;++i){uint8_t off=1;if(records&&s.read(records+0x110*uint64_t(i)+0xf0,&off,1)&&!off)++f.active;}
    f.readable=true;return f;
}
std::vector<uint32_t> FightSync::observe(const Fights& f,uint64_t nowMs){
    std::vector<uint32_t> out;if(!f.readable)return out;
    bool active=f.active>0;
    if(primed_){
#ifndef MUTANT_BT_LOST_ECHO
        if(active_&&!active&&f.cleared<=cleared_)lostAt_=nowMs?nowMs:1;
#endif
#ifndef MUTANT_BT_CLEARED_ANY
        if(f.cleared>cleared_)out.push_back(AreaCleared);
#else
        if(f.cleared!=cleared_)out.push_back(AreaCleared);
#endif
        if(active&&!active_){out.push_back(FightOn);looks_=0;}
        else if(active&&++looks_>=RepeatLooks){out.push_back(FightOn);looks_=0;}
    }
    primed_=true;cleared_=f.cleared;active_=active;
    return out;
}
bool FightSync::wanted(const Fights& f,uint32_t what,double distance){
    if(!f.readable)return false;
#ifndef MUTANT_BT_FAR
    if(what==FightOn)return f.active==0&&distance<=FightRange;
#else
    (void)distance;if(what==FightOn)return f.active==0;
#endif
#ifndef MUTANT_BT_END_IDLE
    if(what==AreaCleared)return f.active>0;
#else
    if(what==AreaCleared)return true;
#endif
    return false;
}
std::vector<Catcher> readCatchers(const discovery::Sampler& s,uint64_t base){
    std::vector<Catcher> out;uint64_t manager=0,records=0;int32_t n=0;
    if(!s.read(base+0x7bee060,&manager,8)||!manager||!s.read(manager+0x88,&n,4)||!s.read(manager+0x90,&records,8)||!records||n<=0||n>64)return out;
    for(int32_t i=0;i<n;++i){
        uint8_t r[0x110];uint64_t weak=0,list=0;uint32_t id=0;int32_t count=0;
        if(!s.read(records+0x110*uint64_t(i),r,sizeof r)||r[0xf0])continue;
        std::memcpy(&weak,r,8);std::memcpy(&id,r+0x20,4);
        if(weak<0x20||!s.read(weak-0x20+0x90,&count,4)||!s.read(weak-0x20+0x98,&list,8)||!list||count<=0||count>512)continue;
        std::vector<uint64_t> parts;parts.resize(size_t(count));if(!s.read(list,parts.data(),parts.size()*8))continue;
        for(auto part:parts){
            uint64_t vtable=0,entity=0;uint32_t owner=0;float life[2]{};
            if(!part||!s.read(part,&vtable,8)||vtable!=base+0x3daf340)continue;          // the catcher's base component
            if(!s.read(part+0x48,&entity,8)||entity!=weak-0x20||!s.read(part+0x58,&owner,4)||owner!=id||!s.read(part+0x138,life,8))break;
            if(!(life[0]>0)||!(life[1]>=0)||life[1]>life[0])break;
            Catcher c;c.life=part+0x13c;c.percent=life[1]/life[0]*100.f;c.type=r[0x28];out.push_back(c);break;
        }
    }
    return out;
}
std::vector<HealthSync::Hurt> HealthSync::observe(const std::vector<Catcher>& catchers){
    std::vector<Hurt> out;
    if(catchers.size()!=last_.size()){last_.clear();for(auto& c:catchers)last_.push_back(c.percent);return out;}   // another fight, or one more catcher: learn
    for(size_t i=0;i<catchers.size();++i){
#ifndef MUTANT_BT_HEAL
        if(catchers[i].percent<last_[i]-Least)out.push_back({uint32_t(i),last_[i]-catchers[i].percent});
#else
        if(catchers[i].percent!=last_[i])out.push_back({uint32_t(i),last_[i]-catchers[i].percent});
#endif
        last_[i]=catchers[i].percent;
    }
    return out;
}
std::optional<float> HealthSync::applied(const std::vector<Catcher>& catchers,const Hurt& h){
    if(h.ordinal>=catchers.size()||catchers.size()!=last_.size()||!(h.percent>0)||h.percent>100)return std::nullopt;
    float now=catchers[h.ordinal].percent;
#ifndef MUTANT_BT_KILL
    if(now<=Floor)return std::nullopt;
    float next=now-h.percent<Floor?Floor:now-h.percent;
#else
    float next=now-h.percent;
#endif
#ifndef MUTANT_BT_HURT_ECHO
    last_[h.ordinal]=next;                                             // taken off for the partner: not news
#endif
    return next;
}
std::function<void(const Catcher&,float)> nativeHurt(discovery::Sampler& s){
#ifdef _WIN32
    return [&s](const Catcher& c,float percent){
        float life[2]{};uint64_t vtable=0;
        if(!c.life||!s.read(c.life-0x13c,&vtable,8)||vtable!=s.base+0x3daf340||!s.read(c.life-4,life,8)||!(life[0]>0)||!(life[1]>0))return;
        float next=life[0]*percent/100.f;if(!(next>0)||next>=life[1])return;
        *reinterpret_cast<volatile float*>(c.life)=next;
    };
#else
    (void)s;throw std::runtime_error("native BTs need the game");
#endif
}
std::function<void(uint64_t)> nativeDestroy(discovery::Sampler& s){
#ifdef _WIN32
    return [&s](uint64_t actor){
        float life=0;if(!actor||!s.read(actor+0x180,&life,4)||!(life>0))return;
        *reinterpret_cast<volatile float*>(actor+0x180)=0.f;
    };
#else
    (void)s;throw std::runtime_error("native BTs need the game");
#endif
}
std::function<void(uint8_t)> nativeCatcher(discovery::Sampler& s){
#ifdef _WIN32
    static const uint8_t want[7]={0x40,0x55,0x48,0x8d,0x6c,0x24,0xa9};uint8_t got[7]{};
    if(!s.read(s.base+0x2a928c0,got,7)||std::memcmp(got,want,7))throw std::runtime_error("catcher command function differs");
    auto f=reinterpret_cast<void(*)(uint8_t)>(s.base+0x2a928c0);
    return [f](uint8_t command){f(command);};
#else
    (void)s;throw std::runtime_error("native BTs need the game");
#endif
}
}
