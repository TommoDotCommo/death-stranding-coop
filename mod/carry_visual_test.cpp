// What the partner carries (carry_visual.h): the read of the porter's carrier tree from fake memory, the summary,
// its wire form, and two simulated copies exchanging it through the real core over a lossy link with an outage.
#include "carry_visual.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <random>
using namespace mod::carry;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"carry oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr uint64_t Image=0x140000000;
// A porter's memory: entity -> carrier component -> carrier -> slots -> stacks -> records in the manager's array.
struct Game {
    uint64_t base=0x10000000;std::vector<unsigned char> mem=std::vector<unsigned char>(0x200000);uint64_t next=0x1000;
    uint64_t entity=0,component=0,carrier=0,manager=0;
    template<class T> void put(uint64_t a,T v){std::memcpy(mem.data()+(a-base),&v,sizeof v);}
    uint64_t alloc(size_t n){uint64_t a=base+next;next+=(n+15)&~size_t(15);return a;}
    mod::discovery::Sampler sampler{[this](uint64_t a,void* p,size_t n){if(a<base||a-base>mem.size()||n>mem.size()-(a-base))return false;std::memcpy(p,mem.data()+(a-base),n);return true;},Image};
    Game(){entity=alloc(0x5500);component=alloc(0xb0);carrier=alloc(0xb0);manager=base+0x100000;
        put(entity+PorterCarrier,component);put(component,Image+ComponentVtable);put(component+CarrierObject,carrier);carry({});}
    uint64_t record(unsigned index,uint32_t hash,uint64_t holder){uint64_t r=manager+RecordBase+RecordStride*index;
        put<uint32_t>(r+RecordHash,hash);put<uint32_t>(r+RecordKind,0x207);put<uint64_t>(r+RecordHolder,holder);return r;}
    // slots: type -> stacks (bottom first) of (record index, hash)
    void carry(const std::vector<std::pair<uint8_t,std::vector<std::vector<std::pair<unsigned,uint32_t>>>>>& slots,uint64_t holder=0){
        std::vector<uint64_t> slotPtrs;
        for(auto& [type,stacks]:slots){uint64_t slot=alloc(0x80);put<uint8_t>(slot+SlotType,type);put<uint64_t>(slot+SlotCarrier,carrier);std::vector<uint64_t> stackPtrs;
            for(auto& st:stacks){uint64_t stack=alloc(0x40),items=alloc(8*st.size()+8);
                for(size_t i=0;i<st.size();++i)put<uint64_t>(items+8*i,record(st[i].first,st[i].second,holder?holder:slot));
                put<int32_t>(stack+Items,int32_t(st.size()));put<uint64_t>(stack+ItemArray,items);stackPtrs.push_back(stack);}
            uint64_t arr=alloc(8*stackPtrs.size()+8);for(size_t i=0;i<stackPtrs.size();++i)put<uint64_t>(arr+8*i,stackPtrs[i]);
            put<int32_t>(slot+Stacks,int32_t(stackPtrs.size()));put<uint64_t>(slot+StackArray,arr);slotPtrs.push_back(slot);}
        uint64_t arr=alloc(8*slotPtrs.size()+8);for(size_t i=0;i<slotPtrs.size();++i)put<uint64_t>(arr+8*i,slotPtrs[i]);
        put<int32_t>(carrier+Slots,int32_t(slotPtrs.size()));put<uint64_t>(carrier+SlotArray,arr);
    }
    std::optional<Summary> read(){auto items=readCarried(sampler,entity,Image+ComponentVtable,manager);if(!items)return std::nullopt;return summarise(*items);}
};
// The receiving copy's world, as far as this feature can touch it. Records model the game's cargo: the porter "has"
// an item when any Player carrier holds it (the game's own check walks every carrier), so a record held by the
// partner body would count as his. Visuals are what is drawn on the partner body and nothing else.
struct World final:Sink {
    std::multimap<uint32_t,uint8_t> playerCarried;   // item -> slot, every record on a Player carrier
    Summary visuals;unsigned attaches=0,detaches=0;
    void attach(const Summary& s) override {visuals=s;++attaches;}
    void detach() override {visuals.clear();++detaches;}
    void hold(uint32_t hash,uint8_t slot) override {playerCarried.emplace(hash,slot);}   // a real record on the partner body
    size_t porterHas(uint32_t hash) const {return playerCarried.count(hash);}
};
struct Endpoint final:ds::StateProvider,ds::Transport {
    std::deque<ds::Bytes>* out=nullptr;
    bool readLocal(ds::State& s) override {s=ds::State{};s.position={1,0,1};return true;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render&) override {}
    void send(const ds::Bytes& b) override {out->push_back(b);}
};
Summary items(uint32_t hash,uint8_t slot,uint8_t count,uint8_t level=0,uint8_t column=0){return {{hash,slot,column,level,count}};}
}
int main(){
    {   // The read: backpack with two stacks, a hand, gear (not cargo), a record held elsewhere, a pointer outside the array.
        Game g;
        g.carry({{1,{{{3,100},{4,100},{5,200}},{{6,300}}}},{7,{{{8,400}}}},{14,{{{9,500}}}}});
        auto s=g.read();CHECK(s&&s->size()==4);
        if(s&&s->size()==4){
            CHECK((*s)[0]==(Entry{100,1,0,0,2}));CHECK((*s)[1]==(Entry{200,1,0,2,1}));CHECK((*s)[2]==(Entry{300,1,1,0,1}));
            CHECK((*s)[3]==(Entry{400,7,0,0,1}));}
        auto raw=readCarried(g.sampler,g.entity,Image+ComponentVtable,g.manager);CHECK(raw&&raw->size()==5&&(*raw)[0].index==3&&(*raw)[4].slot==7);
        // A stale record: still in the stack array, but the record names another holder.
        uint64_t other=g.alloc(0x40);g.carry({{1,{{{3,100}},{{4,200}}}}});g.record(4,200,other);
        auto t=g.read();CHECK(t&&t->size()==1&&(*t)[0].hash==100);
        // A record without an id of its own is named by its list item; without a readable item it stays 0.
        {Game h;h.carry({{1,{{{3,0},{4,0}}}}});uint64_t item=h.alloc(0x40);h.put<uint32_t>(item+ItemId,0x17053e1a);
         h.put<uint64_t>(h.manager+RecordBase+RecordStride*3+RecordItem,item);
         auto u=h.read();CHECK(u&&u->size()==2&&(*u)[0].hash==0x17053e1a&&(*u)[1].hash==0);}
        // The live layout: records beyond index 511 count; a slot that names another carrier does not.
        {Game h;h.carry({{1,{{{557,700}}}}});auto u=h.read();CHECK(u&&u->size()==1&&(*u)[0].hash==700);
         uint64_t slots=0;std::memcpy(&slots,h.mem.data()+(h.carrier+SlotArray-h.base),8);uint64_t slot=0;std::memcpy(&slot,h.mem.data()+(slots-h.base),8);
         h.put<uint64_t>(slot+SlotCarrier,h.alloc(0x40));auto v=h.read();CHECK(v&&v->empty());}
        // Wrong vtable, absurd counts, no component: unreadable (nullopt), never a guess.
        g.put(g.component,uint64_t(0x1234));CHECK(!g.read());g.put(g.component,Image+ComponentVtable);
        g.put<int32_t>(g.carrier+Slots,100000);CHECK(!g.read());g.put<int32_t>(g.carrier+Slots,-1);CHECK(!g.read());
        g.carry({});auto e=g.read();CHECK(e&&e->empty());
        g.put<uint64_t>(g.entity+PorterCarrier,0);CHECK(!g.read());
    }
    {   // Caps: a huge stack is summarised into at most MaxEntries entries, from at most MaxItems records.
        Game g;std::vector<std::pair<unsigned,uint32_t>> stack;for(unsigned i=0;i<30;++i)stack.push_back({i,1000+i});
        std::vector<std::vector<std::pair<unsigned,uint32_t>>> stacks(5,stack);for(unsigned c=0;c<5;++c)for(auto& x:stacks[c])x.first+=c*30;
        g.carry({{1,stacks}});auto raw=readCarried(g.sampler,g.entity,Image+ComponentVtable,g.manager);
        CHECK(raw&&raw->size()==MaxItems);
        auto s=g.read();CHECK(s&&s->size()==MaxEntries);
        auto ev=encode(*s,1);CHECK(ev.size()==MaxEntries);
    }
    {   // Wire: round trip, empty summary, every event a valid core cargo event (type 5, ref below 2^53).
        Summary s={{0x0782598d,1,0,0,3},{42,1,1,0,1},{7,6,0,0,1}};
        auto ev=encode(s,513);Receiver r;std::optional<Summary> got;
        for(auto& e:ev){CHECK(e.type==WireType&&e.ref<(uint64_t(1)<<53));auto x=r.feed(e);if(x)got=x;}
        CHECK(got&&*got==s);
        auto empty=encode({},514);CHECK(empty.size()==1);auto z=r.feed(empty[0]);CHECK(z&&z->empty());
        // A run broken by a lost tail (sender restarted): the half summary is dropped, the next full one is taken.
        Receiver q;q.feed(ev[0]);q.feed(ev[1]);auto again=encode(s,1);std::optional<Summary> later;for(auto& e:again)if(auto x=q.feed(e))later=x;
        CHECK(later&&*later==s);
        Receiver w;CHECK(!w.feed(ev[1])&&!w.feed(ev[2]));   // no start: nothing
        auto bad=ev[0];bad.ref|=uint64_t(1)<<51;CHECK(!Receiver().feed(bad));
        auto gear=encode(items(5,14,1),3);CHECK(!Receiver().feed(gear[0]));   // a gear slot is not cargo
    }
    {   // Sender: waits for a stable read, rate-limited, resends on a new connection.
        Sender s;auto one=items(10,1,1),two=items(10,1,2);
        CHECK(s.observe(one,true,0).empty());CHECK(s.observe(one,true,300).empty());CHECK(!s.observe(one,true,600).empty());
        CHECK(s.observe(one,true,900).empty());                                       // unchanged
        CHECK(s.observe(two,true,1000).empty()&&s.observe(two,true,1400).empty());     // not stable yet
        CHECK(!s.observe(two,true,1700).empty()&&s.sent()==two);
        CHECK(s.observe(std::nullopt,true,5000).empty());                              // unreadable: keep what was sent
        CHECK(s.observe(two,false,6000).empty());                                      // away: nothing
        auto back=s.observe(two,true,6100);CHECK(!back.empty());                       // back: the same, again
    }
    {   // parseSpec (coop-partner --carry)
        auto a=parseSpec("125983117");CHECK(a&&a->size()==1&&(*a)[0]==(Entry{125983117,1,0,0,1}));
        auto b=parseSpec("1:1:2,2:1,3:7");CHECK(b&&b->size()==3&&(*b)[1].level==2&&(*b)[2].slot==7&&(*b)[2].level==0);
        auto n=parseSpec("none");CHECK(n&&n->empty());
        for(auto bad:{"","x","1:0","1:14","1:1:0","1:1:300","0","1:2:3:4","99999999999",",","1,"})CHECK(!parseSpec(bad));
    }
    {   // Two copies through the real core: A's porter carries, B shows it on the partner body and nothing else.
        // Loss, duplicates, an outage long enough to reconnect, and changes while away.
        std::mt19937 rng(7);
        std::deque<ds::Bytes> aToB,bToA;Endpoint ea,eb;ea.out=&aToB;eb.out=&bToA;
        ds::Config ca;ca.role=ds::Role::Host;ca.session=9;ca.nonce=11;ds::Config cb=ca;cb.role=ds::Role::Client;cb.nonce=22;
        ds::Core a(ca,ea,ea),b(cb,eb,eb);Game game;World seenA,seenB;Exchange xa,xb;
        seenB.playerCarried.emplace(77,1);   // B's own porter carries something of his own
        bool outage=false;uint64_t now=0;
        auto pump=[&](std::deque<ds::Bytes>& q,ds::Core& to){while(!q.empty()){auto p=q.front();q.pop_front();
            if(outage||rng()%10==0)continue;
            to.receive(p.data(),p.size(),now);if(rng()%20==0)to.receive(p.data(),p.size(),now);}};
        auto run=[&](uint64_t until){for(;now<until;now+=10){
            a.tick(now);b.tick(now);pump(aToB,b);pump(bToA,a);
            for(auto& e:b.takeCargo())CHECK(xb.incoming(e,seenB));
            for(auto& e:a.takeCargo())xa.incoming(e,seenA);
            if(now%250==0){for(auto& e:xa.tick(game.read(),a.connected(),now,seenA))a.queueCargo(e);
                           for(auto& e:xb.tick(Summary{},b.connected(),now,seenB))b.queueCargo(e);}}};
        game.carry({{1,{{{3,100},{4,100}}}},{7,{{{8,400}}}}});
        run(5000);
        Summary want={{100,1,0,0,2},{400,7,0,0,1}};
        CHECK(a.connected()&&b.connected());
        CHECK(seenB.visuals==want);                                    // drawn on the partner body
        CHECK(seenB.playerCarried.size()==1&&seenB.porterHas(100)==0&&seenB.porterHas(400)==0);   // B's cargo untouched
        CHECK(seenA.visuals.empty()&&seenA.attaches==0);               // B carries nothing: A shows nothing
        // A puts everything down: B takes the items off the partner body.
        game.carry({});run(9000);
        CHECK(seenB.visuals.empty()&&seenB.detaches>=1);
        // A picks two up again, then the link fails long enough for both cores to reconnect.
        game.carry({{1,{{{5,100},{6,100}}}}});run(12000);
        CHECK(seenB.visuals==items(100,1,2));
        outage=true;game.carry({{1,{{{5,100},{6,100},{9,600}}}}});run(17000);
        CHECK(!b.connected()&&seenB.visuals.empty());                  // partner gone: nothing left on its body
        outage=false;run(24000);
        Summary after={{100,1,0,0,2},{600,1,0,2,1}};
        CHECK(a.connected()&&b.connected()&&seenB.visuals==after);     // exactly what A carries now, no duplicates
        CHECK(seenB.playerCarried.size()==1);
        CHECK(xb.view().shown()==after);
    }
    // The record's variant bit (+204 bit 5) is read, splits a run of the same item, and survives the wire.
    {
        Game g;g.carry({{1,{{{5,0xAA},{6,0xAA},{7,0xAA}}}}});
        g.put<uint32_t>(g.manager+RecordBase+RecordStride*6+RecordFlags,0x321);
        auto v=g.read();
        CHECK(v&&v->size()==3&&!(*v)[0].variant&&(*v)[1].variant&&!(*v)[2].variant&&(*v)[1].level==1);
        if(v){Receiver rx;std::optional<Summary> got;for(auto& e:encode(*v,9))got=rx.feed(e);
            CHECK(got&&*got==*v);}
    }
    if(!failures)std::puts("carry visual checks passed");
    return failures?1:0;
}
