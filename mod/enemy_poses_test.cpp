// MULE positions (enemy_poses.h): the host's selection, the guest's correction policy, the actor checks of the native
// move on fake memory, and a seeded fuzz of two diverging worlds joined by two cores over a link that loses, repeats,
// delays and reorders datagrams and goes down for a while.
#include "enemy_poses.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <set>
using namespace mod::enemyposes;
using mod::enemies::Mule;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"enemy position oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Mule mule(uint8_t n,double x,double y,uint32_t lod=2,uint32_t state=1,uint32_t flags=0){
    Mule m;m.address=0x1000+0x10*uint64_t(n);m.uuid[0]=n;m.uuid[9]=n;m.state=state;m.flags=flags;m.lod=lod;m.position={x,y,50};m.rotation=upright(0.5);return m;}
static uint64_t keyOf(uint8_t n){return mod::enemies::key(mule(n,0,0).uuid);}
static ds::EnemiesShown shown(std::vector<ds::EnemyPose> poses,uint32_t age=100){ds::EnemiesShown s;s.ageMs=age;s.sequence=1;for(auto& p:poses)s.enemies.poses[s.enemies.count++]=p;return s;}
static ds::EnemyPose pose(uint8_t n,double x,double y,uint8_t flags=HasBody,double h=1.0){return {keyOf(n),{x,y,50},h,flags};}
static double dist(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(size_t k=0;k<3;++k)d+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(d);}
static void units(){
    const std::array<double,3> porter{0,0,50},away{1000,1000,50};
    CHECK(std::fabs(heading(upright(1.25))-1.25)<1e-6&&std::fabs(heading(upright(-3.0))+3.0)<1e-6);
    { std::array<float,9> bad{};bad[0]=std::nanf("");CHECK(heading(bad)==0); }
    // Host: standing MULEs within range of either player, the nearest twelve, keys increasing, the body flag.
    { Sampler s;std::vector<Mule> v;for(uint8_t i=1;i<=20;++i)v.push_back(mule(i,double(21-i)*3,0,i%2?2:0));   // 20 is nearest (3 m), 1 at 60 m
      v.push_back(mule(30,500,0));v.push_back(mule(31,5,0,2,4,1u<<20));v.push_back(mule(32,6,0,2,5));v.push_back(mule(33,7,0,2,3));
      auto e=s.select(v,porter,std::nullopt,1000);
      CHECK(e.count==ds::EnemyPosesMax);
      std::set<uint64_t> keys;for(size_t i=0;i<e.count;++i){keys.insert(e.poses[i].key);if(i)CHECK(e.poses[i].key>e.poses[i-1].key);}
      for(uint8_t i=9;i<=20;++i)CHECK(keys.count(keyOf(i)));                      // the twelve nearest
      CHECK(!keys.count(keyOf(31))&&!keys.count(keyOf(32))&&!keys.count(keyOf(33)));   // fainted, removed, dead
      for(size_t i=0;i<e.count;++i)if(e.poses[i].key==keyOf(20))CHECK(e.poses[i].flags==0&&std::fabs(e.poses[i].heading-0.5)<1e-6&&e.poses[i].position[0]==3);
      for(size_t i=0;i<e.count;++i)if(e.poses[i].key==keyOf(19))CHECK(e.poses[i].flags==HasBody);
      ds::Packet p;p.enemies=e;bool ok=true;try{ds::encodeEnemies(p);}catch(const std::exception&){ok=false;}CHECK(ok);   // what the wire takes
      // Range: nothing beyond 80 m of the porter, unless the partner is near it.
      Sampler t;auto none=t.select({mule(30,500,0)},porter,std::nullopt,1000);CHECK(none.count==0);
      auto near=t.select({mule(30,500,0)},porter,std::array<double,3>{470,0,50},1250);CHECK(near.count==1&&near.poses[0].key==keyOf(30));
      // Two locators with the same 52 bits: one entry.
      Mule a=mule(40,5,0),b=mule(40,9,0);b.address=0x9999;b.uuid[15]=7;Sampler u;auto one=u.select({b,a},porter,std::nullopt,1000);CHECK(one.count==1&&one.poses[0].position[0]==5); }
    // Host: a MULE that covers ground faster than a person is flagged (on a truck).
    {   // A MULE the host already sends stays in the sample to KeepRangeM; a new one must come within RangeM.
      Sampler k;CHECK(k.select({mule(50,RangeM+10,0)},porter,std::nullopt,1000).count==0);
      CHECK(k.select({mule(50,RangeM-2,0)},porter,std::nullopt,1200).count==1);
      CHECK(k.select({mule(50,RangeM+10,0)},porter,std::nullopt,1400).count==1);
      CHECK(k.select({mule(50,KeepRangeM-1,0)},porter,std::nullopt,1600).count==1);
      CHECK(k.select({mule(50,KeepRangeM+1,0)},porter,std::nullopt,1800).count==0);
      CHECK(k.select({mule(50,RangeM+10,0)},porter,std::nullopt,2000).count==0);   // dropped: it has to come near again
      CHECK(k.select({mule(50,RangeM-2,0)},porter,std::nullopt,2200).count==1); }
    { Sampler s;auto a=s.select({mule(1,10,0)},porter,std::nullopt,1000);CHECK(a.count==1&&!(a.poses[0].flags&Fast));
      a=s.select({mule(1,11,0)},porter,std::nullopt,1250);CHECK(!(a.poses[0].flags&Fast));          // 4 m/s
      a=s.select({mule(1,15,0)},porter,std::nullopt,1500);CHECK(a.poses[0].flags&Fast);             // 16 m/s
      a=s.select({mule(1,15.5,0)},porter,std::nullopt,1750);CHECK(!(a.poses[0].flags&Fast)); }
    // Guest: agreement does nothing; a small error is a short step towards the host's pose, at most every 500 ms.
    { Corrector c;auto h=shown({pose(1,20,0)});
      CHECK(c.decide({mule(1,20.5,0)},&h,porter,1000).empty());
      auto m=c.decide({mule(1,22,0)},&h,porter,1250);
      CHECK(m.size()==1&&!m[0].snap&&m[0].address==mule(1,0,0).address&&std::fabs(m[0].position[0]-21.5)<1e-9&&std::fabs(m[0].heading-0.5)<1e-6);
      CHECK(c.decide({mule(1,21.5,0)},&h,porter,1500).empty());                                       // too soon
      m=c.decide({mule(1,21.5,0)},&h,porter,1750);CHECK(m.size()==1&&std::fabs(m[0].position[0]-21.0)<1e-9&&c.pulls()==2&&c.snaps()==0);
      Corrector off;CHECK(off.decide({mule(1,22,0)},&h,porter,1000,false).empty()); }                 // pulls not wanted
    // Guest: a large error is a teleport to the host's pose, once it has lasted, and then not again for two seconds.
    { Corrector c;auto h=shown({pose(1,20,0)});
      CHECK(c.decide({mule(1,30,0)},&h,porter,1000).empty());
      CHECK(c.decide({mule(1,30,0)},&h,porter,1200).empty());
      auto m=c.decide({mule(1,30,0)},&h,porter,1300);CHECK(m.size()==1&&m[0].snap&&m[0].position[0]==20&&m[0].heading==1.0&&c.snaps()==1);
      // It was put there and walks off again: moved at 1300, not before 3300.
      CHECK(c.decide({mule(1,20.2,0)},&h,porter,1600).empty());CHECK(c.decide({mule(1,30,0)},&h,porter,2900).empty());CHECK(c.decide({mule(1,30,0)},&h,porter,3200).empty());
      m=c.decide({mule(1,30,0)},&h,porter,3300);CHECK(m.size()==1&&m[0].snap);
      // An error that was large for one look only (a late sample) moves nothing.
      Corrector d;CHECK(d.decide({mule(1,30,0)},&h,porter,1000).empty());CHECK(d.decide({mule(1,20.2,0)},&h,porter,1250).empty());
      CHECK(d.decide({mule(1,30,0)},&h,porter,1500).empty()); }
    // Guest: a MULE with no body here is simply put there (nobody sees it), at most once a second.
    { Corrector c;auto h=shown({pose(1,20,0)});
      auto m=c.decide({mule(1,22,0,0)},&h,porter,1000);CHECK(m.size()==1&&m[0].snap&&m[0].position[0]==20);
      CHECK(c.decide({mule(1,20.1,0,0)},&h,porter,1200).empty());CHECK(c.decide({mule(1,22,0,0)},&h,porter,1800).empty());CHECK(c.decide({mule(1,22,0,0)},&h,porter,2000).size()==1); }
    // Never: a MULE that is down or gone, one within the porter's reach, a pose on top of the porter, a vehicle, a
    // stale or missing sample, a MULE the host did not send, an absurd error.
    { auto h=shown({pose(1,20,0),pose(2,2,0),pose(3,0.5,0.5),pose(4,20,0,HasBody|Fast),pose(6,2000,0)});
      auto apart=[&](Corrector& c,Mule m,const ds::EnemiesShown* s){c.decide({m},s,porter,1000);return c.decide({m},s,porter,1400);};
      { Corrector c;CHECK(apart(c,mule(1,30,0,2,4,1u<<19),&h).empty()); }
      { Corrector c;CHECK(apart(c,mule(1,30,0,2,3),&h).empty()); }
      { Corrector c;CHECK(apart(c,mule(1,30,0,0,5),&h).empty()); }
      { Corrector c;CHECK(apart(c,mule(1,30,0,2,2),&h).size()==1); }                                      // state 2 stands
      { Corrector c;CHECK(apart(c,mule(1,2.5,0),&h).empty()); }                                          // in reach: 17.5 m of error, left alone
      { Corrector c;CHECK(apart(c,mule(1,0,2.9),&h).empty()); }
      { Corrector c;CHECK(apart(c,mule(1,3.5,0),&h).size()==1); }
      { Corrector c;CHECK(apart(c,mule(3,10,0),&h).empty()); }                                           // the host's pose is on the porter
      { Corrector c;CHECK(apart(c,mule(2,10,0),&h).size()==1); }                                         // 2 m from him is allowed
      { Corrector c;CHECK(apart(c,mule(4,30,0),&h).empty()); }                                           // the host says it rides
      { Corrector c;c.decide({mule(1,30,0)},&h,porter,1000);c.decide({mule(1,34,0)},&h,porter,1250);   // 16 m/s here
        CHECK(c.decide({mule(1,38,0)},&h,porter,1500).empty());CHECK(c.decide({mule(1,42,0)},&h,porter,1750).empty()); }
      { Corrector c;CHECK(apart(c,mule(1,30,0),nullptr).empty()); }
      { Corrector c;auto old=shown({pose(1,20,0)},StaleMs+1);CHECK(apart(c,mule(1,30,0),&old).empty());auto edge=shown({pose(1,20,0)},StaleMs);CHECK(apart(c,mule(1,30,0),&edge).size()==1); }
      { Corrector c;CHECK(apart(c,mule(5,30,0),&h).empty()); }
      { Corrector c;CHECK(apart(c,mule(6,30,0),&h).empty()); }
      { Corrector c;auto nan=shown({pose(1,20,0)});nan.enemies.poses[0].position[1]=std::nan("");CHECK(apart(c,mule(1,30,0),&nan).empty()); }
      // A pull never steps into the porter's reach either.
      { Corrector c;auto t=shown({pose(1,1.6,0)});Mule m=mule(1,1.9,0);CHECK(c.decide({m},&t,{0,3.2,50},1000).empty()); } }
    // This side's own move is not speed: after a teleport of 30 m the MULE is still corrected.
    { Corrector c;auto h=shown({pose(1,20,0)});c.decide({mule(1,50,0)},&h,away,1000);auto m=c.decide({mule(1,50,0)},&h,away,1300);CHECK(m.size()==1&&m[0].snap);
      auto h2=shown({pose(1,21.5,0)});CHECK(c.decide({mule(1,20,0)},&h2,away,1550).empty());           // cooldown (a pull, 500 ms)
      m=c.decide({mule(1,20,0)},&h2,away,1800);CHECK(m.size()==1&&!m[0].snap); }
    // Budget: two native calls per look, the largest errors first.
    { Corrector c;std::vector<Mule> v;std::vector<ds::EnemyPose> p;for(uint8_t i=1;i<=6;++i){v.push_back(mule(i,100+10.0*i,0,0));p.push_back(pose(i,100+10.0*i,3.0+i));}
      auto h=shown(p);auto m=c.decide(v,&h,porter,1000);CHECK(m.size()==MovesPerLook&&m[0].key==keyOf(6)&&m[1].key==keyOf(5));
      m=c.decide(v,&h,porter,1250);CHECK(m.size()==MovesPerLook&&m[0].key==keyOf(4)&&m[1].key==keyOf(3)); }
    // A teleport that does not move the MULE (a rider) is tried again later and later; one that takes clears that.
    { Corrector c;auto h=shown({pose(1,20,0)});
      c.decide({mule(1,60,0)},&h,away,1000);auto m=c.decide({mule(1,60,0)},&h,away,1300);CHECK(m.size()==1&&m[0].snap&&m[0].stuck==0);
      CHECK(c.decide({mule(1,60,0)},&h,away,1500).empty());CHECK(c.decide({mule(1,60,0)},&h,away,3300).empty());   // it stayed: 4 s, not 2
      CHECK(c.decide({mule(1,60,0)},&h,away,5200).empty());
      m=c.decide({mule(1,60,0)},&h,away,5300);CHECK(m.size()==1&&m[0].snap&&m[0].stuck==1);
      CHECK(c.decide({mule(1,60,0)},&h,away,5500).empty());CHECK(c.decide({mule(1,60,0)},&h,away,13200).empty());  // again: 8 s
      m=c.decide({mule(1,60,0)},&h,away,13300);CHECK(m.size()==1&&m[0].stuck==2);
      // This time it is where it was put; when it has walked off again it is an ordinary MULE.
      CHECK(c.decide({mule(1,20.1,0)},&h,away,13500).empty());
      auto h2=shown({pose(1,40,0)});c.decide({mule(1,20.1,0)},&h2,away,15000);
      m=c.decide({mule(1,20.1,0)},&h2,away,15400);CHECK(m.size()==1&&m[0].snap&&m[0].stuck==0);
      // The wait is never longer than StuckMaxMs.
      Corrector d;uint64_t t=1000,lastMove=0,longest=0;
      for(;t<600000;t+=200){auto r=d.decide({mule(1,60,0)},&h,away,t);if(!r.empty()){if(lastMove)longest=std::max(longest,t-lastMove);lastMove=t;}}
      CHECK(longest>=StuckMaxMs&&longest<=StuckMaxMs+200); }
    // Steering (enemy_position_goal): a MULE with a body beyond the deadband is offered a walk to the host's place (the
    // one in agreement and the one without a body are not; the latter is still put there).
    { Corrector c;auto h=shown({pose(1,20,0),pose(2,40,0),pose(3,60,0)});std::vector<Move> steer;std::map<uint64_t,uint64_t> none;
      auto m=c.decide({mule(1,20.5,0),mule(2,42,0),mule(3,63,0,0)},&h,away,1000,false,&none,&steer);
      CHECK(steer.size()==1&&steer[0].key==keyOf(2)&&steer[0].position[0]==40&&steer[0].from[0]==42&&std::fabs(steer[0].error-2)<1e-9);
      CHECK(m.size()==1&&m[0].key==keyOf(3)&&m[0].snap);
      // The list is this look's: an old one is not kept, and nothing is offered without a fresh sample.
      c.decide({mule(1,20.5,0)},&h,away,1250,false,&none,&steer);CHECK(steer.empty());
      steer.resize(3);c.decide({mule(2,42,0)},nullptr,away,1500,false,&none,&steer);CHECK(steer.empty()); }
    // One that is walking it off is not stepped...
    { auto h=shown({pose(1,20,0)});std::vector<Move> steer;std::map<uint64_t,uint64_t> on{{keyOf(1),900}};
      Corrector a;CHECK(a.decide({mule(1,22,0)},&h,away,1000,true,&on,&steer).empty()&&steer.size()==1);
      Corrector b;CHECK(b.decide({mule(1,22,0)},&h,away,1000,true,nullptr,nullptr).size()==1);
      // ...is left alone at 6 m, where another would be teleported...
      Corrector d;CHECK(d.decide({mule(1,26,0)},&h,away,1000,true,&on,&steer).empty());CHECK(d.decide({mule(1,26,0)},&h,away,1400,true,&on,&steer).empty());
      Corrector e;e.decide({mule(1,26,0)},&h,away,1000);CHECK(e.decide({mule(1,26,0)},&h,away,1400).size()==1);
      // ...is teleported from SteerSnapM...
      Corrector f;f.decide({mule(1,32,0)},&h,away,1000,true,&on,&steer);auto m=f.decide({mule(1,32,0)},&h,away,1400,true,&on,&steer);CHECK(m.size()==1&&m[0].snap&&steer.size()==1);
      // ...and is like any other once its last forced destination is older than SteerHoldMs.
      Corrector g;g.decide({mule(1,26,0)},&h,away,900+SteerHoldMs+1,true,&on,&steer);m=g.decide({mule(1,26,0)},&h,away,900+SteerHoldMs+401,true,&on,&steer);CHECK(m.size()==1&&m[0].snap); }
    // Budget: SteersPerLook forced destinations per look, the largest errors first.
    { Corrector c;std::vector<Mule> v;std::vector<ds::EnemyPose> p;for(uint8_t i=1;i<=8;++i){v.push_back(mule(i,100+20.0*i+1+0.3*i,0));p.push_back(pose(i,100+20.0*i,0));}
      auto h=shown(p);std::vector<Move> steer;std::map<uint64_t,uint64_t> none;c.decide(v,&h,away,1000,false,&none,&steer);
      CHECK(steer.size()==SteersPerLook&&steer[0].key==keyOf(8)&&steer[SteersPerLook-1].key==keyOf(3)); }
    // The native move's checks on the actor: the MULE actor vtable with the teleport in its slot, and the live byte.
    { const uint64_t base=0x100000,heap=0x9000000;std::vector<uint8_t> image(ActorVtable+0x100),mem(0x4000);
      auto put=[&](uint64_t a,const void* v,size_t n){if(a>=heap)std::memcpy(&mem[a-heap],v,n);else std::memcpy(&image[a-base],v,n);};
      auto q=[&](uint64_t a,uint64_t v){put(a,&v,8);};
      mod::discovery::Sampler s{[&](uint64_t a,void* o,size_t n){
          if(a>=heap&&a+n<=heap+mem.size()){std::memcpy(o,&mem[a-heap],n);return true;}
          if(a>=base&&a+n<=base+image.size()){std::memcpy(o,&image[a-base],n);return true;}return false;},base};
      q(base+ActorVtable+TeleportSlot,base+TeleportFunction);q(base+0x1000+TeleportSlot,base+TeleportFunction);
      uint64_t good=heap+0x100,other=heap+0x800,dead=heap+0x1000,patched=heap+0x1800;uint8_t one=1;
      q(good,base+ActorVtable);put(good+0x20,&one,1);q(other,base+0x1000);put(other+0x20,&one,1);q(dead,base+ActorVtable);
      CHECK(movable(s,base,good));CHECK(!movable(s,base,other));CHECK(!movable(s,base,dead));CHECK(!movable(s,base,0));CHECK(!movable(s,base,good+1));
      CHECK(!movable(s,base,heap+0x100000));
      q(patched,base+0x2000);put(patched+0x20,&one,1);CHECK(!movable(s,base,patched)); }                 // a vtable without the teleport
}
// Two worlds with the same MULEs. Each game moves its own (they drift apart), some go down, some ride; the guest's
// porter walks among them. The host samples, two cores carry the samples over a bad link, the guest corrects.
namespace {
struct Wire : ds::Transport {
    struct Item { uint64_t at; std::vector<uint8_t> bytes; };
    std::deque<Item> queue;std::mt19937* rng=nullptr;uint64_t now=0;bool up=true;double loss=0.2,repeat=0.2;uint32_t delayMax=250;
    void push(std::vector<uint8_t> b){
        if(!up)return;
        auto u=[&]{return double((*rng)()%10000)/10000.;};
        if(u()<loss)return;
        queue.push_back({now+20+(*rng)()%delayMax,b});if(u()<repeat)queue.push_back({now+20+(*rng)()%(2*delayMax),b});
    }
    void send(const ds::Bytes& b) override {push({b.begin(),b.end()});}
    void sendLong(const std::vector<uint8_t>& b) override {push(b);}
    void deliver(ds::Core& to,uint64_t clock){
        for(size_t i=0;i<queue.size();){if(queue[i].at<=now){auto b=queue[i].bytes;queue.erase(queue.begin()+long(i));to.receive(b.data(),b.size(),now+clock);}else ++i;}
    }
};
struct Provider : ds::StateProvider {
    bool readLocal(ds::State& s) override {s={};return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render&) override {}
};
}
static void fuzz(unsigned seed,bool pulls){
    std::mt19937 rng(seed);auto u=[&](double a,double b){return a+(b-a)*double(rng()%100000)/100000.;};
    const unsigned n=30;
    struct Sim { std::array<double,3> host{},guest{}; double hostHeading=0; bool down=false,rides=false,body=true; uint64_t movedAt=0; };
    std::vector<Sim> sims(n);for(auto& s:sims){s.host={u(-60,60),u(-60,60),50};s.guest={s.host[0]+u(-17,17),s.host[1]+u(-17,17),50};s.body=rng()%4!=0;}
    std::array<double,3> hostPorter{0,0,50},guestPorter{u(-30,30),u(-30,30),50};
    Provider pa,pb;Wire toGuest,toHost;toGuest.rng=toHost.rng=&rng;
    ds::Core host({ds::Role::Host,seed+1,100,120},pa,toGuest),guest({ds::Role::Client,seed+1,200,120},pb,toHost);
    const uint64_t clockHost=5000,clockGuest=90000+seed*13;
    Sampler sampler;Corrector corrector;uint64_t moves=0,lastSentAt=0;
    auto build=[&](bool hostSide){std::vector<Mule> v;for(unsigned i=0;i<n;++i){Mule m=mule(uint8_t(i+1),0,0,sims[i].body||hostSide?2:0,sims[i].down?4:1,sims[i].down?1u<<20:0);
        m.position=hostSide?sims[i].host:sims[i].guest;m.rotation=upright(sims[i].hostHeading);v.push_back(m);}return v;};
    #define FCHECK(x) do{if(!(x)){std::fprintf(stderr,"enemy position fuzz oracle: %s (seed %u t %llu line %d)\n",#x,seed,(unsigned long long)t,__LINE__);++failures;return;}}while(0)
    for(uint64_t t=0;t<=60000;t+=50){
        bool quiet=t>=48000;                         // the end: a good link and nobody moves, so the worlds must meet
        if(t==20000){toGuest.up=toHost.up=false;}
        if(t==24000){toGuest.up=toHost.up=true;}
        if(quiet){toGuest.loss=toHost.loss=0;toGuest.delayMax=toHost.delayMax=30;}
        toGuest.now=toHost.now=t;toGuest.deliver(guest,clockGuest);toHost.deliver(host,clockHost);
        host.tick(t+clockHost);guest.tick(t+clockGuest);
        if(!quiet){
            for(auto& s:sims){
                if(rng()%2000==0)s.down=!s.down;
                if(rng()%1500==0)s.rides=!s.rides;
                double hs=s.rides?0.9:0.15;
                if(!s.down){s.host[0]+=u(-hs,hs)+(s.rides?0.8:0);s.host[1]+=u(-hs,hs);s.hostHeading=u(-3,3);
                    if(!s.rides){s.guest[0]+=u(-0.2,0.2);s.guest[1]+=u(-0.2,0.2);}else{s.guest[0]+=0.8;}}
            }
            guestPorter[0]+=u(-0.3,0.3);guestPorter[1]+=u(-0.3,0.3);
            if(rng()%400==0){auto& s=sims[rng()%n];guestPorter={s.guest[0]+1,s.guest[1],50};}        // the porter walks up to one
        } else for(auto& s:sims)s.rides=false;
        if(t%200==0){auto e=sampler.select(build(true),hostPorter,std::optional<std::array<double,3>>(guestPorter),t);
            for(size_t i=0;i<e.count;++i){FCHECK(e.poses[i].key&&(!i||e.poses[i].key>e.poses[i-1].key));unsigned id=unsigned(e.poses[i].key&0xff)-1;FCHECK(id<n&&!sims[id].down);}
            if(host.sendEnemies(e,t+clockHost))lastSentAt=t;}
        FCHECK(!guest.sendEnemies(sampler.select(build(false),guestPorter,std::nullopt,t),t+clockGuest));
        auto latest=guest.latestEnemies(t+clockGuest);
        if(t>21500&&t<24000)FCHECK(!latest);                                                          // the outage: stale
        auto mules=build(false);
        auto out=corrector.decide(mules,latest?&*latest:nullptr,guestPorter,t,pulls);
        FCHECK(out.size()<=MovesPerLook);FCHECK(latest||out.empty());
        for(auto& mv:out){
            unsigned id=unsigned(mv.key&0xff)-1;FCHECK(id<n&&mv.address==mules[id].address);auto& s=sims[id];
            FCHECK(!s.down);FCHECK(dist(s.guest,guestPorter)>=ReachM);FCHECK(dist(mv.position,guestPorter)>=LandM);
            FCHECK(std::isfinite(mv.heading));for(auto v:mv.position)FCHECK(std::isfinite(v));
            FCHECK(mv.error>=DeadbandM&&mv.error<=MaxErrorM);
            FCHECK(mv.snap||(pulls&&dist(mv.position,s.guest)<=PullStepM+1e-9));
            FCHECK(!s.movedAt||t-s.movedAt>=(mv.snap?(s.body?SnapCooldownMs:UnseenCooldownMs):PullIntervalMs)||!mv.snap);
            FCHECK(!s.movedAt||t-s.movedAt>=PullIntervalMs);
            // The pose it is put at is one the host really had within the last sample's life (never an old world).
            FCHECK(latest->ageMs<=StaleMs&&t-lastSentAt<=StaleMs+2500);
            s.guest=mv.position;s.movedAt=t;++moves;
        }
    }
    uint64_t t=60000;FCHECK(moves>0);FCHECK(host.connected()&&guest.connected());
    // After the quiet seconds every MULE the host sends that may be moved is where the host has it.
    auto last=guest.latestEnemies(t+clockGuest);FCHECK(last&&last->enemies.count>0);unsigned met=0;
    for(size_t i=0;i<last->enemies.count;++i){unsigned id=unsigned(last->enemies.poses[i].key&0xff)-1;auto& s=sims[id];
        if(s.down||dist(s.guest,guestPorter)<ReachM||dist(s.host,guestPorter)<LandM+PullStepM)continue;
        ++met;FCHECK(dist(s.guest,s.host)<(pulls||!s.body?DeadbandM+0.01:SnapM+0.01));}
    FCHECK(met>0);
}
int main(){
    units();
    if(!failures){for(unsigned seed=1;seed<=60&&!failures;++seed){fuzz(seed,true);fuzz(1000+seed,false);}}
    if(!failures)std::puts("enemy position selection, correction policy, actor checks and 120 fuzzed sessions passed");
    return failures?1:0;
}
