#pragma once
// Shared BTs (bts.cpp). Each game simulates its own BTs; the area itself is shared through the rain (weather.h). What
// is shared here: a gazer (the floating BT) one player destroys is destroyed in the partner's game, found by its
// locator UUID, which is the same in every player's game; a catcher (boss) fight one player is pulled into starts in
// the partner's game when the partner is near; and when one player's fight ends with the area cleared, the partner's
// ends the same way. game_offsets.md, "BTs".
#include "discovery.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>
namespace mod::bts {
constexpr uint8_t WireType=13;                       // the ordered event stream's type for BT news
constexpr uint32_t GazerDead=1,FightOn=2,AreaCleared=3;   // the event's item
constexpr double FightRange=300;                     // a fight starts for a partner within this many metres
constexpr size_t MassDeath=12;                       // more gazers than this gone in one look is a reset, not kills
struct Gazer { uint64_t address=0; std::array<uint8_t,16> uuid{}; float life=0; uint32_t flags=0,lod=0; };
// Flag bit 20: a gazer the game made for the moment (it removes those itself when the rain stops): not shared.
inline bool dynamic(uint32_t flags){return flags&(1u<<20);}
uint64_t key(const std::array<uint8_t,16>& uuid);
// Every live gazer actor with a locator (actor manager image+0x7bbb8c0; life +0x180, flags +0x1b4).
std::vector<Gazer> readGazers(const discovery::Sampler&,uint64_t base);
class GazerSync {
public:
    // Gazers seen alive at the last look and destroyed now (the first look learns).
    std::vector<uint64_t> observe(const std::vector<Gazer>&);
    // The partner destroyed a gazer: the local one, or null when unknown here or already gone.
    static const Gazer* wanted(const std::vector<Gazer>&,uint64_t key);
    void applied(uint64_t key){applied_.insert(key);}
private:
    std::map<uint64_t,bool> alive_;std::set<uint64_t> applied_;bool primed_=false;
};
// The catcher manager (image+0x7bee060): fights running now and the spheres cleared so far.
struct Fights { bool readable=false; int32_t active=0,cleared=0; };
Fights readFights(const discovery::Sampler&,uint64_t base);
class FightSync {
public:
    // One call per look: FightOn when a fight starts and again every RepeatLooks while it runs (a partner who comes
    // near later joins it), AreaCleared when a sphere was cleared since the last look. The first look learns.
    std::vector<uint32_t> observe(const Fights&);
    static constexpr int RepeatLooks=20;
    // What the partner's news asks of this game: FightOn = start one (none runs here and the partner is in range),
    // AreaCleared = end the one running here. Otherwise nothing.
    static bool wanted(const Fights&,uint32_t what,double partnerDistance);
private:
    bool primed_=false;int32_t cleared_=0;bool active_=false;int looks_=0;
};
// Writes the gazer's life to zero; the game's own actor update does the rest (flag, record, the body's reaction).
std::function<void(uint64_t actor)> nativeDestroy(discovery::Sampler&);
// The game's catcher control command (0x2a928c0): 0 brings a catcher to the player, 1 returns it (area cleared).
std::function<void(uint8_t command)> nativeCatcher(discovery::Sampler&);
}
