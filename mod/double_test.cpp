#include "double.h"
#include "fake_memory.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace mod::puppet;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"double oracle: %s\n",#x);++failures;}}while(0)
// The game's side: one parked double owned by one player entity.
struct Scene {
    Anchor anchor;uint64_t twin=0x7000,found=0x7000,flags=FlagSleeping|0x551;mod::Pose pose;
    bool ignorePlace=false,ignoreWake=false,alterLocal=false;Transform mover{{0,0,0},{1,0,0,0,1,0,0,0,1}};double speed=-1;unsigned animations=0;uint32_t actions=0xffffffff;uint64_t network=0x500,upgradedFor=0;unsigned downgrades=0;bool effectsShown=true;unsigned effectCalls=0;
    std::vector<std::string> calls,events;
    Scene(){anchor.entity=0x1000;anchor.pose.available=anchor.pose.coherent=true;anchor.pose.position={10,20,30};anchor.pose.rotation={1,0,0,0,1,0,0,0,1};
        flags&=~FlagVisible;pose.available=pose.coherent=true;pose.rotation={1,0,0,0,1,0,0,0,1};}
    void call(const char* name,uint64_t e){CHECK(e==twin);calls.push_back(name);if(alterLocal)anchor.entity=0x999;}
    DoubleApi api(){return {[this]{return anchor;},[this](uint64_t p){return p==anchor.entity?found:0;},[this](uint64_t){return flags;},[this](uint64_t){return pose;},
        [this](uint64_t e,bool b){call(b?"sleep":"wake",e);if(!ignoreWake)flags=b?flags|FlagSleeping:flags&~FlagSleeping;},
        [this](uint64_t e,bool b){call(b?"show":"hide",e);flags=b?flags|FlagVisible:flags&~FlagVisible;},
        [this](uint64_t e,const Transform& t){call("place",e);if(!ignorePlace)pose.position=t.position;},
        [this](uint64_t e,const Transform& t){CHECK(e==twin);mover=t;},
        [this](uint64_t e,double v,uint32_t f){CHECK(e==twin);speed=v;actions=f;++animations;},
        [this](uint64_t e,uint64_t player){CHECK(e==twin);upgradedFor=player;auto old=network;network=0x600;return old;},
        [this](uint64_t e,uint64_t original){CHECK(e==twin&&original==0x500);network=original;++downgrades;},
        [this](uint64_t e,bool show){CHECK(e==twin);effectsShown=show;++effectCalls;}};}
    // One engine frame: an awake double is put where its FixedPositionMover says.
    void frame(){if(!(flags&FlagSleeping))pose.position=mover.position;}
    Double make(){Double d(api(),[this](const std::string& row){events.push_back(row);});return d;}
    bool last(const char* event) const {return !events.empty()&&events.back().find(std::string("\"")+event+"\"")!=std::string::npos;}
};
Transform at(double x){return {{x,2,3},{1,0,0,0,1,0,0,0,1}};}
using Calls=std::vector<std::string>;
void lifecycle(){
    Scene s;auto d=s.make();auto t=at(5);
    d.tick(true,&t);CHECK(s.calls.empty());                       // off until enabled
    d.enable();d.tick(false,&t);CHECK(s.calls.empty());           // only on the permitted game thread
    d.tick(true,nullptr);CHECK(s.calls.empty());                  // nothing to show
    d.tick(true,&t);CHECK((s.calls==Calls{"place","show","wake"}));CHECK(d.stage()==Double::Stage::Driving&&s.last("acquired"));
    auto u=at(6);s.frame();d.tick(true,&u);CHECK(s.calls.size()==3&&s.mover.position[0]==6);   // no teleport while driving
    s.frame();CHECK(s.pose.position[0]==6);
    d.tick(true,nullptr);CHECK((Calls(s.calls.begin()+3,s.calls.end())==Calls{"place","hide","sleep"}));
    CHECK(d.stage()==Double::Stage::Parked&&s.pose.position[0]==0&&(s.flags&FlagSleeping)&&!(s.flags&FlagVisible)&&s.last("released"));
    d.tick(true,&t);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==9);   // can be taken again
}
void mover(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(s.mover.position[0]==5);
    for(double x:{6.,7.,8.}){s.frame();auto u=at(x);d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.mover.position[0]==x);}
    d.tick(true,nullptr);CHECK(s.mover.position[0]==0&&d.stage()==Double::Stage::Parked);s.frame();CHECK(s.pose.position[0]==0);
}
void animation(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t,2.5);CHECK(s.speed==2.5&&s.animations==1);
    auto near=[](double a,double b){return std::fabs(a-b)<1e-9;};
    auto u=at(6);d.tick(true,&u,0);CHECK(near(s.speed,1.5));s.frame();d.tick(true,&u,NAN);CHECK(near(s.speed,.9));   // smoothed, NaN counts as 0
    s.frame();d.tick(true,&u,1e9);CHECK(near(s.speed,.6*.9+.4*20));                                                 // capped at 20
    s.frame();d.tick(true,&u,3);CHECK(near(s.speed,.6*(.6*.9+8)+1.2));s.frame();d.tick(true,nullptr,3);CHECK(s.speed==0&&d.stage()==Double::Stage::Parked);   // stands again when released
}
void network(){
    {Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(s.network==0x500&&!s.upgradedFor);}   // off by default
    for(int ending=0;ending<3;++ending){
        Scene s;auto d=s.make();d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
        CHECK(s.upgradedFor==s.anchor.entity&&s.network==0x600&&d.stage()==Double::Stage::Driving);
        if(ending==0){d.tick(true,nullptr);CHECK(s.network==0x500&&s.downgrades==1);}             // release
        if(ending==1){s.pose.position[0]=40;auto u=at(6);d.tick(true,&u);CHECK(s.network==0x500&&d.stage()==Double::Stage::Yielded&&s.mover.position[0]==0);}   // game takes it
        if(ending==2){s.anchor.entity=0x2000;s.found=0x7000;d.tick(true,&t);CHECK(s.downgrades==0);}   // world gone: nothing to restore
    }
}
void refusedUpgrade(){
    Scene s;auto api=s.api();api.upgrade=[](uint64_t,uint64_t)->uint64_t{throw std::runtime_error("layout");};
    Double d(api,[&](const std::string& r){s.events.push_back(r);});d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
    CHECK(d.stage()==Double::Stage::Driving);d.tick(true,nullptr);CHECK(s.downgrades==0&&d.stage()==Double::Stage::Parked);
}
void actions(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t,3,FlagSprintBit|FlagCrouchBit|0x20|JumpBit|(1u<<21));
    CHECK(s.actions==(FlagSprintBit|FlagCrouchBit|JumpBit));               // only the replayed bits pass
    s.frame();auto u=at(6);d.tick(true,&u,3,FlagCrouchBit);CHECK(s.actions==FlagCrouchBit);
    d.tick(true,nullptr,3,FlagSprintBit);CHECK(s.actions==0&&s.speed==0);  // released standing
    // Reading the porter's own variables: entity 0x6000 -> components -> manager -> named slots.
    fakemem::Fake f;auto B=f.base;
    f.put<uint32_t>(0x6090,2);f.put<uint64_t>(0x6098,B+0x7000);f.put<uint64_t>(0x7000,B+0x7100);f.put<uint64_t>(0x7008,B+0x7200);
    f.put<uint64_t>(0x7100,B+0x1f00);f.put<uint64_t>(0x7200,B+0x1f40);f.put<uint64_t>(0x7248,B+0x6000);   // second is the animator
    f.put<uint32_t>(0x7250,3);f.put<uint64_t>(0x7258,B+0x8000);
    const char* names[]={"Speed","Sprint","Is_Crouch","Is_Jump"};
    f.put<uint32_t>(0x7250,4);
    for(unsigned i=0;i<4;++i){f.put<uint64_t>(0x8000+48*i,B+0x9000+32*i);memcpy(f.data.data()+0x9000+32*i,names[i],strlen(names[i])+1);}
    auto sm=f.sampler();ActionCache c;
    CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==0&&c.index[0]==1&&c.index[1]==2&&c.index[2]==3&&c.index[3]==-1);
    f.data[0x8000+48+0x20]=1;CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==FlagSprintBit);
    f.data[0x8000+96+0x20]=1;f.data[0x8000+144+0x20]=1;CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==(FlagSprintBit|FlagCrouchBit|JumpBit));
    CHECK(readActions(sm,B+0x6000,B+0x1f00,c)==0);                         // wrong animator vtable: nothing
    f.put<uint64_t>(0x7248,B+0x6100);ActionCache d2;CHECK(readActions(sm,B+0x6000,B+0x1f40,d2)==0);   // not owned by this entity
}
void effects(){
    for(int ending=0;ending<3;++ending){
        Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(!s.effectsShown&&s.effectCalls==1);
        s.frame();auto u=at(6);d.tick(true,&u);CHECK(!s.effectsShown&&s.effectCalls==2);   // re-hidden every driving tick
        if(ending==0){s.frame();d.tick(true,nullptr);}
        if(ending==1){s.pose.position[0]=40;d.tick(true,&u);}
        if(ending==2){s.anchor.entity=0x2000;s.found=0x7000;d.tick(true,&t);CHECK(!s.effectsShown&&s.effectCalls==2);continue;}   // gone with the world
        CHECK(s.effectsShown&&s.effectCalls==3);
    }
}
void cargoCommand(){
    CHECK(parseCargoCommand("create 519 S1\n","S1")==519u);
    CHECK(parseCargoCommand("give 7 S1\n","S1","give")==7u);CHECK(parseCargoCommand("give 125983117 S1\n","S1","give")==125983117u);CHECK(!parseCargoCommand("give 7 S1\n","S1"));CHECK(!parseCargoCommand("create 7 S1\n","S1","give"));
    for(const char* bad:{"create 519 S2\n","create 519\n","create -1 S1\n","create 4294967296 S1\n","create 5x S1\n","delete 519 S1\n","create 519 S1 x\n",""})
        CHECK(!parseCargoCommand(bad,"S1"));
    CHECK(parseCargoCommand("delete 519 S1\n","S1","delete")==519u);CHECK(!parseCargoCommand("create 519 S1\n","S1","delete"));
    CHECK(parseCargoCommand("list 0 S1\n","S1","list")==0u);CHECK(!parseCargoCommand("list 0 S2\n","S1","list"));
}
void privateRoom(){
    Scene s;auto d=s.make();
    CHECK(!d.gameUsesDouble(s.anchor.entity));                                       // parked: no
    s.flags&=~FlagSleeping;CHECK(d.gameUsesDouble(s.anchor.entity));                 // woken by the game: yes
    s.flags|=FlagSleeping;d.enable();auto t=at(5);d.tick(true,&t);CHECK(!d.gameUsesDouble(s.anchor.entity));   // ours
    s.pose.position[0]=40;auto u=at(6);d.tick(true,&u);CHECK(d.stage()==Double::Stage::Yielded&&d.gameUsesDouble(s.anchor.entity));
    Scene z;z.flags&=~FlagSleeping;auto e=z.make();CHECK(!e.gameUsesDouble(0));         // no porter: nothing to say
}
void busy(){
    Scene s;s.flags|=FlagVisible;auto d=s.make();d.enable();auto t=at(5);
    d.tick(true,&t);CHECK(s.calls.empty()&&d.stage()==Double::Stage::Parked);  // the game is showing it: hands off
    Scene a;a.flags&=~FlagSleeping;auto e=a.make();e.enable();e.tick(true,&t);CHECK(a.calls.empty());
    Scene b;b.found=0;auto f=b.make();f.enable();f.tick(true,&t);CHECK(b.calls.empty());
}
void yields(bool move){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    if(move)s.pose.position[0]=40;else s.flags|=FlagSleeping;     // the game uses its double
    auto u=at(6);d.tick(true,&u);d.tick(true,&u);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Yielded&&s.events.size()>=2);
    CHECK(s.mover.position[0]==0&&s.mover.position[1]==0);   // the mover is back at the resting transform
    d.tick(true,nullptr);CHECK(s.calls.size()==3);
    s.flags=(s.flags|FlagSleeping)&~FlagVisible;d.tick(true,&u);CHECK(d.stage()==Double::Stage::Parked&&s.calls.size()==3);
    d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==6);
}
void releasedByGame(){
    // The game hides the double while we hold it and the partner leaves: nothing is called on the double except the
    // restores, and the mover target goes back to rest.
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);s.flags|=FlagSleeping;s.flags&=~FlagVisible;
    d.tick(true,nullptr);CHECK(s.mover.position[0]==0&&d.stage()==Double::Stage::Parked&&s.calls.size()==3);
}
void transient(){
    // One unreadable pose with the same porter: no forget, no calls; the double keeps being driven afterwards.
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    s.anchor.pose.available=false;auto u=at(6);d.tick(true,&u);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Driving&&!s.last("world_changed"));
    s.anchor.pose.available=true;d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.mover.position[0]==6);
    s.anchor.pose.available=false;d.tick(true,nullptr);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==3);   // release waits for a readable pose
    s.anchor.pose.available=true;s.frame();d.tick(true,nullptr);CHECK(d.stage()==Double::Stage::Parked&&s.calls.back()=="sleep");
}
void identity(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    s.found=0x7100;d.tick(true,&t);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Parked&&s.last("double_changed"));
    Scene w;auto e=w.make();e.enable();e.tick(true,&t);
    w.anchor.entity=0x2000;w.found=0x7000;e.tick(true,&t);CHECK(w.calls.size()==3&&w.last("world_changed"));
    e.tick(true,nullptr);CHECK(w.calls.size()==3);
}
void unconfirmed(){
    Scene s;s.ignoreWake=true;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    CHECK((s.calls==Calls{"place","show","wake","place","hide","sleep"}));CHECK(d.stage()==Double::Stage::Failed&&!(s.flags&FlagVisible));
    d.tick(true,&t);d.tick(true,nullptr);CHECK(s.calls.size()==6);
    // The game stops following the mover target (it took the double): the next tick yields with no further calls.
    Scene p;auto e=p.make();e.enable();e.tick(true,&t);auto u=at(9);e.tick(true,&u);e.tick(true,&u);
    CHECK(e.stage()==Double::Stage::Yielded&&p.calls.size()==3);
    Scene l;l.alterLocal=true;auto f=l.make();f.enable();bool threw=false;try{f.tick(true,&t);}catch(const std::exception&){threw=true;}
    CHECK(threw&&f.stage()==Double::Stage::Failed);
}
void tree(){
    // player 0x6000 -> other child 0x6800, camera 0x9000 -> camera 0x9400, double 0x9800
    auto build=[](fakemem::Fake& f){auto B=f.base;
        auto link=[&](size_t e,uint64_t vt,uint64_t parent,uint64_t first,uint64_t next){f.put<uint64_t>(e,vt);f.put<uint64_t>(e+0x70,parent);f.put<uint64_t>(e+0x78,first);f.put<uint64_t>(e+0x80,next);};
        f.put<uint64_t>(0x6078,B+0x6800);link(0x6800,B+0x1f00,B+0x6000,0,B+0x9000);
        link(0x9000,B+0x1f10,B+0x6000,B+0x9400,0);link(0x9400,B+0x1f10,B+0x9000,0,B+0x9800);link(0x9800,B+0x1f20,B+0x9000,0,0);};
    fakemem::Fake f;build(f);auto s=f.sampler();auto B=f.base;
    CHECK(findDouble(s,B+0x6000,B+0x1f10,B+0x1f20)==B+0x9800);
    CHECK(findDouble(s,B+0x6000,B+0x1f10,B+0x1f30)==0);
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9870,B+0x6000);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);}   // disowned
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9880,B+0xa000);g.put<uint64_t>(0xa000,B+0x1f20);g.put<uint64_t>(0xa070,B+0x9000);auto t=g.sampler();
        CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);}                                                                          // two doubles
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9880,B+0x9400);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);} // sibling loop
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9000,B+0x1f00);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);} // no camera
}
void conversion(){
    for(double yaw:{0.,1.,-2.5,3.}){auto t=fromState({4,5,6},yaw);
        CHECK(t.position[0]==4&&t.position[1]==6&&t.position[2]==5);
        CHECK(std::fabs(std::atan2(t.rotation[3],t.rotation[4])-yaw)<1e-6&&t.rotation[8]==1);
        double det=t.rotation[0]*(t.rotation[4]*t.rotation[8]-t.rotation[5]*t.rotation[7])-t.rotation[1]*(t.rotation[3]*t.rotation[8]-t.rotation[5]*t.rotation[6])+t.rotation[2]*(t.rotation[3]*t.rotation[7]-t.rotation[4]*t.rotation[6]);
        CHECK(std::fabs(det-1)<1e-6);}
    bool threw=false;try{fromState({NAN,0,0},0);}catch(const std::exception&){threw=true;}CHECK(threw);
}
void hold(){
    // A partner who drops out briefly: the double hides in place and stays borrowed (no second upgrade), then reappears.
    Scene s;auto d=s.make();d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
    s.frame();d.tick(true,nullptr,0,0,true);
    CHECK(d.stage()==Double::Stage::Hidden&&!(s.flags&FlagVisible)&&(s.flags&FlagSleeping)&&s.network==0x600&&s.downgrades==0&&!s.effectsShown&&s.last("hidden"));
    CHECK(!d.gameUsesDouble(s.anchor.entity));
    for(int i=0;i<5;++i)d.tick(true,nullptr,0,0,true);
    CHECK(d.stage()==Double::Stage::Hidden&&s.downgrades==0);
    auto u=at(9);d.tick(true,&u,0,0,true);
    CHECK(d.stage()==Double::Stage::Driving&&(s.flags&FlagVisible)&&!(s.flags&FlagSleeping)&&s.pose.position[0]==9&&s.mover.position[0]==9&&s.last("shown"));
    CHECK(s.upgradedFor==s.anchor.entity&&s.downgrades==0);   // the same borrowed network, never rebuilt
    // Hidden, then handed back for real (event or mode off): everything restored.
    s.frame();d.tick(true,nullptr,0,0,true);d.tick(true,nullptr,0,0,false);
    CHECK(d.stage()==Double::Stage::Parked&&s.network==0x500&&s.downgrades==1&&s.effectsShown&&s.mover.position[0]==0&&s.last("released"));
    // Hidden, and the game takes its double (private room): yield, everything restored, no calls of ours.
    Scene g;auto e=g.make();e.enable();e.walk(true);e.tick(true,&t);g.frame();e.tick(true,nullptr,0,0,true);
    auto n=g.calls.size();g.flags&=~FlagSleeping;g.flags|=FlagVisible;
    e.tick(true,nullptr,0,0,true);CHECK(e.stage()==Double::Stage::Yielded&&g.network==0x500&&g.mover.position[0]==0&&g.calls.size()==n&&e.gameUsesDouble(g.anchor.entity));
    // Hidden, and the double changes: forgotten, nothing touched.
    Scene c;auto f=c.make();f.enable();f.tick(true,&t);c.frame();f.tick(true,nullptr,0,0,true);n=c.calls.size();
    c.found=0x7100;f.tick(true,&t,0,0,true);CHECK(f.stage()==Double::Stage::Parked&&c.calls.size()==n&&c.last("double_changed"));
}
int main(){
    lifecycle();mover();animation();network();refusedUpgrade();actions();effects();releasedByGame();cargoCommand();privateRoom();
    try{transient();}catch(const std::exception& e){std::fprintf(stderr,"double oracle: transient threw %s\n",e.what());++failures;}
    hold();busy();yields(true);yields(false);identity();unconfirmed();tree();conversion();
    if(!failures)std::puts("double borrow, yield, release and pointer-path checks passed");
    return failures?1:0;
}
