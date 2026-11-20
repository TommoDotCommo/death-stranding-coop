#pragma once
// Orders taken together in group play (opt-in `group_orders`; the server's rule `cargo`; README "Orders taken
// together"). Two-player play shares every standard order with the partner (orders.h); in a group nobody is the
// partner, so an order is shared by the players who were there when it was taken:
//   - a standard order this porter takes at a terminal starts a party: its start is said to the server with a fresh
//     party number and where he stands (`…/coop/events`, kind `order`, value 1);
//   - a game that hears that start while its own porter stands within JoinMetres of that place, within JoinWindowMs of
//     the start, with the same order available in his world, takes the order on too with the game's own start request,
//     and is in that party;
//   - a unit any of the party hands in (kind `unit`) is marked delivered in the others' games, as share_order_units
//     does for the partner; a success said by any of them completes the order in the others' games; a give-up said by
//     the one who took it (the server lets nobody else say it) gives it up in theirs. A member who gives up leaves the
//     party alone: nothing is said.
// Each game decides only about its own world: a start only for an order available here, a unit only for a row of an
// order of the party under way here that is not delivered, an end only for an order of the party under way here.
// Nothing is said back that was done here for the party. Pure: no game, no server (the glue is mod.cpp tickGroupOrders).
#include "groupevents.h"
#include "orders.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace mod::grouporders {
constexpr double JoinMetres=30;           // standing at the terminal with the porter who took it
constexpr uint64_t JoinWindowMs=120000;   // ... within two minutes of his taking it
constexpr uint64_t JoinSettleMs=15000;    // a start asked for here that the game has not shown by then is given up
constexpr size_t HeardMax=4096;           // what is kept of the others' parties
constexpr int64_t PartyMax=(int64_t(1)<<52)-1;
using Place=std::array<double,3>;
// What the glue does in the game: the game's own start, success or fail request, or a unit's row marked delivered
// (with what the hand-in left in it) and this game's copy of that cargo lying in the world removed.
struct Request {
    enum What : uint8_t {Start=1,Success=2,Fail=3,Unit=4};
    What what=Start; uint64_t mission=0; uint8_t unit=0; int64_t party=0; uint32_t packed=0; float life[2]{};
};
// What the player is told in the info log.
enum class Line : uint8_t {Joined,UnitByOther,DoneByOther,GivenUpByLeader};
struct Step { std::vector<groupevents::Event> say; std::vector<Request> apply; std::vector<Line> lines; std::vector<std::string> log; };
class Together {
public:
    explicit Together(uint64_t seed):rng_(seed){}
    // What the server listed of the others' parties (kinds order and unit; anything else is ignored).
    void hear(const std::vector<groupevents::Event>&,uint64_t nowMs);
    // One look at this world: the orders and unit rows as read, where the porter stands (none when unknown).
    // The first look after construction or reset() learns: an order already under way starts no party.
    Step step(uint64_t nowMs,const std::vector<orders::Order>&,const std::vector<orders::Unit>&,const std::optional<Place>& porter);
    // The glue reports whether the game took a request.
    void done(const Request&,bool accepted);
    // A new world (a load): the parties this game was in are left; what it heard of others stays.
    void reset();
    size_t parties() const {return members_.size();}
    bool member(uint64_t mission,int64_t party) const {auto m=members_.find(mission);return m!=members_.end()&&m->second.party==party&&!m->second.pending;}
private:
    struct Member { int64_t party=0; bool leader=false,pending=false; uint64_t since=0; };
    struct Offer { Place at{}; uint64_t heardAt=0; };
    int64_t fresh();
    uint64_t rng_; bool primed_=false;
    std::map<uint64_t,orders::Order> seen_; std::map<uint64_t,uint16_t> seenUnits_;
    std::map<uint64_t,Member> members_;                                  // mission -> the party this game is in for it
    std::map<std::pair<int64_t,uint64_t>,Offer> offers_;                 // (party, mission): a start heard
    std::map<std::pair<int64_t,uint64_t>,uint8_t> ends_;                 // (party, mission): a success or give-up heard
    std::map<std::pair<int64_t,uint64_t>,std::vector<double>> units_;    // (party, unit key): a hand-in heard
    std::set<int64_t> left_;                                             // parties left or ended: never joined again
    std::map<uint64_t,uint8_t> applied_;                                 // mission -> what was asked here for the party
    std::set<uint64_t> unitsApplied_;                                    // unit keys marked delivered here for the party
};
}
