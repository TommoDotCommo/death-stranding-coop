#pragma once
// Shared structures, first version: a structure one porter builds (a ladder, a climbing rope, a post, a bridge...)
// appears in the partner's world as the game's own "Net" structure, the variant the game uses for other players'
// structures. Read side (read-only): the construction manager (global image+7bf4960) keeps one 0x710-byte record per
// structure in a table that grows downwards from manager+0x5699c8 (live 2026-11-07): +0 a pointer into the image
// (the record class) with the same pointer +0x10 stored at +0x10, +18 id (u32), +1c type (EDSConstructionPointType),
// +1d category, +1e state (2 built, 1 the game's own, 0 refused or pending), +28 the world transform (3 f64 position,
// 9 f32 rotation rows). Ids: 10000..99999 are structures this porter built, 100000 and up "Net" structures (ours
// from the partner included), lower ids the game's own. Write side: CreateConstruction(type, transform, false)
// (game_offsets.md, "Structures").
#include "discovery.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace mod::structures {
struct Transform { std::array<double,3> position{}; std::array<float,9> rotation{}; };   // game axes, rows
struct Record { uint32_t id=0; uint8_t type=0, category=0, state=0; Transform at; };
constexpr uint32_t PlayerFirst=10000,NetFirst=100000;
constexpr uint64_t TableEnd=0x5699c8+0x710,Stride=0x710;constexpr unsigned MaxRecords=640;
// Every record in the table window (the image range is [base, base+imageSize)).
std::vector<Record> readTable(const discovery::Sampler&,uint64_t manager,uint64_t base,uint64_t imageSize);
// Player-built type -> the Net variant shown for another player's structure; nothing for types without one.
std::optional<uint8_t> netVariant(uint8_t playerType);
// A structure as it travels: type (the builder's Player variant), position, and the rotation as yaw/pitch/roll
// (radians; rows are right, forward, up). pack/unpack put type, pitch and roll into one u32 (8 + 12 + 12 bits).
struct Built { uint8_t type=0; std::array<double,3> position{}; double yaw=0,pitch=0,roll=0; uint64_t id=0; };
std::array<float,9> rotation(double yaw,double pitch,double roll);
void angles(const std::array<float,9>&,double& yaw,double& pitch,double& roll);
uint32_t pack(const Built&);
Built unpack(uint32_t item,const std::array<double,3>& position,double yaw);
// True when a built structure of `type` already stands within `range` metres of `position` (the partner's structure
// was created here in an earlier session and saved: do not build it twice).
bool standing(const std::vector<Record>&,uint8_t type,const std::array<double,3>& position,double range=.5);
// The partner's structures built here as Net copies: a dismantle may arrive before the game has built the copy
// (construction is asynchronous), so it waits until a built Net structure of that type stands at the place, then is
// due (remove it there); it gives up after GiveUp ms.
class NetCopies {
public:
    static constexpr uint64_t GiveUp=30000;
    void built(uint64_t ref,uint8_t netType,const std::array<double,3>& at){copies_[ref]={netType,at,0};}
    bool known(uint64_t ref) const {return copies_.count(ref)!=0;}
    void dismantled(uint64_t ref,uint64_t now){auto it=copies_.find(ref);if(it!=copies_.end()&&!it->second.goneAt)it->second.goneAt=now?now:1;}
    // Places to clear now: dismantled copies that stand built here. Each is returned once.
    std::vector<std::array<double,3>> due(const std::vector<Record>&,uint64_t now);
private:
    struct Copy{uint8_t type=0;std::array<double,3> at{};uint64_t goneAt=0;};
    std::map<uint64_t,Copy> copies_;
};
class Sync {
public:
    // One call per observation. The first call only learns what exists. Returns structures this porter built since.
    std::vector<Built> observe(const std::vector<Record>&);
    // Ids of announced structures that are gone since (dismantled or destroyed): the partner removes its copy.
    std::vector<uint32_t> takeRemoved();
    size_t known() const {return seen_.size();}
private:
    std::map<uint32_t,uint8_t> seen_; bool primed_=false;
    std::map<uint32_t,bool> announced_; std::vector<uint32_t> removed_;
};
}
