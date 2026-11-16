// The relay envelope and the player's side of the relay (relay.h): every message survives the wire, every malformed
// datagram is refused whole, and the roster stays right through loss, repeats, a restarted server and a silent one.
#include "../relay.h"
#include <cstdio>
#include <cstring>
using namespace ds::relay;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"relay oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
template<class F> static bool throws(F f){try{f();}catch(const std::invalid_argument&){return true;}return false;}
static Error parseBuffer(const Buffer& b,Message& m){return parse(b.data(),b.size(),m);}
static Error parseBuffer(const Buffer& b){Message m;return parseBuffer(b,m);}
static Hello sampleHello(){
    Hello h;h.id=0x1122334455667788ull;h.token="Group-key_1";h.nickname="Sam Porter";h.sequence=7;h.stream=3;h.ack=9;h.area=42;
    h.havePosition=true;h.position={1234.56,-80.25,-99999.99};return h;}
static Buffer data(Type t,uint64_t peer,size_t size){std::vector<uint8_t> p(size?size:1,0xab);Buffer b;encodeData(b,t,peer,p.data(),size?size:1);if(!size)b.resize(DataHeader);return b;}

static void wire(){
    {   // HELLO: the layout, byte for byte where it matters, and the way back.
        auto h=sampleHello();auto b=encode(h);CHECK(b.size()==HelloSize&&HelloSize==112);
        CHECK(!std::memcmp(b.data(),"DSR1",4)&&b[4]==1&&b[5]==0&&b[6]==1&&b[7]==0);
        CHECK(b[8]==0x88&&b[15]==0x11);CHECK(!std::memcmp(b.data()+16,"Group-key_1",11)&&b[27]==0&&b[47]==0);
        CHECK(b[48]==7&&b[52]==3&&b[56]==9&&b[60]==42&&b[64]==1);
        CHECK(b[68]==0x40&&b[69]==0xe2&&b[70]==0x01&&b[71]==0);   // 123456 cm
        CHECK(!std::memcmp(b.data()+80,"Sam Porter",10)&&b[90]==0);
        Message m;CHECK(parseBuffer(b,m)==Error::None&&m.type==Type::Hello);auto& g=m.hello;
        CHECK(g.id==h.id&&g.token==h.token&&g.nickname==h.nickname&&g.sequence==7&&g.stream==3&&g.ack==9&&g.area==42&&g.havePosition);
        for(unsigned i=0;i<3;++i)CHECK(std::fabs(g.position[i]-h.position[i])<=.005+1e-9);
        // No position: the place is not sent at all.
        h.havePosition=false;b=encode(h);for(size_t i=68;i<80;++i)CHECK(b[i]==0);
        CHECK(parseBuffer(b,m)==Error::None&&!m.hello.havePosition);
        // The widest texts and the edge of the map.
        h=sampleHello();h.token=std::string(32,'k');h.nickname=std::string(32,'n');h.position={100000,-100000,0};
        CHECK(parseBuffer(encode(h),m)==Error::None&&m.hello.token.size()==32&&m.hello.nickname.size()==32&&m.hello.position[0]==100000);
        h.token.clear();h.nickname.clear();CHECK(parseBuffer(encode(h),m)==Error::None&&m.hello.token.empty()&&m.hello.nickname.empty());
    }
    {   // The other messages.
        Message m;
        Welcome w;w.id=5;w.sequence=11;w.stream=0xfffffff0u;w.enterMetres=150;w.leaveMetres=200;w.timeoutSeconds=10;w.nearby=3;
        auto b=encode(w);CHECK(b.size()==WelcomeSize&&parseBuffer(b,m)==Error::None&&m.type==Type::Welcome);
        CHECK(m.welcome.id==5&&m.welcome.sequence==11&&m.welcome.stream==0xfffffff0u&&m.welcome.enterMetres==150&&m.welcome.leaveMetres==200&&m.welcome.timeoutSeconds==10&&m.welcome.nearby==3);
        Refused r;r.id=6;r.sequence=12;r.reason=Refusal::IdInUse;b=encode(r);
        CHECK(b.size()==RefusedSize&&parseBuffer(b,m)==Error::None&&m.type==Type::Refused&&m.refused.id==6&&m.refused.sequence==12&&m.refused.reason==Refusal::IdInUse);
        Roster o;o.stream=4;o.first=100;o.events={{true,8,"Ann"},{false,9,""},{true,10,std::string(32,'x')}};b=encode(o);
        CHECK(b.size()==RosterHeader+10+3+10+10+32&&parseBuffer(b,m)==Error::None&&m.type==Type::Roster&&m.roster.stream==4&&m.roster.first==100&&m.roster.events.size()==3);
        CHECK(m.roster.events[0].enter&&m.roster.events[0].id==8&&m.roster.events[0].nickname=="Ann"&&!m.roster.events[1].enter&&m.roster.events[1].id==9&&m.roster.events[2].nickname.size()==32);
        o.events.assign(RosterEventsMax,{true,77,std::string(32,'y')});b=encode(o);
        CHECK(b.size()<=MaxDatagram&&parseBuffer(b,m)==Error::None&&m.roster.events.size()==RosterEventsMax);
        for(auto type:{Type::Data,Type::Relayed})for(size_t size:{size_t(1),size_t(128),MaxPayload}){
            b=data(type,0xa1a2a3a4a5a6a7a8ull,size);
            CHECK(b.size()==DataHeader+size&&parseBuffer(b,m)==Error::None&&m.type==type&&m.data.peer==0xa1a2a3a4a5a6a7a8ull&&m.data.size==size&&m.data.payload==b.data()+16&&m.data.payload[size-1]==0xab);}
        Bye y;y.id=13;y.token="t";b=encode(y);CHECK(b.size()==ByeSize&&parseBuffer(b,m)==Error::None&&m.type==Type::Bye&&m.bye.id==13&&m.bye.token=="t");
    }
    // Refusals. Every sample cut short at any length, or with one byte more, is refused.
    Roster roster;roster.stream=1;roster.first=1;roster.events={{true,8,"Ann"},{false,9,""}};
    Welcome welcome;welcome.id=5;Refused refused;refused.id=5;refused.reason=Refusal::Full;Bye bye;bye.id=5;
    std::vector<Buffer> samples={encode(sampleHello()),encode(welcome),encode(refused),encode(roster),encode(bye)};
    for(auto& s:samples){
        for(size_t n=0;n<s.size();++n){Message m;CHECK(parse(s.data(),n,m)!=Error::None);}
        auto more=s;more.push_back(0);CHECK(parseBuffer(more)==Error::Length);
        auto v=s;v[4]=2;CHECK(parseBuffer(v)==Error::Version);v=s;v[5]=1;CHECK(parseBuffer(v)==Error::Version);
        v=s;v[0]='X';CHECK(parseBuffer(v)==Error::Magic);v=s;v[7]=1;CHECK(parseBuffer(v)==Error::Reserved);
        v=s;v[6]=0;CHECK(parseBuffer(v)==Error::Type);v[6]=8;CHECK(parseBuffer(v)==Error::Type);v[6]=255;CHECK(parseBuffer(v)==Error::Type);
        if(s[6]!=uint8_t(Type::Roster)){v=s;std::fill(v.begin()+8,v.begin()+16,uint8_t(0));CHECK(parseBuffer(v)==Error::Range);}   // player id 0
    }
    {Message m;CHECK(parse(nullptr,0,m)==Error::Length);}
    CHECK(parseBuffer(data(Type::Data,5,0))==Error::Length);CHECK(parseBuffer(data(Type::Relayed,5,0))==Error::Length);   // no payload
    {auto b=data(Type::Data,5,MaxPayload);b.push_back(1);CHECK(parseBuffer(b)==Error::Length);                          // too long
     b=data(Type::Data,5,4);std::fill(b.begin()+8,b.begin()+16,uint8_t(0));CHECK(parseBuffer(b)==Error::Range);}
    {   // HELLO fields.
        auto good=encode(sampleHello());auto put32=[](Buffer& b,size_t at,int32_t v){for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(uint32_t(v)>>(8*i));};
        for(size_t axis=0;axis<3;++axis)for(int32_t cm:{10000001,-10000001,INT32_MAX,INT32_MIN}){auto b=good;put32(b,68+4*axis,cm);CHECK(parseBuffer(b)==Error::Range);}
        {auto b=good;put32(b,68,10000000);put32(b,72,-10000000);CHECK(parseBuffer(b)==Error::None);}
        {auto b=good;b[64]=2;CHECK(parseBuffer(b)==Error::Reserved);b[64]=1;b[66]=1;CHECK(parseBuffer(b)==Error::Reserved);}
        {auto b=good;b[64]=0;CHECK(parseBuffer(b)==Error::Reserved);}                       // a place without the flag
        {auto b=good;b[16+3]='/';CHECK(parseBuffer(b)==Error::Text);b=good;b[16+3]=' ';CHECK(parseBuffer(b)==Error::Text);}
        {auto b=good;b[16+20]='a';CHECK(parseBuffer(b)==Error::Text);}                       // text after the padding began
        {auto b=good;b[80+2]=0x1f;CHECK(parseBuffer(b)==Error::Text);b[80+2]=0x7f;CHECK(parseBuffer(b)==Error::Text);b[80+2]='\n';CHECK(parseBuffer(b)==Error::Text);}
        {auto b=good;b[80+20]='a';CHECK(parseBuffer(b)==Error::Text);}
        {auto b=encode(bye);b[16]='*';CHECK(parseBuffer(b)==Error::Text);}
    }
    {   // ROSTER fields.
        auto good=encode(roster);
        {auto b=good;b[16]=0;CHECK(parseBuffer(b)!=Error::None);b[16]=3;CHECK(parseBuffer(b)==Error::Length);b[16]=1;CHECK(parseBuffer(b)==Error::Length);b[16]=25;CHECK(parseBuffer(b)==Error::Range);}
        {auto b=good;b[18]=1;CHECK(parseBuffer(b)==Error::Reserved);}
        {auto b=good;b[20]=3;CHECK(parseBuffer(b)==Error::Range);b[20]=0;CHECK(parseBuffer(b)==Error::Range);}
        {auto b=good;b[29]=33;CHECK(parseBuffer(b)==Error::Range);b[29]=32;CHECK(parseBuffer(b)!=Error::None);}
        {auto b=good;b[30]=7;CHECK(parseBuffer(b)==Error::Text);}
        {auto b=good;std::fill(b.begin()+21,b.begin()+29,uint8_t(0));CHECK(parseBuffer(b)==Error::Range);}
        {auto b=encode(refused);b[20]=0;CHECK(parseBuffer(b)==Error::Range);b[20]=3;CHECK(parseBuffer(b)==Error::Range);b[20]=1;b[22]=1;CHECK(parseBuffer(b)==Error::Reserved);}
    }
    {   // The encoders refuse what the wire cannot carry.
        auto h=sampleHello();h.id=0;CHECK(throws([&]{encode(h);}));
        h=sampleHello();h.token="has space";CHECK(throws([&]{encode(h);}));h.token=std::string(33,'a');CHECK(throws([&]{encode(h);}));
        h=sampleHello();h.nickname="a\tb";CHECK(throws([&]{encode(h);}));h.nickname=std::string(33,'a');CHECK(throws([&]{encode(h);}));
        h=sampleHello();h.position[1]=std::nan("");CHECK(throws([&]{encode(h);}));h.position[1]=100000.5;CHECK(throws([&]{encode(h);}));
        h.havePosition=false;CHECK(!throws([&]{encode(h);}));
        Buffer b;uint8_t one=1;std::vector<uint8_t> big(MaxPayload+1);
        CHECK(throws([&]{encodeData(b,Type::Data,5,&one,0);}));CHECK(throws([&]{encodeData(b,Type::Data,5,big.data(),big.size());}));
        CHECK(throws([&]{encodeData(b,Type::Data,0,&one,1);}));CHECK(throws([&]{encodeData(b,Type::Hello,5,&one,1);}));
        Roster empty;CHECK(throws([&]{encode(empty);}));empty.events.assign(RosterEventsMax+1,{true,5,""});CHECK(throws([&]{encode(empty);}));
        Refused none;none.id=5;CHECK(throws([&]{encode(none);}));
    }
    {   // Damaged datagrams never take the parser outside the buffer, and what it accepts it can write again.
        uint64_t seed=0x9e3779b97f4a7c15ull;auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
        samples.push_back(data(Type::Data,5,300));unsigned accepted=0;
        for(unsigned round=0;round<40000;++round){
            auto b=samples[next()%samples.size()];
            for(unsigned k=next()%4;k;--k)b[next()%b.size()]=uint8_t(next());
            if(next()%5==0)b.resize(next()%(b.size()+1));
            std::vector<uint8_t> exact(b.begin(),b.end());   // its own allocation: a read past the end is a sanitizer finding
            Message m;if(parse(exact.data(),exact.size(),m)!=Error::None)continue;
            ++accepted;
            if(m.type==Type::Hello){Message again;CHECK(parseBuffer(encode(m.hello),again)==Error::None&&again.hello.nickname==m.hello.nickname&&again.hello.token==m.hello.token);}
            if(m.type==Type::Roster)CHECK(encode(m.roster)==exact);
            if(m.type==Type::Data)CHECK(m.data.size>=1&&m.data.size<=MaxPayload);}
        CHECK(accepted>1000);
    }
}

