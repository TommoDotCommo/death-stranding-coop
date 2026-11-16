// Presence across linked servers (relay.h, presence.h) with a clock of its own and a network in memory: two or three
// relays, each on a clock of its own, linked to each other, with real players' sides (core/broadcast_client.h Client)
// on each. A player of one server near a player of the other sees him, with his animation and his name followed by his
// server's name for the link, under an id of this server's; a player far from everyone of the other server is not
// sent; the byte budget holds, nearest players first; a link that goes quiet loses its players, and a player who
// leaves his server leaves the other; nothing goes round a triangle of servers; a player here takes his id back from
// a linked server's player, whose state never lands on a player here, and whose build keeps him apart; a datagram
// without the link's tag, or one seen before, changes nothing.
#include "relay.h"
#include "../core/broadcast_client.h"
#include <cmath>
#include <cstdio>
#include <memory>
using namespace world;
namespace bc=ds::broadcast;
using ds::relay::Buffer;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"linked presence oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static bc::BuildId buildOf(uint8_t seed){bc::BuildId b{};for(size_t i=0;i<b.size();++i)b[i]=uint8_t(seed+i);return b;}
static std::function<ds::State(double)> circle(std::array<double,3> centre){
    return [centre](double t){double a=t*0.75;ds::State s;s.position={centre[0]+2*std::cos(a),centre[1],centre[2]+2*std::sin(a)};
        s.velocity={-1.5*std::sin(a),0,1.5*std::cos(a)};s.yaw=0.5;s.action=3;s.phase=0.25;return s;};}
static std::function<ds::State(double)> standing(std::array<double,3> at){return [at](double){ds::State s;s.position=at;s.action=3;return s;};}
static double distance(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);}
static std::string nameShown(const bc::Client& c,uint64_t source){
    auto* b=c.state(source,bc::StateKind::Name);if(!b)return "(none)";auto n=presence::nameOf(b->data(),b->size());return n?*n:"(not a name)";}

