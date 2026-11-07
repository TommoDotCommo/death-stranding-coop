#include "join.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <cmath>
#include <optional>
using namespace mod::join;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"join oracle: %s\n",#__VA_ARGS__);++failures;}}while(0)
namespace {
Input good() {
    Input in;in.enabled=true;in.gameThread=true;in.connected=true;in.localGameplay=true;in.role=Role::Guest;in.connection=1;
    in.host.visible=true;in.host.stale=false;in.host.state.position={400,7,0};return in;
}
struct World {
    uint64_t base=0x100000;std::vector<unsigned char> bytes=std::vector<unsigned char>(0x40000);
    bool torn=false;unsigned calls=0;bool change=false;unsigned slotReads=0;
    ProgressSites sites{0x3000,0x3010,0x3020,0x1100,0x1120};
    template<class T> void put(size_t at,T value){std::memcpy(bytes.data()+at,&value,sizeof value);}
    static uint32_t crc(const unsigned char* data){uint32_t c=0;for(unsigned i=0;i<16;++i){c^=data[i];for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0x82f63b78u:0);}return c;}
    World(){
        const unsigned char a[]={0x40,0x53,0x48,0x83,0xec,0x20},b[]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57};
        std::memcpy(bytes.data()+0x1100,a,6);std::memcpy(bytes.data()+0x1120,b,8);
        put<uint64_t>(0x3000,base+0x4000);put<uint64_t>(0x4028,base+0x5000);put<uint64_t>(0x51f0,base+0x6000);
        put<uint64_t>(0x3010,base+0x7000);bytes[0x3020]=1;
        put<uint64_t>(0x7020,base+0x8000);put<uint32_t>(0x702c,2);
        put<uint32_t>(0x8000,crc(bytes.data()+0x3020)|0x80000000u);std::memcpy(bytes.data()+0x8008,bytes.data()+0x3020,16);
        put<uint64_t>(0x8018,base+0x9000);
        put<uint64_t>(0x6068,base+0xa000);bytes[0xa008]=2;
        put<uint64_t>(0x9050,base+0xb000);put<uint32_t>(0x905c,2);
        put<uint32_t>(0xb000,crc(bytes.data()+0xa008)|0x80000000u);std::memcpy(bytes.data()+0xb004,bytes.data()+0xa008,16);
        put<int32_t>(0x6040,3);put<uint64_t>(0x6048,base+0xc000);
        for(unsigned i=0;i<3;++i){put<uint64_t>(0xc000+8*i,base+0xd000+0x100*i);bytes[0xd008+0x100*i]=uint8_t(3+i);}
        put<uint32_t>(0xb014,crc(bytes.data()+0xd108)); // current episode is resource index 1
    }
    mod::discovery::Sampler sampler(){
        mod::discovery::Sampler s([this](uint64_t at,void* out,size_t n){
            ++calls;if(at<base||at-base>=bytes.size()||n>bytes.size()-(at-base))return false;
            if(change&&at==base+0x3000&&++slotReads==3)put<uint64_t>(0x3000,base+0x4100);
            std::memcpy(out,bytes.data()+at-base,n);if(torn&&calls%2)static_cast<unsigned char*>(out)[0]^=1;return true;
        },base);
        s.sections={{base,bytes.size(),false}};s.signatures.resize(6);return s;
    }
    ProgressObservation read(){auto s=sampler();return ProgressReader(s,sites).read();}
};

