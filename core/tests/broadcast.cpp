// The broadcast envelope (broadcast.h) and the player's side of it (broadcast_client.h): the layout byte for byte, every
// message through the wire and back, every malformed datagram refused whole; then a Client driven by hand-made server
// messages: the server's clock, the roster, entries in and out of order, strangers, impossible motion and teleports,
// versioned state with gaps repaired, what it publishes, and interpolation on the server's clock over a lossy, jittery
// link at the full rate and at the rate the server keeps for far players.
#include "../broadcast_client.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
using namespace ds;
using namespace ds::broadcast;
using WireError=ds::relay::Error;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"broadcast oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
template<class F> static bool throws(F f){try{f();}catch(const std::invalid_argument&){return true;}return false;}
static WireError parseBuffer(const Buffer& b,Message& m){return parse(b.data(),b.size(),m);}
static WireError parseBuffer(const Buffer& b){Message m;return parseBuffer(b,m);}
static uint64_t get(const Buffer& b,size_t at,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[at+i])<<(8*i);return v;}
static BuildId buildOf(uint8_t seed){BuildId b{};for(size_t i=0;i<b.size();++i)b[i]=uint8_t(seed+i);return b;}
static ds::State walker(double x,double z=0){ds::State s;s.position={x,1.5,z};s.velocity={1,0,0};s.yaw=0.5;s.action=3;s.phase=0.25;s.flags=Sprint;return s;}
static Presence blockOf(const ds::State& s){Packet q;q.kind=ds::Kind::Presence;q.state=s;auto b=ds::encode(q);return presenceOf(b.data());}
static Hello sampleHello(){
    Hello h;h.base.id=0x1122334455667788ull;h.base.token="group";h.base.nickname="Sam";h.base.sequence=7;h.base.stream=3;h.base.ack=9;h.base.area=42;
    h.base.havePosition=true;h.base.position={12.5,-3,100};h.game=13300582;h.build=buildOf(0xa0);h.t0=123456789;return h;}