struct Net {
    struct Server {std::string name;RelayAddress address;int64_t skew=0;RelayConfig config;std::unique_ptr<Relay> relay;uint32_t seed=1;
                   uint64_t clock(uint64_t now) const {return uint64_t(int64_t(now)+skew);}};
    struct Peer {
        uint64_t id=0;Server* server=nullptr;RelayAddress address;int64_t skew=0;bool silent=false;uint32_t area=0;
        std::unique_ptr<bc::Client> client;std::unique_ptr<ds::relay::Client> pairs;bc::BuildId build{};uint32_t game=ds::GameBuild;std::string nickname;
        std::function<ds::State(double)> pose;bool jump=false,animate=false;unsigned pulses=0;
        std::vector<std::pair<uint64_t,unsigned>> pulsesSent;std::map<uint64_t,std::set<unsigned>> pulsesGot;
        std::vector<Buffer> got;
        uint64_t clock(uint64_t now) const {return uint64_t(int64_t(now)+skew);}
        bool sees(uint64_t other) const {return client?client->nearby().count(other)!=0:pairs->nearby().count(other)!=0;}
        std::string nickOf(uint64_t other) const {auto it=client->nearby().find(other);return it==client->nearby().end()?"":it->second;}
        size_t entries(uint64_t source,uint8_t parts=0) const {
            size_t n=0;
            for(auto& b:got){bc::Message m;if(bc::parse(b.data(),b.size(),m)!=ds::relay::Error::None||m.type!=bc::Type::Frame)continue;
                for(auto& u:m.frame.entries)if(u.player==source&&(u.parts&parts)==parts)++n;}
            return n;}
    };
    struct Datagram {RelayAddress from,to;Buffer bytes;};
    uint64_t now=100000;std::vector<std::unique_ptr<Server>> servers;std::vector<std::unique_ptr<Peer>> peers;
    std::map<RelayAddress,Server*> serverAt;std::map<RelayAddress,Peer*> peerAt;std::deque<Datagram> queue;
    std::function<bool(const Datagram&)> drop;   // between servers: true drops it
    std::vector<Datagram> between;                // every datagram between servers that was delivered
    uint64_t lost=0;
    Server& server(const std::string& name,int64_t skew){
        auto s=std::make_unique<Server>();s->name=name;s->skew=skew;s->address={0x0a640000u+uint32_t(servers.size())+1,9000};
        s->config.token="group";s->config.serverId="server"+name;s->config.seed=uint32_t(11+servers.size()*31);s->seed=s->config.seed;
        serverAt[s->address]=s.get();servers.push_back(std::move(s));return *servers.back();}
    // A link both ways; `known`: each side is given the other's address (false: only `a` is, `b` learns it).
    void link(Server& a,Server& b,const std::string& key,uint32_t budget=250000,size_t players=64,bool known=true){
        RelayLink x;x.name=b.name;x.key=key;x.peer=b.address;x.budget=budget;x.players=players;a.config.links.push_back(x);
        RelayLink y;y.name=a.name;y.key=key;if(known)y.peer=a.address;y.budget=budget;y.players=players;b.config.links.push_back(y);}
    void start(Server& s){
        auto* raw=&s;s.relay=std::make_unique<Relay>(s.config,[this,raw](const RelayAddress& to,const uint8_t* d,size_t n){queue.push_back({raw->address,to,Buffer(d,d+n)});});}
    void startAll(){for(auto& s:servers)start(*s);}
    void restart(Server& s){s.config.seed+=1000;start(s);}
    Peer& add(Server& s,uint64_t id,const std::string& nickname,std::array<double,3> at,bc::BuildId build=buildOf(1),uint32_t game=ds::GameBuild){
        auto p=std::make_unique<Peer>();auto* raw=p.get();p->id=id;p->server=&s;p->nickname=nickname;p->build=build;p->game=game;
        p->address={0x0b000000u+uint32_t(peers.size())+1,uint16_t(40000+peers.size())};p->skew=int64_t((peers.size()*7919)%6000)-3000;
        peerAt[p->address]=raw;peers.push_back(std::move(p));fresh(*raw);raw->client->setPosition(at);raw->pose=standing(at);return *raw;}
    void fresh(Peer& p){   // a game that started again: a new client, the same id and address
        bc::ClientConfig c;c.id=p.id;c.token="group";c.nickname=p.nickname;c.build=p.build;c.game=p.game;
        auto* raw=&p;p.client=std::make_unique<bc::Client>(c,[this,raw](const uint8_t* d,size_t n){queue.push_back({raw->address,raw->server->address,Buffer(d,d+n)});});
        p.client->setState(bc::StateKind::Name,presence::nameBlob(p.nickname));}   // the name, as the mod posts it (mod/crowd.h)
    Peer& addPairs(Server& s,uint64_t id,std::array<double,3> at){
        auto p=std::make_unique<Peer>();auto* raw=p.get();p->id=id;p->server=&s;p->address={0x0b000000u+uint32_t(peers.size())+1,uint16_t(40000+peers.size())};
        ds::relay::ClientConfig c;c.id=id;c.token="group";c.nickname="Pat";
        p->pairs=std::make_unique<ds::relay::Client>(c,[this,raw](const uint8_t* d,size_t n){queue.push_back({raw->address,raw->server->address,Buffer(d,d+n)});});
        p->pairs->setPosition(at);peerAt[p->address]=raw;peers.push_back(std::move(p));return *raw;}
    void deliver(Datagram d){
        if(auto s=serverAt.find(d.to);s!=serverAt.end()){
            if(serverAt.count(d.from)){if(drop&&drop(d)){++lost;return;}between.push_back(d);}
            if(s->second->relay)s->second->relay->receive(d.from,d.bytes.data(),d.bytes.size(),s->second->clock(now));
            return;}
        auto p=peerAt.find(d.to);if(p==peerAt.end())return;
        auto& peer=*p->second;if(peer.silent)return;peer.got.push_back(d.bytes);
        if(peer.client){peer.client->receive(d.bytes.data(),d.bytes.size(),peer.clock(now));peer.client->takeEvents();}
        else{uint64_t from=0;const uint8_t* payload=nullptr;size_t size=0;peer.pairs->receive(d.bytes.data(),d.bytes.size(),peer.clock(now),from,payload,size);peer.pairs->takeEvents();}}
    void pump(){while(!queue.empty()){auto d=std::move(queue.front());queue.pop_front();deliver(std::move(d));}}
    void raw(const RelayAddress& from,Server& to,const Buffer& b){to.relay->receive(from,b.data(),b.size(),to.clock(now));pump();}
    void step(uint64_t ms=10){
        now+=ms;
        for(auto& p:peers){
            if(p->silent)continue;
            uint64_t t=p->clock(now);
            if(!p->client){p->pairs->tick(t);continue;}
            auto s=p->pose(double(now)/1000.);p->client->setPosition(s.position,p->area);p->client->setLocal(s,true,false,t,p->jump);p->jump=false;
            if(p->animate){
                ds::Animation a;a.held[0]=1;a.floats[0]=float(now%1000)/1000.f;unsigned k=p->pulses%256;a.rose[k/8]=uint8_t(1u<<(k%8));
                if(p->client->offerAnimation(a,t)){p->pulsesSent.push_back({now,k});++p->pulses;}}
            p->client->tick(t);}
        pump();
        for(auto& s:servers)if(s->relay)s->relay->tick(s->clock(now));
        pump();
        for(auto& p:peers)if(p->client&&!p->silent)for(auto& near:p->client->nearby()){
            auto a=p->client->takeAnimation(near.first,p->clock(now));if(!a)continue;
            for(unsigned k=0;k<256;++k)if(a->animation.rose[k/8]&(1u<<(k%8)))p->pulsesGot[near.first].insert(k);}
    }
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=10)step();}
    template<class F> bool until(F done,uint64_t ms){for(uint64_t t=0;t<ms;t+=10){if(done())return true;step();}return done();}
    void clear(){for(auto& p:peers)p->got.clear();}
    // The update entries of one player in the FRAMEs between two servers (opened with the link's key).
    size_t carried(const Server& from,const std::string& key,uint64_t source) const {
        size_t n=0;auto k=presence::keyOf(key);
        for(auto& d:between){if(!(d.from==from.address))continue;
            presence::Header h;const uint8_t* body=nullptr;size_t size=0;
            if(!presence::open(d.bytes.data(),d.bytes.size(),k,h,body,size)||h.type!=presence::Type::Frame)continue;
            bc::Message m;if(bc::parse(body,size,m)!=ds::relay::Error::None)continue;
            for(auto& u:m.frame.entries)n+=u.player==source;}
        return n;}
};

