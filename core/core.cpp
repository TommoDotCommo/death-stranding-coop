#include "core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ds {
namespace {
constexpr double Pi=3.14159265358979323846;
void put(Bytes& b,size_t at,uint64_t v,size_t n) { for(size_t i=0;i<n;++i) b[at+i]=uint8_t(v>>(8*i)); }
uint64_t get(const uint8_t* b,size_t at,size_t n) { uint64_t v=0; for(size_t i=0;i<n;++i) v|=uint64_t(b[at+i])<<(8*i); return v; }
int64_t signedGet(const uint8_t* b,size_t at,size_t n) { uint64_t v=get(b,at,n); return (v & (uint64_t(1)<<(n*8-1))) ? int64_t(v)-int64_t(uint64_t(1)<<(n*8)) : int64_t(v); }
uint32_t checksum(const uint8_t* b,size_t n=WireSize) { uint32_t h=2166136261u; for(size_t i=0;i<n;++i) { h^=(i>=120 && i<124)?0:b[i]; h*=16777619u; } return h; }
double distance(const State& a,const State& b) { double d=0; for(int i=0;i<3;++i) d+=std::pow(a.position[i]-b.position[i],2); return std::sqrt(d); }
double speed(const State& a) { double v=0; for(double x:a.velocity) v+=x*x; return std::sqrt(v); }
}
bool vehicleValid(const State& s) {
    if(s.vehicle>VehicleKindMax || bool(s.flags&Vehicle)!=(s.vehicle!=0)) return false;
    for(double x:s.vehicleOffset) if(!std::isfinite(x)||std::abs(x)>VehicleOffsetMax||(!s.vehicle&&x!=0)) return false;
    for(double x:s.vehicleAngles) if(!std::isfinite(x)||std::abs(x)>Pi+1e-9||(!s.vehicle&&x!=0)) return false;
    if(!std::isfinite(s.steer)||std::abs(s.steer)>SteerMax||(!s.vehicle&&s.steer!=0)) return false;
    if(s.vehicleVariant>VehicleVariantMax||(!s.vehicle&&(s.vehicleVariant||s.vehicleLook))) return false;
    return true;
}
namespace {
bool valid(const State& s) {
    for(double x:s.position) if(!std::isfinite(x)||std::abs(x)>100000) return false;
    for(double x:s.velocity) if(!std::isfinite(x)||std::abs(x)>100) return false;
    return std::isfinite(s.yaw)&&std::abs(s.yaw)<=Pi+1e-9 && std::isfinite(s.phase)&&s.phase>=0&&s.phase<=1 && s.action<=31 && !(s.flags&~FlagMask) && speed(s)<=100.02;
}
Error parse(const uint8_t* b,size_t n,Packet& out,const Protection& p) {
    // ANIMATION is the one kind longer than the header (its payload follows it).
    bool long_=b && n>WireSize && n<=AnimationWireMax && b[6]==uint8_t(Kind::Animation);
    if(!b || (p.length && n!=WireSize && !long_)) return Error::Length;
    if(p.magic && get(b,0,4)!=0x31505344) return Error::Magic;
    if(p.version && get(b,4,2)!=1) return Error::Version;
    if(p.length && get(b,8,2)!=n) return Error::Length;
    if(p.checksum && get(b,120,4)!=checksum(b,long_?n:WireSize)) return Error::Checksum;
    if(p.reserved && (get(b,10,2)||b[95]||get(b,124,4))) return Error::Reserved;
    if(p.kind && (b[6]<1||b[6]>10)) return Error::Kind;
    if(p.role && (b[7]<1||b[7]>2)) return Error::Role;
    Packet q; q.kind=Kind(b[6]); q.role=Role(b[7]); q.game=uint32_t(get(b,12,4));
    std::copy(b+16,b+32,q.mod.begin());
    if(p.build && (q.game!=GameBuild||q.mod!=ModBuild)) return Error::Build;
    q.session=get(b,32,8); q.nonce=get(b,40,8); q.echo=get(b,48,8);
    if(!q.session||!q.nonce) return Error::Nonce;
    q.sequence=uint32_t(get(b,56,4)); q.state.flags=uint16_t(get(b,60,2)); q.state.action=b[62]; q.state.phase=b[63]/255.;
    q.stamp=get(b,64,8);
    // Limit clocks to 53 bits so integer milliseconds convert exactly to double.
    if(q.kind==Kind::Hello||q.kind==Kind::Ack) {
        q.progress={uint32_t(get(b,96,4)),uint32_t(get(b,100,4))};
        q.progressPolicy=ProgressPolicy(b[104]);
        if(p.range&&b[104]>1)return Error::Range;
        if(p.reserved&&(get(b,105,7)||get(b,112,8)))return Error::Reserved;
    } else if(q.kind==Kind::Presence) {
        if(p.reserved&&get(b,114,6))return Error::Reserved;
        q.state.vehicle=b[96];
        for(int i=0;i<3;++i) { q.state.vehicleOffset[i]=signedGet(b,98+2*i,2)/100.; q.state.vehicleAngles[i]=signedGet(b,104+2*i,2)*Pi/32767.; }
        q.state.steer=signedGet(b,110,2)/100.;
        q.state.vehicleVariant=b[97]; q.state.vehicleLook=uint16_t(get(b,112,2));
    } else if(q.kind==Kind::Animation) {
        if(p.reserved && (get(b,96,8)||get(b,104,8)||get(b,112,8))) return Error::Reserved;
        size_t at=WireSize;
        if(at+1>n) return Error::Length;
        size_t bools=b[at++]; if(bools>AnimationBoolBytes||at+2*bools+1>n) return Error::Length;
        std::copy(b+at,b+at+bools,q.animation.held.begin()); at+=bools;
        std::copy(b+at,b+at+bools,q.animation.rose.begin()); at+=bools;
        size_t floats=b[at++]; if(floats>AnimationFloats||at+5*floats!=n) return Error::Length;
        int last=-1;
        for(size_t i=0;i<floats;++i,at+=5) {
            unsigned k=b[at]; uint32_t bits=uint32_t(get(b,at+1,4)); float v; std::memcpy(&v,&bits,4);
            if(p.animationRange && (int(k)<=last||k>=AnimationFloats||!std::isfinite(v)||std::abs(v)>AnimationFloatMax||v==0)) return Error::Range;
            last=int(k); if(k<AnimationFloats) q.animation.floats[k]=v;
        }
    } else { q.t0=get(b,96,8); q.t1=get(b,104,8); q.t2=get(b,112,8); }
    if(p.time && (q.stamp>(1ull<<53)||q.t0>(1ull<<53)||q.t1>(1ull<<53)||q.t2>(1ull<<53))) return Error::Time;
    for(int i=0;i<3;++i) { q.state.position[i]=signedGet(b,72+4*i,4)/100.; q.state.velocity[i]=signedGet(b,84+2*i,2)/100.; }
    q.state.yaw=signedGet(b,90,2)*Pi/32767.; q.state.health=b[92]; q.state.stamina=b[93]; q.byeReason=b[94];
    if(p.range && (!valid(q.state)||q.byeReason>3)) return Error::Range;
    if(p.vehicle && q.kind==Kind::Presence && !vehicleValid(q.state)) return Error::Range;
    if(q.kind==Kind::Environment) {
        if(p.environmentRange && q.t0>=EnvironmentDayMs)return Error::Range;
        // t1: bit 32 = a rain sample follows in bits 0..15 (int16 thousandths); nothing else may be set.
        if(p.reserved && (q.t1&~((uint64_t(1)<<32)|0xffffu)))return Error::Reserved;
        if(p.reserved && q.t1 && !(q.t1>>32))return Error::Reserved;
        if(p.environmentRange && (q.t1>>32) && std::abs(int16_t(uint16_t(q.t1&0xffffu))/1000.f)>EnvironmentRainMax)return Error::Range;
        if(p.reserved && (q.t2||q.state.flags||q.state.action||b[63]||q.byeReason||
            q.state.position!=State{}.position||q.state.velocity!=State{}.velocity||q.state.yaw||
            q.state.health!=255||q.state.stamina!=255))return Error::Reserved;
    }
    out=q; return Error::None;
}
// Height between two snapshots: blend gentle slopes and steps, snap real discontinuities (ledges, falls, lifts).
// Live 2026-11-02: the former rule (any change over 5 mm snaps) snapped 25-30 % of samples on outdoor terrain, which a
// puppet would show as constant jumps. Vertical EXTRApolation stays forbidden (see the end of sampling).
constexpr double FloorStepMax=1.0;   // metres in one snapshot interval
constexpr double FloorSpeedMax=8.0;  // vertical metres per second
bool verticalJump(const Snapshot& a,const Snapshot& z) {
    double dy=std::abs(a.state.position[1]-z.state.position[1]);
    if(dy>FloorStepMax) return true;
    if(z.time<=a.time) return dy>0.005;
    return dy/(double(z.time-a.time)/1000.)>FloorSpeedMax;
}
Render sampling(const std::deque<Snapshot>& b,uint64_t now,uint32_t delay,uint32_t extrap,const Protection& p) {
    Render r; r.delayMs=delay; if(b.empty()) return r;
    const auto& latest=b.back();
    uint64_t age=now>=latest.time?now-latest.time:0;
    // A longer presentation delay (a slow link) moves the staleness limits with it: 500 and 1500 ms at the least delay.
    uint64_t slack=delay>120?delay-120:0;
    r.stale=p.stale && age>500+slack; r.visible=(!p.stale||age<=1500+slack) && !(latest.state.flags&PrivateRoom);
    uint64_t target=now>delay?now-delay:0;
    r.state=b.front().state;
    for(size_t i=1;i<b.size();++i) {
        const auto& a=b[i-1]; const auto& z=b[i];
        bool jump=(z.state.flags&Teleport)||(a.state.flags&PrivateRoom)||(z.state.flags&PrivateRoom)||distance(a.state,z.state)>8 || (p.floor && verticalJump(a,z));
        if(target<z.time) {
            r.snapped=jump;
            if(!jump && target>=a.time && z.time>a.time) {
                double t=double(target-a.time)/double(z.time-a.time);
                r.state=a.state;
                for(int k=0;k<3;++k) { r.state.position[k]+=(z.state.position[k]-a.state.position[k])*t; r.state.velocity[k]+=(z.state.velocity[k]-a.state.velocity[k])*t; }
                double dy=std::remainder(z.state.yaw-a.state.yaw,2*Pi);
                r.state.yaw=std::remainder(a.state.yaw+t*dy,2*Pi);
                if(a.state.action==z.state.action) r.state.phase=std::fmod(a.state.phase+t*std::remainder(z.state.phase-a.state.phase,1.)+1.,1.);
                if(p.vehicleLerp && a.state.vehicle && a.state.vehicle==z.state.vehicle) for(int k=0;k<3;++k) {
                    r.state.vehicleOffset[k]+=(z.state.vehicleOffset[k]-a.state.vehicleOffset[k])*t;
                    r.state.vehicleAngles[k]=std::remainder(a.state.vehicleAngles[k]+t*std::remainder(z.state.vehicleAngles[k]-a.state.vehicleAngles[k],2*Pi),2*Pi);
                }
                if(p.vehicleLerp && a.state.vehicle && a.state.vehicle==z.state.vehicle) r.state.steer=a.state.steer+(z.state.steer-a.state.steer)*t;
            }
            return r;
        }
        r.state=z.state; r.snapped=jump;
    }
    // Do not invent vertical motion: without collision geometry even a 1 cm Y blend can cross a floor.
    r.late=target>latest.time;
    if(target>latest.time && !(latest.state.flags&(Teleport|PrivateRoom))) {
        double dt=double(p.extrapolate?std::min<uint64_t>(target-latest.time,extrap):target-latest.time)/1000.;
        for(int k:{0,2}) r.state.position[k]=std::clamp(r.state.position[k]+r.state.velocity[k]*dt,-100000.,100000.);
    }
    return r;
}
}
const char* reason(Error e) {
    static const char* names[]={"ok","bad length","bad magic","protocol version mismatch","checksum mismatch","reserved bits","unknown kind","role mismatch","game/mod build mismatch","wrong session","nonce mismatch","out of range","duplicate/reordered sequence","invalid timestamp","impossible motion","teleport rate limit","not connected","story progress mismatch"};
    return names[static_cast<unsigned>(e)];
}
namespace {
bool sanitiseImpl(State& s,const Protection& p) {
    for(double x:s.position) if(!std::isfinite(x)) return !p.finite;
    for(double x:s.velocity) if(!std::isfinite(x)) return !p.finite;
    if(!std::isfinite(s.yaw)||!std::isfinite(s.phase)) return !p.finite;
    if(!p.clamp) return true;
    for(double& x:s.position) x=std::clamp(x,-100000.,100000.);
    for(double& x:s.velocity) x=std::clamp(x,-100.,100.);
    double v=speed(s); if(v>100) for(double& x:s.velocity) x*=100/v;
    s.yaw=std::remainder(s.yaw,2*Pi); s.phase=std::clamp(s.phase,0.,1.); s.flags&=FlagMask; s.action=std::min<uint8_t>(s.action,31);
    bool riding=s.vehicle>0&&s.vehicle<=VehicleKindMax;
    for(double x:s.vehicleOffset) if(!std::isfinite(x)) riding=false;
    for(double x:s.vehicleAngles) if(!std::isfinite(x)) riding=false;
    if(!riding||!std::isfinite(s.steer)) { s.vehicle=0; s.vehicleOffset={}; s.vehicleAngles={}; s.steer=0; s.vehicleVariant=0; s.vehicleLook=0; s.flags&=uint16_t(~Vehicle); return true; }
    if(s.vehicleVariant>VehicleVariantMax) { s.vehicleVariant=0; s.vehicleLook=0; }
    s.steer=std::clamp(s.steer,-SteerMax,SteerMax);
    for(double& x:s.vehicleOffset) x=std::clamp(x,-VehicleOffsetMax,VehicleOffsetMax);
    for(double& x:s.vehicleAngles) x=std::remainder(x,2*Pi);
    s.flags|=Vehicle; return true;
}
}
bool sanitise(State& s) { return sanitiseImpl(s,{}); }
#ifdef DS_TESTING
bool sanitiseForTest(State& s,Protection p) { return sanitiseImpl(s,p); }
#endif
Bytes encode(Packet q) {
    if(!sanitise(q.state)) throw std::invalid_argument("nonfinite state");
    Bytes b{}; put(b,0,0x31505344,4); put(b,4,1,2); b[6]=uint8_t(q.kind); b[7]=uint8_t(q.role); put(b,8,WireSize,2); put(b,12,q.game,4); std::copy(q.mod.begin(),q.mod.end(),b.begin()+16);
    put(b,32,q.session,8); put(b,40,q.nonce,8); put(b,48,q.echo,8); put(b,56,q.sequence,4); put(b,60,q.state.flags,2); b[62]=q.state.action; b[63]=uint8_t(std::lround(q.state.phase*255)); put(b,64,q.stamp,8);
    for(int i=0;i<3;++i) { put(b,72+4*i,uint32_t(int32_t(std::lround(q.state.position[i]*100))),4); put(b,84+2*i,uint16_t(int16_t(std::lround(q.state.velocity[i]*100))),2); }
    put(b,90,uint16_t(int16_t(std::lround(q.state.yaw*32767/Pi))),2); b[92]=q.state.health; b[93]=q.state.stamina; b[94]=q.byeReason;
    if(q.kind==Kind::Hello||q.kind==Kind::Ack) {
        put(b,96,q.progress.episode,4);put(b,100,q.progress.id,4);b[104]=uint8_t(q.progressPolicy);
    } else if(q.kind==Kind::Presence) {
        // Vehicle block: [96] kind, [97] 0, [98..103] offset cm (int16 x3), [104..109] yaw/pitch/roll (int16 x3),
        // [110..111] steer (Turn_Heading, centidegrees, int16), [97] variant, [112..113] paint word, [114..119] 0.
        b[96]=q.state.vehicle;
        for(int i=0;i<3;++i) { put(b,98+2*i,uint16_t(int16_t(std::lround(q.state.vehicleOffset[i]*100))),2); put(b,104+2*i,uint16_t(int16_t(std::lround(q.state.vehicleAngles[i]*32767/Pi))),2); }
        put(b,110,uint16_t(int16_t(std::lround(q.state.steer*100))),2);
        b[97]=q.state.vehicleVariant; put(b,112,q.state.vehicleLook,2);
    } else { put(b,96,q.t0,8); put(b,104,q.t1,8); put(b,112,q.t2,8); }
    put(b,120,checksum(b.data()),4); return b;
}
std::vector<uint8_t> encodeAnimation(const Packet& q) {
    Packet h=q; h.kind=Kind::Animation; h.t0=h.t1=h.t2=0;
    auto head=encode(h); std::vector<uint8_t> b(head.begin(),head.end());
    const auto& a=q.animation; size_t bools=0;
    for(size_t i=0;i<AnimationBoolBytes;++i) if(a.held[i]||a.rose[i]) bools=i+1;
    b.push_back(uint8_t(bools));
    b.insert(b.end(),a.held.begin(),a.held.begin()+bools); b.insert(b.end(),a.rose.begin(),a.rose.begin()+bools);
    size_t count=b.size(); b.push_back(0); uint8_t floats=0;
    for(size_t k=0;k<AnimationFloats;++k) {
        float v=a.floats[k]; if(v==0) continue;
        if(!std::isfinite(v)||std::abs(v)>AnimationFloatMax) throw std::invalid_argument("animation control out of range");
        uint32_t bits; std::memcpy(&bits,&v,4); b.push_back(uint8_t(k)); for(int i=0;i<4;++i) b.push_back(uint8_t(bits>>(8*i))); ++floats;
    }
    b[count]=floats;
    b[8]=uint8_t(b.size()); b[9]=uint8_t(b.size()>>8);
    uint32_t c=checksum(b.data(),b.size()); for(int i=0;i<4;++i) b[120+i]=uint8_t(c>>(8*i));
    return b;
}
Error decode(const uint8_t* b,size_t n,Packet& q) { return parse(b,n,q,{}); }
#ifdef DS_TESTING
Error decodeForTest(const uint8_t* b,size_t n,Packet& q,Protection p) { return parse(b,n,q,p); }
#endif
Render sample(const std::deque<Snapshot>& b,uint64_t now,uint32_t delay,uint32_t extrap) { return sampling(b,now,delay,extrap,{}); }
uint16_t portForSlot(unsigned slot) { if(slot>=16) throw std::out_of_range("slot must be 0..15"); return uint16_t(49000+slot); }
Core::Core(Config c,StateProvider& p,Transport& t):config_(c),provider_(p),transport_(t) {
    delay_=c.delayMs;
    if(!c.session||!c.nonce||c.delayMs>1000||(c.role!=Role::Host&&c.role!=Role::Client)) throw std::invalid_argument("invalid config");
}
ProgressStatus compareProgress(Progress a,Progress b) {
    if((a.episode!=UnknownProgress&&b.episode!=UnknownProgress&&a.episode!=b.episode)||
       (a.id!=UnknownProgress&&b.id!=UnknownProgress&&a.id!=b.id))return ProgressStatus::Mismatch;
    if(a.episode==UnknownProgress||b.episode==UnknownProgress||a.id==UnknownProgress||b.id==UnknownProgress)return ProgressStatus::Unknown;
    return ProgressStatus::Equal;
}
const char* progressReason(ProgressStatus s) {
    return s==ProgressStatus::Mismatch?"mismatch":s==ProgressStatus::Equal?"equal":"unknown";
}
void Core::send(Kind k,uint64_t now) {
    Packet q; q.kind=k; q.role=config_.role; q.session=config_.session; q.nonce=config_.nonce; q.echo=peer_; q.stamp=now;
    if(k==Kind::Hello||k==Kind::Ack){q.progress=provider_.readProgress();q.progressPolicy=config_.progressPolicy;}
    if(k==Kind::Hello) { q.echo=0; sentHello_=true; }
    if(k==Kind::Ping) { q.t0=now;
        if(!protections_.pingHistory)pendingPings_.clear();
        pendingPings_.push_back(now);
        while(pendingPings_.size()>16)pendingPings_.pop_front(); }
    transport_.send(encode(q));
}
void Core::reset(bool rotate) {
    connected_=false; sentHello_=false; peer_=0; snapshots_.clear(); haveSequence_=false; haveTeleport_=false; haveSent_=false; clockValid_=false; pendingPings_.clear(); rtt_=-1; offset_=0; lateness_.clear(); delay_=config_.delayMs; pongs_.clear();
    nextHello_=nextSend_=nextPing_=0; outgoing_=0;
    haveEnvironment_=false;environmentOut_=environmentIn_=0;environmentReceived_=environmentStamp_=0;
    animations_.clear();haveAnimation_=false;sentAnimationOnce_=false;animationOut_=animationIn_=animationTaken_=0;animationStamp_=0;
    // Send/apply rate limits survive reconnects; changing a nonce cannot accelerate native calls.
    localProgress_={};peerProgress_={};progressStatus_=ProgressStatus::Unknown;
    // A new connection numbers cargo from 1 again on both sides; undelivered events are kept and renumbered.
    cargoSeen_=0; cargoIn_.clear(); cargoNext_=0; for(auto& e:cargoOut_) e.sequence=++cargoNext_; nextCargo_=0;
    if(rotate) { if(config_.nonce==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("nonce exhausted"); ++config_.nonce; }
}
void Core::adaptDelay(uint64_t now) {
    double least=config_.delayMs, most=std::max<double>(config_.delayMs,DelayMaxMs), target=least;
    if(protections_.adapt && !lateness_.empty()) {
        auto sorted=std::vector<int32_t>(lateness_.begin(),lateness_.end()); std::sort(sorted.begin(),sorted.end());
        target=std::clamp(double(sorted[(sorted.size()-1)*95/100])+DelayMarginMs,least,most);
    }
    // Nothing is on screen yet during the first samples of a connection: take the target at once.
    double step=lateness_.size()<=DelayWarmup?most:DelaySlew*double(now>delayAt_?now-delayAt_:0);
    delay_=std::clamp(target,delay_-step,delay_+step); delay_=std::clamp(delay_,least,most); delayAt_=now;
}
void Core::tick(uint64_t now) {
    if(connected_ && protections_.timeout && now-lastReceive_>3000) reset(true);
    if(!connected_&&protections_.progressReady&&!provider_.readyForSession())return;
    adaptDelay(now);
    if(!connected_) { if(now>=nextHello_) { send(Kind::Hello,now); nextHello_=now+250; } return; }
    if(now>=nextPing_) { send(Kind::Ping,now); nextPing_=now+((clockValid_||!protections_.pingRetry)?1000:100); }
    if(!cargoOut_.empty() && now>=nextCargo_) { sendCargo(cargoOut_.front(),now); nextCargo_=now+200; }
    if(protections_.gate && provider_.isInCutsceneOrLoading()) return;
    State s; if(!provider_.readLocal(s)||!sanitiseImpl(s,protections_)) return;
    s.flags&=uint16_t(~ProgressMismatch);
    if(protections_.progressFlag&&progressStatus_==ProgressStatus::Mismatch)s.flags|=ProgressMismatch;
    bool changed=!haveSent_||distance(s,sentState_)>=0.01||std::abs(s.yaw-sentState_.yaw)>=0.01||s.flags!=sentState_.flags||s.action!=sentState_.action||std::abs(s.phase-sentState_.phase)>=1./255.||s.health!=sentState_.health||s.stamina!=sentState_.stamina||s.velocity!=sentState_.velocity||s.vehicle!=sentState_.vehicle||s.vehicleOffset!=sentState_.vehicleOffset||s.vehicleAngles!=sentState_.vehicleAngles||std::abs(s.steer-sentState_.steer)>=0.5||s.vehicleVariant!=sentState_.vehicleVariant||s.vehicleLook!=sentState_.vehicleLook;
    if(protections_.rate && (now<nextSend_ || (!changed && now<lastStamp_+500))) return;
    auto sampleTime=provider_.localSampleTime(now);
    if(sampleTime>now || (haveSent_ && sampleTime<=lastLocalSample_)) return;
    Packet q; q.kind=Kind::Presence; q.role=config_.role; q.session=config_.session; q.nonce=config_.nonce; q.echo=peer_; q.stamp=sampleTime; q.sequence=++outgoing_; q.state=s;
    transport_.send(encode(q)); sentState_=s; lastLocalSample_=sampleTime; haveSent_=true; lastStamp_=now; nextSend_=now+50;
}
Error Core::receive(const uint8_t* b,size_t n,uint64_t now) {
    Packet q; auto fail=[&](Error e){ error_=e; return e; };
    Error e=parse(b,n,q,protections_); if(e!=Error::None) return fail(e);
    if(protections_.role && q.role==config_.role) return fail(Error::Role);
    if(protections_.session && q.session!=config_.session) return fail(Error::Session);
    if(!connected_&&protections_.progressReady&&!provider_.readyForSession())return fail(Error::NotConnected);
    auto localProgress=provider_.readProgress();
    auto status=compareProgress(localProgress,q.progress);
    if(!protections_.progressUnknown&&status==ProgressStatus::Unknown)status=ProgressStatus::Mismatch;
    bool refuses=config_.progressPolicy==ProgressPolicy::Refuse || (protections_.progressPolicy&&q.progressPolicy==ProgressPolicy::Refuse);
    if(q.kind==Kind::Hello) {
        if(connected_ && q.nonce!=peer_) return fail(Error::Nonce);
        if(protections_.progress&&refuses&&status==ProgressStatus::Mismatch)return fail(Error::Progress);
        // HELLO itself never establishes pairing or refreshes liveness.
        Packet a; a.kind=Kind::Ack; a.role=config_.role; a.session=config_.session; a.nonce=config_.nonce; a.echo=q.nonce; a.stamp=now;a.progress=localProgress;a.progressPolicy=config_.progressPolicy; transport_.send(encode(a));
        return fail(Error::None);
    }
    if(protections_.nonce && q.echo!=config_.nonce) return fail(Error::Nonce);
    if(q.kind==Kind::Ack) {
        if(protections_.handshake && !sentHello_) return fail(Error::NotConnected);
        if(connected_) return fail(q.nonce==peer_?Error::None:Error::Nonce);
        if(protections_.progress&&refuses&&status==ProgressStatus::Mismatch)return fail(Error::Progress);
        localProgress_=localProgress;peerProgress_=q.progress;progressStatus_=status;++connectionSerial_;
        peer_=q.nonce; connected_=true; lastReceive_=now; return fail(Error::None);
    }
    if(protections_.handshake && !connected_) return fail(Error::NotConnected);
    if(protections_.nonce && q.nonce!=peer_) return fail(Error::Nonce);
    if(q.kind==Kind::Environment) {
        if(protections_.environmentRole && (config_.role!=Role::Client||q.role!=Role::Host))return fail(Error::Role);
        if(q.echo!=config_.nonce)return fail(Error::Nonce);
        uint32_t advance=q.sequence-environmentIn_;
        if(protections_.environmentOrder && (!q.sequence||(haveEnvironment_&&(advance==0||advance>=0x80000000u||q.stamp<=environmentStamp_))))return fail(Error::Duplicate);
        double mapped=double(q.stamp)-offset_;
        if(protections_.environmentFresh && (!clockValid_||mapped<0||std::abs(mapped-double(now))>2000))return fail(Error::Time);
        environment_={uint32_t(q.t0),(q.t1>>32)!=0,(q.t1>>32)?int16_t(uint16_t(q.t1&0xffffu))/1000.f:0.f};haveEnvironment_=true;environmentIn_=q.sequence;environmentStamp_=q.stamp;environmentReceived_=now;
        // Optional traffic cannot keep a dead presence connection alive.
        return fail(Error::None);
    }
    if(q.kind==Kind::Animation) {
        uint32_t advance=q.sequence-animationIn_;
        if(protections_.animationOrder && (!q.sequence||(haveAnimation_&&(advance==0||advance>=0x80000000u||q.stamp<=animationStamp_))))return fail(Error::Duplicate);
        double mapped=double(q.stamp)-offset_;
        if(!clockValid_||mapped<0||std::abs(mapped-double(now))>2000)return fail(Error::Time);
        uint64_t time=uint64_t(mapped); if(!animations_.empty()) time=std::max(time,animations_.back().time+1);
        animations_.push_back({q.animation,time,q.sequence}); while(animations_.size()>64) animations_.pop_front();
        haveAnimation_=true; animationIn_=q.sequence; animationStamp_=q.stamp;
        // Optional traffic cannot keep a dead presence connection alive.
        return fail(Error::None);
    }
    if(q.kind==Kind::Bye) { reset(true); return fail(Error::None); }
    if(q.kind==Kind::Ping) {
        Packet a=q; a.kind=Kind::Pong; a.role=config_.role; a.nonce=config_.nonce; a.echo=peer_; a.stamp=now; a.t1=now; a.t2=now; transport_.send(encode(a));
        // PING replay cannot hold a disconnected puppet alive; timeout follows presence/PONG.
        return fail(Error::None);
    }
    if(q.kind==Kind::Pong) {
        auto pending=std::find(pendingPings_.begin(),pendingPings_.end(),q.t0);
        if(protections_.ping && (pending==pendingPings_.end()||now<q.t0||q.t2<q.t1||q.t2-q.t1>now-q.t0||now-q.t0>2000)) return fail(Error::Time);
        if(pending!=pendingPings_.end())pendingPings_.erase(pending);
        rtt_=double(now-q.t0)-double(q.t2-q.t1);
        double candidate=((double(q.t1)-double(q.t0))+(double(q.t2)-double(now)))/2.;
        // Keep the established timeline stable; clock updates cannot reorder buffered snapshots.
        if(!clockValid_) { offset_=candidate; clockValid_=true; offsetAt_=now; }
        pongs_.push_back({rtt_,candidate}); while(pongs_.size()>ClockWindow) pongs_.pop_front();
        if(protections_.refine) {
            auto best=std::min_element(pongs_.begin(),pongs_.end()); double step=ClockSlew*double(now>offsetAt_?now-offsetAt_:0);
            offset_=std::clamp(best->second,offset_-step,offset_+step);
        }
        offsetAt_=now;
        lastReceive_=now; return fail(Error::None);
    }
    if(q.kind==Kind::Cargo) {
        if(q.state.action<1||q.state.action>CargoTypeMax||q.t0>0xffffffffull||!q.t1) return fail(Error::Range);
        bool next=q.sequence==cargoSeen_+1;
        if(next||!protections_.cargoOrder) {
            CargoEvent e; e.sequence=q.sequence; e.type=q.state.action; e.item=uint32_t(q.t0); e.position=q.state.position; e.yaw=q.state.yaw; e.id=q.t1; e.ref=q.t2;
            bool seen=std::find(deliveredIds_.begin(),deliveredIds_.end(),e.id)!=deliveredIds_.end();
            if(!seen||!protections_.cargoOnce) { cargoIn_.push_back(e); deliveredIds_.push_back(e.id); while(deliveredIds_.size()>4096) deliveredIds_.pop_front(); }
            cargoSeen_=q.sequence;
        }
        // Acknowledge the event just taken and any repeat of an older one (its acknowledgement may have been lost).
        if(next||q.sequence<=cargoSeen_||!protections_.cargoOrder) {
            Packet a; a.kind=Kind::CargoAck; a.role=config_.role; a.session=config_.session; a.nonce=config_.nonce; a.echo=peer_; a.stamp=now; a.sequence=q.sequence;
            transport_.send(encode(a));
        }
        return fail(next||q.sequence<=cargoSeen_?Error::None:Error::Duplicate);
    }
    if(q.kind==Kind::CargoAck) {
        if(!cargoOut_.empty() && (cargoOut_.front().sequence==q.sequence||!protections_.cargoAck)) { cargoOut_.pop_front(); nextCargo_=0; }
        return fail(Error::None);
    }
    uint32_t advance=q.sequence-incoming_;
    if(protections_.duplicate && haveSequence_ && (advance==0||advance>=0x80000000u)) return fail(Error::Duplicate);
    double mapped=double(q.stamp)-offset_;
    uint64_t localTime=clockValid_?uint64_t(std::max(0.,mapped)):now;
    if(protections_.time && (mapped<0 || (clockValid_ && std::abs(mapped-double(now))>2000) || (haveSequence_ && (q.stamp<=lastArrival_ || localTime<=snapshots_.back().time)))) return fail(Error::Time);
    bool teleport=(q.state.flags&Teleport)!=0;
    if(protections_.teleport && teleport && haveTeleport_ && now-lastTeleport_<5000) return fail(Error::Teleport);
    if(protections_.speed && haveSequence_ && !teleport) {
        double elapsed=double(q.stamp-lastArrival_)/1000.;
        if(distance(q.state,snapshots_.back().state)>100*std::min(elapsed,2.)+0.5) return fail(Error::Speed);
    }
    // Samples on a new clock mapping are admitted only once they advance the existing timeline.
    snapshots_.push_back({q.state,localTime,q.sequence});
    if(protections_.buffer) while(snapshots_.size()>64) snapshots_.pop_front();
    if(clockValid_) { lateness_.push_back(int32_t(std::clamp(double(now)-mapped,-2000.,2000.))); while(lateness_.size()>DelayWindow) lateness_.pop_front(); adaptDelay(now); }
    incoming_=q.sequence; haveSequence_=true; lastArrival_=q.stamp; lastReceive_=now; ++accepted_;
    if(teleport) { haveTeleport_=true; lastTeleport_=now; }
    return fail(Error::None);
}
void Core::queueCargo(CargoEvent e) {
    // Sender identity: a per-process counter in the low 20 bits, salted with 33 bits mixed from the nonce. It must stay
    // below 2^53 like every t-field (the receiver refuses larger values as invalid times): the old (nonce<<20)^n
    // overflowed for any real (random 64-bit) nonce, so the game dropped every cargo event (live 2026-11-07).
    if(!e.id) { uint64_t salt=(config_.nonce*0x9e3779b97f4a7c15ull)>>31; e.id=((salt<<20)|(++cargoIds_&0xfffff))&((1ull<<53)-1); if(!e.id) e.id=1; }
    e.sequence=++cargoNext_; cargoOut_.push_back(e);
}
std::vector<CargoEvent> Core::takeCargo() { std::vector<CargoEvent> out; out.swap(cargoIn_); return out; }
void Core::sendCargo(const CargoEvent& e,uint64_t now) {
    Packet q; q.kind=Kind::Cargo; q.role=config_.role; q.session=config_.session; q.nonce=config_.nonce; q.echo=peer_; q.stamp=now;
    q.sequence=e.sequence; q.state.action=e.type; q.t0=e.item; q.t1=e.id; q.t2=e.ref; q.state.position=e.position; q.state.yaw=e.yaw;
    transport_.send(encode(q));
}
Render Core::render(uint64_t now) const {
    auto r=sampling(snapshots_,now,protections_.delay?presentationDelayMs():0,150,protections_);
    r.state.flags&=uint16_t(~ProgressMismatch);
    if(protections_.progressFlag&&progressStatus_==ProgressStatus::Mismatch)r.state.flags|=ProgressMismatch;
    return r;
}
Render Core::latestPresence(uint64_t now) const { return sampling(snapshots_,now,0,0,protections_); }
void Core::frame(uint64_t now) { Render r=render(now); if(protections_.gate && provider_.isInCutsceneOrLoading()) r.visible=false; provider_.applyRemote(r); }
void Core::disconnect(uint64_t now) { if(connected_) send(Kind::Bye,now); reset(true); }
bool Core::sendEnvironment(Environment value,uint64_t now) {
    if(!connected_||config_.role!=Role::Host||value.timeOfDayMs>=EnvironmentDayMs)return false;
    if(value.haveRain&&!(std::isfinite(value.rain)&&std::abs(value.rain)<=EnvironmentRainMax))return false;
    if(protections_.environmentRate && sentEnvironment_&&(now<environmentSent_||now-environmentSent_<30000))return false;
    Packet q;q.kind=Kind::Environment;q.role=config_.role;q.session=config_.session;q.nonce=config_.nonce;q.echo=peer_;
    q.stamp=now;q.sequence=++environmentOut_;q.t0=value.timeOfDayMs;
    if(value.haveRain)q.t1=(uint64_t(1)<<32)|uint16_t(int16_t(std::lround(value.rain*1000)));
    transport_.send(encode(q));sentEnvironment_=true;environmentSent_=now;return true;
}
std::optional<Environment> Core::latestEnvironment(uint64_t now) const {
    if(!connected_||!haveEnvironment_||now<environmentReceived_||now-environmentReceived_>2000)return {};
    return environment_;
}
bool Core::sendAnimation(const Animation& a,uint64_t now) {
    if(!connected_) return false;
    if(protections_.gate && provider_.isInCutsceneOrLoading()) return false;
    if(protections_.rate && sentAnimationOnce_ && (now<animationSent_+50 || (a==sentAnimation_ && now<animationSent_+500))) return false;
    Packet q; q.kind=Kind::Animation; q.role=config_.role; q.session=config_.session; q.nonce=config_.nonce; q.echo=peer_;
    q.stamp=std::min(provider_.localSampleTime(now),now); q.sequence=++animationOut_; q.animation=a;
    transport_.sendLong(encodeAnimation(q)); sentAnimation_=a; sentAnimationOnce_=true; animationSent_=now; return true;
}
std::optional<AnimationShown> Core::takeAnimation(uint64_t now) {
    if(!connected_||animations_.empty()) return {};
    uint32_t delay=protections_.delay?presentationDelayMs():0; uint64_t target=now>delay?now-delay:0;
    size_t due=animations_.size();
    for(size_t i=0;i<animations_.size()&&animations_[i].time<=target;++i) due=i;
    if(due==animations_.size()) return {};
    AnimationShown r; r.animation=animations_[due].animation; r.sequence=animations_[due].sequence; r.animation.rose={};
    for(size_t i=0;i<=due;++i) {
        uint32_t advance=animations_[i].sequence-animationTaken_;
        if(animationTaken_&&(advance==0||advance>=0x80000000u)) continue;
        if(protections_.animationRose||i==due) for(size_t k=0;k<AnimationBoolBytes;++k) r.animation.rose[k]|=animations_[i].animation.rose[k];
        r.fresh=true;
    }
    animationTaken_=r.sequence;
    animations_.erase(animations_.begin(),animations_.begin()+long(due));
    if(target-animations_.front().time>1000) return {};
    return r;
}
}
