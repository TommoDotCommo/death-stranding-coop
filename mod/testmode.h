#pragma once
// Test mode for alpha testers (README "Test mode"; config `test_mode`, testers' servers switch it on with the policy
// setting `test_mode`): material crates on a key, and blood and stamina kept full. Off by default.
//
// Materials: one press of the key asks the game for one crate of each material it has a hash for, with the game's own
// DSBaggage2Manager CreateBaggage(hash, transform) on the game thread (game_offsets.md, "Cargo"), in a row two metres
// behind the porter. At most one set every GapMs, and at most MaxOnGround of this player's crates on the ground: what
// is already there is never removed to make room (the oldest are left alone), the press is refused instead.
//
// Gauges: the porter's status component S = *(entity + 0x54e0): blood S+0xc0 of S+0xc4, stamina S+0x110 of S+0x104 with its
// ceiling S+0x108, as the game's own full recovery (0x2738940) sets them; his battery is in the component at entity +
// 0x54c8 (game_offsets.md, "Blood, stamina and battery"). The block at player + 0x2b0 only holds copies the game rewrites
// every frame: writing it changes nothing (seen live 2026-11-19). A value is set to its maximum only when below it, only
// when both read as sane floats, only through the gate (the game thread, the porter readable, no game event, not dead
// and not caught).
#include "cargo.h"
#include "discovery.h"
#include "vitals.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace mod::testmode {
// The six materials, the largest crate of each that the game has. hash 0: no hash known for this build yet (that
// material is left out of a set, and the log says so once). verified: seen live as that material's crate.
struct Material { const char* name;uint32_t hash;bool verified; };
constexpr size_t MaterialCount=6;
extern const Material Materials[MaterialCount];
std::vector<uint32_t> knownHashes();   // the hashes of a set, in the table's order
std::string missingNames();            // the materials without a hash, for the log ("" when none)

struct Drop { uint32_t hash=0;std::array<double,3> at{}; };   // game axes, z up
// Where a set's n crates go: Behind metres behind the porter (forward = his facing in the ground plane), in a row across
// his back Spacing metres apart, Lift metres up so they settle onto the ground. A degenerate facing uses +y.
constexpr double Behind=2.0,Spacing=1.2,Lift=0.5;
std::vector<std::array<double,3>> places(const std::array<double,3>& porter,double forwardX,double forwardY,size_t n);

// The rate limiter and the cap. A crate counts from its request: until ExpectMs pass without a record of its item in the
// world within MatchRange of where it was asked for, and then as long as one lies within MatchRange of where it was last
// seen (it may settle or slide a little). A crate picked up, used or gone stops counting. Each record counts for one
// crate at most.
class Dropper {
public:
    static constexpr uint64_t GapMs=5000,ExpectMs=10000;
    static constexpr size_t MaxOnGround=20;
    static constexpr double MatchRange=2.0;
    struct Result { std::vector<Drop> drops;std::string refused; };   // refused: why nothing was asked for ("" when some was)
    // This tick's cargo records (the manager's table), every few hundred milliseconds.
    void observe(const std::vector<cargo::Record>&,uint64_t now);
    // The key was pressed: the crates to ask for now (fewer than a set when the cap leaves less room), already counted.
    Result press(uint64_t now,const std::array<double,3>& porter,double forwardX,double forwardY,const std::vector<uint32_t>& hashes);
    size_t onGround() const {return tracked_.size();}
private:
    struct Tracked { uint32_t hash=0;std::array<double,3> at{};uint64_t since=0;bool seen=false; };
    std::vector<Tracked> tracked_;uint64_t last_=0;bool pressed_=false;
};

// Blood and stamina.
constexpr uint64_t PlayerManagerSlot=0x7bc7568,PlayerInfo=0x2b0;   // as signals.h readInfo
constexpr uint64_t EntityStatus=0x54e0,BloodValue=0xc0,BloodMax=0xc4,StaminaValue=0x110,StaminaMax=0x104,StaminaCeiling=0x108;
// The porter's own battery (not a vehicle's): component E+0x54c8 of his entity; the charge is the float +0x4810, the full
// charge +0x4814 (read live: 1000 of 1000). The game's own refill (DSPlayerSystem_sExportedRecoverBattery, 0x27e1010)
// copies the second to the first and clears +0x47f8.
constexpr uint64_t EntityBattery=0x54c8,BatteryValue=0x4810,BatteryMax=0x4814,BatteryClear=0x47f8;
constexpr float GaugeCeiling=1e6f;   // a maximum above this is not a gauge
// The value to write, or nothing: the maximum when value < maximum and both are finite, 0 <= value and 0 < maximum <=
// GaugeCeiling.
std::optional<float> refill(float value,float maximum);
// When the gauges may be written: on the game thread, with the porter readable, outside a game event (cutscene,
// private room...), with his state known, and neither dead nor caught by BTs. Returns "" when they may, else why not.
struct Gate { bool gameThread=false,porterReadable=false,inEvent=true;vitals::State vitals; };
const char* refusal(const Gate&);
// The porter's status block B (player + PlayerInfo), accepted only when the player's entity is this porter; 0 = none.
uint64_t infoBlock(const discovery::Sampler&,uint64_t imageBase,uint64_t porterEntity);
// His status component, on the same condition; 0 = none.
uint64_t statusComponent(const discovery::Sampler&,uint64_t imageBase,uint64_t porterEntity);
using Writable=std::function<bool(uint64_t at,size_t n)>;          // committed read-write memory, all of it
using Poke=std::function<void(uint64_t at,const void* from,size_t n)>;
struct Kept { const char* refused="";unsigned blood=0,stamina=0; };   // writes made by one call
// One tick: through the gate, each switched-on gauge of status component `status` set to its maximum when it is below it.
Kept keepFull(const Gate&,const discovery::Sampler&,uint64_t status,bool blood,bool stamina,const Writable&,const Poke&);
// The same for the porter's battery. status: as above, nonzero only for this porter, whose entity is `entity`. The writes made (0 or 1).
unsigned keepBattery(const Gate&,const discovery::Sampler&,uint64_t status,uint64_t entity,const Writable&,const Poke&);
}
