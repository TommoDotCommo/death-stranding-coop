#pragma once
// Roads paved together in group play (opt-in `group_roads`). The game asks the server what the other players put into
// its auto-pavers (getHighwayResources) and keeps the answer per paver and material (the highway ledger's map at
// [[image+0x7c173b0]]+0x310: open addressing, the slot count at +0x31c, 0x30 bytes a slot, an empty slot's first word
// 0; +0x10 construction id, +0x14 material, +0x20 what the game took itself, +0x28 the server's total), but by its own
// rules it takes only a share of each requirement, counting this player's own deliveries, and nothing for some pavers
// (game_offsets.md, "Roads paved together"). So the mod gives the rest as two-player play gives the partner's
// materials (roads.h give, 0x2c321a0: delivered by others, the road built when complete): per paver still without
// its road and material, what the server says the others put in, less what the game took and what the mod gave
// before, never past what the paver still misses. What the mod gave is kept per server world in a small file, so a
// restart does not give it again.
#include "roads.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace mod::roadshare {
constexpr uint64_t LedgerSlot=0x7c173b0,MapAt=0x310,SlotsAt=0x31c,SlotBytes=0x30;
constexpr uint32_t MaxSlots=4096;
struct Asked { uint32_t paver=0,material=0; int64_t taken=0,total=0; };
std::vector<Asked> readAsked(const discovery::Sampler&,uint64_t base);
// What the mod gave, per paver and material: kept as text, one "paver material amount" a line.
class Ledger {
public:
    int64_t given(uint32_t paver,uint32_t material) const;
    void add(uint32_t paver,uint32_t material,int64_t n){if(n>0)given_[{paver,material}]+=n;}
    std::string dump() const;
    static Ledger load(const std::string&);
    bool empty() const {return given_.empty();}
private:
    std::map<std::pair<uint32_t,uint32_t>,int64_t> given_;
};
struct Give { uint64_t paver=0; uint32_t id=0; roads::Set set{}; };
// What to give now, and the ledger moved on by it. `missing`: what a paver still lacks for its next level.
std::vector<Give> plan(const std::vector<Asked>&,const std::vector<roads::Paver>&,const std::function<roads::Set(uint64_t paver)>& missing,Ledger&);
}
