// The relay (relay.h) with a clock of its own and a network in memory: real players' sides (core/relay.h Client) and
// raw datagrams from addresses that are not players. Who is near whom with hysteresis, nothing passed on to players who
// are far, sources that never said HELLO, silence, the rate cap, the player cap, a lossy link and a hundred players.
#include "relay.h"
#include <cstdio>
#include <cstring>
#include <memory>
using namespace world;
using namespace ds::relay;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"relay oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
struct Sim {
    struct Peer {
        RelayAddress address;std::unique_ptr<Client> client;bool silent=false;
        std::vector<RosterEvent> events;std::vector<std::pair<uint64_t,Buffer>> got;std::vector<Buffer> sent;
        bool sees(uint64_t id) const {return client->nearby().count(id)!=0;}
    };
    RelayConfig config;std::unique_ptr<Relay> relay;std::vector<std::unique_ptr<Peer>> peers;std::map<RelayAddress,Peer*> byAddress;
    std::deque<std::pair<RelayAddress,Buffer>> up,down;std::map<RelayAddress,std::vector<Buffer>> stray;std::vector<std::string> lines;
    uint64_t now=1000,rng=88172645463325252ull,relaySent=0;unsigned lossPerMille=0;
    explicit Sim(RelayConfig c={}):config(std::move(c)){if(config.token.empty())config.token="group";start();}
    void start(){
        relay=std::make_unique<Relay>(config,[this](const RelayAddress& a,const uint8_t* d,size_t n){++relaySent;down.push_back({a,Buffer(d,d+n)});});
        relay->log=[this](const std::string& s){lines.push_back(s);};}
    Peer& add(uint64_t id,const std::string& nickname,std::array<double,3> position,uint32_t area=0,const std::string& token="group"){
        auto p=std::make_unique<Peer>();auto* raw=p.get();p->address={0x0a000000u+uint32_t(peers.size())+1,uint16_t(40000+peers.size())};
        ClientConfig c;c.id=id;c.token=token;c.nickname=nickname;
        p->client=std::make_unique<Client>(c,[this,raw](const uint8_t* d,size_t n){raw->sent.emplace_back(d,d+n);up.push_back({raw->address,Buffer(d,d+n)});});
        p->client->setPosition(position,area);byAddress[p->address]=raw;peers.push_back(std::move(p));return *raw;}
    bool lost(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng%1000<lossPerMille;}
    void pump(){
        while(!up.empty()||!down.empty()){
            if(!up.empty()){auto d=std::move(up.front());up.pop_front();if(!lost())relay->receive(d.first,d.second.data(),d.second.size(),now);continue;}
            auto d=std::move(down.front());down.pop_front();
            auto it=byAddress.find(d.first);if(it==byAddress.end()){stray[d.first].push_back(d.second);continue;}
            if(lost())continue;
            auto& p=*it->second;uint64_t from=0;const uint8_t* payload=nullptr;size_t size=0;
            if(p.client->receive(d.second.data(),d.second.size(),now,from,payload,size))p.got.push_back({from,Buffer(payload,payload+size)});
            for(auto& e:p.client->takeEvents())p.events.push_back(e);}
    }
    void raw(const RelayAddress& from,const Buffer& b){relay->receive(from,b.data(),b.size(),now);pump();}
    void step(uint64_t ms=100){
        now+=ms;
        for(auto& p:peers)if(!p->silent){p->client->tick(now);for(auto& e:p->client->takeEvents())p->events.push_back(e);}
        pump();relay->tick(now);pump();}
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=100)step();}
    void move(Peer& p,std::array<double,3> position,uint32_t area=0){p.client->setPosition(position,area);run(1000);}
};
static Buffer dataTo(uint64_t peer,uint8_t fill=0x42,size_t size=128){std::vector<uint8_t> p(size,fill);Buffer b;encodeData(b,Type::Data,peer,p.data(),p.size());return b;}
static Buffer helloFrom(uint64_t id,const std::string& token,std::array<double,3> position,uint32_t sequence=1){
    Hello h;h.id=id;h.token=token;h.nickname="raw";h.sequence=sequence;h.havePosition=true;h.position=position;return encode(h);}