// The envelope's parts: the tag, the window, the lists, the name.
static void wire(){
    uint8_t key[16];for(int i=0;i<16;++i)key[i]=uint8_t(i);uint8_t m[15];for(int i=0;i<15;++i)m[i]=uint8_t(i);
    uint64_t k0=ds::relay::detail::get(key,8),k1=ds::relay::detail::get(key+8,8);
    CHECK(presence::siphash(k0,k1,m,0)==0x726fdb47dd0e0e31ull&&presence::siphash(k0,k1,m,15)==0xa129ca6149be45e5ull);   // the reference vectors
    auto k=presence::keyOf("key-of-north-and-south"),other=presence::keyOf("key-of-north-and-southx");
    presence::Header h;h.type=presence::Type::Present;h.sender=5;h.receiver=6;h.sequence=7;uint8_t body[3]={1,2,3};
    auto b=presence::seal(h,body,3,k);CHECK(b.size()==43);
    presence::Header g;const uint8_t* p=nullptr;size_t n=0;
    CHECK(presence::open(b.data(),b.size(),k,g,p,n)&&g.sender==5&&g.receiver==6&&g.sequence==7&&n==3&&p[2]==3);
    CHECK(!presence::open(b.data(),b.size(),other,g,p,n));
    for(size_t i=0;i<b.size();++i){auto c=b;c[i]^=0x20;CHECK(!presence::open(c.data(),c.size(),k,g,p,n));}
    presence::Window w;CHECK(w.take(1)&&w.take(3)&&!w.take(3)&&w.take(2)&&!w.take(1)&&w.take(70)&&!w.take(6)&&w.take(7)&&!w.take(7));
    // A list of 300 players of the longest name: parts of at most 1160 bytes, read back whole.
    std::vector<presence::Present> list;for(uint64_t i=1;i<=300;++i){presence::Present x;x.id=i;x.area=226;x.generation=uint16_t(i);x.nickname=std::string(32,'a');x.build=buildOf(3);list.push_back(x);}
    auto parts=presence::encodePresent(9,list);CHECK(parts.size()==22);
    presence::Assembly<presence::Present> in;bool complete=false;
    for(size_t i=parts.size();i-->0;){presence::List l;std::vector<presence::Present> got;CHECK(parts[i].size()<=presence::BodyMax&&presence::parse(parts[i].data(),parts[i].size(),l,got));complete=in.take(l,got);}
    CHECK(complete&&in.items.size()==300);
    auto cells=presence::encodeCells(4,{});CHECK(cells.size()==1&&cells[0].size()==8);
    // The name state, as mod/nametag.cpp and mod/crowd.h make it: "Bea" is one event of type 10.
    auto blob=presence::nameBlob("Bea");
    CHECK(blob.size()==28&&blob[0]==1&&blob[1]==10&&ds::relay::detail::get(blob.data()+2,4)==(uint32_t('B')|uint32_t('e')<<8|uint32_t('a')<<16)&&ds::relay::detail::get(blob.data()+6,8)==16);
    for(std::string s:{std::string(),std::string("Bea"),std::string("A name of exactly 36 bytes, no more."),std::string("Zo\xc3\xab")}){blob=presence::nameBlob(s);CHECK(presence::nameOf(blob.data(),blob.size())==std::optional<std::string>(s));}
    CHECK(!presence::nameOf(blob.data(),blob.size()-1)&&!presence::nameOf(nullptr,0));
    CHECK(presence::shownName("Bea","north")=="Bea \xc2\xb7 north");
    auto longest=presence::shownName("Bartholomew the Long-Walking","a-very-long-link-name");
    CHECK(longest=="Bartholom \xc2\xb7 a-very-long-");   // 12 characters of the link's name, the name cut to fit 24
    CHECK(presence::shownName("\xc3\x89lodie\x01 of the \xc3\xa9toiles","s")=="\xc3\x89lodie of the \xc3\xa9toile \xc2\xb7 s");
    CHECK(presence::shownName(std::string(40,'x'),"south").size()<=32);
}

