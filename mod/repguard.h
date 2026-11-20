#pragma once
// Another player's body must not run its inventory display for THIS porter's cargo. The body is a DSPlayerAKukanEntity,
// and its DSPlayerAKukanInventoryComponentRep answers MsgEntityRepUpdate with 0x2747a90, which walks a global list of
// the local player's carried items (*(image+0x7be79b0)+0x170) and, for each, places it at one of the running entity's
// bones, shows or hides it by that entity's visibility, and sets its shader variables (StippleAlpha,
// Incandescence_Intensity, ...): deferred rep calls into the items' queues. Run by a body beside this porter's own
// update, those calls race with his (the deferred-call corruption, game_offsets.md "Deferred rep calls"; named live
// 2026-11-20 by the rep trace while an order was taken at a terminal: ~18,000 such calls from one body). The game's own
// resident double never runs it only because it always sleeps.
//
// The handler is reached only through the 5-byte jump 0x2759710 (jmp 0x2730b20 -> jmp 0x2747a90), the one entry of
// that rep's message table. The mod points that jump at a stub (padding at 0x275900f) into its own function, which
// returns at once for the mod's bodies and calls the game's handler for every other entity. Every byte is checked
// first; nothing is changed on a mismatch. The bodies are published each tick (game thread); the check runs on the
// game's workers and does not lock.
#include "discovery.h"
#include <cstdint>
#include <vector>
namespace mod::repguard {
constexpr size_t MaxBodies=72;
void bodies(const std::vector<uint64_t>& entities);   // the mod's bodies now (others, crowd, the partner's)
void install(discovery::Sampler&);                   // throws when it cannot; game thread
bool installed();
uint64_t skipped();                                  // handler calls skipped for bodies so far
}
