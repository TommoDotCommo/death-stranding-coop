// The relay's fight channel (relay.h, "Fights with visitors"; core/broadcast.h types 11..16) with real players' sides
// (core/broadcast_client.h) and raw datagrams: a fight offered to the players in reach and to nobody else, joined up to
// its cap, the owner's state to its joined visitors only, a visitor's report to the owner only and with the visitor as
// its source, nothing from a player who has not joined, one role at a time, and the visitors released when the owner
// closes, leaves, goes silent or restarts, and when a visitor goes out of reach.
#include "relay.h"
#include "../core/broadcast_client.h"
#include <cmath>
#include <cstdio>
#include <memory>
using namespace world;
namespace bc=ds::broadcast;
using ds::relay::Buffer;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"fight relay oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)

struct Sim {
    struct Peer {
        uint64_t id=0;RelayAddress address;std::unique_ptr<bc::Client> client;bool silent=false,keepless=false;   // keepless: its FIGHTs are lost
        std::vector<Buffer> got;std::vector<bc::FightNews> news;
        // What arrived of one type (and, for DELIVER, from one source).
        std::vector<bc::Message> of(bc::Type t,uint64_t source=0) const {
            std::vector<bc::Message> out;
            for(auto& b:got){bc::Message m;if(bc::parse(b.data(),b.size(),m)==ds::relay::Error::None&&m.type==t&&(!source||m.post.player==source))out.push_back(m);}
            return out;}
        std::vector<bc::FightStatus> statuses() const {std::vector<bc::FightStatus> out;for(auto& m:of(bc::Type::Status))out.push_back(m.status.status);return out;}
        size_t reports(uint64_t source) const {size_t n=0;for(auto& x:news)n+=x.delivered&&x.source==source;return n;}
    };
    RelayConfig config;std::unique_ptr<Relay> relay;std::vector<std::unique_ptr<Peer>> peers;std::map<RelayAddress,Peer*> byAddress;
    std::deque<std::pair<RelayAddress,Buffer>> up,down;uint64_t now=100000;
    explicit Sim(RelayConfig c={}):config(std::move(c)){
        config.token="group";
        relay=std::make_unique<Relay>(config,[this](const RelayAddress& a,const uint8_t* d,size_t n){down.push_back({a,Buffer(d,d+n)});});}
    Peer& add(uint64_t id,std::array<double,3> at,bc::Show show=bc::Show::Everyone){
        auto p=std::make_unique<Peer>();auto* raw=p.get();p->id=id;p->address={0x0a000000u+uint32_t(peers.size())+1,uint16_t(40000+peers.size())};
        bc::ClientConfig c;c.id=id;c.token="group";c.nickname="P"+std::to_string(id);c.show=show;
        p->client=std::make_unique<bc::Client>(c,[this,raw](const uint8_t* d,size_t n){if(raw->keepless&&n>6&&d[6]==uint8_t(bc::Type::Fight))return;up.push_back({raw->address,Buffer(d,d+n)});});
        p->client->setPosition(at);byAddress[p->address]=raw;peers.push_back(std::move(p));return *raw;}
    void pump(){
        while(!up.empty()||!down.empty()){
            if(!up.empty()){auto d=std::move(up.front());up.pop_front();relay->receive(d.first,d.second.data(),d.second.size(),now);continue;}
            auto d=std::move(down.front());down.pop_front();auto it=byAddress.find(d.first);if(it==byAddress.end())continue;
            auto& p=*it->second;p.got.push_back(d.second);if(p.silent)continue;
            p.client->receive(d.second.data(),d.second.size(),now);p.client->takeEvents();
            for(auto& n:p.client->takeFightNews())p.news.push_back(std::move(n));}
    }
    void raw(const Peer& from,const Buffer& b){relay->receive(from.address,b.data(),b.size(),now);pump();}
    void step(uint64_t ms=50){now+=ms;for(auto& p:peers)if(!p->silent)p->client->tick(now);pump();relay->tick(now);pump();}
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=50)step();}
    void clear(){for(auto& p:peers){p->got.clear();p->news.clear();}}
};
static bc::FightInfo fightAt(std::array<double,3> place,uint32_t id=7,uint8_t most=9,uint16_t radius=50){
    bc::FightInfo f;f.fight=id;f.kind=1;f.most=most;f.radius=radius;f.place=place;return f;}