static void wire(){
    {   // HELLO: version 1's 112 bytes with 2 in the version field, then the build and the clock.
        auto h=sampleHello();auto b=encode(h);CHECK(b.size()==HelloSize&&HelloSize==140);
        CHECK(!std::memcmp(b.data(),"DSR1",4)&&b[4]==2&&b[5]==0&&b[6]==1&&b[7]==0);
        auto v1=relay::encode(h.base);CHECK(std::equal(v1.begin()+6,v1.end(),b.begin()+6));
        CHECK(get(b,112,4)==13300582&&b[116]==0xa0&&b[131]==0xaf&&get(b,132,8)==123456789);
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Hello&&m.hello.base.id==h.base.id&&m.hello.base.token=="group"&&m.hello.base.nickname=="Sam");
        CHECK(m.hello.base.area==42&&m.hello.base.havePosition&&m.hello.base.position[2]==100&&m.hello.game==13300582&&m.hello.build==h.build&&m.hello.t0==123456789);
        // Version 1's parser refuses it, and this one refuses version 1's.
        relay::Message m1;CHECK(relay::parse(b.data(),b.size(),m1)==WireError::Version);
        CHECK(parse(v1.data(),v1.size(),m)==WireError::Version);
        auto as1=b;as1[4]=1;CHECK(parseBuffer(as1)==WireError::Version);as1[4]=3;CHECK(parseBuffer(as1)==WireError::Version);
        CHECK(versionOf(b.data(),b.size())==2&&versionOf(v1.data(),v1.size())==1&&versionOf(b.data(),3)==0);
        // Byte 65, reserved in version 1: who the player wants to be shown. 0 (everyone) is the HELLO of before.
        CHECK(b[65]==0&&m.hello.show==Show::Everyone);
        for(auto show:{Show::Friends,Show::Nobody}){
            h.show=show;auto c=encode(h);CHECK(c[65]==uint8_t(show)&&std::equal(c.begin(),c.begin()+65,b.begin())&&std::equal(c.begin()+66,c.end(),b.begin()+66));
            CHECK(parseBuffer(c,m)==WireError::None&&m.hello.show==show&&m.hello.base.nickname=="Sam"&&m.hello.base.area==42&&m.hello.t0==123456789);}
        for(uint8_t bad:{3,4,255}){auto c=b;c[65]=bad;CHECK(parseBuffer(c)==WireError::Range);}
        {auto c=b;c[66]=1;CHECK(parseBuffer(c)==WireError::Reserved);}   // the other reserved bytes stay reserved
        h.show=Show(3);CHECK(throws([&]{encode(h);}));
    }
    {   // WELCOME.
        Welcome w;w.base.id=5;w.base.sequence=11;w.base.stream=99;w.base.enterMetres=150;w.base.leaveMetres=200;w.base.timeoutSeconds=10;w.base.nearby=3;
        w.t0=1000;w.serverTime=0x123456789aull;w.versions={1,2,0,0xffffffffu};w.fullMetres=30;w.animationMetres=50;w.intervalMs=200;w.otherBuild=4;w.budget=1200;
        auto b=encode(w);CHECK(b.size()==WelcomeSize&&WelcomeSize==76&&b[4]==2&&b[6]==2);
        CHECK(get(b,32,8)==1000&&get(b,40,8)==0x123456789aull&&get(b,48,4)==1&&get(b,52,4)==2&&get(b,60,4)==0xffffffffu&&get(b,64,2)==30&&get(b,66,2)==50&&get(b,68,2)==200&&get(b,70,2)==4&&get(b,72,2)==1200);
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Welcome&&m.welcome.base.stream==99&&m.welcome.base.nearby==3&&m.welcome.serverTime==w.serverTime&&m.welcome.versions==w.versions&&m.welcome.budget==1200);
        auto r=b;r[74]=1;CHECK(parseBuffer(r)==WireError::Reserved);
    }
    {   // REFUSED, with the new reason.
        Refused r;r.id=6;r.sequence=12;r.reason=Refusal::Build;auto b=encode(r);
        Message m;CHECK(b.size()==RefusedSize&&b[20]==3&&parseBuffer(b,m)==WireError::None&&m.refused.reason==Refusal::Build&&m.refused.sequence==12);
        b[20]=4;CHECK(parseBuffer(b)==WireError::Range);b[20]=0;CHECK(parseBuffer(b)==WireError::Range);
    }
    {   // ROSTER and BYE: version 1's.
        relay::Roster o;o.stream=4;o.first=100;o.events={{true,8,"Ann"},{false,9,""}};auto b=encodeRoster(o);
        Message m;CHECK(b[4]==2&&parseBuffer(b,m)==WireError::None&&m.type==Type::Roster&&m.roster.first==100&&m.roster.events.size()==2&&m.roster.events[0].nickname=="Ann");
        relay::Bye y;y.id=13;y.token="t";b=encodeBye(y);CHECK(b.size()==ByeSize&&b[6]==10&&parseBuffer(b,m)==WireError::None&&m.type==Type::Bye&&m.bye.id==13);
    }
    Presence block=blockOf(walker(12.34));
    std::vector<uint8_t> animation=encodeAnimationPayload([]{Animation a;a.held[0]=1;a.rose[2]=4;a.floats[5]=0.5f;return a;}());
    {   // PUBLISH: presence and animation, either alone.
        Update u;u.player=77;u.sequence=0xfffffffeu;u.stamp=5000;u.parts=HasPresence|HasAnimation;u.presence=block.data();u.animation=animation.data();u.animationSize=animation.size();
        auto b=encodePublish(u);CHECK(b.size()==PublishHeader+PresenceBytes+animation.size()&&b[6]==5);
        CHECK(get(b,8,8)==77&&get(b,16,4)==0xfffffffeu&&b[20]==3&&b[21]==0&&get(b,22,2)==animation.size()&&get(b,24,8)==5000);
        CHECK(!std::memcmp(b.data()+32,block.data(),PresenceBytes)&&!std::memcmp(b.data()+84,animation.data(),animation.size()));
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Publish&&m.publish.player==77&&m.publish.parts==3&&m.publish.presence==b.data()+32&&m.publish.animationSize==animation.size());
        u.parts=HasPresence;u.animationSize=0;CHECK(parseBuffer(encodePublish(u),m)==WireError::None&&m.publish.animation==nullptr);
        u.parts=HasAnimation;u.animationSize=animation.size();b=encodePublish(u);CHECK(b.size()==PublishHeader+animation.size()&&parseBuffer(b,m)==WireError::None&&m.publish.presence==nullptr);
        u.parts=0;CHECK(throws([&]{encodePublish(u);}));u.parts=4|HasPresence;CHECK(throws([&]{encodePublish(u);}));
        u.parts=HasAnimation;u.animationSize=0;CHECK(throws([&]{encodePublish(u);}));
        u.parts=HasPresence|HasAnimation;b=encodePublish({77,1,5000,3,0,block.data(),animation.data(),animation.size()});
        auto v=b;v[20]=0;CHECK(parseBuffer(v)!=WireError::None);v=b;v[20]=7;CHECK(parseBuffer(v)==WireError::Reserved);v=b;v[21]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        v=b;v[22]=uint8_t(animation.size()+1);CHECK(parseBuffer(v)==WireError::Length);v=b;v[22]=0;v[23]=0;CHECK(parseBuffer(v)!=WireError::None);
        v=b;std::fill(v.begin()+8,v.begin()+16,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[84]=33;CHECK(parseBuffer(v)==WireError::Range);   // more bool bytes than the core has
        v=b;v[31]=0x40;CHECK(parseBuffer(v)==WireError::Range);  // a stamp past 2^53
    }
    {   // FRAME: entries packed back to back, the count and the server's time at the front.
        Buffer b;beginFrame(b);
        Update a;a.player=1;a.sequence=10;a.stamp=900;a.parts=HasPresence;a.presence=block.data();a.generation=7;
        Update c;c.player=2;c.sequence=20;c.stamp=901;c.parts=HasPresence|HasAnimation;c.presence=block.data();c.animation=animation.data();c.animationSize=animation.size();
        Update d;d.player=3;d.sequence=30;d.stamp=902;d.parts=HasAnimation;d.animation=animation.data();d.animationSize=animation.size();
        appendEntry(b,a);appendEntry(b,c);appendEntry(b,d);finishFrame(b,4242);
        CHECK(b.size()==FrameHeader+3*EntryHeader+2*PresenceBytes+2*animation.size()&&b[6]==6&&get(b,8,8)==4242&&b[16]==3);
        CHECK(get(b,20,8)==1&&get(b,28,4)==10&&get(b,32,8)==900&&b[40]==1&&get(b,42,2)==0&&get(b,44,2)==7&&get(b,46,2)==0);
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Frame&&m.frame.serverTime==4242&&m.frame.entries.size()==3);
        CHECK(m.frame.entries[0].generation==7&&m.frame.entries[1].animationSize==animation.size()&&m.frame.entries[2].presence==nullptr&&m.frame.entries[2].player==3);
        CHECK(!std::memcmp(m.frame.entries[1].presence,block.data(),PresenceBytes)&&!std::memcmp(m.frame.entries[2].animation,animation.data(),animation.size()));
        auto v=b;v.push_back(0);CHECK(parseBuffer(v)==WireError::Length);   // a byte after the last entry
        v=b;v[16]=4;CHECK(parseBuffer(v)==WireError::Length);v=b;v[16]=0;CHECK(parseBuffer(v)==WireError::Range);v=b;v[17]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        v=b;v[46]=1;CHECK(parseBuffer(v)==WireError::Reserved);v=b;std::fill(v.begin()+20,v.begin()+28,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
        // Nothing past the datagram's 1200 bytes.
        Buffer big;beginFrame(big);size_t fitted=0;try{for(;;){appendEntry(big,c);++fitted;}}catch(const std::invalid_argument&){}
        finishFrame(big,1);CHECK(big.size()<=MaxDatagram&&parseBuffer(big)==WireError::None&&fitted==(MaxDatagram-FrameHeader)/entrySize(c));
        Buffer empty;beginFrame(empty);CHECK(throws([&]{finishFrame(empty,1);}));
        // The longest entry fits alone: a full animation payload.
        std::vector<uint8_t> longest(AnimationMax,0);longest[0]=0;Update l=c;l.animation=longest.data();l.animationSize=longest.size();
        Buffer one;beginFrame(one);appendEntry(one,l);finishFrame(one,1);CHECK(one.size()<=MaxDatagram&&parseBuffer(one)==WireError::None);
    }
    {   // STATE up, SHARE down, FETCH.
        std::vector<uint8_t> payload(300,0x5a);
        StatePost s;s.player=9;s.kind=StateKind::Gear;s.version=4;s.payload=payload.data();s.size=payload.size();auto b=encode(s);
        CHECK(b.size()==StateHeader+300&&b[6]==7&&get(b,8,8)==9&&b[16]==2&&get(b,18,2)==300&&get(b,20,4)==4&&b[24]==0x5a);
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::State&&m.state.kind==StateKind::Gear&&m.state.version==4&&m.state.size==300);
        auto v=b;v[16]=5;CHECK(parseBuffer(v)==WireError::Range);v[16]=0;CHECK(parseBuffer(v)==WireError::Range);
        v=b;std::fill(v.begin()+20,v.begin()+24,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);v=b;v.pop_back();CHECK(parseBuffer(v)==WireError::Length);
        s.size=StateMax+1;std::vector<uint8_t> huge(StateMax+1);s.payload=huge.data();CHECK(throws([&]{encode(s);}));
        Share h;h.source=9;h.kind=StateKind::Carry;h.version=6;h.generation=0xfffe;h.versions={1,0,6,2};h.payload=payload.data();h.size=payload.size();b=encode(h);
        CHECK(b.size()==ShareHeader+300&&b[6]==8&&get(b,24,2)==0xfffe&&get(b,28,4)==1&&get(b,36,4)==6&&get(b,40,4)==2);
        CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Share&&m.share.kind==StateKind::Carry&&m.share.generation==0xfffe&&m.share.versions==h.versions);
        v=b;v[36]=7;CHECK(parseBuffer(v)==WireError::Range);   // the kind's version disagrees with the list
        v=b;v[16]=6;CHECK(parseBuffer(v)==WireError::Range);
        // Nothing of a kind: version 0 and no payload, and nothing else.
        h.version=0;h.versions[2]=0;h.size=0;b=encode(h);CHECK(b.size()==ShareHeader&&parseBuffer(b,m)==WireError::None&&m.share.version==0&&m.share.payload==nullptr);
        v=b;v.push_back(1);CHECK(parseBuffer(v)!=WireError::None);
        h.version=3;CHECK(throws([&]{encode(h);}));
        Fetch f;f.player=1;f.source=2;b=encode(f);CHECK(b.size()==FetchSize&&get(b,16,8)==2&&parseBuffer(b,m)==WireError::None&&m.fetch.source==2);
        v=b;std::fill(v.begin()+16,v.end(),uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
    }
    // The fight channel: FIGHT and OFFER share a layout, JOIN, STATUS, POST and DELIVER.
    FightInfo info;info.fight=0x01020304;info.kind=1;info.most=9;info.radius=300;info.place={1.25,-2,1000};info.area=7;
    {
        Fight f;f.player=0x55;f.op=FightOp::Open;f.info=info;auto b=encode(f);
        CHECK(b.size()==FightSize&&FightSize==44&&b[4]==2&&b[6]==11&&get(b,8,8)==0x55&&get(b,16,4)==0x01020304&&b[20]==1&&b[21]==1&&b[22]==9&&b[23]==0);
        CHECK(get(b,24,2)==300&&get(b,26,2)==0&&int32_t(get(b,28,4))==125&&int32_t(uint32_t(get(b,32,4)))==-200&&get(b,36,4)==100000&&get(b,40,4)==7);
        Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Fight&&m.fight.player==0x55&&m.fight.op==FightOp::Open&&m.fight.info.fight==info.fight);
        CHECK(m.fight.info.kind==1&&m.fight.info.most==9&&m.fight.info.radius==300&&m.fight.info.place==info.place&&m.fight.info.area==7);
        f.op=FightOp::Close;CHECK(parseBuffer(encode(f),m)==WireError::None&&m.fight.op==FightOp::Close);
        auto v=b;v[20]=3;CHECK(parseBuffer(v)==WireError::Range);v=b;v[21]=0;CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[22]=0;CHECK(parseBuffer(v)==WireError::Range);v=b;v[22]=33;CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[24]=0;v[25]=0;CHECK(parseBuffer(v)==WireError::Range);v=b;v[24]=0xe9;v[25]=3;CHECK(parseBuffer(v)==WireError::Range);   // 1001 m
        v=b;std::fill(v.begin()+16,v.begin()+20,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[23]=1;CHECK(parseBuffer(v)==WireError::Reserved);v=b;v[27]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        v=b;v[31]=0x7f;CHECK(parseBuffer(v)==WireError::Range);   // a place beyond 100 km
        info.most=0;f.info=info;CHECK(throws([&]{encode(f);}));info.most=9;
        f.info=info;f.op=FightOp(3);CHECK(throws([&]{encode(f);}));
        Offer o;o.owner=0x55;o.visitors=4;o.info=info;b=encode(o);
        CHECK(b.size()==OfferSize&&b[6]==12&&b[20]==4&&b[21]==1&&b[22]==9&&parseBuffer(b,m)==WireError::None&&m.type==Type::Offer&&m.offer.owner==0x55&&m.offer.visitors==4&&m.offer.info.radius==300);
        v=b;v[20]=33;CHECK(parseBuffer(v)==WireError::Range);
        Join j;j.player=0x66;j.owner=0x55;j.fight=info.fight;j.op=JoinOp::Join;b=encode(j);
        CHECK(b.size()==JoinSize&&JoinSize==32&&b[6]==13&&get(b,8,8)==0x66&&get(b,16,8)==0x55&&get(b,24,4)==info.fight&&b[28]==1);
        CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Join&&m.join.owner==0x55&&m.join.op==JoinOp::Join);
        v=b;v[28]=3;CHECK(parseBuffer(v)==WireError::Range);v=b;v[29]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        v=b;std::fill(v.begin()+16,v.begin()+24,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
        Status st;st.owner=0x55;st.fight=info.fight;st.status=FightStatus::Released;st.visitors=9;b=encode(st);
        CHECK(b.size()==StatusSize&&b[6]==14&&b[20]==9&&b[21]==9&&parseBuffer(b,m)==WireError::None&&m.type==Type::Status&&m.status.status==FightStatus::Released);
        v=b;v[20]=12;CHECK(parseBuffer(v)==WireError::Range);v=b;v[20]=0;CHECK(parseBuffer(v)==WireError::Range);v=b;v[21]=33;CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[22]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        st.status=FightStatus(12);CHECK(throws([&]{encode(st);}));
        std::vector<uint8_t> payload(200,7);Post p;p.player=0x66;p.owner=0x55;p.fight=info.fight;p.version=3;p.payload=payload.data();p.size=payload.size();
        b=encodePost(p);CHECK(b.size()==PostHeader+200&&b[6]==15&&get(b,8,8)==0x66&&get(b,16,8)==0x55&&get(b,24,4)==info.fight&&get(b,28,4)==3&&get(b,32,2)==200&&b[36]==7);
        CHECK(parseBuffer(b,m)==WireError::None&&m.type==Type::Post&&m.post.player==0x66&&m.post.size==200&&m.post.payload==b.data()+36);
        auto d=encodeDeliver(p);CHECK(d[6]==16&&std::equal(d.begin()+7,d.end(),b.begin()+7)&&parseBuffer(d,m)==WireError::None&&m.type==Type::Deliver);
        v=b;v.pop_back();CHECK(parseBuffer(v)==WireError::Length);v=b;std::fill(v.begin()+28,v.begin()+32,uint8_t(0));CHECK(parseBuffer(v)==WireError::Range);
        v=b;v[34]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        p.size=PostMax+1;std::vector<uint8_t> huge(PostMax+1);p.payload=huge.data();CHECK(throws([&]{encodePost(p);}));
        // A server or a game from before the channel refuses the types (version 1's parser here stands for it).
        relay::Message m1;CHECK(relay::parse(b.data(),b.size(),m1)!=WireError::None);
    }
    // Every sample cut short at any length, one byte longer, another magic, type or reserved byte: refused.
    std::vector<Buffer> samples;
    {Welcome w;w.base.id=5;Refused r;r.id=5;r.reason=Refusal::Full;relay::Bye y;y.id=5;Fetch f;f.player=1;f.source=2;
     std::vector<uint8_t> payload(10,1);StatePost s;s.player=9;s.version=1;s.payload=payload.data();s.size=10;
     Share h;h.source=9;h.version=1;h.versions={1,0,0,0};h.payload=payload.data();h.size=10;
     Buffer frame;beginFrame(frame);Update a;a.player=1;a.parts=HasPresence;a.presence=block.data();appendEntry(frame,a);finishFrame(frame,1);
     Update p=a;p.parts=HasPresence|HasAnimation;p.animation=animation.data();p.animationSize=animation.size();
     samples={encode(sampleHello()),encode(w),encode(r),encodeBye(y),encode(f),encode(s),encode(h),frame,encodePublish(p)};
     Fight g;g.player=3;g.info=info;Offer o;o.owner=3;o.info=info;Join j;j.player=4;j.owner=3;j.fight=1;Status st;st.owner=3;st.fight=1;
     Post q;q.player=4;q.owner=3;q.fight=1;q.version=1;q.payload=payload.data();q.size=10;
     for(auto x:{encode(g),encode(o),encode(j),encode(st),encodePost(q),encodeDeliver(q)})samples.push_back(x);}
    {Mark k;k.player=5;k.mark=0x12300001;samples.push_back(encodeMark(k));samples.push_back(encodeMarked(k));}
    {Signal g;g.player=5;g.serial=3;g.place={1,2,3};samples.push_back(encodeSignal(g));g.kind=SignalKind::Like;g.target=6;samples.push_back(encodeSignalled(g));}
    for(auto& s:samples){
        for(size_t n=0;n<s.size();++n){Message m;CHECK(parse(s.data(),n,m)!=WireError::None);}
        auto more=s;more.push_back(0);CHECK(parseBuffer(more)!=WireError::None);
        auto v=s;v[0]='X';CHECK(parseBuffer(v)==WireError::Magic);v=s;v[7]=1;CHECK(parseBuffer(v)==WireError::Reserved);
        v=s;v[6]=0;CHECK(parseBuffer(v)==WireError::Type);v[6]=TypeLast+1;CHECK(parseBuffer(v)==WireError::Type);
        v=s;v[4]=1;CHECK(parseBuffer(v)==WireError::Version);}
    {Message m;CHECK(parse(nullptr,0,m)==WireError::Length);Buffer big(MaxDatagram+1,0);std::memcpy(big.data(),"DSR1\x02\x00\x06\x00",8);CHECK(parseBuffer(big)==WireError::Length);}
    // Damaged datagrams: never accepted with a wrong shape, never a crash.
    std::mt19937_64 rng(5);unsigned taken=0;
    for(unsigned i=0;i<40000;++i){
        auto v=samples[i%samples.size()];unsigned flips=1+unsigned(rng()%4);
        for(unsigned k=0;k<flips;++k)v[rng()%v.size()]^=uint8_t(1u<<(rng()%8));
        if(rng()%5==0)v.resize(rng()%(v.size()+1));
        Message m;if(parseBuffer(v,m)==WireError::None)++taken;}
    CHECK(taken<40000);
}

