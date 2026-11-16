// The relay's broadcast mode (relay.h, envelope version 2) with a clock of its own and a network in memory: real
// players' sides (core/broadcast_client.h Client, each on a clock of its own) and raw datagrams from players' addresses
// and from addresses that are not players. Every update of a player near, one in an interval beyond the full distance,
// animation only within its distance, a teleport always, the pulses of the samples left out carried by the next one;
// the shown state handed out on ENTER, on a change and on FETCH, and never older; FETCH for a player who is not near;
// a sender naming another player and a sender who never said HELLO; the order and the stamp of updates; pairs and
// broadcast players, and two builds, kept apart; FRAMEs within the budget and the flush time; 24 players in one place.
#include "relay.h"
#include "../core/broadcast_client.h"
#include <cmath>
#include <cstdio>
#include <memory>
using namespace world;
namespace bc=ds::broadcast;
using ds::relay::Buffer;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"broadcast relay oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static bc::BuildId buildOf(uint8_t seed){bc::BuildId b{};for(size_t i=0;i<b.size();++i)b[i]=uint8_t(seed+i);return b;}
static bc::Presence blockOf(const ds::State& s){ds::Packet q;q.kind=ds::Kind::Presence;q.state=s;auto b=ds::encode(q);return bc::presenceOf(b.data());}
static ds::State standing(std::array<double,3> at){ds::State s;s.position=at;s.action=3;return s;}
// A porter walking a 2 m circle around `centre` at 1.5 m/s.
static std::function<ds::State(double)> circle(std::array<double,3> centre){
    return [centre](double t){double a=t*0.75;ds::State s;s.position={centre[0]+2*std::cos(a),centre[1],centre[2]+2*std::sin(a)};
        s.velocity={-1.5*std::sin(a),0,1.5*std::cos(a)};s.yaw=0.5;s.action=3;s.phase=0.25;return s;};}
static double distance(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);}
static const RelayAddress Stranger{0xc0a80063u,5555};

