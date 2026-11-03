#pragma once
// Experiment: the partner's own body, created through the engine (game_offsets.md, "The engine's common entity
// creator"). The double is the game's private-room Sam and the game takes it back; a second Sam made from the
// double's own resource (double+58) would belong to the mod alone. One command creates one request; the probe then
// watches the request object for the entity it produces and logs what it finds. Nothing here drives the body yet.
#include "discovery.h"
#include "puppet.h"
#include <functional>
#include <string>
#include <vector>
namespace mod::body {
using puppet::Transform;
struct Api {
    // 0x2380920(Ref* out, const WorldTransform*, Resource*, owner (may be null), const GGUUID*): returns *out, the
    // creation request (refcounted; one reference is ours).
    std::function<uint64_t(const Transform&,uint64_t resource,const std::array<uint64_t,2>& id)> create;
    std::function<void(uint64_t entity)> remove;   // Entity_ExportedRemove
    std::function<bool(uint64_t,void*,size_t)> read;
};
constexpr uint64_t ResourceOffset=0x58,RequestSize=0x1f0;
// The request's 8-byte slots that point to an object whose first word is `vtable` (the entity class), excluding
// `exclude` (the double itself). Pure: `words` is the request's memory, `vtableOf` reads an object's first word.
std::vector<uint64_t> entitiesIn(const std::vector<uint64_t>& words,uint64_t vtable,uint64_t exclude,
                                 const std::function<uint64_t(uint64_t)>& vtableOf);
class Probe {
public:
    Probe(Api api,uint64_t entityVtable,std::function<void(const std::string&)> log):api_(std::move(api)),vtable_(entityVtable),log_(std::move(log)){}
    // One request: the double's resource, at `at`. Refused (logged) while a request is being watched, or without a
    // resource. id is a fresh GGUUID.
    bool create(uint64_t twin,const Transform& at,const std::array<uint64_t,2>& id);
    // Watch the request for WatchTicks ticks: log its non-zero words at a few ticks and any new entity it holds.
    void tick();
    bool remove();   // removes the entity found, if any
    uint64_t entity() const {return entity_;}
    bool watching() const {return request_&&ticks_<WatchTicks;}
    static constexpr unsigned WatchTicks=100;
private:
    Api api_;uint64_t vtable_;std::function<void(const std::string&)> log_;
    uint64_t request_=0,twin_=0,entity_=0;unsigned ticks_=0;
};
// Signature-checked native calls (Windows); throws elsewhere or when the pinned image differs.
Api nativeApi(discovery::Sampler&);
}
