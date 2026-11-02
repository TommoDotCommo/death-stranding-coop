#include "providers.h"
#include "timing.h"
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
    else o<<",\"reason\":\""<<reason_<<'"';
    o<<"},\"remote\":{\"visible\":"<<(r.visible?"true":"false")<<",\"stale\":"<<(r.stale?"true":"false")<<",\"snapped\":"<<(r.snapped?"true":"false");
    if(r.visible) o<<",\"x\":"<<r.state.position[0]<<",\"y\":"<<r.state.position[1]<<",\"z\":"<<r.state.position[2]<<",\"yaw_deg\":"<<deg(r.state.yaw);
    o<<"}}";
    log_->record(us,o.str());
}
}
