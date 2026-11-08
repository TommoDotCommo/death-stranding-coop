#include "double.h"
#include <algorithm>
#include <cctype>
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
    // The manager found last time, if it is still this entity's (2 reads instead of a walk over every component).
    if(c.manager&&c.entity==e){uint64_t v=0,owner=0;if(q(s,c.manager,v)&&v==animator&&q(s,c.manager+0x48,owner)&&owner==e)manager=c.manager;}
    if(!manager&&(!s.read(e+0x90,&components,4)||components>4096||!q(s,e+0x98,list)))return 0;
    for(uint32_t i=0;i<components&&!manager;++i){uint64_t m=0,v=0,owner=0;
#ifndef MUTANT_DOUBLE_ACTION_OWNER
        if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator&&q(s,m+0x48,owner)&&owner==e)manager=m;
#else
        if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator)manager=m;
#endif
    }
    uint32_t count=0;uint64_t slots=0;
    if(!manager||!s.read(manager+0x50,&count,4)||!count||count>4096||!q(s,manager+0x58,slots))return 0;
    if(c.manager!=manager||c.count!=count||c.entity!=e){
        c=ActionCache{};c.manager=manager;c.count=count;c.entity=e;
        for(uint32_t i=0;i<count;++i){
            uint64_t name=0;char text[24]{};if(!q(s,slots+48ull*i,name)||!s.read(name,text,sizeof text-1))continue;
            for(unsigned k=0;k<7;++k)if(!std::strcmp(text,ActionNames[k]))c.index[k]=int(i);
        }
    }
    uint32_t out=0;
    for(unsigned k=0;k<7;++k){uint8_t b=0;if(c.index[k]>=0&&s.read(slots+48ull*unsigned(c.index[k])+0x20,&b,1)&&b)out|=ActionBits[k];}
    return out;
}
std::optional<float> readFloatVariable(const discovery::Sampler& s,uint64_t e,uint64_t animator,const char* want,FloatVariable& c){
    uint32_t components=0;uint64_t list=0,manager=0;
    if(c.manager&&c.entity==e){uint64_t v=0,owner=0;if(q(s,c.manager,v)&&v==animator&&q(s,c.manager+0x48,owner)&&owner==e)manager=c.manager;}
    if(!manager&&(!s.read(e+0x90,&components,4)||components>4096||!q(s,e+0x98,list)))return std::nullopt;
    for(uint32_t i=0;i<components&&!manager;++i){uint64_t m=0,v=0,owner=0;
        if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator&&q(s,m+0x48,owner)&&owner==e)manager=m;}
    uint32_t count=0;uint64_t slots=0;
    if(!manager||!s.read(manager+0x50,&count,4)||!count||count>4096||!q(s,manager+0x58,slots))return std::nullopt;
    if(c.manager!=manager||c.count!=count||c.entity!=e){
        c=FloatVariable{};c.manager=manager;c.count=count;c.entity=e;
        for(uint32_t i=0;i<count;++i){uint64_t name=0;char text[32]{};
            if(q(s,slots+48ull*i,name)&&s.read(name,text,sizeof text-1)&&!std::strcmp(text,want)){c.index=int(i);break;}}
    }
    float v=0;
    if(c.index<0||!s.read(slots+48ull*unsigned(c.index)+0x20,&v,4)||!std::isfinite(v))return std::nullopt;
    return v;
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
float vehicleAnimationType(uint32_t kind){return kind==1?0.f:kind==2?1.f:kind==3?3.f:-1.f;}
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
    switch(stage_){case Stage::Off:return "off";case Stage::Parked:return "parked";case Stage::Driving:return "driving";case Stage::Hidden:return "hidden";case Stage::Yielded:return "yielded";default:return "failed";}
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
    if(api_.steer&&(flags_&VehicleMask))api_.steer(double_,float(steer_));
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
// Hidden: still borrowed (network, effects, mover), asleep and invisible where the partner was last seen.
void Double::hide(const Anchor& source){
    api_.animate(double_,0,0);api_.visible(double_,false);api_.sleep(double_,true);
    auto after=api_.flags(double_);checkLocal(source);
    if(!parked(after)){note("hide_unconfirmed",after,{});release(source,true);return;}
    stage_=Stage::Hidden;note("hidden",after,{});
}
// While hidden the double looks parked to the game. If the game takes it (private room) or it changes, give it up.
bool Double::stillOurs(const Anchor& source){
#ifndef MUTANT_DOUBLE_IDENTITY
    if(api_.find(source.entity)!=double_){forget("double_changed");return false;}
#endif
#ifndef MUTANT_DOUBLE_HIDDEN_YIELD
    auto f=api_.flags(double_);
    if(!parked(f)){restoreNetwork();stage_=Stage::Yielded;note("yielded",f,api_.read(double_));return false;}
#endif
    return true;
}
void Double::reveal(const Anchor& source,const Transform& target){
    placed_=target;api_.anchor(double_,target);api_.place(double_,target);api_.visible(double_,true);api_.sleep(double_,false);
    api_.animate(double_,speed_,flags_);
    if(api_.steer&&(flags_&VehicleMask))api_.steer(double_,float(steer_));
    if(hidEffects_)api_.effects(double_,false);
    auto after=api_.flags(double_);auto p=api_.read(double_);checkLocal(source);
    if(!driven(after)||!atTarget(p,target)){note("reveal_unconfirmed",after,p);release(source,true);stage_=Stage::Failed;return;}
    stage_=Stage::Driving;note("shown",after,p);
}
void Double::tick(bool permitted,const Transform* target,double speed,uint32_t flags,bool hold){
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
            if(!target){
#ifndef MUTANT_DOUBLE_HOLD
                if(hold&&driven(api_.flags(double_))&&api_.find(source.entity)==double_){hide(source);break;}
#endif
                release(source,false);break;}
#else
            if(!target)break;
#endif
            drive(source,*target);break;
        case Stage::Hidden:
            if(!stillOurs(source))break;
            if(target){reveal(source,*target);break;}
            if(!hold)release(source,true);
            break;
        case Stage::Yielded:
            if(api_.find(source.entity)==double_&&parked(api_.flags(double_))){note("returned_by_game",parkedFlags_,{});double_=player_=0;stage_=Stage::Parked;}
            break;
        default:break;
        }
    } catch(...) {stage_=Stage::Failed;note("failed",0,{});throw;}
}
uint64_t OwnBody::tick(const Anchor& porter){
    if(!porter.entity||!porter.pose.available)return 0;
#ifndef MUTANT_OWNBODY_WORLD
    if(porter.entity!=porter_){if(body_)record_("BODY world changed: a new body will be made\n");porter_=porter.entity;body_=0;}
#endif
#ifndef MUTANT_OWNBODY_ALIVE
    if(body_&&!api_.alive(body_)){record_("BODY lost: a new body will be made\n");body_=0;}
#endif
    if(body_||exhausted())return body_;
#ifndef MUTANT_OWNBODY_READY
    if(api_.ready&&waited_<ReadyWaitTicks&&!api_.ready(porter.entity)){
        if(!waited_++)record_("BODY waiting: the game has not dressed the porter yet\n");
        if(waited_==ReadyWaitTicks)record_("BODY waited long enough; making the body anyway\n");
        return 0;
    }
#endif
    auto resource=api_.resource(porter.entity);if(!resource)return 0;
    // Made 3 m in front of the porter (the placement tried live), then parked at once.
    auto t=transform(porter.pose);double l=std::hypot(t.rotation[3],t.rotation[4]);
    if(l>.5){t.position[0]+=3*t.rotation[3]/l;t.position[1]+=3*t.rotation[4]/l;}
    body_=api_.create(resource,t);
    if(!body_){++failures_;record_(exhausted()?"BODY creation refused; giving up, the resident double is used\n":"BODY creation refused\n");return 0;}
#ifndef MUTANT_OWNBODY_DRESS
    if(api_.dress)record_(api_.dress(body_)?"BODY model components built\n":"BODY model components could not be built\n");
#endif
#ifndef MUTANT_OWNBODY_PARK
    api_.park(body_);
#endif
    std::ostringstream o;o<<"BODY own body 0x"<<std::hex<<body_<<" made and parked\n";record_(o.str());
    return body_;
}
void OwnBody::remove(){if(body_){api_.remove(body_);record_("BODY own body removed\n");body_=0;}}
std::optional<unsigned> parseOwnCommand(const std::string& command,const std::string& stamp){
    if(command=="own"+stamp)return DressArtPartsInPlace;   // live 2026-11-08: one ArtPartsData, drawn cleanly (was 8: apply, two sets)
    if(command.rfind("own",0)!=0||command.size()<=3+stamp.size()||command.compare(command.size()-stamp.size(),stamp.size(),stamp))return std::nullopt;
    auto digits=command.substr(3,command.size()-3-stamp.size());
    if(digits.empty()||digits.size()>2)return std::nullopt;
    for(char c:digits)
#ifndef MUTANT_OWNBODY_OWN_PARSE
        if(!std::isxdigit(static_cast<unsigned char>(c))||std::isupper(static_cast<unsigned char>(c)))return std::nullopt;
#else
        if(!std::isxdigit(static_cast<unsigned char>(c)))return std::nullopt;
#endif
    return unsigned(std::stoul(digits,nullptr,16));
}
namespace {
// The entity's own components of one class (vtable), in list order; false when the list is unreadable.
bool ownComponents(const discovery::Sampler& s,uint64_t entity,uint64_t vtable,std::vector<uint64_t>& out){
    uint32_t count=0;uint64_t list=0;
    if(!s.read(entity+0x90,&count,4)||count>4096||!q(s,entity+0x98,list))return false;
    for(uint32_t i=0;i<count;++i){uint64_t c=0,v=0,owner=0;
        if(!q(s,list+8*i,c))return false;
        if(q(s,c,v)&&v==vtable&&q(s,c+ComponentOwnerOffset,owner)&&owner==entity)out.push_back(c);}
    return true;
}
// The entity's component at a cached slot when it is its own and of the class; 0 otherwise.
uint64_t ownSlot(const discovery::Sampler& s,uint64_t entity,uint64_t slot,uint64_t vtable){
    uint64_t c=0,v=0,owner=0;
    if(!q(s,entity+slot,c)||!c||!q(s,c+ComponentOwnerOffset,owner)||owner!=entity)return 0;
    if(vtable&&(!q(s,c,v)||v!=vtable))return 0;
    return c;
}
std::string hex(uint64_t v){std::ostringstream o;o<<"0x"<<std::hex<<v;return o.str();}
}
ModelSwapPlan planModelSwap(const discovery::Sampler& s,uint64_t body,uint64_t resident,uint64_t artParts,bool exact){
    ModelSwapPlan p;
    if(!body||!resident||body==resident){p.refusal="no resident double";return p;}
    if(!ownSlot(s,body,EntityModelOffset,0)){p.refusal="body has no model of its own";return p;}
#ifndef MUTANT_OWNBODY_MODEL_OWNER
    uint64_t model=ownSlot(s,resident,EntityModelOffset,0);
#else
    uint64_t model=0;q(s,resident+EntityModelOffset,model);
#endif
    uint64_t resource=0,builds=0,primary=ownSlot(s,resident,EntityArtPartsOffset,artParts),wanted=0;
    if(!model||!q(s,model+ComponentResourceOffset,resource)||!resource){p.refusal="double's model unreadable";return p;}
    if(!primary||!q(s,primary+ComponentResourceOffset,wanted)||!wanted){p.refusal="double's art parts unreadable";return p;}
    if(!q(s,resource+ModelArtPartsOffset,builds)){p.refusal="double's model resource unreadable";return p;}
    p.builds=builds;p.wanted=wanted;
    if(!builds){p.refusal="double's model builds no art parts";return p;}
#ifndef MUTANT_OWNBODY_MODEL_EXACT
    if(exact&&builds!=wanted){p.refusal="double's model builds other art parts than the double wears";return p;}
#else
    (void)exact;
#endif
    p.model=resource;return p;
}
bool dressedLike(const discovery::Sampler& s,uint64_t body,uint64_t artParts,uint64_t wanted){
    std::vector<uint64_t> all;if(!ownComponents(s,body,artParts,all))return false;
    uint64_t primary=ownSlot(s,body,EntityArtPartsOffset,artParts),resource=0;
    if(!primary||!q(s,primary+ComponentResourceOffset,resource)||resource!=wanted)return false;
#ifndef MUTANT_OWNBODY_DRESSED_COUNT
    return all.size()==1&&all[0]==primary;
#else
    return std::find(all.begin(),all.end(),primary)!=all.end();
#endif
}
std::string describeBody(const discovery::Sampler& s,const char* label,uint64_t entity,uint64_t artParts,unsigned part){
    std::ostringstream o;uint32_t count=0;uint64_t list=0;
    if(!entity||!s.read(entity+0x90,&count,4)||count>4096||!q(s,entity+0x98,list)){o<<"BODY report "<<label<<" unreadable\n";return o.str();}
    o<<"BODY report "<<label<<" "<<hex(entity)<<" components "<<count<<" model "<<hex(ownSlot(s,entity,EntityModelOffset,0))
     <<" artparts "<<hex(ownSlot(s,entity,EntityArtPartsOffset,artParts))<<"\n";
    for(uint32_t i=0;i<count;++i){
        uint64_t c=0,v=0,owner=0,resource=0;if(!q(s,list+8*i,c)){o<<"BODY report "<<label<<" component "<<i<<" unreadable\n";continue;}
        q(s,c,v);q(s,c+ComponentOwnerOffset,owner);q(s,c+ComponentResourceOffset,resource);
        o<<"BODY report "<<label<<" component "<<i<<" vtable +"<<hex(v>=s.base?v-s.base:v)<<(owner==entity?"":" foreign")<<" resource "<<hex(resource);
        if(v==artParts){
            uint32_t parts=0;uint64_t array=0,p=0;uint8_t state=0;s.read(c+0x100,&parts,4);
            o<<" parts "<<parts;
            if(part<parts&&parts<=4096&&q(s,c+0x108,array)&&q(s,array+8ull*part,p)&&p&&s.read(p+0x2c,&state,1))o<<" part"<<part<<" state "<<hex(state);
            // The resource's name map: hash -> index (12-byte entries {used, hash, index}, capacity at +ec).
            int32_t capacity=0;uint64_t map=0;
            if(resource&&s.read(resource+0xec,&capacity,4)&&capacity>0&&capacity<=4096&&q(s,resource+0xe0,map))
                for(int32_t k=0;k<capacity;++k){uint32_t e[3]{};if(!s.read(map+12ull*unsigned(k),e,12))break;
                    if(e[0]&&e[2]==part){o<<" part"<<part<<" hash "<<hex(e[1]);break;}}
        }
        o<<"\n";
    }
    return o.str();
}
bool dressByModel(const discovery::Sampler& s,uint64_t body,uint64_t resident,uint64_t artParts,unsigned mask,
                  const ModelSwapCalls& calls,const std::function<void(const std::string&)>& note){
    auto say=[&](const std::string& row){if(note)note(row);};
    bool exact=mask&DressModelExact;
    auto plan=planModelSwap(s,body,resident,artParts,exact);
    if(!plan.model){say("BODY model swap refused: "+plan.refusal+" (builds "+hex(plan.builds)+", wears "+hex(plan.wanted)+")\n");return false;}
    say("BODY model swap: SetModel with the double's model resource "+hex(plan.model)+" (builds "+hex(plan.builds)+", wears "+hex(plan.wanted)+")\n");
    calls.setModel(body,plan.model);
    bool done=dressedLike(s,body,artParts,plan.wanted);
#ifndef MUTANT_OWNBODY_APPLY_ALWAYS
    if(!done&&!exact&&(mask&DressModelThenApply)){
#else
    if(!exact&&(mask&DressModelThenApply)){
#endif
        say("BODY model swap left the body unlike the double: applying the double's art parts\n");
        calls.apply(body,plan.wanted);done=dressedLike(s,body,artParts,plan.wanted);
    }
    say(done?"BODY dressed like the double: one ArtPartsData, the double's\n":"BODY not dressed like the double after the model swap\n");
    return done;
}
ArtPartsSwapPlan planArtPartsSwap(const discovery::Sampler& s,uint64_t body,uint64_t resident,uint64_t artParts){
    ArtPartsSwapPlan p;
    if(!body||!resident||body==resident){p.refusal="no resident double";return p;}
    std::vector<uint64_t> all;uint64_t primary=ownSlot(s,body,EntityArtPartsOffset,artParts);
    if(!ownComponents(s,body,artParts,all)||!primary||!q(s,primary+ComponentResourceOffset,p.wears)||!p.wears){p.refusal="body's art parts unreadable";return p;}
#ifndef MUTANT_OWNBODY_INPLACE_SINGLE
    // A second ArtPartsData (from mask 8's apply) would stay beside the replaced one: the drawing this mask is meant to avoid.
    if(all.size()!=1||all[0]!=primary){p.refusal="body has more than one ArtPartsData";return p;}
#endif
    uint64_t wanted=0;
#ifndef MUTANT_OWNBODY_INPLACE_RESOURCE
    uint64_t worn=ownSlot(s,resident,EntityArtPartsOffset,artParts);
    if(!worn||!q(s,worn+ComponentResourceOffset,wanted)||!wanted){p.refusal="double's art parts unreadable";return p;}
#else
    // What the double's model builds (its ModelResource+30), not what the double wears.
    uint64_t model=ownSlot(s,resident,EntityModelOffset,0),resource=0;
    if(!model||!q(s,model+ComponentResourceOffset,resource)||!q(s,resource+ModelArtPartsOffset,wanted)||!wanted){p.refusal="double's art parts unreadable";return p;}
#endif
#ifndef MUTANT_OWNBODY_INPLACE_SAME
    if(wanted==p.wears){p.refusal="body already wears the double's art parts";return p;}
#endif
    p.wanted=wanted;return p;
}
uint64_t readStreamingDouble(const discovery::Sampler& s,uint64_t global){
    uint64_t object=0,ref=0;
    if(!q(s,global,object)||!object||!q(s,object+StreamingDoubleRef,ref)||ref<0x20)return 0;
#ifndef MUTANT_OWNBODY_STREAMING_REF
    return ref-0x20;   // a reference points at entity+20
#else
    return ref;
#endif
}
unsigned wornParts(const discovery::Sampler& s,uint64_t entity,uint64_t artParts){
    uint64_t c=ownSlot(s,entity,EntityArtPartsOffset,artParts);uint32_t parts=0;
    if(!c||!s.read(c+0x100,&parts,4)||parts>4096)return 0;
    return parts;
}
bool dressInPlace(const discovery::Sampler& s,uint64_t body,uint64_t resident,uint64_t artParts,
                  const std::function<void(uint64_t,uint64_t)>& setArtParts,const std::function<void(const std::string&)>& note,uint64_t porter){
    auto say=[&](const std::string& row){if(note)note(row);};
    auto plan=planArtPartsSwap(s,body,resident,artParts);
#ifndef MUTANT_OWNBODY_INPLACE_PORTER
    // Live 2026-11-08 (two copies): right after a load the game had not dressed its double yet (it still wore the
    // one-part set, like a made body). The streaming control dresses the double with the porter's own set, so take
    // that one directly.
    if(!plan.wanted&&porter&&plan.wears&&plan.refusal=="body already wears the double's art parts") {
        auto fromPorter=planArtPartsSwap(s,body,porter,artParts);
        if(fromPorter.wanted){say("BODY art-parts swap: the double is not dressed yet; using the porter's art parts\n");plan=fromPorter;}
    }
#endif
    if(!plan.wanted){say("BODY art-parts swap refused: "+plan.refusal+" (wears "+hex(plan.wears)+")\n");return false;}
    say("BODY art-parts swap: the game's in-place replacement with the double's art parts "+hex(plan.wanted)+" (wears "+hex(plan.wears)+")\n");
    setArtParts(body,plan.wanted);
    bool done=dressedLike(s,body,artParts,plan.wanted);
    say(done?"BODY dressed like the double: one ArtPartsData, the double's\n":"BODY not dressed like the double after the art-parts swap\n");
    return done;
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
std::function<void(uint32_t)> nativeCargoDelete(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native cargo API requires Windows");
#else
    const std::vector<uint8_t> sig={0x4c,0x8b,0xdc,0x48,0x81,0xec,0xe8,0,0,0,0x48,0x8b,0x05,0xdf,0xa2,0x28};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"cargo code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2cd6d40,"cargo delete signature moved/duplicated");}
    }
    require(count==1,"cargo delete signature missing/ambiguous");
    using DeleteFn=void(*)(uint32_t);auto erase=reinterpret_cast<DeleteFn>(s.base+0x2cd6d40);
    return [erase](uint32_t id){erase(id);};
#endif
}
std::function<void(uint8_t,const Transform&,bool)> nativeConstruct(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native construction API requires Windows");
#else
    const std::vector<uint8_t> sig={0x40,0x53,0x56,0x57,0x48,0x81,0xec,0x00,0x07,0x00,0x00,0x48,0x8b,0x05,0x1e,0xa4,0x28,0x02,0x48,0x33};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"construction code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2cd6c00,"construction signature moved/duplicated");}
    }
    require(count==1,"construction signature missing/ambiguous");
    using ConstructFn=void(*)(uint8_t,const Transform*,bool);auto make=reinterpret_cast<ConstructFn>(s.base+0x2cd6c00);
    return [make](uint8_t type,const Transform& t,bool flag){make(type,&t,flag);};
#endif
}
std::function<void(const std::array<float,4>&,const std::array<float,4>&,const Transform&,bool,bool,bool)> nativeRemoveConstructions(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native construction API requires Windows");
#else
    const std::vector<uint8_t> sig={0x4c,0x8b,0xdc,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x05,0xbe,0x78,0x28,0x02,0x48,0x33};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"construction code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2cd9760,"construction removal signature moved/duplicated");}
    }
    require(count==1,"construction removal signature missing/ambiguous");
    using RemoveFn=void(*)(const float*,const float*,const Transform*,bool,bool,bool);auto erase=reinterpret_cast<RemoveFn>(s.base+0x2cd9760);
    return [erase](const std::array<float,4>& lo,const std::array<float,4>& hi,const Transform& t,bool a,bool b,bool c){
        alignas(16) float l[4]={lo[0],lo[1],lo[2],lo[3]},h[4]={hi[0],hi[1],hi[2],hi[3]};erase(l,h,&t,a,b,c);};