// ---- The player's side, against a hand-made server ----------------------------------------------------------------------
struct Bench {
    uint64_t now=10000;int64_t offset=500000;   // the server's clock is this side's plus offset
    std::vector<Buffer> sent;Client client;uint32_t stream=77;uint32_t rosterNext=1;
    explicit Bench(uint32_t delayMs=120):client(config(delayMs),[this](const uint8_t* d,size_t n){sent.emplace_back(d,d+n);}){}
    static ClientConfig config(uint32_t delayMs){ClientConfig c;c.id=1;c.token="group";c.nickname="Me";c.build=buildOf(1);c.delayMs=delayMs;return c;}
    uint64_t server() const {return uint64_t(int64_t(now)+offset);}
    std::vector<Message> take(Type t){std::vector<Message> out;for(auto& b:sent){Message m;if(parse(b.data(),b.size(),m)==WireError::None&&m.type==t)out.push_back(m);}return out;}
    // The latest HELLO answered: `up` ms to the server, `down` back.
    void answer(uint32_t up=10,uint32_t down=10,Versions versions={},uint16_t nearby=0,bool advance=true){
        auto hellos=take(Type::Hello);if(hellos.empty())return;auto h=hellos.back().hello;
        Welcome w;w.base.id=1;w.base.sequence=h.base.sequence;w.base.stream=stream;w.base.enterMetres=150;w.base.leaveMetres=200;w.base.timeoutSeconds=10;w.base.nearby=nearby;
        w.t0=h.t0;w.serverTime=uint64_t(int64_t(h.t0)+offset+up);w.versions=versions;w.fullMetres=30;w.animationMetres=50;w.intervalMs=200;w.budget=1200;
        if(advance){now+=up+down;}
        auto b=encode(w);client.receive(b.data(),b.size(),now);}
    void online(){client.tick(now);answer();}
    void roster(std::vector<relay::RosterEvent> events){
        relay::Roster r;r.stream=stream;r.first=rosterNext;r.events=std::move(events);rosterNext+=uint32_t(r.events.size());
        auto b=encodeRoster(r);client.receive(b.data(),b.size(),now);client.takeEvents();}
    void frame(const std::vector<Update>& entries){
        Buffer b;beginFrame(b);for(auto& u:entries)appendEntry(b,u);finishFrame(b,server());client.receive(b.data(),b.size(),now);}
    // One entry of `source` with this state stamped at server time `stamp`.
    Presence keep;
    Update entry(uint64_t source,uint32_t sequence,uint64_t stamp,const ds::State& s,uint16_t generation=0){
        keep=blockOf(s);Update u;u.player=source;u.sequence=sequence;u.stamp=stamp;u.parts=HasPresence;u.presence=keep.data();u.generation=generation;return u;}
    void share(uint64_t source,StateKind kind,uint32_t version,uint16_t generation,Versions versions,std::vector<uint8_t> bytes){
        Share h;h.source=source;h.kind=kind;h.version=version;h.generation=generation;h.versions=versions;h.payload=bytes.data();h.size=bytes.size();
        auto b=encode(h);client.receive(b.data(),b.size(),now);}
};