// One FRAME entry as it arrived, copied out of the datagram.
struct Seen {uint64_t source=0,stamp=0,serverTime=0;uint32_t sequence=0;uint8_t parts=0;uint16_t generation=0,flags=0;size_t frameSize=0,frameEntries=0;};
struct Sim {
    struct Peer {
        uint64_t id=0;RelayAddress address;int64_t skew=0;bool silent=false;
        std::unique_ptr<bc::Client> client;std::unique_ptr<ds::relay::Client> pairs;   // a broadcast player, or one of version 1
        std::function<ds::State(double)> pose;bool jump=false;   // set: the porter moves, and his side publishes
        bool animate=false;unsigned pulses=0;std::vector<std::pair<uint64_t,unsigned>> pulsesSent;   // (time, pulse) offered and taken
        std::map<uint64_t,std::set<unsigned>> pulsesGot;   // per source: the pulses shown
        std::vector<Buffer> got;   // every datagram the relay sent this player
        uint64_t clock(uint64_t now) const {return uint64_t(int64_t(now)+skew);}
        bool sees(uint64_t other) const {return client?client->nearby().count(other)!=0:pairs->nearby().count(other)!=0;}
        std::vector<Seen> seen(uint64_t source=0) const {
            std::vector<Seen> out;
            for(auto& b:got){bc::Message m;if(bc::parse(b.data(),b.size(),m)!=ds::relay::Error::None||m.type!=bc::Type::Frame)continue;
                for(auto& u:m.frame.entries){if(source&&u.player!=source)continue;
                    Seen s;s.source=u.player;s.sequence=u.sequence;s.stamp=u.stamp;s.serverTime=m.frame.serverTime;s.parts=u.parts;s.generation=u.generation;
                    s.flags=u.presence?bc::presenceFlags(u.presence):0;s.frameSize=b.size();s.frameEntries=m.frame.entries.size();out.push_back(s);}}
            return out;}
        size_t count(bc::Type t,uint64_t source=0) const {
            size_t n=0;for(auto& b:got){bc::Message m;if(bc::parse(b.data(),b.size(),m)==ds::relay::Error::None&&m.type==t&&(!source||t!=bc::Type::Share||m.share.source==source))++n;}return n;}
    };
    RelayConfig config;std::unique_ptr<Relay> relay;std::vector<std::unique_ptr<Peer>> peers;std::map<RelayAddress,Peer*> byAddress;
    std::deque<std::pair<RelayAddress,Buffer>> up,down;std::map<RelayAddress,std::vector<Buffer>> stray;
    uint64_t now=100000,relaySent=0;
    explicit Sim(RelayConfig c={}):config(std::move(c)){
        if(config.token.empty())config.token="group";
        relay=std::make_unique<Relay>(config,[this](const RelayAddress& a,const uint8_t* d,size_t n){++relaySent;down.push_back({a,Buffer(d,d+n)});});}
    Peer& place(uint64_t id){
        auto p=std::make_unique<Peer>();auto* raw=p.get();p->id=id;
        p->address={0x0a000000u+uint32_t(peers.size())+1,uint16_t(40000+peers.size())};
        p->skew=int64_t((peers.size()*7919)%6000)-3000;   // every player's clock is its own
        byAddress[p->address]=raw;peers.push_back(std::move(p));return *raw;}
    bc::Client::Send sender(Peer* p){return [this,p](const uint8_t* d,size_t n){up.push_back({p->address,Buffer(d,d+n)});};}
    Peer& add(uint64_t id,const std::string& nickname,std::array<double,3> at,bc::BuildId build=buildOf(1),uint32_t game=ds::GameBuild){
        auto& p=place(id);bc::ClientConfig c;c.id=id;c.token="group";c.nickname=nickname;c.build=build;c.game=game;
        p.client=std::make_unique<bc::Client>(c,sender(&p));p.client->setPosition(at);return p;}
    Peer& addPairs(uint64_t id,const std::string& nickname,std::array<double,3> at){
        auto& p=place(id);ds::relay::ClientConfig c;c.id=id;c.token="group";c.nickname=nickname;
        p.pairs=std::make_unique<ds::relay::Client>(c,sender(&p));p.pairs->setPosition(at);return p;}
    void pump(){
        while(!up.empty()||!down.empty()){
            if(!up.empty()){auto d=std::move(up.front());up.pop_front();relay->receive(d.first,d.second.data(),d.second.size(),now);continue;}
            auto d=std::move(down.front());down.pop_front();
            auto it=byAddress.find(d.first);if(it==byAddress.end()){stray[d.first].push_back(d.second);continue;}
            auto& p=*it->second;p.got.push_back(d.second);
            if(p.client)p.client->receive(d.second.data(),d.second.size(),p.clock(now));
            else{uint64_t from=0;const uint8_t* payload=nullptr;size_t size=0;p.pairs->receive(d.second.data(),d.second.size(),p.clock(now),from,payload,size);}
            if(p.client)p.client->takeEvents();else p.pairs->takeEvents();}
    }
    void raw(const RelayAddress& from,const Buffer& b){relay->receive(from,b.data(),b.size(),now);pump();}
    void step(uint64_t ms=10){
        now+=ms;
        for(auto& p:peers){
            if(p->silent)continue;
            uint64_t t=p->clock(now);
            if(!p->client){p->pairs->tick(t);continue;}
            if(p->pose){auto s=p->pose(double(now)/1000.);p->client->setPosition(s.position);p->client->setLocal(s,true,false,t,p->jump);p->jump=false;}
            if(p->animate){
                // A pulse of its own in every sample offered, so that each one can be traced to the players who see it.
                ds::Animation a;a.held[0]=1;a.floats[0]=float(now%1000)/1000.f;unsigned k=p->pulses%256;a.rose[k/8]=uint8_t(1u<<(k%8));
                if(p->client->offerAnimation(a,t)){p->pulsesSent.push_back({now,k});++p->pulses;}}
            p->client->tick(t);}
        pump();relay->tick(now);pump();
        for(auto& p:peers)if(p->client&&!p->silent)for(auto& near:p->client->nearby()){
            auto a=p->client->takeAnimation(near.first,p->clock(now));if(!a)continue;
            for(unsigned k=0;k<256;++k)if(a->animation.rose[k/8]&(1u<<(k%8)))p->pulsesGot[near.first].insert(k);}
    }
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=10)step();}
    void clear(){for(auto& p:peers)p->got.clear();}
};
static Buffer publishRaw(uint64_t player,uint32_t sequence,uint64_t stamp,const ds::State& s){
    auto block=blockOf(s);bc::Update u;u.player=player;u.sequence=sequence;u.stamp=stamp;u.parts=bc::HasPresence;u.presence=block.data();return bc::encodePublish(u);}
