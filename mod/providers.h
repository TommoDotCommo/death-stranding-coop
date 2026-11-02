#pragma once
#include "../core/core.h"
#include "discovery.h"
#include "pose.h"
#include <array>
#include <string>
#include <functional>
namespace mod {
uint64_t nowMs();
ds::State trajectory(uint64_t time, unsigned slot);
class SyntheticProvider final : public ds::StateProvider {
    unsigned slot_;
public:
    ds::Render remote;
    explicit SyntheticProvider(unsigned slot):slot_(slot) {}
    bool readLocal(ds::State& s) override {s=trajectory(nowMs(),slot_);return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render& r) override {remote=r;}
};
// ---- Game provider: read-only pose exchange ----------------------------------------------------------------------------
// Adapter axes: x = game x, y (UP) = game z, z = game y. Yaw convention (the same as SyntheticProvider, which uses
// atan2(vx, vz)): yaw = atan2(forward.x, forward.z) in adapter axes = atan2(game fx, game fy), so yaw 0 faces game +y
// and a positive yaw turns toward game +x. The core only interpolates yaw along the shortest arc (std::remainder), so any
// consistent convention works; this one makes yaw agree with the velocity direction (atan2(vx, vz)) for a forward walk.
// Relation to a map heading h = atan2(game dy, game dx): yaw = 90 degrees - h.
double yawFromForward(double gameForwardX,double gameForwardY,double previous);   // previous is kept when facing straight up/down
ds::State stateFromPose(const Pose&,double previousYaw=0);   // position, yaw, phase 0, flags 0, velocity 0
struct Motion { std::array<double,3> velocity{}; bool teleport=false; };
// Finite difference of the last two coherent samples (adapter axes, metres, microseconds).
class VelocityTracker {
    bool have_=false; uint64_t t_=0; std::array<double,3> p_{};
public:
    static constexpr double MaxSpeed=20, JumpMetres=5;
    static constexpr uint64_t MaxGapUs=500000;
    Motion update(uint64_t us,const std::array<double,3>& position);
};
// At most 4 lines a second, bounded by the discovery Budget. A failed write or a full budget stops the log, not the mod.
class PresenceLog {
    std::function<bool(const std::string&)> write_;
    discovery::Budget budget_;
    std::function<void(const char*)> note_;
    uint64_t last_=0; bool started_=false, stopped_=false;
public:
    static constexpr uint64_t PeriodUs=250000;
    PresenceLog(std::function<bool(const std::string&)> write,uint64_t limitBytes,std::function<void(const char*)> note):write_(std::move(write)),budget_{limitBytes},note_(std::move(note)) {}
    bool stopped() const {return stopped_;}
    bool record(uint64_t us,const std::string& line);
};
class GameProvider final : public ds::StateProvider {
    std::function<Pose()> pose_; std::function<uint64_t()> clock_; PresenceLog* log_;
    VelocityTracker tracker_; bool available_=false; double yaw_=0; std::string reason_="not read"; ds::State local_{};
public:
    GameProvider(std::function<Pose()> pose,std::function<uint64_t()> clockUs,PresenceLog* log=nullptr):pose_(std::move(pose)),clock_(std::move(clockUs)),log_(log) {}
    // Unavailable (menu, loading, death, any failed read check) is a normal result, never a fabricated pose.
    bool readLocal(ds::State& s) override;
    // True while readLocal is unavailable. The visual cutscene gate itself is UNKNOWN: a cutscene with a valid pose is not gated.
    bool isInCutsceneOrLoading() const override {return !available_;}
    // NO game writes and NO actor: only logs the remote Render to the presence log.
    void applyRemote(const ds::Render& r) override;
    const std::string& reason() const {return reason_;}
};
}