static void clockAndRoster(){
    Bench t;t.client.setPosition({1,2,3});t.client.tick(t.now);
    auto hello=t.take(Type::Hello);CHECK(hello.size()==1&&hello[0].hello.build==buildOf(1)&&hello[0].hello.game==GameBuild&&hello[0].hello.t0==t.now);
    CHECK(hello[0].hello.show==Show::Everyone);
    // Who the player wants to be shown goes in every HELLO; a choice the wire has no value for is refused.
    {auto c=Bench::config(120);c.show=Show::Friends;std::vector<Buffer> sent;Client f(c,[&](const uint8_t* d,size_t n){sent.emplace_back(d,d+n);});
     f.tick(5);f.tick(600);Message m;CHECK(sent.size()==2&&parse(sent[1].data(),sent[1].size(),m)==WireError::None&&m.hello.show==Show::Friends);
     c.show=Show(3);CHECK(throws([&]{Client x(c,[](const uint8_t*,size_t){});}));}
    CHECK(!t.client.clockValid()&&!t.client.online());
    // Symmetric 10 ms each way: the offset to within a millisecond.
    t.answer(10,10);CHECK(t.client.online()&&t.client.clockValid()&&std::fabs(t.client.offsetMs()-500000)<=1&&t.client.rttMs()==20);
    CHECK(t.client.serverNow(t.now)==t.server()||t.client.serverNow(t.now)+1==t.server()||t.client.serverNow(t.now)==t.server()+1);
    // A slow round trip later does not move it more than 1 % of the time passed; the quickest round trip wins.
    for(int i=0;i<10;++i){t.now+=500;t.client.tick(t.now);t.answer(200,10);}
    CHECK(std::fabs(t.client.offsetMs()-500000)<=1);
    // A WELCOME for a HELLO never sent, or a second answer to the same one: no clock sample, no change.
    {Welcome w;w.base.id=1;w.base.sequence=999;w.base.stream=t.stream;w.t0=1;w.serverTime=1;auto b=encode(w);t.client.receive(b.data(),b.size(),t.now);}
    CHECK(std::fabs(t.client.offsetMs()-500000)<=1&&t.client.online());
    // The roster: in order, with the server's nicknames.
    t.roster({{true,2,"Ann"},{true,3,"Bob"}});CHECK(t.client.nearby().size()==2&&t.client.nearby().at(2)=="Ann");
    {relay::Roster r;r.stream=t.stream;r.first=10;r.events={{true,4,"Cy"}};auto b=encodeRoster(r);t.client.receive(b.data(),b.size(),t.now);}
    CHECK(!t.client.nearby().count(4));   // a gap is not taken
    // A WELCOME with another stream: the server began again; everyone leaves, the clock is taken again.
    t.stream=78;t.rosterNext=1;t.now+=500;t.client.tick(t.now);t.offset=900000;t.answer(10,10);
    auto events=t.client.takeEvents();
    CHECK(t.client.nearby().empty()&&events.size()==2&&!events[0].enter&&!events[1].enter);
    CHECK(std::fabs(t.client.offsetMs()-900000)<=1);
    // Five seconds without a WELCOME: offline.
    t.roster({{true,2,"Ann"}});CHECK(t.client.nearby().size()==1);
    for(int i=0;i<12;++i){t.now+=500;t.client.tick(t.now);}
    CHECK(!t.client.online()&&t.client.nearby().empty());
    // Refused for another build.
    {auto hellos=t.take(Type::Hello);Refused r;r.id=1;r.sequence=hellos.back().hello.base.sequence;r.reason=Refusal::Build;auto b=encode(r);t.client.receive(b.data(),b.size(),t.now);}
    CHECK(t.client.refusal()==Refusal::Build);
}