// Two servers on clocks 7 s apart: Bea (north) and Carl (south) see each other, walking and animating, with their
// names and their servers' names; Fay far away in the north is never sent; Dup in the south has Bea's id there.
static void across(){
    Net net;auto& north=net.server("north",0);auto& south=net.server("south",7000);net.link(north,south,"key-of-north-and-south");net.startAll();
    auto& bea=net.add(north,7,"Bea",{0,0,0});bea.pose=circle({0,0,0});bea.animate=true;
    auto& fay=net.add(north,8,"Fay",{5000,0,0});fay.pose=circle({5000,0,0});
    auto& carl=net.add(south,9,"Carl",{10,0,0});carl.pose=circle({10,0,0});carl.animate=true;
    auto& dup=net.add(south,7,"Dup",{-3000,0,0});auto& eli=net.add(south,10,"Eli",{-3000,0,5});
    net.run(3000);
    CHECK(north.relay->linkUp(0)&&south.relay->linkUp(0));
    uint64_t beaThere=south.relay->remoteId(0,7),carlThere=north.relay->remoteId(0,9);
    CHECK(beaThere&&beaThere!=7&&beaThere!=9&&beaThere!=10&&carlThere&&carlThere!=9&&carlThere!=7&&carlThere!=8);
    CHECK(carl.client->nearby().size()==1&&carl.sees(beaThere)&&carl.nickOf(beaThere)=="Bea \xc2\xb7 north");
    CHECK(bea.client->nearby().size()==1&&bea.sees(carlThere)&&bea.nickOf(carlThere)=="Carl \xc2\xb7 south");
    CHECK(nameShown(*carl.client,beaThere)=="Bea \xc2\xb7 north"&&nameShown(*bea.client,carlThere)=="Carl \xc2\xb7 south");
    CHECK(south.relay->players()==3&&south.relay->remotePlayers()==1&&north.relay->players()==2&&north.relay->remotePlayers()==1);
    // Fay is near nobody of the south: never sent, not one update of hers on the link.
    CHECK(!north.relay->exports(0,8)&&south.relay->remoteId(0,8)==0&&net.carried(north,"key-of-north-and-south",8)==0&&net.carried(north,"key-of-north-and-south",7)>=40);
    // Dup has id 7 in the south: he is himself, with his own name, and Eli sees him, not Bea.
    CHECK(dup.client->online()&&eli.sees(7)&&!eli.sees(beaThere)&&nameShown(*eli.client,7)=="Dup"&&eli.nickOf(7)=="Dup");
    // Every update with its animation (within 30 m), shown where Bea is, and every pulse she sent.
    net.clear();auto start=net.now;bea.pulsesSent.clear();carl.pulsesGot.clear();
    net.run(3000);
    CHECK(carl.entries(beaThere)>=55&&carl.entries(beaThere,bc::HasAnimation)>=55&&bea.entries(carlThere)>=55);
    auto r=carl.client->render(beaThere,carl.clock(net.now));CHECK(r.visible&&distance(r.state.position,{0,0,0})<2.5);
    auto q=bea.client->render(carlThere,bea.clock(net.now));CHECK(q.visible&&distance(q.state.position,{10,0,0})<2.5);
    size_t due=0,got=0;for(auto& [t,k]:bea.pulsesSent)if(t+1000<net.now&&t>start){++due;got+=carl.pulsesGot[beaThere].count(k);}
    CHECK(due>=30&&got==due);
    CHECK(carl.client->counters().refused==0&&carl.client->counters().unknown==0);
    std::printf("linked presence: across two servers %zu updates of Bea to Carl in 3 s, link north->south %llu bytes a second\n",carl.entries(beaThere),
                (unsigned long long)(north.relay->linkStats(0).sentBytes/9));
}

