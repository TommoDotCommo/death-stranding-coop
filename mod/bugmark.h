#pragma once
#include "../core/core.h"
#include <optional>
namespace mod::bugmark {
constexpr unsigned Key=0x71; // F2: free in the supported build's recorded default bindings.
constexpr uint8_t WireType=24;
// A mark's number: the player's prefix in its top `bits` bits, then a counter. Two players: the slot (4 bits), which
// the partners never share. Group play: every player has the same slot, so the prefix is 12 bits of his relay id
// (GroupPrefix), and the marks of a group seldom share a number (the flight files are named by it).
constexpr unsigned SlotBits=4,GroupBits=12;
inline unsigned GroupPrefix(uint64_t id){id^=id>>33;id*=0xff51afd7ed558ccdull;id^=id>>33;id*=0xc4ceb9fe1a85ec53ull;id^=id>>33;return unsigned(id&0xfff);}
class Button {
    bool down_=false;uint32_t counter_=0;
public:
    std::optional<uint32_t> press(bool down,unsigned prefix,unsigned bits=SlotBits) {
        bool edge=down&&!down_;down_=down;
#ifdef MUTANT_DIAG_REPEAT
        edge=down;
#endif
        if(!edge)return {};
        uint32_t mask=uint32_t((1ull<<(32-bits))-1);
        counter_=(counter_+1)&mask;if(!counter_)++counter_;
        return (uint32_t(prefix)<<(32-bits))|counter_;
    }
};
inline ds::CargoEvent Encode(uint32_t mark) {ds::CargoEvent e;e.type=WireType;e.item=mark;return e;}
inline std::optional<uint32_t> Decode(const ds::CargoEvent& e) {
    if(e.type!=WireType||!e.item||e.ref||e.position!=std::array<double,3>{}||e.yaw)return {};
    return e.item;
}
}
