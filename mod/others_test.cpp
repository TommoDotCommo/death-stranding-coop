// Others (others.h), game-free: several players joined by a link in memory, each with a presence core per pair, and a
// fake world that makes, moves, hides and removes bodies as the game would. Every protection here has a mutant
// (MUTANT_OTHERS_*, MUTANT_BODIES_*; mod/Makefile) that this test must fail.
#include "others.h"
#include "controls.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <vector>
using namespace mod::others;
using mod::puppet::Anchor;using mod::puppet::Transform;using mod::puppet::FlagSleeping;using mod::puppet::FlagVisible;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"others oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr unsigned Control=40,Pulse=7;          // one float control and one bool control of the animation sample
const std::array<uint8_t,16> Build{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
void sum(std::vector<uint8_t>& b){uint32_t h=2166136261u;for(size_t i=0;i<b.size();++i){h^=i>=120&&i<124?0:b[i];h*=16777619u;}for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));}

// The game's side: this porter and whatever bodies were made.
struct World {
    struct Entity {uint64_t flags=0,key=0;mod::Pose pose;Transform mover;double speed=-1;uint32_t actions=0;float control=-1;unsigned mirrors=0,pulses=0,settles=0;};
    Anchor anchor;std::map<uint64_t,Entity> live;uint64_t next=0x10000;unsigned creates=0,removes=0;std::map<uint64_t,unsigned> createsOf;
    std::vector<uint64_t> createdAt;uint64_t clock=0;
    World(){anchor.entity=0x1000;anchor.pose.available=anchor.pose.coherent=true;anchor.pose.position={0,0,0};anchor.pose.rotation={1,0,0,0,1,0,0,0,1};}
    BodyParts parts(uint64_t key){
        BodyParts p;
        p.own.resource=[](uint64_t){return uint64_t(0x9000);};
        p.own.create=[this,key](uint64_t,const Transform& t){auto id=next+=0x100;auto& e=live[id];e.key=key;e.flags=FlagVisible|0x451;
            e.pose.available=e.pose.coherent=true;e.pose.position=t.position;e.pose.rotation=t.rotation;e.mover=t;++creates;++createsOf[key];createdAt.push_back(clock);return id;};
        p.own.alive=[this](uint64_t b){return live.count(b)!=0;};
        p.own.park=[this](uint64_t b){auto& e=live.at(b);e.flags=(e.flags|FlagSleeping)&~FlagVisible;};
        p.own.dress=[](uint64_t){return true;};
        p.own.remove=[this](uint64_t b){++removes;live.erase(b);};
        auto& d=p.drive;
        d.local=[this]{return anchor;};
        d.flags=[this](uint64_t e){auto it=live.find(e);return it==live.end()?uint64_t(0):it->second.flags;};
        d.read=[this](uint64_t e){auto it=live.find(e);return it==live.end()?mod::Pose{}:it->second.pose;};
        d.sleep=[this](uint64_t e,bool b){auto& f=live.at(e).flags;f=b?f|FlagSleeping:f&~FlagSleeping;};
        d.visible=[this](uint64_t e,bool b){auto& f=live.at(e).flags;f=b?f|FlagVisible:f&~FlagVisible;};
        d.place=[this](uint64_t e,const Transform& t){live.at(e).pose.position=t.position;};
        d.anchor=[this](uint64_t e,const Transform& t){live.at(e).mover=t;};
        d.animate=[this](uint64_t e,double v,uint32_t a){auto& x=live.at(e);x.speed=v;x.actions=a;};
        d.upgrade=[](uint64_t,uint64_t){return uint64_t(0x500);};d.downgrade=[](uint64_t,uint64_t){};d.effects=[](uint64_t,bool){};
        d.mirror=[this](uint64_t e,const ds::Animation* a,bool fresh){auto& x=live.at(e);
            if(!a){x.control=-1;return;}
            x.control=a->floats[Control];++x.mirrors;if(fresh&&mod::controls::bit(a->rose,Pulse))++x.pulses;};
        p.settle=[this](uint64_t e,uint64_t,uint64_t){++live.at(e).settles;};
        return p;
    }
    // One engine frame: an awake body is put where its mover says.
    void frame(){for(auto& [id,e]:live)if(!(e.flags&FlagSleeping))e.pose.position=e.mover.position;}
    void reload(){live.clear();anchor.entity+=0x1000;}   // a new world: the old entities went with it
    const Entity* of(uint64_t entity) const {auto it=live.find(entity);return it==live.end()?nullptr:&it->second;}
    bool shown(uint64_t entity) const {auto e=of(entity);return e&&(e->flags&FlagVisible)&&!(e->flags&FlagSleeping);}
};
// One player: a porter walking along x on a lane of its own, on a clock of its own.
struct Player {
    uint64_t id,clock;double lane;std::unique_ptr<Others> others;
    double offset=0;bool jumped=false,walking=true,down=false,gated=false,sendAnimation=false,pulse=false;float control=0;
    std::map<uint64_t,unsigned> pulsesFrom;
    Player(uint64_t i,uint64_t c,double z):id(i),clock(c),lane(z){}
    double x(uint64_t now) const {return offset+(walking?1.5*double(now)/1000.:0);}
    Local local(uint64_t now){
        Local l;l.available=true;l.gated=gated;l.sampledAt=now+clock;l.jumped=jumped;jumped=false;
        l.state.position={x(now),0,lane};l.state.velocity={walking?1.5:0,0,0};l.state.yaw=1.5707963;if(down)l.state.flags|=ds::Down;
        return l;}
};
struct Sim {
    MemoryHub hub;World world;std::vector<std::unique_ptr<Player>> players;uint64_t now=5000;
    Bodies bodies{[this](uint64_t key){return std::make_unique<Body>(world.parts(key),[](const std::string&){});}};
    std::vector<Shown> shown;Plan plan;bool event=false;
    Player& add(uint64_t id,double lane,size_t cap=8,std::array<uint8_t,16> build=Build,bool join=true){
        auto p=std::make_unique<Player>(id,id*7919,lane);
        Settings s;s.self=id;s.session=777;s.cap=cap;s.build=build;s.nickname="Porter "+std::to_string(id);
        uint64_t seed=id*1000003;
        p->others=std::make_unique<Others>(s,hub.link(id),[seed]() mutable {seed+=0x9e3779b97f4a7c15ull;return seed|1;},[](const std::string&){});
        auto& r=*p;players.push_back(std::move(p));if(join)hub.join(id,*r.others,now,r.clock);return r;}
    Player& me(){return *players.front();}
    // 10 ms: everybody reads its porter, the link delivers, everybody's network tick, then this game's frame.
    void step(){
        now+=10;world.clock=now;
        for(auto& p:players)p->others->local(p->local(now));
        hub.deliver(now);
        for(auto& p:players){
            p->others->tick(now+p->clock);
            if(p->sendAnimation){ds::Animation a;a.floats[Control]=p->control;if(p->pulse){mod::controls::setBit(a.rose,Pulse,true);p->pulse=false;}p->others->animation(a,now+p->clock);}
        }
        for(size_t i=1;i<players.size();++i)for(auto& v:players[i]->others->frame(now+players[i]->clock))
            if(v.animation&&v.animation->fresh&&mod::controls::bit(v.animation->animation.rose,Pulse))++players[i]->pulsesFrom[v.id];
        shown=me().others->frame(now+me().clock);
        for(auto& v:shown)if(v.animation&&v.animation->fresh&&mod::controls::bit(v.animation->animation.rose,Pulse))++me().pulsesFrom[v.id];
        mod::others::plan(shown,!event,true,true,plan);
        bodies.drive(plan.wants,world.anchor,now+me().clock);
        world.frame();
    }
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=10)step();}
    const Shown* see(uint64_t id) const {for(auto& v:shown)if(v.id==id)return &v;return nullptr;}
    // How far this game's body of that player stands from where the player is now (game axes: x, y horizontal).
    double error(Player& p){auto e=world.of(bodies.entity(p.id));if(!e)return 1e9;return std::hypot(e->pose.position[0]-p.x(now),e->pose.position[1]-p.lane);}
};