// A player is sent only while he is near one of the other server's players, and leaves when he is no longer.
static void interest(){
    Net net;auto& north=net.server("north",-4000);auto& south=net.server("south",0);net.link(north,south,"key-of-north-and-south");net.startAll();
    auto& bea=net.add(north,7,"Bea",{400,0,0});auto& carl=net.add(south,9,"Carl",{0,0,0});(void)carl;
    net.run(3000);
    CHECK(north.relay->linkUp(0)&&!north.relay->exports(0,7)&&south.relay->remoteId(0,7)==0&&net.carried(north,"key-of-north-and-south",7)==0);
    bea.pose=standing({50,0,0});bea.jump=true;
    CHECK(net.until([&]{return carl.client->nearby().size()==1;},1500));
    uint64_t there=south.relay->remoteId(0,7);CHECK(there&&carl.sees(there)&&north.relay->exports(0,7));
    // Another area: not near, not sent.
    bea.area=5;net.run(1500);
    CHECK(!north.relay->exports(0,7)&&carl.client->nearby().empty());
    bea.area=0;
    CHECK(net.until([&]{return carl.client->nearby().size()==1;},1500));
    bea.pose=standing({400,0,0});bea.jump=true;
    CHECK(net.until([&]{return carl.client->nearby().empty()&&!north.relay->exports(0,7)&&south.relay->remoteId(0,7)==0;},1500));
}