// Fake game for the native move: the porter, the gameplay/vehicle gates and the player teleport.
struct Game {
    Porter porter{1,2,true,{10,20,30},{1,0,0,0,1,0,0,0,1}};
    bool gameplay=true;std::optional<bool> riding=false;
    unsigned calls=0;bool moves=true,replace=false;double landOffset=-1; // the ground probe lands 1 m below
    mod::puppet::Transform last{};uint64_t lastPlayer=0;
    MoveApi api(){
        MoveApi a;a.porter=[this]{return porter;};a.gameplay=[this]{return gameplay;};a.riding=[this]{return riding;};
        a.teleport=[this](uint64_t player,const mod::puppet::Transform& t){++calls;last=t;lastPlayer=player;
            if(moves){porter.position=t.position;porter.position[2]+=landOffset;porter.rotation=t.rotation;}
            if(replace)porter.entity=99;};
        return a;
    }
};
Plan plan(double x=400,double y=7,double z=-3){Plan p;p.position={x,y,z};return p;}
bool has(const std::string& s,const char* what){return s.find(what)!=std::string::npos;}
// A small image with the three pinned code prefixes, the override's call and both vtables at fake RVAs.
struct Image {
    uint64_t base=0x200000;std::vector<unsigned char> bytes=std::vector<unsigned char>(0x3000,0xcc);
    MoveSites sites{0x1200,0x1400,0x1600,0x2000,0x1800,0x2100,0x1900,0xa8,0x20,0x7f};
    std::vector<unsigned char> code[3]={
        {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x1d,0xc1,0x92,0x02,0x48,0x33,0xc4,0x48,0x89,0x44,0x24,0x38,0x48,0x8b,0x79,0x70,0x41,0x8b,0xd8,0x48,0x8b,0xea,0x48,0x8b,0xf1},
        {0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55,0x57,0x41,0x56,0x48,0x8d,0xa8,0x48,0xff,0xff,0xff,0x48,0x81,0xec,0xa0,0x01,0x00,0x00,0xc5,0xf8,0x29,0x70,0xd8,0xc5,0xf8,0x29,0x78,0xc8,0xc5},
        {0x40,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x10,0x02,0x00,0x00,0x48,0x8b,0x05,0xea,0x96,0x82,0x01,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x00,0x02,0x00,0x00,0xc5,0xf8,0x10,0x15,0x07,0x0a,0x48}};
    template<class T> void put(size_t at,T v){std::memcpy(bytes.data()+at,&v,sizeof v);}
    Image(){
        std::copy(code[0].begin(),code[0].end(),bytes.begin()+0x1200);std::copy(code[1].begin(),code[1].end(),bytes.begin()+0x1400);
        std::copy(code[2].begin(),code[2].end(),bytes.begin()+0x1600);
        bytes[0x1200+0x7f]=0xe8;put<int32_t>(0x1200+0x7f+1,int32_t(0x1400-(0x1200+0x7f+5)));
        put<uint64_t>(0x2000,base+0x1800);put<uint64_t>(0x2000+0xa8,base+0x1200);
        put<uint64_t>(0x2100,base+0x1900);put<uint64_t>(0x2100+0x20,base+0x1600);
        put<uint64_t>(0x2800,base+0x2000);put<uint64_t>(0x2870,base+0x2a00);put<uint64_t>(0x2a00,base+0x2100); // player, player+0x70, entity
        // Player path: manager slot -> manager, selection record (index 0, mode 1, kind 2), manager+0x48 -> player.
        put<uint64_t>(0x2c00,base+0x2d00);put<uint64_t>(0x2d48,base+0x2800);
        put<int32_t>(0x2e00,0);put<int32_t>(0x2e04,1);put<int32_t>(0x2e08,2);put<uint64_t>(0x2e10,0);
        const double at[3]={1,2,3};const float r[9]={1,0,0,0,1,0,0,0,1};std::memcpy(bytes.data()+0x2ac8,at,24);std::memcpy(bytes.data()+0x2ae0,r,36);
    }
    mod::discovery::Sampler sampler(){
        mod::discovery::Sampler s([this](uint64_t at,void* out,size_t n){
            if(at<base||at-base>=bytes.size()||n>bytes.size()-(at-base))return false;
            std::memcpy(out,bytes.data()+at-base,n);return true;},base);
        s.sections={{base+0x1000,0x1000,true},{base+0x2000,0x1000,false}};s.signatures.resize(6);
        s.signatures[0].targets["player_manager_global"]=base+0x2c00;s.signatures[1].targets["player_selection_state"]=base+0x2e04;return s;
    }
    std::string verify(){auto s=sampler();return verifyMoveCode(s,sites);}
};
}
int main(){
    {Decision d;auto p=d.tick(good());CHECK(p&&p->position==std::array<double,3>({402,7,0})&&p->distance==std::sqrt(160049.));CHECK(!d.tick(good()));CHECK(d.finished());CHECK(wouldMove(*p).find("JOIN would move ")==0);}
    for(double distance:{0.,299.999,300.}){Decision d;auto in=good();in.host.state.position={distance,0,0};CHECK(!d.tick(in));in.host.state.position[0]=1000;CHECK(!d.tick(in));}
    {Decision d;auto in=good();in.host.state.position={300.001,0,0};CHECK(d.tick(in).has_value());}
    {Decision d;auto in=good();in.connection=2;CHECK(!d.tick(in));}
    for(auto role:{Role::None,Role::Host}){Decision d;auto in=good();in.role=role;CHECK(!d.tick(in));}
    {Decision d;auto in=good();in.enabled=false;CHECK(!d.tick(in));}
    {Decision d;auto in=good();in.gameThread=false;CHECK(!d.tick(in));in.gameThread=true;CHECK(d.tick(in).has_value());}
    for(unsigned gate=0;gate<7;++gate){Decision d;auto in=good();
        if(gate==0)in.localGameplay=false;
        if(gate==1)in.host.visible=false;
        if(gate==2)in.host.stale=true;
        if(gate==3)in.local.flags=ds::PrivateRoom;
        if(gate==4)in.host.state.flags=ds::PrivateRoom;
        if(gate==5)in.local.flags=ds::Vehicle;
        if(gate==6)in.host.state.flags=ds::Vehicle;
        CHECK(!d.tick(in));CHECK(d.tick(good()).has_value());
    }
    {Decision d;auto in=good();in.connected=false;CHECK(!d.tick(in));CHECK(d.tick(good()).has_value());}
    {Decision d;auto in=good();in.localGameplay=false;CHECK(!d.tick(in));in.connection=2;in.localGameplay=true;CHECK(!d.tick(in));}
    {Decision d;auto in=good();in.localGameplay=false;CHECK(!d.tick(in));in.connected=false;d.tick(in);CHECK(!d.tick(good()));}
    {Decision d;auto in=good();in.host.state.yaw=3.14159265358979323846/2;auto p=d.tick(in);CHECK(p&&std::abs(p->position[0]-400)<1e-9&&std::abs(p->position[2]+2)<1e-9&&p->position[1]==7);}
    for(double v:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),100001.}){Decision d;auto in=good();in.local.position[0]=v;CHECK(!d.tick(in));}
    {World w;auto before=w.bytes;auto p=w.read();CHECK(p.value.episode==1&&p.value.id==ds::UnknownProgress&&p.episodeHash!=ds::UnknownProgress);CHECK(w.bytes==before);}
    {World w;w.put<uint32_t>(0xb014,0);w.bytes[0xd108]=0;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.torn=true;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.change=true;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;auto s=w.sampler();s.signatures.clear();CHECK(ProgressReader(s,w.sites).read().value.episode==ds::UnknownProgress);}
    {World w;w.bytes[0x1100]=0;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.put<uint64_t>(0x3000,0);CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.put<uint32_t>(0x905c,8192);CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.bytes[0x8008]^=1;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.bytes[0xb004]^=1;CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;std::memcpy(w.bytes.data()+0xb018,w.bytes.data()+0xb000,24);CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.put<int32_t>(0x6040,65);CHECK(w.read().value.episode==ds::UnknownProgress);}
    {World w;w.bytes[0xd108]^=1;CHECK(w.read().value.episode==ds::UnknownProgress);}
    // Native move: once, gated right before the call, read back within 5 m, then a settle check for a fall.
    {CHECK(parseMoveMode("log")==MoveMode::Log&&parseMoveMode("native")==MoveMode::Native&&!parseMoveMode("Native")&&!parseMoveMode(""));}
    {Game g;g.porter.rotation={0,-1,0,1,0,0,0,0,1};Mover m(g.api());auto out=m.start(plan(),1000);
        CHECK(g.calls==1&&g.lastPlayer==1&&g.last.position==std::array<double,3>({400,-3,7}));
        CHECK(g.last.rotation==std::array<float,9>({0,-1,0,1,0,0,0,0,1})); // rotation kept, as the game's map teleport does
        CHECK(has(out,"JOIN native move called")&&has(out,"JOIN moved local porter: readback 1.00 m"));CHECK(m.stage()==Mover::Stage::Settling);
        CHECK(m.tick(1000+Mover::SettleMs-1).empty());auto settle=m.tick(1000+Mover::SettleMs);CHECK(has(settle,"JOIN settle check: porter holding"));
        CHECK(m.stage()==Mover::Stage::Done&&m.tick(99999).empty());
        CHECK(m.start(plan(),99999).empty()&&g.calls==1);} // never a second move, even after success
    {Game g;Mover m(g.api());m.start(plan(),0);g.porter.position={400,-3,7};m.start(plan(900,0,0),10);CHECK(g.calls==1);}
    {Game g;g.gameplay=false;Mover m(g.api());auto out=m.start(plan(),0);CHECK(g.calls==0&&has(out,"refused")&&has(out,"normal gameplay"));
        g.gameplay=true;CHECK(m.start(plan(),1).empty()&&g.calls==0);CHECK(m.stage()==Mover::Stage::Refused);}
    {Game g;g.riding=true;Mover m(g.api());CHECK(has(m.start(plan(),0),"vehicle")&&g.calls==0);}
    {Game g;g.riding=std::nullopt;Mover m(g.api());CHECK(has(m.start(plan(),0),"vehicle state unknown")&&g.calls==0);}
    for(auto bad:{plan(16000.5),plan(0,3000.5),plan(0,-500.5),plan(0,0,-16001),plan(std::numeric_limits<double>::quiet_NaN())}){
        Game g;Mover m(g.api());CHECK(has(m.start(bad,0),"sanity box")&&g.calls==0);}
    {Game g;g.porter.readable=false;Mover m(g.api());CHECK(has(m.start(plan(),0),"unreadable")&&g.calls==0);}
    {Game g;g.porter.position[0]=std::numeric_limits<double>::infinity();Mover m(g.api());m.start(plan(),0);CHECK(g.calls==0);}
    {Game g;MoveApi a=g.api();a.riding=nullptr;Mover m(a);CHECK(has(m.start(plan(),0),"incomplete")&&g.calls==0);}
    {Game g;g.moves=false;Mover m(g.api());auto out=m.start(plan(),0); // the game ignored the call: no false success
        CHECK(g.calls==1&&has(out,"JOIN readback failed")&&!has(out,"JOIN moved")&&m.stage()==Mover::Stage::Failed);CHECK(m.tick(99999).empty());}
    {Game g;g.landOffset=-5.5;Mover m(g.api());CHECK(has(m.start(plan(),0),"readback failed: porter 5.50 m"));}
    {Game g;g.replace=true;Mover m(g.api());CHECK(has(m.start(plan(),0),"replaced"));}
    {Game g;Mover m(g.api());m.start(plan(),0);g.porter.position[2]-=6;CHECK(has(m.tick(Mover::SettleMs),"settle check failed: porter dropped 6.00 m"));CHECK(m.stage()==Mover::Stage::Failed);}
    {Game g;Mover m(g.api());m.start(plan(),0);g.porter.position[0]+=20;CHECK(has(m.tick(Mover::SettleMs),"porter holding"));} // walking on is fine
    {Image i;auto s=i.sampler();auto p=readPorter(s);
        CHECK(p.readable&&p.player==i.base+0x2800&&p.entity==i.base+0x2a00&&p.position==std::array<double,3>({1,2,3})&&p.rotation[4]==1);}
    {Image i;i.put<double>(0x2ad0,std::numeric_limits<double>::quiet_NaN());auto s=i.sampler();auto p=readPorter(s);CHECK(!p.readable&&!p.player);}
    {Image i;i.put<int32_t>(0x2e04,2);auto s=i.sampler();CHECK(!readPorter(s).readable);}
    {Image i;auto s=i.sampler();s.signatures.clear();CHECK(!readPorter(s).readable);}
    // Pinned code identity on a fake image.
    {Image i;CHECK(i.verify().empty());}
    {Image i;i.bytes[0x1210]^=1;CHECK(has(i.verify(),"PlayerGame teleport signature missing"));}
    {Image i;std::copy(i.code[1].begin(),i.code[1].end(),i.bytes.begin()+0x1800);CHECK(has(i.verify(),"entity teleport signature moved"));}
    {Image i;std::fill(i.bytes.begin()+0x1600,i.bytes.begin()+0x1628,0xcc);std::copy(i.code[2].begin(),i.code[2].end(),i.bytes.begin()+0x1700);
        CHECK(has(i.verify(),"ground placement signature moved"));} // present once, but not where it is pinned
    {Image i;i.put<int32_t>(0x1200+0x7f+1,0);CHECK(has(i.verify(),"calls something else"));}
    {Image i;i.bytes[0x1200+0x7f]=0x90;CHECK(has(i.verify(),"call site differs"));}
    {Image i;i.put<uint64_t>(0x2000+0xa8,i.base+0x1400);CHECK(has(i.verify(),"PlayerGame vtable differs"));}
    {Image i;i.put<uint64_t>(0x2100,i.base+0x1800);CHECK(has(i.verify(),"DSPlayerEntity vtable differs"));}
    {Image i;auto s=i.sampler();CHECK(movableIdentity(s,i.base+0x2800,i.base+0x2a00,i.sites));
        CHECK(!movableIdentity(s,i.base+0x2800,i.base+0x2a08,i.sites));CHECK(!movableIdentity(s,0,i.base+0x2a00,i.sites));
        i.put<uint64_t>(0x2a00,i.base+0x2000);CHECK(!movableIdentity(s,i.base+0x2800,i.base+0x2a00,i.sites));}
    if(!failures)std::puts("join decisions, native move and read-only progress discovery passed");
    return failures?1:0;
}
