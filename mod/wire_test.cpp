// The newer event types through the real core, both ways: what one copy tells is what the other reads. The codecs'
// own tests hand encode's result straight to decode; the wire between them holds a position to +-100 km in
// centimetres, numbers each event in its id and refuses integers of 2^53 and over (the story news lost its size to
// the first of these on its first live run).
#include "rewards.h"
#include "signals.h"
#include "orderload.h"
#include "../core/core.h"
#include <cstdio>
#include <deque>
static int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
namespace {
struct End final:ds::StateProvider,ds::Transport {
    std::deque<ds::Bytes>* out=nullptr;
    bool readLocal(ds::State& s) override {s=ds::State{};s.position={1,0,1};return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render&) override {}
    void send(const ds::Bytes& b) override {out->push_back(b);}
};
}
int main(){
    using namespace mod;
    std::deque<ds::Bytes> aToB,bToA;End ea,eb;ea.out=&aToB;eb.out=&bToA;
    ds::Config ca;ca.role=ds::Role::Host;ca.session=9;ca.nonce=0xfedcba9876543210ull;ds::Config cb=ca;cb.role=ds::Role::Client;cb.nonce=22;
    ds::Core a(ca,ea,ea),b(cb,eb,eb);uint64_t now=0;std::vector<ds::CargoEvent> got;
    auto pump=[&](std::deque<ds::Bytes>& q,ds::Core& to){while(!q.empty()){auto p=q.front();q.pop_front();to.receive(p.data(),p.size(),now);}};
    auto run=[&](uint64_t until){for(;now<until;now+=10){a.tick(now);b.tick(now);pump(aToB,b);pump(bToA,a);for(auto& e:b.takeCargo())got.push_back(e);a.takeCargo();}};
    run(3000);CHECK(a.connected()&&b.connected());
    auto through=[&](ds::CargoEvent e)->std::optional<ds::CargoEvent>{got.clear();a.queueCargo(e);run(now+1500);
        if(got.size()!=1)return std::nullopt;return got[0];};
    // Rewards: a facility's total, at both ends of its range.
    for(rewards::News n:{rewards::News{0x9abcdef0u,1,0},rewards::News{1,rewards::MaxPoints,rewards::MaxLevel},rewards::News{0xffffffffu,65536,3},rewards::News{7,65535,1}}){
        auto e=through(rewards::encode(n));CHECK(e);auto d=e?rewards::decode(*e):std::nullopt;
        CHECK(d&&d->hash==n.hash&&d->points==n.points&&d->level==n.level);
    }
    // Shout and scan, and the vitals at their extremes.
    for(auto k:{signals::Kind::Shout,signals::Kind::Scan}){auto e=through(signals::encodeEvent(k,0xffffffffu));CHECK(e);auto d=e?signals::decodeEvent(*e):std::nullopt;CHECK(d&&*d==k);}
    for(signals::Vitals v:{signals::Vitals{0,0,false,false},signals::Vitals{100,100,true,true},signals::Vitals{signals::Unknown,37,true,false}}){
        auto e=through(signals::encodeVitals(v,0xffffffffu));CHECK(e);auto d=e?signals::decodeVitals(*e):std::nullopt;CHECK(d&&*d==v);
    }
    // An order case's tag: the largest ids and the life values a case can have.
    for(orderload::Tag t:{orderload::Tag{0xffffffffull,63,orderload::TypeStandard,uint8_t(15&~orderload::FlagStory),0xffffffffu,{100000.f,0.f},0xffff},
                          orderload::Tag{226007,2,orderload::TypeStandard,0,125983117u,{-1.f,-1.f},1},
                          orderload::Tag{1,0,orderload::TypeStandard,0,1,{87.25f,12.5f},0}}){
        auto w=orderload::encode(t);ds::CargoEvent e;e.type=orderload::WireType;e.item=w.item;e.ref=w.ref;e.position=w.position;
        auto r=through(e);CHECK(r);auto d=r?orderload::decode({r->item,r->ref,r->position}):std::nullopt;
        CHECK(d&&d->mission==t.mission&&d->unit==t.unit&&d->type==t.type&&d->flags==t.flags&&d->item==t.item&&d->link==t.link&&d->life[0]==t.life[0]&&d->life[1]==t.life[1]);
    }
    if(!failures)std::puts("wire round trips passed");
    return failures?1:0;
}