// The byte budget: 10 players near Carl and 20 farther, all walking and animating, on a link of 35 kB a second (they
// would take about 51).
static void budget(){
    Net net;auto& north=net.server("north",0);auto& south=net.server("south",2500);net.link(north,south,"key-of-north-and-south",35000);net.startAll();
    auto& carl=net.add(south,9,"Carl",{0,0,0});
    for(unsigned i=0;i<30;++i){
        double radius=i<10?12:60+3*(i-10),a=i*0.7;std::array<double,3> at{radius*std::cos(a),0,radius*std::sin(a)};
        auto& p=net.add(north,100+i,"Sam "+std::to_string(i),at);p.pose=circle(at);p.animate=true;}
    net.run(3000);
    CHECK(carl.client->nearby().size()==30&&north.relay->exported(0)==30);
    auto before=north.relay->linkStats(0);net.clear();net.run(4000);
    auto after=north.relay->linkStats(0);double perSecond=double(after.sentBytes-before.sentBytes)/4;
    size_t nearMin=1000,farMax=0,farMin=1000;
    for(unsigned i=0;i<30;++i){size_t n=carl.entries(south.relay->remoteId(0,100+i));if(i<10)nearMin=std::min(nearMin,n);else{farMax=std::max(farMax,n);farMin=std::min(farMin,n);}}
    std::printf("linked presence: a 35000-byte budget, 30 players: %.0f bytes a second, %llu updates left out and %llu without animation; at least %zu updates in 4 s of each near one, %zu..%zu of each far one\n",
                perSecond,(unsigned long long)(after.budgetDropped-before.budgetDropped),(unsigned long long)(after.stripped-before.stripped),nearMin,farMin,farMax);
    CHECK(perSecond<=35000*1.07&&after.budgetDropped>before.budgetDropped+400);
    CHECK(nearMin>=72&&farMin>=3);   // the near ones first (18 a second each), every far one now and then
    CHECK(carl.client->nearby().size()==30);
}

// A link that goes quiet loses its players; a player who leaves his server leaves the other; a server that starts again.
static void loss(){
    Net net;auto& north=net.server("north",0);auto& south=net.server("south",1234);net.link(north,south,"key-of-north-and-south");net.startAll();
    auto& bea=net.add(north,7,"Bea",{0,0,0});bea.pose=circle({0,0,0});auto& carl=net.add(south,9,"Carl",{10,0,0});
    net.run(3000);uint64_t there=south.relay->remoteId(0,7);CHECK(there&&carl.sees(there));
    net.drop=[](const Net::Datagram&){return true;};auto cut=net.now;
    CHECK(net.until([&]{return !carl.sees(there);},4000));
    CHECK(net.now-cut>=2500&&net.now-cut<=3600&&south.relay->remotePlayers()==0&&!south.relay->linkUp(0)&&north.relay->remotePlayers()==0);
    CHECK(carl.client->online());   // his own server never went away
    net.drop=nullptr;
    CHECK(net.until([&]{return carl.client->nearby().size()==1;},2500));
    there=south.relay->remoteId(0,7);
    // Bea leaves her server: she leaves Carl's within a second.
    bea.client->bye();bea.silent=true;net.pump();auto left=net.now;
    CHECK(net.until([&]{return carl.client->nearby().empty();},1000)&&net.now-left<=800&&south.relay->remoteId(0,7)==0);
    bea.silent=false;net.fresh(bea);
    CHECK(net.until([&]{return carl.client->nearby().size()==1;},2500));
    // The northern server starts again (a new session): the south drops what it held and takes her again.
    uint64_t sessions=south.relay->linkStats(0).sessions;
    net.restart(north);
    CHECK(net.until([&]{return south.relay->linkStats(0).sessions==sessions+1&&carl.client->nearby().size()==1;},4000));
    // A link of which only one side knows the other's address: the other learns it from the first HELLO.
    Net half;auto& a=half.server("north",0);auto& b=half.server("south",-500);half.link(a,b,"key-of-north-and-south",250000,64,false);half.startAll();
    half.add(a,7,"Bea",{0,0,0});auto& c=half.add(b,9,"Carl",{10,0,0});half.run(3000);
    CHECK(a.relay->linkUp(0)&&b.relay->linkUp(0)&&c.client->nearby().size()==1);
}

