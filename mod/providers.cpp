#include "providers.h"
#include "timing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace mod {
uint64_t nowMs() {
    return nowUs()/1000;
}
ds::State trajectory(uint64_t time,unsigned slot) {
    double t=double(time%600000)/1000.*(6.283185307179586/6.)+slot*.7;
    double w=6.283185307179586/6.; ds::State s;
    s.position={10*std::sin(t),0,5*std::sin(2*t)};
    s.velocity={10*w*std::cos(t),0,10*w*std::cos(2*t)};
    s.yaw=std::atan2(s.velocity[0],s.velocity[2]);
    s.action=1+uint8_t((time/2000)%3);s.phase=(time%1000)/1000.;return s;
}
double yawFromForward(double fx,double fy,double previous) {
    if(!(std::hypot(fx,fy)>1e-3)) return previous;
    return std::atan2(fx,fy);
}
ds::State stateFromPose(const Pose& p,double previousYaw) {
    ds::State s;
    s.position={p.position[0],p.position[2],p.position[1]};
    s.yaw=yawFromForward(p.rotation[3],p.rotation[4],previousYaw);
    return s;
}
std::array<double,3> anglesFromRotation(const std::array<float,9>& m) {
    // Rows: right (0..2), forward (3..5), up (6..8).
    double yaw=std::atan2(double(m[3]),double(m[4]));
    double pitch=std::asin(std::clamp(double(m[5]),-1.,1.));
    double roll=std::atan2(-double(m[2]),double(m[8]));
    return {yaw,pitch,roll};
}
std::array<float,9> rotationFromAngles(const std::array<double,3>& a) {
    double sy=std::sin(a[0]),cy=std::cos(a[0]),sp=std::sin(a[1]),cp=std::cos(a[1]),sr=std::sin(a[2]),cr=std::cos(a[2]);
    std::array<double,3> f0{sy,cy,0},r0{cy,-sy,0},z{0,0,1},f,u1,r,u;
    for(int i=0;i<3;++i){f[i]=f0[i]*cp+z[i]*sp;u1[i]=z[i]*cp-f0[i]*sp;}
    for(int i=0;i<3;++i){r[i]=r0[i]*cr-u1[i]*sr;u[i]=u1[i]*cr+r0[i]*sr;}
    return {float(r[0]),float(r[1]),float(r[2]),float(f[0]),float(f[1]),float(f[2]),float(u[0]),float(u[1]),float(u[2])};
}
Ride rideFrom(uint8_t kind,const std::array<double,3>& vehicle,const std::array<float,9>& rotation,const std::array<double,3>& rider) {
    Ride r;if(!kind||kind>ds::VehicleKindMax)return r;
    r.kind=kind;r.offset={vehicle[0]-rider[0],vehicle[2]-rider[2],vehicle[1]-rider[1]};r.angles=anglesFromRotation(rotation);
    return r;
}
Motion VelocityTracker::update(uint64_t us,const std::array<double,3>& position) {
    Motion m;
    if(have_ && us>t_) {
        double d=0;for(unsigned i=0;i<3;++i)d+=(position[i]-p_[i])*(position[i]-p_[i]);
        d=std::sqrt(d);
        if(d>JumpMetres) m.teleport=true;
        if(!m.teleport && us-t_<=MaxGapUs) {
            double dt=double(us-t_)/1e6,speed=d/dt;
            double scale=speed>MaxSpeed?MaxSpeed/speed:1.;
            for(unsigned i=0;i<3;++i)m.velocity[i]=(position[i]-p_[i])/dt*scale;
        }
    }
    if(!have_ || us>t_) {have_=true;t_=us;p_=position;}
    return m;
}
bool PresenceLog::record(uint64_t us,const std::string& line) {
    if(stopped_) return false;
    if(started_ && (us<last_ || us-last_<PeriodUs)) return false;
    started_=true;last_=us;
    std::string text=line+'\n';
    if(!budget_.append(text.size())) {stopped_=true;note_("presence log size cap reached; presence logging stopped\n");return false;}
    if(!write_(text)) {stopped_=true;note_("presence log write failed; presence logging stopped\n");return false;}
    return true;
}
bool GameProvider::readLocal(ds::State& s) {
    auto us=clock_();auto p=pose_();
    available_=p.available&&p.coherent;reason_=available_?"":p.reason;
    if(!available_) return false;
    s=stateFromPose(p,yaw_);yaw_=s.yaw;
    auto motion=tracker_.update(us,s.position);
    s.velocity=motion.velocity;
    if(motion.teleport) s.flags|=ds::Teleport;
    if(ride_) {
        auto r=ride_(p);
        if(r.kind) {s.vehicle=r.kind;s.vehicleOffset=r.offset;s.vehicleAngles=r.angles;s.steer=r.steer;s.flags|=ds::Vehicle;}
        lastRide_=r;
    }
    local_=s;
    return true;
}
void GameProvider::applyRemote(const ds::Render& r) {
    if(!log_) return;
    auto us=clock_();
    std::ostringstream o;o<<std::setprecision(9);
    auto deg=[](double rad){return rad*180./3.14159265358979323846;};
    o<<"{\"t_us\":"<<us<<",\"local\":{\"available\":"<<(available_?"true":"false");
    if(available_) o<<",\"x\":"<<local_.position[0]<<",\"y\":"<<local_.position[1]<<",\"z\":"<<local_.position[2]<<",\"yaw_deg\":"<<deg(local_.yaw)<<",\"speed\":"<<std::hypot(local_.velocity[0],local_.velocity[2]);
    if(available_&&local_.vehicle) o<<",\"vehicle\":"<<int(local_.vehicle)<<",\"vehicle_deg\":["<<deg(local_.vehicleAngles[0])<<','<<deg(local_.vehicleAngles[1])<<','<<deg(local_.vehicleAngles[2])<<']';
    else o<<",\"reason\":\""<<reason_<<'"';
    o<<"},\"remote\":{\"visible\":"<<(r.visible?"true":"false")<<",\"stale\":"<<(r.stale?"true":"false")<<",\"snapped\":"<<(r.snapped?"true":"false");
    o<<",\"flags\":"<<r.state.flags<<",\"gated\":"<<(isInCutsceneOrLoading()?"true":"false");   // partner's flags; our gate
    if(r.visible) o<<",\"x\":"<<r.state.position[0]<<",\"y\":"<<r.state.position[1]<<",\"z\":"<<r.state.position[2]<<",\"yaw_deg\":"<<deg(r.state.yaw);
    if(r.visible&&r.state.vehicle) o<<",\"vehicle\":"<<int(r.state.vehicle)<<",\"vehicle_offset\":["<<r.state.vehicleOffset[0]<<','<<r.state.vehicleOffset[1]<<','<<r.state.vehicleOffset[2]
        <<"],\"vehicle_deg\":["<<deg(r.state.vehicleAngles[0])<<','<<deg(r.state.vehicleAngles[1])<<','<<deg(r.state.vehicleAngles[2])<<']';
    o<<"}}";
    log_->record(us,o.str());
}
}
