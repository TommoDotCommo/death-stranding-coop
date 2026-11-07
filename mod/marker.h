#pragma once
// The partner on the map, and pings (OFF by default; armed per process by the stamp-guarded `puppet.marker` command).
//
// The game's user markers (the pins a player places on the map, also drawn in the world) live in the marker manager
// (global ds.exe+0x7bfd940): a block of 50 records of 0x50 bytes at manager+0x5ef8, five groups of ten (a group is a
// single pin or a route of up to ten points). Record: +0x10 position (3 x f32, game axes, z up), +0x20 id (u64, a new
// one on every set), +0x2c bit 0 in use, +0x2d marker type (6 = user marker), +0x4c group (0..4), +0x4d point (0..9).
// Write side (internal functions, not script exports; game_offsets.md, "Map markers"):
//   set    ds.exe+0x2f22c90(manager, const float position[4], group, point, snap to ground, flag): replaces that point;
//   remove ds.exe+0x2f378b0(manager, record*): removes one record (and closes the gap in a route).
// User markers are written to the save (the game's load path reads all 50 back), so a mod marker present when the game
// saves is saved. The mod only ever uses point 0 of two groups it reserves, removes its markers whenever they should not
// show, and fingerprints their position (stamp(), below) so that one left in a save is recognised and removed the next time
// the feature is armed. A player's own pin is never moved or removed: a group the player uses is left alone.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace mod::marker {
constexpr unsigned Records=50, Groups=5, Points=10, RecordSize=0x50;
constexpr uint8_t UserMarkerType=6;
struct Record { uint32_t index=0; uint64_t id=0; std::array<float,3> position{}; uint8_t group=0, point=0; bool used=false; };
// The 50 user-marker records, read-only. Empty when the manager is absent or the block does not look like user markers.
std::vector<Record> readRecords(const discovery::Sampler&, uint64_t managerSlot);
// Mod markers carry a fingerprint: their position is nudged by a few float steps (at most a few centimetres) until a
// keyed hash of its three coordinates has its top 18 bits zero. A player's pin matches by chance once in 2^18.
std::array<float,3> stamp(const std::array<double,3>& gamePosition);
bool stamped(const std::array<float,3>&);
struct Api {
    std::function<std::vector<Record>()> read;
    std::function<void(const std::array<float,3>&,uint8_t group,uint8_t point)> set;   // on the game thread
    std::function<void(uint32_t index)> remove;                                         // record index 0..49
};
struct Ping { uint64_t ref=0; std::array<double,3> position{}; };   // game axes; ref below 2^53 (a wire t-field)
// A held key fires once, then not again until it is released and at least Cooldown ms have passed.
class KeyEdge {
public:
    static constexpr uint64_t Cooldown=1000;
    bool pressed(bool down,uint64_t now);
private:
    bool was_=false;uint64_t last_=0;bool fired_=false;
};
class Markers {
public:
    // Refresh: at most one move of the partner marker per second. Moved: it moves only when the partner has moved this
    // far (metres) from where it stands. PingLife: a ping is removed after a minute.
    static constexpr uint64_t Refresh=1000, PingLife=60000;
    static constexpr double Moved=2.0;
    Markers(Api api,std::function<void(const std::string&)> log,uint8_t partnerGroup=4,uint8_t pingGroup=3);
    void enable(bool on);   // off removes what the mod placed
    bool enabled() const {return enabled_;}
    // Once per game tick. partner: where the partner is in this world (game axes) while he is shown here; nothing when
    // disconnected, in his private room or an event. hidden: this porter is in an event or his private room.
    void tick(uint64_t now,const std::optional<std::array<double,3>>& partner,bool hidden);
    // The partner pinged: shown once per ref, however often the event arrives.
    void partnerPing(const Ping&,uint64_t now);
    // This porter pings: shown here and returned for the partner (nothing while the feature is off).
    std::optional<Ping> localPing(const std::array<double,3>& at,uint64_t now);
    void setRefBase(uint64_t base){nextRef_=base;}
    // Remove every marker the mod placed (needs the game thread, like tick).
    void clear();
    size_t sets() const {return sets_;}
    size_t removals() const {return removals_;}
    size_t duplicates() const {return duplicates_;}
private:
    struct Slot { uint8_t group=0; bool everSet=false, placed=false, pending=false; uint64_t id=0, setAt=0; uint32_t index=0;
                  std::array<float,3> at{}; std::array<double,3> goal{}; };
    void place(Slot&,const std::array<double,3>&,uint64_t now);
    void sync(Slot&,const std::vector<Record>&);
    void drop(Slot&,const std::vector<Record>&);
    bool foreign(const Slot&,const std::vector<Record>&) const;
    Api api_;std::function<void(const std::string&)> log_;
    Slot partner_,ping_;bool enabled_=false,swept_=false,takenNoted_=false;uint64_t pingAt_=0,nextRef_=1;
    std::optional<Ping> pendingPing_;
    std::deque<uint64_t> seen_;size_t sets_=0,removals_=0,duplicates_=0;
};
// Windows: is the key down while this process's window has the focus (never reads keys for another window).
bool keyDown(unsigned virtualKey);
// The game's own set/remove for the running pinned build (signatures checked); throws when they cannot be verified.
Api nativeApi(discovery::Sampler&);
}