struct Wire {
    std::vector<Buffer> sent;
    Client::Send send(){return [this](const uint8_t* d,size_t n){sent.emplace_back(d,d+n);};}
    Message last(Type type){Message m;CHECK(!sent.empty()&&parse(sent.back().data(),sent.back().size(),m)==Error::None&&m.type==type);return m;}
};
static bool give(Client& c,const Buffer& b,uint64_t now,uint64_t* from=nullptr,std::vector<uint8_t>* payload=nullptr){
    uint64_t f=0;const uint8_t* p=nullptr;size_t n=0;bool got=c.receive(b.data(),b.size(),now,f,p,n);
    if(got&&from)*from=f;
    if(got&&payload)payload->assign(p,p+n);
    return got;}
static Buffer welcomeFor(uint64_t id,uint32_t sequence,uint32_t stream){Welcome w;w.id=id;w.sequence=sequence;w.stream=stream;w.enterMetres=150;w.leaveMetres=200;w.timeoutSeconds=10;return encode(w);}
static Buffer rosterOf(uint32_t stream,uint32_t first,std::vector<RosterEvent> events){Roster r;r.stream=stream;r.first=first;r.events=std::move(events);return encode(r);}

static void client(){
    Wire wire;ClientConfig config;config.id=1;config.token="key";config.nickname="Ann";Client c(config,wire.send());
    CHECK(throws([&]{ClientConfig bad;Client x(bad,wire.send());}));
    CHECK(c.setPosition({10,20,30},5));CHECK(!c.online());
    // HELLO at once, then every half second.
    c.tick(1000);CHECK(wire.sent.size()==1);auto h=wire.last(Type::Hello).hello;
    CHECK(h.id==1&&h.token=="key"&&h.nickname=="Ann"&&h.sequence==1&&h.stream==0&&h.ack==0&&h.area==5&&h.havePosition&&h.position[1]==20);
    c.tick(1499);CHECK(wire.sent.size()==1);c.tick(1500);CHECK(wire.sent.size()==2&&wire.last(Type::Hello).hello.sequence==2);
    // A place the wire cannot carry is not reported; neither is none.
    CHECK(!c.setPosition({1e9,0,0}));c.tick(2000);CHECK(!wire.last(Type::Hello).hello.havePosition);
    c.setPosition({10,20,30},5);c.clearPosition();c.tick(2500);CHECK(!wire.last(Type::Hello).hello.havePosition);c.setPosition({10,20,30},5);
    // Nothing from the relay counts before a WELCOME; one for another player or for a HELLO never sent does not count.
    CHECK(!give(c,rosterOf(7,1,{{true,2,"Bob"}}),2600)&&c.nearby().empty());
    give(c,welcomeFor(2,4,7),2600);CHECK(!c.online());give(c,welcomeFor(1,99,7),2600);CHECK(!c.online());
    give(c,welcomeFor(1,4,7),2600);CHECK(c.online()&&c.welcome().enterMetres==150);
    // The roster: in order, each event once.
    give(c,rosterOf(7,1,{{true,2,"Bob"},{true,3,""}}),2700);
    auto e=c.takeEvents();CHECK(e.size()==2&&e[0].enter&&e[0].id==2&&e[0].nickname=="Bob"&&e[1].id==3&&c.nearby().size()==2);
    size_t before=wire.sent.size();c.tick(2701);CHECK(wire.sent.size()==before+1);h=wire.last(Type::Hello).hello;CHECK(h.stream==7&&h.ack==2);   // acknowledged at once
    give(c,rosterOf(7,1,{{true,2,"Bob"},{true,3,""}}),2800);CHECK(c.takeEvents().empty());                 // a repeat
    give(c,rosterOf(7,5,{{true,9,"Far"}}),2800);CHECK(c.takeEvents().empty()&&!c.nearby().count(9));           // a gap: not yet
    give(c,rosterOf(8,3,{{true,9,"Far"}}),2800);CHECK(c.takeEvents().empty());                               // another stream
    give(c,rosterOf(7,2,{{true,3,""},{false,3,""},{true,1,"me"}}),2900);                                     // one known, two new
    e=c.takeEvents();CHECK(e.size()==1&&!e[0].enter&&e[0].id==3&&c.nearby().size()==1);
    give(c,rosterOf(7,3,{{false,3,""}}),2950);CHECK(c.takeEvents().empty());
    c.tick(2950);CHECK(wire.last(Type::Hello).hello.ack==4);
    give(c,rosterOf(7,5,{{true,3,"C"},{false,3,""}}),3000);CHECK(c.takeEvents().size()==2);
    give(c,rosterOf(7,5,{{true,3,"C"},{false,3,""}}),3000);CHECK(c.takeEvents().empty()&&c.nearby().size()==1);   // the pair again: nobody comes back
    // Core datagrams: to and from players in the roster only.
    uint8_t core[128];std::memset(core,0x5a,sizeof core);
    CHECK(c.sendTo(2,core,sizeof core));auto d=wire.last(Type::Data).data;CHECK(d.peer==2&&d.size==128&&d.payload[127]==0x5a);
    before=wire.sent.size();CHECK(!c.sendTo(3,core,sizeof core)&&!c.sendTo(2,core,0)&&!c.sendTo(2,nullptr,5)&&wire.sent.size()==before);
    {std::vector<uint8_t> big(MaxPayload+1);CHECK(!c.sendTo(2,big.data(),big.size()));big.pop_back();CHECK(c.sendTo(2,big.data(),big.size()));}
    uint64_t from=0;std::vector<uint8_t> payload;
    CHECK(give(c,data(Type::Relayed,2,128),3000,&from,&payload)&&from==2&&payload.size()==128&&payload[0]==0xab);
    CHECK(!give(c,data(Type::Relayed,3,128),3000));CHECK(!give(c,data(Type::Data,2,128),3000));   // a stranger; a message meant for the server
    CHECK(!give(c,Buffer(40,0),3000));
    // An older WELCOME that arrives late changes nothing; a new stream means the server knows nothing: start again.
    c.tick(3500);auto sequence=wire.last(Type::Hello).hello.sequence;
    give(c,welcomeFor(1,sequence,7),3500);CHECK(c.takeEvents().empty()&&c.nearby().size()==1);
    give(c,welcomeFor(1,sequence-1,9),3500);CHECK(c.takeEvents().empty()&&c.nearby().size()==1);
    c.tick(4000);sequence=wire.last(Type::Hello).hello.sequence;give(c,welcomeFor(1,sequence,9),4000);
    e=c.takeEvents();CHECK(e.size()==1&&!e[0].enter&&e[0].id==2&&e[0].nickname=="Bob"&&c.nearby().empty()&&c.online());
    c.tick(4500);h=wire.last(Type::Hello).hello;CHECK(h.stream==9&&h.ack==0);
    CHECK(!give(c,data(Type::Relayed,2,128),4500));
    give(c,rosterOf(7,7,{{true,4,"old"}}),4500);CHECK(c.takeEvents().empty());      // the old stream's next event
    give(c,rosterOf(9,1,{{true,2,"Bob"}}),4500);CHECK(c.takeEvents().size()==1);
    // A refusal is reported until a HELLO is accepted again.
    c.tick(5000);sequence=wire.last(Type::Hello).hello.sequence;
    {Refused r;r.id=1;r.sequence=sequence;r.reason=Refusal::IdInUse;give(c,encode(r),5000);CHECK(c.refusal()==Refusal::IdInUse);}
    c.tick(5500);give(c,welcomeFor(1,wire.last(Type::Hello).hello.sequence,9),5500);CHECK(c.refusal()==Refusal::None&&c.nearby().size()==1);
    // The relay goes silent: after five seconds everyone near is gone and the next HELLO asks for everything again.
    for(uint64_t t=6000;t<=10400;t+=100)c.tick(t);
    CHECK(c.online()&&c.takeEvents().empty());
    c.tick(10500);c.tick(10600);
    e=c.takeEvents();CHECK(!c.online()&&e.size()==1&&!e[0].enter&&e[0].id==2&&c.nearby().empty());
    CHECK(!c.sendTo(2,core,sizeof core));
    c.tick(11100);h=wire.last(Type::Hello).hello;CHECK(h.stream==0&&h.ack==0);
    give(c,welcomeFor(1,h.sequence,9),11100);give(c,rosterOf(9,1,{{true,2,"Bob"}}),11100);CHECK(c.online()&&c.takeEvents().size()==1);
    // BYE: said once, everyone forgotten.
    c.bye();auto y=wire.last(Type::Bye).bye;CHECK(y.id==1&&y.token=="key");e=c.takeEvents();CHECK(e.size()==1&&!e[0].enter&&!c.online());
}
int main(){
    wire();client();
    if(failures)return 1;
    std::puts("relay envelope tests passed");return 0;
}
