#pragma once
// Lab probes for a MULE's movement goal (game_offsets.md, "What gives a MULE its goal"). Not a feature: two test
// commands of puppet.cargo, there to check what was read from the code before anything is built on it.
//
// The chain, read with the sampler's guarded reads only (nothing of the game is called):
//   actor +0x68 -> entity (the pointer less 0x20) -> its components (+0x90 count, +0x98 array) -> the AI component
//   (DSAIIndividualComponent, image+0x3dca218, or its base AIIndividualComponent, image+0x3dc9c10) -> the AI, kept
//   twice (component +0x50 and +0x80) -> the body-goal table at AI +0x3720 (entries pointer, capacity at +0xc; an
//   entry is 0x20 bytes: hash, key at +8, value at +0x10) -> the value for AIBodyGoalDSWalk (image+0x4c91610): the
//   walk's destination at +0x9e0 and +0x850;
//   AI +0x3690 -> D -> its request blocks at D +0x2a0: request = D + 0x520 + i*0x240, i = the i32 at D +0x4d20, or 2
//   when the byte at D +0x4d24 is set; the forced destination at request +0x78, its bit 0x40 in request +0x238.
// The write (`force`) is the plan's own `!ds_force_destination`, done from outside: three doubles and one bit, only
// when every link is the object it should be and the memory from D to the end of the request is plain writable.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
namespace mod::mulegoal {
constexpr uint64_t ActorEntity=0x68,EntityBias=0x20,ComponentCount=0x90,ComponentArray=0x98;
constexpr uint64_t DsComponentVtable=0x3dca218,BaseComponentVtable=0x3dc9c10,ComponentAi=0x50,ComponentAiAgain=0x80;
constexpr uint64_t AiVtable=0x3e11880,AiState=0x48,AiGoals=0x3720,AiRequests=0x3690;
constexpr uint64_t WalkGoalKey=0x4c91610,GoalDestination=0x9e0,GoalDestinationCopy=0x850;
constexpr uint64_t RequestIndex=0x4d20,RequestFixed=0x4d24,RequestFirst=0x520,RequestSize=0x240,RequestForced=0x78,RequestFlags=0x238;
constexpr uint8_t ForcedBit=0x40;
constexpr uint32_t MaxComponents=256,MaxGoals=4096,RequestCount=32;
constexpr uint32_t MaxOffsetCm=5000,RepeatBase=1000000;   // mulegoto <cm>, or <cm + RepeatBase> for the repeated write
constexpr double MinPorterM=6,MinWalkM=1,MaxReachM=200,MaxPositionM=100000;
constexpr uint64_t RepeatMs=5000,WatchMs=3000,LogMs=500;
using Vec3=std::array<double,3>;
// Every link of the chain; a value is absent when its read failed or the link before it did.
struct Chain {
    uint64_t actor=0;
    std::optional<uint64_t> entity,component,componentVtable;      // component: 0 = the entity has no AI component
    std::optional<uint64_t> ai,aiAgain,aiVtable;                   // by component +0x50 and +0x80
    std::optional<uint8_t> state;
    std::optional<uint64_t> goal;                                  // 0 = the table was read and holds no walk goal
    std::optional<Vec3> destination,destinationCopy;
    std::optional<uint64_t> d,dVtable;
    std::optional<int32_t> index;
    std::optional<uint64_t> request;
    std::optional<Vec3> forced;
    std::optional<uint32_t> flags;
    bool agree() const {return ai&&aiAgain&&*ai&&*ai==*aiAgain;}
};
Chain read(const discovery::Sampler&,uint64_t base,uint64_t actor);
// The fields of one log line after the MULE's own (key, position): fixed order, `unreadable` for an absent link.
std::string describe(const Chain&,uint64_t base);
// mulegoto's argument: the offset east in centimetres and whether the write repeats; nothing when over the bound.
struct Ask { uint32_t cm=0; bool repeat=false; };
std::optional<Ask> ask(uint32_t argument);
using Writable=std::function<bool(uint64_t at,size_t n)>;          // plain committed read-write memory, all of it
using Poke=std::function<void(uint64_t at,const void* from,size_t n)>;
// Why this MULE cannot be given a forced destination, or empty when it can: it has a body and walks (a walk goal
// with a destination over MinWalkM from where it stands), it is MinPorterM or more from the porter, every link is
// the object it should be, and the request lies in writable memory that begins at D. `first` = the write that starts
// an experiment: a repeated write goes on when the MULE has arrived.
std::string refusal(const discovery::Sampler&,uint64_t base,const Chain&,const Vec3& mule,const Vec3& porter,const Writable&,bool first=true);
// The MULE's own destination moved east.
Vec3 east(const Vec3& destination,uint32_t cm);
// Writes `destination` as the forced destination of the chain's request and sets its bit. The chain is read again
// and has to be the same objects (`was`), and still pass `refusal`; the destination is at most MaxReachM from the
// MULE. Returns the reason when nothing was written.
std::string force(const discovery::Sampler&,uint64_t base,const Chain& was,const Vec3& mule,const Vec3& porter,const Vec3& destination,bool first,const Writable&,const Poke&);
// Windows: VirtualQuery for Writable and a copy for Poke; elsewhere they refuse and do nothing.
Writable nativeWritable();
Poke nativePoke();
}
