#pragma once
// The Boss Battle replay path, seen from four call sites (game_offsets.md, "Boss replays"). Installed for the lab
// (`puppet.cargo replaylog 1`, which also logs every record) and for `group_replays` (which needs the started mission
// and the menu code: groupreplay.h). Each call goes through a stub (12 bytes of padding near it) into the mod, which
// records what it sees and calls the game's function as before:
//
// - Accept: the Boss Battle menu's accept (0x30ee879 -> 0x307a4f0); rcx is the menu data D, whose +0x20 the game has
//   just set to the code (0x1f + row) and +0x30 is the row.
// - Blackboard: the private room's player logic hands the code on (0x27677ef -> 0x273fe10(rcx, 0x1a, 1, 0)); the code
//   is still D+0x20 (D = *(*(image+0x7c00128)+0x318)); rcx is recorded (which object the game passes).
// - Script: the room script's RequestMissionStart finds the mission (0x33c9c45 -> 0x333b890(S+0xa8, resource)); the
//   record found has the mission id at +0x38.
// - Start: every mission start request is checked (0x33b80bc -> 0x33b0bc0(S, id, 0)); the result is the check's.
//
// Every byte (the five-byte call, the callee's first bytes, the padding) is checked first; nothing is changed on a
// mismatch. The records are kept in a small ring the mod drains on the game thread.
#include "discovery.h"
#include <cstdint>
#include <vector>
namespace mod::replayhooks {
enum class Kind : uint8_t { Accept=1, Blackboard=2, Script=3, Start=4 };
// Accept: a=code, b=row. Blackboard: a=code, b=rcx, c=index. Script: a=resource, b=record, c=mission id (0: none).
// Start: a=mission id, b=the check's result (0 or 1).
struct Record { Kind kind=Kind::Accept; uint64_t a=0,b=0,c=0; };
constexpr size_t Ring=64;
void install(discovery::Sampler&);   // throws when it cannot; game thread
bool installed();
std::vector<Record> drain();         // the records since the last drain (the newest Ring)
int64_t lastMission();               // the last mission whose start passed the check (-1: none yet)
int32_t lastCode();                  // the last code the Boss Battle menu accepted (-1: none yet)
}
