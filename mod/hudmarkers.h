#pragma once
// Other players beyond the bodies drawn here, as markers on the HUD (config `player_hud`, opt-in). Where they are is
// what the map already knows (mapicons.h: the server's list, read every two seconds); the players drawn as bodies are
// left out, and so are players in another area of the map.
//
// The game rebuilds the HUD's world-marker list every frame: the producer 0x30941b0 empties the list at HUD logic
// +0x1a3c0 and adds a 0x40-byte record per thing to mark, and sorts it last with 0x30934b0, called at 0x3096571
// (game_offsets.md, "HUD markers"). The mod puts that sort call through its own function, which adds one record per
// player with the game's own adder 0x308ea00(list, kind, position, no marker, id, 0) (the game adds its likes markers
// the same way, without a marker) and then sorts. Nothing is made: no marker, entity, mission or save record. The HUD
// drops records beyond its marker distance and shows at most 256 at once. The call runs in the HUD's own job, not on
// the game thread: it reads a published picture and does not lock.
#include "discovery.h"
#include "mapicons.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <set>
#include <vector>
namespace mod::hudmarkers {
constexpr size_t MaxMarks=64;
// Kinds whose whole path takes a record without a marker. 0x98 (the HUD's MuleLastKnownIcon) is drawn at any
// distance and pinned to the screen's edge when off screen; 0x79 (MuleMachineGunIcon) and 0xbc (NPCFragileIcon) are
// dropped off screen and beyond the HUD's marker distance (live 2026-11-20: added, not seen). The others read the
// marker unchecked, or (0xaf) look up a real user marker by id.
constexpr uint8_t DefaultKind=0x98;
inline bool safeKind(uint8_t k){return k==0x98||k==0x79||k==0xbc;}
struct Mark {float x=0,y=0,z=0;uint64_t id=0;};   // game axes (z up)
// The map's icon id of a player (mapicons::icons: the player's relay id as text, hashed).
uint64_t iconId(uint64_t player);
// The marks: the picture's icons not drawn here (their icon ids), in this area (0, theirs or ours: not known, kept),
// nearest to `here` first (game axes; none: as listed), at most `limit` (and MaxMarks). Each id is the icon's with
// another tag, so it is stable and cannot be one of the game's.
std::vector<Mark> marks(const mapicons::Board::Picture&,uint32_t area,const std::set<uint64_t>& drawn,const std::array<double,3>* here,size_t limit);
class Board {
public:
    struct Picture {uint32_t count=0;std::array<Mark,MaxMarks> marks{};};
    void publish(const std::vector<Mark>&);
    void read(Picture&) const;
private:
    static constexpr unsigned Slots=8;
    std::array<Picture,Slots> slots_{};std::atomic<unsigned> current_{0};
};
struct Stats {uint64_t frames=0,added=0,kept=0;};   // kept: records the HUD's list took (the adder drops one beyond its kind's distance)
// Windows: the HUD's sort call through the mod (every byte checked first, nothing changed on a mismatch). Throws when
// it cannot. board lives as long as the process. Game thread.
void install(discovery::Sampler&,const Board&);
void setKind(uint8_t);   // only a safe kind is taken
Stats stats();
}