// Three servers linked in a triangle: each player sees the two others once, from their own servers; a line of three
// servers: the two ends do not see each other.
static void loops(){
    Net net;auto& a=net.server("ann",0);auto& b=net.server("bob",3000);auto& c=net.server("cy",-2000);
    net.link(a,b,"key-of-ann-and-bob");net.link(b,c,"key-of-bob-and-cy");net.link(c,a,"key-of-cy-and-ann");net.startAll();
    auto& x=net.add(a,1,"Xan",{0,0,0});auto& y=net.add(b,2,"Yu",{5,0,0});auto& z=net.add(c,3,"Zed",{0,0,5});
    for(auto* p:{&x,&y,&z})p->pose=circle(p->pose(0).position);
    net.run(4000);
    for(auto* s:{&a,&b,&c})CHECK(s->relay->remotePlayers()==2&&s->relay->players()==1);
    for(auto* p:{&x,&y,&z}){
        CHECK(p->client->nearby().size()==2);
        for(auto& [id,nick]:p->client->nearby()){
            CHECK(id!=p->id&&nick.find(p->nickname)!=0);   // never himself
            auto shown=nameShown(*p->client,id);CHECK(shown==nick);
            CHECK(shown=="Xan \xc2\xb7 ann"||shown=="Yu \xc2\xb7 bob"||shown=="Zed \xc2\xb7 cy");}}
    Net line;auto& l1=line.server("ann",0);auto& l2=line.server("bob",0);auto& l3=line.server("cy",0);
    line.link(l1,l2,"key-of-ann-and-bob");line.link(l2,l3,"key-of-bob-and-cy");line.startAll();
    auto& p1=line.add(l1,1,"Xan",{0,0,0});line.add(l2,2,"Yu",{5,0,0});auto& p3=line.add(l3,3,"Zed",{0,0,5});
    line.run(3000);
    CHECK(p1.client->nearby().size()==1&&p3.client->nearby().size()==1&&l1.relay->remotePlayers()==1&&l3.relay->remotePlayers()==1&&l2.relay->remotePlayers()==2);
}

// Ids, state and builds: a player here takes his id back; a linked player's state never lands on a player here; his
// build keeps him apart; a pairs player is not sent; state changes and a game that starts again cross the link, and a
// lost SHARE is asked for again.
static void identities(){
    Net net;auto& north=net.server("north",0);auto& south=net.server("south",-6000);net.link(north,south,"key-of-north-and-south");net.startAll();
    auto& bea=net.add(north,7,"Bea",{0,0,0});bea.pose=circle({0,0,0});
    auto& carl=net.add(south,9,"Carl",{10,0,0});auto& dup=net.add(south,7,"Dup",{-3000,0,0});auto& eli=net.add(south,10,"Eli",{-3000,0,5});
    net.add(north,11,"Hal",{5,0,5},buildOf(2));net.addPairs(north,12,{0,0,8});
    net.run(3000);
    uint64_t there=south.relay->remoteId(0,7);CHECK(there&&carl.sees(there)&&carl.client->nearby().size()==1);
    // Hal's build is not Carl's: not near, though he is sent (the north does not know who is of which build there).
    uint64_t halThere=south.relay->remoteId(0,11);CHECK(halThere&&!carl.sees(halThere)&&!south.relay->areNear(9,halThere));
    CHECK(!north.relay->exports(0,12)&&north.relay->exported(0)==2);
    // Bea's gear: every change reaches Carl, and never Dup, who has her id in the south.
    for(uint8_t i=1;i<=5;++i){CHECK(bea.client->setState(bc::StateKind::Gear,{1,2,i}));net.run(200);
        auto* g=carl.client->state(there,bc::StateKind::Gear);CHECK(g&&*g==std::vector<uint8_t>({1,2,i}));}
    CHECK(eli.client->state(7,bc::StateKind::Gear)==nullptr&&nameShown(*eli.client,7)=="Dup");
    // Half the link's datagrams lost while her gear changes: Carl ends with the last one.
    unsigned n=0;net.drop=[&n](const Net::Datagram&){return ++n%2==0;};
    for(uint8_t i=6;i<=9;++i){CHECK(bea.client->setState(bc::StateKind::Gear,{1,2,i}));net.run(150);}
    net.run(1500);net.drop=nullptr;net.run(1500);
    {auto* g=carl.client->state(there,bc::StateKind::Gear);CHECK(g&&*g==std::vector<uint8_t>({1,2,9}));}
    there=south.relay->remoteId(0,7);
    // Bea's game starts again: what she showed is gone in the south too.
    net.fresh(bea);
    CHECK(net.until([&]{return carl.client->state(there,bc::StateKind::Gear)==nullptr&&nameShown(*carl.client,there)=="Bea \xc2\xb7 north";},3000));
    // Gus joins the south with the id the south gave Bea: he gets it, and Bea comes back under another one.
    auto& gus=net.add(south,there,"Gus",{-3000,0,-5});
    CHECK(net.until([&]{return gus.client->online()&&south.relay->remoteId(0,7)&&south.relay->remoteId(0,7)!=there&&carl.client->nearby().size()==1;},2000));
    CHECK(gus.client->refusal()==bc::Refusal::None&&eli.sees(there)&&nameShown(*eli.client,there)=="Gus");
    {uint64_t again=south.relay->remoteId(0,7);CHECK(carl.sees(again)&&carl.nickOf(again)=="Bea \xc2\xb7 north");}
    (void)dup;
    // A southern server that takes one build only: Hal is not taken at all.
    Net pinned;auto& n2=pinned.server("north",0);auto& s2=pinned.server("south",0);s2.config.pinBuild=true;s2.config.game=ds::GameBuild;s2.config.build=buildOf(1);
    pinned.link(n2,s2,"key-of-north-and-south");pinned.startAll();
    pinned.add(n2,7,"Bea",{0,0,0});pinned.add(n2,11,"Hal",{5,0,5},buildOf(2));pinned.add(s2,9,"Carl",{10,0,0});pinned.run(3000);
    CHECK(s2.relay->remoteId(0,7)&&s2.relay->remoteId(0,11)==0&&s2.relay->linkStats(0).otherBuild>0);
}

