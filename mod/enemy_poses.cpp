#include "enemy_poses.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::enemyposes {
namespace {
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(int k=0;k<3;++k)d+=(a[size_t(k)]-b[size_t(k)])*(a[size_t(k)]-b[size_t(k)]);return std::sqrt(d);}
bool finite(const std::array<double,3>& a){for(auto v:a)if(!std::isfinite(v)||std::fabs(v)>ds::EnemyPositionMax)return false;return true;}
bool standing(const enemies::Mule& m){return enemies::condition(m.state,m.flags)==enemies::Up&&m.state!=5;}
// Faster than a person since the last look? Looks closer than 50 ms or further apart than 2 s say nothing.
template<class Seen> bool fastNow(Seen& s,const std::array<double,3>& position,uint64_t now){
    if(s.at&&now>s.at+50&&now<=s.at+2000)s.fast=distance(position,s.position)/(double(now-s.at)/1000.)>FastMps;
    else if(!s.at||now>s.at+2000)s.fast=false;
    if(!s.at||now>s.at+50){s.position=position;s.at=now;}
    return s.fast;
}
}
double heading(const std::array<float,9>& r){double h=std::atan2(double(r[3]),double(r[0]));return std::isfinite(h)?h:0;}
std::array<float,9> upright(double h){float c=float(std::cos(h)),s=float(std::sin(h));return {c,-s,0,s,c,0,0,0,1};}
ds::EnemyPoses Sampler::select(const std::vector<enemies::Mule>& mules,const std::array<double,3>& porter,const std::optional<std::array<double,3>>& partner,uint64_t now){
    struct Candidate { double d; ds::EnemyPose pose; };
    std::vector<Candidate> found;std::map<uint64_t,Seen> next;
    for(auto& m:mules){
        uint64_t k=enemies::key(m.uuid);if(!k||!finite(m.position))continue;
        Seen s=seen_.count(k)?seen_[k]:Seen{};bool fast=fastNow(s,m.position,now);next[k]=s;
#ifndef MUTANT_ENEMYPOS_AWAKE
        if(!standing(m))continue;
#endif
        double d=distance(m.position,porter);if(partner)d=std::min(d,distance(m.position,*partner));
#ifndef MUTANT_ENEMYPOS_RANGE
        if(d>RangeM)continue;
#endif
        ds::EnemyPose p;p.key=k;p.position=m.position;p.heading=heading(m.rotation);p.flags=uint8_t((m.lod==2?HasBody:0)|(fast?Fast:0));
        found.push_back({d,p});
    }
    seen_.swap(next);
#ifndef MUTANT_ENEMYPOS_NEAREST
    std::sort(found.begin(),found.end(),[](const Candidate& a,const Candidate& b){return a.d<b.d||(a.d==b.d&&a.pose.key<b.pose.key);});
#endif
    if(found.size()>ds::EnemyPosesMax)found.resize(ds::EnemyPosesMax);
    // The wire wants each identity once, in increasing order (two locators that share 52 bits: the nearer one stays).
#ifndef MUTANT_ENEMYPOS_KEYS
    std::stable_sort(found.begin(),found.end(),[](const Candidate& a,const Candidate& b){return a.pose.key<b.pose.key;});
    found.erase(std::unique(found.begin(),found.end(),[](const Candidate& a,const Candidate& b){return a.pose.key==b.pose.key;}),found.end());
#endif
    ds::EnemyPoses out;for(auto& c:found)out.poses[out.count++]=c.pose;
    return out;
}
std::vector<Move> Corrector::decide(const std::vector<enemies::Mule>& mules,const ds::EnemiesShown* shown,const std::array<double,3>& porter,uint64_t now,bool pull){
    std::vector<Move> out;std::map<uint64_t,Seen> next;
    bool fresh=shown!=nullptr;
#ifndef MUTANT_ENEMYPOS_STALE
    if(fresh&&shown->ageMs>StaleMs)fresh=false;
#endif
    for(auto& m:mules){
        uint64_t k=enemies::key(m.uuid);if(!k||!finite(m.position))continue;
        Seen s=seen_.count(k)?seen_[k]:Seen{};
        // A MULE this side moved at the last look did not run there: that jump is not speed.
        bool mine=s.moved;s.moved=false;
        if(mine){s.position=m.position;s.at=now;}
        bool fast=fastNow(s,m.position,now);
        auto keep=[&](bool away){if(!away){s.wasFar=false;s.farSince=0;}next[k]=s;};
        const ds::EnemyPose* h=nullptr;
        if(fresh)for(size_t i=0;i<shown->enemies.count;++i)if(shown->enemies.poses[i].key==k){h=&shown->enemies.poses[i];break;}
        if(!h||!finite(h->position)||!std::isfinite(h->heading)){keep(false);continue;}
#ifndef MUTANT_ENEMYPOS_DOWN
        if(!standing(m)){keep(false);continue;}
#endif
#ifndef MUTANT_ENEMYPOS_FAST
        if(fast||(h->flags&Fast)){keep(false);continue;}
#else
        (void)fast;
#endif
#ifndef MUTANT_ENEMYPOS_REACH
        if(distance(m.position,porter)<ReachM){keep(false);continue;}
#endif
#ifndef MUTANT_ENEMYPOS_LAND
        if(distance(h->position,porter)<LandM){keep(false);continue;}
#endif
        double error=distance(m.position,h->position);
#ifndef MUTANT_ENEMYPOS_FARCAP
        if(error>MaxErrorM){keep(false);continue;}
#endif
#ifndef MUTANT_ENEMYPOS_DEADBAND
        if(error<DeadbandM){keep(false);continue;}
#endif
        Move mv;mv.address=m.address;mv.key=k;mv.error=error;
        if(m.lod!=2){
            // No body in this game: nobody sees the move, the actor's transform is simply set.
            keep(false);
#ifndef MUTANT_ENEMYPOS_COOLDOWN
            if(s.movedAt&&now<s.movedAt+UnseenCooldownMs)continue;
#endif
            mv.snap=true;mv.position=h->position;mv.heading=h->heading;out.push_back(mv);continue;
        }
        if(error>=SnapM){
            if(!s.wasFar){s.wasFar=true;s.farSince=now;}
            keep(true);
#ifndef MUTANT_ENEMYPOS_HOLD
            if(now<s.farSince+SnapHoldMs)continue;
#endif
#ifndef MUTANT_ENEMYPOS_COOLDOWN
            if(s.movedAt&&now<s.movedAt+SnapCooldownMs)continue;
#endif
            mv.snap=true;mv.position=h->position;mv.heading=h->heading;out.push_back(mv);continue;
        }
        keep(false);
        if(!pull||(s.movedAt&&now<s.movedAt+PullIntervalMs))continue;
        double step=std::min(error,PullStepM);
        for(int i=0;i<3;++i)mv.position[size_t(i)]=m.position[size_t(i)]+(h->position[size_t(i)]-m.position[size_t(i)])*step/error;
        // The step itself may not end in the porter's reach either.
#ifndef MUTANT_ENEMYPOS_LAND
        if(distance(mv.position,porter)<LandM)continue;
#endif
        mv.heading=heading(m.rotation);out.push_back(mv);
    }
    // The largest errors first, and only a few native calls per look.
    std::sort(out.begin(),out.end(),[](const Move& a,const Move& b){return a.error>b.error||(a.error==b.error&&a.key<b.key);});
#ifndef MUTANT_ENEMYPOS_BUDGET
    if(out.size()>MovesPerLook)out.resize(MovesPerLook);
#endif
    for(auto& mv:out){auto& s=next[mv.key];s.movedAt=now;s.moved=true;s.wasFar=false;s.farSince=0;if(mv.snap)++snaps_;else ++pulls_;}
    seen_.swap(next);
    return out;
}
bool movable(const discovery::Sampler& s,uint64_t base,uint64_t actor){
    uint64_t vtable=0,slot=0;uint8_t live=0;
    if(!actor||(actor&7)||!s.read(actor,&vtable,8))return false;
#ifndef MUTANT_ENEMYPOS_VTABLE
    if(vtable!=base+ActorVtable)return false;
#endif
    if(!s.read(vtable+TeleportSlot,&slot,8)||slot!=base+TeleportFunction)return false;
#ifndef MUTANT_ENEMYPOS_LIVE
    if(!s.read(actor+0x20,&live,1)||!live)return false;
#endif
    return true;
}
std::function<bool(uint64_t,const Place&)> nativeTeleport(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native enemy position API requires Windows");
#else
    // The override, the base teleport it calls, and that call's own call to the entity teleport, as in the pinned image.
    const uint8_t wantA[16]={0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x05};
    const uint8_t wantB[16]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xd9,0xff,0x50,0x10,0x83};
    uint8_t got[16]{};int32_t rel=0;uint8_t op=0;
    if(!s.read(s.base+TeleportFunction,got,16)||std::memcmp(got,wantA,16))throw std::runtime_error("MULE teleport function differs");
    if(!s.read(s.base+BaseTeleport,got,16)||std::memcmp(got,wantB,16))throw std::runtime_error("actor teleport function differs");
    if(!s.read(s.base+0x2ad216c,&op,1)||op!=0xe8||!s.read(s.base+0x2ad216d,&rel,4)||0x2ad216c+5+int64_t(rel)!=int64_t(BaseTeleport))throw std::runtime_error("MULE teleport does not call the actor teleport");
    if(!s.read(s.base+0x22f8dd5,&op,1)||op!=0xe8||!s.read(s.base+0x22f8dd6,&rel,4)||0x22f8dd5+5+int64_t(rel)!=int64_t(EntityTeleport))throw std::runtime_error("actor teleport does not call the entity teleport");
    uint64_t slot=0;if(!s.read(s.base+ActorVtable+TeleportSlot,&slot,8)||slot!=s.base+TeleportFunction)throw std::runtime_error("MULE actor vtable differs");
    auto* sp=&s;
    return [sp](uint64_t actor,const Place& p)->bool{
        MEMORY_BASIC_INFORMATION mbi{};
        if(!actor||!VirtualQuery(reinterpret_cast<void*>(actor),&mbi,sizeof mbi)||mbi.State!=MEM_COMMIT)return false;
        if(!movable(*sp,sp->base,actor))return false;
        for(auto v:p.position)if(!std::isfinite(v)||std::fabs(v)>ds::EnemyPositionMax)return false;
        for(auto v:p.rotation)if(!std::isfinite(v)||std::fabs(v)>1.01f)return false;
        // WorldTransform: 3 doubles, 9 floats (0x3c bytes; the game copies 0x24 bytes of rotation).
        alignas(16) uint8_t transform[0x40]{};std::memcpy(transform,p.position.data(),24);std::memcpy(transform+0x18,p.rotation.data(),36);
        auto f=reinterpret_cast<void(*)(uint64_t,const uint8_t*,uint8_t)>(sp->base+TeleportFunction);
        f(actor,transform,1);
        return true;};
#endif
}
}
