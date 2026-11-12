#pragma once
// Shared orders (orders.cpp). A standard delivery order one player takes on is taken on in the partner's game too, and
// when it is completed or given up in one game it is completed or given up in the other, each through the game's own
// mission requests (game_offsets.md, "Orders"). Only standard orders (the repeatable deliveries and collections) are
// shared: main and sub orders move the story, and that stays each player's own.
#include "discovery.h"
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
namespace mod::orders {
constexpr uint8_t WireType=12;                       // the ordered event stream's type for an order change
constexpr uint8_t Start=1,Success=2,Fail=3;          // what happened to it
constexpr uint16_t Available=1,Progress=4;           // the game's order state (+0x2c)
constexpr uint8_t Completed=4;                       // the game's order result (+0x2a): 1 not started, 2 under way, 4 completed
struct Order { uint64_t id=0; uint16_t state=0; uint8_t result=0,detail=0; };
// Category detail of the order's resource: 2 a standard delivery, 3 a standard collection.
inline bool shareable(const Order& o){return o.detail==2||o.detail==3;}
// The static orders of the game's mission system (image+0x7c0aff8, array at +0x108); empty when unreadable.
// details: a cache of each order's category detail (it never changes), filled as orders are first seen.
std::vector<Order> readOrders(const discovery::Sampler&,uint64_t base,std::map<uint64_t,uint8_t>& details);
struct Change { uint64_t id=0; uint8_t what=Start; };
class Sync {
public:
    // One call per look while the partner is connected. The first look after reset() reports every shareable order
    // under way (the partner takes them on); later looks report what started, was completed or was given up here,
    // except what this side did for the partner (applied()).
    std::vector<Change> observe(const std::vector<Order>&);
    void reset(){seen_.clear();primed_=false;}
    // The partner reports a change: true when it applies here (a start to an available order, an end to one under way).
    bool wanted(const std::vector<Order>&,const Change&) const;
    void applied(const Change& c){applied_[c.id]=c.what;}
private:
    std::map<uint64_t,Order> seen_;std::map<uint64_t,uint8_t> applied_;bool primed_=false;
};
// One cargo unit of an order (the game's table at mission system +0x18): state 1 not delivered, 2 delivered.
// row: where the row's state is, to mark it delivered here.
struct Unit { uint64_t mission=0; uint8_t unit=0; uint16_t state=0; uint64_t stateAt=0; };
constexpr uint16_t NotDelivered=1,Delivered=2;
constexpr uint8_t UnitBase=100;                      // an order change's `what` for "unit N delivered" is UnitBase+N
std::vector<Unit> readUnits(const discovery::Sampler&,uint64_t base);
// A split delivery: each unit one player hands in is marked delivered for the partner, so partial hand-ins add up.
class UnitSync {
public:
    // Units of the given (shareable, under way) orders that went from not delivered to delivered since the last
    // look, except those marked here for the partner. The first look learns.
    std::vector<Unit> observe(const std::vector<Unit>&,const std::vector<uint64_t>& missions);
    // The partner handed a unit in: the local row to mark, or null when it is unknown here or not waiting.
    const Unit* wanted(const std::vector<Unit>&,uint64_t mission,uint8_t unit) const;
    void applied(uint64_t mission,uint8_t unit){applied_[key(mission,unit)]=true;}
    static uint64_t key(uint64_t mission,uint8_t unit){return mission|(uint64_t(unit&0x3f)<<48);}
private:
    std::map<uint64_t,uint16_t> seen_;std::map<uint64_t,bool> applied_;bool primed_=false;
};
// The game's own requests: start (0x33b80a0), success (0x33b82d0), fail (0x33b75e0). Returns whether it was accepted.
std::function<bool(uint64_t id,uint8_t what)> nativeRequest(discovery::Sampler&);
}
