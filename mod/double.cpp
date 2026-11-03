#include "double.h"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace mod::puppet {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool usable(const Pose& p){if(!p.available||!p.coherent)return false;for(auto x:p.position)if(!std::isfinite(x))return false;for(auto x:p.rotation)if(!std::isfinite(x))return false;return true;}
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(unsigned i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
bool atTarget(const Pose& p,const Transform& t){return usable(p)&&distance(p.position,t.position)<.05;}
bool parked(uint64_t f){return (f&FlagSleeping)&&!(f&FlagVisible);}
bool driven(uint64_t f){return (f&FlagVisible)&&!(f&FlagSleeping);}
bool q(const discovery::Sampler& s,uint64_t address,uint64_t& value){return address&&address<0x800000000000ULL&&s.read(address,&value,8);}
constexpr unsigned MaxChildren=64;
}
uint64_t readFlags(const discovery::Sampler& s,uint64_t e){uint64_t f=0;require(q(s,e+FlagsOffset,f),"double flags unreadable");return f;}
uint32_t readActions(const discovery::Sampler& s,uint64_t e,uint64_t animator,ActionCache& c){
    uint32_t components=0;uint64_t list=0,manager=0;
    if(!s.read(e+0x90,&components,4)||components>4096||!q(s,e+0x98,list))return 0;
    for(uint32_t i=0;i<components&&!manager;++i){uint64_t m=0,v=0,owner=0;
#ifndef MUTANT_DOUBLE_ACTION_OWNER
        if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator&&q(s,m+0x48,owner)&&owner==e)manager=m;
#else
        if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator)manager=m;
