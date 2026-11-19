// Crowd (crowd.h), game-free: several players' Crowds, and a bare broadcast client that posts what this test writes,
// through the players' own server (world/relay.h) in memory, each player on a clock of its own. Every protection here
// has a mutant (MUTANT_CROWD_*; mod/Makefile) that this test must fail.
#include "crowd.h"
#include "controls.h"
#include "../world/relay.h"
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>
using namespace mod;namespace bc=ds::broadcast;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"crowd oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr unsigned Pulse=7;
const std::array<uint8_t,16> Build{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
std::vector<ds::CargoEvent> join(std::vector<ds::CargoEvent> a,const std::vector<ds::CargoEvent>& b){a.insert(a.end(),b.begin(),b.end());return a;}
// One player: a Crowd (or, with bare, only a broadcast client) standing at `at`, with what its porter wears and carries.
struct Player {
    uint64_t id=0;world::RelayAddress address;uint64_t offset=0;std::array<double,3> at{};bool frozen=false,gated=false;
    std::unique_ptr<crowd::Crowd> crowd;std::unique_ptr<bc::Client> bare;
    std::optional<gear::WornSet> worn;std::optional<carry::Summary> carried;std::optional<std::vector<carryplace::Placed>> placed;
    std::map<uint64_t,others::Shown> seen;std::map<uint64_t,unsigned> pulsesFrom,cartsFrom;
};
struct Sim {
    struct Datagram {bool up=false;world::RelayAddress address;std::vector<uint8_t> bytes;};
    uint64_t now=100000;std::deque<Datagram> queue;std::vector<std::unique_ptr<Player>> players;
    bool gear=false,carry=false;size_t cap=8;
    static world::RelayConfig relayConfig(){world::RelayConfig c;c.token="k";c.seed=7;c.maxPlayers=64;return c;}
    world::Relay relay{relayConfig(),[this](const world::RelayAddress& to,const uint8_t* d,size_t n){queue.push_back({false,to,{d,d+n}});}};
    uint64_t clock(const Player& p) const {return now+p.offset;}
    Player& add(uint64_t id,std::array<double,3> at,bool bare=false){
        auto p=std::make_unique<Player>();p->id=id;p->at=at;p->offset=id*7919;p->address={0x7f000001,uint16_t(40000+players.size())};
        auto address=p->address;auto send=[this,address](const uint8_t* d,size_t n){queue.push_back({true,address,{d,d+n}});};
        bc::ClientConfig c;c.token="k";c.nickname="Porter "+std::to_string(id);c.id=id;c.build=Build;   // the server puts only the same build near
        if(bare)p->bare=std::make_unique<bc::Client>(c,send);
        else{others::Settings s;s.self=id;s.cap=cap;s.build=Build;s.nickname=c.nickname;s.gear=gear;s.carry=carry;
             p->crowd=std::make_unique<crowd::Crowd>(s,c,send,[](const std::string&){});}
        players.push_back(std::move(p));return *players.back();}
    others::Local local(const Player& p) const {
        others::Local l;l.available=true;l.gated=p.gated;l.sampledAt=clock(p);l.state.position=p.at;return l;}
    // One 20 ms step: every player's pass, then what is due is handed over (answers wait for the next step).
    void step(){
        now+=20;
        for(auto& p:players){
            auto t=clock(*p);
            if(p->bare){p->bare->setPosition(p->at);if(!p->frozen)p->bare->setLocal(local(*p).state,true,false,t,false);p->bare->tick(t);p->bare->takeEvents();continue;}
            if(!p->frozen)p->crowd->local(local(*p));
            p->crowd->wears(p->worn,t);p->crowd->carries(p->carried,p->placed,t);
            p->crowd->tick(t);
        }
        size_t due=queue.size();
        for(size_t i=0;i<due;++i){
            auto d=std::move(queue.front());queue.pop_front();
            if(d.up){relay.receive(d.address,d.bytes.data(),d.bytes.size(),now);continue;}
            for(auto& p:players)if(p->address==d.address){
                if(p->bare)p->bare->receive(d.bytes.data(),d.bytes.size(),clock(*p));else p->crowd->receive(d.bytes.data(),d.bytes.size(),clock(*p));}
        }
        relay.flush(now);if(now%250==0)relay.tick(now);
        for(auto& p:players){
            if(!p->crowd)continue;
            p->seen.clear();
            for(auto& v:p->crowd->frame(clock(*p))){
                if(v.animation&&v.animation->fresh&&controls::bit(v.animation->animation.rose,Pulse))++p->pulsesFrom[v.id];
                if(v.animation)p->cartsFrom[v.id]=std::max<unsigned>(p->cartsFrom[v.id],v.animation->animation.cartCount);
                p->seen[v.id]=v;}
        }
    }
    void run(uint64_t ms){for(uint64_t end=now+ms;now<end;)step();}
};
bool near(const std::array<double,3>& a,const std::array<double,3>& b,double reach){for(unsigned i=0;i<3;++i)if(std::fabs(a[i]-b[i])>reach)return false;return true;}
std::vector<carryplace::Placed> cases(uint32_t item,unsigned n){
    std::vector<carryplace::Placed> out;
    for(unsigned i=0;i<n;++i){carryplace::Placed p;p.item=item;p.slot=1;p.rel.position={0.05*i,0.3,-0.2};p.rel.rotation={1,0,0,0,1,0,0,0,1};out.push_back(p);}
    return out;}

// The blob: the core's CARGO fields, refused whole for any length, type or value the core's decoder would refuse.
void blobs(){
    auto places=carryplace::encode(cases(901,3),5);
    auto b=crowd::pack(places);CHECK(b.size()==1+3*crowd::EventBytes&&b[0]==3);
    auto back=crowd::unpack(b,carryplace::WireType);CHECK(back&&back->size()==3);
    if(back)for(size_t i=0;i<3;++i){auto& x=(*back)[i];auto& y=places[i];
        CHECK(x.type==y.type&&x.item==y.item&&x.ref==y.ref&&std::fabs(x.yaw-y.yaw)<1e-4);
        for(unsigned k=0;k<3;++k)CHECK(std::fabs(x.position[k]-std::round(y.position[k]*100)/100)<1e-9);}
    {carryplace::Receiver r;std::optional<std::vector<carryplace::Placed>> got;for(auto& e:*back)got=r.feed(e);
     CHECK(got&&got->size()==3&&(*got)[2].item==901&&std::fabs((*got)[2].rel.position[0]-0.1)<1e-3);}
    // Length: one byte more or less, a count that does not match.
    {auto more=b;more.push_back(0);CHECK(!crowd::unpack(more,carryplace::WireType));}
    {auto less=b;less.pop_back();CHECK(!crowd::unpack(less,carryplace::WireType));}
    {auto count=b;count[0]=2;CHECK(!crowd::unpack(count,carryplace::WireType));}
    CHECK(!crowd::unpack({},carryplace::WireType)&&!crowd::unpack({0},carryplace::WireType));
    // Type: events of another kind.
    CHECK(!crowd::unpack(b,nametag::WireType)&&!crowd::unpack(crowd::pack(nametag::encode("Ann")),gear::GearWireType));
    CHECK(crowd::unpack(crowd::pack(nametag::encode("Ann")),nametag::WireType));
    // Range: a t-field beyond 2^53, a place beyond 100 km, a yaw beyond pi.
    {auto x=b;uint64_t ref=(1ull<<53)+1;for(unsigned i=0;i<8;++i)x[1+5+i]=uint8_t(ref>>(8*i));CHECK(!crowd::unpack(x,carryplace::WireType));}
    {auto x=b;uint32_t cm=10000001;for(unsigned i=0;i<4;++i)x[1+13+i]=uint8_t(cm>>(8*i));CHECK(!crowd::unpack(x,carryplace::WireType));}
    {auto x=b;x[1+25]=0x00;x[1+26]=0x80;CHECK(!crowd::unpack(x,carryplace::WireType));}
    // pack refuses what the wire cannot carry.
    CHECK(crowd::pack({}).empty());
    {std::vector<ds::CargoEvent> many(crowd::MaxEvents+1);for(auto& e:many)e.type=6;CHECK(crowd::pack(many).empty());many.pop_back();CHECK(!crowd::pack(many).empty());}
    {ds::CargoEvent e;e.type=6;e.ref=(1ull<<53)+1;CHECK(crowd::pack({e}).empty());}
    {ds::CargoEvent e;e.type=6;e.position={100000.01,0,0};CHECK(crowd::pack({e}).empty());}
    {ds::CargoEvent e;e.type=6;e.yaw=3.2;CHECK(crowd::pack({e}).empty());}
}

// Three players near each other; Ann and Bob show gear and cases, Cee has neither setting.
void shown(){
    Sim sim;sim.gear=sim.carry=true;
    auto& a=sim.add(1,{0,0,0});auto& b=sim.add(2,{5,0,0});sim.gear=sim.carry=false;auto& c=sim.add(3,{0,0,8});
    a.worn=gear::WornSet{{gear::helperHash("HLP_Backpack"),0}};
    b.worn=gear::WornSet{{gear::helperHash("HLP_Backpack"),0},{gear::helperHash("HLP_Hcuf"),0}};
    c.worn=gear::WornSet{{gear::helperHash("HLP_Hcuf"),0}};
    a.carried=carry::parseSpec("777");b.carried=carry::parseSpec("501:1:3");c.carried=carry::parseSpec("503:1:4");
    b.placed=cases(501,3);c.placed=cases(503,4);
    sim.run(8000);   // a worn set goes once it has held for 5 s
    CHECK(a.seen.size()==2&&a.seen.count(2)&&a.seen.count(3)&&a.crowd->size()==2&&a.crowd->connected()==2);
    if(a.seen.size()!=2||c.seen.size()!=2)return;
    auto& bob=a.seen[2];auto& cee=a.seen[3];
    CHECK(bob.connected&&bob.visible&&cee.connected&&cee.visible);
    CHECK(bob.name=="Porter 2"&&cee.name=="Porter 3"&&c.seen[1].name=="Porter 1"&&b.seen[1].name=="Porter 1");
    CHECK(near(bob.render.state.position,b.at,0.05)&&near(cee.render.state.position,c.at,0.05));
    // Bob's sets on Bob; Cee sends none of hers.
    CHECK(bob.worn==b.worn&&bob.carried==b.carried&&bob.places&&bob.places->size()==3&&(*bob.places)[0].item==501);
    CHECK(!cee.worn&&!cee.carried&&!cee.places);
    // Cee takes none of Ann's: they arrived and were dropped.
    CHECK(!c.seen[1].worn&&!c.seen[1].carried&&!c.seen[1].places);
    auto& kc=c.crowd->counters();CHECK(kc.gear==0&&kc.carried==0&&kc.events>0);
    // Bob takes Ann's (worn; carried without places: she measured none).
    CHECK(b.seen[1].worn==a.worn&&b.seen[1].carried==a.carried&&!b.seen[1].places);
    auto& ka=a.crowd->counters();CHECK(ka.gear>=1&&ka.carried>=2&&ka.malformed==0&&ka.unknown==0&&ka.refused==0&&ka.events==0&&ka.sent>0&&ka.received>0&&ka.accepted>0);
    // This game in a cutscene: nobody is shown, from that frame on.
    a.gated=true;a.crowd->local(sim.local(a));
    for(auto& v:a.crowd->frame(sim.clock(a)))CHECK(!v.visible);
    a.gated=false;a.crowd->local(sim.local(a));
}

// At most `cap` players; the one over it waits and is taken when one of them leaves. Who leaves is gone.
void cap(){
    Sim sim;sim.cap=2;auto& a=sim.add(1,{0,0,0});sim.cap=8;
    for(uint64_t id:{2,3,4})sim.add(id,{double(id),0,0});
    sim.run(3000);
    CHECK(a.crowd->size()==2&&a.seen.size()==2&&a.crowd->waiting()==1&&a.crowd->counters().full>=1);
    std::set<uint64_t> before;for(auto& [id,v]:a.seen)before.insert(id);
    uint64_t waiting=0;for(uint64_t id:{2,3,4})if(!before.count(id))waiting=id;
    uint64_t leaver=*before.begin();
    for(auto& p:sim.players)if(p->id==leaver)p->at={1000,0,0};   // farther than the server's 200 m
    sim.run(3000);
    CHECK(a.crowd->size()==2&&a.crowd->waiting()==0&&!a.seen.count(leaver)&&a.seen.count(waiting)&&a.seen[waiting].connected);
    for(auto& p:sim.players)if(p->id==leaver)CHECK(p->seen.empty());
}

// A player who goes quiet (no new pose) is not connected and nothing he carried is shown; heard again, it is back.
void quiet(){
    Sim sim;sim.gear=sim.carry=true;auto& a=sim.add(1,{0,0,0});auto& b=sim.add(2,{4,0,0});
    b.carried=carry::parseSpec("501:1:3");b.placed=cases(501,3);
    sim.run(3000);
    CHECK(a.seen.count(2)&&a.seen[2].carried==b.carried&&a.seen[2].places);
    b.frozen=true;sim.run(4000);
    CHECK(a.seen.count(2)&&!a.seen[2].connected&&!a.seen[2].visible&&!a.seen[2].carried&&!a.seen[2].places&&a.crowd->connected()==0);
    b.frozen=false;sim.run(2000);
    CHECK(a.seen.count(2)&&a.seen[2].connected&&a.seen[2].carried==b.carried&&a.seen[2].places&&a.seen[2].places->size()==3);
}

// A pulse in a sample the client did not take (too soon after the last) goes with the next one.
void pulses(){
    Sim sim;auto& a=sim.add(1,{0,0,0});auto& b=sim.add(2,{4,0,0});
    sim.run(2000);
    ds::Animation plain,pulse,later;controls::setBit(pulse.rose,Pulse,true);later.floats[0]=0.5f;
    CHECK(a.crowd->animation(plain,sim.clock(a)));
    sim.step();CHECK(!a.crowd->animation(pulse,sim.clock(a)));
    sim.step();sim.step();sim.step();CHECK(a.crowd->animation(later,sim.clock(a)));
    sim.run(1500);
    CHECK(b.pulsesFrom[1]==1);
    // The carts he tows travel with his controls (others_carts shows them at his body).
    ds::Animation towing;towing.floats[0]=0.25f;towing.cartCount=1;towing.carts[0].offset={1500,0,0};
    sim.step();sim.step();sim.step();CHECK(a.crowd->animation(towing,sim.clock(a)));
    sim.run(1500);CHECK(b.cartsFrom[1]==1);
}

// A blob is one complete sender output: two names in one are refused; one name alone is taken.
void whole(){
    Sim sim;auto& a=sim.add(1,{0,0,0});auto& e=sim.add(9,{3,0,0},true);
    e.bare->setState(bc::StateKind::Name,crowd::pack(join(nametag::encode("Ann"),nametag::encode("Eve"))));
    sim.run(2000);
    CHECK(a.seen.count(9)&&a.seen[9].name.empty()&&a.crowd->counters().malformed==1);
    e.bare->setState(bc::StateKind::Name,crowd::pack(nametag::encode("Eve")));
    sim.run(1000);
    CHECK(a.seen.count(9)&&a.seen[9].name=="Eve"&&a.crowd->counters().malformed==1);
    // A gear blob of carried-case events, from a game with the settings on: refused, nothing worn.
    Sim sim2;sim2.gear=sim2.carry=true;auto& a2=sim2.add(1,{0,0,0});auto& e2=sim2.add(9,{3,0,0},true);
    e2.bare->setState(bc::StateKind::Gear,crowd::pack(carry::encode(*carry::parseSpec("501:1:3"),1)));
    sim2.run(2000);
    CHECK(a2.seen.count(9)&&!a2.seen[9].worn&&a2.crowd->counters().malformed==1&&a2.crowd->counters().gear==0);
}
}
int main(){
    blobs();shown();cap();quiet();pulses();whole();
    if(failures){std::fprintf(stderr,"crowd: %u checks failed\n",failures);return 1;}
    std::puts("crowd: blobs, three players with and without gear and cases, the cap, quiet and back, pulses, whole sets passed");
    return 0;
}