static void entries(){
    Bench t;t.online();t.roster({{true,2,"Ann"},{true,3,"Bob"}});
    uint64_t s0=t.server();
    // In order: taken and shown on the server's clock.
    for(uint32_t i=0;i<10;++i){t.frame({t.entry(2,i+1,s0+50*i,walker(10+0.05*i))});t.now+=50;}
    CHECK(t.client.counters().accepted==10&&t.client.live(2,t.now));
    auto r=t.client.render(2,t.now);CHECK(r.visible&&r.state.position[0]>10&&r.state.position[0]<10.5);
    // A repeat, an older one and one whose stamp does not advance: refused.
    auto accepted=t.client.counters().accepted;
    t.frame({t.entry(2,10,s0+450,walker(10.45))});t.frame({t.entry(2,5,s0+200,walker(10.2))});t.frame({t.entry(2,11,s0+450,walker(10.5))});
    CHECK(t.client.counters().accepted==accepted&&t.client.counters().refused>=3);
    // A stamp far from the server's clock: refused.
    t.frame({t.entry(2,12,t.server()+5000,walker(10.5))});t.frame({t.entry(2,13,t.server()-5000,walker(10.5))});
    CHECK(t.client.counters().accepted==accepted);
    // A stranger: counted, nothing shown, no state kept.
    auto unknown=t.client.counters().unknown;
    t.frame({t.entry(99,1,t.server(),walker(0))});
    CHECK(t.client.counters().unknown==unknown+1&&!t.client.render(99,t.now).visible&&!t.client.live(99,t.now));
    t.share(99,StateKind::Name,1,1,{1,0,0,0},{1,2,3});CHECK(t.client.state(99,StateKind::Name)==nullptr&&t.client.counters().unknown==unknown+2);
    // Several sources in one FRAME, each to its own.
    uint64_t s1=t.server();
    t.frame({t.entry(3,1,s1,walker(-20)),t.entry(2,20,s1,walker(11))});
    auto b3=t.client.counters().accepted;CHECK(b3==accepted+2);
    // Impossible motion: refused; a second of it (by the source's stamps) is a fresh baseline.
    t.now+=50;t.frame({t.entry(2,21,t.server(),walker(500))});CHECK(t.client.counters().accepted==b3);
    for(uint32_t i=0;i<25;++i){t.now+=50;t.frame({t.entry(2,22+i,t.server(),walker(500+0.05*i))});}
    CHECK(t.client.counters().rebased==1&&t.client.counters().accepted>b3);
    t.now+=500;r=t.client.render(2,t.now);CHECK(r.visible&&r.state.position[0]>499);
    // Teleports: one taken, the next within five seconds refused, the one after taken.
    auto jump=[&](double x,uint32_t& seq){ds::State s=walker(x);s.flags|=Teleport;t.frame({t.entry(3,seq++,t.server(),s)});};
    uint32_t seq=2;accepted=t.client.counters().accepted;
    t.now+=100;jump(1000,seq);CHECK(t.client.counters().accepted==accepted+1);
    t.now+=1000;jump(2000,seq);CHECK(t.client.counters().accepted==accepted+1);
    t.now+=4500;jump(3000,seq);CHECK(t.client.counters().accepted==accepted+2);
    // A source that leaves is forgotten; its entries are strangers' from then on.
    t.roster({{false,3,""}});unknown=t.client.counters().unknown;t.frame({t.entry(3,seq++,t.server(),walker(3000))});
    CHECK(t.client.counters().unknown==unknown+1&&!t.client.render(3,t.now).visible);
    // Live: 3 s after the last presence it is not.
    t.now+=3500;CHECK(!t.client.live(2,t.now));
}

static void state(){
    Bench t;t.online();t.roster({{true,2,"Ann"}});
    // A SHARE complete in itself: taken, generation known, no FETCH.
    t.share(2,StateKind::Name,1,1,{1,0,0,0},{'A','n','n'});
    uint64_t revision=0;auto* name=t.client.state(2,StateKind::Name,&revision);
    CHECK(name&&*name==std::vector<uint8_t>({'A','n','n'})&&t.client.generation(2)==1&&revision==1);
    t.frame({t.entry(2,1,t.server(),walker(0),1)});
    for(int i=0;i<10;++i){t.now+=100;t.client.tick(t.now);}
    CHECK(t.take(Type::Fetch).empty());
    // An entry with a generation this side does not have: after the grace, one FETCH every half second until settled.
    t.frame({t.entry(2,2,t.server(),walker(0),3)});
    t.now+=100;t.client.tick(t.now);CHECK(t.take(Type::Fetch).empty());   // a SHARE may be on its way
    t.now+=250;t.client.tick(t.now);auto fetches=t.take(Type::Fetch);CHECK(fetches.size()==1&&fetches[0].fetch.source==2&&fetches[0].fetch.player==1);
    t.now+=100;t.client.tick(t.now);CHECK(t.take(Type::Fetch).size()==1);
    t.now+=450;t.client.tick(t.now);CHECK(t.take(Type::Fetch).size()==2);
    // The answer: every kind, an empty one among them.
    t.share(2,StateKind::Name,1,3,{1,2,0,0},{'A','n','n'});t.share(2,StateKind::Gear,2,3,{1,2,0,0},{9,9});
    t.share(2,StateKind::Carry,0,3,{1,2,0,0},{});t.share(2,StateKind::Places,0,3,{1,2,0,0},{});
    CHECK(t.client.generation(2)==3&&t.client.state(2,StateKind::Gear)&&t.client.state(2,StateKind::Carry)==nullptr);
    t.client.state(2,StateKind::Name,&revision);CHECK(revision==1);   // the same bytes: the same revision
    auto sent=t.take(Type::Fetch).size();for(int i=0;i<10;++i){t.now+=100;t.client.tick(t.now);}CHECK(t.take(Type::Fetch).size()==sent);
    // An older SHARE that arrives late changes nothing.
    t.share(2,StateKind::Gear,4,6,{1,4,0,0},{4,4});t.share(2,StateKind::Gear,3,5,{1,3,0,0},{3,3});
    CHECK(*t.client.state(2,StateKind::Gear)==std::vector<uint8_t>({4,4})&&t.client.generation(2)==6);
    // Neither it nor an entry of an older generation (one the server had queued before the state changed) is a gap.
    t.frame({t.entry(2,3,t.server(),walker(0),5)});
    sent=t.take(Type::Fetch).size();for(int i=0;i<10;++i){t.now+=100;t.client.tick(t.now);}CHECK(t.take(Type::Fetch).size()==sent);
    // A SHARE older than the generation this side has complete adds nothing, even of a kind it does not hold.
    t.share(2,StateKind::Places,1,4,{1,2,0,1},{7});CHECK(t.client.state(2,StateKind::Places)==nullptr&&t.client.generation(2)==6);
    // A kind that is gone (the player's game started again): dropped.
    t.share(2,StateKind::Gear,0,7,{1,0,0,0},{});CHECK(t.client.state(2,StateKind::Gear)==nullptr&&t.client.generation(2)==7);
    // This porter's own state: a version per change, none for the same bytes, sent at once; again after a WELCOME that
    // shows the server holds an older version.
    CHECK(t.client.setState(StateKind::Name,{'M','e'})&&t.client.versions()[0]==1);
    auto posts=t.take(Type::State);CHECK(posts.size()==1&&posts[0].state.version==1&&posts[0].state.kind==StateKind::Name);
    CHECK(t.client.setState(StateKind::Name,{'M','e'})&&t.client.versions()[0]==1&&t.take(Type::State).size()==1);
    CHECK(t.client.setState(StateKind::Name,{'M','e','!'})&&t.client.versions()[0]==2&&t.take(Type::State).size()==2);
    CHECK(!t.client.setState(StateKind::Gear,{})&&!t.client.setState(StateKind::Gear,std::vector<uint8_t>(StateMax+1,1)));
    t.now+=500;t.client.tick(t.now);t.answer(10,10,{1,0,0,0});posts=t.take(Type::State);
    CHECK(posts.size()==3&&posts.back().state.version==2);
    t.now+=500;t.client.tick(t.now);t.answer(10,10,{2,0,0,0});CHECK(t.take(Type::State).size()==3);
}

