#pragma once
// The partner's outfit (art-part states: hood, caps, suit pieces), with partner_gear (gear.h).
//
// A Sam's look is the enabled state of the parts of his ArtPartsData (the component with vtable image+3cf2b18 owned by
// the entity; part count at +100, part array at +108, each part's enabled request at part +2c bit 0; 52 parts in build
// 13300582). The own body is dressed from the resident double, whose part states are not the porter's (live
// 2026-11-08: 3 of 52 differ). Each side sends its porter's enabled mask (one CARGO event of type 8); the partner's body
// switches the parts that differ with the game's own part worker (22b8fe0, double.h nativePartToggle), a few per call.
// Part 20 is never switched: switched on a made body it drew stretched polygons (game_offsets.md, own body).
#include "discovery.h"
#include "../core/core.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
namespace mod::outfit {
constexpr uint8_t WireType=8;
constexpr uint64_t ArtPartsVtable=0x3cf2b18,ComponentCount=0x90,ComponentArray=0x98,ComponentOwner=0x48,
                   PartCount=0x100,PartArray=0x108,PartState=0x2c;
constexpr unsigned MaxParts=64,MaxComponents=256,Skipped=20;
struct Parts {
    uint8_t count=0;uint64_t enabled=0;
    bool operator==(const Parts& o) const {return count==o.count&&enabled==o.enabled;}
    bool operator!=(const Parts& o) const {return !(*this==o);}
};
// The entity's own ArtPartsData and its enabled mask; nullopt when unreadable or over MaxParts parts.
std::optional<Parts> read(const discovery::Sampler&,uint64_t entity);
// Parts to switch on the body to look like `want`: (index, on) pairs, at most `limit`, never Skipped; none when the
// part counts differ (another resource).
std::vector<std::pair<unsigned,bool>> changes(const Parts& body,const Parts& want,unsigned limit);
ds::CargoEvent encode(const Parts&,uint16_t generation);
std::optional<Parts> decode(const ds::CargoEvent&);
// Sends the mask once it has held for Stable ms, at most once per MinGap ms, and again on every new connection.
class Sender {
public:
    static constexpr uint64_t Stable=3000,MinGap=1000;
    std::optional<ds::CargoEvent> observe(const std::optional<Parts>& read,bool connected,uint64_t now);
private:
    Parts candidate_,sent_;uint64_t since_=0,lastSend_=0;bool haveCandidate_=false,haveSent_=false,wasConnected_=false,resend_=false;
    uint16_t generation_=0;
};
}
