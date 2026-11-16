#pragma once
// Offering the player's existing strand (the structures his save already holds) to a server of our own
// (game_offsets.md, "Offering the player's existing strand"). Game-free, not wired into the mod. The game itself does
// the upload when asked on its game thread while signed in:
// - an own record (owner byte 1) with no online id: 0x2c9ed40(object) queues the game's own createObject (online
//   object manager slot 0x28) and sets the record's retry timer to 120 s; the answer's id lands at object +0xb8. The
//   game does the same by itself when the porter is within 15 m of such a record.
// - an own record with an id of the original online service: manager slot 0x68 queues the game's "re-create", the
//   same body plus "id" (the old one), whose answer the game ignores (it keeps the old id).
// What this header adds: which records to offer and how (plan), and, for a mod that does not call the game, the body
// the game would send, made from the record's memory (body; not for climbing ropes, whose "ri" comes from a call),
// with the map cell the game computes (cell). The game also sends "mt", "rmt", "tags" and "ci" when a record has
// them; the body made here leaves them out, and "lp" (likes) is an input.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
namespace mod::offer_strand {
constexpr uint64_t QueueUpload=0x2c9ed40;            // (object): the game's own retry call
constexpr uint64_t OnlineGlobal=0x7c1abf0;           // the online object manager
constexpr unsigned SlotUpload=0x28,SlotRecreate=0x68;   // its virtual slots, (manager, object)
constexpr uint8_t Player=1;                          // owner byte of a Player variant (built by this porter)
constexpr unsigned MaxId=64;
// What the game's body is made from, read from a construction object (the table record - 0x18).
struct Object {
    uint8_t type=0,owner=0,state=0;                  // +0x34, +0xb2, record state (+0x36)
    bool held=false;                                 // +0xab bit 7: the game refuses to upload it
    uint32_t level=0;                                // +0x84
    std::array<double,3> position{};                 // +0x40
    std::array<float,9> rotation{};                  // +0x58, rows right, forward, up
    float angle=0;                                   // a bridge's, chiral bridge's or jump stand's own angle (radians)
    std::string online;                              // +0xb8, empty when on no server
};
std::optional<Object> read(const discovery::Sampler&,uint64_t object);
// The server letter the game's body carries for a construction type (Player variants and the post's Net variant, as
// the game's switch has them); 0 for a type it sends as "unknown".
char letter(uint8_t type);
enum class Plan { Skip, Upload, Recreate };
struct Decision { Plan plan=Plan::Skip; const char* why=""; };
Decision plan(const Object&);
// The map cell (x, y) of a position: tile size and tile counts are the game's data (game_offsets.md).
std::array<int32_t,2> cell(double x,double y,int32_t tile,int32_t width,int32_t height);
// The rest of the body that the game takes from its own functions, not from the record.
struct Placement { int32_t x=0,y=0,map=0,area=0,priority=0,alternative=0; int64_t likes=0; };
// The game's angles for its rotation rows, in its integer form (degrees x 10^5).
std::array<int32_t,3> angles(const std::array<float,9>&);
// The createObject body the game would send for this record ("id" added for a re-create); empty when it cannot be
// made from memory (a climbing rope, an unknown kind).
std::string body(const Object&,const Placement&,bool recreate=false);
}
