#pragma once
// Shared MULEs (enemies.cpp). Each game simulates its own enemies; what is shared is who is down. The game keeps every
// MULE as a persistent actor (game_offsets.md, "Enemies"), placed by a locator with a UUID that is the same in every
// player's game, so a MULE one player knocks out is found in the partner's game by that UUID and put down there with
// the game's own defeat function. Positions and fights are not shared.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
namespace mod::enemies {
constexpr uint8_t WireType=11;                       // the ordered event stream's type for a MULE going down
// How a MULE is down (the game's defeat kinds, the argument of its defeat function); Up = standing.
constexpr uint8_t Knocked=0,Fainted=1,Asleep=2,Dead=3,Up=255;
struct Mule { uint64_t address=0; uint32_t id=0; std::array<uint8_t,16> uuid{}; uint32_t state=0,flags=0,lod=0; std::array<double,3> position{}; };
// Life state +0xea0 (1, 2 up; 4 down with the kind in flag bits 19..21; 3 dead; 5 removed) and flags +0xea4.
uint8_t condition(uint32_t state,uint32_t flags);
// The wire key: 52 bits of the locator UUID (wire ids stay below 2^53).
uint64_t key(const std::array<uint8_t,16>& uuid);
// Every live MULE actor of the game's actor manager (image+0x7bbb8c0); empty when it is not there or unreadable.
std::vector<Mule> readMules(const discovery::Sampler&,uint64_t base);
struct Down { uint64_t key=0; uint8_t kind=Knocked; };
// A MULE camp: its phase (the game's EDSMulePhase at +0x214: 0 normal, 1 return, 2 pre-caution, 3 caution, 4 evasion,
// 5 alert). From caution up the camp knows someone is there: that is what is shared. The game's force function
// asked for alert gives caution when nobody is near the camp and alert when someone is.
constexpr uint8_t CampAlert=100;                     // the event's kind for "this camp went on alert"
constexpr uint32_t AlertPhase=5;
inline bool alarmed(uint32_t phase){return phase>=3&&phase<=5;}
struct Camp { uint64_t address=0; std::array<uint8_t,16> uuid{}; uint32_t phase=0; };
std::vector<Camp> readCamps(const discovery::Sampler&,uint64_t base);
class CampSync {
public:
    // Camps that went on alert since the last look (the first look learns), except those alerted for the partner.
    std::vector<uint64_t> observe(const std::vector<Camp>&);
    // The partner's camp went on alert: the local camp to alert, or null when unknown or already on alert.
    const Camp* wanted(const std::vector<Camp>&,uint64_t key) const;
    void applied(uint64_t key){applied_[key]=false;}
private:
    std::map<uint64_t,uint32_t> seen_;std::map<uint64_t,bool> applied_;bool primed_=false;
};
// The game's own script function that forces a camp's phase (0x2b649a0), here to alert.
std::function<void(const std::array<uint8_t,16>& uuid)> nativeAlert(discovery::Sampler&);
class Sync {
public:
    // One call per look. The first look only learns. Returns the MULEs that went down since the last look, except
    // those this side put down for the partner (applied()).
    std::vector<Down> observe(const std::vector<Mule>&);
    // On joining: every MULE that is down in this game now, so the partner's game has them down too.
    static std::vector<Down> everything(const std::vector<Mule>&);
    // The partner reports a MULE down: the local actor to put down, or null when it is unknown here or already down.
    const Mule* wanted(const std::vector<Mule>&,const Down&) const;
    void applied(uint64_t key){applied_[key]=false;}
private:
    std::map<uint64_t,uint8_t> seen_,pending_;std::map<uint64_t,bool> applied_;bool primed_=false;
};
// The game's own defeat (0x2aa8000): through the damage component when the MULE has a body, by state otherwise.
std::function<void(uint64_t actor,uint8_t kind)> nativeDefeat(discovery::Sampler&);
}
