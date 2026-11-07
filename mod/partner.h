#pragma once
// A stand-in partner for testing with one game copy: it speaks the real protocol from the second copy's slot, so the
// game sees an ordinary partner, without a second game on the same GPU. Pure movement logic here, pinned by
// partner_test.cpp; partner.cpp is the network shell (coop-partner.exe).
//
// follow: the partner keeps a place beside the porter (FollowRight m to his right, FollowBack m behind), walking,
// running or sprinting to it like a player would, and turns to face where it goes (or the porter's way once there).
// replay: a recorded walk (presence-*.jsonl "local" rows) played back from beside the porter.
#include "../core/core.h"
#include <cmath>
#include <optional>
#include <string>
#include <vector>
namespace mod::partner {
constexpr double Pi=3.14159265358979323846;
constexpr double FollowRight=3,FollowBack=1,Arrived=.3,Walk=1.6,Run=3.6,Sprint=5.8,RunFrom=4,SprintFrom=12,TeleportFrom=60;
constexpr double Accel=4;   // m/s^2: speed changes like a person's, not instantly
// Adapter axes: x, y up, z. Facing yaw: forward (sin yaw, 0, cos yaw), right (cos yaw, 0, -sin yaw).
inline std::array<double,3> beside(const ds::State& porter){
    double y=porter.yaw;auto p=porter.position;
    return {p[0]+FollowRight*std::cos(y)-FollowBack*std::sin(y),p[1],p[2]-FollowRight*std::sin(y)-FollowBack*std::cos(y)};
}
// Test-fixture progress values and a fixed displacement of the follow target (adapter axes).
uint32_t progressValue(const std::string&); // decimal u32, or "unknown"; throws on malformed input
std::optional<ds::State> offsetTarget(const std::optional<ds::State>&,const std::array<double,3>&);
class Follower {
public:
    // porter: the game's porter as last received (nullopt before the first one). Returns the partner's state, or
    // nullopt while there is nobody to stand beside yet (nothing is sent: no partner at the world origin).
    std::optional<ds::State> step(const std::optional<ds::State>& porter,double dt){
        if(!porter)return have_?std::optional<ds::State>(state_):std::nullopt;
        auto goal=beside(*porter);
        if(!have_){have_=true;state_={};state_.position=goal;state_.yaw=porter->yaw;state_.flags=ds::Teleport;return state_;}
        state_.flags=0;
        double dx=goal[0]-state_.position[0],dz=goal[2]-state_.position[2],d=std::hypot(dx,dz);
#ifndef MUTANT_PARTNER_TELEPORT
        if(d>TeleportFrom){state_.position=goal;state_.velocity={};speed_=0;state_.yaw=porter->yaw;state_.flags=ds::Teleport;return state_;}
#endif
        double want=d<=Arrived?0:d>SprintFrom?Sprint:d>RunFrom?Run:Walk;
#ifndef MUTANT_PARTNER_ACCEL
        speed_=want>speed_?std::min(want,speed_+Accel*dt):std::max(want,speed_-2*Accel*dt);
#else
        speed_=want;
#endif
        double move=std::min(speed_*dt,d);
        if(d>1e-9&&move>0){state_.position[0]+=dx/d*move;state_.position[2]+=dz/d*move;state_.yaw=std::atan2(dx,dz);}
        else state_.yaw=porter->yaw;
        if(d<=Arrived)speed_=0;
        // Height follows the porter (no terrain here); the receiving side keeps the body on its own floor.
        state_.position[1]=porter->position[1];
        double v=dt>0?move/dt:0;
        state_.velocity={d>1e-9?dx/d*v:0,0,d>1e-9?dz/d*v:0};
        if(speed_>Run+.1)state_.flags|=ds::Sprint;
        return state_;
    }
    double speed() const {return speed_;}
private:
    ds::State state_{};bool have_=false;double speed_=0;
};
// One recorded row: adapter position, facing (radians) and time (ms from the first row).
struct Sample {double t=0;std::array<double,3> position{};double yaw=0;};
// Reads presence-*.jsonl lines ({"t_us":..,"local":{"available":true,"x":..,"y":..,"z":..,"yaw_deg":..}}); rows without
// an available local pose are skipped. Minimal parser for this one known shape.
std::vector<Sample> parseRecording(const std::string& text);
// Wire identity. The mod in the game sends its adapter identity (identity.h, WireBuild) in bytes 16..31 of every
// frame, accepts only frames that carry it, and gives the core the core's own ModBuild (mod.cpp send/accept). The
// partner does the same, or the game drops every partner frame (live 2026-11-07: the partner stayed "waiting").
void toWire(ds::Bytes&);                     // core frame -> wire frame (WireBuild, checksum redone)
bool fromWire(uint8_t* frame,size_t size);   // wire frame -> core frame; false (unchanged) for another identity
class Replay {
public:
    explicit Replay(std::vector<Sample> s):samples_(std::move(s)){}
    bool usable() const {return samples_.size()>=2;}
    std::array<double,3> first() const {return samples_.empty()?std::array<double,3>{}:samples_.front().position;}
    double span() const {return samples_.empty()?0:samples_.back().t;}   // ms; each loop starts with a jump
    // Elapsed ms since the replay started; it loops (the caller flags the jump at each seam, once).
    // origin: where the recording's first point is put.
    ds::State at(double ms,const std::array<double,3>& origin) const;
private:
    std::vector<Sample> samples_;
};
}
