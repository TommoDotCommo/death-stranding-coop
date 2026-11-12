#pragma once
// Shared roads (roads.cpp). An auto-paver is one of the game's constructions (type 36) that every player's game has
// under the same construction id. What a player delivers to a paver is given to the partner's paver as "delivered by
// others", with the game's own routine for online contributions, which also builds the road at once when the
// materials are complete. A road that gets built in one game is completed in the partner's the same way.
// game_offsets.md, "Roads".
#include "discovery.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>
namespace mod::roads {
constexpr uint8_t WireType=14;                       // the ordered event stream's type for road news
constexpr int Kinds=6;                               // the game's paver materials (crystals, resin, metal, ceramic, chemical, alloy)
constexpr uint32_t Built=1,Material=10;              // the event's item: Built, or Material + kind with the amount
constexpr int32_t Most=1000000;                      // more than this in one look is not a delivery
using Set=std::array<int32_t,Kinds>;
// Level 1 = paver without road, 2 and up = road built. own = delivered by this player, net = by others.
struct Paver { uint64_t address=0; uint32_t id=0,level=0; Set own{},net{}; };
// Every paver of the construction manager (image+0x7bf4960; list +0x56a258 / +0x56a260, type byte +0x34 == 36).
std::vector<Paver> readPavers(const discovery::Sampler&,uint64_t base);
struct News { uint32_t id=0,item=0; int32_t amount=0; };
class Sync {
public:
    // What this player delivered to each paver since the last look, and the roads built since (the first look
    // learns; a road completed here for the partner is not news).
    std::vector<News> observe(const std::vector<Paver>&);
    // The partner's news: the local paver it applies to, or null (unknown paver, nonsense, or a road already built).
    static const Paver* wanted(const std::vector<Paver>&,const News&);
    void applied(uint32_t id){applied_.insert(id);}
    void forget(){seen_.clear();applied_.clear();primed_=false;}        // another world: learn again
private:
    std::map<uint32_t,Paver> seen_;std::set<uint32_t> applied_;bool primed_=false;
};
struct Api {
    std::function<void(uint64_t paver,const Set&)> give;        // materials from the partner (0x2c321a0)
    std::function<void(uint64_t paver)> complete;               // give what is still missing: the road is built
    std::function<Set(uint64_t paver)> missing;                 // what the next level needs in all (0x2c59770); empty when supplied
    std::function<void(uint64_t paver,const Set&)> deliver;     // test: a delivery by this player, as the terminal does it
};
Api nativeApi(discovery::Sampler&);
}