static Buffer stateRaw(uint64_t player,bc::StateKind kind,uint32_t version,std::vector<uint8_t> bytes){
    bc::StatePost s;s.player=player;s.kind=kind;s.version=version;s.payload=bytes.data();s.size=bytes.size();return bc::encode(s);}
static Buffer fetchRaw(uint64_t player,uint64_t source){bc::Fetch f;f.player=player;f.source=source;return bc::encode(f);}
static bool hasSequence(const std::vector<Seen>& v,uint32_t sequence){for(auto& s:v)if(s.sequence==sequence)return true;return false;}

// Who gets what of a walking, animating player: B within the full distance, E beyond it but within the animation
// distance, C beyond both and still near, D far away.
static void interest(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{10,0,0});auto& e=s.add(5,"Eve",{40,0,0});auto& c=s.add(3,"Cy",{100,0,0});auto& d=s.add(4,"Di",{1000,0,0});
    a.pose=circle({0,0,0});a.animate=true;
    s.run(1000);CHECK(a.sees(2)&&a.sees(3)&&a.sees(5)&&!a.sees(4)&&b.sees(1)&&c.sees(1)&&e.sees(1)&&!d.sees(1));
    CHECK(b.client->welcome().fullMetres==30&&b.client->welcome().animationMetres==50&&b.client->welcome().intervalMs==200&&b.client->welcome().budget==1200);
    s.clear();auto start=s.now;a.pulsesSent.clear();auto publishedBefore=s.relay->stats().published,thinnedBefore=s.relay->stats().thinned;for(auto* p:{&b,&c,&e})p->pulsesGot.clear();
    s.run(4000);
    auto toB=b.seen(1),toE=e.seen(1),toC=c.seen(1),toD=d.seen();
    auto published=s.relay->stats().published-publishedBefore,thinned=s.relay->stats().thinned-thinnedBefore;
    // B: every update (20 a second); E and C: one in 200 ms; D: nothing at all.
    CHECK(toB.size()>=75&&toB.size()<=82);
    for(auto* v:{&toE,&toC}){
        CHECK(v->size()>=17&&v->size()<=21);
        for(size_t i=1;i<v->size();++i)CHECK((*v)[i].stamp>=(*v)[i-1].stamp+190);}
    CHECK(toD.empty()&&d.got.size()<=d.count(bc::Type::Welcome)+d.count(bc::Type::Roster));
    CHECK(published>=75&&thinned*10>=published*14);   // E and C: three in four left out
    // Animation within 50 m only: B and E take samples, C takes presence alone.
    size_t bAnimated=0,eAnimated=0,cAnimated=0;
    for(auto& x:toB)bAnimated+=(x.parts&bc::HasAnimation)!=0;
    for(auto& x:toE)eAnimated+=(x.parts&bc::HasAnimation)!=0;
    for(auto& x:toC)cAnimated+=(x.parts&bc::HasAnimation)!=0;
    CHECK(bAnimated>=70&&eAnimated>=15&&cAnimated==0);
    CHECK(b.client->counters().animationsTaken>=70&&c.client->counters().animationsTaken==0&&c.client->counters().accepted>=17);
    // Every pulse A sent (bar the last second's, still behind the shown time) is shown by B and by E, although E was sent
    // one sample in four: the relay carried the others' pulses with the next one. C shows none.
    size_t due=0,atB=0,atE=0;
    for(auto& [t,k]:a.pulsesSent)if(t+1000<s.now&&t>start){++due;atB+=b.pulsesGot[1].count(k);atE+=e.pulsesGot[1].count(k);}
    CHECK(due>=50&&atB==due&&atE==due);CHECK(c.pulsesGot[1].empty());
    // All three show A where he is (at their own delay behind).
    for(auto* p:{&b,&e,&c}){auto r=p->client->render(1,p->clock(s.now));CHECK(r.visible&&distance(r.state.position,{0,0,0})<2.5);}
}