#endif
    }
    uint32_t count=0;uint64_t slots=0;
    if(!manager||!s.read(manager+0x50,&count,4)||!count||count>4096||!q(s,manager+0x58,slots))return 0;
    if(c.manager!=manager||c.count!=count){
        c=ActionCache{};c.manager=manager;c.count=count;
        for(uint32_t i=0;i<count;++i){
            uint64_t name=0;char text[24]{};if(!q(s,slots+48ull*i,name)||!s.read(name,text,sizeof text-1))continue;
            for(unsigned k=0;k<7;++k)if(!std::strcmp(text,ActionNames[k]))c.index[k]=int(i);
        }
    }
    uint32_t out=0;
    for(unsigned k=0;k<7;++k){uint8_t b=0;if(c.index[k]>=0&&s.read(slots+48ull*unsigned(c.index[k])+0x20,&b,1)&&b)out|=ActionBits[k];}
    return out;
}
uint64_t findDouble(const discovery::Sampler& s,uint64_t player,uint64_t cameraVtable,uint64_t doubleVtable){
    // Visits the children of parent; false when a link is unreadable, a child disowns its parent or the list is too long.
    auto children=[&s](uint64_t parent,auto&& visit){
        uint64_t c=0;if(!q(s,parent+FirstChildOffset,c))return false;
        for(unsigned n=0;c;++n){
            uint64_t back=0;if(n>=MaxChildren||!q(s,c+ParentOffset,back))return false;
#ifndef MUTANT_DOUBLE_FIND_PARENT
            if(back!=parent)return false;
#endif
            visit(c);if(!q(s,c+NextSiblingOffset,c))return false;
        }
        return true;
    };
    uint64_t found=0;unsigned count=0;bool ok=true;
    ok&=children(player,[&](uint64_t camera){
        uint64_t vt=0;if(!q(s,camera,vt)||vt!=cameraVtable)return;
        ok&=children(camera,[&](uint64_t e){uint64_t v=0;if(q(s,e,v)&&v==doubleVtable){++count;if(!found)found=e;}});
    });
#ifdef MUTANT_DOUBLE_FIND_UNIQUE
    count=std::min(count,1u);
#endif
    return ok&&count==1?found:0;
}
Transform fromState(const std::array<double,3>& p,double yaw){
    require(std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&std::isfinite(yaw),"double target not finite");
    float c=float(std::cos(yaw)),n=float(std::sin(yaw));
    return {{p[0],p[2],p[1]},{c,-n,0,n,c,0,0,0,1}};
}
bool Double::gameUsesDouble(uint64_t porter){
    if(stage_==Stage::Yielded)return true;
    if(stage_==Stage::Driving||!porter||!api_.find||!api_.flags)return false;
    auto d=api_.find(porter);
#ifndef MUTANT_DOUBLE_PRIVATE_ROOM
    return d&&!parked(api_.flags(d));
#else
    return false;
#endif
}
const char* Double::name() const {
    switch(stage_){case Stage::Off:return "off";case Stage::Parked:return "parked";case Stage::Driving:return "driving";case Stage::Yielded:return "yielded";default:return "failed";}
}
void Double::note(const char* event,uint64_t flags,const Pose& p){
    std::ostringstream o;o<<std::setprecision(17)<<"DOUBLE {\"event\":\""<<event<<"\",\"double\":\"0x"<<std::hex<<double_<<"\",\"player\":\"0x"<<player_<<"\",\"flags\":\"0x"<<flags<<std::dec<<"\",\"readback\":";
    if(usable(p))o<<"["<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<"]";else o<<"null";
    o<<",\"placed\":["<<placed_.position[0]<<","<<placed_.position[1]<<","<<placed_.position[2]<<"]}\n";record_(o.str());
}
void Double::checkLocal(const Anchor& before){
    auto after=api_.local();
#ifndef MUTANT_DOUBLE_LOCAL
    require(after.entity==before.entity&&usable(after.pose)&&distance(before.pose.position,after.pose.position)<.10,"double altered the local player");
#endif
}
// The original network instance goes back before anything else is handed to the game. An entity that is gone
// (world change) takes the borrowed instance with it, so there is nothing to restore.
// Everything borrowed goes back before the double is handed to the game: the mover's resting target (the game never
// sets it itself, so a stale target would pull its double to the partner's last position), the animation network
// and the floor effects.
void Double::restoreNetwork(){
#ifndef MUTANT_DOUBLE_YIELD_ANCHOR
    if(double_)api_.anchor(double_,parked_);
#endif
#ifndef MUTANT_DOUBLE_NETWORK
    if(original_&&api_.downgrade)api_.downgrade(double_,original_);
#endif
    original_=0;
#ifndef MUTANT_DOUBLE_EFFECTS
    if(hidEffects_&&api_.effects)api_.effects(double_,true);
#endif
    hidEffects_=false;
}
void Double::forget(const char* why){note(why,0,{});original_=0;hidEffects_=false;double_=player_=0;stage_=Stage::Parked;}
void Double::acquire(const Anchor& source,const Transform& target){
    auto d=api_.find(source.entity);if(!d)return;
    auto f=api_.flags(d);
#ifndef MUTANT_DOUBLE_PARKED
    if(!parked(f))return; // in use by the game (or not yet parked); never take it
#endif
    auto rest=api_.read(d);if(!usable(rest))return;
    player_=source.entity;double_=d;parkedFlags_=f;parked_=transform(rest);placed_=target;
#ifndef MUTANT_DOUBLE_ANCHOR
    api_.anchor(d,target);
#endif
    // Record the floor effects' parked state before showing the double (showing it propagates to them a frame later).
    if(api_.effects){api_.effects(d,false);hidEffects_=true;}
    api_.place(d,target);api_.visible(d,true);api_.sleep(d,false);
    // Optional: a refused upgrade leaves the double as it was (identity and layout checks run before any write).
    if(walk_&&api_.upgrade){try{original_=api_.upgrade(d,source.entity);}catch(const std::exception&){original_=0;note("walk_unavailable",0,{});}}
    api_.animate(d,speed_,flags_);
    auto after=api_.flags(d);auto p=api_.read(d);checkLocal(source);
#ifndef MUTANT_DOUBLE_READBACK
    if(!driven(after)||!atTarget(p,target)){note("acquire_unconfirmed",after,p);release(source,true);stage_=Stage::Failed;return;}
#endif
    stage_=Stage::Driving;note("acquired",after,p);
}
void Double::drive(const Anchor& source,const Transform& target){
#ifndef MUTANT_DOUBLE_IDENTITY
    if(api_.find(source.entity)!=double_){forget("double_changed");return;}
#endif
    auto f=api_.flags(double_);auto p=api_.read(double_);
#ifndef MUTANT_DOUBLE_YIELD
    // Anything we did not do means the game is using its double: stop calling, wait until it parks it again.
    if(!driven(f)||!atTarget(p,placed_)){restoreNetwork();stage_=Stage::Yielded;note("yielded",f,p);return;}
#endif
    // Only the mover target changes: the game moves the double on its next frame. SetWorldTransform here would
    // mark it teleported every tick, which restarts its animation (live u4: frozen mid-stride). The next tick's
    // check above confirms the move happened.
    placed_=target;
#ifndef MUTANT_DOUBLE_ANCHOR
    api_.anchor(double_,target);
#else
    api_.place(double_,target);
#endif
    api_.animate(double_,speed_,flags_);
    if(hidEffects_)api_.effects(double_,false);   // keep them hidden as the show propagates
    checkLocal(source);
}
void Double::release(const Anchor& source,bool force){
    if(api_.find(source.entity)!=double_){forget("double_changed");return;}
    auto f=api_.flags(double_);
    // force: our own acquire half-applied, so the flags are ours to put back whatever they show.
    if(!force&&!driven(f)){restoreNetwork();forget("released_by_game");return;}
    restoreNetwork();
#ifndef MUTANT_DOUBLE_STILL
    api_.animate(double_,0,0);   // back to the standing state the game left it in
#endif
    api_.anchor(double_,parked_);api_.place(double_,parked_);api_.visible(double_,false);api_.sleep(double_,true);
    auto after=api_.flags(double_);auto p=api_.read(double_);checkLocal(source);placed_=parked_;
    note(parked(after)&&atTarget(p,parked_)?"released":"release_unconfirmed",after,p);
    stage_=parked(after)?Stage::Parked:Stage::Failed;if(stage_==Stage::Parked)double_=player_=0;
}
void Double::tick(bool permitted,const Transform* target,double speed,uint32_t flags){
    if(stage_==Stage::Off||stage_==Stage::Failed)return;
#ifndef MUTANT_DOUBLE_ACTION_MASK
    flags_=flags&ReplayedActions;
#else
    flags_=flags;
#endif
    // Speed comes from position differences and is noisy tick to tick; the double takes it as is on acquire and
    // follows it smoothly after that (about 0.1 s at 20 Hz), so its walk does not stutter.
    double measured=std::isfinite(speed)&&speed>0?std::min(speed,20.):0;
#ifndef MUTANT_DOUBLE_SMOOTH
    speed_=stage_==Stage::Driving?.6*speed_+.4*measured:measured;
#else
    speed_=measured;
#endif
#ifndef MUTANT_DOUBLE_PERMISSION
    if(!permitted)return;
#endif
    try {
        auto source=api_.local();
#ifndef MUTANT_DOUBLE_WORLD
        // No porter, or a new porter entity, means a reload: the old double may be gone, so never touch it again.
        if(double_&&(!source.entity||source.entity!=player_)){forget("world_changed");return;}
#endif
        // Same porter but this pose read failed (torn read, transition): skip the tick and keep everything as it is.
#ifndef MUTANT_DOUBLE_TRANSIENT
        if(!source.entity||!usable(source.pose))return;
#else
        if(!source.entity)return;
#endif
        switch(stage_){
        case Stage::Parked:if(target)acquire(source,*target);break;
        case Stage::Driving:
#ifndef MUTANT_DOUBLE_RELEASE
            if(!target){release(source,false);break;}
#else
            if(!target)break;
#endif
            drive(source,*target);break;
        case Stage::Yielded:
            if(api_.find(source.entity)==double_&&parked(api_.flags(double_))){note("returned_by_game",parkedFlags_,{});double_=player_=0;stage_=Stage::Parked;}
            break;
        default:break;
        }
    } catch(...) {stage_=Stage::Failed;note("failed",0,{});throw;}
}
std::optional<uint32_t> parseCargoCommand(const std::string& text,const std::string& stamp,const char* expected){
    std::istringstream in(text);std::string verb,id,tail,extra;
    if(!(in>>verb>>id>>tail)||(in>>extra)||verb!=expected||tail!=stamp)return std::nullopt;
    if(id.empty()||id.size()>10||id.find_first_not_of("0123456789")!=std::string::npos)return std::nullopt;
    auto v=std::stoull(id);if(v>0xffffffffull)return std::nullopt;
    return uint32_t(v);
}
std::function<void(uint32_t,const Transform&)> nativeCargoCreate(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native cargo API requires Windows");
#else
    const std::vector<uint8_t> sig={0x4c,0x8b,0xdc,0x48,0x81,0xec,0xc8,0,0,0,0x48,0x8b,0x05,0xff,0xa4,0x28};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"cargo code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2cd6b20,"cargo function signature moved/duplicated");}
    }
    require(count==1,"cargo function signature missing/ambiguous");
    using CreateFn=void(*)(uint32_t,const Transform*);auto create=reinterpret_cast<CreateFn>(s.base+0x2cd6b20);
    return [create](uint32_t id,const Transform& t){create(id,&t);};
