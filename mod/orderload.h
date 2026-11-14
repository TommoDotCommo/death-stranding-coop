#pragma once
// One shared load per order (orderload.cpp; config `share_order_load`, with `share_orders`). Not run live yet.
//
// Without it each game makes its own cargo for a shared order, so two players carry two loads. With it only the
// player who took the order keeps the order's cargo:
// - Strip: in the game that took the order on for the partner (a mirrored start, orders.h), the cargo the game made
//   for it is removed again, case by case, while it lies in the world. A case on the porter is never removed.
// - Share: a case of a shared order that a porter puts down goes to the partner like any shared cargo (cargo.h), with
//   a tag naming the order, the unit and the item. The partner's game makes it with the game's own cargo creator and
//   the same creation record the mission system fills (game_offsets.md, "Order cargo"), so the copy is the order's
//   cargo there: the order's id, the unit, and a place in the manager's list of order cargo that the mission code walks.
// The game refuses a second record with the handle of a unit that still exists, so one game never holds a unit twice.
#include "cargo.h"
#include "discovery.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <map>
#include <vector>
namespace mod::orderload {
constexpr uint8_t WireType=19;                                   // the ordered event stream's type for a case's tag
constexpr uint64_t UnitMask=0x003f000000000000ull;               // the unit's bits in a handle
// The handle of an order's cargo unit: the record's +18, and the key of the order's unit table.
inline uint64_t handle(uint64_t mission,uint8_t unit){return (mission&~UnitMask)|(uint64_t(unit&0x3f)<<48);}
// What a case keeps of its record's flag words: +204 bit 20 (a main or sub order's cargo), bit 21 (a standard
// order's), +208 bits 1 and 12 (two bytes of the order's cargo resource).
constexpr uint8_t FlagStory=1,FlagStandard=2,FlagResourceA=4,FlagResourceB=8;
constexpr uint8_t TypeStandard=2;                                // record +104 of a standard order's cargo
// item: the item's own hash (what the creator looks the item up by). life: the two condition values, -1 = the item's.
// link: the low 16 bits of the shared id of the placement the tag belongs to (the next event of the stream).
struct Tag { uint64_t mission=0; uint8_t unit=0,type=0,flags=0; uint32_t item=0; float life[2]{-1,-1}; uint16_t link=0; };
struct Wire { uint32_t item=0; uint64_t ref=0; std::array<double,3> position{}; };
// The tag of a placement; nothing when it is not a standard order's case or does not fit the wire (the placement
// then goes out as plain cargo, as before).
std::optional<Tag> tagOf(const cargo::Placement&);
Wire encode(const Tag&);
// Whether a tag is the one of the placement with this shared id (the tag travels right before its placement).
bool belongs(const Tag&,uint64_t placementId);
// Refuses anything a standard order's case cannot be: story cargo, another type, a unit or id out of range.
std::optional<Tag> decode(const Wire&);
// A case to make here: the tag, the order's name for the case (record +c8, the placement's item), the shared id and
// the place (game axes; rotation as the game's 3x3).
struct Case { Tag tag; uint32_t name=0; uint64_t id=0; std::array<double,3> position{}; std::array<float,9> rotation{}; };
class Loads {
public:
    static constexpr uint64_t Retry=500, GiveUp=10000, AskAgain=2000, Grace=5000;   // ms
    // An order started here: forPartner when this game took it on because the partner took it (this game then holds
    // none of its cargo); otherwise this player took it and keeps the load.
    void started(uint64_t mission,bool forPartner,uint64_t now=0);
    void ended(uint64_t mission);                                // completed or given up: nothing of it is kept
    // The orders under way here now: a mirrored order that is not among them Grace after its start has ended unseen
    // (while the partner was away), so that a later taking of it by this player is his own load.
    void underWay(const std::vector<uint64_t>& missions,uint64_t now);
    bool mirrored(uint64_t mission) const {return mirrored_.count(mission)!=0;}
    // A record this game made for a mirrored order (not a case the partner put down): never tracked as shared cargo.
    bool own(const cargo::Record&) const;
    // Record indexes to delete now (each asked for again only after AskAgain). left: own records not in the world
    // (on the porter, on a shelf), which stay.
    std::vector<uint32_t> strip(const std::vector<cargo::Record>&,uint64_t now,unsigned* left=nullptr);
    // The partner put a case down. A newer case of the same unit replaces an older one still waiting.
    void want(const Case&,uint64_t now);
    // The partner took the case with this shared id before it was made here: true when it was waiting (now dropped).
    bool cancel(uint64_t id);
    // Cases to make now: none while a record of that unit still exists here. Report each with tried().
    std::vector<Case> due(const std::vector<cargo::Record>&,uint64_t now);
    void tried(uint64_t id,bool accepted,uint64_t now);
    std::vector<Case> takeGivenUp();                             // waited GiveUp: this game still had the unit
    size_t waiting() const {return pending_.size();}
private:
    struct Pending { Case c; uint64_t since=0,next=0; };
    std::map<uint64_t,uint64_t> mirrored_;std::set<uint64_t> shared_;std::map<uint64_t,uint64_t> asked_;std::vector<Pending> pending_;std::vector<Case> gaveUp_;
};
// The game's cargo creator 0x2c470e0(manager, creation record) with the record's own constructor 0x277fc90, filled as
// the mission system fills it for an order's cargo (0x33a3a50). Game thread. False when the game refused (the unit's
// handle is in use, or the manager is absent).
std::function<bool(const Case&)> nativeCreate(discovery::Sampler&);
}
