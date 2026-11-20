// The relay's broadcast mode (relay.h, envelope version 2) with a clock of its own and a network in memory: real
// players' sides (core/broadcast_client.h Client, each on a clock of its own) and raw datagrams from players' addresses
// and from addresses that are not players. Every update of a player near, one in an interval beyond the full distance,
// animation only within its distance, a teleport always, the pulses of the samples left out carried by the next one;
// the shown state handed out on ENTER, on a change and on FETCH, and never older; FETCH for a player who is not near;
// a sender naming another player and a sender who never said HELLO; the order and the stamp of updates; pairs and
// broadcast players, and two builds, kept apart; FRAMEs within the budget and the flush time; 24 players in one place.
// How many one player sees: at most the cap, the nearest, kept while about as near (no flapping), friends first,
// only friends or nobody by choice (the server's record before the HELLO's), state handed out to whoever starts being
// shown, and nothing at all of the players near who are not shown; 100 players in one place with and without a cap.
// Bug marks: one line in the server's log, the mark to the players near who are shown the sender and to nobody else,
// one every 2 s and 30 an hour, nothing from an address that never said HELLO, in another's name, or refused by the keys.
#include "relay.h"
#include <tuple>
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
    Peer& add(uint64_t id,const std::string& nickname,std::array<double,3> at,bc::BuildId build=buildOf(1),uint32_t game=ds::GameBuild,bc::Show show=bc::Show::Everyone){
        auto& p=place(id);bc::ClientConfig c;c.id=id;c.token="group";c.nickname=nickname;c.build=build;c.game=game;c.show=show;
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

static std::set<uint64_t> ids(const std::map<uint64_t,std::string>& near){std::set<uint64_t> out;for(auto& e:near)out.insert(e.first);return out;}
static std::function<ds::State(double)> still(std::array<double,3> at){return [at](double){return standing(at);};}

// At most the cap, the nearest; nothing at all of the others near; a slot that is freed goes to the next nearest.
static void nearCap(){
    RelayConfig config;config.nearCap=4;Sim s(config);
    auto& r=s.add(1,"Ray",{0,0,0});
    // Ten walking, animating players 5 to 50 m away, the nearest with the highest ids (nearest is no id order).
    for(unsigned i=0;i<10;++i){std::array<double,3> at{5.+5*i,0,0};auto& p=s.add(200-i,"Sam"+std::to_string(i),at);p.pose=circle(at);p.animate=true;
        CHECK(p.client->setState(bc::StateKind::Name,{uint8_t('a'+i)}));}
    s.run(1500);
    const std::set<uint64_t> nearest{200,199,198,197};
    CHECK(ids(r.client->nearby())==nearest&&s.relay->shown(1)==4&&r.client->welcome().base.nearby==4);
    for(unsigned i=0;i<10;++i)CHECK(s.relay->areNear(1,200-i)&&s.relay->shows(1,200-i)==(i<4));
    // Everyone else near is shown his own four nearest, Ray among them only for those he is one of the four.
    CHECK(s.peers[1]->sees(1)&&!s.peers[10]->sees(1)&&s.peers[10]->client->nearby().size()==4);
    s.clear();auto unshown=s.relay->stats().unshown;s.run(2000);
    // Every update and the animation of the four, nothing of the six: no entry, no SHARE, no ROSTER naming them.
    for(unsigned i=0;i<10;++i){auto got=r.seen(200-i);if(i<4)CHECK(got.size()>=36);else CHECK(got.empty()&&r.count(bc::Type::Share,200-i)==0);}
    CHECK(s.relay->stats().unshown>=unshown+6*36&&r.client->counters().unknown==0);
    for(unsigned i=0;i<4;++i)CHECK(!r.pulsesGot[200-i].empty());
    // A state change of a player not shown is not sent to him; one of a player shown is.
    s.clear();CHECK(s.peers[10]->client->setState(bc::StateKind::Gear,{9})&&s.peers[1]->client->setState(bc::StateKind::Gear,{8}));s.run(100);
    CHECK(r.count(bc::Type::Share,191)==0&&r.count(bc::Type::Share,200)==1);
    // FETCH for a player near who is not shown: nothing.
    auto notNear=s.relay->stats().notNear;s.clear();s.raw(r.address,fetchRaw(1,191));CHECK(r.count(bc::Type::Share)==0&&s.relay->stats().notNear==notNear+1);
    // The nearest leaves: the fifth nearest takes the slot, with his name handed out at once (no FETCH).
    auto fetches=r.client->counters().fetches;
    s.peers[1]->client->bye();s.peers[1]->silent=true;s.run(200);
    CHECK(ids(r.client->nearby())==(std::set<uint64_t>{199,198,197,196})&&r.client->state(196,bc::StateKind::Name)&&*r.client->state(196,bc::StateKind::Name)==std::vector<uint8_t>({'e'}));
    CHECK(r.client->counters().fetches==fetches);
    // Without a cap he is shown all nine.
    RelayConfig open;Sim u(open);auto& o=u.add(1,"Ray",{0,0,0});for(unsigned i=0;i<9;++i)u.add(200-i,"Sam",{5.+5*i,0,0});u.run(1500);CHECK(o.client->nearby().size()==9);
    for(size_t bad:{size_t(1025)}){RelayConfig x;x.token="t";x.nearCap=bad;bool refused=false;try{Relay r2(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
    for(double bad:{-1.,1001.}){RelayConfig x;x.token="t";x.nearMargin=bad;bool refused=false;try{Relay r2(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
}

// Two players about as far away: the one shown stays while the other swings 2 m nearer and farther; one who comes
// clearly nearer (more than the margin) takes the place, and that is one LEAVE and one ENTER.
static void nearHysteresis(){
    RelayConfig config;config.nearCap=1;config.nearMargin=10;Sim s(config);
    auto& r=s.add(1,"Ray",{0,0,0});auto& x=s.add(2,"Xan",{30,0,0});x.pose=still({30,0,0});
    s.run(1000);CHECK(ids(r.client->nearby())==std::set<uint64_t>{2});
    auto& y=s.add(3,"Yul",{0,0,33});y.pose=[](double t){return standing({0,0,30+3*std::sin(t*2.)});};
    std::set<uint64_t> last=ids(r.client->nearby());unsigned changes=0;
    for(int i=0;i<600;++i){s.step();auto now=ids(r.client->nearby());if(now!=last){++changes;last=now;}}
    CHECK(changes==0&&last==std::set<uint64_t>{2});
    // Yul comes to 15 m: more than 10 m nearer than Xan, he is shown instead.
    y.pose=still({0,0,15});s.run(1500);CHECK(ids(r.client->nearby())==std::set<uint64_t>{3});
    // And Xan, back at 25 m (10 m farther, not more), does not take it back.
    x.pose=still({25,0,0});s.run(1500);CHECK(ids(r.client->nearby())==std::set<uint64_t>{3});
}

// Who a player sees, by his choice: the server's record of it first, else his HELLO's. Friends first within the cap,
// only friends, nobody. The choice is his own view: the others still see him.
static void showChoice(){
    std::map<uint64_t,RelayViewer> viewers;
    RelayConfig config;config.nearCap=2;config.viewer=[&](uint64_t id){auto it=viewers.find(id);return it==viewers.end()?RelayViewer{}:it->second;};
    Sim s(config);
    // Ray asks his game for friends only; his friends on the server are Fay (100 m) and Fil (120 m); three strangers stand
    // near; the server gives him a cap of 3 (so a third would fit).
    viewers[1].friends={7,8};viewers[1].cap=3;
    auto& r=s.add(1,"Ray",{0,0,0},buildOf(1),ds::GameBuild,bc::Show::Friends);
    auto walker=[&](uint64_t id,const std::string& name,std::array<double,3> at)->Sim::Peer&{auto& p=s.add(id,name,at);p.pose=circle(at);return p;};
    auto& fay=walker(7,"Fay",{100,0,0});walker(8,"Fil",{120,0,0});auto& a=walker(4,"Ann",{5,0,0});walker(5,"Bob",{6,0,0});walker(6,"Cy",{7,0,0});
    s.run(1500);CHECK(ids(r.client->nearby())==(std::set<uint64_t>{7,8}));
    s.clear();s.run(1000);for(uint64_t id:{4,5,6})CHECK(r.seen(id).empty());CHECK(r.seen(7).size()>=4);
    // The others still see him (his choice is what he sees, not who sees him).
    CHECK(a.sees(1)||s.relay->shows(4,1));CHECK(fay.sees(1));
    viewers[1].cap=-1;   // the relay's cap of 2 from here on
    // Everyone within the cap of 2: a friend first (Fay), then the nearest stranger (Ann).
    viewers[1].chosen=true;viewers[1].show=bc::Show::Everyone;viewers[1].friends={7};s.run(1500);CHECK(ids(r.client->nearby())==(std::set<uint64_t>{7,4}));
    // Nobody: his roster empties and nothing more comes, entries or state.
    viewers[1].show=bc::Show::Nobody;s.run(1000);CHECK(r.client->nearby().empty());
    s.clear();CHECK(a.client->setState(bc::StateKind::Gear,{1}));s.run(1000);
    CHECK(r.seen().empty()&&r.count(bc::Type::Share)==0&&r.count(bc::Type::Roster)==0);
    // The server's record of nothing chosen: his game's choice (friends) applies again.
    viewers[1].chosen=false;viewers[1].friends={7,8};s.run(1500);CHECK(ids(r.client->nearby())==(std::set<uint64_t>{7,8}));
    // A game that asks for nobody, with nothing on the server: nobody; the server's everyone over it: the two nearest.
    std::map<uint64_t,RelayViewer>().swap(viewers);
    Sim t(config);auto& q=t.add(1,"Quin",{0,0,0},buildOf(1),ds::GameBuild,bc::Show::Nobody);t.add(4,"Ann",{5,0,0});t.add(5,"Bob",{6,0,0});t.add(6,"Cy",{7,0,0});
    t.run(1500);CHECK(q.client->nearby().empty()&&t.relay->areNear(1,4));
    viewers[1].chosen=true;viewers[1].show=bc::Show::Everyone;t.run(1500);CHECK(ids(q.client->nearby())==(std::set<uint64_t>{4,5}));
}

// 100 players in one place, each walking and sending presence 20 times a second: what one of them receives with a cap
// of 16 and without one.
static void hundred(){
    auto measure=[](size_t cap,size_t& shown,double& datagrams,double& bytes){
        RelayConfig config;config.nearCap=cap;config.maxPlayers=128;Sim s(config);const unsigned N=100;
        for(unsigned i=0;i<N;++i){double a=i*2*3.14159265/N;std::array<double,3> at{(5+i%10)*std::cos(a),0,(5+i%10)*std::sin(a)};s.add(1000+i,"Sam",at).pose=circle(at);}
        s.run(2000);s.clear();std::vector<uint64_t> unknown;for(auto& p:s.peers)unknown.push_back(p->client->counters().unknown);
        s.run(2000);
        shown=0;double d=0,b=0;size_t k=0;
        size_t least=N,most=0;
        for(auto& p:s.peers){size_t n=p->client->nearby().size();least=std::min(least,n);most=std::max(most,n);for(auto& g:p->got){d+=1;b+=double(g.size());}
            if(p->client->counters().unknown!=unknown[k++])most=N;}   // an entry from a player who is not shown: not bounded
        shown=least==most?least:0;datagrams=d/N/2;bytes=b/N/2;};
    size_t shown16=0,shownAll=0;double d16=0,b16=0,dAll=0,bAll=0;
    measure(16,shown16,d16,b16);measure(0,shownAll,dAll,bAll);
    std::printf("broadcast relay: 100 players in one place, each receives %.0f datagrams and %.0f bytes a second with a cap of 16 (%zu shown), %.0f and %.0f without one (%zu shown)\n",
                d16,b16,shown16,dAll,bAll,shownAll);
    CHECK(shown16==16&&shownAll==99&&b16*4<bAll&&b16<16*20*(bc::EntryHeader+bc::PresenceBytes)*1.3);
}

static Buffer markRaw(uint64_t player,uint32_t mark){bc::Mark k;k.player=player;k.mark=mark;return bc::encodeMark(k);}
static void marks(){
    Sim s;std::vector<std::string> lines;
    s.relay->log=[&](const std::string& l){if(l.rfind("bugmark ",0)==0)lines.push_back(l);};s.relay->utc=[]{return uint64_t(1763000000123ull);};
    // Bob near and shown Ann; Cy near but shown nobody (his choice); Di far away.
    auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{10,0,0});auto& c=s.add(3,"Cy",{20,0,0},buildOf(1),ds::GameBuild,bc::Show::Nobody);auto& d=s.add(4,"Di",{1000,0,0});
    s.run(1000);CHECK(b.sees(1)&&s.relay->shows(2,1)&&s.relay->areNear(3,1)&&!s.relay->shows(3,1)&&!s.relay->areNear(4,1));
    s.clear();
    auto marked=[&](Sim::Peer& p){return p.count(bc::Type::Marked);};
    uint64_t first=s.now;
    CHECK(a.client->mark(0x00100001u,a.clock(s.now)));s.step(10);
    CHECK(lines.size()==1&&lines[0]=="bugmark 1048577 from 0000000000000001 (Ann) at 1763000000123");
    auto got=b.client->takeMarks();CHECK(got.size()==1&&got[0].source==1&&got[0].name=="Ann"&&got[0].mark==0x00100001u);
    CHECK(marked(b)==1&&marked(a)==0&&marked(c)==0&&marked(d)==0&&c.client->takeMarks().empty()&&d.client->takeMarks().empty());
    CHECK(s.relay->stats().marks==1&&s.relay->stats().marksSent==1);
    // The rate: a second mark within 2 s is dropped (the server's own check, the game's skipped); then one every 2 s
    // until 30 in the hour; the first an hour old, one more.
    s.now=first+1000;s.raw(a.address,markRaw(1,0x00100002u));
    CHECK(lines.size()==1&&marked(b)==1&&s.relay->stats().marksRefused==1);
    for(unsigned i=1;i<=34;++i){s.step(first+2000*i-s.now);s.raw(a.address,markRaw(1,0x00100010u+i));}
    CHECK(lines.size()==bc::MarksPerHour&&marked(b)==bc::MarksPerHour&&s.relay->stats().marksRefused==6);
    while(s.now+2000<first+bc::MarkHourMs)s.step(2000);
    s.step(first+bc::MarkHourMs+20-s.now);s.raw(a.address,markRaw(1,0x00100100u));   // the first was taken at first+10
    CHECK(lines.size()==bc::MarksPerHour+1&&lines.back().rfind("bugmark 1048832 from 0000000000000001 (Ann)",0)==0);
    CHECK(marked(c)==0&&marked(d)==0&&b.sees(1));
    // Not from an address that never said HELLO, and not in another player's name.
    auto unknown=s.relay->stats().unknownSource,spoofed=s.relay->stats().spoofed;size_t toBob=marked(b);
    s.step(2500);s.raw(Stranger,markRaw(1,77));s.raw(c.address,markRaw(1,78));s.raw(d.address,markRaw(2,79));
    CHECK(s.relay->stats().unknownSource==unknown+1&&s.relay->stats().spoofed==spoofed+2&&lines.size()==bc::MarksPerHour+1&&marked(b)==toBob&&s.stray.empty());
    // Di's own mark: logged under his id, and passed to nobody (nobody near).
    CHECK(d.client->mark(5,d.clock(s.now)));s.step(10);
    CHECK(lines.size()==bc::MarksPerHour+2&&lines.back()=="bugmark 5 from 0000000000000004 (Di) at 1763000000123"&&marked(a)==0&&marked(b)==toBob&&marked(c)==0);
    // Player keys: a HELLO the server's keys refuse makes no player; his marks go nowhere.
    RelayConfig keyed;keyed.admit=[](uint64_t id,const std::string&){return id!=9;};
    Sim k(keyed);std::vector<std::string> keyedLines;k.relay->log=[&](const std::string& l){if(l.rfind("bugmark ",0)==0)keyedLines.push_back(l);};
    auto& ann=k.add(1,"Ann",{0,0,0});auto& eve=k.add(9,"Eve",{5,0,0});k.run(1000);
    CHECK(!k.relay->known(9)&&!eve.client->online()&&!eve.client->mark(3,eve.clock(k.now)));
    k.raw(eve.address,markRaw(9,4));k.raw(eve.address,markRaw(1,4));k.step(10);
    CHECK(keyedLines.empty()&&ann.count(bc::Type::Marked)==0&&ann.client->takeMarks().empty());
}

// Where each player is, for the server's collections (RelayConfig::spotted): a broadcast player with his facing from
// his presence, a player of the other mode without one; about once a second each.
static void spots(){
    RelayConfig config;std::map<uint64_t,std::vector<std::tuple<std::array<double,3>,bool,double,uint64_t>>> got;Sim* sim=nullptr;
    config.spotted=[&](uint64_t id,uint32_t,const std::array<double,3>& at,bool haveYaw,double yaw,const std::string&){got[id].emplace_back(at,haveYaw,yaw,sim->now);};
    Sim s(config);sim=&s;auto& a=s.add(1,"Ann",{50,0,-20});a.pose=[](double){ds::State x;x.position={50,3,-20};x.yaw=-2.25;x.action=3;return x;};
    s.addPairs(2,"Bob",{60,0,-20});
    // A broadcast player publishes only while another is near him: alone, his place is known and his facing is not.
    s.run(2000);
    if(!got[1].empty())CHECK(!std::get<1>(got[1].back()));
    s.add(3,"Cy",{55,0,-20});s.run(3000);
    CHECK(got[1].size()>=4&&got[1].size()<=6&&got[2].size()>=4&&got[2].size()<=6);
    if(!got[1].empty()){auto& [at,haveYaw,yaw,when]=got[1].back();(void)when;CHECK(haveYaw&&std::abs(yaw+2.25)<0.001&&distance(at,{50,3,-20})<0.02);}
    if(!got[2].empty()){auto& [at,haveYaw,yaw,when]=got[2].back();(void)yaw;(void)when;CHECK(!haveYaw&&distance(at,{60,0,-20})<0.02);}
    for(auto& [id,list]:got)for(size_t i=1;i<list.size();++i)CHECK(std::get<3>(list[i])>=std::get<3>(list[i-1])+1000);
}
static Buffer signalRaw(uint64_t player,bc::SignalKind kind,uint32_t serial,uint64_t target,std::array<double,3> place){
    bc::Signal k;k.player=player;k.kind=kind;k.serial=serial;k.target=target;k.place=place;return bc::encodeSignal(k);}
// Pings and likes: a ping to the players near who are shown the sender, only near where the server has him; a like to
// the one player it names, while he is near, shown the sender and within reach; one every 500 ms and 20 a minute;
// nothing from an address that never said HELLO or in another's name.
static void signalsRelay(){
    Sim s;
    // Bob near and shown Ann; Cy near but shown nobody; Di far away; Ed near and shown, but 80 m from Ann.
    auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{10,0,0});auto& c=s.add(3,"Cy",{20,0,0},buildOf(1),ds::GameBuild,bc::Show::Nobody);
    auto& d=s.add(4,"Di",{1000,0,0});auto& e=s.add(5,"Ed",{0,0,80});
    s.run(1000);CHECK(b.sees(1)&&e.sees(1)&&s.relay->shows(2,1)&&s.relay->shows(5,1)&&s.relay->areNear(3,1)&&!s.relay->shows(3,1)&&!s.relay->areNear(4,1));
    s.clear();
    auto got=[&](Sim::Peer& p){return p.count(bc::Type::Signalled);};
    const uint64_t first=s.now;
    // A ping: to Bob and Ed, with Ann as its source and her place; not to Cy (not shown her), Di (far) or Ann herself.
    CHECK(a.client->signal(bc::SignalKind::Ping,1,0,{3,1,4},a.clock(s.now)));s.step(10);
    CHECK(got(b)==1&&got(e)==1&&got(a)==0&&got(c)==0&&got(d)==0&&s.relay->stats().signals==1&&s.relay->stats().signalsSent==2);
    {auto n=b.client->takeSignals();CHECK(n.size()==1&&n[0].source==1&&n[0].name=="Ann"&&n[0].kind==bc::SignalKind::Ping&&n[0].serial==1&&n[0].place[0]==3&&n[0].place[2]==4);}
    CHECK(e.client->takeSignals().size()==1);
    // A ping 400 m from where the server has her is refused.
    s.step(600);s.raw(a.address,signalRaw(1,bc::SignalKind::Ping,2,0,{400,0,0}));
    CHECK(got(b)==1&&s.relay->stats().signalsRefused==1);
    // A like for Bob: to Bob only, with the server's place of Ann.
    s.step(600);s.raw(a.address,signalRaw(1,bc::SignalKind::Like,3,2,{9,9,9}));
    CHECK(got(b)==2&&got(e)==1&&got(c)==0);
    {auto n=b.client->takeSignals();CHECK(n.size()==1&&n[0].kind==bc::SignalKind::Like&&n[0].serial==3&&n[0].place[0]==0&&n[0].place[2]==0);}
    // Not for Ed (80 m away), Cy (not shown her), Di (far), an unknown player, or a pairs player.
    auto refused=s.relay->stats().signalsRefused;
    for(uint64_t target:{5ull,3ull,4ull,77ull}){s.step(600);s.raw(a.address,signalRaw(1,bc::SignalKind::Like,uint32_t(10+target),target,{0,0,0}));}
    CHECK(s.relay->stats().signalsRefused==refused+4&&got(e)==1&&got(c)==0&&got(d)==0&&got(b)==2);
    // The rate: within 500 ms of the last one taken, refused; then one every 500 ms until 20 in the minute.
    s.step(600);CHECK(a.client->signal(bc::SignalKind::Ping,30,0,{0,0,0},a.clock(s.now)));s.step(10);
    s.now+=100;s.raw(a.address,signalRaw(1,bc::SignalKind::Ping,31,0,{0,0,0}));
    CHECK(got(b)==3&&s.relay->stats().signals==3);
    for(unsigned i=0;i<30;++i){s.step(500);s.raw(a.address,signalRaw(1,bc::SignalKind::Ping,40+i,0,{0,0,0}));}
    CHECK(s.relay->stats().signals==bc::SignalsPerMinute&&got(b)==bc::SignalsPerMinute);
    while(s.now<first+bc::SignalMinuteMs+20000)s.step(1000);
    s.raw(a.address,signalRaw(1,bc::SignalKind::Ping,90,0,{0,0,0}));CHECK(s.relay->stats().signals==bc::SignalsPerMinute+1);
    // Not from an address that never said HELLO, and not in another player's name.
    auto unknown=s.relay->stats().unknownSource,spoofed=s.relay->stats().spoofed;size_t toBob=got(b);
    s.step(600);s.raw(Stranger,signalRaw(1,bc::SignalKind::Ping,91,0,{0,0,0}));s.raw(c.address,signalRaw(1,bc::SignalKind::Ping,92,0,{0,0,0}));
    s.raw(d.address,signalRaw(2,bc::SignalKind::Like,93,1,{0,0,0}));
    CHECK(s.relay->stats().unknownSource==unknown+1&&s.relay->stats().spoofed==spoofed+2&&got(b)==toBob&&got(a)==0);
}

int main(){
    marks();signalsRelay();spots();
    interest();teleport();shownState();senders();orderAndStamp();modesAndBuilds();batching();crowd();
    nearCap();nearHysteresis();showChoice();hundred();
    if(failures)return 1;
    std::puts("broadcast relay tests passed");return 0;
}
