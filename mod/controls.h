#pragma once
// The partner's body moves like the partner's porter: every animation control of the porter's network (the bools and
// floats his player states write: balance, slipping, wading, pick-ups, gestures, resting, falling, aiming...) is read
// from his own MorphemeAnimationManager records, sent beside his presence (core ANIMATION) and written into the
// body's network, which is an instance of the same network. Read-only on the sender.
#include "../core/core.h"
#include "discovery.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace mod::controls {
// By the game's own numbering where it has one (the player's control registration, ds.exe 0x18838b0..: the bool enum
// and the float enum), then controls the porter's records hold beyond it. nullptr: no such control.
constexpr unsigned BoolCount=160,FloatCount=160;
static_assert(BoolCount<=8*ds::AnimationBoolBytes&&FloatCount<=ds::AnimationFloats,"controls fit the wire");
extern const char* const BoolNames[BoolCount];
extern const char* const FloatNames[FloatCount];
// The game only ever pulses it for one frame (0x2741c80): the body gets a pulse (TriggerBool), never a held value.
bool pulseOnly(unsigned boolIndex);
// Never written into the body: physics it does not have (Is_Ragdoll, Is_ActiveSkeleton), the body class's own
// (Is_AKukan), and what the mod sets itself (Is_PrivateRoom).
bool skipBool(unsigned index);
bool skipFloat(unsigned index);
inline bool bit(const std::array<uint8_t,ds::AnimationBoolBytes>& a,unsigned i){return (a[i/8]>>(i%8))&1;}
inline void setBit(std::array<uint8_t,ds::AnimationBoolBytes>& a,unsigned i,bool on){if(on)a[i/8]|=uint8_t(1u<<(i%8));else a[i/8]&=uint8_t(~(1u<<(i%8)));}

// Sender: each frame's values; a bool that went from off to on in any frame since the last send is kept in rose
// until sent() (a one-frame pulse is seen only that way).
class Gather {
public:
    void add(const std::array<uint8_t,ds::AnimationBoolBytes>& held,const std::array<float,ds::AnimationFloats>& floats);
    const ds::Animation& current() const {return now_;}
    bool ready() const {return have_;}
    void hands(const std::array<ds::HeldCase,ds::AnimationHands>& h,uint8_t count){now_.hands=h;now_.handCount=count;}
    void sent(){now_.rose={};}
    void reset(){*this=Gather{};}
private:
    ds::Animation now_{};bool have_=false;
};

// The porter's records: name pointer +0, network id +8, value +0x20 (a byte for a bool, a float), 48 bytes each
// (readActions). The slot -> control map is built once per manager and record count.
// slot: >=0 bool, <-1 float -(i+2), -1 none; unknown: record names not in the lists (set when rebound, for the log).
struct Binding {uint64_t manager=0,entity=0;uint32_t count=0;std::vector<int16_t> slot;std::vector<std::string> unknown;bool rebound=false;};
bool readPorter(const discovery::Sampler&,uint64_t entity,uint64_t animatorVtable,Binding&,
                std::array<uint8_t,ds::AnimationBoolBytes>& held,std::array<float,ds::AnimationFloats>& floats);

// Receiver: what to write into the body to go from the last applied sample to this one (all of it when there is
// none). fresh: this sample is new since the last call, so its rose bits are pulses still to give.
struct Write {bool isBool=true;unsigned index=0;float value=0;bool trigger=false;};
std::vector<Write> plan(const ds::Animation* last,const ds::Animation& now,bool fresh);
// Between two samples (core AnimationShown::between): continuous floats (rates, weights, headings, look angles) are
// blended towards the next sample, headings and angles the short way round in degrees; kinds, states, levels and the
// shared Temporary_* values step as sent; a case held in the same hand in both samples moves between its two places.
// Without a next sample, the shown one as it is.
bool continuous(unsigned floatIndex);
bool angular(unsigned floatIndex);
ds::Animation blend(const ds::AnimationShown&);
// Leaving the mirror: every control the mirror set goes back to zero.
std::vector<Write> clear(const ds::Animation& last);
}
