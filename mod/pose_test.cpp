// Game-free tests for the read-only pose exchange. Every check names an "oracle" so test_pose_mutants.py can prove it fails
// when the protection under test is removed. Argument: one test name or "all".
#include "fake_memory.h"
#include "pose.h"
#include "providers.h"
#include "timing.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace mod::discovery;
using namespace fakemem;
using mod::Pose;
using mod::PoseReader;
namespace {
constexpr double Pi=3.14159265358979323846;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
bool near(double a,double b,double tol){return std::abs(a-b)<=tol;}
double deg(double rad){return rad*180./Pi;}
// Live numbers (launch 9): forward (m10,m11,m12) = (-0.131,-0.991,0); rows are orthonormal, row 0 is the right vector.
const float LiveRotation[9]={0.991f,-0.131f,0.f, -0.131f,-0.991f,0.f, 0.f,0.f,1.f};
Pose read(Fake& f,const char* oracle=nullptr) {auto m=f.sampler();m.scan(PinnedHash,PinnedHash);Pose p=PoseReader(m).read();(void)oracle;return p;}
void world(Fake& f,double x=1.5,double y=-2.5,double z=3.25) {f.select(2);f.pose(x,y,z,LiveRotation);}
void expectReason(Fake& f,const char* reason,const char* oracle) {auto p=read(f);check(!p.available&&p.reason==reason,oracle);check(!p.coherent,oracle);}
// A sampler whose memory changes once, right after the rotation block has been copied (a pointer swapped mid-read).
template<class F> void incoherent(F mutate,const char* oracle) {
    Fake f;world(f);bool done=false;
    Sampler m([&](uint64_t a,void* p,size_t n){bool ok=f.read(a,p,n);if(a==f.base+0x60e0&&!done){done=true;mutate(f);}return ok;},f.base);
    m.scan(PinnedHash,PinnedHash);auto p=PoseReader(m).read();
    check(done&&!p.available&&!p.coherent&&p.reason=="incoherent chain",oracle);
}
}
int main(int argc,char** argv) {
    try {
        std::string which=argc>1?argv[1]:"all";
        auto run=[&](const char* name,auto f){if(which=="all"||which==name)f();};
        // ---- (1) PoseReader -------------------------------------------------------------------------------------------
        run("pose_clean",[]{Fake f;world(f);auto p=read(f);
            check(p.available&&p.coherent&&p.reason.empty(),"pose oracle: a coherent live chain is available");
            check(p.position[0]==1.5&&p.position[1]==-2.5&&p.position[2]==3.25,"pose oracle: position is 3 x float64 at entity+0xc8");
            check(p.rotation[3]==-0.131f&&p.rotation[4]==-0.991f&&p.rotation[8]==1.f,"pose oracle: rotation is 3x3 float32 at entity+0xe0");});
        run("pose_manager_null",[]{Fake f;world(f);f.menu();expectReason(f,"manager null","pose oracle: manager null (main menu)");});
        run("pose_slow_path",[]{
            for(auto bad:{std::array<int64_t,4>{2,0,2,0},{2,1,3,0},{2,1,2,1}}){Fake f;world(f);f.select(int32_t(bad[0]),int32_t(bad[1]),int32_t(bad[2]),uint64_t(bad[3]));expectReason(f,"slow selection path","pose oracle: slow selection path is not followed");}});
        run("pose_bad_index",[]{for(int32_t index:{-1,64,1000}){Fake f;world(f);f.select(index);expectReason(f,"bad index","pose oracle: bad index");}});
        run("pose_index_selects_slot",[]{Fake f;world(f);f.put<int32_t>(0x30fc,0);expectReason(f,"player null","pose oracle: the index selects the player slot");});
        run("pose_player_null",[]{Fake f;world(f);f.put<uint64_t>(0x4000+0x48+16,0);expectReason(f,"player null","pose oracle: player null");});
        run("pose_entity_null",[]{Fake f;world(f);f.put<uint64_t>(0x5070,0);expectReason(f,"entity null","pose oracle: entity null");});
        run("pose_nonfinite_position",[]{Fake f;world(f);f.put<double>(0x60d0,std::nan(""));expectReason(f,"non-finite pose","pose oracle: non-finite position");});
        run("pose_nonfinite_rotation",[]{Fake f;world(f);f.put<float>(0x60e0+16,INFINITY);expectReason(f,"non-finite pose","pose oracle: non-finite rotation");});
        run("pose_not_human",[]{for(auto flags:{std::array<int,2>{0,0},{1,1},{2,0}}){Fake f;world(f);f.data[0x5078]=uint8_t(flags[0]);f.data[0x5079]=uint8_t(flags[1]);expectReason(f,"not local human","pose oracle: local-human flags must be 01 00");}});
        run("pose_unreadable",[]{Fake f;world(f);f.blocked=f.base+0x60e4;auto p=read(f);check(!p.available&&p.reason=="pose unreadable","pose oracle: unreadable pose");});
        run("pose_incoherent_entity",[]{incoherent([](Fake& f){f.put<uint64_t>(0x5070,f.base+0x7000);},"pose oracle: entity pointer changed during the read");});
        run("pose_incoherent_player",[]{incoherent([](Fake& f){f.put<uint64_t>(0x4000+0x48+16,f.base+0x7400);},"pose oracle: player pointer changed during the read");});
        run("pose_incoherent_manager",[]{incoherent([](Fake& f){f.put<uint64_t>(0x3000,f.base+0x7800);},"pose oracle: manager pointer changed during the read");});
        run("pose_torn",[]{Fake f;world(f);unsigned seen=0;   // the position block changes between the two guarded copies
            Sampler m([&](uint64_t a,void* p,size_t n){bool ok=f.read(a,p,n);if(a==f.base+0x60c8&&n==24&&(++seen%2))static_cast<unsigned char*>(p)[5]^=0x10;return ok;},f.base);
            m.scan(PinnedHash,PinnedHash);auto p=PoseReader(m).read();
            check(seen>=2&&!p.available&&p.reason=="pose unreadable","pose oracle: torn copies are never presented");});
        // ---- (2) conversion --------------------------------------------------------------------------------------------
        run("convert_axes",[]{Fake f;world(f,10,20,30);auto p=read(f);auto s=mod::stateFromPose(p);
            check(s.position[0]==10&&s.position[1]==30&&s.position[2]==20,"convert oracle: adapter (x, y=game z up, z=game y)");});
        run("yaw_live",[]{Fake f;world(f);auto s=mod::stateFromPose(read(f));
            check(near(deg(s.yaw),deg(std::atan2(-0.131,-0.991)),0.01),"yaw oracle: live forward (-0.131,-0.991) from row m10..m12 gives -172.47 degrees");
            // Walk heading -97.3 degrees (map convention atan2(dy,dx)); yaw = 90 - heading = -172.7 after wrapping.
            check(near(deg(s.yaw),std::remainder(90.+97.3,360.),0.5),"yaw oracle: matches the walk heading -97.3 degrees within 0.5 degree");});
        run("yaw_axes",[]{
            check(near(mod::yawFromForward(0,1,7),0,1e-12),"yaw oracle: game +y is yaw 0");
            check(near(deg(mod::yawFromForward(1,0,7)),90,1e-9),"yaw oracle: game +x is yaw +90");
            check(near(deg(mod::yawFromForward(0,-1,7)),180,1e-9)||near(deg(mod::yawFromForward(0,-1,7)),-180,1e-9),"yaw oracle: game -y is yaw 180");
            check(mod::yawFromForward(0,0,0.25)==0.25&&mod::yawFromForward(1e-6,0,0.25)==0.25,"yaw oracle: facing straight up keeps the previous yaw");});
        run("yaw_matches_velocity",[]{
            // Walk along heading -97.3 degrees at 4 m/s, 50 ms samples; yaw from the velocity direction atan2(vx,vz) in adapter
            // axes must equal the yaw from the forward row (the SyntheticProvider convention).
            mod::VelocityTracker t;double h=-97.3*Pi/180;mod::Motion m;
            for(unsigned i=0;i<4;++i)m=t.update(uint64_t(i)*50000,{100+4*std::cos(h)*0.05*i,10,50+4*std::sin(h)*0.05*i});
            check(near(std::hypot(m.velocity[0],m.velocity[2]),4,1e-6),"velocity oracle: 4 m/s walk");
            check(near(deg(std::atan2(m.velocity[0],m.velocity[2])),std::remainder(90.+97.3,360.),1e-6),"yaw oracle: velocity direction agrees with the yaw convention");
            Fake f;world(f);auto s=mod::stateFromPose(read(f));
            check(near(deg(std::atan2(m.velocity[0],m.velocity[2])),deg(s.yaw),0.5),"yaw oracle: forward row and walk velocity agree within 0.5 degree");});
        // ---- velocity ---------------------------------------------------------------------------------------------------
        run("velocity_basic",[]{mod::VelocityTracker t;auto a=t.update(1000000,{0,0,0});check(a.velocity[0]==0&&!a.teleport,"velocity oracle: first sample has zero velocity");
            auto b=t.update(1050000,{0.2,0.01,-0.1});check(near(b.velocity[0],4,1e-9)&&near(b.velocity[1],.2,1e-9)&&near(b.velocity[2],-2,1e-9)&&!b.teleport,"velocity oracle: finite difference of the last two samples");
            auto c=t.update(1100000,{0.2,0.01,-0.1});check(c.velocity[0]==0&&c.velocity[2]==0,"velocity oracle: standing still is zero (uses the last two samples only)");});
        run("velocity_clamp",[]{mod::VelocityTracker t;t.update(0,{0,0,0});auto m=t.update(50000,{1.5,0,0});   // 30 m/s, below the 5 m jump
            check(near(std::hypot(m.velocity[0],m.velocity[2]),20,1e-9)&&!m.teleport,"velocity oracle: clamped to 20 m/s");});
        run("velocity_gap",[]{mod::VelocityTracker t;t.update(0,{0,0,0});auto m=t.update(600000,{1,0,0});
            check(m.velocity[0]==0&&!m.teleport,"velocity oracle: zero after a gap over 500 ms (no teleport for a small move)");
            auto n=t.update(650000,{1.2,0,0});check(near(n.velocity[0],4,1e-9),"velocity oracle: resumes after the gap");
            mod::VelocityTracker u;u.update(0,{0,0,0});auto edge=u.update(500000,{1,0,0});check(near(edge.velocity[0],2,1e-9),"velocity oracle: exactly 500 ms is still a normal difference");});
        run("velocity_jump",[]{mod::VelocityTracker t;t.update(0,{0,0,0});auto m=t.update(50000,{0,0,6});
            check(m.velocity[2]==0&&m.teleport,"velocity oracle: a jump over 5 m is zero velocity plus a teleport flag");
            auto after=t.update(100000,{0,0,6.2});check(near(after.velocity[2],4,1e-9)&&!after.teleport,"velocity oracle: the sample after a jump is normal");
            mod::VelocityTracker g;g.update(0,{0,0,0});auto both=g.update(2000000,{0,0,9});check(both.teleport&&both.velocity[2]==0,"velocity oracle: a gap with a jump is a teleport");});
        run("provider_flow",[]{
            Fake f;world(f,100,50,10);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);uint64_t clock=1000000;
            mod::GameProvider g([&]{return PoseReader(m).read();},[&]{return clock;});ds::State s;
            check(g.readLocal(s)&&!g.isInCutsceneOrLoading()&&s.position[1]==10&&s.flags==0&&s.phase==0&&s.velocity[0]==0,"provider oracle: available pose, no gate, flags 0, phase 0");
            clock+=50000;f.pose(100.2,50,10,LiveRotation);g.readLocal(s);check(near(s.velocity[0],4,1e-6),"provider oracle: velocity from the last two reads");
            clock+=50000;f.pose(120,50,10,LiveRotation);g.readLocal(s);check((s.flags&ds::Teleport)&&s.velocity[0]==0,"provider oracle: a 20 m jump sets the teleport flag");
            check(ds::sanitise(s),"provider oracle: the state passes the core's sanitiser");});
        run("gate",[]{
            Fake f;world(f);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);uint64_t clock=0;
            mod::GameProvider g([&]{return PoseReader(m).read();},[&]{return clock+=1000;});ds::State s;
            f.menu();check(!g.readLocal(s),"gate oracle: no pose at the main menu");check(g.isInCutsceneOrLoading(),"gate oracle: gated while the pose is unavailable");check(g.reason()=="manager null","gate oracle: the reason is kept");
            f.game();check(g.readLocal(s)&&!g.isInCutsceneOrLoading(),"gate oracle: opens when the pose is available");
            f.put<uint64_t>(0x5070,0);check(!g.readLocal(s)&&g.isInCutsceneOrLoading(),"gate oracle: gated again after the entity vanishes (loading or death)");});
        run("no_writes",[]{
            Fake f;world(f);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);auto before=f.data;uint64_t clock=0;std::vector<std::string> lines;
            mod::PresenceLog log([&](const std::string& l){lines.push_back(l);return true;},1<<20,[](const char*){});
            mod::GameProvider g([&]{return PoseReader(m).read();},[&]{return clock+=300000;},&log);ds::State s;
            for(int i=0;i<10;++i){g.readLocal(s);ds::Render r;r.visible=true;g.applyRemote(r);}
            check(f.data==before,"memory oracle: reading and logging never modify the game memory image");check(!lines.empty(),"memory oracle: something was logged");});
        // ---- presence log -------------------------------------------------------------------------------------------------
        run("presence_rate",[]{
            Fake f;world(f);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);uint64_t clock=5000000;std::vector<std::string> lines;
            mod::PresenceLog log([&](const std::string& l){lines.push_back(l);return true;},1<<20,[](const char*){});
            mod::GameProvider g([&]{return PoseReader(m).read();},[&]{return clock;},&log);ds::State s;
            for(int i=0;i<100;++i){clock=5000000+uint64_t(i)*10000;g.readLocal(s);ds::Render r;r.visible=true;r.stale=false;r.state.position={1,2,3};r.state.yaw=1;g.applyRemote(r);}
            check(lines.size()==4,"presence oracle: 100 calls over 1 s log at most 4 lines");
            check(lines[0].find("\"remote\":{\"visible\":true,\"stale\":false,\"snapped\":false,\"flags\":0,\"gated\":false,\"x\":1,\"y\":2,\"z\":3")!=std::string::npos&&lines[0].find("\"local\":{\"available\":true")!=std::string::npos&&lines[0].back()=='\n',"presence oracle: line content (visible, stale, snapped, flags, gate, position, yaw, local pose)");});
        run("presence_cap",[]{
            std::vector<std::string> lines;size_t total=0;std::string noted;
            mod::PresenceLog log([&](const std::string& l){lines.push_back(l);total+=l.size();return true;},500,[&](const char* s){noted=s;});
            for(int i=0;i<100;++i)log.record(uint64_t(i)*300000,"{\"x\":"+std::to_string(i)+"}");
            check(total<=500&&!lines.empty()&&log.stopped(),"presence oracle: the size cap bounds the log and stops it");check(noted.find("size cap")!=std::string::npos,"presence oracle: the cap is reported");
            size_t n=lines.size();log.record(99000000,"{}");check(lines.size()==n,"presence oracle: nothing is written after the cap");});
        run("presence_write_failure",[]{
            unsigned calls=0;mod::PresenceLog log([&](const std::string&){++calls;return false;},1<<20,[](const char*){});
            log.record(0,"{}");log.record(1000000,"{}");check(calls==1&&log.stopped(),"presence oracle: a failed write stops the log without retrying");});
        // ---- (3) self tick schedule ---------------------------------------------------------------------------------------
        run("self_tick_rate",[]{
            mod::FixedRate r(50000);unsigned n=0;for(uint64_t t=0;t<=1000000;t+=1000)if(r.due(t))++n;
            check(n==21,"self_tick oracle: 20 Hz means one tick per 50 ms (21 including the first)");});
        run("self_tick_no_catch_up",[]{
            mod::FixedRate r(50000);check(r.due(0),"self_tick oracle: first tick");
            check(r.due(10000000),"self_tick oracle: one tick is due after a 10 s stall");
            unsigned burst=0;for(int i=0;i<100;++i)if(r.due(10000000))++burst;
            check(burst==0,"self_tick oracle: no replay of missed ticks after a stall");
            check(r.deadline>10000000&&r.deadline<=10050000,"self_tick oracle: the next deadline lies in the future");
            check(!r.due(10049999)&&r.due(10050000),"self_tick oracle: cadence resumes on the original grid");});
        std::cout<<"pose tests passed: "<<which<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