static void publishing(){
    Bench t;t.online();
    ds::State me=walker(5);t.client.setLocal(me,true,false,t.now,false);
    // Nobody near: nothing published.
    for(int i=0;i<5;++i){t.now+=50;t.client.setLocal(walker(5+i),true,false,t.now,false);t.client.tick(t.now);}
    CHECK(t.take(Type::Publish).empty());
    t.roster({{true,2,"Ann"}});
    // Moving: at most 20 a second, stamped on the server's clock, the presence the core's encoder gives.
    for(int i=0;i<40;++i){t.now+=10;t.client.setLocal(walker(5+0.01*i),true,false,t.now-3,false);t.client.tick(t.now);}
    auto ups=t.take(Type::Publish);CHECK(ups.size()>=7&&ups.size()<=9);
    for(size_t i=1;i<ups.size();++i)CHECK(later(ups[i].publish.sequence,ups[i-1].publish.sequence)&&ups[i].publish.stamp>ups[i-1].publish.stamp);
    {auto& u=ups.back().publish;CHECK(u.parts==HasPresence&&u.stamp<=t.server()&&u.stamp+60>=t.server()&&u.player==1);
     ds::Bytes b=ds::encode([&]{Packet q;q.kind=ds::Kind::Presence;q.sequence=u.sequence;q.stamp=u.stamp;q.state=walker(5.39);return q;}());
     Packet p;presenceInto(u.presence,b.data());uint32_t h=2166136261u;for(size_t i=0;i<128;++i){h^=(i>=120&&i<124)?0:b[i];h*=16777619u;}for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));
     CHECK(ds::decode(b.data(),b.size(),p)==ds::Error::None&&p.state.position[0]>=5.3&&p.state.position[0]<=5.395&&p.state.flags==Sprint&&p.state.action==3);}
    // Standing still: every 500 ms.
    for(int i=0;i<100;++i){t.now+=10;t.client.setLocal(walker(9),true,false,t.now,false);t.client.tick(t.now);}
    auto still=t.take(Type::Publish).size()-ups.size();CHECK(still>=2&&still<=4);
    // Gated, or no pose: no presence.
    auto before=t.take(Type::Publish).size();
    for(int i=0;i<20;++i){t.now+=50;t.client.setLocal(walker(10+i),true,true,t.now,false);t.client.tick(t.now);}
    for(int i=0;i<20;++i){t.now+=50;t.client.setLocal(walker(10+i),false,false,t.now,false);t.client.tick(t.now);}
    CHECK(t.take(Type::Publish).size()==before);
    // A jump: the teleport flag goes on the next PUBLISH with presence, and only that one.
    t.now+=50;t.client.setLocal(walker(500),true,false,t.now,true);t.client.tick(t.now);
    t.now+=50;t.client.setLocal(walker(500.5),true,false,t.now,false);t.client.tick(t.now);
    ups=t.take(Type::Publish);CHECK(ups.size()==before+2);
    CHECK((presenceFlags(ups[before].publish.presence)&Teleport)!=0&&(presenceFlags(ups[before+1].publish.presence)&Teleport)==0);
    // Animation: offered at most every 50 ms; a sample not yet sent passes its pulses on to the next; sent with the
    // presence of the same tick, or alone.
    Animation a;a.held[0]=1;a.rose[0]=1;Animation b=a;b.held[0]=3;b.rose[0]=2;
    t.now+=50;CHECK(t.client.offerAnimation(a,t.now));CHECK(!t.client.offerAnimation(b,t.now+10));
    CHECK(t.client.offerAnimation(b,t.now+60));t.now+=60;
    t.client.tick(t.now);ups=t.take(Type::Publish);
    {auto& u=ups.back().publish;Animation got;CHECK((u.parts&HasAnimation)&&decodeAnimationPayload(u.animation,u.animationSize,got)&&got.held[0]==3&&got.rose[0]==3);}
    t.now+=20;CHECK(!t.client.offerAnimation(b,t.now));t.now+=500;CHECK(t.client.offerAnimation(b,t.now));t.client.tick(t.now);
    ups=t.take(Type::Publish);CHECK(ups.back().publish.parts&HasAnimation);
    // Nothing offered while gated or alone.
    t.client.setLocal(walker(500.5),true,true,t.now,false);t.now+=600;CHECK(!t.client.offerAnimation(a,t.now));
    t.client.setLocal(walker(500.5),true,false,t.now,false);t.roster({{false,2,""}});t.now+=600;CHECK(!t.client.offerAnimation(a,t.now));
    // BYE: once, and everyone is gone.
    t.client.bye();CHECK(t.take(Type::Bye).size()==1&&!t.client.online());
}

// Interpolation on the server's clock: a player walking a circle, sampled every 50 ms; the server sends every sample
// (near) or one in 200 ms (far); the link delays each FRAME by 20..120 ms (so they cross), loses 10 % and repeats 5 %.
// The shown pose is compared with the true one at the shown time (the server's now minus that player's delay).
struct Result {double p95=0,p99=0,max=0,late=0;size_t samples=0;};
static Result interpolate(uint64_t seed,uint32_t intervalMs,unsigned lossPercent){
    std::mt19937_64 rng(seed);Bench t;t.offset=int64_t(rng()%2000000)+1000;t.online();t.roster({{true,2,"Ann"}});
    const double radius=15,speed=3;auto truth=[&](double ms){double a=speed*ms/1000/radius;ds::State s;s.position={radius*std::cos(a),0,radius*std::sin(a)};
        s.velocity={-speed*std::sin(a),0,speed*std::cos(a)};s.yaw=0;return s;};
    uint64_t start=t.server();std::vector<std::pair<uint64_t,Buffer>> link;uint32_t seq=0;uint64_t lastSent=0;
    std::vector<double> errors;size_t late=0;
    for(uint64_t ms=0;ms<12000;ms+=10){
        t.now+=10;uint64_t server=t.server();
        if(ms%50==0){
            ++seq;uint64_t stamp=start+ms;
            if(!lastSent||stamp>=lastSent+intervalMs){lastSent=stamp;
                ds::State s=truth(double(ms));Buffer b;beginFrame(b);appendEntry(b,t.entry(2,seq,stamp,s));finishFrame(b,server);
                if(rng()%100>=lossPercent){uint64_t due=t.now+20+rng()%101;link.push_back({due,b});if(rng()%100<5)link.push_back({due+rng()%30,b});}}
        }
        for(size_t i=0;i<link.size();)if(link[i].first<=t.now){t.client.receive(link[i].second.data(),link[i].second.size(),t.now);link.erase(link.begin()+long(i));}else ++i;
        if(t.now%500==0){t.client.tick(t.now);t.answer(0,0,{},0,false);}
        if(ms<2000)continue;   // the delay settles
        auto r=t.client.render(2,t.now);if(!r.visible)continue;
        double shown=double(t.client.serverNow(t.now))-double(t.client.presentationDelayMs(2))-double(start);
        auto want=truth(shown);double e=std::hypot(r.state.position[0]-want.position[0],r.state.position[2]-want.position[2]);
        errors.push_back(e);late+=r.late;}
    Result out;if(errors.empty())return out;std::sort(errors.begin(),errors.end());
    out.samples=errors.size();out.p95=errors[(errors.size()-1)*95/100];out.p99=errors[(errors.size()-1)*99/100];out.max=errors.back();out.late=double(late)/double(errors.size());return out;
}
static void interpolation(){
    // At the full rate a lost sample is bridged by the next; one in 200 ms bridges nothing, so a lost one freezes the
    // pose for a moment (the core's extrapolation cap): that is the price of the server's interval for far players.
    struct Case {uint32_t interval;unsigned loss;double p95,p99,late;};
    for(auto c:{Case{0,10,0.02,0.02,0.02},Case{200,0,0.02,0.02,0.01},Case{200,3,0.05,0.3,0.10},Case{200,10,0.2,1.2,0.16}}){
        std::vector<double> p95s,p99s;double worstLate=0,worstMax=0;size_t samples=0;
        for(uint64_t seed=1;seed<=40;++seed){auto r=interpolate(seed,c.interval,c.loss);p95s.push_back(r.p95);p99s.push_back(r.p99);worstLate=std::max(worstLate,r.late);worstMax=std::max(worstMax,r.max);samples+=r.samples;}
        double p95=*std::max_element(p95s.begin(),p95s.end()),p99=*std::max_element(p99s.begin(),p99s.end());
        std::printf("broadcast interpolation, %s, %u %% lost: %zu poses, worst p95 %.3f m, p99 %.3f m, max %.3f m, shown past the newest sample %.1f %%\n",
                    c.interval?"one sample in 200 ms":"every sample",c.loss,samples,p95,p99,worstMax,100*worstLate);
        CHECK(samples>40*900);CHECK(p95<c.p95);CHECK(p99<c.p99);CHECK(worstLate<c.late);
    }
}

