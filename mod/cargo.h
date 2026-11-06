#pragma once
// Shared-world cargo, first version: cargo one porter places in the world appears in the partner's world.
// Read side (read-only): the game's DSBaggage2Manager keeps 511 inline DSBaggage2 records (manager+20, stride 0x240):
// +18 slot, +20 state bits, +28 item definition (non-null when the record is in use), +38 world position (3 x f64,
// game axes), +c8 item hash. Write side: DSBaggage2Manager_sExportedCreateBaggage(item hash, transform) on the game
// thread (game_offsets.md, "Cargo"). Pick-up and ownership are not synchronised yet.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <map>
#include <vector>
namespace mod::cargo {
struct Record { uint32_t slot=0, state=0, hash=0; std::array<double,3> position{}; uint32_t holder=0; };   // game axes, z up; holder = +1c (candidate)
// Every record in use. Empty when the manager is absent or does not look like one (wrong vtable).
std::vector<Record> readRecords(const discovery::Sampler&, uint64_t manager, uint64_t managerVtable);
struct Placement { uint32_t hash=0; std::array<double,3> position{}; };                  // game axes
// Decides what this porter has placed, and remembers what the partner placed here so it is never sent back.
class Sync {
public:
    // Constants in metres and milliseconds.
    static constexpr double HeldRange=2.5, AwayRange=3.0, NearRange=15.0, Still=0.05, MatchRange=2.0;
    static constexpr uint64_t Settle=1000, HeldMemory=30000, ExpectFor=10000;
    // One call per observation. The first call only learns what already exists (nothing is announced for it).
    std::vector<Placement> observe(const std::vector<Record>&, const std::array<double,3>& porter, uint64_t now);
    // A partner placement about to be created here: the record that appears for it is the partner's, not ours.
    void expect(const Placement&, uint64_t now);
    size_t mirrored() const;
private:
    struct Track { uint32_t hash=0; std::array<double,3> position{}; uint64_t stillSince=0, heldAt=0; bool fresh=false, announced=false, mirror=false; };
    std::map<uint32_t,Track> tracks_; bool primed_=false;
    std::vector<std::pair<Placement,uint64_t>> expected_;
};
}