// A teleport goes to every player near at once, even beyond the full distance within the interval.
static void teleport(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& c=s.add(3,"Cy",{100,0,0});
    a.pose=circle({0,0,0});s.run(1500);CHECK(c.sees(1));
    // Right after C was sent an update, A jumps 90 m: within C's interval.
    s.clear();do s.step();while(c.seen(1).empty());
    auto jumpAt=s.now;a.pose=circle({0,0,90});a.jump=true;
    s.run(100);
    auto toC=c.seen(1);bool flagged=false;for(auto& x:toC)if(x.flags&ds::Teleport){flagged=true;CHECK(x.stamp<=jumpAt+60&&x.stamp+60>=jumpAt);}
    CHECK(flagged);
    // C's side takes it (a teleport is no impossible motion) and shows A at the new place within half a second.
    s.run(400);auto r=c.client->render(1,c.clock(s.now));CHECK(r.visible&&distance(r.state.position,{0,0,90})<2.5&&c.client->counters().rebased==0);
}

// The state a player shows: handed out on ENTER (before anyone asks), on a change, again on FETCH; never older.
static void shownState(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{500,0,0});auto& c=s.add(3,"Cy",{1000,0,0});
    s.run(1000);
    CHECK(a.client->setState(bc::StateKind::Name,{'A','n','n'})&&a.client->setState(bc::StateKind::Gear,{1,2,3}));
    s.run(1000);CHECK(s.relay->stats().states==2&&!b.sees(1)&&b.count(bc::Type::Share)==0);
    // B comes near: A's name and gear arrive with the ENTER, before B could see a gap and ask.
    b.client->setPosition({20,0,0});
    while(!b.sees(1))s.step();
    CHECK(b.client->state(1,bc::StateKind::Name)&&*b.client->state(1,bc::StateKind::Name)==std::vector<uint8_t>({'A','n','n'}));
    CHECK(b.client->state(1,bc::StateKind::Gear)&&b.client->state(1,bc::StateKind::Carry)==nullptr);
    CHECK(b.client->counters().fetches==0&&s.relay->stats().fetches==0);
    // A walks while his gear changes: every change reaches B at once, and the updates already queued for B with the
    // older generation are no reason to ask again.
    a.pose=circle({0,0,0});s.run(500);
    for(uint8_t i=0;i<6;++i){
        CHECK(a.client->setState(bc::StateKind::Gear,{1,2,3,i}));s.step();
        CHECK(*b.client->state(1,bc::StateKind::Gear)==std::vector<uint8_t>({1,2,3,i}));s.run(i%2?30:70);}
    s.run(1000);CHECK(b.client->counters().fetches==0&&s.relay->stats().fetches==0&&b.client->generation(1)==8);
    // An older version from A's own address: refused, B keeps the newer bytes.
    auto stale=s.relay->stats().staleStates;
    s.raw(a.address,stateRaw(1,bc::StateKind::Gear,2,{9}));CHECK(s.relay->stats().staleStates==stale+1&&*b.client->state(1,bc::StateKind::Gear)==std::vector<uint8_t>({1,2,3,5}));
    s.raw(a.address,stateRaw(1,bc::StateKind::Gear,a.client->versions()[1],{9}));CHECK(s.relay->stats().staleStates==stale+2&&*b.client->state(1,bc::StateKind::Gear)==std::vector<uint8_t>({1,2,3,5}));
    // FETCH for a player near: every kind (the empty ones too), at most once in 250 ms.
    s.clear();auto fetches=s.relay->stats().fetches;
    s.raw(b.address,fetchRaw(2,1));CHECK(b.count(bc::Type::Share,1)==4&&s.relay->stats().fetches==fetches+1);
    s.raw(b.address,fetchRaw(2,1));CHECK(b.count(bc::Type::Share,1)==4);
    s.step(250);s.raw(b.address,fetchRaw(2,1));CHECK(b.count(bc::Type::Share,1)==8);
    // FETCH for a player who is not near: nothing.
    s.clear();auto notNear=s.relay->stats().notNear,sent=s.relaySent;
    s.raw(c.address,fetchRaw(3,1));s.raw(c.address,fetchRaw(3,99));s.raw(b.address,fetchRaw(2,3));
    CHECK(c.count(bc::Type::Share)==0&&b.count(bc::Type::Share)==0&&s.relaySent==sent&&s.relay->stats().notNear==notNear+3);
    // B's game starts again on the same address: it knows nobody, and is handed A's state again with the new ENTER.
    {bc::ClientConfig config;config.id=2;config.token="group";config.nickname="Bob";config.build=buildOf(1);
     b.client=std::make_unique<bc::Client>(config,s.sender(&b));b.client->setPosition({20,0,0});}
    while(!b.sees(1))s.step();
    CHECK(b.client->state(1,bc::StateKind::Name)&&*b.client->state(1,bc::StateKind::Gear)==std::vector<uint8_t>({1,2,3,5})&&b.client->counters().fetches==0);
    // A's game starts again: what it showed is gone; B learns that from the next update's generation.
    {bc::ClientConfig config;config.id=1;config.token="group";config.nickname="Ann";config.build=buildOf(1);
     a.client=std::make_unique<bc::Client>(config,s.sender(&a));a.client->setPosition({0,0,0});}
    s.run(2000);CHECK(b.sees(1)&&b.client->state(1,bc::StateKind::Name)==nullptr&&b.client->state(1,bc::StateKind::Gear)==nullptr&&b.client->counters().fetches>=1);
}