static Buffer postRaw(uint64_t player,uint64_t owner,uint32_t fight,uint32_t version,std::vector<uint8_t> bytes){
    bc::Post p;p.player=player;p.owner=owner;p.fight=fight;p.version=version;p.payload=bytes.data();p.size=bytes.size();return bc::encodePost(p);}
static Buffer joinRaw(uint64_t player,uint64_t owner,uint32_t fight){bc::Join j;j.player=player;j.owner=owner;j.fight=fight;return bc::encode(j);}

// Who is offered the fight, who may join, who gets what.
static void visitors(){
    RelayConfig c;c.fightVisitors=2;Sim s(c);
    auto& o=s.add(1,{0,0,0});auto& v1=s.add(2,{10,0,0});auto& v2=s.add(3,{20,0,0});auto& n=s.add(4,{5,0,0});
    auto& far=s.add(5,{100,0,0});       // near the owner for the relay (150 m), beyond the fight's 50 m
    auto& shy=s.add(6,{6,0,0},bc::Show::Nobody);
    s.run(1000);CHECK(s.relay->areNear(1,5)&&s.relay->areNear(1,4));
    o.client->hostFight(fightAt({0,0,0}),s.now);s.run(200);
    CHECK(s.relay->fightOf(1)==7&&o.client->fightsHere());
    // Offered to the players in reach who would be shown the owner; not to one beyond the radius or one who sees nobody.
    CHECK(v1.of(bc::Type::Offer).size()==1&&v2.of(bc::Type::Offer).size()==1&&n.of(bc::Type::Offer).size()==1);
    CHECK(far.of(bc::Type::Offer).empty()&&shy.of(bc::Type::Offer).empty()&&o.of(bc::Type::Offer).empty());
    {auto offer=v1.of(bc::Type::Offer)[0].offer;CHECK(offer.owner==1&&offer.info.fight==7&&offer.info.radius==50&&offer.visitors==0);}
    // Again with the owner's keep, once a second.
    s.run(2000);CHECK(v1.of(bc::Type::Offer).size()==3);
    // Two join; the cap (the server's 2) refuses the third; the one beyond the radius is refused for it.
    CHECK(v1.client->joinFight(1,7,s.now)&&v2.client->joinFight(1,7,s.now));s.run(200);
    CHECK(v1.client->joined()&&v2.client->joined()&&s.relay->visitorsOf(1)==2&&o.client->visitors()==2&&s.relay->visiting(2)==1);
    n.client->joinFight(1,7,s.now);s.run(200);CHECK(!n.client->joined()&&!n.client->visitingOwner()&&n.statuses().back()==bc::FightStatus::Full);
    far.client->joinFight(1,7,s.now);s.run(200);CHECK(!far.client->joined()&&far.statuses().back()==bc::FightStatus::Far);
    s.raw(n,joinRaw(4,1,8));CHECK(n.statuses().back()==bc::FightStatus::NoFight);   // another fight id
    CHECK(s.relay->visitorsOf(1)==2&&s.relay->stats().joinsRefused>=3);
    // A visitor offered no more.
    s.clear();s.run(1500);CHECK(v1.of(bc::Type::Offer).empty()&&!n.of(bc::Type::Offer).empty());
    // The owner's state: to both visitors, from the owner; to nobody else (the near player, the far one, the shy one).
    s.clear();CHECK(o.client->postFight({1,2,3}));s.pump();
    CHECK(v1.of(bc::Type::Deliver,1).size()==1&&v2.of(bc::Type::Deliver,1).size()==1&&v1.reports(1)==1&&v2.reports(1)==1);
    for(auto* p:{&n,&far,&shy,&o})CHECK(p->of(bc::Type::Deliver).empty());
    // A visitor's report: to the owner, with the visitor as its source; never to the other visitor.
    s.clear();CHECK(v1.client->postFight({9}));s.pump();
    CHECK(o.of(bc::Type::Deliver,2).size()==1&&o.reports(2)==1&&o.of(bc::Type::Deliver).size()==1);
    CHECK(v2.of(bc::Type::Deliver).empty()&&n.of(bc::Type::Deliver).empty());
    // A report repeated or older: once. One from a player who has not joined, or naming another sender: nothing.
    s.clear();s.raw(v1,postRaw(2,1,7,1,{9}));s.raw(n,postRaw(4,1,7,5,{6}));s.raw(far,postRaw(5,1,7,5,{6}));s.raw(v2,postRaw(2,1,7,9,{6}));
    CHECK(o.of(bc::Type::Deliver).empty()&&v1.of(bc::Type::Deliver).empty()&&v2.of(bc::Type::Deliver).empty());
    // A visitor's POST that names himself the owner, or posts the state of the fight he visits: no state for anyone.
    s.clear();s.raw(v1,postRaw(2,2,7,3,{6}));CHECK(v2.of(bc::Type::Deliver).empty()&&o.of(bc::Type::Deliver).empty());
    // The latest state goes at once to whoever joins later.
    o.client->postFight({4,4});s.pump();v2.client->leaveFight();s.run(100);
    CHECK(v2.statuses().back()==bc::FightStatus::Left&&s.relay->visitorsOf(1)==1);
    s.clear();n.client->joinFight(1,7,s.now);s.run(200);
    {auto d=n.of(bc::Type::Deliver,1);CHECK(n.client->joined()&&d.size()==1&&d[0].post.size==2&&d[0].post.payload[0]==4);}
    // One role at a time: the owner joins nothing; a visitor who opens a fight of his own leaves the one he visits.
    auto& other=s.add(7,{8,0,0});s.run(1000);other.client->hostFight(fightAt({8,0,0},3),s.now);s.run(200);
    CHECK(!o.client->joinFight(7,3,s.now));s.raw(o,joinRaw(1,7,3));CHECK(o.statuses().back()==bc::FightStatus::Busy&&s.relay->visiting(1)==0);
    n.client->hostFight(fightAt({5,0,0},11),s.now);s.run(200);
    CHECK(s.relay->visiting(4)==0&&s.relay->fightOf(4)==11&&s.relay->visitorsOf(1)==1&&!n.client->visitingOwner());
    s.raw(n,joinRaw(4,7,3));CHECK(n.statuses().back()==bc::FightStatus::Busy);
    // A visitor who goes beyond the radius and the margin is released; within the margin he stays.
    v1.client->setPosition({90,0,0});s.run(600);CHECK(s.relay->visiting(2)==1);
    v1.client->setPosition({110,0,0});s.run(600);CHECK(s.relay->visiting(2)==0&&!v1.client->joined()&&v1.statuses().back()==bc::FightStatus::Released);
}