#endif
}
std::function<void(uint32_t)> nativeCargoGive(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native cargo API requires Windows");
#else
    const std::vector<uint8_t> sig={0x4c,0x8b,0xdc,0x48,0x81,0xec,0xe8,0,0,0,0x48,0x8b,0x05,0xbf,0xa5,0x28};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"cargo code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2cd6a60,"cargo give signature moved/duplicated");}
    }
    require(count==1,"cargo give signature missing/ambiguous");
    using GiveFn=void(*)(uint32_t,bool);auto give=reinterpret_cast<GiveFn>(s.base+0x2cd6a60);
    return [give](uint32_t id){give(id,false);};
#endif
}
std::function<bool()> nativeGameEvent(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native game event API requires Windows");
#else
    const std::vector<uint8_t> sig={0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x1d,0x37,0xe2,0x04,0x48,0x85,0xc0,0x74,0x2d};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"event code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2dd3760,"event function signature moved/duplicated");}
    }
    require(count==1,"event function signature missing/ambiguous");
    using EventFn=bool(*)();auto event=reinterpret_cast<EventFn>(s.base+0x2dd3760);
    return [event]{return event();};
#endif
}
DoubleApi nativeDoubleApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native double API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    const std::array<Entry,8> entries={{
        {0x2361e20,{0x48,0x85,0xc9,0x74,0x2f,0x48,0x8b,0x81,0x88,0,0,0,0x48,0xc1,0xe8,0x08,0x84,0xd2,0x74,0x12,0xa8,0x01,0x75,0x1c,0x48,0x8b,0x01,0x41,0x0f,0xb6,0xd0,0x48,0xff,0xa0,0xa0,0,0,0}},
        {0x2361e90,{0x48,0x85,0xc9,0x0f,0x85,0xa7,0xd7,0xff,0xff,0xc3}},
        {0x2361ea0,{0x48,0x85,0xc9,0x74,0x1c,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xce,0xe0,0xf7,0xff,0x33,0xd2,0x48,0x8b,0xcb,0xe8,0xa4,0xa0,0xff,0xff}},
        // AnimationManager_Exported{Init,Set}{Bool,Float}AnimationVariable
        {0x2397050,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x09,0x8d,0x41,0xff,0x48,0x83,0xc4,0x40,0x5b,0xc3,0x48,0x8d,0x4c,0x24,0x30,0xe8,0x8f}},
        {0x23970b0,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x09,0x8d,0x41,0xff,0x48,0x83,0xc4,0x40,0x5b,0xc3,0x48,0x8d,0x4c,0x24,0x30,0xe8,0x2f}},
        {0x2397220,{0x48,0x85,0xc9,0x74,0x33,0x48,0x63,0xc2,0x48,0x8d,0x14,0x40,0x48,0xc1,0xe2,0x04}},
        {0x23971e0,{0x48,0x85,0xc9,0x74,0x2b,0x48,0x63,0xc2,0x48,0x8d,0x14,0x40,0x48,0xc1,0xe2,0x04}},
        // MorphemeAnimationManager: (re)create its network instance from resource+d8 into manager+a0
        {0x23a2fc0,{0x48,0x89,0x5c,0x24,0x18,0x55,0x48,0x8d,0x6c,0x24,0xc0,0x48,0x81,0xec,0x40,0x01,0,0,0xc5,0xf8,0x29,0xb4,0x24,0x30}}}};
    std::array<unsigned,8> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"double code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"double code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
            ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"double function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"double function signature missing/ambiguous");
    // Slot 0 of each class vtable is its type getter; both must match the pinned image.
    const uint64_t camera=s.base+0x3d198e8,twin=s.base+0x3d7de48,mover=s.base+0x3d0d270;uint64_t g=0;
    require(q(s,camera,g)&&g==s.base+0x242daf0&&q(s,twin,g)&&g==s.base+0x2711a80&&q(s,mover,g)&&g==s.base+0x23b3e40,"double vtables differ from pinned image");
    using SleepFn=void(*)(uint64_t,bool,uint8_t);using VisibleFn=void(*)(uint64_t,bool);
    using InitFn=int(*)(uint64_t,const char*);using SetFloatFn=void(*)(uint64_t,int,float);using SetBoolFn=void(*)(uint64_t,int,bool);
    auto sleep=reinterpret_cast<SleepFn>(s.base+entries[0].rva);auto visible=reinterpret_cast<VisibleFn>(s.base+entries[1].rva);
    auto place=reinterpret_cast<PlaceFn>(s.base+entries[2].rva);
    auto initBool=reinterpret_cast<InitFn>(s.base+entries[3].rva);auto initFloat=reinterpret_cast<InitFn>(s.base+entries[4].rva);
    auto setFloat=reinterpret_cast<SetFloatFn>(s.base+entries[5].rva);auto setBool=reinterpret_cast<SetBoolFn>(s.base+entries[6].rva);
    const uint64_t animator=s.base+0x3d09910;   // MorphemeAnimationManager
    // Handles are per manager; the double's network (sam/references/akukan) has these locomotion controls.
    struct Handles{uint64_t manager=0;int speed=-1,move=-1,moveFloat=-1,moveInterp=-1,crouchRate=-1,basic=-1;int action[7]={-1,-1,-1,-1,-1,-1,-1};};auto handles=std::make_shared<Handles>();
    // The borrowed instance is kept after release and swapped back in on the next borrow of the same manager,
    // so repeated borrows build one instance per double, not one per borrow.
    struct Borrowed{uint64_t manager=0,own=0,instance=0,def=0;};auto borrowed=std::make_shared<Borrowed>();
    using RecreateFn=void(*)(uint64_t);auto recreate=reinterpret_cast<RecreateFn>(s.base+entries[7].rva);
    const uint64_t managerResource=s.base+0x3d0a7e8,networkDef=s.base+0x3ba8a40,floorEffect=s.base+0x3d7db80;
    require(q(s,managerResource,g)&&g==s.base+0x2398f00&&q(s,networkDef,g)&&g==s.base+0x1a4c150&&q(s,floorEffect,g)&&g==s.base+0x2711a40,"animation vtables differ from pinned image");
    struct Effect{uint64_t child=0;bool visible=false,sleeping=false;};
    struct Effects{uint64_t entity=0;std::vector<Effect> recorded;};auto effects=std::make_shared<Effects>();
    auto managerOf=[&s,animator](uint64_t e)->uint64_t{
        uint32_t count=0;uint64_t list=0;
        require(s.read(e+0x90,&count,4)&&count<=4096&&q(s,e+0x98,list),"entity components unreadable");
        for(uint32_t i=0;i<count;++i){uint64_t c=0,v=0,owner=0;if(q(s,list+8*i,c)&&q(s,c,v)&&v==animator&&q(s,c+0x48,owner)&&owner==e)return c;}
        return 0;
    };
    auto writable=[](uint64_t at,size_t n){MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE
               &&at+n<=reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize;};
    auto check=[&s,twin](uint64_t e){uint64_t v=0;require(e&&q(s,e,v)&&v==twin,"double identity changed");};
    return {[&s]{return readLocalAnchor(s);},[&s,camera,twin](uint64_t p){return findDouble(s,p,camera,twin);},
        [&s,check](uint64_t e){check(e);return readFlags(s,e);},[&s,check](uint64_t e){check(e);return readEntityPose(s,e);},
        [sleep,check](uint64_t e,bool b){check(e);sleep(e,b,0);},[visible,check](uint64_t e,bool b){check(e);visible(e,b);},
        [place,check](uint64_t e,const Transform& t){check(e);place(e,&t);},
        [&s,check,mover](uint64_t e,const Transform& t){
            check(e);uint64_t m=0,v=0,owner=0;
            require(q(s,e+MoverOffset,m)&&q(s,m,v)&&v==mover&&q(s,m+MoverOwnerOffset,owner)&&owner==e,"double mover identity changed");
            MEMORY_BASIC_INFORMATION info{};auto at=m+MoverTransformOffset;
            require(VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE
                    &&at+60<=reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize,"double mover not plain writable memory");
            std::memcpy(reinterpret_cast<void*>(at),&t,60);   // position (3 x f64) + rotation (3x3 f32), the entity+c8 layout
        },
        [check,managerOf,handles,initBool,initFloat,setFloat,setBool](uint64_t e,double speed,uint32_t flags){
            check(e);auto manager=managerOf(e);
            if(!manager)return;   // no animation manager: position-only body
            if(handles->manager!=manager){*handles={};handles->manager=manager;
                handles->speed=initFloat(manager,"Speed");handles->move=initBool(manager,"DSMove");
                handles->moveFloat=initFloat(manager,"Move_Float");handles->moveInterp=initFloat(manager,"Move_Float_Interp");
                for(unsigned k=0;k<7;++k)handles->action[k]=initBool(manager,ActionNames[k]);
                handles->crouchRate=initFloat(manager,"Crouch_Rate");handles->basic=initBool(manager,"Is_BasicAction");}
            bool moving=speed>MovingSpeed;
            if(handles->speed>=0)setFloat(manager,handles->speed,float(speed));
            if(handles->move>=0)setBool(manager,handles->move,moving);
            if(handles->moveFloat>=0)setFloat(manager,handles->moveFloat,moving?1.f:0.f);
            if(handles->moveInterp>=0)setFloat(manager,handles->moveInterp,moving?1.f:0.f);
            for(unsigned k=0;k<7;++k)if(handles->action[k]>=0){
                bool on=flags&ActionBits[k];if(ActionBits[k]==FlagSprintBit)on=on&&moving;
                setBool(manager,handles->action[k],on);
            }
            if(handles->crouchRate>=0)setFloat(manager,handles->crouchRate,(flags&FlagCrouchBit)?1.f:0.f);
            if(handles->basic>=0)setBool(manager,handles->basic,!(flags&ActionsBit));
        },
        [&s,check,managerOf,handles,borrowed,recreate,managerResource,networkDef,writable](uint64_t e,uint64_t player)->uint64_t{
            check(e);auto dm=managerOf(e),pm=managerOf(player);if(!dm||!pm)return 0;
            uint64_t dres=0,pres=0,v=0,ddef=0,pdef=0,old=0,now=0;
            require(q(s,dm+0x30,dres)&&q(s,dres,v)&&v==managerResource&&q(s,pm+0x30,pres)&&q(s,pres,v)&&v==managerResource,"animation manager resource identity");
            require(q(s,dres+0xd8,ddef)&&q(s,ddef,v)&&v==networkDef&&q(s,pres+0xd8,pdef)&&q(s,pdef,v)&&v==networkDef,"network definition identity");
            if(ddef==pdef||!q(s,dm+0xa0,old)||!old)return 0;
            if(borrowed->manager==dm&&borrowed->own==old&&borrowed->def==pdef&&borrowed->instance&&q(s,borrowed->instance+0x38,v)&&v==pdef){
                require(writable(dm+0xa0,8),"double manager not plain writable memory");
                std::memcpy(reinterpret_cast<void*>(dm+0xa0),&borrowed->instance,8);handles->manager=0;return old;
            }
            require(writable(dres+0xd8,8),"double manager resource not plain writable memory");
            // The definition is read only while the instance is built; the resource is restored at once.
            std::memcpy(reinterpret_cast<void*>(dres+0xd8),&pdef,8);recreate(dm);std::memcpy(reinterpret_cast<void*>(dres+0xd8),&ddef,8);
            require(q(s,dm+0xa0,now)&&now&&now!=old&&q(s,now+0x38,v)&&v==pdef,"network instance not rebuilt");
            *borrowed={dm,old,now,pdef};handles->manager=0;return old;   // variable handles are looked up again for the new network
        },
        [&s,check,managerOf,handles,borrowed,writable](uint64_t e,uint64_t original){
            check(e);auto dm=managerOf(e);require(dm&&writable(dm+0xa0,8),"double manager unavailable for restore");
            uint64_t now=0;if(borrowed->manager==dm&&q(s,dm+0xa0,now))borrowed->instance=now;   // kept for the next borrow
            std::memcpy(reinterpret_cast<void*>(dm+0xa0),&original,8);handles->manager=0;
        },
        // show=false: on the first call of a borrow record each floor effect's visibility, then hide any that are visible.
        // show=true: put every recorded effect back to its recorded visibility.
        // Waking the double wakes its children, and an awake floor effect shows itself every frame, so they are put back
        // to sleep as well as hidden. show=true restores each recorded effect's visibility and sleep state.
        [&s,check,visible,sleep,floorEffect,effects](uint64_t e,bool show){
            check(e);
            auto ours=[&](uint64_t c){uint64_t v=0,parent=0;return q(s,c,v)&&v==floorEffect&&q(s,c+ParentOffset,parent)&&parent==e;};
            auto flags=[&](uint64_t c){uint64_t f=0;q(s,c+FlagsOffset,f);return f;};
            if(show){
                if(effects->entity==e)for(auto& r:effects->recorded)if(ours(r.child)){
                    auto f=flags(r.child);
                    if(bool(f&FlagSleeping)!=r.sleeping)sleep(r.child,r.sleeping,0);
                    if(bool(f&FlagVisible)!=r.visible)visible(r.child,r.visible);
                }
                *effects={};return;
            }
            bool first=effects->entity!=e;if(first)*effects={e,{}};
            uint64_t c=0;q(s,e+FirstChildOffset,c);
            for(unsigned n=0;c&&n<MaxChildren;++n){
                if(ours(c)){auto f=flags(c);
                    if(first)effects->recorded.push_back({c,bool(f&FlagVisible),bool(f&FlagSleeping)});
                    if(f&FlagVisible)visible(c,false);
                    if(!(f&FlagSleeping))sleep(c,true,0);
                }
                if(!q(s,c+NextSiblingOffset,c))break;
            }
        }};
#endif
}
}
