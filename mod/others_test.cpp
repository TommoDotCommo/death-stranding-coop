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
    // What this player's mod read of its porter: worn gear, the carry summary, where the cases sit.
    std::optional<mod::gear::WornSet> worn;std::optional<mod::carry::Summary> carried;std::optional<std::vector<mod::carryplace::Placed>> placed;
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
    std::vector<Shown> shown;Plan plan;bool event=false,gear=false,carry=false;   // gear, carry: the settings of the players added next
    Player& add(uint64_t id,double lane,size_t cap=8,std::array<uint8_t,16> build=Build,bool join=true){
        auto p=std::make_unique<Player>(id,id*7919,lane);
        Settings s;s.self=id;s.session=777;s.cap=cap;s.build=build;s.nickname="Porter "+std::to_string(id);s.gear=gear;s.carry=carry;
        uint64_t seed=id*1000003;
        p->others=std::make_unique<Others>(s,hub.link(id),[seed]() mutable {seed+=0x9e3779b97f4a7c15ull;return seed|1;},[](const std::string&){});
        auto& r=*p;players.push_back(std::move(p));if(join)hub.join(id,*r.others,now,r.clock);return r;}
    Player& me(){return *players.front();}
    // 10 ms: everybody reads its porter, the link delivers, everybody's network tick, then this game's frame.
    void step(){
        now+=10;world.clock=now;
        for(auto& p:players){p->others->local(p->local(now));p->others->wears(p->worn,now+p->clock);p->others->carries(p->carried,p->placed,now+p->clock);}
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
// Names over the others' heads: a label per body, with that player's name, over that player's body; none before the
// body is made, none over a hidden body or during a game event, gone when the player leaves.
void labelling(){
    struct Hud {
        std::map<uint64_t,std::pair<std::u16string,std::array<double,3>>> live;uint64_t next=0x100;unsigned creates=0,removes=0;
        mod::nametag::Api api(){mod::nametag::Api a;
            a.create=[this](const std::u16string& n,const std::array<double,3>& at)->uint64_t{++creates;live[next+=0x40]={n,at};return next;};
            a.move=[this](uint64_t r,const std::array<double,3>& at){live.at(r).second=at;};
            a.remove=[this](uint64_t r){++removes;live.erase(r);};
            return a;}
    } hud;
    Sim s;
    mod::nametag::Tags tags(hud.api(),[&](uint64_t body)->std::optional<std::array<double,3>>{
        if(!s.world.shown(body))return std::nullopt;
        auto at=s.world.of(body)->pose.position;at[2]+=2.1;return at;},{});
    auto tick=[&]{s.step();tags.tick(!s.event,labels(s.shown,s.bodies));};
    auto run=[&](uint64_t ms){for(uint64_t t=0;t<ms;t+=10)tick();};
    auto over=[&](uint64_t id,const char16_t* name){
        auto r=tags.record(id);auto e=s.world.of(s.bodies.entity(id));if(!r||!e||!hud.live.count(r))return false;
        auto& l=hud.live.at(r);return l.first==name&&l.second[0]==e->pose.position[0]&&l.second[1]==e->pose.position[1]&&std::fabs(l.second[2]-e->pose.position[2]-2.1)<1e-9;};
    auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);(void)me;
    // Two players near, one body object so far and no entity shown yet: a label only where a body is.
    tick();CHECK(s.shown.size()==2&&labels(s.shown,s.bodies).size()<=1&&hud.live.empty());
    run(4000);
    CHECK(labels(s.shown,s.bodies).size()==2&&hud.live.size()==2&&hud.creates==2);
    CHECK(over(50,u"Porter 50")&&over(200,u"Porter 200"));
    run(1000);CHECK(over(50,u"Porter 50")&&over(200,u"Porter 200")&&hud.creates==2);   // they walk: each label follows its own body
    // A player whose name has not arrived is "Porter".
    {auto shown=s.shown;for(auto& v:shown)v.name.clear();auto l=labels(shown,s.bodies);CHECK(l.size()==2&&l[0].name==u"Porter"&&l[1].name==u"Porter"&&l[0].key!=l[1].key);}
    // C goes stale (its body is hidden): its label goes, B's stays; back: a label again.
    c.gated=true;run(2500);CHECK(!tags.record(200)&&over(50,u"Porter 50")&&hud.live.size()==1);
    c.gated=false;run(1500);CHECK(over(200,u"Porter 200")&&hud.live.size()==2);
    // A game event here: no label; over: both again.
    s.event=true;run(100);CHECK(hud.live.empty());
    s.event=false;run(500);CHECK(hud.live.size()==2&&over(50,u"Porter 50"));
    // B leaves: its label goes with its body.
    s.hub.leave(50,s.now);tick();CHECK(hud.live.size()==1&&tags.size()==1&&over(200,u"Porter 200")&&labels(s.shown,s.bodies).size()==1);
    // A new world: the records are handed back, and the label is made again over the new body.
    auto removes=hud.removes;s.world.reload();tags.forget("world changed");CHECK(hud.removes==removes+1&&hud.live.empty());
    run(3000);CHECK(hud.live.size()==1&&over(200,u"Porter 200"));
    (void)b;
}
// What each player wears and carries reaches every player who takes it, per player, and nobody else.
void wearing(){
    using mod::gear::WornSet;using mod::carry::Summary;using mod::carryplace::Placed;
    Sim s;s.gear=s.carry=true;auto& me=s.add(100,0);auto& b=s.add(50,20);auto& c=s.add(200,40);
    s.gear=s.carry=false;auto& d=s.add(300,60);                          // D's mod has neither setting
    s.step();CHECK(s.shown.size()==3&&wearers(s.shown,s.bodies).size()<=1);   // a wearer only where a body is
    const WornSet wb{{0x1111,0}},wc{{0x2222,0},{0x3333,1}},wd{{0x4444,0}};
    const Summary sb{{501,1,0,0,2,false},{7,2,0,0,1,true}},sc{{502,1,0,0,1,true}};
    std::vector<Placed> pb(3);for(size_t i=0;i<pb.size();++i){pb[i].item=900+uint32_t(i);pb[i].rel.position={0.1*double(i),0.2,0.3};pb[i].rel.rotation={1,0,0,0,1,0,0,0,1};}
    b.worn=wb;c.worn=wc;d.worn=wd;me.worn=WornSet{{0x9999,0}};b.carried=sb;c.carried=sc;d.carried=sb;b.placed=pb;
    s.run(8000);
    auto vb=s.see(50),vc=s.see(200),vd=s.see(300);
    CHECK(vb&&vb->worn&&*vb->worn==wb&&vb->carried&&*vb->carried==sb);
    CHECK(vc&&vc->worn&&*vc->worn==wc&&vc->carried&&*vc->carried==sc&&!vc->places);   // each player's own, not another's
    CHECK(vb&&vb->places&&vb->places->size()==3&&(*vb->places)[2].item==902&&std::fabs((*vb->places)[2].rel.position[0]-0.2)<1e-3);
    CHECK(vd&&vd->connected&&!vd->worn&&!vd->carried&&!vd->places);       // D sends nothing of it
    CHECK(me.others->counters().gear==2&&me.others->counters().carried==3&&me.others->counters().events==0);
    // D takes none of it: this player's set arrived there, was acknowledged and dropped.
    auto dsees=d.others->frame(s.now+d.clock);CHECK(dsees.size()==3);
    for(auto& v:dsees)CHECK(!v.worn&&!v.carried&&!v.places);
    CHECK(d.others->counters().events>=3&&d.others->counters().gear==0&&d.others->counters().carried==0);
    // The wearers: one per body, each with its own player's set on its own player's body.
    auto w=wearers(s.shown,s.bodies);CHECK(w.size()==3);
    for(auto& x:w){CHECK(x.body&&x.body==s.bodies.entity(x.key)&&!x.local);
        CHECK(x.key==50?(x.worn&&*x.worn==wb):x.key==200?(x.worn&&*x.worn==wc):!x.worn);}
    // A changed set replaces the old one; an empty summary is "carries nothing".
    c.worn=WornSet{};c.carried=Summary{};s.run(8000);
    vc=s.see(200);CHECK(vc&&vc->worn&&vc->worn->empty()&&vc->carried&&vc->carried->empty());
    vb=s.see(50);CHECK(vb&&vb->worn&&*vb->worn==wb);
    // B's session drops: what B carried is forgotten (what B wore stays); back: B says it again.
    s.hub.drop=[](uint64_t from,uint64_t to,const std::vector<uint8_t>&){return from==50||to==50;};
    s.run(6000);vb=s.see(50);CHECK(vb&&!vb->connected&&!vb->carried&&!vb->places&&vb->worn);
    s.hub.drop=nullptr;s.run(8000);vb=s.see(50);CHECK(vb&&vb->connected&&vb->carried&&*vb->carried==sb&&vb->places&&vb->places->size()==3);
}
// Whose cases are drawn: the nearest few with a backpack copy to hang them on.
void carrying(){
    using mod::carry::Summary;using mod::carryplace::Placed;
    Sim s;s.add(100,0);for(uint64_t id=10;id<=15;++id)s.add(id,20.*double(id));
    s.run(4000);CHECK(s.bodies.made()==6);
    auto shown=s.shown;CHECK(shown.size()==6);
    // Player 15 is the nearest (10 m), 10 the farthest (60 m); each carries two cases on the back and one on an arm.
    for(auto& v:shown){CHECK(v.visible);v.render.state.position={double(16-v.id)*10,0,0};v.carried=Summary{{uint32_t(500+v.id),1,0,0,2,false},{7,2,0,0,1,false}};}
    auto at=[&](uint64_t id)->Shown& {for(auto& v:shown)if(v.id==id)return v;return shown.front();};
    auto pack=[](uint64_t id){return id==13?uint64_t(0):0x9000+id;};       // 13 wears no backpack copy
    auto keys=[](const std::vector<Carrier>& c){std::set<uint64_t> k;for(auto& x:c)k.insert(x.key);return k;};
    const std::array<double,3> viewer{0,0,0};Nearest n;
    auto c=carriers(shown,s.bodies,pack,true,&viewer,3,n);
    CHECK(c.size()==3&&(keys(c)==std::set<uint64_t>{15,14,12}));
    for(auto& x:c)CHECK(x.body==s.bodies.entity(x.key)&&x.pack==0x9000+x.key&&x.items.size()==2&&x.items[0].id==500+x.key&&!x.placed);
    CHECK(n.holds(12)&&!n.holds(11));
    // Hysteresis: 11 comes 1 m nearer than 12: 12 keeps its cases; KeepMetres nearer and more: 11 has them.
    at(11).render.state.position[0]=39;c=carriers(shown,s.bodies,pack,true,&viewer,3,n);CHECK((keys(c)==std::set<uint64_t>{15,14,12}));
    at(11).render.state.position[0]=37;c=carriers(shown,s.bodies,pack,true,&viewer,3,n);CHECK((keys(c)==std::set<uint64_t>{15,14,11}));
    // A hidden player has none, and the next one takes the place; a local game event: nobody.
    at(15).visible=false;c=carriers(shown,s.bodies,pack,true,&viewer,3,n);CHECK((keys(c)==std::set<uint64_t>{14,11,12}));
    CHECK(carriers(shown,s.bodies,pack,false,&viewer,3,n).empty());
    at(15).visible=true;
    // No limit reached: everybody with a backpack copy; never more than MaxCarriers; limit 0: nobody.
    c=carriers(shown,s.bodies,pack,true,&viewer,100,n);CHECK(c.size()==5&&!keys(c).count(13));
    CHECK(carriers(shown,s.bodies,pack,true,&viewer,0,n).empty());
    // Without a viewer: as listed.
    {Nearest fresh;c=carriers(shown,s.bodies,pack,true,nullptr,2,fresh);CHECK((keys(c)==std::set<uint64_t>{10,11}));}
    // Measured places: those cases, each with its place; a list that says nothing is carried: no carrier; nothing on
    // the back in the summary: no carrier.
    std::vector<Placed> places(3);for(size_t i=0;i<3;++i){places[i].item=900+uint32_t(i);places[i].variant=i==1;places[i].rel.position={0.1,0.2,0.3+double(i)};}
    at(14).places=places;at(15).places=std::vector<Placed>{};at(12).carried=Summary{{7,2,0,0,1,false}};
    {Nearest fresh;c=carriers(shown,s.bodies,pack,true,&viewer,8,fresh);CHECK((keys(c)==std::set<uint64_t>{14,11,10}));
     for(auto& x:c)if(x.key==14){CHECK(x.items.size()==3&&x.placed&&x.placed->size()==3&&x.items[1].id==901&&x.items[1].variant&&(*x.placed)[2].position[2]==2.3);}
                   else CHECK(!x.placed&&x.items.size()==2&&x.items[0].id==500+x.key);}
    // Many players near: never more than MaxCarriers.
    {std::vector<std::pair<uint64_t,double>> many;for(uint64_t k=1;k<=20;++k)many.push_back({k,double(k)});Nearest fresh;
     auto chosen=fresh.choose(many,MaxCarriers);size_t count=0;for(bool x:chosen)count+=x;CHECK(count==MaxCarriers&&chosen[0]&&!chosen[MaxCarriers]);}
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
    Settings st;st.self=100;st.session=777;st.cap=4;st.build=Build;st.nickname="Me";st.gear=st.carry=true;
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
    // Every other reliable event the two-player layer knows (cargo put down and taken, structures, pings, the outfit,
    // likes, enemies, orders, BTs, roads, voidouts, cart cases, order loads, shouts, vitals, story, rewards), every
    // type the core carries: acknowledged and dropped, also with gear and carried cargo on. Only 6, 7 and 16 are taken.
    {auto before=me.counters().events;unsigned sent=0;
     for(unsigned type=1;type<=ds::CargoTypeMax;++type){if(type==6||type==7||type==10||type==16)continue;
         ds::CargoEvent w;w.type=uint8_t(type);w.item=1;w.ref=1;w.position={1,2,3};raw.core->queueCargo(w);++sent;}
     run(4000);CHECK(raw.core->cargoPending()==0&&me.counters().events==before+sent);
     f=me.frame(now);CHECK(f.size()==1&&!f[0].worn&&!f[0].carried&&!f[0].places&&me.counters().gear==0&&me.counters().carried==0);
     mod::gear::WornSet set{{0xabc,2}};mod::carry::Summary sum{{77,1,0,0,3,false}};std::vector<mod::carryplace::Placed> where(1);where[0].item=77;
     for(auto& w:mod::gear::encode(set,1))raw.core->queueCargo(w);
     for(auto& w:mod::carry::encode(sum,1))raw.core->queueCargo(w);
     for(auto& w:mod::carryplace::encode(where,1))raw.core->queueCargo(w);
     run(1500);f=me.frame(now);
     CHECK(f.size()==1&&f[0].worn&&*f[0].worn==set&&f[0].carried&&*f[0].carried==sum&&f[0].places&&f[0].places->size()==1);
     CHECK(me.counters().events==before+sent&&me.counters().gear==1&&me.counters().carried==2);
     // What this porter wears and carries goes out as those three types and nothing else.
     raw.core->takeCargo();
     for(uint64_t t=0;t<8000;t+=10){me.wears(set,now);me.carries(sum,where,now);run(10);}
     std::set<uint8_t> types;for(auto& g:raw.core->takeCargo())types.insert(g.type);
     CHECK((types==std::set<uint8_t>{6,7,16}));}
    // Leaving says goodbye once: their side lets go without waiting for the timeout.
    me.left(10,now);while(!raw.toRaw.empty()){auto b=raw.toRaw.front();raw.toRaw.pop_front();raw.core->receive(b.data(),b.size(),now);}
    CHECK(!raw.core->connected()&&me.size()==0);
}
}
int main(){
    pairing();several();stale();leaving();limits();isolation();identity();jump();animation();reload();showing();labelling();wearing();carrying();kinds();
    if(failures){std::fprintf(stderr,"others: %u failures\n",failures);return 1;}
    std::puts("others: sessions per player, bodies per player, limits, isolation, staleness and leaving hold");
    return 0;
}
