#include "join.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <cmath>
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
    if(!failures)std::puts("join decisions and read-only progress discovery passed");
    return failures?1:0;
}
