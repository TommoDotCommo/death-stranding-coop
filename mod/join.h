#pragma once
#include "../core/core.h"
#include "discovery.h"
#include "puppet.h"
#include <functional>
#include <optional>
#include <string>
namespace mod::join {
enum class Role { None, Host, Guest };
struct Input {
    bool enabled=false, gameThread=false, connected=false, localGameplay=false;
    Role role=Role::None;
    uint64_t connection=0;
    ds::State local{};
    ds::Render host{}; // latest accepted presence, without interpolation; stale samples cannot authorize a join
};
// A plan, not permission to move Sam. Only Mover (join_move native) acts on it, through the game's player teleport.
struct Plan { std::array<double,3> position{}; double yaw=0, distance=0; };
class Decision {
    uint64_t first_=0;
    bool finished_=false;
public:
    std::optional<Plan> tick(const Input&);
    bool finished() const { return finished_; }
};
std::string wouldMove(const Plan&); // join_move log: the plan only
std::string willMove(const Plan&);  // join_move native: the plan the Mover is about to act on

// join_move: "log" (default) only logs the plan; "native" moves the porter once with the game's own player teleport.
enum class MoveMode { Log, Native };
std::optional<MoveMode> parseMoveMode(const std::string&);
// The local porter as read from the game, game axes (x, y, z up), entity+c8/+e0 layout.
struct Porter {
    uint64_t player=0, entity=0; bool readable=false;
    std::array<double,3> position{}; std::array<float,9> rotation{};
};
// Every hook is called on the game thread from the verified callback, immediately before and after the move.
struct MoveApi {
    std::function<Porter()> porter;                    // fresh read each call
    std::function<bool()> gameplay;                    // normal gameplay now: no event, cutscene, private room or loading
    std::function<std::optional<bool>()> riding;       // vehicle reader: true riding, false on foot, nullopt unknown
    std::function<void(uint64_t player,const puppet::Transform&)> teleport; // the game's player teleport (mode 2)
};
// A sanity box for the target, in adapter axes (x, Y up, z). The real playable bounds are not known statically.
struct Bounds { double horizontal=16000, low=-500, high=3000; };
// One native move per session: rechecks gameplay, vehicle, bounds and the porter right before the call, reads the
// porter back after it (within ArriveMetres of the target, same player and entity), then checks once more after
// SettleMs that he has not dropped more than ArriveMetres (a fall through ground that is not streamed in yet).
// Never retries, never moves back.
class Mover {
public:
    static constexpr double ArriveMetres=5;
    static constexpr uint64_t SettleMs=3000;
    enum class Stage { Ready, Settling, Done, Refused, Failed };
    explicit Mover(MoveApi api,Bounds bounds={}):api_(std::move(api)),bounds_(bounds) {}
    std::string start(const Plan&,uint64_t nowMs); // log lines, each ending in '\n'
    std::string tick(uint64_t nowMs);              // the settle readback, once
    Stage stage() const { return stage_; }
    bool used() const { return stage_!=Stage::Ready; }
private:
    MoveApi api_; Bounds bounds_; Stage stage_=Stage::Ready;
    uint64_t player_=0, entity_=0, settleAt_=0; std::array<double,3> target_{}, arrived_{};
};
// Code identity for the native move on the pinned build (ds.exe RVAs). Checked once at init against the loaded image:
// unique code prefixes, the override's call into the entity teleport, and both class vtables' type getter and slot.
struct MoveSites {
    uint64_t playerTeleport=0x2634f00, entityTeleport=0x235a010, placeOnGround=0x3737930;
    uint64_t playerVtable=0x3d5c860, playerType=0x262b920, entityVtable=0x3d7d0d8, entityType=0x2711b40;
    unsigned playerSlot=0xa8, entitySlot=0x20, callAt=0x7f;
};
// Empty when the image matches; otherwise the first reason it does not. Reads only.
std::string verifyMoveCode(const discovery::Sampler&,const MoveSites& sites={});
// The local porter through the validated player path (manager -> selected Player -> entity +0x70), with the
// position (+0xc8, 3 x f64) and rotation (+0xe0, 9 x f32) read twice; unreadable or torn reads give readable=false.
Porter readPorter(const discovery::Sampler&);
// Runtime identity of the objects passed to the teleport: the player is a PlayerGame and its entity a DSPlayerEntity.
bool movableIdentity(const discovery::Sampler&,uint64_t player,uint64_t entity,const MoveSites& sites={});
#ifdef _WIN32
// Calls verifyMoveCode first and throws on a mismatch. The returned call checks identity again before every use.
std::function<void(uint64_t,const puppet::Transform&)> nativeTeleport(const discovery::Sampler&);
// The same without ground placement (tests: an exact spot and heading, indoors too).
std::function<void(uint64_t,const puppet::Transform&)> nativeTeleportExact(const discovery::Sampler&);
#endif

// Read-only progress discovery for the pinned build. These sites describe statically reviewed code/data;
// callers must use the hash-validated Sampler. Tests supply a small fake image with the identical layout.
struct ProgressSites {
    uint64_t missionSlot=0x7c0b000, factsSlot=0x5794720, scope=0x4dd9600;
    uint64_t missionCode=0x335f0f0, integerCode=0x1afc710;
};
struct ProgressObservation {
    ds::Progress value{}; // episode = episode-resource array index; main-order/chapter id remains unknown
    uint32_t episodeHash=ds::UnknownProgress;
    std::string reason="unknown";
};
class ProgressReader {
    const discovery::Sampler& memory_;
    ProgressSites sites_;
public:
    explicit ProgressReader(const discovery::Sampler& s,ProgressSites sites={}):memory_(s),sites_(sites) {}
    ProgressObservation read() const;
};
}
