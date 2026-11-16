#pragma once
// The player's own strand, read-only (own_strand.cpp; game_offsets.md, "The player's own strand"). Two places in a
// game's memory say what is this player's own:
// - the construction records (structures.h): the owner byte at object +0xb2 is 1 for a Player variant, which is what
//   the porter built in this save; 2 is another player's (Net), 0 the game's own. Object +0xb4 is the creator's user
//   id (1 = the signed-in player, live on every owner-1 record of a save; 0 = nobody known) and +0xb8 the online id
//   once the object is on a server (empty before). A server cannot hand a player his own objects back: the game sets
//   them to Fail when its save lacks them (live 2026-11-17).
// - the online object manager (image+0x7c1abf0): per area three lists of 0x28-byte elements (+8 id string, +0x10
//   EDSNetSyncObjectType, +0x11 state, +0x14 creator user id). The game files an element whose creator is user 1 in
//   the second list, everything else in the first, and moves failed ones (state 4 and up) to the third.
// Likes per object are not read from the game (where it keeps them is unknown); a server's counts can be merged in by
// online id. As a whole not wired into the running mod yet: a reader for the launcher's "My strand" and for a live
// probe. Its structure reader is what `build_received` scans with (received.h).
#include "discovery.h"
#include "structures.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace mod::own_strand {
constexpr uint64_t ConstructionGlobal=0x7bf4960,OnlineGlobal=0x7c1abf0;
constexpr uint32_t Self=1;                           // the user manager's id for the signed-in player
constexpr uint8_t Game=0,Player=1,Net=2;             // owner byte at object +0xb2
constexpr uint8_t NoKind=24;                         // EDSNetSyncObjectType "none"
constexpr uint8_t Failed=4;                          // element states from here up are Fail, AlwaysFail, Removed
constexpr unsigned MaxAreas=4096,MaxPerList=4096,MaxId=64;
// flags: the object's bytes +0xa8..+0xab (+0xaa 0x20: still needs the online placement check, 0x10: skip the range
// test, 0x40 and +0xab bit 0: marked for removal; game_offsets.md, "Received structures built").
struct Structure { structures::Record record; uint8_t owner=0; uint32_t creator=0; std::string online; std::array<uint8_t,4> flags{}; };
struct Online { std::string id; uint32_t area=0; uint8_t kind=NoKind,state=0,list=0; uint32_t creator=0; uint64_t address=0; };
// Which records are read further: `record` is asked before the object is read, `object` before the online id is;
// what either refuses is left out. Empty: every record.
struct Select { bool(*record)(const structures::Record&)=nullptr; bool(*object)(const Structure&)=nullptr; };
constexpr uint64_t ObjectFields=0xa8;                // flags +0xa8, owner +0xb2, creator +0xb4, online id +0xb8: one read
// Every construction record with its flags, owner byte, creator and online id (objects that cannot be read are left out).
std::vector<Structure> readStructures(const discovery::Sampler&,uint64_t base,uint64_t imageSize,Select={});
// Every element of every area's three lists.
std::vector<Online> readOnline(const discovery::Sampler&,uint64_t base);
bool mine(const Structure&);   // a built Player variant
bool mine(const Online&);      // creator user 1, not failed
// The server's object letter for a construction type (both variants), and for an EDSNetSyncObjectType; 0 when none.
char letter(uint8_t constructionType);
char syncLetter(uint8_t syncType);
// One object of the player's own strand. area 0 = not known (a structure that is on no server: the game's own area
// lookup, 0x3497b40, is a call, not a read); likes -1 = not known.
struct Item { char letter=0; std::string online; uint32_t area=0; bool placed=false; std::array<double,3> position{};
              uint8_t type=0; uint32_t level=0,id=0; int64_t likes=-1; const char* from=""; };
// The own strand: own structures (with the area of their online element when there is one) and own online objects
// that are no structure of this save, one item per object.
std::vector<Item> strand(const std::vector<Structure>&,const std::vector<Online>&);
// Likes from a server, by online id (objects the server does not know stay -1).
void likes(std::vector<Item>&,const std::map<std::string,int64_t>&);
std::string line(const Item&);   // one log line: letter, online id, area, position, level, likes, source
}
