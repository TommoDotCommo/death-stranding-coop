// Test mode (testmode.h), game-free: where a set's crates go, the rate limiter and the cap of crates on the ground, the
// gauges' gate (no write on another thread, in a game event, while down or caught), the refill rule and the status
// block's search, with fake memory. Every protection has a mutant (MUTANT_TESTMODE_*) this test must fail.
#include "testmode.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod;
using namespace mod::testmode;
static unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"testmode oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
// Sparse fake process memory: any address, bytes default to unreadable.
struct Memory {
    std::map<uint64_t,uint8_t> bytes;unsigned pokes=0;
    template<class T> void put(uint64_t at,T v){uint8_t b[sizeof v];std::memcpy(b,&v,sizeof v);for(size_t i=0;i<sizeof v;++i)bytes[at+i]=b[i];}
    template<class T> T get(uint64_t at){uint8_t b[sizeof(T)];for(size_t i=0;i<sizeof(T);++i)b[i]=bytes[at+i];T v;std::memcpy(&v,b,sizeof v);return v;}
    bool read(uint64_t at,void* to,size_t n){for(size_t i=0;i<n;++i){auto it=bytes.find(at+i);if(it==bytes.end())return false;static_cast<uint8_t*>(to)[i]=it->second;}return true;}
    discovery::Sampler sampler(uint64_t base){return discovery::Sampler([this](uint64_t a,void* p,size_t n){return read(a,p,n);},base);}
    Writable writable(){return [this](uint64_t at,size_t n){for(size_t i=0;i<n;++i)if(!bytes.count(at+i))return false;return true;};}
    Poke poke(){return [this](uint64_t at,const void* from,size_t n){++pokes;for(size_t i=0;i<n;++i)bytes[at+i]=static_cast<const uint8_t*>(from)[i];};}
};
constexpr uint64_t Image=0x140000000,Manager=0x200000,List=0x210000,Player=0x220000,Other=0x230000,Porter=0x300000;
static void world(Memory& m,uint64_t owner=Porter){
    m.put<uint64_t>(Image+PlayerManagerSlot,Manager);m.put<int32_t>(Manager+0x38,2);m.put<uint64_t>(Manager+0x40,List);
    m.put<uint64_t>(List,Other);m.put<uint32_t>(Other+0x88,1);m.put<uint64_t>(Other+0x70,0x999000);   // not number zero
    m.put<uint64_t>(List+8,Player);m.put<uint32_t>(Player+0x88,0);m.put<uint64_t>(Player+0x70,owner);
    const uint64_t B=Player+PlayerInfo;
    m.put<float>(B+BloodValue,40);m.put<float>(B+BloodMax,100);m.put<float>(B+0x34,90);m.put<float>(B+StaminaValue,30);m.put<float>(B+StaminaMax,90);
}
static Gate open(){Gate g;g.gameThread=true;g.porterReadable=true;g.inEvent=false;g.vitals.known=true;return g;}
static cargo::Record crate(uint32_t hash,std::array<double,3> at,uint8_t where=cargo::InWorld){cargo::Record r;r.hash=hash;r.position=at;r.state=uint32_t(where)<<8|7u;return r;}

