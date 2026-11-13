#include "carts.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod::carts;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"carts oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    // Fake memory: image at 0x100000 (the manager slot at +Manager), heap above it.
    const uint64_t base=0x100000,heap=0x9000000;std::vector<uint8_t> image(Manager+16),mem(0x480000);
    auto put=[&](uint64_t a,const void* v,size_t n){if(a>=heap)std::memcpy(&mem[a-heap],v,n);else std::memcpy(&image[a-base],v,n);};
    auto q=[&](uint64_t a,uint64_t v){put(a,&v,8);};auto d=[&](uint64_t a,uint32_t v){put(a,&v,4);};
    mod::discovery::Sampler s{[&](uint64_t a,void* o,size_t n){
        if(a>=heap&&a+n<=heap+mem.size()){std::memcpy(o,&mem[a-heap],n);return true;}
        if(a>=base&&a+n<=base+image.size()){std::memcpy(o,&image[a-base],n);return true;}return false;},base};
    const uint64_t manager=heap+0x10000,list=heap+0x100;
    q(base+Manager,manager);q(manager+CarrierCount,0);d(manager+CarrierCount,4);q(manager+CarrierArray,list);
    // Four carriers: an attached cart, a parked cart, a truck (type 3) and a cart whose entity is something else.
    auto cart=[&](int i,uint32_t type,uint32_t id,uint32_t flags,uint64_t vt,std::array<double,3> at){
        uint64_t c=heap+0x1000+0x400*uint64_t(i),e=heap+0x8000+0x400*uint64_t(i);q(list+8*uint64_t(i),c);
        d(c,type);d(c+CarrierId,id);d(c+CarrierFlags,flags);q(c+CarrierEntity,e+0x20);q(e,vt);put(e+Position,at.data(),24);
        std::array<float,9> r{0,0,1, 1,0,0, 0,1,0};put(e+Rotation,r.data(),36);};
    cart(0,2,(9u<<13)|1,0x21,base+EntityVtable,{102,200,10});
    cart(1,2,(9u<<13)|2,0x20,base+EntityVtable,{150,200,10});
    cart(2,3,(9u<<13)|3,0x21,base+EntityVtable,{101,200,10});
    cart(3,2,(9u<<13)|4,0x21,base+0x1234,{101,200,10});
    auto all=read(s,base);
    CHECK(all.size()==2&&all[0].attached&&!all[1].attached&&std::fabs(all[0].position[0]-102)<1e-9);
    // Towed: the attached one only, as an offset in mm; one drawn here for the partner is not sent back.
    auto t=towed(all,{100,200,9.5},{});
    CHECK(t.size()==1&&t[0].offset[0]==2000&&t[0].offset[1]==0&&t[0].offset[2]==500&&t[0].flags==1);
    CHECK(towed(all,{100,200,9.5},{(9u<<13)|1}).empty());
    CHECK(towed(all,{-1000,200,10},{}).empty());   // more than 100 m away: does not fit the wire
    auto back=place(t[0],{10,20,30});CHECK(std::fabs(back.position[0]-12)<1e-9&&std::fabs(back.position[2]-30.5)<1e-9);
    CHECK(std::fabs(back.rotation[2]-1)<1e-3&&std::fabs(back.rotation[3]-1)<1e-3&&std::fabs(back.rotation[7]-1)<1e-3);
    // Nearest first, at most two.
    std::vector<Cart> three(3);for(int i=0;i<3;++i){three[size_t(i)].id=uint32_t(i);three[size_t(i)].attached=true;three[size_t(i)].rotation={1,0,0,0,1,0,0,0,1};three[size_t(i)].position={double(10-3*i),0,0};}
    auto two=towed(three,{0,0,0},{});CHECK(two.size()==2&&two[0].offset[0]==4000&&two[1].offset[0]==7000);
    // Display: one make at a time (the game makes a cart a few frames later), moves once it exists, removes extras,
    // gives up on a cart that never came, clears.
    std::map<uint32_t,int> exists;uint32_t next=100;int made=0,removed=0;
    Api api;api.make=[&](const Place&){++made;return next++;};
    api.move=[&](uint32_t id,const Place&){return exists.count(id)>0;};
    api.remove=[&](uint32_t id){++removed;exists.erase(id);};
    Display shown;Place p;
    shown.show({p,p},api,1000);CHECK(made==1&&shown.count()==1);
    shown.show({p,p},api,1100);CHECK(made==1);                       // the first is not there yet
    exists[100]=1;shown.show({p,p},api,1200);CHECK(made==2&&shown.count()==2);
    shown.show({p,p},api,1200+Display::MakeWaitMs+1);CHECK(removed==1&&made==3&&shown.count()==2);   // 101 never came: removed, asked again
    exists[102]=1;shown.show({p},api,9000);CHECK(shown.count()==1&&removed==2&&shown.ids()==std::set<uint32_t>{100});
    exists.erase(100);shown.show({p},api,9100);CHECK(removed==3&&made==4&&shown.count()==1);   // gone in the game: made again
    shown.clear(api);CHECK(shown.count()==0&&removed==4);
    if(!failures)std::puts("carts read, towed, place and display checks passed");
    return failures?1:0;
}