// A PUBLISH, STATE or FETCH is the player's whose address it came from, never the one it names; from an address that
// never said HELLO it is nothing.
static void senders(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{10,0,0});auto& c=s.add(3,"Cy",{1000,0,0});
    s.run(1000);CHECK(a.client->setState(bc::StateKind::Name,{'A'}));s.run(100);CHECK(b.sees(1)&&!c.sees(1));
    s.clear();auto spoofed=s.relay->stats().spoofed;
    // C names A: an update, a state and a FETCH in B's name.
    s.raw(c.address,publishRaw(1,0x10000000u,s.now,standing({0,0,0})));s.step(30);
    s.raw(c.address,stateRaw(1,bc::StateKind::Name,1000,{'X'}));
    s.raw(c.address,fetchRaw(2,1));
    CHECK(s.relay->stats().spoofed==spoofed+3);
    CHECK(!hasSequence(b.seen(1),0x10000000u)&&b.count(bc::Type::Share)==0&&*b.client->state(1,bc::StateKind::Name)==std::vector<uint8_t>({'A'}));
    // An address that never said HELLO: counted, and nothing goes out.
    auto unknown=s.relay->stats().unknownSource,sent=s.relaySent;
    s.raw(Stranger,publishRaw(1,0x20000000u,s.now,standing({0,0,0})));s.raw(Stranger,stateRaw(1,bc::StateKind::Name,2000,{'Y'}));s.raw(Stranger,fetchRaw(1,2));
    s.step(30);
    CHECK(s.relay->stats().unknownSource==unknown+3&&s.stray.empty()&&!hasSequence(b.seen(1),0x20000000u)&&*b.client->state(1,bc::StateKind::Name)==std::vector<uint8_t>({'A'}));
    (void)sent;
    // Messages meant for players, sent to the server: counted as malformed, never answered.
    auto malformed=s.relay->stats().malformed;sent=s.relaySent;
    {bc::Welcome w;w.base.id=1;s.raw(a.address,bc::encode(w));Buffer f;bc::beginFrame(f);auto block=blockOf(standing({0,0,0}));bc::Update u;u.player=1;u.parts=bc::HasPresence;u.presence=block.data();
     bc::appendEntry(f,u);bc::finishFrame(f,1);s.raw(a.address,f);s.raw(a.address,Buffer(40,0));}
    CHECK(s.relay->stats().malformed==malformed+3&&s.relaySent==sent);
}

