#include "testmode.h"
#include <algorithm>
#include <cmath>
namespace mod::testmode {
// game_offsets.md, "Test mode: material crates": only resins is known for this build (Resins (40), made live by
// CreateBaggage on 2026-11-03). The executable holds item hashes as code constants but not their names (the names are
// in the packed game data), so the other five are filled in from a live `CARGO list` beside each crate.
const Material Materials[MaterialCount]={
    // The largest crate of each, from the item list of a live copy (kit listitems.py, 2026-11-18: name text "X ({0})",
    // item hash at +34, weight at +50; each size exists twice with two hashes, the first of each is used here).
    {"chiral crystals",0x3325515bu,true},    // Chiral Crystals, weight 32
    {"metals",0x485b9f46u,true},             // Metals ({0}), weight 100
    {"ceramics",0x09e6bb84u,true},           // Ceramics ({0}), weight 80
    {"chemicals",0x2b555a36u,true},          // Chemicals ({0}), weight 60
    {"special alloys",0x0f0cdc02u,true},     // Special Alloys ({0}), weight 120
    {"resins",0x0ae5c9acu,true},             // Resins ({0}), weight 80 (0x0782598d, weight 4, was made live 2026-11-03)
};
std::vector<uint32_t> knownHashes(){std::vector<uint32_t> h;for(auto& m:Materials)if(m.hash)h.push_back(m.hash);return h;}
std::string missingNames(){std::string s;for(auto& m:Materials)if(!m.hash)s+=(s.empty()?"":", ")+std::string(m.name);return s;}
std::vector<std::array<double,3>> places(const std::array<double,3>& p,double fx,double fy,size_t n){
    double l=std::hypot(fx,fy);if(!(l>0.5)||!std::isfinite(l)){fx=0;fy=1;l=1;}
    fx/=l;fy/=l;const double rx=fy,ry=-fx;   // his right in the ground plane
    std::vector<std::array<double,3>> out;
    for(size_t i=0;i<n;++i){double side=(double(i)-double(n-1)/2)*Spacing;
        out.push_back({p[0]-Behind*fx+side*rx,p[1]-Behind*fy+side*ry,p[2]+Lift});}
    return out;
}
namespace {
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(int k=0;k<3;++k)d+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(d);}
}
void Dropper::observe(const std::vector<cargo::Record>& records,uint64_t now){
    std::vector<bool> used(records.size(),false);
    std::vector<Tracked> kept;
    for(auto& t:tracked_){
        long best=-1;double bestD=MatchRange;
        for(size_t i=0;i<records.size();++i){auto& r=records[i];
            if(used[i]||r.hash!=t.hash||cargo::location(r)!=cargo::InWorld)continue;
            double d=distance(r.position,t.at);if(d<=bestD){bestD=d;best=long(i);}}
        if(best>=0){
#ifndef MUTANT_TESTMODE_SHARED_RECORD
            used[size_t(best)]=true;
#endif
            Tracked u=t;u.at=records[size_t(best)].position;u.seen=true;kept.push_back(u);continue;}
#ifndef MUTANT_TESTMODE_FORGET
        if(!t.seen&&now-t.since<ExpectMs)kept.push_back(t);   // asked for, not made yet
#else
        kept.push_back(t);
#endif
    }
    tracked_=std::move(kept);
}
Dropper::Result Dropper::press(uint64_t now,const std::array<double,3>& porter,double fx,double fy,const std::vector<uint32_t>& hashes){
    Result r;
    if(hashes.empty()){r.refused="no material crate is known for this build";return r;}
#ifndef MUTANT_TESTMODE_RATE
    if(pressed_&&now-last_<GapMs){r.refused="one set every "+std::to_string(GapMs/1000)+" s";return r;}
#endif
#ifndef MUTANT_TESTMODE_CAP
    size_t room=tracked_.size()>=MaxOnGround?0:MaxOnGround-tracked_.size();
#else
    size_t room=hashes.size();
#endif
    if(!room){r.refused=std::to_string(MaxOnGround)+" of your crates are on the ground already: pick some up first";return r;}
    size_t n=std::min(room,hashes.size());auto at=places(porter,fx,fy,hashes.size());
    for(size_t i=0;i<n;++i){r.drops.push_back({hashes[i],at[i]});tracked_.push_back({hashes[i],at[i],now,false});}
    pressed_=true;last_=now;return r;
}
std::optional<float> refill(float v,float m){
    if(!std::isfinite(v)||!std::isfinite(m)||!(m>0)||m>GaugeCeiling||v<0)return std::nullopt;
#ifndef MUTANT_TESTMODE_ABOVE
    if(!(v<m))return std::nullopt;
#endif
    return m;
}
const char* refusal(const Gate& g){
#ifndef MUTANT_TESTMODE_THREAD
    if(!g.gameThread)return "not on the game thread";
#endif
    if(!g.porterReadable)return "the porter is not readable";
#ifndef MUTANT_TESTMODE_EVENT
    if(g.inEvent)return "a game event runs";
#endif
    if(!g.vitals.known)return "the porter's state is not known";
#ifndef MUTANT_TESTMODE_DEAD
    if(g.vitals.dead)return "the porter is down";
    if(g.vitals.caught)return "BTs have caught the porter";
#endif
    return "";
}
uint64_t infoBlock(const discovery::Sampler& s,uint64_t base,uint64_t entity){
    uint64_t manager=0,list=0,player=0,owner=0;int32_t count=0;
    if(!entity||!s.read(base+PlayerManagerSlot,&manager,8)||!manager||!s.read(manager+0x38,&count,4)||count<=0||count>16||!s.read(manager+0x40,&list,8)||!list)return 0;
    for(int32_t i=0;i<count&&!player;++i){uint64_t p=0;uint32_t number=1;if(s.read(list+8ull*uint32_t(i),&p,8)&&p&&s.read(p+0x88,&number,4)&&number==0)player=p;}
    if(!player||!s.read(player+0x70,&owner,8))return 0;
#ifndef MUTANT_TESTMODE_OWNER
    if(owner!=entity)return 0;
#endif
    return player+PlayerInfo;
}
uint64_t statusComponent(const discovery::Sampler& s,uint64_t base,uint64_t entity){
    uint64_t status=0;
    return infoBlock(s,base,entity)&&s.read(entity+EntityStatus,&status,8)?status:0;
}
Kept keepFull(const Gate& g,const discovery::Sampler& s,uint64_t block,bool blood,bool stamina,const Writable& writable,const Poke& poke){
    Kept k;k.refused=refusal(g);
    if(*k.refused)return k;
    if(!block){k.refused="no status component for this porter";return k;}
    if(!poke||!writable){k.refused="no writer";return k;}
    auto one=[&](uint64_t value,uint64_t maximum)->unsigned{
        float v=0,m=0;if(!s.read(block+value,&v,4)||!s.read(block+maximum,&m,4))return 0;
        auto to=refill(v,m);if(!to||!writable(block+value,4))return 0;
        float w=*to;poke(block+value,&w,4);return 1;};
    if(blood)k.blood=one(BloodValue,BloodMax);
    if(stamina){k.stamina=one(StaminaValue,StaminaMax);
#ifndef MUTANT_TESTMODE_CEILING
        float ceiling=0,m=0;   // a lowered ceiling would hold the value down: raised with it, as the game's recovery does
        if(k.stamina&&s.read(block+StaminaCeiling,&ceiling,4)&&s.read(block+StaminaMax,&m,4)&&refill(ceiling,m)&&writable(block+StaminaCeiling,4))poke(block+StaminaCeiling,&m,4);
#endif
    }
    return k;
}
unsigned keepBattery(const Gate& g,const discovery::Sampler& s,uint64_t block,uint64_t entity,const Writable& writable,const Poke& poke){
#ifndef MUTANT_TESTMODE_BATTERY_GATE
    if(*refusal(g))return 0;
#endif
#ifndef MUTANT_TESTMODE_BATTERY_OWNER
    if(!block)return 0;
#endif
    if(!entity||!poke||!writable)return 0;
    uint64_t c=0;float v=0,m=0;
    if(!s.read(entity+EntityBattery,&c,8)||!c||!s.read(c+BatteryValue,&v,4)||!s.read(c+BatteryMax,&m,4))return 0;
    auto to=refill(v,m);if(!to||!writable(c+BatteryValue,4))return 0;
    float w=*to;poke(c+BatteryValue,&w,4);
#ifndef MUTANT_TESTMODE_BATTERY_CLEAR
    if(writable(c+BatteryClear,4)){const uint32_t zero=0;poke(c+BatteryClear,&zero,4);}
#endif
    return 1;
}
}
