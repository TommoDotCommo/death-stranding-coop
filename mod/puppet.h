#pragma once
#include "discovery.h"
#include "pose.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
namespace mod::puppet {
// Engine transform uses the first 60 bytes. Natural alignment leaves four tail
// padding bytes; no engine field is placed there. Creator rotation has row stride16.
struct Transform {std::array<double,3> position{};std::array<float,9> rotation{};};
struct Rotation {float rows[3][4]{};};
struct Tagged {uint64_t tag=0,pointer=0;};
static_assert(offsetof(Transform,rotation)==24&&sizeof(Transform)==64);
static_assert(sizeof(Rotation)==48&&sizeof(Tagged)==16);
using GetResourceFn=uint64_t(*)(uint64_t);
using CreateFn=uint64_t(*)(uint64_t,const double*,const Rotation*,const char*,bool,uint64_t,uint64_t,const Tagged*);
using PlaceFn=void(*)(uint64_t,const Transform*);
using RemoveFn=void(*)(uint64_t,bool);
uint64_t invokeCreate(CreateFn,uint64_t,const Transform&);
void invokeRemove(RemoveFn,uint64_t);

struct Anchor {uint64_t entity=0;Pose pose;};
Transform transform(const Pose&);
Rotation padded(const Transform&);
struct Api {
    std::function<Anchor()> local;
    std::function<Pose(uint64_t)> read;
    std::function<uint64_t(uint64_t)> resource;
    std::function<uint64_t(uint64_t,const Transform&)> create;
    std::function<void(uint64_t,const Transform&)> place;
    std::function<void(uint64_t)> remove;
    // Missing approval fails closed. Native Sam cloning is quarantined.
    std::function<bool(uint64_t)> canCreate;
};
// Production factory verifies unique, pinned function prefixes. It never calls
// the functions during init. Calls belong exclusively to the validated callback.
Anchor readLocalAnchor(discovery::Sampler&);
Pose readEntityPose(discovery::Sampler&,uint64_t);
Api nativeApi(discovery::Sampler&);
class Probe {
    Api api_;std::function<void(const std::string&)> record_;
    bool armed_=false;unsigned stage_=0;uint64_t at_=0,local_=0,actor_=0,resource_=0;
    Transform target_{};Anchor localAfter_{};
    void verifyLocal(const Anchor& before);
    void record(const char* event,const Pose& readback);
public:
    explicit Probe(Api api,std::function<void(const std::string&)> record):api_(std::move(api)),record_(std::move(record)) {}
    void arm(){
#ifndef MUTANT_PUPPET_ONCE
        if(armed_)return;
#endif
        armed_=true;stage_=1;
    }
    void tick(uint64_t now,bool permitted);
    bool armed() const {return armed_;}
    const char* stage() const;
    uint64_t actor() const {return actor_;}
};
}
