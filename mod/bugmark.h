#pragma once
#include "../core/core.h"
#include <optional>
namespace mod::bugmark {
constexpr unsigned Key=0x71; // F2: free in the supported build's recorded default bindings.
constexpr uint8_t WireType=24;
class Button {
    bool down_=false;uint32_t counter_=0;
public:
    std::optional<uint32_t> press(bool down,unsigned slot) {
        bool edge=down&&!down_;down_=down;
#ifdef MUTANT_DIAG_REPEAT
        edge=down;
#endif
        if(!edge)return {};
        counter_=(counter_+1)&0x0fffffff;if(!counter_)++counter_;
        return (uint32_t(slot)<<28)|counter_;
    }
};
inline ds::CargoEvent Encode(uint32_t mark) {ds::CargoEvent e;e.type=WireType;e.item=mark;return e;}
inline std::optional<uint32_t> Decode(const ds::CargoEvent& e) {
    if(e.type!=WireType||!e.item||e.ref||e.position!=std::array<double,3>{}||e.yaw)return {};
    return e.item;
}
}