// The order and the stamp of a player's updates: repeats, older ones and stamps outside the window go nowhere.
static void orderAndStamp(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{10,0,0});
    s.run(1000);CHECK(b.sees(1));s.clear();
    uint64_t last=0;   // the stamp of the last update taken
    auto send=[&](uint32_t sequence,uint64_t stamp){s.raw(a.address,publishRaw(1,sequence,stamp,standing({0,0,0})));s.step(30);};
    auto taken=[&](uint32_t sequence,int64_t stamp){last=uint64_t(int64_t(s.now)+stamp);send(sequence,last);};
    taken(1,0);s.run(3000);
    send(2,s.now-2100);taken(3,-1900);send(4,s.now+600);taken(5,400);   // the window: 2 s behind and 0.5 s ahead of the server's clock
    send(5,last+1);send(4,last+2);send(6,last);send(0,last+3);           // a repeat, an older one, one whose stamp is no later, sequence 0
    send(0x80000006u,last+4);                                            // half the sequence space away: earlier, not later
    std::vector<uint32_t> got;for(auto& x:b.seen(1))got.push_back(x.sequence);
    CHECK(got==std::vector<uint32_t>({1,3,5}));
    CHECK(s.relay->stats().badStamps==2&&s.relay->stats().staleUpdates==5);
    // The sequence wraps: each step less than half the space is later, past 0xffffffff too.
    s.run(600);taken(0x70000000u,0);taken(0xe0000000u,0);taken(0xffffffffu,0);taken(0,0);taken(1,0);
    got.clear();for(auto& x:b.seen(1))got.push_back(x.sequence);CHECK(got==std::vector<uint32_t>({1,3,5,0x70000000u,0xe0000000u,0xffffffffu,0,1}));
}

// Pairs players and broadcast players, and broadcast players of two builds, in one place: never near each other.
static void modesAndBuilds(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{5,0,0},buildOf(2));auto& p=s.addPairs(9,"Pat",{0,5,0});auto& c=s.add(3,"Cy",{0,0,5});
    auto& g=s.add(4,"Gus",{5,5,0},buildOf(1),ds::GameBuild+1);auto& q=s.addPairs(8,"Quin",{0,0,-5});
    s.run(1500);
    CHECK(a.client->online()&&b.client->online()&&p.pairs->online()&&c.client->online()&&g.client->online());
    CHECK(a.sees(3)&&c.sees(1)&&!a.sees(2)&&!a.sees(4)&&!a.sees(9)&&!b.sees(1)&&!b.sees(9)&&!g.sees(1)&&!p.sees(1)&&!p.sees(2)&&p.sees(8)&&q.sees(9));
    CHECK(!s.relay->areNear(1,2)&&!s.relay->areNear(1,9)&&!s.relay->areNear(2,9)&&!s.relay->areNear(1,4)&&s.relay->areNear(1,3));
    // WELCOME counts the players in reach whom another build or mode keeps apart.
    CHECK(a.client->welcome().otherBuild==4&&a.client->welcome().base.nearby==1&&b.client->welcome().otherBuild==5);
    // Nothing crosses: a broadcast player's update reaches C only; a pairs player's DATA does not reach a broadcast
    // player; each mode's messages from the other mode's player are counted and dropped.
    s.clear();a.pose=circle({0,0,0});s.run(500);
    CHECK(!c.seen(1).empty()&&b.seen().empty()&&g.seen().empty());
    for(auto& d:p.got)CHECK(bc::versionOf(d.data(),d.size())==1);
    auto wrong=s.relay->stats().wrongMode;
    {uint8_t core[64]{};Buffer d;ds::relay::encodeData(d,ds::relay::Type::Data,1,core,sizeof core);s.raw(p.address,d);ds::relay::encodeData(d,ds::relay::Type::Data,9,core,sizeof core);s.raw(a.address,d);}
    s.raw(p.address,publishRaw(9,1,s.now,standing({0,5,0})));s.raw(p.address,stateRaw(9,bc::StateKind::Name,1,{'P'}));
    CHECK(s.relay->stats().wrongMode==wrong+3&&s.relay->stats().notNear>=1);
    // A server that pins the build refuses the others with their own reason.
    RelayConfig pinned;pinned.pinBuild=true;pinned.game=ds::GameBuild;pinned.build=buildOf(1);Sim t(pinned);
    auto& x=t.add(1,"Ann",{0,0,0});auto& y=t.add(2,"Bob",{5,0,0},buildOf(2));auto& z=t.add(3,"Cy",{0,5,0},buildOf(1),ds::GameBuild+1);t.run(1500);
    CHECK(x.client->online()&&!y.client->online()&&y.client->refusal()==bc::Refusal::Build&&!z.client->online()&&z.client->refusal()==bc::Refusal::Build);
    CHECK(t.relay->players()==1&&t.relay->known(1)&&!t.relay->known(2));
}