// The fight channel on the player's side: the owner keeps its fight and posts; a visitor joins until answered, posts
// only once joined, and takes the state of its own fight from its owner only, each version once; offline ends a visit.
static void fights(){
    FightInfo info;info.fight=21;info.kind=1;info.most=9;info.radius=300;info.place={10,0,10};
    auto status=[](Bench& t,uint64_t owner,uint32_t fight,FightStatus s,uint8_t visitors=0){Status x;x.owner=owner;x.fight=fight;x.status=s;x.visitors=visitors;auto b=encode(x);t.client.receive(b.data(),b.size(),t.now);};
    auto deliver=[](Bench& t,uint64_t source,uint64_t owner,uint32_t fight,uint32_t version,std::vector<uint8_t> bytes){
        Post p;p.player=source;p.owner=owner;p.fight=fight;p.version=version;p.payload=bytes.data();p.size=bytes.size();auto b=encodeDeliver(p);t.client.receive(b.data(),b.size(),t.now);};
    {   // The owner.
        Bench t;t.online();CHECK(!t.client.postFight({1}));
        t.client.hostFight(info,t.now);auto f=t.take(Type::Fight);CHECK(f.size()==1&&f[0].fight.op==FightOp::Open&&f[0].fight.info.fight==21&&f[0].fight.player==1);
        for(int i=0;i<25;++i){t.now+=100;t.client.tick(t.now);}
        CHECK(t.take(Type::Fight).size()==3&&!t.client.fightsHere());
        status(t,1,21,FightStatus::Open,2);CHECK(t.client.fightsHere()&&t.client.visitors()==2&&t.client.hosting()==21);
        CHECK(t.client.postFight({1,2,3})&&t.client.postFight({4}));auto p=t.take(Type::Post);
        CHECK(p.size()==2&&p[0].post.owner==1&&p[0].post.fight==21&&p[0].post.version==1&&p[1].post.version==2);
        // Reports from a visitor, each version once; nothing of another fight, of this player himself, or for another owner.
        deliver(t,7,1,21,1,{9});deliver(t,7,1,21,1,{9});deliver(t,7,1,21,2,{8});deliver(t,8,1,21,1,{5});
        deliver(t,7,1,22,3,{9});deliver(t,1,1,21,9,{9});deliver(t,7,5,21,4,{9});
        auto news=t.client.takeFightNews();size_t taken=0;for(auto& n:news)taken+=n.delivered;
        CHECK(taken==3&&t.client.counters().fightRefused==4);
        // Joining while hosting: refused; closing: one FIGHT close, nothing posts any more.
        CHECK(!t.client.joinFight(9,1,t.now));t.client.closeFight();f=t.take(Type::Fight);CHECK(f.back().fight.op==FightOp::Close&&!t.client.hosting()&&!t.client.postFight({1}));
    }
    {   // A visitor.
        Bench t;t.online();
        {Offer o;o.owner=5;o.visitors=1;o.info=info;auto b=encode(o);t.client.receive(b.data(),b.size(),t.now);}
        CHECK(t.client.fightOffers(t.now).size()==1&&t.client.fightOffers(t.now)[0].owner==5);
        CHECK(t.client.fightOffers(t.now+FightOfferMs+1).empty());
        CHECK(t.client.joinFight(5,21,t.now)&&!t.client.joined()&&!t.client.postFight({1}));
        for(int i=0;i<12;++i){t.now+=100;t.client.tick(t.now);}
        auto j=t.take(Type::Join);CHECK(j.size()==3&&j[0].join.owner==5&&j[0].join.fight==21&&j[0].join.op==JoinOp::Join);
        status(t,5,22,FightStatus::Joined);CHECK(!t.client.joined());   // another fight's answer
        status(t,5,21,FightStatus::Joined);CHECK(t.client.joined()&&t.client.visitingOwner()==5);
        for(int i=0;i<12;++i){t.now+=100;t.client.tick(t.now);}CHECK(t.take(Type::Join).size()==3);   // answered: no more
        CHECK(t.client.postFight({6,6}));auto p=t.take(Type::Post);CHECK(p.size()==1&&p[0].post.owner==5&&p[0].post.fight==21&&p[0].post.player==1);
        // The owner's state: taken in order; another visitor's report, another fight's state and an older version: not.
        deliver(t,5,5,21,1,{1});deliver(t,6,5,21,2,{2});deliver(t,5,5,22,2,{2});deliver(t,5,5,21,1,{1});deliver(t,5,5,21,3,{3});
        auto news=t.client.takeFightNews();std::vector<uint32_t> versions;for(auto& n:news)if(n.delivered){versions.push_back(n.version);CHECK(n.source==5);}
        CHECK((versions==std::vector<uint32_t>{1,3}));
        // The owner closes: the visit ends; nothing more is taken from it.
        status(t,5,21,FightStatus::Closed);news=t.client.takeFightNews();CHECK(news.size()==1&&news[0].status.status==FightStatus::Closed&&!t.client.visitingOwner());
        deliver(t,5,5,21,4,{4});CHECK(t.client.takeFightNews().empty());
        // Refused (full): the visit ends too. Joined again, then silence: offline ends it as Gone.
        t.client.joinFight(5,23,t.now);status(t,5,23,FightStatus::Full);CHECK(!t.client.visitingOwner()&&t.client.takeFightNews().back().status.status==FightStatus::Full);
        t.client.joinFight(5,23,t.now);status(t,5,23,FightStatus::Joined);t.client.takeFightNews();
        // The post versions go on from the last visit's: the server and the owner take a source's versions only forward.
        CHECK(t.client.postFight({7}));p=t.take(Type::Post);CHECK(p.size()==2&&p[1].post.version==2&&p[1].post.fight==23);
        for(int i=0;i<12;++i){t.now+=500;t.client.tick(t.now);}
        news=t.client.takeFightNews();CHECK(!t.client.online()&&news.size()==1&&news[0].status.status==FightStatus::Gone&&!t.client.joined());
    }
}

// Bug marks (F2): MARK and MARKED byte for byte and refused when bad; the player's side sends at most one every 2 s and
// 30 an hour, nothing offline, and takes a passed-on mark only from a player in the roster, with the roster's name.
static void marks(){
    {   Mark k;k.player=0x1122334455667788ull;k.mark=0x5a100003;
        for(auto b:{encodeMark(k),encodeMarked(k)}){
            CHECK(b.size()==MarkSize&&MarkSize==24&&!std::memcmp(b.data(),"DSR1",4)&&b[4]==2&&get(b,8,8)==k.player&&get(b,16,4)==k.mark&&get(b,20,4)==0);
            Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.mark.player==k.player&&m.mark.mark==k.mark);
            auto v=b;v[16]=v[17]=v[18]=v[19]=0;CHECK(parseBuffer(v)==WireError::Range);    // mark 0
            v=b;for(size_t i=8;i<16;++i)v[i]=0;CHECK(parseBuffer(v)==WireError::Range);      // player 0
            v=b;v[22]=1;CHECK(parseBuffer(v)==WireError::Reserved);
            v=b;v.push_back(0);CHECK(parseBuffer(v)==WireError::Length);}
        CHECK(encodeMark(k)[6]==uint8_t(Type::Mark)&&encodeMarked(k)[6]==uint8_t(Type::Marked)&&uint8_t(Type::Mark)==17&&uint8_t(Type::Marked)==18);
        k.mark=0;CHECK(throws([&]{encodeMark(k);}));k.mark=1;k.player=0;CHECK(throws([&]{encodeMarked(k);}));}
    {   // Sending: offline nothing; online one, then none within 2 s; 30 in an hour; the hour over, again.
        Bench t;CHECK(!t.client.mark(7,t.now)&&t.take(Type::Mark).empty());
        t.online();CHECK(t.client.mark(7,t.now));auto sent=t.take(Type::Mark);CHECK(sent.size()==1&&sent[0].mark.player==1&&sent[0].mark.mark==7);
        CHECK(!t.client.mark(8,t.now+1999)&&t.take(Type::Mark).size()==1);
        uint64_t first=t.now;unsigned taken=1;
        for(unsigned i=1;i<40;++i){t.now=first+2000*i;t.client.tick(t.now);t.answer(1,1,{},0,false);taken+=t.client.mark(100+i,t.now);}
        CHECK(taken==MarksPerHour&&t.take(Type::Mark).size()==MarksPerHour&&t.client.counters().marksHeld>=10);
        t.now=first+MarkHourMs;t.client.tick(t.now);t.answer(1,1,{},0,false);CHECK(t.client.online()&&t.client.mark(200,t.now));}
    {   // Receiving: from a player in the roster with his name; from a stranger, or this player himself, nothing.
        Bench t;t.online();t.roster({{true,2,"Ann"}});
        auto marked=[&](uint64_t source,uint32_t mark){Mark k;k.player=source;k.mark=mark;auto b=encodeMarked(k);t.client.receive(b.data(),b.size(),t.now);};
        marked(2,11);marked(9,12);marked(1,13);
        auto got=t.client.takeMarks();CHECK(got.size()==1&&got[0].source==2&&got[0].name=="Ann"&&got[0].mark==11);
        CHECK(t.client.counters().marked==1&&t.client.counters().markRefused==2&&t.client.takeMarks().empty());
        // A MARK (the server's type) sent to a player is not his.
        {Mark k;k.player=2;k.mark=14;auto b=encodeMark(k);t.client.receive(b.data(),b.size(),t.now);}CHECK(t.client.takeMarks().empty());
        // Kept for the adapter at most MarkNewsMax.
        for(unsigned i=0;i<MarkNewsMax+10;++i){marked(2,100+i);}
        CHECK(t.client.takeMarks().size()==MarkNewsMax);}
}

