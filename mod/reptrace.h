#pragma once
// Lab only (armed by the stamp-guarded `puppet.cargo reptrace 1`): who writes into other entities' deferred rep-call
// queues from another player's body's update. Bodies joined to this porter's update group are safe but cost about
// 0.46 ms each per frame (they run one after another with him); unjoined, a body's update corrupted this porter's
// queue now and then (game_offsets.md, "Deferred rep calls"). To switch off the writers on the body instead, they have
// to be named: this records each distinct call stack that appends to a queue not the body's own while a body is being
// updated on that thread.
//
// How: the game's per-entity update 0x2402750(entity) is called from three places (0x23ffdf1, 0x2400383, 0x2401488);
// each call goes through a stub (padding at 0x23721fc) into the mod, which notes the entity for the thread and calls the
// update. The unlocked queue append 0x237a3f0(rep, call) (a 13-instruction leaf, reached by two tail jumps) is replaced
// by the mod's own copy of it, which also records. Every byte is checked first; nothing is changed on a mismatch.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace mod::reptrace {
constexpr size_t MaxBodies=64,MaxStacks=48,Frames=10;
struct Stack {uint64_t count=0;bool porter=false;std::array<uint64_t,Frames> at{};};   // return addresses as image offsets
// The bodies to watch and this porter's rep (entity+0x60), published every tick (game thread).
void watch(const std::vector<uint64_t>& bodies,uint64_t porterRep);
void install(discovery::Sampler&);   // throws when it cannot; game thread, between frames
bool installed();
std::vector<Stack> stacks();         // what was recorded so far
uint64_t appends();                  // every append through the mod's copy (it is on)
}
