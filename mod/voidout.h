#pragma once
// Shared voidouts (voidout.cpp, with share_enemies). When a porter is consumed by a boss BT the game leaves a crater in
// his world: a record in the catcher manager's crater list (image+0x7bee060, +0x280) that the game turns into a crater
// entity and a terrain rebake once its countdown has run (game_offsets.md, "Voidout craters"). The mod sends each new
// crater of its game, and adds the partner's to its own list the way the game queues one, already activated: the
// crater appears, without the blast's effects on things standing there, and is saved with the world like any other.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mod::voidout {
constexpr uint8_t WireType=17;
struct Crater { uint32_t id=0; std::array<double,3> position{}; float radius=0,yaw=0; uint32_t area=0; bool disabled=false,active=false; };
struct Table { bool readable=false; uint32_t counter=0; std::vector<Crater> craters; };
// The active list and the id counter (C+0x2a8). Not readable: the manager is not there.
Table read(const discovery::Sampler&,uint64_t base);
// One crater on the wire: item = radius in cm, ref = area (16 bits) | disabled << 16, position = game axes, yaw.
ds::CargoEvent encode(const Crater&);
std::optional<Crater> decode(const ds::CargoEvent&);
class Sync {
public:
    // Craters that came into this game since the last look (ids above the counter seen then), except those the mod
    // put there for the partner. A counter that went down is a load: learned again, nothing sent.
    std::vector<Crater> observe(const Table&);
    // The partner's crater: true when none stands within 5 m of it here.
    static bool wanted(const Table&,const Crater&);
    void applied(uint32_t id){mine_.insert(id);}
    void forget(){primed_=false;}
private:
    bool primed_=false;uint32_t counter_=0;std::set<uint32_t> mine_;
};
// The area this porter is in (the game's 0x262a8c0); craters are kept per area. 0xffff: none.
std::function<uint32_t()> nativeArea(discovery::Sampler&);
// Adds a crater to this game's queue (Windows only): returns its id, 0 when refused.
using Writable=std::function<bool(uint64_t,size_t)>;
using Grow=std::function<uint64_t(uint64_t,uint64_t,int32_t)>;
using Log=std::function<void(const std::string&)>;
// The same queue writer on host: only the OS protection query and native allocator are injected.
std::function<uint32_t(const Crater&)> craterAdder(discovery::Sampler&,Writable,Grow,Log);
std::function<uint32_t(const Crater&)> nativeAdd(discovery::Sampler&,Log);
}
