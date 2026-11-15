#pragma once
// Shared rewards (rewards.cpp; opt-in share_rewards, needs share_orders; game_offsets.md "Delivery rewards").
// A facility's connection level (the stars of a knot or a prepper) is a points total and a level the game keeps per
// facility. Each mod reads them once a second; when a facility's points rise in its game it tells the partner the new
// total, and the partner's game is topped up to that total with the game's own "add connection points" routine (the
// one a delivery's reward calls), which also runs the level-ups. The level it gives is the number of the facility's
// thresholds the points have reached, never above the facility's level cap: a fact of the game's fact database
// that the story changes. Points above a capped level are kept, and the routine catches the level up at the first
// add after the cap rises. Totals travel, never amounts: a delivery both games rewarded is not counted twice, and
// nothing is ever lowered. The porter's likes and grade totals are read for the log only.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
namespace mod::rewards {
constexpr uint8_t WireType=23;                       // the ordered event stream's type for a facility's total
constexpr int32_t MaxPoints=100000000,MaxLevel=99;   // more than this is not a connection total
constexpr uint64_t ImageSpan=0x10000000;             // a resource's vtable lies inside the image
constexpr size_t MostAtOnce=3;                       // more facilities rising in one look is a loaded save
// One facility. floor and cap are the game's own clamps for its points (resource +0x20; level-5 threshold + 94 steps).
// thresholds and step are the resource's level table (+0x28..+0x38, +0x3c); highest is the game's level cap for it
// right now, filled by readHighest only (Unknown until then, or when the fact database cannot be read).
constexpr int32_t Unknown=-1;
constexpr uint32_t MostFacts=1u<<20;                 // a fact table with more slots than this is not one
struct Facility { uint32_t hash=0; int32_t points=0,level=0,floor=0,cap=0; uint64_t resource=0,record=0;
                  std::array<int32_t,5> thresholds{}; int32_t step=0,highest=Unknown; };
// Every facility of the residents system (image+0x7bfd948: the resources at +0x10, the records at +0x18), sorted by
// name hash; empty when unreadable. A record counts only when its resource is one of the system's own list, carries
// the record's key as its name hash and the list's common vtable, and its numbers are in range.
std::vector<Facility> readFacilities(const discovery::Sampler&,uint64_t base);
// The level the game gives `points` at that facility when `highest` is its level cap (the game's 0x2f23510): the
// thresholds reached in a row, counted up to the cap. Level n needs thresholds[n-1] for n = 1..5 and
// thresholds[4] + (n-5)*step above.
int32_t levelOf(const Facility&,int32_t points,int32_t highest);
// Fills every facility's `highest` the way the game's lookup (0x2f2cd60) finds it, read-only: the resource's
// LevelCap fact (+0x58; none: 99), its value in the fact database (image+0x5794720, the context named by the key at
// image+0x7bfd980, integer values keyed by the fact's whole UUID) or else the fact's default (+0x30), held to
// 0..99. Read without the database's lock: good for a log line, not for a decision.
void readHighest(const discovery::Sampler&,uint64_t base,std::vector<Facility>&);
// The porter's totals (read-only): likes from the world and from other players (likes state +0x18, +0x20) and the
// five grade categories' experience and level (image+0x7bfd960; kept masked by the game, unmasked here).
struct Porter { bool readable=false; int64_t likes=0,likesOnline=0; std::array<uint64_t,5> experience{}; std::array<uint32_t,5> level{}; };
Porter readPorter(const discovery::Sampler&,uint64_t base);
uint64_t mask64(uint32_t key);                       // the game's mask for an experience total made from a 32-bit key

struct News { uint32_t hash=0; int32_t points=0,level=0; };
ds::CargoEvent encode(const News&);
std::optional<News> decode(const ds::CargoEvent&);   // nullopt: not reward news, zero, or out of range
class Sync {
public:
    // Facilities whose points rose here since the last look, above what the partner is known to have. The first
    // look learns; a fall, or many rising at once (a loaded save), is learnt silently.
    std::vector<News> observe(const std::vector<Facility>&);
    // The partner's total: remembered, so that what is added here for it is not sent back.
    void heard(const News&);
    // How many points the local facility is short of the partner's total (0: nothing to add; never negative, and
    // never past the game's own cap for that facility).
    static int32_t owed(const Facility&,const News&);
    void unlinked(){known_.clear();}                 // another partner: what he has is unknown again
    void forget(){seen_.clear();known_.clear();primed_=false;}   // another world: learn again
private:
    std::map<uint32_t,int32_t> seen_,known_;bool primed_=false;
};
// The game's own routine (0x2f29f00), signature-checked: adds `amount` points to the facility of `resource` and runs
// the level-ups it earns. False when the residents system is not there or the amount is not a positive one in range.
struct Api { std::function<bool(uint64_t resource,int32_t amount)> add; };
Api nativeApi(discovery::Sampler&);                  // Windows, pinned build; throws elsewhere or when the code differs
}