#endif
}
std::function<bool(uint64_t,unsigned,bool)> nativePartToggle(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native part toggle requires Windows");
#else
    const std::vector<uint8_t> toggleSig={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x0f,0xb6,0x59,0x2c,0x4d};
    {std::vector<uint8_t> at(toggleSig.size());require(s.read(s.base+0x22b8fe0,at.data(),at.size())&&at==toggleSig,"part toggle signature differs");}
    using ToggleFn=void(*)(uint64_t,bool,uint64_t,uint64_t,uint64_t);auto toggle=reinterpret_cast<ToggleFn>(s.base+0x22b8fe0);
    const uint64_t artParts=s.base+0x3cf2b18;
    return [&s,toggle,artParts](uint64_t body,unsigned index,bool on){
        uint32_t count=0;uint64_t list=0,model=0;
        if(!s.read(body+0x90,&count,4)||count>4096||!q(s,body+0x98,list)||!q(s,body+0xb8,model))return false;
        for(uint32_t i=0;i<count;++i){
            uint64_t c=0,v=0,owner=0,arr=0,part=0;uint32_t parts=0;
            if(!q(s,list+8*i,c)||!q(s,c,v)||v!=artParts||!q(s,c+0x48,owner)||owner!=body||!s.read(c+0x100,&parts,4)||parts<2)continue;
            if(index>=parts||!q(s,c+0x108,arr)||!q(s,arr+8*index,part)||!part)return false;
            // Part +2c bit 0 is the enabled request; the worker re-applies the part on every call, so only call it on a change.
            uint8_t state=0;if(!s.read(part+0x2c,&state,1))return false;
            if(bool(state&1)!=on)toggle(part,on,body,model,c);
            return true;
        }
        return false;
    };
#endif
}
std::function<bool(uint64_t)> nativeDress(discovery::Sampler& s,std::function<unsigned()> mask,std::function<void(const std::string&)> note){
#ifndef _WIN32
    (void)s;(void)mask;(void)note;throw std::runtime_error("native dress API requires Windows");
#else
    struct Entry{uint64_t rva;uint8_t slot;};const std::array<Entry,3> entries={{{0x22b2010,0x60},{0x22b1f80,0x68},{0x22b20a0,0x70}}};
    for(auto& e:entries){
        const std::vector<uint8_t> sig={0x40,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0x05};
        std::vector<uint8_t> tail={0x48,0x33,0xc4,0x48,0x89,0x44,0x24,0x30,0x48,0x8b,0x41,e.slot,0x48,0x8b,0xf1,0x48,0x85,0xc0,0x75};
        std::vector<uint8_t> head(sig.size()+4+tail.size());require(s.read(s.base+e.rva,head.data(),head.size()),"dress code unreadable");
        require(std::equal(sig.begin(),sig.end(),head.begin())&&std::equal(tail.begin(),tail.end(),head.begin()+sig.size()+4),"dress function signature differs");
    }
    const uint64_t artParts=s.base+0x3cf2b18;
    // ArtPartsVariation_sExportedApplyArtPartsVariation (22bc100): adds a component built from a resource and applies
    // it. Mask bit 8 gives the body the resident double's art-parts resource (the porter's, 52 parts) this way.
    const std::vector<uint8_t> applySig={0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x19,0x48,0x85,0xd2,0x74,0x14,0x41,0xb0,0x01,0xe8,0x7a,0xed,0x08,0x00};
    {std::vector<uint8_t> at(applySig.size());require(s.read(s.base+0x22bc100,at.data(),at.size())&&at==applySig,"apply variation signature differs");}
    using ApplyFn=void(*)(uint64_t,uint64_t);auto apply=reinterpret_cast<ApplyFn>(s.base+0x22bc100);
    const uint64_t camera=s.base+0x3d198e8,twin=s.base+0x3d7de48;
    using MakeFn=uint64_t(*)(uint64_t);
    std::array<MakeFn,3> make;for(unsigned i=0;i<3;++i)make[i]=reinterpret_cast<MakeFn>(s.base+entries[i].rva);
    // Entity SetModel (235e010: entity, ModelResource, blend): the engine's own model swap. Under the entity lock it takes
    // the entity out of the scene, removes the primary ArtPartsData (entity+180) and the old model, builds the new model
    // and an ArtPartsData from ModelResource+30, then puts the entity back (game_offsets.md). Mask 0x10 / 0x20 only.
    const std::vector<uint8_t> setModelSig={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,
        0x48,0x81,0xec,0xd0,0x00,0x00,0x00,0xc5,0xf8,0x29,0xb4,0x24,0xc0,0x00,0x00,0x00,0x48,0x8b,0x05,0xf4,0x2f,0xc0,0x02,0x48,0x33,0xc4,0x48,0x89,0x45,0x0f,
        0x4c,0x8b,0xb1,0xb8,0x00,0x00,0x00};
    bool setModelOk=false;{std::vector<uint8_t> at(setModelSig.size());setModelOk=s.read(s.base+0x235e010,at.data(),at.size())&&at==setModelSig;}
    using SetModelFn=void(*)(uint64_t,uint64_t,float);auto setModel=reinterpret_cast<SetModelFn>(s.base+0x235e010);
    // The in-place art-parts replacement (235d320: entity, ArtPartsDataResource, force) that the porter's
    // DSPlayerStreamingControlComponent calls on the porter and on the resident double (game_offsets.md). Mask 0x80 only.
    const std::vector<uint8_t> inPlaceSig={0x40,0x55,0x57,0x41,0x56,0x48,0x8b,0xec,0x48,0x83,0xec,0x70,0x48,0x8b,0x05,0xfd,0x3c,0xc0,0x02,0x48,0x33,0xc4,
        0x48,0x89,0x45,0xe0,0x48,0x8b,0x81,0x80,0x01,0x00,0x00,0x4c,0x8b,0xf2,0x48,0x8b,0xf9,0x48,0x85,0xc0,0x74,0x0f,0x48,0x39,0x50,0x30};
    bool inPlaceOk=false;{std::vector<uint8_t> at(inPlaceSig.size());inPlaceOk=s.read(s.base+0x235d320,at.data(),at.size())&&at==inPlaceSig;}
    using InPlaceFn=void(*)(uint64_t,uint64_t,bool);auto inPlace=reinterpret_cast<InPlaceFn>(s.base+0x235d320);
    return [&s,artParts,make,entries,mask,apply,camera,twin,note,setModelOk,setModel,inPlaceOk,inPlace](uint64_t body){
        unsigned m=mask?mask():7;
        auto say=[&note](const std::string& row){if(note)note(row);};
        auto resident=[&]{auto porter=readLocalAnchor(s);return porter.entity?findDouble(s,porter.entity,camera,twin):uint64_t(0);};
        auto report=[&](bool result){
            if(m&DressReport){say(describeBody(s,"made",body,artParts));auto r=resident();if(r&&r!=body)say(describeBody(s,"double",r,artParts));
                auto streaming=readStreamingDouble(s,s.base+StreamingDoubleGlobal);
                say("BODY report streaming double "+hex(streaming)+(streaming&&streaming==r?" (the double)\n":" (not the double)\n"));}
            return result;};
        if(m&DressArtPartsInPlace){
            if(!inPlaceOk){say("BODY art-parts swap refused: in-place replacement signature differs\n");return report(false);}
            return report(dressInPlace(s,body,resident(),artParts,[inPlace](uint64_t b,uint64_t res){inPlace(b,res,true);},say,readLocalAnchor(s).entity));
        }
        if(m&(DressModelExact|DressModelThenApply)){
            if(!setModelOk){say("BODY model swap refused: SetModel signature differs\n");return report(false);}
            ModelSwapCalls calls;calls.setModel=[setModel](uint64_t b,uint64_t model){setModel(b,model,0.f);};
            calls.apply=[apply](uint64_t b,uint64_t res){apply(b,res);};
            return report(dressByModel(s,body,resident(),artParts,m,calls,say));
        }
        uint64_t x=0,v=0,owner=0;
        if(!q(s,body+0x180,x)||!x||!q(s,x,v)||v!=artParts||!q(s,x+0x48,owner)||owner!=body)return report(false);
        if(m&8){
            auto res0=resident();
            uint64_t rx=0,rv=0,res=0;
            if(!res0||res0==body||!q(s,res0+0x180,rx)||!q(s,rx,rv)||rv!=artParts||!q(s,rx+0x30,res)||!res)return report(false);
            uint64_t old=x;apply(body,res);
            // Part 20 of the porter's set draws the long line (game_offsets.md); switching it off at this point does not
            // last, and enforcing it while the body is shown corrupted the model's drawing, so it is left alone.
            // Mask bit 2 with 8: switch off the leftover one-part ArtPartsData's part with the toggle worker 22b8fe0
            // (part, enable, entity, entity model, ArtPartsData), the call EnableModelPartFromHash makes.
            if(m&2){
                const std::vector<uint8_t> toggleSig={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x0f,0xb6,0x59,0x2c,0x4d};
                std::vector<uint8_t> at(toggleSig.size());if(!s.read(s.base+0x22b8fe0,at.data(),at.size())||at!=toggleSig)return report(false);
                uint32_t count=0;uint64_t arr=0,part=0,model=0;
                if(!s.read(old+0x100,&count,4)||count!=1||!q(s,old+0x108,arr)||!q(s,arr,part)||!part||!q(s,body+0xb8,model))return report(false);
                using ToggleFn=void(*)(uint64_t,bool,uint64_t,uint64_t,uint64_t);reinterpret_cast<ToggleFn>(s.base+0x22b8fe0)(part,false,body,model,old);
            }
            // The apply adds a second ArtPartsData (from the porter's resource) that builds the model components itself.
            // Mask 9 also removes the body's own one-part ArtPartsData (EntityComponent_ExportedRemove, 23618c0).
            if(m&1){
                const std::vector<uint8_t> removeSig={0x48,0x85,0xc9,0x74,0x0c,0x48,0x8b,0xd1,0x48,0x8b,0x49,0x48,0xe9};
                std::vector<uint8_t> at(removeSig.size());
                if(!s.read(s.base+0x23618c0,at.data(),at.size())||at!=removeSig)return report(false);
                using RemoveFn=void(*)(uint64_t);reinterpret_cast<RemoveFn>(s.base+0x23618c0)(old);
            }
            return report(true);
        }
        for(unsigned i=0;i<3;++i){if(!(m&(1u<<i)))continue;make[i](x);uint64_t c=0;if(!q(s,x+entries[i].slot,c)||!c)return report(false);}
        return report(true);
    };
#endif
}
std::function<void()> nativeContinueLastSave(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native continue API requires Windows");
#else
    const std::vector<uint8_t> sig={0x48,0x83,0x3d,0x10,0x39,0x5a,0x05,0x00,0x75,0x11,0x41,0xb0,0xff,0x48,0x8d,0x0d,0x1c,0x39,0x5a,0x05,0xb2,0x0f,0xe9,0x55,0x20,0xfe,0xff};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"continue code unreadable");
        for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
            ++count;require(section.address+size_t(at-code.begin())==s.base+0x2642a00,"continue function signature moved/duplicated");}
    }
    require(count==1,"continue function signature missing/ambiguous");
    using ContinueFn=void(*)();auto go=reinterpret_cast<ContinueFn>(s.base+0x2642a00);
    return [go]{go();};
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
    const std::array<Entry,9> entries={{
        {0x2361e20,{0x48,0x85,0xc9,0x74,0x2f,0x48,0x8b,0x81,0x88,0,0,0,0x48,0xc1,0xe8,0x08,0x84,0xd2,0x74,0x12,0xa8,0x01,0x75,0x1c,0x48,0x8b,0x01,0x41,0x0f,0xb6,0xd0,0x48,0xff,0xa0,0xa0,0,0,0}},
        {0x2361e90,{0x48,0x85,0xc9,0x0f,0x85,0xa7,0xd7,0xff,0xff,0xc3}},
        {0x2361ea0,{0x48,0x85,0xc9,0x74,0x1c,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xce,0xe0,0xf7,0xff,0x33,0xd2,0x48,0x8b,0xcb,0xe8,0xa4,0xa0,0xff,0xff}},
        // AnimationManager_Exported{Init,Set}{Bool,Float}AnimationVariable
        {0x2397050,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x09,0x8d,0x41,0xff,0x48,0x83,0xc4,0x40,0x5b,0xc3,0x48,0x8d,0x4c,0x24,0x30,0xe8,0x8f}},
        {0x23970b0,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x09,0x8d,0x41,0xff,0x48,0x83,0xc4,0x40,0x5b,0xc3,0x48,0x8d,0x4c,0x24,0x30,0xe8,0x2f}},
        {0x2397220,{0x48,0x85,0xc9,0x74,0x33,0x48,0x63,0xc2,0x48,0x8d,0x14,0x40,0x48,0xc1,0xe2,0x04}},
        {0x23971e0,{0x48,0x85,0xc9,0x74,0x2b,0x48,0x63,0xc2,0x48,0x8d,0x14,0x40,0x48,0xc1,0xe2,0x04}},
        // MorphemeAnimationManager: (re)create its network instance from resource+d8 into manager+a0
        {0x23a2fc0,{0x48,0x89,0x5c,0x24,0x18,0x55,0x48,0x8d,0x6c,0x24,0xc0,0x48,0x81,0xec,0x40,0x01,0,0,0xc5,0xf8,0x29,0xb4,0x24,0x30}},
        // AnimationManager_ExportedTriggerBoolAnimationVariable (manager, handle, seconds): true now, false when it expires.
        {0x2397310,{0x48,0x83,0xec,0x38,0x48,0x85,0xc9,0x74,0x17,0xc5,0xea,0x5f,0x1d,0xef,0xf4,0x82,0x05,0x4c,0x8d,0x44}}}};
    std::array<unsigned,9> counts{};
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
    struct Handles{uint64_t manager=0;int speed=-1,move=-1,moveFloat=-1,moveInterp=-1,crouchRate=-1,basic=-1,isVehicle=-1,vehicleType=-1,vehicleSeat=-1,
        rewind=-1,rewindNoInter=-1,upperReset=-1,turnHeading=-1;bool riding=false;int action[7]={-1,-1,-1,-1,-1,-1,-1};};auto handles=std::make_shared<Handles>();
    using TriggerFn=void(*)(uint64_t,int,float);auto trigger=reinterpret_cast<TriggerFn>(s.base+entries[8].rva);
    // The borrowed instance is kept after release and swapped back in on the next borrow of the same manager,
    // so repeated borrows build one instance per double, not one per borrow.
    struct Borrowed{uint64_t manager=0,own=0,instance=0,def=0;};auto borrowed=std::make_shared<Borrowed>();
    using RecreateFn=void(*)(uint64_t);auto recreate=reinterpret_cast<RecreateFn>(s.base+entries[7].rva);
    const uint64_t managerResource=s.base+0x3d0a7e8,networkDef=s.base+0x3ba8a40,floorEffect=s.base+0x3d7db80;
    require(q(s,managerResource,g)&&g==s.base+0x2398f00&&q(s,networkDef,g)&&g==s.base+0x1a4c150&&q(s,floorEffect,g)&&g==s.base+0x2711a40,"animation vtables differ from pinned image");
    struct Effect{uint64_t child=0;bool visible=false,sleeping=false;};
    struct Effects{uint64_t entity=0;std::vector<Effect> recorded;};auto effects=std::make_shared<Effects>();
    auto managerCache=std::make_shared<std::pair<uint64_t,uint64_t>>(0,0);   // entity, its manager
    auto managerOf=[&s,animator,managerCache](uint64_t e)->uint64_t{
        if(managerCache->first==e&&managerCache->second){uint64_t v=0,owner=0;auto m=managerCache->second;
            if(q(s,m,v)&&v==animator&&q(s,m+0x48,owner)&&owner==e)return m;}
        uint32_t count=0;uint64_t list=0;
        require(s.read(e+0x90,&count,4)&&count<=4096&&q(s,e+0x98,list),"entity components unreadable");
        for(uint32_t i=0;i<count;++i){uint64_t c=0,v=0,owner=0;if(q(s,list+8*i,c)&&q(s,c,v)&&v==animator&&q(s,c+0x48,owner)&&owner==e){*managerCache={e,c};return c;}}
        return 0;
    };
    auto writable=[](uint64_t at,size_t n){MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE
               &&at+n<=reinterpret_cast<uint64_t>(info.BaseAddress)+info.RegionSize;};
    auto check=[&s,twin](uint64_t e){uint64_t v=0;require(e&&q(s,e,v)&&v==twin,"double identity changed");};
    // The double found last time is re-checked through its links (class, parent camera, the camera's class and its
    // parent porter: 4 reads) instead of walking both child lists every tick; the full walk, with its uniqueness
    // check, still runs at least every 2 s and whenever the link check fails.
    struct FoundDouble{uint64_t player=0,found=0,fullAt=0;};auto foundCache=std::make_shared<FoundDouble>();
    auto findCached=[&s,camera,twin,foundCache](uint64_t p)->uint64_t{
        uint64_t now=GetTickCount64();auto& c=*foundCache;
        if(c.player==p&&c.found&&now>=c.fullAt&&now-c.fullAt<2000){
            uint64_t v=0,cam=0,camVt=0,owner=0;
            if(q(s,c.found,v)&&v==twin&&q(s,c.found+ParentOffset,cam)&&cam&&q(s,cam,camVt)&&camVt==camera&&q(s,cam+ParentOffset,owner)&&owner==p)return c.found;
        }
        c={p,findDouble(s,p,camera,twin),now};return c.found;};
    DoubleApi api{[&s]{return readLocalAnchor(s);},findCached,
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
        [&s,check,managerOf,handles,initBool,initFloat,setFloat,setBool,trigger,writable](uint64_t e,double speed,uint32_t flags){
            check(e);auto manager=managerOf(e);
            if(!manager)return;   // no animation manager: position-only body
            if(handles->manager!=manager){*handles={};handles->manager=manager;
                // Controls resolve by name against the manager resource's definition (+0xd8), which is the body's own while
                // it runs the porter's network instance: bind against the instance's definition instead, or a control can
                // land on another node (static, game_offsets.md "Vehicle animation entry").
                uint64_t res=0,resDef=0,instance=0,activeDef=0;bool swapped=false;
                if(q(s,manager+0x30,res)&&q(s,res+0xd8,resDef)&&q(s,manager+0xa0,instance)&&instance&&q(s,instance+0x38,activeDef)
                   &&activeDef&&activeDef!=resDef&&writable(res+0xd8,8)){std::memcpy(reinterpret_cast<void*>(res+0xd8),&activeDef,8);swapped=true;}
                handles->speed=initFloat(manager,"Speed");handles->move=initBool(manager,"DSMove");
                handles->moveFloat=initFloat(manager,"Move_Float");handles->moveInterp=initFloat(manager,"Move_Float_Interp");
                for(unsigned k=0;k<7;++k)handles->action[k]=initBool(manager,ActionNames[k]);
                handles->crouchRate=initFloat(manager,"Crouch_Rate");handles->basic=initBool(manager,"Is_BasicAction");
                handles->isVehicle=initBool(manager,"Is_Vehicle");handles->vehicleType=initFloat(manager,"Vehicle_Type");
                handles->vehicleSeat=initFloat(manager,"Vehicle_Seat");handles->rewind=initBool(manager,"Action_Rewind");
                handles->rewindNoInter=initBool(manager,"Action_Rewind_NoInter");handles->upperReset=initBool(manager,"Is_Force_Upper_Reset");
                handles->turnHeading=initFloat(manager,"Turn_Heading");
                if(swapped)std::memcpy(reinterpret_cast<void*>(res+0xd8),&resDef,8);}
            // Riding: the porter network's own vehicle controls (enum 0x19 / 0x1b, set by the game's mount at 0x2772ee9 and
            // 0x2774469); locomotion stays at rest underneath.
            auto type=vehicleAnimationType((flags&VehicleMask)>>VehicleShift);bool riding=type>=0;
            if(handles->isVehicle>=0)setBool(manager,handles->isVehicle,riding);
            if(riding&&handles->vehicleType>=0)setFloat(manager,handles->vehicleType,type);
            if(riding&&handles->vehicleSeat>=0)setFloat(manager,handles->vehicleSeat,0.f);
            // Entering: the game's mount pulses these right after Is_Vehicle (0x2772ef7, 0x2772f05; the action selector
            // pulses Action_Rewind when its bitset changes, 0x2803fa1). Once per entry, not every tick.
            if(riding&&!handles->riding){for(int h:{handles->rewind,handles->rewindNoInter,handles->upperReset})if(h>=0)trigger(manager,h,0.1f);}
            handles->riding=riding;
            // A seated porter shows Is_Actions and Is_Landed (live 2026-11-08, driving a truck); locomotion at rest.
            if(riding){speed=0;flags=(flags&VehicleMask)|ActionsBit|LandedBit;}
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
    // The rider's steering (Turn_Heading, enum 0x04 in the porter's network; the game writes it from the vehicle's
    // steering at 0x2776680..0x27766c0) while the partner rides.
    api.steer=[check,managerOf,handles,setFloat](uint64_t e,float degrees){
        check(e);auto manager=managerOf(e);
        if(manager&&handles->manager==manager&&handles->turnHeading>=0)setFloat(manager,handles->turnHeading,degrees);};
    return api;
#endif
}
}
