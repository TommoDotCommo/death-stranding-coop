#include "../core.h"
#include "coopfuzz/net.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
struct Queue : Transport {
    std::vector<Bytes> out;std::vector<std::vector<uint8_t>> longs;
    void send(const Bytes& b) override {out.push_back(b);}
    void sendLong(const std::vector<uint8_t>& b) override {longs.push_back(b);}
};
struct Pair {
    Fake a,b;Queue at,bt;Core host{{Role::Host,42,100,120},a,at},guest{{Role::Client,42,200,120},b,bt};
    Pair(){host.setProtections(protection);guest.setProtections(protection);pump(0);pump(10);at.out.clear();bt.out.clear();}
    void drain(uint64_t now) {
        for(unsigned i=0;i<8&&(!at.out.empty()||!bt.out.empty()||!at.longs.empty());++i){
            auto as=std::move(at.out);at.out.clear();for(auto& p:as)guest.receive(p.data(),p.size(),now);
            auto ls=std::move(at.longs);at.longs.clear();for(auto& p:ls)guest.receive(p.data(),p.size(),now);
            auto bs=std::move(bt.out);bt.out.clear();for(auto& p:bs)host.receive(p.data(),p.size(),now);
        }
    }
    void pump(uint64_t now){host.tick(now);guest.tick(now);drain(now);}
    Error receive(const Packet& p,uint64_t now=20){auto wire=encodeAnimation(p);return guest.receive(wire.data(),wire.size(),now);}
    Packet sample(uint32_t sequence=1,uint64_t stamp=20){Packet p;p.kind=Kind::Animation;p.role=Role::Host;p.session=42;p.nonce=100;p.echo=200;p.sequence=sequence;p.stamp=stamp;p.animation.held[0]=1;return p;}
};
void rechecksum(std::vector<uint8_t>& b){uint32_t h=2166136261u;for(size_t i=0;i<b.size();++i){h^=(i>=120&&i<124)?0:b[i];h*=16777619u;}for(int i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));}
Animation varied(){Animation a;a.held[0]=5;a.held[17]=128;a.rose[3]=1;a.floats[0]=1.5f;a.floats[17]=-90.f;a.floats[AnimationFloats-1]=4.f;a.handCount=1;a.hands[0]={0xdeadbeef,7,{-3000,120,2999},0xabcdef012345ull};return a;}
void parser() {
    Packet p;p.kind=Kind::Animation;p.sequence=1;p.stamp=5;p.animation=varied();Packet q;
    auto b=encodeAnimation(p);check(b.size()==WireSize+1+2*18+1+3*5+1+AnimationHandBytes+1,"animation_size");
    check(decode(b.data(),b.size(),q)==Error::None&&q.kind==Kind::Animation&&q.animation==p.animation&&q.sequence==1&&q.stamp==5,"animation_roundtrip");
    {Packet full=p;for(auto& x:full.animation.held)x=0xff;for(auto& x:full.animation.rose)x=0xff;for(size_t k=0;k<AnimationFloats;++k)full.animation.floats[k]=float(k+1)*(k%2?-1.f:1.f);full.animation.handCount=2;full.animation.hands[1]={1,6,{0,0,0},(1ull<<48)-1};
     auto w=encodeAnimation(full);check(w.size()==WireSize+1+64+1+5*AnimationFloats+1+2*AnimationHandBytes+1&&decode(w.data(),w.size(),q)==Error::None&&q.animation==full.animation,"animation_full");}
    {Packet empty=p;empty.animation={};auto w=encodeAnimation(empty);check(w.size()==WireSize+4&&decode(w.data(),w.size(),q)==Error::None&&q.animation==Animation{},"animation_empty");}
    {auto w=b;w.pop_back();w[8]=uint8_t(w.size());rechecksum(w);check(decode(w.data(),w.size(),q)==Error::Length,"animation_short");}
    {auto w=b;w.push_back(0);w[8]=uint8_t(w.size());rechecksum(w);check(decode(w.data(),w.size(),q)==Error::Length,"animation_long");}
    {auto w=b;w[WireSize+5]^=1;check(decode(w.data(),w.size(),q)==Error::Checksum,"animation_checksum");}
    {auto w=b;w[WireSize]=AnimationBoolBytes+1;rechecksum(w);check(decode(w.data(),w.size(),q)==Error::Length,"animation_bool_count");}
    {Packet h=p;h.kind=Kind::Presence;auto w=encode(h);w[6]=uint8_t(Kind::Animation);w[96]=0;
     std::vector<uint8_t> v(w.begin(),w.end());rechecksum(v);check(decode(v.data(),v.size(),q)==Error::Length,"animation_no_payload");}
    {std::vector<uint8_t> v(b);v.resize(AnimationWireMax+1);v[8]=uint8_t(v.size());v[9]=uint8_t(v.size()>>8);rechecksum(v);check(decode(v.data(),v.size(),q)==Error::Length,"animation_oversize");}
    {Packet h=p;h.kind=Kind::Presence;auto w=encode(h);std::vector<uint8_t> v(w.begin(),w.end());v.push_back(0);v.push_back(0);v[8]=uint8_t(v.size());rechecksum(v);
     check(decode(v.data(),v.size(),q)==Error::Length,"animation_only_long_kind");}
    {auto w=b;w[100]=1;rechecksum(w);check(decode(w.data(),w.size(),q)==Error::Reserved,"animation_reserved");}
    // Float entries: [u8 index][f32]; the last entry starts before the hand block (count byte + one case).
    auto entry=[&](unsigned index,float v){auto w=b;size_t at=w.size()-2-AnimationHandBytes-5;w[at]=uint8_t(index);uint32_t bits;std::memcpy(&bits,&v,4);for(int i=0;i<4;++i)w[at+1+i]=uint8_t(bits>>(8*i));rechecksum(w);return w;};
    {auto w=entry(AnimationFloats-1,std::numeric_limits<float>::quiet_NaN());check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=entry(AnimationFloats-1,std::numeric_limits<float>::infinity());check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=entry(AnimationFloats-1,2e6f);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=entry(AnimationFloats-1,0.f);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=entry(AnimationFloats,1.f);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=entry(17,1.f);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}   // not increasing
    {auto w=b;size_t at=w.size()-1-AnimationHandBytes;w[at]=w[at+1]=w[at+2]=w[at+3]=0;rechecksum(w);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}   // no item
    {auto w=b;size_t at=w.size()-1-AnimationHandBytes+5;w[at]=uint8_t(3001&0xff);w[at+1]=uint8_t(3001>>8);rechecksum(w);check(decodeForTest(w.data(),w.size(),q,protection)==Error::Range,"animationRange");}
    {auto w=b;w[w.size()-2-AnimationHandBytes]=3;rechecksum(w);check(decode(w.data(),w.size(),q)==Error::Length,"animation_hand_count");}
    {Packet bad=p;bad.animation.hands[0].position[1]=-3001;bool threw=false;try{encodeAnimation(bad);}catch(const std::invalid_argument&){threw=true;}check(threw,"animation_encode_hand");}
    {   // While driving: no controls, the vehicle's joints.
        Packet r;r.kind=Kind::Animation;r.sequence=2;r.stamp=6;r.animation.skeleton=uint16_t(AnimationJoints+16);r.animation.jointCount=AnimationJoints;
        for(size_t i=0;i<AnimationJoints;++i)r.animation.joints[i]={uint16_t(i+10),{int16_t(i),-32767,5,32767},{-2000,int16_t(i*7),1500}};
        auto w=encodeAnimation(r);check(w.size()==WireSize+4+2+AnimationJoints*AnimationJointBytes&&w.size()<=AnimationWireMax,"animation_joints_size");
        check(decode(w.data(),w.size(),q)==Error::None&&q.animation==r.animation,"animation_joints_roundtrip");
        auto bad=w;bad[WireSize+6+AnimationJointBytes*(AnimationJoints-1)]=uint8_t(AnimationJoints+16);rechecksum(bad);check(decodeForTest(bad.data(),bad.size(),q,protection)==Error::Range,"animationRange");   // index past the skeleton
        bad=w;bad[WireSize+6+AnimationJointBytes]=10;rechecksum(bad);check(decodeForTest(bad.data(),bad.size(),q,protection)==Error::Range,"animationRange");   // not increasing
        bad=w;bad.pop_back();bad[8]=uint8_t(bad.size());bad[9]=uint8_t(bad.size()>>8);rechecksum(bad);check(decode(bad.data(),bad.size(),q)==Error::Length,"animation_joints_short");
        Packet both=r;for(size_t k=0;k<AnimationFloats;++k)both.animation.floats[k]=1;bool threw=false;try{encodeAnimation(both);}catch(const std::invalid_argument&){threw=true;}check(threw,"animation_too_long");
        Packet wrong=r;wrong.animation.joints[3].index=uint16_t(AnimationJoints+16);threw=false;try{encodeAnimation(wrong);}catch(const std::invalid_argument&){threw=true;}check(threw,"animation_encode_joint");}
    {   // The payload alone, as a recording keeps it: the same bytes after the header, the same checks.
        auto pay=encodeAnimationPayload(p.animation);check(pay.size()+WireSize==b.size()&&std::equal(pay.begin(),pay.end(),b.begin()+WireSize),"animation_payload_bytes");
        Animation back;check(decodeAnimationPayload(pay.data(),pay.size(),back)&&back==p.animation,"animation_payload_roundtrip");
        auto cut=pay;cut.pop_back();check(!decodeAnimationPayload(cut.data(),cut.size(),back),"animation_payload_short");}
    {Packet bad=p;bad.animation.floats[2]=std::numeric_limits<float>::quiet_NaN();bool threw=false;try{encodeAnimation(bad);}catch(const std::invalid_argument&){threw=true;}check(threw,"animation_encode_nan");}
    {Packet bad=p;bad.animation.floats[2]=-2e6f;bool threw=false;try{encodeAnimation(bad);}catch(const std::invalid_argument&){threw=true;}check(threw,"animation_encode_range");}
}
void sessions() {
    {Pair f;check(f.host.connected()&&f.guest.connected(),"animation_pair");
        auto a=varied();check(f.host.sendAnimation(a,20),"animation_send");check(f.at.longs.size()==1,"animation_sent_long");f.drain(20);
        check(!f.guest.takeAnimation(139),"animation_not_due");   // shown 120 ms late, like presence
        auto got=f.guest.takeAnimation(140);check(got&&got->fresh&&got->sequence==1&&got->animation==a,"animation_due");
        got=f.guest.takeAnimation(141);check(got&&!got->fresh&&got->animation.rose==Animation{}.rose&&got->animation.held==a.held,"animation_rose_once");
        check(!f.host.sendAnimation(a,69),"animation_rate_burst");check(!f.host.sendAnimation(a,519),"animation_unchanged_waits");
        check(f.host.sendAnimation(a,520),"animation_unchanged_resend");
        auto b=a;b.held[1]=1;check(!f.host.sendAnimation(b,569),"animation_changed_burst");check(f.host.sendAnimation(b,570),"animation_changed_sends");
        f.drain(570);got=f.guest.takeAnimation(690);check(got&&got->fresh&&got->animation.held[1]==1,"animation_follows");
        check(f.guest.takeAnimation(1690).has_value()&&!f.guest.takeAnimation(1691),"animation_expiry");}
    {Pair f;auto p=f.sample(1,20);p.animation.floats[4]=1;check(f.receive(p)==Error::None,"animation_between_first");
        auto q=f.sample(2,70);q.animation.floats[4]=3;check(f.receive(q,70)==Error::None,"animation_between_second");
        auto got=f.guest.takeAnimation(160);   // shown at 40: two fifths of the way from 20 to 70
        check(got&&got->sequence==1&&got->between&&got->next.floats[4]==3&&std::abs(got->fraction-.4f)<1e-6f,"animation_between");
        got=f.guest.takeAnimation(200);check(got&&got->sequence==2&&!got->between,"animation_between_last");}
    {Pair f;auto p=f.sample();p.animation.rose[0]=1;check(f.receive(p)==Error::None,"animation_first");
        auto p2=f.sample(2,40);p2.animation.rose[0]=2;check(f.receive(p2,40)==Error::None,"animation_second");
        auto got=f.guest.takeAnimation(200);check(got&&got->sequence==2&&got->animation.rose[0]==3,"animationRose");}
    {Pair f;auto p=f.sample();check(f.receive(p)==Error::None,"animation_initial");check(f.receive(p)==Error::Duplicate,"animationOrder");
        p.sequence=2;p.stamp=19;check(f.receive(p)==Error::Duplicate,"animationOrder");
        p.sequence=0;p.stamp=21;check(f.receive(p)==Error::Duplicate,"animationOrder");}
    {Pair f;auto p=f.sample();p.stamp=2021;check(f.receive(p)==Error::Time,"animation_fresh");}
    {Pair f;auto p=f.sample();p.echo=201;check(f.receive(p)==Error::Nonce,"animation_echo");}
    {Pair f;check(f.receive(f.sample())==Error::None,"animation_before_reset");f.guest.disconnect(21);check(!f.guest.takeAnimation(500),"animation_reset");}
    {Fake a;Queue q;Core lone({Role::Host,42,100,120},a,q);check(!lone.sendAnimation(varied(),20)&&q.longs.empty(),"animation_needs_connection");}
}
struct Network : Transport {
    coopfuzz::LossyLink& link;int side;
    Network(coopfuzz::LossyLink& l,int s):link(l),side(s){}
    void send(const Bytes& b) override {link.send(side,1-side,{b.begin(),b.end()});}
    void sendLong(const std::vector<uint8_t>& b) override {link.send(side,1-side,{b.begin(),b.end()});}
};
void fuzz() {
    unsigned shown=0;
    for(uint64_t seed=1;seed<=100;++seed){
        coopfuzz::LossyLink link(2,seed);link.setLink({20,150,.10,.10,0});Fake a,b;Network at(link,0),bt(link,1);
        Core host({Role::Host,seed,100,120},a,at),guest({Role::Client,seed,200,120},b,bt);
        const uint64_t clocks[2]={10000,20000+seed*7};uint32_t lastSequence=0;
        for(uint64_t now=0;now<=30000;now+=16){
            link.advance(now);int from;coopfuzz::Bytes bytes;
            for(int side=0;side<2;++side)while(link.poll(side,from,bytes))(side?guest:host).receive(bytes.data(),bytes.size(),now+clocks[side]);
            host.tick(now+clocks[0]);guest.tick(now+clocks[1]);
            Animation x;x.floats[0]=float(now/100);x.held[0]=uint8_t(now/1000);host.sendAnimation(x,now+clocks[0]);
            if(auto got=guest.takeAnimation(now+clocks[1])){++shown;
                check(got->sequence>=lastSequence,"animation_fuzz_order");check(!got->fresh||got->sequence>lastSequence,"animation_fuzz_fresh");lastSequence=got->sequence;
                check(got->animation.floats[0]<=float(now/100)&&got->animation.held[0]==uint8_t(uint64_t(got->animation.floats[0])/10),"animation_fuzz_value");}
        }
    }
    check(shown>100000,"animation_fuzz_shown");
    std::printf("animation fuzz: 100 seeds, 30 s simulated, 10%% loss/duplicates, independent clocks, %u frames shown\n",shown);
}
}
int main(int argc,char** argv){
    std::string mutant=argc>1?argv[1]:"";
#define MUTATE(name) if(mutant==#name)protection.name=false
    MUTATE(animationRange);MUTATE(animationOrder);MUTATE(animationRose);
#undef MUTATE
    parser();sessions();if(mutant.empty()&&!failures)fuzz();
    std::printf("animation core failures=%u\n",failures);return failures?1:0;
}
