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
#include <chrono>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace mod::structures {
struct Transform { std::array<double,3> position{}; std::array<float,9> rotation{}; };   // game axes, rows
struct Record { uint32_t id=0; uint8_t type=0, category=0, state=0; Transform at; uint64_t built=0; uint32_t level=0; uint64_t object=0; std::array<int32_t,6> own{}; };   // own: materials this player delivered to it (+0x1f8, six kinds)   // built: the game's clock (clockNow); level: +0x6c (1 as built, 2 and up upgraded); object: the construction object (record - 0x18)
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
bool standing(const std::vector<Record>&,uint8_t type,const std::array<double,3>& position,double range=1.0);
// The partner's structures built here as Net copies: a dismantle may arrive before the game has built the copy
// (construction is asynchronous), so it waits until a built Net structure of that type stands at the place, then is
// due (remove it there); it gives up after GiveUp ms.
class NetCopies {
public:
    static constexpr uint64_t GiveUp=30000;
    void built(uint64_t ref,uint8_t netType,const std::array<double,3>& at){copies_[ref]={netType,at,0};}
    // The copy of that ref (its Net type and place), and the ref of the copy of that type at a place.
    std::optional<std::pair<uint8_t,std::array<double,3>>> find(uint64_t ref) const;
    std::optional<uint64_t> refAt(uint8_t netType,const std::array<double,3>& at) const;
    bool known(uint64_t ref) const {return copies_.count(ref)!=0;}
    void dismantled(uint64_t ref,uint64_t now){auto it=copies_.find(ref);if(it!=copies_.end()&&!it->second.goneAt)it->second.goneAt=now?now:1;}
    // A dismantle whose ref is not known here (either mod restarted since it was built): the Net structure of that
    // type at that place is cleared instead.
    void dismantledAt(uint8_t netType,const std::array<double,3>& at,uint64_t now){copies_[(uint64_t(1)<<63)|++unknown_]={netType,at,now?now:1};}
    // Places to clear now: dismantled copies that stand built here. Each is returned once.
    std::vector<std::array<double,3>> due(const std::vector<Record>&,uint64_t now);
private:
    struct Copy{uint8_t type=0;std::array<double,3> at{};uint64_t goneAt=0;};
    std::map<uint64_t,Copy> copies_;uint64_t unknown_=0;
};
// The game's clock for a record's build time: microseconds since year 1, UTC.
inline uint64_t clockNow(){
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count())+62135596800000000ull;
}
// Upgrades: a structure that reaches a higher level in one game (the builder's own, or the partner's copy of it,
// which either player can upgrade as in the game) is raised to that level in the other. Keyed by the structure's ref.
class LevelSync {
public:
    struct Level { uint64_t ref=0; uint32_t level=0; };
    // Structures whose level rose since the last look (a ref seen for the first time is learned), except what was
    // raised here for the partner.
    std::vector<Level> observe(const std::vector<Level>&);
    // The partner's structure reached a level: true when the local one (at localLevel) is below it.
    static bool wanted(uint32_t localLevel,const Level& l){return l.level>=2&&l.level<=MaxLevel&&localLevel>=1&&localLevel<l.level;}
    void applied(const Level& l){auto& a=applied_[l.ref];if(l.level>a)a=l.level;}
    void forget(){seen_.clear();}
    static constexpr uint32_t MaxLevel=8;
private:
    std::map<uint64_t,uint32_t> seen_,applied_;
};
// Materials: what a player delivers to a structure (his own, or the partner's copy, as in the game) is given to the
// same structure in the partner's game as delivered by others, so both can build and upgrade one structure together.
class MaterialSync {
public:
    struct Have { uint64_t ref=0; std::array<int32_t,6> own{}; };
    struct Delivered { uint64_t ref=0; uint8_t kind=0; int32_t amount=0; };
    static constexpr int32_t Most=1000000;
    // What was delivered here since the last look (a ref seen for the first time is learned; a number going down, or
    // several structures changing at one look, a loaded save, is not a delivery).
    std::vector<Delivered> observe(const std::vector<Have>&);
    static bool wanted(const Delivered& d){return d.kind<6&&d.amount>0&&d.amount<=Most;}
    void forget(){seen_.clear();}
private:
    std::map<uint64_t,std::array<int32_t,6>> seen_;
};
class Sync {
public:
    // since: only structures built from then on (Record::built, the game's clock) are announced, so the structures
    // of the save are not, however late the game brings them in after a load.
    explicit Sync(uint64_t since=0):since_(since){}
    // One call per observation. Returns structures this porter built in this session, each once.
    std::vector<Built> observe(const std::vector<Record>&);
    // On joining (share_saved_structures): every structure of this porter that stands, those of the save included,
    // so the partner's world gets them too. They count as announced (observe does not send them again).
    std::vector<Built> everything(const std::vector<Record>&);
    // Structures this porter built that are gone since (dismantled or destroyed), with type and place: the partner
    // removes its copy, by ref or, when the ref is not known there, by type and place. Includes structures built in
    // an earlier session. A structure counts as gone after RemovedLooks observations without it: the game takes a
    // record away for a moment when the player comes back into its range.
    static constexpr unsigned RemovedLooks=6;
    struct Removed { uint32_t id=0; uint8_t type=0; std::array<double,3> position{}; };
    std::vector<Removed> takeRemoved();
    size_t known() const {return seen_.size();}
private:
    uint64_t since_=0;
    std::map<uint32_t,uint8_t> seen_;
    std::map<uint32_t,uint64_t> announced_;   // id -> build time announced (an id can come back as another structure after a load)
    struct Known { uint8_t type=0; std::array<double,3> position{}; unsigned missing=0; };
    std::map<uint32_t,Known> standing_; std::vector<Removed> removed_;
};
}
