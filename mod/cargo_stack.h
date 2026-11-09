#pragma once
// The partner's carried cargo drawn on the partner body (opt-in with `carry_visual`; game_offsets.md "Visual-only
// cargo stack on the partner body" and "Per-item case models for the partner's cargo"). What the partner carries
// arrives as the carry summary (carry_visual.h, item ids); this draws one case mesh per carried backpack item, the
// item's own case model and appearance where the game has an ordinary case for it (a plain case otherwise), packed in
// two columns on the copy of the backpack the body wears (gear.h), or on the body itself without one. The meshes are
// the game's own cargo case meshes (no cargo record, carrier, slot or manager table is made or touched), with their
// collision turned off, moved every tick and released when the partner puts the cargo down, the body goes or the world
// is replaced.
#include "discovery.h"
#include "puppet.h"
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace mod::stack {
using puppet::Transform;
// One carried item: its id and its record's variant bit (carry_visual.h RecordVariant).
struct Item { uint32_t id=0; bool variant=false; bool operator==(const Item& o) const {return id==o.id&&variant==o.variant;} bool operator<(const Item& o) const {return id<o.id||(id==o.id&&variant<o.variant);} };
// What one item looks like: the case model's loaded object, the item's appearance override (0: none) and its volume
// type (0..3: S, M, L, LL), which sets its height.
struct Case {
    uint64_t model=0,look=0;uint8_t volume=1;
    bool operator==(const Case& o) const {return model==o.model&&look==o.look&&volume==o.volume;}
};
double height(uint8_t volume);   // the case's height in the stack (metres): 0.132, 0.132, 0.264, 0.396
struct Api {
    std::function<std::optional<Case>(uint32_t item,bool variant)> caseFor;   // the item's case, a plain case, or nullopt
    std::function<uint64_t(const Case&,const Transform&)> create;       // a new mesh instance, or 0
    std::function<bool(uint64_t mesh,const Transform&)> place;          // false: not the mesh made (left alone)
    std::function<void(uint64_t mesh)> release;
};
// Cases are packed in `columns` columns `across` apart (centred), each case going on the lowest column and sitting on
// the cases below it along `up`; from the anchor's position plus `offset`; all along the anchor's axes (rows right,
// forward, up), with its rotation.
struct Layout {
    std::array<double,3> offset{0,0,0};          // right, forward, up of the anchor (metres)
    std::array<double,3> up{0,0,1};              // the stacking direction (unit, anchor axes)
    std::array<double,3> across{0.40,0,0};       // per column: one case's width (0.382) and a gap
    unsigned columns=2;
};
// Where each case of `volumes` (bottom first, in order) sits.
std::vector<Transform> place(const Transform& anchor,const Layout&,const std::vector<uint8_t>& volumes);
class Stack {
public:
    static constexpr unsigned MaxCases=12,PerTick=2,MaxFailures=3;
    Stack(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // `items` (bottom first; capped at MaxCases) placed on `anchor`; permitted=false (no body, a game event, the mode
    // off) removes them. A mesh is kept while its item stays at its place in the list; from the first changed item up,
    // the meshes are replaced.
    void tick(bool permitted,const std::vector<Item>& items,const Transform* anchor);
    void forget(const char* why);   // the world was replaced: the meshes went with it; never call the game
    void clear();                   // releases every mesh
    size_t shown() const {return meshes_.size();}
    unsigned failures() const {return failures_;}
    Layout layout;
private:
    struct Mesh{uint64_t mesh=0;Item item;uint8_t volume=1;};
    Api api_;std::function<void(const std::string&)> log_;std::vector<Mesh> meshes_;unsigned failures_=0;
    std::map<Item,std::optional<Case>> cases_;   // per item, for this world
};
// Signature-checked native calls (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
}
