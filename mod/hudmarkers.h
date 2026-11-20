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
#include <functional>
#include <set>
#include <string>
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
// --- The icon the mod's markers show (lab: `puppet.map hudicon KIND` / `hudicon off`; no change by default) ---
// The HUD draws a marker slot with the icon item of the slot's kind: item = *(I+0x20+8*kind) (DSUIMarkerHUDIconResource,
// kinds 0..LastKind), I = *(L+0x1a358), L the marker HUD logic (the list the sort gets is L+0x1a3c0); the item's +0x20
// Icon is a DSUIMarkerIconResource (UITextures count/data +0x20/+0x28, HUDIconHeight +0x38, TextureScale +0x40). A slot
// reads the item only when the game (re)assigns it: 0x3086770(L, slot, 2) -> 0x3091090(L, slot, 1), whenever the
// slot's record differs in kind, id or index in the sorted list, or the slot was not in state 2. The slot then keeps
// the textures. So the mod's markers keep kind 0x98's placement (any distance, pinned to the screen's edge) and get
// another kind's look by having the game's own 0x3091090 run once more on each of their slots, after the game assigned
// it, with the slot's kind byte set to the source kind for that one call and put back after. The game's markers, its
// icon resource and the logic's fields are never changed; a real "MULE last known" marker keeps its icon. The cost: a
// slot the game (re)assigns shows kind 0x98's icon until the mod's next frame (one frame), and the HUD's per-frame
// scale for the slot reads kind 0x98's TextureScale and curve (its size follows the source's texture). Off again: each
// slot the mod restyled and the game has not reassigned since gets kind 0x98's own look back the same way.
// (A swap of kind 0x98's item pointer instead would change every real MULE marker assigned meanwhile and hold the
// resource's reference counts wrong; an unused kind of its own has no item and not 0x98's distance: 0xb2 and 0 take
// the default distance, 0xb3 is the MULE odradek's marker.)
constexpr uint8_t NoIcon=0,DefaultIconSource=NoIcon,LastKind=0xc2;
constexpr size_t HudSlots=256,MaxLayers=16;constexpr uint8_t SlotAssigned=2;
inline bool ownId(uint64_t id){return (id>>48)==0x4855;}   // a record of the mod's (marks() ids)
// The kind whose item the HUD's scale reads as well for a slot of this kind (0x3089aa0: kinds 9..0x2d, unchecked);
// the kind itself for the others.
uint8_t scaleAlias(uint8_t kind);
// A source the mod may draw its markers with: 1..LastKind, not 0xaf (its slots hold a user marker the assignment reads),
// not DefaultKind. Whether the game has an item for it is checked on the HUD job, every frame.
bool safeSource(uint8_t kind);
// One HUD slot as the hook reads it. other: one of its layers shows a loaded texture that is not the wanted one.
struct SlotState {uint8_t kind=0,state=0;uint32_t index=0;uint64_t id=0;bool other=false;};
// Which slots get the textures of `target` (mark: back to the kind's own), one decision per slot per frame. A slot of
// the mod's in state 2 is restyled once after each assignment by the game (a new id or index, or a lapse out of state
// 2 since); a loaded texture that is not the wanted one restyles it again at most every RetryFrames.
class Restyle {
public:
    static constexpr uint64_t RetryFrames=30;
    bool due(size_t slot,const SlotState&,uint8_t mark,uint8_t target,uint64_t frame);
    void done(size_t slot,const SlotState&,uint8_t mark,uint8_t target,uint64_t frame);
private:
    struct Seen {bool valid=false,lapsed=false;uint8_t target=0;uint32_t index=0;uint64_t id=0,frame=0;};
    std::array<Seen,HudSlots> seen_{};
};
// The game's icon items, one row per kind that has one (kind, item, icon, height, scale, each texture's address, size
// and name: UITexture +0x48 ISize, +0x40 TextureName, a counted string), then one row of the kinds without an item.
// Read only.
using Read=std::function<bool(uint64_t,void*,size_t)>;
std::vector<std::string> iconTable(const Read&,uint64_t icons);
// --- Names over the far players (lab: `puppet.map hudnames N`; none by default) ---
// The nearest `most` marks with a name on the map get a hologram name label (nametag.h) NameHeight above the place the
// server gave; key: the mark's id (never a player id, so a body's label and this one are two labels).
constexpr double NameHeight=2.1;constexpr size_t MaxFarNames=3;
struct FarName {uint64_t key=0;std::array<double,3> at{};std::u16string name;};
std::vector<FarName> farNames(const mapicons::Board::Picture&,const std::vector<Mark>&,size_t most);
struct Stats {uint64_t frames=0,added=0,kept=0,restyled=0;};   // kept: records the HUD's list took (the adder drops one beyond its kind's distance)
// Windows: the HUD's sort call through the mod (every byte checked first, nothing changed on a mismatch). Throws when
// it cannot. board lives as long as the process. Game thread.
void install(discovery::Sampler&,const Board&);
void setKind(uint8_t);   // only a safe kind is taken
bool setIconSource(uint8_t);   // NoIcon or a safe source; anything else is refused (false)
uint8_t iconSource();
// The icon table of the HUD the hook last saw (its logic checked by vtable first), or one row saying why not.
std::vector<std::string> iconTableNow(discovery::Sampler&);
Stats stats();
}