static const RelayAddress Stranger{0xc0a80063u,5555};

static void nearAndFar(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{300,0,0});
    s.run(1000);CHECK(a.client->online()&&b.client->online()&&s.relay->players()==2);
    CHECK(a.client->welcome().enterMetres==150&&a.client->welcome().leaveMetres==200&&a.client->welcome().timeoutSeconds==10);
    CHECK(!a.sees(2)&&!b.sees(1)&&a.events.empty());
    // In at 150 m, out past 200 m: between the two nothing changes either way.
    s.move(b,{160,0,0});CHECK(!a.sees(2)&&!s.relay->areNear(1,2));
    s.move(b,{150.5,0,0});CHECK(!a.sees(2));
    s.move(b,{150,0,0});CHECK(a.sees(2)&&b.sees(1)&&s.relay->areNear(1,2)&&s.relay->areNear(2,1));
    CHECK(a.events.size()==1&&a.events[0].enter&&a.events[0].id==2&&a.events[0].nickname=="Bob"&&b.events.size()==1&&b.events[0].nickname=="Ann");
    CHECK(a.client->welcome().nearby==1);
    s.move(b,{175,0,0});s.move(b,{199,0,0});s.move(b,{200,0,0});CHECK(a.sees(2)&&b.sees(1)&&a.events.size()==1);
    s.move(b,{200.5,0,0});CHECK(!a.sees(2)&&!b.sees(1)&&!s.relay->areNear(1,2)&&a.events.size()==2&&!a.events[1].enter&&a.events[1].id==2&&b.events.size()==2);
    s.move(b,{199,0,0});s.move(b,{151,0,0});CHECK(!a.sees(2)&&a.events.size()==2);
    // Distance is in three dimensions: 120 m along and 100 m up is 156 m.
    s.move(b,{120,100,0});CHECK(!a.sees(2));s.move(b,{120,80,0});CHECK(a.sees(2));
    // The other one moving away counts the same.
    s.move(a,{-90,0,0});CHECK(!a.sees(2)&&!b.sees(1));
    // Another area is another map: never near, however close. No position: near nobody.
    s.move(a,{120,80,0},7);CHECK(!a.sees(2));s.move(b,{120,80,0},7);CHECK(a.sees(2)&&b.sees(1));
    s.move(b,{120,80,0},8);CHECK(!a.sees(2)&&!b.sees(1));s.move(a,{120,80,0},8);CHECK(a.sees(2));
    b.client->clearPosition();s.run(1000);CHECK(!a.sees(2)&&!b.sees(1)&&s.relay->known(2));
    b.client->setPosition({120,80,0},8);s.run(1000);CHECK(a.sees(2)&&b.sees(1));
}
static void forwarding(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{50,0,0});auto& c=s.add(3,"Cy",{1000,0,0});
    s.run(1000);CHECK(a.sees(2)&&b.sees(1)&&!a.sees(3)&&!c.sees(1));
    // Near: the datagram arrives whole, marked with who sent it.
    uint8_t core[128];for(unsigned i=0;i<128;++i)core[i]=uint8_t(i);
    CHECK(a.client->sendTo(2,core,128));s.pump();
    CHECK(b.got.size()==1&&b.got[0].first==1&&b.got[0].second==Buffer(core,core+128)&&a.got.empty()&&c.got.empty());
    CHECK(b.client->sendTo(1,core,64));s.pump();CHECK(a.got.size()==1&&a.got[0].first==2&&a.got[0].second.size()==64);
    {std::vector<uint8_t> big(MaxPayload,7);CHECK(a.client->sendTo(2,big.data(),big.size()));s.pump();CHECK(b.got.size()==2&&b.got[1].second==big);}
    // Far: the player's side does not send, and the relay does not pass on what is sent anyway.
    CHECK(!a.client->sendTo(3,core,128));
    auto forwarded=s.relay->stats().forwarded;auto sent=s.relaySent;
    s.raw(a.address,dataTo(3));s.raw(c.address,dataTo(1));s.raw(a.address,dataTo(1));s.raw(a.address,dataTo(99));
    CHECK(c.got.empty()&&a.got.size()==1&&s.relay->stats().forwarded==forwarded&&s.relaySent==sent&&s.relay->stats().notNear>=4);
    // Once they part, nothing more goes through, in either direction.
    s.move(b,{500,0,0});CHECK(!a.sees(2));
    s.raw(a.address,dataTo(2));s.raw(b.address,dataTo(1));CHECK(b.got.size()==2&&a.got.size()==1&&s.relay->stats().forwarded==forwarded);
    // A source that never said HELLO: dropped, whatever it names; nothing is answered.
    s.move(b,{50,0,0});CHECK(a.sees(2));
    sent=s.relaySent;auto unknown=s.relay->stats().unknownSource;
    s.raw(Stranger,dataTo(2));s.raw(Stranger,dataTo(1));
    CHECK(b.got.size()==2&&a.got.size()==1&&s.relaySent==sent&&s.relay->stats().unknownSource==unknown+2&&s.stray.empty());
    // HELLO without the group's token: not a player, no answer, and its data still goes nowhere.
    s.raw(Stranger,helloFrom(9,"wrong",{0,0,0}));s.raw(Stranger,helloFrom(9,"",{0,0,0}));s.raw(Stranger,helloFrom(9,"groupX",{0,0,0}));s.raw(Stranger,helloFrom(9,"grou",{0,0,0}));
    CHECK(!s.relay->known(9)&&s.relay->players()==3&&s.relaySent==sent&&s.stray.empty()&&s.relay->stats().badToken==4);
    s.raw(Stranger,dataTo(2));CHECK(b.got.size()==2);
    // HELLO with the token for a player who is here, from another address: refused, and the player keeps his address.
    s.raw(Stranger,helloFrom(1,"group",{50,0,0},5000));
    CHECK(s.stray[Stranger].size()==1);
    {Message m;auto& r=s.stray[Stranger][0];CHECK(parse(r.data(),r.size(),m)==Error::None&&m.type==Type::Refused&&m.refused.reason==Refusal::IdInUse&&m.refused.id==1&&m.refused.sequence==5000);}
    CHECK(b.client->sendTo(1,core,32));s.pump();CHECK(a.got.size()==2&&s.stray[Stranger].size()==1);
    s.raw(Stranger,dataTo(2));CHECK(b.got.size()==2);
    // Garbage and messages meant for players: counted, never answered.
    sent=s.relaySent;auto malformed=s.relay->stats().malformed;
    s.raw(a.address,Buffer(MaxDatagram+1,0x44));s.raw(a.address,Buffer(5,0));s.raw(Stranger,Buffer(700,0xff));
    {Welcome w;w.id=2;w.stream=1;s.raw(a.address,encode(w));auto relayed=dataTo(2);relayed[6]=uint8_t(Type::Relayed);s.raw(a.address,relayed);}
    CHECK(s.relaySent==sent&&s.relay->stats().malformed==malformed+5&&b.got.size()==2);
}
static void silenceAndBye(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{50,0,0});auto& c=s.add(3,"Cy",{0,50,0});
    s.run(1000);CHECK(a.sees(2)&&a.sees(3)&&b.sees(3)&&s.relay->players()==3);
    // Ten seconds without HELLO: gone, and the others are told. Data alone does not keep a player.
    b.silent=true;auto start=s.now;
    while(s.now-start<9000){s.step();s.raw(b.address,dataTo(1));}
    CHECK(s.relay->known(2)&&a.sees(2)&&!a.got.empty());
    s.run(2000);CHECK(!s.relay->known(2)&&s.relay->players()==2&&!a.sees(2)&&!c.sees(2)&&a.sees(3)&&s.relay->stats().timedOut==1);
    CHECK(!a.events.empty()&&!a.events.back().enter&&a.events.back().id==2);
    auto got=a.got.size();s.raw(b.address,dataTo(1));CHECK(a.got.size()==got);   // his address is forgotten with him
    // He comes back: a new start, everyone near again; his own side noticed the silence and forgot them in between.
    b.silent=false;s.run(1000);CHECK(s.relay->known(2)&&a.sees(2)&&b.sees(1)&&b.sees(3)&&c.sees(2));
    // BYE: at once.
    c.client->bye();c.silent=true;s.pump();CHECK(!s.relay->known(3)&&!a.sees(3)&&!b.sees(3)&&c.client->nearby().empty());
    // A BYE from someone else's address, or with the wrong token, removes nobody.
    {Bye y;y.id=1;y.token="group";s.raw(Stranger,encode(y));s.raw(b.address,encode(y));y.token="x";s.raw(a.address,encode(y));}
    CHECK(s.relay->known(1)&&a.sees(2));
    // The same address under a new id is the same sender again: the old player goes.
    s.raw(b.address,helloFrom(77,"group",{0,0,0}));CHECK(!s.relay->known(2)&&s.relay->known(77)&&s.relay->areNear(1,77)&&!a.sees(2));
    CHECK(s.lines.size()>=6);
}
static void rateCap(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{50,0,0});
    s.run(3000);CHECK(a.sees(2));
    // A burst far over the cap (200 a second for one player near): about a second's worth gets through, no more.
    auto before=s.relay->stats().forwarded;
    for(unsigned i=0;i<2000;++i)s.raw(a.address,dataTo(2));
    auto passed=s.relay->stats().forwarded-before;CHECK(passed>=150&&passed<=200&&b.got.size()==passed&&s.relay->stats().rateDropped>=1800);
    // It does not cost the other player anything, and after a second the sender may send again.
    CHECK(b.client->sendTo(1,reinterpret_cast<const uint8_t*>("x"),1));s.pump();CHECK(a.got.size()==1);
    s.run(1000);CHECK(a.sees(2)&&s.relay->known(1));
    before=s.relay->stats().forwarded;for(unsigned i=0;i<50;++i)s.raw(a.address,dataTo(2));CHECK(s.relay->stats().forwarded-before==50);
    // A steady 100 a second for ten seconds is under the cap: all of it arrives.
    before=s.relay->stats().forwarded;auto dropped=s.relay->stats().rateDropped;
    for(unsigned t=0;t<100;++t){s.step();for(unsigned i=0;i<10;++i)s.raw(a.address,dataTo(2));}
    CHECK(s.relay->stats().forwarded-before==1000&&s.relay->stats().rateDropped==dropped);
}
static void playerCap(){
    RelayConfig c;c.maxPlayers=3;Sim s(c);
    auto& a=s.add(1,"A",{0,0,0});s.add(2,"B",{1,0,0});s.add(3,"C",{2,0,0});s.run(1000);
    auto& d=s.add(4,"D",{3,0,0});s.run(1000);
    CHECK(s.relay->players()==3&&!s.relay->known(4)&&!d.client->online()&&d.client->refusal()==Refusal::Full&&!a.sees(4)&&d.client->nearby().empty());
    a.client->bye();a.silent=true;s.run(1000);CHECK(s.relay->known(4)&&d.client->online()&&d.client->refusal()==Refusal::None&&d.sees(2)&&d.sees(3));
    for(auto bad:{0.,0.5,300.}){RelayConfig x;x.token="t";x.enterMetres=bad;bool refused=false;try{Relay r(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
    {RelayConfig x;x.token="not a token";bool refused=false;try{Relay r(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
    {RelayConfig x;x.maxPlayers=RelayPlayersMax+1;bool refused=false;try{Relay r(x,[](const RelayAddress&,const uint8_t*,size_t){});}catch(const std::invalid_argument&){refused=true;}CHECK(refused);}
}
static void lateHello(){
    // A HELLO that arrives after a later one says where the player was: it must not bring him back.
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{50,0,0});
    s.run(1000);CHECK(a.sees(2));auto old=b.sent.back();
    {Message m;CHECK(parse(old.data(),old.size(),m)==Error::None&&m.type==Type::Hello&&m.hello.stream!=0);}
    s.move(b,{900,0,0});CHECK(!a.sees(2));auto events=a.events.size();
    s.raw(b.address,old);s.run(300);CHECK(!a.sees(2)&&!s.relay->areNear(1,2)&&a.events.size()==events);
}
static void restarts(){
    Sim s;auto& a=s.add(1,"Ann",{0,0,0});auto& b=s.add(2,"Bob",{50,0,0});auto& c=s.add(3,"Cy",{0,50,0});
    s.run(1000);CHECK(a.sees(2)&&a.sees(3));
    // The server starts again and knows nobody: every player is told who is near once more, and data flows again.
    s.config.seed=5000;s.start();s.run(1500);
    CHECK(s.relay->players()==3&&a.sees(2)&&a.sees(3)&&b.sees(1)&&b.sees(3)&&c.sees(1)&&c.sees(2));
    CHECK(a.client->sendTo(2,reinterpret_cast<const uint8_t*>("x"),1));s.pump();CHECK(b.got.size()==1&&b.got[0].first==1);
    // A player's game starts again on the same address (a fixed port): it asks for everything, the others see no change.
    auto events=a.events.size();
    ClientConfig config;config.id=2;config.token="group";config.nickname="Bob";auto* raw=&b;
    b.client=std::make_unique<Client>(config,[&s,raw](const uint8_t* d,size_t n){s.up.push_back({raw->address,Buffer(d,d+n)});});
    b.client->setPosition({50,0,0});s.run(1500);
    CHECK(b.sees(1)&&b.sees(3)&&b.client->nearby().size()==2&&a.events.size()==events&&a.sees(2));
}
static void hundred(){
    RelayConfig config;config.maxPlayers=128;Sim s(config);const unsigned N=100;
    for(unsigned i=0;i<N;++i)s.add(1000+i,"Sam "+std::to_string(i),{40.*i,0,0});
    auto verify=[&](const char* phase,double spacing){
        unsigned wrong=0,pairs=0;
        for(unsigned i=0;i<N;++i)for(unsigned j=0;j<N;++j){
            if(i==j)continue;
            bool expect=spacing*(i>j?i-j:j-i)<=150;pairs+=expect;
            if(s.peers[i]->sees(1000+j)!=expect||s.relay->areNear(1000+i,1000+j)!=expect)++wrong;}
        if(wrong)std::fprintf(stderr,"relay oracle: %s: %u of the roster entries are wrong\n",phase,wrong),++failures;
        // Every player sends one datagram to each player near him; each arrives once, from the right player, and nothing else does.
        for(auto& p:s.peers)p->got.clear();
        for(unsigned i=0;i<N;++i)for(auto& other:s.peers[i]->client->nearby()){uint64_t id=1000+i;uint8_t payload[8];std::memcpy(payload,&id,8);if(!s.peers[i]->client->sendTo(other.first,payload,8))++wrong;}
        s.pump();unsigned delivered=0;
        for(unsigned i=0;i<N;++i){
            std::set<uint64_t> from;
            for(auto& g:s.peers[i]->got){uint64_t id=0;if(g.second.size()==8)std::memcpy(&id,g.second.data(),8);if(id!=g.first||!s.peers[i]->sees(id)||!from.insert(id).second)++wrong;++delivered;}
            if(from.size()!=s.peers[i]->client->nearby().size())++wrong;}
        if(wrong||delivered!=pairs)std::fprintf(stderr,"relay oracle: %s: %u wrong deliveries, %u of %u delivered\n",phase,wrong,delivered,pairs),++failures;
    };
    // A line of players 40 m apart, on a link that loses three datagrams in ten each way: the roster still comes out right.
    s.lossPerMille=300;s.run(30000);s.lossPerMille=0;s.run(3000);
    CHECK(s.relay->players()==N);verify("line",40);
    // Far players get nothing even when it is sent.
    {auto forwarded=s.relay->stats().forwarded;s.raw(s.peers[0]->address,dataTo(1050));s.raw(s.peers[99]->address,dataTo(1000));CHECK(s.relay->stats().forwarded==forwarded);}
    // Everyone walks to one place (99 near each), then far apart (nobody near), still on the lossy link.
    for(auto& p:s.peers)p->client->setPosition({5,0,5});
    s.lossPerMille=300;s.run(30000);s.lossPerMille=0;s.run(3000);verify("crowd",0);
    CHECK(s.peers[17]->client->nearby().size()==N-1&&s.peers[17]->client->nearby().at(1042)=="Sam 42"&&s.peers[17]->client->welcome().nearby==N-1);
    for(unsigned i=0;i<N;++i)s.peers[i]->client->setPosition({1000.*i-50000,0,0});
    s.lossPerMille=300;s.run(30000);s.lossPerMille=0;s.run(3000);verify("apart",1000);
    CHECK(s.relay->players()==N&&s.relay->stats().timedOut==0);
    // Every ENTER had its LEAVE, in that order, for every player.
    unsigned unbalanced=0;
    for(auto& p:s.peers){std::set<uint64_t> in;for(auto& e:p->events){if(e.enter?!in.insert(e.id).second:!in.erase(e.id))++unbalanced;}unbalanced+=unsigned(in.size());}
    CHECK(unbalanced==0);
}
// Where each player is, for the server's collections (RelayConfig::spotted): only a player who says where he is.
static void spots(){
    RelayConfig config;std::map<uint64_t,std::array<double,3>> got;std::map<uint64_t,bool> yaw;
    config.spotted=[&](uint64_t id,uint32_t,const std::array<double,3>& at,bool haveYaw,double,const std::string&){got[id]=at;yaw[id]=haveYaw;};
    Sim s(config);auto& a=s.add(1,"Ann",{10,2,30});auto& b=s.add(2,"Bob",{0,0,0});b.client->clearPosition();
    (void)a;s.run(2500);
    CHECK(got.count(1)&&got[1]==(std::array<double,3>{10,2,30})&&!yaw[1]);
    CHECK(!got.count(2));
}
static void onlineNames(){
    RelayConfig config;std::vector<std::vector<std::string>> told;config.online=[&](std::vector<std::string> n){told.push_back(std::move(n));};
    Sim s(config);s.step();CHECK(told.size()==1&&told[0].empty());         // said once at the start, even with nobody on
    auto& b=s.add(2,"Bob",{0,0,0});s.add(1,"Ann",{9000,0,0});s.run(1500);    // near each other or not, everyone counts
    CHECK(told.back()==(std::vector<std::string>{"Ann","Bob"}));const size_t n=told.size();
    s.run(2000);CHECK(told.size()==n);                                       // nothing new, nothing said
    b.silent=true;s.run(s.config.timeoutMs+1000);CHECK(told.back()==(std::vector<std::string>{"Ann"}));
}
int main(){
    onlineNames();spots();nearAndFar();forwarding();silenceAndBye();rateCap();playerCap();lateHello();restarts();hundred();
    if(failures)return 1;
    std::puts("relay tests passed");return 0;
}
