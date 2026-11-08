#include "../core.h"
#include "coopfuzz/net.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace ds;
namespace {
Protection protection;unsigned failures=0;
void check(bool ok,const char* name){if(!ok){std::fprintf(stderr,"FAIL %s\n",name);++failures;}}
struct Fake : StateProvider {
    bool readLocal(State& s) override {s={};return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const Render&) override {}
};
struct Queue : Transport {std::vector<Bytes> out;void send(const Bytes& b) override {out.push_back(b);}};
struct Pair {
    Fake a,b;Queue at,bt;Core host{{Role::Host,42,100,120},a,at},guest{{Role::Client,42,200,120},b,bt};
    explicit Pair(bool clock=true){host.setProtections(protection);guest.setProtections(protection);pump(0);if(clock)pump(10);at.out.clear();bt.out.clear();}
    void drain(uint64_t now) {
        for(unsigned i=0;i<8&&(!at.out.empty()||!bt.out.empty());++i){
            auto as=std::move(at.out);at.out.clear();for(auto& p:as)guest.receive(p.data(),p.size(),now);
            auto bs=std::move(bt.out);bt.out.clear();for(auto& p:bs)host.receive(p.data(),p.size(),now);
        }
    }
    void pump(uint64_t now){host.tick(now);guest.tick(now);drain(now);}
    Error receive(Packet p,uint64_t now=20){auto wire=encode(p);return guest.receive(wire.data(),wire.size(),now);}
    Packet sample(uint32_t sequence=1,uint64_t stamp=20){Packet p;p.kind=Kind::Environment;p.role=Role::Host;p.session=42;p.nonce=100;p.echo=200;p.sequence=sequence;p.stamp=stamp;p.t0=43200000;return p;}
};
void parser() {
    Packet p;p.kind=Kind::Environment;p.t0=EnvironmentDayMs-1;p.sequence=1;Packet q;
    auto bytes=encode(p);check(decode(bytes.data(),bytes.size(),q)==Error::None&&q.t0==EnvironmentDayMs-1&&q.kind==Kind::Environment,"environment_roundtrip");
    p.t0=EnvironmentDayMs;bytes=encode(p);check(decodeForTest(bytes.data(),bytes.size(),q,protection)==Error::Range,"environmentRange");
    p.t0=0;
    {   // rain rides in t1: bit 32 + int16 thousandths; other t1 bits are reserved; |rain| <= 2.5
        Packet r=p;r.t1=(uint64_t(1)<<32)|uint16_t(int16_t(-1750));auto b=encode(r);
        check(decodeForTest(b.data(),b.size(),q,protection)==Error::None,"environment_rain_roundtrip");
        r.t1=(uint64_t(1)<<32)|uint16_t(int16_t(-2600));b=encode(r);check(decodeForTest(b.data(),b.size(),q,protection)==Error::Range,"environmentRange");
        r.t1=(uint64_t(1)<<33)|5;b=encode(r);check(decodeForTest(b.data(),b.size(),q,protection)==Error::Reserved,"reserved");
        Pair pair;Environment w{43200000,true,-1.75f};check(pair.host.sendEnvironment(w,20),"environment_rain_send");pair.drain(20);
        auto got=pair.guest.latestEnvironment(20);check(got&&got->haveRain&&std::abs(got->rain+1.75f)<0.001f,"environment_rain_delivered");
        Pair dry;check(dry.host.sendEnvironment({43200000},20),"environment_no_rain_send");dry.drain(20);
        got=dry.guest.latestEnvironment(20);check(got&&!got->haveRain,"environment_no_rain");
        Pair bad;check(!bad.host.sendEnvironment({43200000,true,9.f},20),"environment_rain_refused");}
    for(unsigned field=0;field<9;++field){Packet bad=p;if(field==0)bad.t1=1;if(field==1)bad.t2=1;if(field==2)bad.state.position[0]=1;if(field==3)bad.state.flags=1;
        if(field==4)bad.state.action=1;
        if(field==5)bad.state.velocity[2]=1;
        if(field==6)bad.state.yaw=1;
        if(field==7)bad.state.health=0;
        if(field==8)bad.byeReason=1;
        bytes=encode(bad);check(decode(bytes.data(),bytes.size(),q)==Error::Reserved,"environment_reserved");}
}
void sessions() {
    {Pair f(false);check(f.receive(f.sample())==Error::Time,"environmentFresh");}
    {Pair f;check(f.host.connected()&&f.guest.connected(),"environment_pair");check(f.host.sendEnvironment({45000000},20),"environment_send");f.drain(20);
        auto value=f.guest.latestEnvironment(20);check(value&&value->timeOfDayMs==45000000,"environment_delivery");
        check(f.guest.latestEnvironment(2020).has_value()&&!f.guest.latestEnvironment(2021)&&!f.guest.latestEnvironment(19),"environment_expiry");
        check(!f.guest.sendEnvironment({0},20),"environment_guest_cannot_send");
        check(!f.host.sendEnvironment({EnvironmentDayMs},20),"environment_send_range");
        check(!f.host.sendEnvironment({0},30019),"environmentRate");
        check(f.host.sendEnvironment({0},30020),"environment_rate_boundary");}
    {Pair f;auto p=f.sample();check(f.receive(p)==Error::None,"environment_initial");check(f.receive(p)==Error::Duplicate,"environmentOrder");
        p.sequence=2;p.stamp=19;check(f.receive(p)==Error::Duplicate,"environmentOrder");
        p.sequence=0;p.stamp=21;check(f.receive(p)==Error::Duplicate,"environmentOrder");}
    {Pair f;auto p=f.sample();p.stamp=2021;check(f.receive(p)==Error::Time,"environmentFresh");}
    {Pair f;auto p=f.sample();p.stamp=20;check(f.receive(p,2021)==Error::Time,"environmentFresh");}
    {Pair f;auto p=f.sample();auto policy=protection;policy.role=false;f.guest.setProtections(policy);p.role=Role::Client;check(f.receive(p)==Error::Role,"environmentRole");}
    {Pair f;auto p=f.sample();p.echo=201;check(f.receive(p)==Error::Nonce,"environment_echo");p.echo=200;p.nonce=101;check(f.receive(p)==Error::Nonce,"environment_nonce");}
    {Pair f;check(f.receive(f.sample())==Error::None,"environment_before_reset");f.guest.disconnect(21);check(!f.guest.latestEnvironment(21),"environment_reset");auto p=f.sample();p.echo=201;check(f.receive(p,22)==Error::NotConnected,"environment_before_handshake");}
    {Pair f;check(f.host.sendEnvironment({0},20),"environment_first_rate");f.host.disconnect(21);f.guest.disconnect(21);f.at.out.clear();f.bt.out.clear();f.pump(22);f.pump(32);
        check(f.host.connected()&&f.guest.connected(),"environment_reconnect");check(!f.host.sendEnvironment({0},32),"environmentRate");}
}
struct Network : Transport {
    coopfuzz::LossyLink& link;int side;unsigned envSends=0;
    Network(coopfuzz::LossyLink& l,int s):link(l),side(s){}
    void send(const Bytes& b) override {if(b[6]==uint8_t(Kind::Environment))++envSends;link.send(side,1-side,{b.begin(),b.end()});}
};
void fuzz() {
    for(uint64_t seed=1;seed<=200;++seed){
        coopfuzz::LossyLink link(2,seed);link.setLink({20,150,.10,.10,0});Fake a,b;Network at(link,0),bt(link,1);
        Core host({Role::Host,seed,100,120},a,at),guest({Role::Client,seed,200,120},b,bt);
        const uint64_t clocks[2]={10000,20000+seed*7};unsigned delivered=0;
        uint32_t expected=uint32_t(seed*31111%EnvironmentDayMs);
        for(uint64_t now=0;now<=95000;now+=50){
            if(now==40000)link.setConnected(1,false);
            if(now==45000)link.setConnected(1,true);
            if(now==85000)link.setLink({20,150,0,0,0}); // bounded quiet period guarantees a final sample opportunity
            link.advance(now);int from;coopfuzz::Bytes bytes;
            for(int side=0;side<2;++side)while(link.poll(side,from,bytes))(side?guest:host).receive(bytes.data(),bytes.size(),now+clocks[side]);
            host.tick(now+clocks[0]);guest.tick(now+clocks[1]);host.sendEnvironment({expected},now+clocks[0]);
            if(auto got=guest.latestEnvironment(now+clocks[1])){check(got->timeOfDayMs==expected,"environment_fuzz_value");++delivered;}
            check(!host.latestEnvironment(now+clocks[0]),"environment_fuzz_authority");
            // Structured payload corruption reaches the new parser with a recomputed checksum.
            Packet p;p.kind=Kind::Environment;p.t0=(seed*now*1009)%(EnvironmentDayMs*2ULL);
            auto raw=encode(p);Packet decoded;
            check(decode(raw.data(),raw.size(),decoded)==(p.t0<EnvironmentDayMs?Error::None:Error::Range),"environment_fuzz_range");
        }
        check(delivered>0,"environment_fuzz_delivery");check(at.envSends>=3&&at.envSends<=4&&bt.envSends==0,"environment_fuzz_rate");
        check(host.connected()&&guest.connected(),"environment_fuzz_reconnect");
    }
    std::puts("environment fuzz: 200 seeds, 95 s simulated, 10% loss/duplicates, outage/reconnect, independent clocks");
}
}
int main(int argc,char** argv){
    std::string mutant=argc>1?argv[1]:"";
#define MUTATE(name) if(mutant==#name)protection.name=false
    MUTATE(environmentRange);MUTATE(environmentRole);MUTATE(environmentOrder);MUTATE(environmentFresh);MUTATE(environmentRate);
#undef MUTATE
    parser();sessions();if(mutant.empty()&&!failures)fuzz();
    std::printf("environment core failures=%u\n",failures);return failures?1:0;
}