static void table(){
    CHECK(MaterialCount==6&&std::strcmp(Materials[5].name,"resins")==0&&Materials[5].hash==0x0ae5c9acu);
    for(auto& m:Materials)CHECK(m.hash!=0);   // every material has its largest crate (listitems.py)
    auto h=knownHashes();CHECK(h.size()==6&&h.back()==0x0ae5c9acu);
    for(auto& m:Materials)CHECK(m.hash||!m.verified);   // nothing verified without a hash
    CHECK(missingNames().find("metals")!=std::string::npos||Materials[1].hash);
}
static void placing(){
    // Facing +x: the crates go 2 m behind him (x - 2), across his back (y spread), half a metre up.
    auto p=places({10,20,30},1,0,6);CHECK(p.size()==6);
    for(auto& a:p)CHECK(std::fabs(a[0]-8)<1e-9&&std::fabs(a[2]-30.5)<1e-9);
    CHECK(std::fabs(p[0][1]-(20+3.0))<1e-9&&std::fabs(p[5][1]-(20-3.0))<1e-9&&std::fabs((p[1][1]-p[2][1])-Spacing)<1e-9);
    auto one=places({0,0,0},0,0,1);CHECK(one.size()==1&&std::fabs(one[0][1]+Behind)<1e-9);   // no facing: +y
}
static void dropping(){
    const std::vector<uint32_t> six{1,2,3,4,5,6};const std::array<double,3> here{0,0,0};
    {   Dropper d;
        auto r=d.press(1000,here,0,1,six);CHECK(r.refused.empty()&&r.drops.size()==6&&d.onGround()==6);
        // Rate: a second press within 5 s asks for nothing; at 5 s it does.
        auto again=d.press(1000+Dropper::GapMs-1,here,0,1,six);CHECK(again.drops.empty()&&!again.refused.empty()&&d.onGround()==6);
        auto later=d.press(1000+Dropper::GapMs,here,0,1,six);CHECK(later.drops.size()==6&&d.onGround()==12);
        // Cap: 20 at most; the fourth set brings only 2, the fifth nothing, and nothing already there is touched.
        CHECK(d.press(1000+2*Dropper::GapMs,here,0,1,six).drops.size()==6&&d.onGround()==18);
        auto partial=d.press(1000+3*Dropper::GapMs,here,0,1,six);CHECK(partial.drops.size()==2&&d.onGround()==20);
        auto full=d.press(1000+4*Dropper::GapMs,here,0,1,six);CHECK(full.drops.empty()&&full.refused.find("20")!=std::string::npos&&d.onGround()==20);
    }
    {   // Crates that never appear stop counting after 10 s; those that lie on the ground keep counting.
        Dropper d;auto r=d.press(0,here,0,1,six);
        std::vector<cargo::Record> recs;for(size_t i=0;i<4;++i)recs.push_back(crate(r.drops[i].hash,r.drops[i].at));
        d.observe(recs,500);CHECK(d.onGround()==6);
        d.observe(recs,Dropper::ExpectMs+1);CHECK(d.onGround()==4);
        // Settled a little lower: still his. One picked up (carried: location 2), one gone: they stop counting.
        recs[0].position[2]-=0.4;recs[1].state=uint32_t(cargo::OnPorter)<<8|7u;recs.erase(recs.begin()+2);
        d.observe(recs,Dropper::ExpectMs+600);CHECK(d.onGround()==2);
        // A record counts for one crate only: two of his crates of one item, one record.
        Dropper e;std::vector<uint32_t> same{9,9};auto two=e.press(0,here,0,1,same);
        e.observe({crate(9,two.drops[0].at)},Dropper::ExpectMs+1);CHECK(e.onGround()==1);
        // Someone else's crate far away is not his; another item at his spot is not his.
        Dropper f;auto three=f.press(0,here,0,1,{7});f.observe({crate(7,{50,50,0}),crate(8,three.drops[0].at)},Dropper::ExpectMs+1);CHECK(f.onGround()==0);
    }
    {   Dropper d;auto r=d.press(0,here,0,1,{});CHECK(r.drops.empty()&&!r.refused.empty());
        CHECK(d.press(1,here,0,1,six).drops.size()==6);   // a refused press with no hashes does not start the 5 s
    }
}
static void gauges(){
    CHECK(refill(40,100)==100.f&&refill(0,90)==90.f);
    CHECK(!refill(100,100)&&!refill(101,100));                      // full (or over): nothing written
    CHECK(!refill(NAN,100)&&!refill(10,NAN)&&!refill(10,0)&&!refill(-1,100)&&!refill(10,INFINITY)&&!refill(10,2e6f));
    {   Gate g=open();CHECK(*refusal(g)==0);
        auto shut=[&](Gate x){return *refusal(x)!=0;};
        Gate a=g;a.gameThread=false;CHECK(shut(a));
        Gate b=g;b.porterReadable=false;CHECK(shut(b));
        Gate c=g;c.inEvent=true;CHECK(shut(c));
        Gate d=g;d.vitals.dead=true;CHECK(shut(d));
        Gate e=g;e.vitals.caught=true;CHECK(shut(e));
        Gate f=g;f.vitals.known=false;CHECK(shut(f));
    }
    {   Memory m;world(m);auto s=m.sampler(Image);const uint64_t B=Player+PlayerInfo;
        CHECK(infoBlock(s,Image,Porter)==B);
        CHECK(infoBlock(s,Image,Porter+8)==0&&infoBlock(s,Image,0)==0);   // another porter's entity: not his block
        auto k=keepFull(open(),s,B,true,true,m.writable(),m.poke());
        CHECK(*k.refused==0&&k.blood==1&&k.stamina==1&&m.get<float>(B+BloodValue)==100.f&&m.get<float>(B+StaminaValue)==90.f);
        CHECK(m.get<float>(B+0x34)==90.f&&m.get<float>(B+BloodMax)==100.f&&m.get<float>(B+StaminaMax)==90.f);   // nothing else
        unsigned before=m.pokes;auto full=keepFull(open(),s,B,true,true,m.writable(),m.poke());CHECK(full.blood==0&&full.stamina==0&&m.pokes==before);
        // Each closed gate: nothing written.
        for(int which=0;which<5;++which){Memory n;world(n);auto t=n.sampler(Image);Gate g=open();
            switch(which){case 0:g.gameThread=false;break;case 1:g.inEvent=true;break;case 2:g.vitals.dead=true;break;case 3:g.vitals.caught=true;break;default:g.porterReadable=false;}
            auto r=keepFull(g,t,B,true,true,n.writable(),n.poke());CHECK(*r.refused!=0&&n.pokes==0&&n.get<float>(B+BloodValue)==40.f&&n.get<float>(B+StaminaValue)==30.f);}
        // Switches: only stamina.
        {Memory n;world(n);auto t=n.sampler(Image);auto r=keepFull(open(),t,B,false,true,n.writable(),n.poke());CHECK(r.blood==0&&r.stamina==1&&n.get<float>(B+BloodValue)==40.f);}
        // No block, memory that is not writable: nothing.
        {Memory n;world(n);auto t=n.sampler(Image);auto r=keepFull(open(),t,0,true,true,n.writable(),n.poke());CHECK(*r.refused!=0&&n.pokes==0);
         auto none=[](uint64_t,size_t){return false;};auto q=keepFull(open(),t,B,true,true,none,n.poke());CHECK(q.blood==0&&q.stamina==0&&n.pokes==0);}
        // Another player's block (his entity is not this porter's): the search refuses it.
        {Memory n;world(n,0x777000);auto t=n.sampler(Image);CHECK(infoBlock(t,Image,Porter)==0);}
    }
}
int main(){
    table();placing();dropping();gauges();
    if(failures){std::fprintf(stderr,"%u testmode failures\n",failures);return 1;}
    std::puts("test mode: crate places, one set every 5 s, at most 20 on the ground (nothing removed), gauges kept full through the gate and only this porter's block passed");
}
