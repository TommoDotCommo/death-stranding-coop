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
#include <memory>
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
    // placed: when given (one per item, relative to the anchor: carry_place.h), each case goes exactly there instead
    // of into the layout's columns, and up to MaxPlaced cases are drawn (hips and arms too).
    // budget (optional): how many more meshes the game may be asked for now; counted down for every one asked for
    // (several stacks share one: Stacks, below).
    void tick(bool permitted,const std::vector<Item>& items,const Transform* anchor,const std::vector<Transform>* placed=nullptr,unsigned* budget=nullptr);
    static constexpr unsigned MaxPlaced=24;
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
// Several stacks, one per body, by key (the mod uses 0 for the partner and a player's id for another player). They share
// one Api and one budget: the game is asked for at most PerTick new meshes a game-thread tick over all bodies, in the
// order listed (the caller lists the partner first), so the partner alone gets what a single Stack gave him. A key that
// is no longer listed (the player left, hides, is no longer among the nearest, put everything down) has its meshes
// released and its stack dropped; a listed key that is not permitted has them released and keeps its stack (and with it
// the count of refusals, so a stack that gave up stays given up). How many keys are listed is the caller's limit.
struct Load {
    uint64_t key=0;bool permitted=false;Transform anchor;std::vector<Item> items;
    std::optional<std::vector<Transform>> placed;   // carry_place.h: one per item, relative to the anchor
};
class Stacks {
public:
    static constexpr unsigned PerTick=Stack::PerTick;
    Stacks(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    void tick(const std::vector<Load>&);
    void forget(const char* why);    // the world was replaced: every stack forgets, none calls the game
    void clear();                    // every mesh released, every stack dropped
    Stack* of(uint64_t key){auto it=stacks_.find(key);return it==stacks_.end()?nullptr:it->second.get();}
    size_t size() const {return stacks_.size();}
    size_t shown() const {size_t n=0;for(auto& [key,s]:stacks_)n+=s->shown();return n;}   // meshes that exist, all bodies
    size_t shown(uint64_t key) const {auto it=stacks_.find(key);return it==stacks_.end()?0:it->second->shown();}
    Layout layout;                   // given to every stack on every tick
private:
    Api api_;std::function<void(const std::string&)> log_;std::map<uint64_t,std::unique_ptr<Stack>> stacks_;
};
// Signature-checked native calls (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
}
