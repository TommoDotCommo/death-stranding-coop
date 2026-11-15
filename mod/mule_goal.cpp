#include "mule_goal.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::mulegoal {
namespace {
bool pointerLike(uint64_t p){return p>=0x10000&&p<0x0000800000000000ULL&&!(p&7);}
std::optional<uint64_t> q(const discovery::Sampler& s,uint64_t at){uint64_t v=0;if(!s.read(at,&v,8))return std::nullopt;return v;}
std::optional<Vec3> vec(const discovery::Sampler& s,uint64_t at){Vec3 v{};if(!s.read(at,v.data(),24))return std::nullopt;return v;}
bool sane(const Vec3& v){for(auto x:v)if(!std::isfinite(x)||std::fabs(x)>MaxPositionM)return false;return true;}
double distance(const Vec3& a,const Vec3& b){double d=0;for(size_t k=0;k<3;++k)d+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(d);}
}
Chain read(const discovery::Sampler& s,uint64_t base,uint64_t actor){
    Chain c;c.actor=actor;
    auto ref=q(s,actor+ActorEntity);if(!ref)return c;
    if(!*ref){c.entity=0;return c;}
    if(!pointerLike(*ref-EntityBias))return c;
    c.entity=*ref-EntityBias;
    uint32_t n=0;auto array=q(s,*c.entity+ComponentArray);
    if(!s.read(*c.entity+ComponentCount,&n,4)||!array||!pointerLike(*array))return c;
    c.component=0;
    for(uint32_t i=0;i<n&&i<MaxComponents;++i){
        auto p=q(s,*array+8*uint64_t(i));if(!p||!pointerLike(*p))continue;
        auto v=q(s,*p);if(!v)continue;
#ifndef MUTANT_MULEGOAL_COMPONENT
        if(*v!=base+DsComponentVtable&&*v!=base+BaseComponentVtable)continue;
#endif
        c.component=*p;c.componentVtable=*v;break;
    }
    if(!*c.component)return c;
    c.ai=q(s,*c.component+ComponentAi);c.aiAgain=q(s,*c.component+ComponentAiAgain);
    if(!c.ai||!pointerLike(*c.ai))return c;
    uint64_t ai=*c.ai;
    c.aiVtable=q(s,ai);
    uint8_t state=0;if(s.read(ai+AiState,&state,1))c.state=state;
    // The body-goal table, walked entry by entry: the game's own lookup hashes; a scan needs no hash.
    auto entries=q(s,ai+AiGoals);uint32_t capacity=0;
    if(entries&&s.read(ai+AiGoals+0xc,&capacity,4)){
        bool bounded=capacity<=MaxGoals;
#ifdef MUTANT_MULEGOAL_CAPACITY
        bounded=true;
#endif
        if(!*entries||!capacity)c.goal=0;
        else if(bounded&&pointerLike(*entries)){
            std::vector<uint8_t> table(size_t(capacity)*0x20);
            if(s.readOnce(*entries,table.data(),table.size())){
                c.goal=0;
                for(uint32_t i=0;i<capacity;++i){
                    uint32_t hash=0;uint64_t key=0,value=0;
                    std::memcpy(&hash,table.data()+size_t(i)*0x20,4);std::memcpy(&key,table.data()+size_t(i)*0x20+8,8);std::memcpy(&value,table.data()+size_t(i)*0x20+0x10,8);
#ifndef MUTANT_MULEGOAL_KEY
                    if(!hash||key!=base+WalkGoalKey)continue;
#else
                    if(!hash)continue;
#endif
                    if(pointerLike(value))c.goal=value;
                    break;
                }
            }
        }
    }
    if(c.goal&&*c.goal){c.destination=vec(s,*c.goal+GoalDestination);c.destinationCopy=vec(s,*c.goal+GoalDestinationCopy);}
    c.d=q(s,ai+AiRequests);
    if(!c.d||!pointerLike(*c.d))return c;
    uint64_t d=*c.d;
    c.dVtable=q(s,d);
    int32_t index=0;uint8_t fixed=0;
    if(!s.read(d+RequestIndex,&index,4)||!s.read(d+RequestFixed,&fixed,1))return c;
    if(fixed)index=2;
    c.index=index;
#ifndef MUTANT_MULEGOAL_INDEX
    if(index<0||uint32_t(index)>=RequestCount)return c;
#endif
    c.request=d+RequestFirst+uint64_t(int64_t(index))*RequestSize;
    c.forced=vec(s,*c.request+RequestForced);
    uint32_t flags=0;if(s.read(*c.request+RequestFlags,&flags,4))c.flags=flags;
    return c;
}
std::string describe(const Chain& c,uint64_t base){
    std::ostringstream o;
    auto hex=[&](const char* name,const std::optional<uint64_t>& v){o<<" "<<name<<" ";if(v)o<<std::hex<<*v<<std::dec;else o<<"unreadable";};
    auto offset=[&](const char* name,const std::optional<uint64_t>& v){o<<" "<<name<<" ";if(!v)o<<"unreadable";else if(*v>=base&&*v-base<0x10000000)o<<"image+"<<std::hex<<*v-base<<std::dec;else o<<std::hex<<*v<<std::dec;};
    auto point=[&](const char* name,const std::optional<Vec3>& v){o<<" "<<name<<" ";if(v)o<<(*v)[0]<<","<<(*v)[1]<<","<<(*v)[2];else o<<"unreadable";};
    hex("entity",c.entity);hex("component",c.component);offset("componentvt",c.componentVtable);
    hex("ai50",c.ai);hex("ai80",c.aiAgain);o<<" agree "<<(c.ai&&c.aiAgain?(c.agree()?"yes":"no"):"unreadable");
    offset("aivt",c.aiVtable);
    o<<" state ";if(c.state)o<<unsigned(*c.state);else o<<"unreadable";
    o<<" walk "<<(!c.goal?"unreadable":*c.goal?"yes":"no");
    hex("goal",c.goal);point("dest9e0",c.destination);point("dest850",c.destinationCopy);
    hex("d",c.d);offset("dvt",c.dVtable);
    o<<" index ";if(c.index)o<<*c.index;else o<<"unreadable";
    hex("request",c.request);point("forced",c.forced);
    o<<" flags32 ";if(c.flags)o<<std::hex<<*c.flags<<std::dec;else o<<"unreadable";
    o<<" flags8 ";if(c.flags)o<<std::hex<<(*c.flags&0xff)<<std::dec;else o<<"unreadable";
    return o.str();
}
std::optional<Ask> ask(uint32_t argument){
    Ask a;a.repeat=argument>=RepeatBase;a.cm=a.repeat?argument-RepeatBase:argument;
#ifndef MUTANT_MULEGOAL_OFFSET
    if(a.cm>MaxOffsetCm)return std::nullopt;
#endif
    return a;
}
std::string refusal(const discovery::Sampler& s,uint64_t base,const Chain& c,const Vec3& mule,const Vec3& porter,const Writable& writable,bool first){
    if(!sane(mule)||!sane(porter))return "position not finite";
#ifndef MUTANT_MULEGOAL_NEAR
    if(distance(mule,porter)<MinPorterM)return "MULE nearer than 6 m to the porter";
#endif
    if(!c.entity||!*c.entity)return "no body";
    if(!c.component||!*c.component)return "no AI component";
#ifndef MUTANT_MULEGOAL_DSCOMPONENT
    if(!c.componentVtable||*c.componentVtable!=base+DsComponentVtable)return "AI component is not a DSAIIndividualComponent";
#endif
#ifndef MUTANT_MULEGOAL_AGREE
    if(!c.agree())return "the two AI pointers differ";
#endif
#ifndef MUTANT_MULEGOAL_AI_VTABLE
    if(!c.aiVtable||*c.aiVtable!=base+AiVtable)return "AI is not a DSAIIndividual";
#endif
#ifndef MUTANT_MULEGOAL_RUNNING
    // Live: the byte is 0 while the MULE's AI runs and 2 while the game holds it still (no player near): a forced
    // destination written then is never read, and its bit stays set.
    if(!c.state||*c.state!=AiRunning)return "AI not running";
#endif
    if(!c.goal||!*c.goal||!c.destination||!c.destinationCopy)return "no walk goal";
#ifndef MUTANT_MULEGOAL_WALKING
    if(!sane(*c.destination)||distance(*c.destination,*c.destinationCopy)>0.001||(first&&distance(*c.destination,mule)<MinWalkM))return "not walking";
#else
    (void)first;
#endif
    if(!c.d||!c.dVtable||!c.index||!c.request||!c.forced||!c.flags)return "request unreadable";
#ifndef MUTANT_MULEGOAL_D_VTABLE
    if(!s.region(*c.dVtable,8,true))return "request owner has no vtable in the image";
#else
    (void)s;
#endif
    if(*c.index<0||uint32_t(*c.index)>=RequestCount||*c.request!=*c.d+RequestFirst+uint64_t(*c.index)*RequestSize)return "request index out of range";
#ifndef MUTANT_MULEGOAL_WRITABLE
    if(!writable||!writable(*c.d,size_t(*c.request+RequestSize-*c.d)))return "request not in plain writable memory of its owner";
#else
    (void)writable;
#endif
    return "";
}
Vec3 east(const Vec3& destination,uint32_t cm){Vec3 v=destination;v[0]+=double(cm)/100.;return v;}
std::string force(const discovery::Sampler& s,uint64_t base,const Chain& was,const Vec3& mule,const Vec3& porter,const Vec3& destination,bool first,const Writable& writable,const Poke& poke){
    if(!poke)return "no writer";
    if(!sane(destination))return "destination not finite";
    Chain now=read(s,base,was.actor);
#ifndef MUTANT_MULEGOAL_SAME
    if(now.entity!=was.entity||now.component!=was.component||now.ai!=was.ai||now.goal!=was.goal||now.d!=was.d)return "the MULE's objects changed";
#endif
    auto why=refusal(s,base,now,mule,porter,writable,first);if(!why.empty())return why;
#ifndef MUTANT_MULEGOAL_REACH
    if(distance(destination,mule)>MaxReachM)return "destination over 200 m from the MULE";
#endif
    poke(*now.request+RequestForced,destination.data(),24);
    uint8_t flags=uint8_t(*now.flags&0xff)|ForcedBit;
    poke(*now.request+RequestFlags,&flags,1);
    return "";
}
Writable nativeWritable(){
#ifdef _WIN32
    return [](uint64_t at,size_t n){MEMORY_BASIC_INFORMATION info{};
        return at&&at<0x800000000000ULL&&n&&n<=0x800000000000ULL-at
            &&VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)==sizeof info&&info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE
            &&n<=info.RegionSize&&at-reinterpret_cast<uint64_t>(info.BaseAddress)<=info.RegionSize-n;};
#else
    return [](uint64_t,size_t){return false;};
#endif
}
Poke nativePoke(){
#ifdef _WIN32
    return [](uint64_t at,const void* from,size_t n){std::memcpy(reinterpret_cast<void*>(at),from,n);};
#else
    return [](uint64_t,const void*,size_t){};
#endif
}
}