void pairing(){
    CHECK(pairSession(777,100,200)==pairSession(777,200,100));           // the same on both sides
    CHECK(pairSession(777,100,200)!=pairSession(777,100,300));           // another pair, another session
    CHECK(pairSession(777,100,200)!=pairSession(778,100,200));
    CHECK(pairSession(777,100,200)!=0&&pairSession(0,0,0)!=0);
    CHECK(pairRole(100,200)==ds::Role::Host&&pairRole(200,100)==ds::Role::Client);
}
// Several players enter, walk, and each body follows its own player.
void several(){
    Sim s;auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);auto& d=s.add(300,60);
    s.step();CHECK(me.others->size()==3&&s.bodies.size()==1);            // one new body object a tick
    s.step();s.step();CHECK(s.bodies.size()==3);
    CHECK(s.world.creates==1);                                           // one new entity at a time
    s.run(250);CHECK(s.world.creates==1);
    s.run(4000);
    CHECK(me.others->connected()==3&&b.others->connected()==3&&c.others->connected()==3&&d.others->connected()==3);
    CHECK(s.world.creates==3&&s.bodies.makes()==3&&s.world.removes==0);   // made once per player
    for(auto* p:{&b,&c,&d}){CHECK(s.world.createsOf[p->id]==1);CHECK(s.world.shown(s.bodies.entity(p->id)));CHECK(s.error(*p)<0.6);
        auto e=s.world.of(s.bodies.entity(p->id));CHECK(e&&e->key==p->id&&e->settles>0&&e->speed>1.0&&e->speed<2.0);}
    for(size_t i=1;i<s.world.createdAt.size();++i)CHECK(s.world.createdAt[i]-s.world.createdAt[i-1]>=MakeEveryMs);
    CHECK(s.bodies.made()==3&&s.bodies.driving()==3&&s.bodies.givenUp()==0);
    // The names came with the sessions.
    auto v=s.see(200);CHECK(v&&v->name=="Porter 200");v=s.see(50);CHECK(v&&v->name=="Porter 50");
    // A longer way round and a lossy link: still each its own.
    s.hub.delayMs=40;unsigned n=0;s.hub.drop=[&](uint64_t,uint64_t,const std::vector<uint8_t>&){return ++n%10==0;};
    s.run(4000);
    for(auto* p:{&b,&c,&d})CHECK(s.error(*p)<0.8);
    CHECK(s.world.creates==3&&s.world.removes==0);
}
// A player whose samples stop (connected, answering pings) and one who is gone for a while (nothing arrives).
void stale(){
    Sim s;auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);
    s.run(3000);CHECK(s.world.shown(s.bodies.entity(200))&&s.world.shown(s.bodies.entity(50)));
    // C's game stops giving a pose (loading): its core sends no presence, but still answers pings.
    c.gated=true;s.run(2500);
    auto v=s.see(200);CHECK(v&&v->connected&&!v->visible);
    auto body=s.bodies.entity(200);
    CHECK(body&&s.world.of(body)&&!s.world.shown(body));                  // hidden, kept
    CHECK((s.world.of(body)->flags&FlagSleeping)&&!(s.world.of(body)->flags&FlagVisible));
    CHECK(s.world.shown(s.bodies.entity(50))&&s.error(b)<0.6);            // the other one goes on
    c.gated=false;s.run(1500);
    CHECK(s.bodies.entity(200)==body&&s.world.shown(body)&&s.error(c)<0.6&&s.world.creates==2);
    // Nothing from C arrives at all: the session times out, the body waits hidden, and comes back when C does.
    s.hub.drop=[](uint64_t from,uint64_t to,const std::vector<uint8_t>&){return from==200||to==200;};
    s.run(5000);v=s.see(200);CHECK(v&&!v->connected&&!v->visible&&me.others->here(200));
    CHECK(s.bodies.entity(200)==body&&!s.world.shown(body)&&s.world.removes==0);
    s.hub.drop=nullptr;s.run(3000);
    CHECK(s.world.shown(body)&&s.error(c)<0.6&&s.world.creates==2&&s.world.removes==0);
    // This game in a cutscene: nobody is shown; over: everybody again, the same bodies.
    me.gated=true;s.run(300);CHECK(!s.world.shown(s.bodies.entity(50))&&!s.world.shown(body));
    me.gated=false;s.run(1500);CHECK(s.world.shown(s.bodies.entity(50))&&s.world.shown(body));
    s.event=true;s.run(100);CHECK(!s.world.shown(s.bodies.entity(50))&&!s.world.shown(body));
    s.event=false;s.run(300);CHECK(s.world.shown(s.bodies.entity(50))&&s.world.shown(body)&&s.world.creates==2&&s.world.removes==0);
}
// A player who leaves: the session and the body go; the others stay.
void leaving(){
    Sim s;auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);
    s.run(3000);auto body=s.bodies.entity(200),other=s.bodies.entity(50);CHECK(body&&other);
    s.hub.leave(200,s.now);s.step();
    CHECK(!me.others->here(200)&&me.others->size()==1&&s.bodies.size()==1);
    CHECK(s.world.removes==1&&!s.world.of(body)&&s.world.of(other)&&s.bodies.removes()==1);
    s.run(1000);CHECK(s.world.shown(other)&&s.error(b)<0.6&&b.others->size()==1);
    CHECK(c.others->size()==0);
    // Back again: a session and a body of its own again.
    s.hub.join(200,*c.others,s.now,c.clock);s.run(3000);
    CHECK(me.others->connected()==2&&s.world.creates==3&&s.world.removes==1&&s.error(c)<0.6);
    // The link goes away: everybody leaves.
    me.others->clear(s.now+me.clock);s.step();CHECK(me.others->size()==0&&s.bodies.size()==0&&s.world.removes==3&&s.world.live.empty());
}
void limits(){
    Sim s;auto& me=s.add(100,0,2);auto& b=s.add(50,20);s.add(200,40);auto& d=s.add(300,60);
    CHECK(me.others->size()==2&&me.others->counters().full==1&&!me.others->here(300));
    s.run(3000);
    CHECK(s.bodies.size()==2&&s.world.creates==2&&me.others->connected()==2&&!s.bodies.entity(300));
    CHECK(d.others->size()==3);                                           // their side waits; ours never answers
    // Somebody leaves: the one who was refused is taken.
    s.hub.leave(50,s.now);s.run(3000);
    CHECK(me.others->size()==2&&me.others->here(300)&&me.others->connected()==2&&s.bodies.entity(300)&&s.error(d)<0.6);
    CHECK(b.others->size()==0);
    // Never this player's own id, never zero, never twice; over the hard limit the setting is cut.
    CHECK(!me.others->entered(100,s.now)&&!me.others->entered(0,s.now)&&!me.others->entered(200,s.now)&&me.others->size()==2);
    Settings big;big.self=1;big.session=1;big.cap=1000;Others o(big,{},[]{return uint64_t(5);},{});CHECK(o.settings().cap==MaxOthers);
    CHECK(!o.entered(1,1)&&!o.entered(0,1)&&o.size()==0&&o.entered(2,1)&&!o.entered(2,1));
    for(uint64_t id=2;id<100;++id)o.entered(id,1);
    CHECK(o.size()==MaxOthers);
    // Off: nobody enters, nothing is sent, nothing is shown.
    Settings off;unsigned sends=0;Others none(off,{[&](uint64_t,const uint8_t*,size_t){++sends;}},{},{});
    CHECK(!none.entered(5,1));none.local({});none.tick(1000);CHECK(none.frame(1000).empty()&&!sends&&none.size()==0);
    uint8_t junk[128]{};CHECK(none.received(5,junk,128,1)!=ds::Error::None);
}
// One player's datagrams never move another's body, and an id that has not entered is nobody.
void isolation(){
    Sim s;auto& me=s.add(100,0);auto& c=s.add(200,40);auto& d=s.add(300,60);
    // C is here but nothing of C's arrives yet; D connects. D's HELLO handed over as C's is another pair's session.
    s.hub.drop=[](uint64_t from,uint64_t,const std::vector<uint8_t>&){return from==200;};
    s.hub.record=true;s.run(1500);
    CHECK(me.others->connected()==1);
    std::vector<MemoryHub::Datagram> fromD;for(auto& g:s.hub.sentLog)if(g.from==300&&g.to==100)fromD.push_back(g);
    bool hello=false;
    for(auto& g:fromD)if(g.bytes[6]==uint8_t(ds::Kind::Hello)){hello=true;CHECK(me.others->received(200,g.bytes.data(),g.bytes.size(),s.now+me.clock)==ds::Error::Session);break;}
    CHECK(hello);
    for(auto& g:fromD)CHECK(me.others->received(200,g.bytes.data(),g.bytes.size(),s.now+me.clock)!=ds::Error::None);
    s.step();auto v=s.see(200);CHECK(v&&!v->connected&&!v->visible);
    // Both connected, C standing still: D's presence handed over as C's does not move C's body.
    c.walking=false;c.offset=3;s.hub.drop=nullptr;s.hub.sentLog.clear();s.run(3000);
    CHECK(me.others->connected()==2&&s.error(c)<0.1&&s.error(d)<0.6);
    unsigned presence=0;
    for(auto g:s.hub.sentLog)if(g.from==300&&g.to==100&&g.bytes[6]==uint8_t(ds::Kind::Presence)){++presence;
        CHECK(me.others->received(200,g.bytes.data(),g.bytes.size(),s.now+me.clock)!=ds::Error::None);}
    CHECK(presence>20);
    s.run(500);CHECK(s.error(c)<0.1&&s.error(d)<0.6);
    // From nobody: dropped, and it makes no session and no body.
    auto before=me.others->counters().unknown;auto& g=s.hub.sentLog.back();
    CHECK(me.others->received(999,g.bytes.data(),g.bytes.size(),s.now+me.clock)==ds::Error::NotConnected);
    CHECK(me.others->counters().unknown==before+1&&!me.others->here(999)&&me.others->size()==2);
    s.step();CHECK(s.bodies.size()==2&&s.world.creates==2);
    // Not a datagram of the core at all.
    uint8_t junk[2000]{};auto bad=me.others->counters().malformed;
    CHECK(me.others->received(200,junk,64,s.now)==ds::Error::Length&&me.others->received(200,junk,2000,s.now)==ds::Error::Length&&me.others->received(200,nullptr,128,s.now)==ds::Error::Length);
    CHECK(me.others->counters().malformed==bad+3);
}
// Another build of the mod is not a player here.
void identity(){
    Sim s;auto& me=s.add(100,0);auto other=Build;other[3]^=0x40;s.add(200,40,8,other);auto& d=s.add(300,60);
    s.run(3000);
    auto v=s.see(200);CHECK(v&&!v->connected&&me.others->counters().foreignBuild>0&&me.others->connected()==1);
    CHECK(s.error(d)<0.6);
    auto body=s.bodies.entity(200);CHECK(!s.world.shown(body));
}
// A jump (a fast travel, a load) is told once, and what follows is taken again.
void jump(){
    Sim s;s.add(100,0);auto& b=s.add(50,20);
    s.run(3000);CHECK(s.error(b)<0.6);
    b.offset=500;b.jumped=true;s.run(1000);CHECK(s.error(b)<0.6);
    s.run(3000);CHECK(s.error(b)<0.6);
}
// The animation sample of a player goes into that player's body, each pulse once per receiver.
void animation(){
    Sim s;auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);
    b.sendAnimation=true;b.control=0.75f;
    s.run(3000);
    auto eb=s.world.of(s.bodies.entity(50)),ec=s.world.of(s.bodies.entity(200));
    CHECK(eb&&eb->control==0.75f&&eb->mirrors>10);CHECK(ec&&ec->mirrors==0&&ec->control==-1);
    b.pulse=true;s.run(1000);
    CHECK(me.pulsesFrom[50]==1&&c.pulsesFrom[50]==1&&me.pulsesFrom[200]==0);
    eb=s.world.of(s.bodies.entity(50));CHECK(eb&&eb->pulses==1);
    b.pulse=true;s.run(1000);CHECK(me.pulsesFrom[50]==2&&c.pulsesFrom[50]==2);
    // A player who is down lies there without the mirrored controls.
    b.down=true;b.walking=false;s.run(1000);
    eb=s.world.of(s.bodies.entity(50));CHECK(eb&&eb->control==-1&&eb->actions==mod::puppet::DeadPoseBit&&eb->speed<1e-6);
}
// A new world (a load): every body is made again, once, one at a time; nothing of the old world is touched.
void reload(){
    Sim s;s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);auto& d=s.add(300,60);
    s.run(3000);CHECK(s.world.creates==3);
    s.world.reload();s.world.createdAt.clear();s.step();
    CHECK(s.world.creates==4);
    s.run(3000);
    CHECK(s.world.creates==6&&s.world.removes==0&&s.world.live.size()==3&&s.bodies.size()==3);
    for(size_t i=1;i<s.world.createdAt.size();++i)CHECK(s.world.createdAt[i]-s.world.createdAt[i-1]>=MakeEveryMs);
    for(auto* p:{&b,&c,&d}){CHECK(s.world.createsOf[p->id]==2);CHECK(s.error(*p)<0.6);}
    // No porter (the menu): nothing is made and nothing is called.
    Sim m;m.world.anchor.entity=0;m.add(100,0);m.add(50,20);m.run(2000);CHECK(m.world.creates==0&&m.bodies.size()==1&&m.bodies.made()==0);
}
// How a player is shown.
void showing(){
    Shown v;CHECK(!drive(v).shown);
    v.connected=v.visible=true;v.render.visible=true;v.render.state.position={1,2,3};v.render.state.velocity={3,9,4};v.render.state.flags=ds::Sprint;v.render.state.action=5;
    auto d=drive(v);CHECK(d.shown&&d.goal.position==(std::array<double,3>{1,3,2})&&d.speed==5&&d.actions==(uint32_t(ds::Sprint)|5u<<16)&&!d.mirror);
    v.animation=ds::AnimationShown{};CHECK(drive(v).mirror);
    v.render.state.flags=ds::Down;d=drive(v);CHECK(!d.mirror&&d.speed==0&&d.actions==mod::puppet::DeadPoseBit&&d.goal.position[2]>2);
    v.render.state.flags=ds::Caught;d=drive(v);CHECK(!d.mirror&&d.actions==mod::puppet::DragPoseBit);
    v.render.state.flags=ds::Vehicle;v.render.state.vehicle=ds::Truck;d=drive(v);CHECK(d.shown&&!d.mirror&&(d.actions>>mod::puppet::VehicleShift)==ds::Truck);
    v.visible=false;CHECK(!drive(v).shown);
    Plan p;v.visible=true;v.id=9;v.render.state={};plan({v,v},false,true,true,p);CHECK(p.wants.size()==2&&p.wants[0].key==9&&!p.wants[0].goal&&!p.wants[1].sample);
    plan({v},true,true,false,p);CHECK(p.wants.size()==1&&p.wants[0].goal&&!p.wants[0].sample);
    plan({v},true,true,true,p);CHECK(p.wants[0].goal&&p.wants[0].sample);
}
// A peer that speaks the plain core (as the lower id it is the pair's host): what others never carry is refused or dropped.
struct Raw final:ds::StateProvider,ds::Transport {
    ds::State state;std::deque<std::vector<uint8_t>> toOthers,toRaw;std::unique_ptr<ds::Core> core;
    bool readLocal(ds::State& s) override {s=state;return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render&) override {}
    void put(std::vector<uint8_t> b){std::copy(Build.begin(),Build.end(),b.begin()+16);sum(b);toOthers.push_back(std::move(b));}
    void send(const ds::Bytes& b) override {put({b.begin(),b.end()});}
    void sendLong(const std::vector<uint8_t>& b) override {put(b);}
};
void kinds(){
    Raw raw;ds::Config c;c.role=ds::Role::Host;c.session=pairSession(777,10,100);c.nonce=4242;raw.core=std::make_unique<ds::Core>(c,raw,raw);
    Settings st;st.self=100;st.session=777;st.cap=4;st.build=Build;st.nickname="Me";
    Others me(st,{[&](uint64_t to,const uint8_t* d,size_t n){CHECK(to==10);std::vector<uint8_t> b(d,d+n);std::copy(ds::ModBuild.begin(),ds::ModBuild.end(),b.begin()+16);sum(b);raw.toRaw.push_back(std::move(b));}},
        []{return uint64_t(99);},{});
    CHECK(me.entered(10,1000));
    uint64_t now=1000;Local l;l.available=true;l.gated=false;
    auto run=[&](uint64_t ms){for(uint64_t t=0;t<ms;t+=10){now+=10;l.sampledAt=now;l.state.position[0]=double(now)/1000.;raw.state.position[0]=double(now)/500.;me.local(l);
        while(!raw.toOthers.empty()){auto b=raw.toOthers.front();raw.toOthers.pop_front();me.received(10,b.data(),b.size(),now);}
        while(!raw.toRaw.empty()){auto b=raw.toRaw.front();raw.toRaw.pop_front();raw.core->receive(b.data(),b.size(),now);}
        raw.core->tick(now);me.tick(now);me.frame(now);}};
    run(2500);CHECK(raw.core->connected()&&me.connected()==1);
    auto f=me.frame(now);CHECK(f.size()==1&&f[0].visible&&std::fabs(f[0].render.state.position[0]-double(now)/500.)<0.5);
    // The host's time of day is not for others.
    CHECK(raw.core->sendEnvironment({1000,false,0},now));run(100);CHECK(me.counters().kind==1);
    ds::EnemyPoses poses;poses.count=1;poses.poses[0].key=5;CHECK(raw.core->sendEnemies(poses,now));run(100);CHECK(me.counters().kind+me.counters().malformed==2);
    // A cargo event is acknowledged (their stream goes on) and dropped; a name after it still arrives.
    ds::CargoEvent e;e.type=1;e.item=77;raw.core->queueCargo(e);
    for(auto& n:mod::nametag::encode("Higgs"))raw.core->queueCargo(n);
    run(1500);CHECK(raw.core->cargoPending()==0&&me.counters().events==1);
    f=me.frame(now);CHECK(f.size()==1&&f[0].name=="Higgs");
    // Our name reached them as the core's name events (type 10), and nothing else did.
    auto got=raw.core->takeCargo();CHECK(!got.empty());mod::nametag::Receiver rx;std::string name;
    for(auto& g:got){CHECK(g.type==mod::nametag::WireType);if(auto n=rx.feed(g))name=*n;}
    CHECK(name=="Me");
    // Leaving says goodbye once: their side lets go without waiting for the timeout.
    me.left(10,now);while(!raw.toRaw.empty()){auto b=raw.toRaw.front();raw.toRaw.pop_front();raw.core->receive(b.data(),b.size(),now);}
    CHECK(!raw.core->connected()&&me.size()==0);
}
}
int main(){
    pairing();several();stale();leaving();limits();isolation();identity();jump();animation();reload();showing();kinds();
    if(failures){std::fprintf(stderr,"others: %u failures\n",failures);return 1;}
    std::puts("others: sessions per player, bodies per player, limits, isolation, staleness and leaving hold");
    return 0;
}
