#pragma once
// What has happened to the porter that the partner should know: he is down (dead or collapsed), or BTs have caught
// him. Read-only, from the porter entity (game_offsets.md, "Death, BTs and voidouts for the partner"): the entity's
// dead bit (bit 7 of the qword at +0x88) and the animation controls Is_Dead (0x0c), Is_Drag (0x12) and
// Int_Is_GazerCatch (0x25), each one byte at entity +0x4dc8 + 12 * control, read with the game's own predicate.
// Is_Crouch (0x15) is read too, only to check the reader against something a test can cause. An unreadable porter is
// unknown, never dead (the game's own IsDead export answers "dead" for no entity).
#include "discovery.h"
#include "../core/core.h"
#include <cstdint>
namespace mod::vitals {
constexpr uint64_t DeadWord=0x88,Controls=0x4dc8,ControlStride=12;
constexpr unsigned IsDead=0x0c,IsDrag=0x12,IsCrouch=0x15,GazerCatch=0x25;
struct State { bool known=false,dead=false,caught=false,crouch=false;
    bool operator==(const State& o) const {return known==o.known&&dead==o.dead&&caught==o.caught&&crouch==o.crouch;} };
// The game's predicate for a control byte (0x2717c40).
bool control(uint8_t b);
State read(const discovery::Sampler&,uint64_t porterEntity);
// The presence flags for a state that has lasted: Down and Caught are raised after HoldMs of the state (one odd frame
// is not a death) and dropped after ClearMs without it; nothing is raised from an unknown state, and what was raised
// stays through unknown ticks (a reload decides by what is read after it).
class Reporter {
public:
    static constexpr uint64_t HoldMs=400,ClearMs=1000;
    uint16_t flags(uint64_t nowMs,const State&);
private:
    struct Edge { bool raised=false,seen=false;uint64_t since=0;bool step(uint64_t now,bool value); };
    Edge down_,caught_;
};
}