// FRAMEs: within the budget, each entry sent within the flush time, several entries to a datagram.
static void batching(){
    RelayConfig config;config.budget=400;Sim s(config);
    auto& r=s.add(1,"Ray",{0,0,0});
    for(unsigned i=0;i<12;++i){std::array<double,3> at{15*std::cos(i*0.5),0,15*std::sin(i*0.5)};s.add(100+i,"Sam"+std::to_string(i),at).pose=circle(at);}
    s.run(1500);CHECK(r.client->nearby().size()==12);
    s.clear();s.run(2000);
    auto all=r.seen();size_t frames=r.count(bc::Type::Frame),largest=0;
    for(auto& x:all){largest=std::max(largest,x.frameSize);CHECK(x.serverTime+5>=x.stamp&&x.serverTime<=x.stamp+config.flushMs+10);}
    CHECK(largest<=400&&largest>=300);
    std::printf("broadcast relay: 12 players near one, a 400-byte budget: %zu entries in %zu frames, largest %zu bytes\n",all.size(),frames,largest);
    CHECK(frames>0&&all.size()>=12*36&&double(all.size())/double(frames)>=3.5);
    for(unsigned i=0;i<12;++i)CHECK(r.seen(100+i).size()>=36);
    CHECK(s.relay->stats().frameBytes/std::max<uint64_t>(1,s.relay->stats().frames)<=400);
    // Out of range: refused when the relay is made.
    for(size_t budget:{size_t(255),size_t(1201)}){RelayConfig x;x.token="t";x.budget=budget;bool refused=false;try{Relay bad(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
}

// 24 players in one place, all walking and animating: every pair sees each other, every FRAME fits a datagram.
static void crowd(){
    Sim s;const unsigned N=24;
    for(unsigned i=0;i<N;++i){double a=i*2*3.14159265/N;std::array<double,3> at{12*std::cos(a),0,12*std::sin(a)};auto& p=s.add(1000+i,"Sam"+std::to_string(i),at);p.pose=circle(at);p.animate=true;}
    s.run(2000);s.clear();auto bytes=s.relay->stats().frameBytes;s.run(2000);
    unsigned wrong=0;size_t largest=0,frames=0,entries=0;
    for(unsigned i=0;i<N;++i){
        auto& p=*s.peers[i];if(p.client->nearby().size()!=N-1)++wrong;
        for(auto& x:p.seen()){largest=std::max(largest,x.frameSize);++entries;if(x.source==p.id||!p.sees(x.source))++wrong;}
        frames+=p.count(bc::Type::Frame);
        for(unsigned j=0;j<N;++j)if(j!=i&&(!p.client->render(1000+j,p.clock(s.now)).visible||p.seen(1000+j).size()<36))++wrong;}
    CHECK(wrong==0&&largest<=bc::MaxDatagram&&frames>0&&double(entries)/double(frames)>=4);
    std::printf("broadcast relay: %u players in one place, 2 s: %zu entries in %zu frames (%.1f a frame, largest %zu bytes), %.0f bytes a second to each player\n",
                N,entries,frames,double(entries)/double(frames),largest,double(s.relay->stats().frameBytes-bytes)/double(N)/2.);
}

int main(){
    interest();teleport();shownState();senders();orderAndStamp();modesAndBuilds();batching();crowd();
    if(failures)return 1;
    std::puts("broadcast relay tests passed");return 0;
}
