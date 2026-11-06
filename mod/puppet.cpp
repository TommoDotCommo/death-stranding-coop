#include "puppet.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace mod::puppet {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool valid(const Pose& p){if(!p.available||!p.coherent)return false;for(auto x:p.position)if(!std::isfinite(x))return false;for(auto x:p.rotation)if(!std::isfinite(x))return false;return true;}
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(unsigned i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
[[maybe_unused]] uint64_t pointer(const discovery::Sampler& s,uint64_t address){uint64_t p=0;require(s.read(address,&p,8)&&p&&p<0x800000000000ULL,"puppet pointer unreadable/null");return p;}
Anchor local(discovery::Sampler& s){
    auto managerSlot=s.signatures.at(0).targets.at("player_manager_global");
    uint64_t manager=0;if(!s.read(managerSlot,&manager,8)||!manager)return {};
    auto selected=s.selectPlayer(manager);if(!selected.player)return {};
    uint64_t entity=0;if(!s.read(selected.player+0x70,&entity,8)||!entity)return {};
    // The entity is reported whenever the pointer chain is coherent, even if this pose read is not: callers must tell
    // "porter gone" (entity 0 or changed) from "pose unreadable this tick" (entity kept, pose not valid).
    Anchor a;a.pose=PoseReader(s).read();a.entity=entity;if(!valid(a.pose)){a.pose.available=a.pose.coherent=false;}
    uint64_t manager2=0,player2=0,entity2=0;
    if(!s.read(managerSlot,&manager2,8)||manager2!=manager||!s.read(selected.slot,&player2,8)||player2!=selected.player||!s.read(selected.player+0x70,&entity2,8)||entity2!=entity)return {};
    return a;
}
Pose read(discovery::Sampler& s,uint64_t entity){
    Pose p;if(!entity||entity>=0x800000000000ULL)return p;
    if(!s.read(entity+0xc8,p.position.data(),24)||!s.read(entity+0xe0,p.rotation.data(),36))return p;
    p.available=p.coherent=true;if(!valid(p))p.available=p.coherent=false;return p;
}
}
Anchor readLocalAnchor(discovery::Sampler& s){return local(s);}
Pose readEntityPose(discovery::Sampler& s,uint64_t e){return read(s,e);}
Transform transform(const Pose& pose){require(valid(pose),"puppet source pose unavailable");return {pose.position,pose.rotation};}
Rotation padded(const Transform& t){Rotation r;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)r.rows[i][j]=t.rotation[3*i+j];return r;}
uint64_t invokeCreate(CreateFn create,uint64_t resource,const Transform& t){
    auto rotation=padded(t);const Tagged tag{};
#ifdef MUTANT_PUPPET_ARGUMENTS
    const char* name=nullptr;
#else
    const char* name="coop_puppet_probe";
#endif
    return create(resource,t.position.data(),&rotation,name,true,0,0,&tag);
}
void invokeRemove(RemoveFn remove,uint64_t actor){
#ifdef MUTANT_PUPPET_REMOVE_MODE
    remove(actor,true);
#else
    remove(actor,false); // DL=0 queues the engine's reference-owning removal task.
#endif
}
const char* Probe::stage() const {return stage_==0?"disarmed":stage_==1?"waiting":stage_==2?"created":stage_==3?"moved":stage_==4?"remove_requested":stage_==6?"resource_rejected":"failed";}
void Probe::verifyLocal(const Anchor& before){
    auto after=api_.local();localAfter_=after;
#ifndef MUTANT_PUPPET_LOCAL
    require(after.entity==before.entity&&valid(after.pose)&&distance(before.pose.position,after.pose.position)<.10,"puppet altered local player or source became incoherent");
#endif
}
void Probe::record(const char* event,const Pose& pose){
    std::ostringstream o;o<<std::setprecision(17)<<"PUPPET {\"event\":\""<<event<<"\",\"actor\":\"0x"<<std::hex<<actor_<<"\",\"local_entity\":\"0x"<<local_<<"\",\"resource\":\"0x"<<resource_<<std::dec<<"\",\"requested_position\":[";
    for(unsigned i=0;i<3;++i){o<<(i?",":"")<<target_.position[i];}
    o<<"],\"readback_available\":"<<(valid(pose)?"true":"false")<<",\"readback_position\":[";
    for(unsigned i=0;i<3;++i){o<<(i?",":"")<<pose.position[i];}
    o<<"],\"local_after_entity\":\"0x"<<std::hex<<localAfter_.entity<<std::dec<<"\",\"local_after_available\":"<<(valid(localAfter_.pose)?"true":"false")<<",\"local_after_position\":[";
    for(unsigned i=0;i<3;++i){o<<(i?",":"")<<localAfter_.pose.position[i];}
    o<<"]}\n";record_(o.str());
}
void Probe::tick(uint64_t now,bool permitted){
#ifndef MUTANT_PUPPET_PERMISSION
    if(!permitted)return;
#endif
    if(!armed_||stage_>=4)return;
    try {
        auto source=api_.local();
#ifndef MUTANT_PUPPET_WORLD
        if(!source.entity||!valid(source.pose))return;
#endif
        if(stage_==1){
            local_=source.entity;target_=transform(source.pose);
            // Three metres ahead of the sampled porter, retaining the game Z up.
            double length=std::hypot(target_.rotation[3],target_.rotation[4]);require(length>.5,"puppet forward vector invalid");
            target_.position[0]+=3*target_.rotation[3]/length;target_.position[1]+=3*target_.rotation[4]/length;
            record("resource_requested",{});resource_=api_.resource(local_);require(resource_!=0,"puppet resource unavailable");
#ifndef MUTANT_PUPPET_RESOURCE
            if(!api_.canCreate||!api_.canCreate(resource_)){
                stage_=6;record("resource_rejected",{});return;
            }
#endif
            record("create_requested",{});
            actor_=api_.create(resource_,target_);
#ifndef MUTANT_PUPPET_ALIAS
            require(actor_&&actor_!=local_,"puppet creator returned null or local player");
#endif
            auto observed=api_.read(actor_);
#ifndef MUTANT_PUPPET_READBACK
            require(valid(observed)&&distance(observed.position,target_.position)<.5,"puppet creation pose readback failed");
#endif
            verifyLocal(source);stage_=2;at_=now;record("created",observed);return;
        }
#ifndef MUTANT_PUPPET_LIFETIME
        if(source.entity!=local_){stage_=5;record("world_changed_no_further_calls",{});return;}
#endif
#ifndef MOD_PUPPET_STEP_MS
#define MOD_PUPPET_STEP_MS 1000
#endif
        if(now<at_||now-at_<MOD_PUPPET_STEP_MS)return;
        auto observed=api_.read(actor_);require(valid(observed),"puppet entity no longer readable");
        if(stage_==2){
            target_.position[0]+=target_.rotation[0];target_.position[1]+=target_.rotation[1];
            api_.place(actor_,target_);observed=api_.read(actor_);
#ifndef MUTANT_PUPPET_READBACK
            require(valid(observed)&&distance(observed.position,target_.position)<.05,"puppet transform readback failed");
#endif
            verifyLocal(source);stage_=3;at_=now;record("moved",observed);
        } else if(stage_==3){api_.remove(actor_);verifyLocal(source);stage_=4;record("remove_requested",observed);}
    }catch(...){stage_=5;record("failed",{});throw;}
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native puppet API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    std::array<Entry,4> entries={{
        {0x2362ba0,{0x48,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3,0x48,0x83,0xc1,0x58,0xe9,0xdf,0xbd,0xa7,0xff}},
        {0x23806a0,{0x48,0x83,0xec,0x58,0x48,0x8b,0x84,0x24,0x98,0,0,0,0x4c,0x8b,0x94,0x24,0x90,0,0,0,0x4c,0x8b,0x9c,0x24,0x88,0,0,0,0xc5,0xf8,0x10,0}},
        {0x2361ea0,{0x48,0x85,0xc9,0x74,0x1c,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xce,0xe0,0xf7,0xff,0x33,0xd2,0x48,0x8b,0xcb,0xe8,0xa4,0xa0,0xff,0xff}},
        {0x2361c20,{0xe9,0x7b,0xae,0xfe,0xff}}}};
    std::array<unsigned,4> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"puppet code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"puppet code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
            ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"puppet function signature moved/duplicated");++at;
        }}
    }
    for(auto n:counts)require(n==1,"puppet function signature missing/ambiguous");
    auto get=reinterpret_cast<GetResourceFn>(s.base+entries[0].rva);auto create=reinterpret_cast<CreateFn>(s.base+entries[1].rva);
    auto place=reinterpret_cast<PlaceFn>(s.base+entries[2].rva);auto remove=reinterpret_cast<RemoveFn>(s.base+entries[3].rva);
    struct Identity{uint64_t actor=0,vtable=0,resourceRef=0;};auto identity=std::make_shared<Identity>();
    auto check=[&s,identity](uint64_t e){
        require(e==identity->actor&&e&&pointer(s,e)==identity->vtable&&pointer(s,e+0x58)==identity->resourceRef,"puppet actor identity changed");
    };
    return {[&s]{return local(s);},[&s,check](uint64_t e){check(e);return read(s,e);},
        [get](uint64_t e){return get(e);},
        [&s,identity,create](uint64_t resource,const Transform& t){
            auto vt=pointer(s,resource);require(s.region(vt+0x80,8,true),"puppet resource vtable outside image");
            auto factory=pointer(s,vt+0x80);bool code=false;for(auto section:s.sections)code|=section.executable&&factory>=section.address&&factory-section.address<section.size;
            require(code,"puppet resource factory outside executable image");
            auto actor=invokeCreate(create,resource,t);
            if(actor){identity->actor=actor;identity->vtable=pointer(s,actor);identity->resourceRef=pointer(s,actor+0x58);require(s.region(identity->vtable,8,true),"puppet actor vtable outside image");}
            return actor;
        },[place,check](uint64_t e,const Transform& t){check(e);place(e,&t);},[remove,check](uint64_t e){check(e);invokeRemove(remove,e);},
        // Local Sam resource lacks DSPlayerController during generic activation.
        // No config override: a future backend needs independently reviewed context.
        [](uint64_t){return false;}};
#endif
}
}