// The tag: a datagram made without the link's key, or one taken before, changes nothing; a server with the wrong key
// is never linked; with the right key from another address than the one given, nothing is taken.
static void authentication(){
    const std::string key="key-of-north-and-south";
    Net net;auto& north=net.server("north",0);auto& south=net.server("south",900);net.link(north,south,key);
    auto& thief=net.server("thief",0);RelayLink wrong;wrong.name="south";wrong.key="not-the-right-key";wrong.peer=south.address;thief.config.links.push_back(wrong);
    net.startAll();
    auto& bea=net.add(north,7,"Bea",{0,0,0});auto& carl=net.add(south,9,"Carl",{10,0,0});
    net.run(3000);CHECK(carl.client->nearby().size()==1&&!thief.relay->linkUp(0));
    // The last list from the north that named Bea, as it went.
    Buffer listed;
    for(auto& d:net.between){if(!(d.from==north.address))continue;presence::Header h;const uint8_t* b=nullptr;size_t n=0;
        if(presence::open(d.bytes.data(),d.bytes.size(),presence::keyOf(key),h,b,n)&&h.type==presence::Type::Present&&n>8)listed=d.bytes;}
    CHECK(!listed.empty());
    bea.client->bye();bea.silent=true;net.pump();
    CHECK(net.until([&]{return carl.client->nearby().empty();},1500));
    // Sent again as it was: seen before. Sent with a new sequence and a tag of another key: not the link's.
    auto before=south.relay->linkStats(0);
    for(int i=0;i<3;++i){net.raw(north.address,south,listed);net.step();}
    presence::Header h;const uint8_t* body=nullptr;size_t n=0;presence::open(listed.data(),listed.size(),presence::keyOf(key),h,body,n);
    Buffer later(body,body+n);later[3]=0x40;   // a later list, which would be taken
    h.sequence+=100000;auto forged=presence::seal(h,later.data(),later.size(),presence::keyOf("a-key-the-thief-made"));
    for(int i=0;i<3;++i){net.raw(north.address,south,forged);net.step();}
    // The right key, but from another address than the one the south was given.
    h.sequence+=1000;auto moved=presence::seal(h,body,n,presence::keyOf(key));net.raw({0x0a6400ffu,9000},south,moved);
    net.run(500);
    auto after=south.relay->linkStats(0);
    CHECK(after.replayed>=before.replayed+3&&after.forged>=before.forged+3&&after.stale>=before.stale+1);
    CHECK(carl.client->nearby().empty()&&south.relay->remoteId(0,7)==0&&south.relay->remotePlayers()==0);
    CHECK(!thief.relay->linkUp(0)&&south.relay->linkUp(0));
}

int main(){
    // A broken protection may break the relay's own invariants: an exception is a failure like any other.
    for(auto test:{wire,across,interest,budget,loss,loops,identities,authentication}){
        try{test();}catch(const std::exception& e){std::fprintf(stderr,"linked presence oracle: exception %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("linked presence tests passed");return 0;
}
