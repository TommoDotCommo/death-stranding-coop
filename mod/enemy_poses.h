#pragma once
// MULEs in the same place for both players (config `share_enemy_positions`, opt-in, with share_enemies and an explicit
// role). Each game still simulates its own MULEs; the host's game says where they are. A few times a second the host
// sends the pose of the standing MULEs nearest to either player (ds::EnemyPoses, the core's unreliable ENEMIES
// sample), and the guest moves its own MULE with the same locator UUID towards that pose: nothing while they agree, a
// short step while the error is small, the game's own teleport when it is large. A MULE that is down, that moves like
// a vehicle, or that is within arm's reach of the guest's porter is left alone, so a fight hand to hand stays the
// guest's own.
//
// The move (game_offsets.md, "Moving a MULE"): the MULE game actor's own teleport, virtual +0x40 of its vtable
// (image+0x3db7078 -> 0x2ad20a0), the call the game's script function TeleportNpc makes for its actors. It stores the
// transform, puts the position on the navigation mesh, and at LOD 2 teleports the entity (0x235a010).
#include "discovery.h"
#include "enemies.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
namespace mod::enemyposes {
// Wire flags (below ds::EnemyFlagMask).
constexpr uint8_t HasBody=1;   // the host's game has an entity for it (LOD 2)
constexpr uint8_t Fast=2;      // it moves faster than a person runs there (on a truck): not to be corrected
constexpr double RangeM=80;          // the host sends MULEs within this of either player
// ...and goes on sending one it already sends until it is further than this: a MULE standing near the edge (live: one
// at 78 m) went in and out of the sample, and each time it was out the guest's copy walked its own way.
constexpr double KeepRangeM=100;
constexpr double ReachM=3;           // a MULE this near the guest's porter is the guest's own to fight
constexpr double LandM=1.5;          // no MULE is put this near the guest's porter
constexpr double DeadbandM=0.75;     // below this error nothing is done
constexpr double SnapM=4;            // from this error the MULE is teleported
constexpr double PullStepM=0.5;      // a pull moves it at most this far
constexpr double FastMps=9;          // above a sprint: a vehicle
constexpr double MaxErrorM=150;      // an error this large is a mistake (another world, a wrong identity): ignored
constexpr uint64_t StaleMs=ds::EnemyStaleMs,SnapHoldMs=300,SnapCooldownMs=2000,PullIntervalMs=500,UnseenCooldownMs=1000;
// A teleport that leaves the MULE where it was (live: the riders of a parked or slow truck are not moved by it) is
// tried again later and later: the cooldown doubles with each one that did not take, up to StuckMaxMs.
constexpr double StuckM=0.3,StuckErrorM=1;
constexpr uint64_t StuckMaxMs=60000;
constexpr unsigned MovesPerLook=2;   // native calls per look, the largest errors first
// With `enemy_position_goal`: a MULE with a body that walks is sent to the host's place with the game's own forced
// destination (mule_goal.h) instead of being put there: it walks the error off. While that works for a MULE (a write
// was taken within SteerHoldMs) it is only teleported from SteerSnapM, and never stepped.
constexpr double SteerSnapM=10;
constexpr uint64_t SteerHoldMs=1500;
constexpr unsigned SteersPerLook=6;  // forced destinations per look, the largest errors first
// Heading of a rotation (rows, the game's z up) and the upright rotation of a heading.
double heading(const std::array<float,9>& rotation);
std::array<float,9> upright(double heading);
// Host: the standing MULEs within RangeM of either player (KeepRangeM for one already sent), the nearest first, at most ds::EnemyPosesMax, in key order.
class Sampler {
public:
    ds::EnemyPoses select(const std::vector<enemies::Mule>&,const std::array<double,3>& porter,const std::optional<std::array<double,3>>& partner,uint64_t now);
private:
    struct Seen { std::array<double,3> position{}; uint64_t at=0; bool fast=false,sent=false; };
    std::map<uint64_t,Seen> seen_;
};
struct Move { uint64_t address=0,key=0; bool snap=false; std::array<double,3> position{}; double heading=0,error=0; std::array<double,3> from{}; unsigned stuck=0; };   // stuck: teleports in a row that did not move it
// Guest: what to do this look. `shown` is the core's newest sample (null when there is none or it is stale).
class Corrector {
public:
    // `steer` (when given) receives the MULEs to send walking to the host's place (position = the host's, from = this
    // game's); `steered` (key -> time of the last forced destination the game's checks let through) are the ones that
    // are walking it off.
    std::vector<Move> decide(const std::vector<enemies::Mule>&,const ds::EnemiesShown* shown,const std::array<double,3>& porter,uint64_t now,bool pull=true,
                             const std::map<uint64_t,uint64_t>* steered=nullptr,std::vector<Move>* steer=nullptr);
    void forget(){seen_.clear();}
    uint64_t snaps() const {return snaps_;}
    uint64_t pulls() const {return pulls_;}
private:
    struct Seen { std::array<double,3> position{},before{}; uint64_t at=0,farSince=0,movedAt=0; bool fast=false,wasFar=false,moved=false,judged=false; unsigned stuck=0; };
    std::map<uint64_t,Seen> seen_;uint64_t snaps_=0,pulls_=0;
};
struct Place { std::array<double,3> position{}; std::array<float,9> rotation{}; };
// The MULE actor's own teleport; throws when the game's code differs from the pinned image (and off Windows). The
// returned function checks the actor (committed memory, the MULE actor vtable, its teleport slot, live byte) and
// returns false when it does not move it.
std::function<bool(uint64_t actor,const Place&)> nativeTeleport(discovery::Sampler&);
// The checks nativeTeleport makes on an actor, on any platform (the test runs them on fake memory).
bool movable(const discovery::Sampler&,uint64_t base,uint64_t actor);
constexpr uint64_t ActorVtable=0x3db7078,TeleportSlot=0x40,TeleportFunction=0x2ad20a0,BaseTeleport=0x22f8d90,EntityTeleport=0x235a010;
}