// How a fight ends for its visitors.
static void endings(){
    Sim s;auto& o=s.add(1,{0,0,0});auto& v=s.add(2,{10,0,0});auto& w=s.add(3,{12,0,0});s.run(1000);
    auto visit=[&](uint32_t id){o.client->hostFight(fightAt({0,0,0},id),s.now);s.run(200);v.client->joinFight(1,id,s.now);w.client->joinFight(1,id,s.now);s.run(200);
        CHECK(v.client->joined()&&w.client->joined());};
    // Closed by its owner.
    visit(1);o.client->closeFight();s.run(100);
    CHECK(v.statuses().back()==bc::FightStatus::Closed&&w.statuses().back()==bc::FightStatus::Closed&&!v.client->joined()&&!s.relay->fightOf(1));
    // Replaced by another fight of the same owner.
    visit(2);o.client->hostFight(fightAt({0,0,0},3),s.now);s.run(100);CHECK(v.statuses().back()==bc::FightStatus::Closed&&s.relay->visiting(2)==0);
    // The owner keeps HELLO but stops keeping the fight: after the silence, Gone.
    o.client->closeFight();s.run(100);visit(4);
    o.keepless=true;s.run(2000);CHECK(s.relay->visiting(2)==1);
    s.run(1500);CHECK(s.relay->visiting(2)==0&&v.statuses().back()==bc::FightStatus::Gone&&w.statuses().back()==bc::FightStatus::Gone&&!s.relay->fightOf(1));
    // The owner leaves (BYE): Gone at once.
    o.keepless=false;o.client->closeFight();s.run(1000);visit(5);o.client->bye();s.pump();
    CHECK(v.statuses().back()==bc::FightStatus::Gone&&s.relay->visiting(2)==0&&s.relay->visiting(3)==0&&!v.client->joined());
    // The owner times out: Gone.
    o.client->setPosition({0,0,0});s.run(1000);visit(6);o.silent=true;s.run(11000);
    CHECK(!s.relay->known(1)&&v.statuses().back()==bc::FightStatus::Gone&&s.relay->visiting(2)==0);
    // A visitor who leaves the server: the owner's count goes down.
    o.silent=false;s.run(1000);visit(7);w.client->bye();s.pump();CHECK(s.relay->visitorsOf(1)==1&&o.client->visitors()==1);
    // A server that takes no fights says so to the owner.
    RelayConfig off;off.fightVisitors=0;Sim t(off);auto& a=t.add(1,{0,0,0});auto& b=t.add(2,{3,0,0});t.run(1000);
    a.client->hostFight(fightAt({0,0,0}),t.now);t.run(200);CHECK(a.statuses().back()==bc::FightStatus::Off&&!a.client->fightsHere()&&b.of(bc::Type::Offer).empty()&&!t.relay->fightOf(1));
    // The owner's own cap below the server's.
    Sim u;auto& x=u.add(1,{0,0,0});auto& y=u.add(2,{3,0,0});auto& z=u.add(3,{4,0,0});u.run(1000);
    x.client->hostFight(fightAt({0,0,0},1,1),u.now);u.run(200);y.client->joinFight(1,1,u.now);u.run(200);z.client->joinFight(1,1,u.now);u.run(200);
    CHECK(y.client->joined()&&!z.client->joined()&&z.statuses().back()==bc::FightStatus::Full);
}