// Pings and likes: SIGNAL and SIGNALLED byte for byte and refused when bad; the player's side sends at most one every
// 500 ms and 20 a minute, nothing offline or invalid, and takes a passed-on signal only from a player in the roster, a
// like only when it names this player.
static void signalsTest(){
    {   Signal g;g.player=0x1122334455667788ull;g.kind=SignalKind::Ping;g.serial=0x01020304;g.place={-1234.56,78.9,99999.99};
        for(auto b:{encodeSignal(g),encodeSignalled(g)}){
            CHECK(b.size()==SignalSize&&SignalSize==44&&b[4]==2&&get(b,8,8)==g.player&&b[16]==1&&get(b,17,3)==0&&get(b,20,4)==g.serial&&get(b,24,8)==0);
            CHECK(int32_t(uint32_t(get(b,32,4)))==-123456&&int32_t(uint32_t(get(b,36,4)))==7890&&int32_t(uint32_t(get(b,40,4)))==9999999);
            Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.signal.player==g.player&&m.signal.kind==SignalKind::Ping&&m.signal.serial==g.serial&&m.signal.target==0);
            CHECK(std::fabs(m.signal.place[0]+1234.56)<1e-6&&std::fabs(m.signal.place[2]-99999.99)<1e-6);
            auto v=b;v[20]=v[21]=v[22]=v[23]=0;CHECK(parseBuffer(v)==WireError::Range);      // serial 0
            v=b;for(size_t i=8;i<16;++i)v[i]=0;CHECK(parseBuffer(v)==WireError::Range);       // player 0
            v=b;v[16]=0;CHECK(parseBuffer(v)==WireError::Range);v[16]=3;CHECK(parseBuffer(v)==WireError::Range);   // kind
            v=b;v[24]=9;CHECK(parseBuffer(v)==WireError::Range);                              // a ping names nobody
            v=b;v[18]=1;CHECK(parseBuffer(v)==WireError::Reserved);
            v=b;v[35]=0x7f;CHECK(parseBuffer(v)==WireError::Range);                           // a place beyond 100 km
            v=b;v.push_back(0);CHECK(parseBuffer(v)==WireError::Length);}
        g.kind=SignalKind::Like;g.target=0x99;
        {auto b=encodeSignal(g);Message m;CHECK(parseBuffer(b,m)==WireError::None&&m.signal.kind==SignalKind::Like&&m.signal.target==0x99);
         auto v=b;for(size_t i=24;i<32;++i)v[i]=0;CHECK(parseBuffer(v)==WireError::Range);       // a like names somebody
         v=b;for(size_t i=0;i<8;++i)v[24+i]=v[8+i];CHECK(parseBuffer(v)==WireError::Range);}     // not the sender himself
        CHECK(encodeSignal(g)[6]==uint8_t(Type::Signal)&&encodeSignalled(g)[6]==uint8_t(Type::Signalled)&&uint8_t(Type::Signal)==19&&uint8_t(Type::Signalled)==20);
        g.serial=0;CHECK(throws([&]{encodeSignal(g);}));g.serial=1;g.target=0;CHECK(throws([&]{encodeSignal(g);}));
        g.kind=SignalKind::Ping;g.target=4;CHECK(throws([&]{encodeSignal(g);}));g.target=0;g.place[1]=2e5;CHECK(throws([&]{encodeSignal(g);}));}
    {   // Sending: offline nothing; online one, then none within 500 ms; 20 in a minute; the minute over, again.
        Bench t;CHECK(!t.client.signal(SignalKind::Ping,1,0,{1,2,3},t.now)&&t.take(Type::Signal).empty());
        t.online();CHECK(!t.client.signal(SignalKind::Like,1,1,{1,2,3},t.now));   // a like for himself is no signal
        CHECK(!t.client.signal(SignalKind::Ping,1,7,{1,2,3},t.now)&&t.take(Type::Signal).empty());
        CHECK(t.client.signal(SignalKind::Ping,1,0,{1,2,3},t.now));
        auto sent=t.take(Type::Signal);CHECK(sent.size()==1&&sent[0].signal.player==1&&sent[0].signal.serial==1&&sent[0].signal.place[2]==3);
        CHECK(!t.client.signal(SignalKind::Like,2,9,{1,2,3},t.now+499)&&t.take(Type::Signal).size()==1);
        uint64_t first=t.now;unsigned taken=1;
        for(unsigned i=1;i<30;++i){t.now=first+500*i;t.client.tick(t.now);t.answer(1,1,{},0,false);taken+=t.client.signal(SignalKind::Like,10+i,9,{0,0,0},t.now);}
        CHECK(taken==SignalsPerMinute&&t.take(Type::Signal).size()==SignalsPerMinute&&t.client.counters().signalsHeld>=10);
        t.now=first+SignalMinuteMs;t.client.tick(t.now);t.answer(1,1,{},0,false);CHECK(t.client.online()&&t.client.signal(SignalKind::Ping,99,0,{0,0,0},t.now));}
    {   // Receiving: from a player in the roster with his name; a like only for this player; strangers and himself nothing.
        Bench t;t.online();t.roster({{true,2,"Ann"}});
        auto signalled=[&](uint64_t source,SignalKind kind,uint64_t target,uint32_t serial){Signal k;k.player=source;k.kind=kind;k.target=target;k.serial=serial;k.place={5,6,7};
            auto b=encodeSignalled(k);t.client.receive(b.data(),b.size(),t.now);};
        signalled(2,SignalKind::Ping,0,11);signalled(9,SignalKind::Ping,0,12);signalled(1,SignalKind::Ping,0,13);
        signalled(2,SignalKind::Like,1,14);signalled(2,SignalKind::Like,3,15);
        auto got=t.client.takeSignals();
        CHECK(got.size()==2&&got[0].source==2&&got[0].name=="Ann"&&got[0].kind==SignalKind::Ping&&got[0].serial==11&&got[0].place[1]==6);
        CHECK(got[1].kind==SignalKind::Like&&got[1].serial==14&&t.client.counters().signalled==2&&t.client.counters().signalRefused==3);
        // A SIGNAL (the server's type) sent to a player is not his.
        {Signal k;k.player=2;k.serial=16;auto b=encodeSignal(k);t.client.receive(b.data(),b.size(),t.now);}CHECK(t.client.takeSignals().empty());
        for(unsigned i=0;i<SignalNewsMax+10;++i)signalled(2,SignalKind::Ping,0,100+i);
        CHECK(t.client.takeSignals().size()==SignalNewsMax);}
}

int main(){
    wire();clockAndRoster();entries();state();publishing();interpolation();fights();marks();signalsTest();
    if(failures)return 1;
    std::puts("broadcast tests passed");return 0;
}
