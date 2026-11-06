#pragma once
// Shared-world cargo, first version: cargo one porter places in the world appears in the partner's world.
// Read side (read-only): the game's DSBaggage2Manager keeps 511 inline DSBaggage2 records (manager+20, stride 0x240):
// +18 slot, +20 state bits, +28 item definition (non-null when the record is in use), +38 world position (3 x f64,
// game axes), +c8 item hash. Write side: DSBaggage2Manager_sExportedCreateBaggage(item hash, transform) on the game
// thread (game_offsets.md, "Cargo"). Record +21 (state bits 8..15) is where the cargo is: 1 in the world, 2 carried by
// the porter (live 2026-11-07). DeleteBaggage takes the record's index in the manager's array.
//
// Shared cargo has one id (Placement::id, the wire `ref`) in both worlds, chosen by whoever placed it. When either
// porter picks a shared item up (world -> porter) or it vanishes near him, he sends "taken" and the partner deletes
// its copy; if he puts it down again, that is a new placement with a new id. So an item is never in both worlds
// and carried in one of them.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace mod::cargo {
struct Record { uint32_t slot=0, state=0, hash=0; std::array<double,3> position{}; uint32_t holder=0, index=0; };   // game axes, z up;
// holder = +1c (candidate); index = position in the manager's array (what DeleteBaggage takes)
constexpr uint8_t InWorld=1,OnPorter=2;
inline uint8_t location(const Record& r){return uint8_t(r.state>>8);}
// Every record in use. Empty when the manager is absent or does not look like one (wrong vtable).
std::vector<Record> readRecords(const discovery::Sampler&, uint64_t manager, uint64_t managerVtable);
struct Placement { uint32_t hash=0; std::array<double,3> position{}; uint64_t id=0; };   // game axes; id = shared id
// A pick-up (or a shared item gone) to report: the shared id when there is one, and the item and its last place on the
// ground, so the partner can find its copy even when the id is not known there (after a restart of either mod).
struct Taken { uint64_t id=0; uint32_t hash=0; std::array<double,3> position{}; };
// Decides what this porter has placed, and remembers what the partner placed here so it is never sent back.
class Sync {
public:
    // Constants in metres and milliseconds.
    static constexpr double HeldRange=2.5, AwayRange=3.0, NearRange=15.0, Still=0.05, MatchRange=0.5, FindRange=0.5;
    static constexpr uint64_t Settle=1000, HeldMemory=30000, ExpectFor=10000;
    // One call per observation. The first call only learns what already exists (nothing is announced for it).
    std::vector<Placement> observe(const std::vector<Record>&, const std::array<double,3>& porter, uint64_t now);
    // A partner placement about to be created here: the record that appears for it is the partner's, not ours.
    void expect(const Placement&, uint64_t now);
    // Items this porter took since the last call (any pick-up from the ground; a shared item gone near him): send each.
    std::vector<Taken> takeTaken();
    // The partner took an item: forget it here (its removal is never sent back). Returns the record index to delete when
    // it lies in the world here. Found by shared id, or else as the nearest same item on the ground within FindRange of
    // where the partner picked it up; nothing when not found or this porter carries it too.
    std::optional<uint32_t> partnerTook(uint64_t id,uint32_t hash=0,const std::array<double,3>& at={});
    // Copies of partner items that were taken before they appeared here (placement and pick-up close together): record
    // indexes to delete now that they exist.
    std::vector<uint32_t> takeDoomed();
    // Ids for this porter's placements: base + 1, base + 2, ... (keep below 2^53: they travel as a wire t-field).
    void setIdBase(uint64_t base){nextId_=base;}
    size_t mirrored() const;
    size_t pending() const {return expected_.size()+cancelled_.size();}   // expectations and cancellations still held
    size_t shared() const;
private:
    struct Track { uint32_t hash=0,index=0; std::array<double,3> position{}; uint64_t stillSince=0, heldAt=0, id=0; uint8_t where=0;
                   bool fresh=false, announced=false, mirror=false, putDown=false, learned=false, removed=false; };
    std::map<uint32_t,Track> tracks_; bool primed_=false; uint64_t nextId_=0;
    std::vector<std::pair<Placement,uint64_t>> expected_; std::vector<Taken> taken_;std::vector<uint64_t> cancelled_; std::vector<uint32_t> doomed_;
};
}