// Nine visitors (ten Sams), each sending reports, the owner its state four times a second: every visitor has every state,
// the owner every report, and no visitor any other visitor's.
static void ten(){
    Sim s;auto& o=s.add(1,{0,0,0});std::vector<Sim::Peer*> v;
    for(uint64_t i=0;i<10;++i)v.push_back(&s.add(10+i,{double(i),0,5}));
    s.run(1000);o.client->hostFight(fightAt({0,0,0}),s.now);s.run(200);
    for(auto* p:v)p->client->joinFight(1,7,s.now);
    s.run(200);size_t joined=0;for(auto* p:v)joined+=p->client->joined();
    CHECK(joined==9&&s.relay->visitorsOf(1)==9);
    s.clear();
    for(int i=0;i<20;++i){o.client->postFight({uint8_t(i)});for(auto* p:v)p->client->postFight({uint8_t(p->id)});s.step(250);}
    for(auto* p:v){
        if(!p->client->joined()){CHECK(p->of(bc::Type::Deliver).empty());continue;}
        CHECK(p->reports(1)==20&&p->of(bc::Type::Deliver).size()==20);
        CHECK(o.reports(p->id)==20);}
}

int main(){
    visitors();endings();ten();
    if(failures)return 1;
    std::puts("fight relay tests passed");return 0;
}
