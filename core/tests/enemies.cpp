// The optional ENEMIES sample (host -> guest, unreliable): parser, session rules, each guard with its mutant, and a
// lossy-link fuzz with loss, duplicates, reordering, an outage and independent clocks.
#include "../core.h"
#include "coopfuzz/net.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
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
struct Queue : Transport {std::vector<std::vector<uint8_t>> out;void send(const Bytes& b) override {out.emplace_back(b.begin(),b.end());}
    void sendLong(const std::vector<uint8_t>& b) override {out.push_back(b);}};
EnemyPoses poses(unsigned n,double x=10){EnemyPoses e;e.count=uint8_t(n);for(unsigned i=0;i<n;++i)e.poses[i]={uint64_t(100+i),{x+i,-2000.25,300.5},-1.5+0.1*i,uint8_t(i&3)};return e;}
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
    Packet sample(uint32_t sequence=1,uint64_t stamp=20){Packet p;p.kind=Kind::Enemies;p.role=Role::Host;p.session=42;p.nonce=100;p.echo=200;p.sequence=sequence;p.stamp=stamp;p.enemies=poses(3);return p;}
    Error receive(const Packet& p,uint64_t now=20){auto wire=encodeEnemies(p);return guest.receive(wire.data(),wire.size(),now);}
};
// A sample with one field rewritten after encoding (the checksum recomputed by encoding a legal one is not enough:
// the bytes are patched and the checksum redone here, as a hostile or broken sender would).
uint32_t fnv(const std::vector<uint8_t>& b){uint32_t h=2166136261u;for(size_t i=0;i<b.size();++i){h^=(i>=120&&i<124)?0:b[i];h*=16777619u;}return h;}
std::vector<uint8_t> patched(std::vector<uint8_t> b,size_t at,std::initializer_list<uint8_t> bytes){size_t i=at;for(auto v:bytes)b[i++]=v;uint32_t c=fnv(b);for(int k=0;k<4;++k)b[120+size_t(k)]=uint8_t(c>>(8*k));return b;}
void parser() {
    Packet p;p.kind=Kind::Enemies;p.sequence=1;p.enemies=poses(EnemyPosesMax);Packet q;
    auto wire=encodeEnemies(p);check(wire.size()==WireSize+1+EnemyPoseBytes*EnemyPosesMax,"enemies_size");
    check(decode(wire.data(),wire.size(),q)==Error::None&&q.kind==Kind::Enemies&&q.enemies.count==EnemyPosesMax,"enemies_roundtrip");
    for(size_t i=0;i<q.enemies.count;++i){auto& a=p.enemies.poses[i];auto& b=q.enemies.poses[i];
        check(a.key==b.key&&a.flags==b.flags&&std::abs(a.heading-b.heading)<1e-3,"enemies_roundtrip_fields");
        for(int k=0;k<3;++k)check(std::abs(a.position[size_t(k)]-b.position[size_t(k)])<=0.005+1e-9,"enemies_roundtrip_position");}
    { Packet none=p;none.enemies=poses(0);auto b=encodeEnemies(none);check(b.size()==WireSize+1&&decode(b.data(),b.size(),q)==Error::None&&q.enemies.count==0,"enemies_empty"); }
    // Every field out of its range is refused: key zero, key order, key too wide, a flag bit, a position, the count.
    size_t first=WireSize+1,second=first+EnemyPoseBytes;
    auto refused=[&](std::vector<uint8_t> b,Error want){return decodeForTest(b.data(),b.size(),q,protection)==want;};
    check(refused(patched(wire,first,{0,0,0,0,0,0,0}),Error::Range),"enemiesRange");
    check(refused(patched(wire,second,{100,0,0,0,0,0,0}),Error::Range),"enemiesRange");                    // the same key twice
    check(refused(patched(wire,first+6,{0x10}),Error::Range),"enemiesRange");                             // above 2^52
    check(refused(patched(wire,first+21,{16}),Error::Range),"enemiesRange");
    check(refused(patched(wire,first+7,{0x81,0x96,0x98,0x00}),Error::Range),"enemiesRange");              // 100000.01 m
    check(refused(patched(wire,first+19,{0x00,0x80}),Error::Range),"enemiesRange");
    check(refused(patched(wire,WireSize,{uint8_t(EnemyPosesMax+1)}),Error::Length),"enemies_count");
    check(refused(patched(wire,WireSize,{uint8_t(EnemyPosesMax-1)}),Error::Length),"enemies_count_short");
    { auto b=wire;b.pop_back();b[8]=uint8_t(b.size());b[9]=uint8_t(b.size()>>8);check(refused(patched(b,WireSize,{uint8_t(EnemyPosesMax)}),Error::Length),"enemies_truncated"); }
    check(refused(patched(wire,96,{1}),Error::Reserved),"enemies_reserved");
    // The sender refuses what the wire cannot carry.
    auto throws=[&](EnemyPoses e){Packet x=p;x.enemies=e;try{encodeEnemies(x);}catch(const std::invalid_argument&){return true;}return false;};
    { auto e=poses(2);e.poses[1].key=e.poses[0].key;check(throws(e),"enemies_encode_order"); }
    { auto e=poses(1);e.poses[0].position[2]=std::nan("");check(throws(e),"enemies_encode_nan"); }
    { auto e=poses(1);e.poses[0].position[0]=1e6;check(throws(e),"enemies_encode_far"); }
    { auto e=poses(1);e.poses[0].flags=16;check(throws(e),"enemies_encode_flags"); }
    { auto e=poses(1);e.poses[0].key=EnemyKeyMax+1;check(throws(e),"enemies_encode_key"); }
    { auto e=poses(1);e.count=EnemyPosesMax+1;check(throws(e),"enemies_encode_count"); }
}
void sessions() {
    {Pair f(false);check(f.receive(f.sample())==Error::Time,"enemiesFresh");}
    {Pair f;check(f.host.connected()&&f.guest.connected(),"enemies_pair");check(f.host.sendEnemies(poses(4),20),"enemies_send");f.drain(20);
        auto got=f.guest.latestEnemies(20);check(got&&got->enemies.count==4&&got->enemies.poses[3].key==103&&got->sequence==1,"enemies_delivery");
        check(!f.host.latestEnemies(20),"enemies_host_has_none");
        got=f.guest.latestEnemies(520);check(got&&got->ageMs>=490&&got->ageMs<=510,"enemies_age");
        check(f.guest.latestEnemies(20+EnemyStaleMs-10).has_value()&&!f.guest.latestEnemies(20+EnemyStaleMs+10),"enemiesStale");
        check(!f.guest.sendEnemies(poses(1),20),"enemies_guest_cannot_send");
        check(!f.host.sendEnemies(poses(1),20+EnemySendMs-1),"enemiesRate");
        check(f.host.sendEnemies(poses(1),20+EnemySendMs),"enemies_rate_boundary");
        { auto e=poses(2);e.poses[1].key=1;check(!f.host.sendEnemies(e,1000),"enemies_send_refuses_bad"); }}
    {Pair f;auto p=f.sample();check(f.receive(p)==Error::None,"enemies_initial");check(f.receive(p)==Error::Duplicate,"enemiesOrder");
        p.sequence=2;p.stamp=19;check(f.receive(p)==Error::Duplicate,"enemiesOrder");
        p.sequence=0;p.stamp=21;check(f.receive(p)==Error::Duplicate,"enemiesOrder");
        // An older sample arriving after a newer one never replaces it.
        Packet newer=f.sample(5,30);newer.enemies=poses(1,500);check(f.receive(newer,30)==Error::None,"enemies_newer");
        Packet older=f.sample(4,25);check(f.receive(older,31)==Error::Duplicate,"enemiesOrder");
        auto got=f.guest.latestEnemies(31);check(got&&got->enemies.count==1&&got->enemies.poses[0].position[0]==500,"enemiesOrder");}
    {Pair f;auto p=f.sample();p.stamp=2021;check(f.receive(p)==Error::Time,"enemiesFresh");}
    {Pair f;auto p=f.sample();check(f.receive(p,2021)==Error::Time,"enemiesFresh");}
    // A guest's sample is not taken by the host, whatever the generic role check says.
    {Pair f;auto p=f.sample();p.role=Role::Client;p.nonce=200;p.echo=100;auto wire=encodeEnemies(p);
        check(f.host.receive(wire.data(),wire.size(),20)==Error::Role&&!f.host.latestEnemies(20),"enemiesRole");}
    {Pair f;auto p=f.sample();p.echo=201;check(f.receive(p)==Error::Nonce,"enemies_echo");p.echo=200;p.nonce=101;check(f.receive(p)==Error::Nonce,"enemies_nonce");}
    {Pair f;check(f.receive(f.sample())==Error::None,"enemies_before_reset");f.guest.disconnect(21);check(!f.guest.latestEnemies(21),"enemies_reset");
        auto p=f.sample();p.echo=201;check(f.receive(p,22)==Error::NotConnected,"enemies_before_handshake");}
}
struct Network : Transport {
    coopfuzz::LossyLink& link;int side;unsigned enemySends=0;
    Network(coopfuzz::LossyLink& l,int s):link(l),side(s){}
    void send(const Bytes& b) override {link.send(side,1-side,{b.begin(),b.end()});}
    void sendLong(const std::vector<uint8_t>& b) override {if(b[6]==uint8_t(Kind::Enemies))++enemySends;link.send(side,1-side,b);}
};
void fuzz() {
    for(uint64_t seed=1;seed<=200;++seed){
        coopfuzz::LossyLink link(2,seed);link.setLink({20,180,.15,.15,.02});Fake a,b;Network at(link,0),bt(link,1);
        Core host({Role::Host,seed,100,120},a,at),guest({Role::Client,seed,200,120},b,bt);
        const uint64_t clocks[2]={10000,20000+seed*7};unsigned delivered=0;uint32_t lastSequence=0;double lastX=-1;
        for(uint64_t now=0;now<=60000;now+=25){
            if(now==25000)link.setConnected(1,false);
            if(now==30000)link.setConnected(1,true);
            link.advance(now);int from;coopfuzz::Bytes bytes;
            for(int side=0;side<2;++side)while(link.poll(side,from,bytes))(side?guest:host).receive(bytes.data(),bytes.size(),now+clocks[side]);
            host.tick(now+clocks[0]);guest.tick(now+clocks[1]);
            // The pose's x is the host's time: the guest must only ever see it go forward, and never see an old one.
            host.sendEnemies(poses(1+unsigned(seed%EnemyPosesMax),double(now)/100.),now+clocks[0]);
            check(!guest.sendEnemies(poses(1),now+clocks[1]),"enemies_fuzz_authority");
            check(!host.latestEnemies(now+clocks[0]),"enemies_fuzz_host_none");
            if(auto got=guest.latestEnemies(now+clocks[1])){
                ++delivered;double x=got->enemies.poses[0].position[0];
                check(got->enemies.count==1+seed%EnemyPosesMax,"enemies_fuzz_count");
                check(x>=lastX&&(now>=30000||uint32_t(got->sequence-lastSequence)<0x80000000u),"enemies_fuzz_forward");   // sequences start again after the outage
                check(double(now)/100.-x<=(EnemyStaleMs+2000+50)/100.,"enemies_fuzz_fresh");   // stale limit plus the clock check's slack
                check(got->ageMs<=EnemyStaleMs,"enemies_fuzz_age");
                for(size_t i=1;i<got->enemies.count;++i)check(got->enemies.poses[i].key>got->enemies.poses[i-1].key,"enemies_fuzz_keys");
                lastSequence=got->sequence;lastX=x;}
            if(now>=26500&&now<30000)check(!guest.latestEnemies(now+clocks[1]),"enemies_fuzz_outage_stale");
        }
        check(delivered>200,"enemies_fuzz_delivery");check(at.enemySends>100&&at.enemySends<=60000/EnemySendMs+2&&bt.enemySends==0,"enemies_fuzz_rate");
        check(host.connected()&&guest.connected(),"enemies_fuzz_reconnect");
    }
    std::puts("enemies fuzz: 200 seeds, 60 s simulated, 15% loss/duplicates, 2% corruption, reordering, outage/reconnect, independent clocks");
}
}
int main(int argc,char** argv){
    std::string mutant=argc>1?argv[1]:"";
#define MUTATE(name) if(mutant==#name)protection.name=false
    MUTATE(enemiesRange);MUTATE(enemiesRole);MUTATE(enemiesOrder);MUTATE(enemiesFresh);MUTATE(enemiesStale);MUTATE(enemiesRate);
#undef MUTATE
    parser();sessions();if(mutant.empty()&&!failures)fuzz();
    std::printf("enemies core failures=%u\n",failures);return failures?1:0;
}
