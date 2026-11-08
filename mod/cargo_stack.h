#pragma once
// The partner's carried cargo drawn on the partner body (opt-in with `carry_visual`; game_offsets.md "Visual-only
// cargo stack on the partner body"). What the partner carries arrives as the carry summary (carry_visual.h); this
// draws one plain case mesh per carried backpack item, stacked on the copy of the backpack the body wears (gear.h),
// or on the body itself without one. The meshes are the game's own cargo case meshes made from the cargo config's
// case model (no cargo record, carrier, slot or manager table is made or touched), with their collision turned off,
// moved every tick and released when the partner puts the cargo down, the body goes or the world is replaced.
#include "discovery.h"
#include "puppet.h"
#include <array>
#include <functional>
#include <string>
#include <vector>
namespace mod::stack {
using puppet::Transform;
struct Api {
    std::function<uint64_t()> model;                                   // the case model's loaded object, or 0
    std::function<uint64_t(uint64_t model,const Transform&)> create;   // a new mesh instance, or 0
    std::function<bool(uint64_t mesh,const Transform&)> place;         // false: not the mesh made (left alone)
    std::function<void(uint64_t mesh)> release;
};
// Where case k sits: `columns` cases side by side per row (`across` apart, centred), rows `step` apart, from the
// anchor's position plus `offset`; all along the anchor's axes (rows right, forward, up), with its rotation.
struct Layout {
    std::array<double,3> offset{0,0,0};          // right, forward, up of the anchor (metres)
    std::array<double,3> step{0,0,0.132};        // per row: one medium case's height (0.132) up
    std::array<double,3> across{0.40,0,0};       // per column: one case's width (0.382) and a gap
    unsigned columns=2;
};
Transform place(const Transform& anchor,const Layout&,unsigned k);
class Stack {
public:
    static constexpr unsigned MaxCases=12,PerTick=2,MaxFailures=3;
    Stack(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // wanted cases (0 = none) placed on `anchor`; permitted=false (no body, a game event, the mode off) removes them.
    void tick(bool permitted,unsigned wanted,const Transform* anchor);
    void forget(const char* why);   // the world was replaced: the meshes went with it; never call the game
    void clear();                   // releases every mesh
    size_t shown() const {return meshes_.size();}
    unsigned failures() const {return failures_;}
    Layout layout;
private:
    Api api_;std::function<void(const std::string&)> log_;std::vector<uint64_t> meshes_;unsigned failures_=0;uint64_t model_=0;
};
// Signature-checked native calls (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
}
