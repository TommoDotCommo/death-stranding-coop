#include "rewards.h"
#include <cstdio>
#include <cstring>
using namespace mod::rewards;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"reward oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
// A fake image with the residents system, three facilities' resources and their records, the likes state and the
// porter's grade state.
struct World {
    static constexpr uint64_t Base=0x140000000ull,Heap=0x200000000ull,Vtable=Base+0x4e00000;
    static constexpr uint64_t Globals=Base+0x7bf0000;   // the part of the image that holds the three globals
    std::vector<uint8_t> image=std::vector<uint8_t>(0x10000),heap=std::vector<uint8_t>(0x10000);
    mod::discovery::Sampler sampler{[this](uint64_t a,void* p,size_t n){
        auto in=[&](uint64_t b,std::vector<uint8_t>& m){if(a<b||a-b>m.size()||n>m.size()-(a-b))return false;std::memcpy(p,m.data()+(a-b),n);return true;};
        return in(Globals,image)||in(Heap,heap);},Base};
    template<class T> void put(uint64_t a,T v){std::memcpy((a>=Heap?heap.data()+(a-Heap):image.data()+(a-Globals)),&v,sizeof v);}
    static constexpr uint64_t System=Heap+0x100,List=Heap+0x200,Records=Heap+0x300,Array=Heap+0x400,Table=Heap+0x1000,Likes=Heap+0x3000,Grade=Heap+0x4000;
    static uint64_t resource(int i){return Heap+0x800+uint64_t(i)*0x80;}
    static uint64_t entry(int slot){return Table+uint64_t(slot)*0x30;}
    void facility(int i,int slot,uint32_t hash,int32_t points,int32_t level){
        uint64_t r=resource(i);put<uint64_t>(r,Vtable);put<int32_t>(r+0x20,10);
        const int32_t thresholds[5]={100,300,600,1000,1500};for(int k=0;k<5;++k)put<int32_t>(r+0x28+4*uint64_t(k),thresholds[k]);
        put<int32_t>(r+0x3c,500);put<uint32_t>(r+0x40,hash);put<uint64_t>(Array+8*uint64_t(i),r);
        uint64_t e=entry(slot);put<uint32_t>(e,0x80000000u|hash);put<uint32_t>(e+8,hash);put<uint64_t>(e+0x10,r);put<int32_t>(e+0x18,points);put<int32_t>(e+0x1c,level);
    }
    World(){
        put<uint64_t>(Base+0x7bfd948,System);put<uint64_t>(System+0x10,List);put<uint64_t>(System+0x18,Records);
        put<int32_t>(List+0x20,3);put<uint64_t>(List+0x28,Array);put<uint64_t>(Records+0x28,Table);put<uint32_t>(Records+0x34,16);
        facility(0,5,0x1111,250,1);facility(1,9,0x2222,10,0);facility(2,2,0x3333,48500,99);
        put<uint64_t>(Base+0x7bf83b0,Likes);put<int64_t>(Likes+0x18,1234);put<int64_t>(Likes+0x20,77);
        put<uint64_t>(Base+0x7bfd960,Grade);
        for(unsigned i=0;i<11;++i)put<uint32_t>(Grade+0x8b8+4*i,0x9e3779b9u*(i+1)+i);
        const uint64_t experience[5]={5000,0,123456789,42,999999999999ull};const uint32_t level[5]={12,0,60,1,400};
        const unsigned ek[5]={0x8bc,0x8c0,0x8c4,0x8c8,0x8cc},lk[5][2]={{0x8c8,0x8dc},{0x8cc,0x8dc},{0x8d0,0x8dc},{0x8d4,0x8dc},{0x8e0,0x8b8}};
        for(int i=0;i<5;++i){put<uint64_t>(Grade+0x18+8*uint64_t(i),experience[i]^mask64(key(ek[i])^key(0x8d8)));put<uint32_t>(Grade+0x538+4*uint64_t(i),level[i]^key(lk[i][0])^key(lk[i][1]));}
    }
    uint32_t key(unsigned offset){uint32_t k=0;std::memcpy(&k,heap.data()+(Grade-Heap)+offset,4);return k;}
};
static Facility facility(uint32_t hash,int32_t points,int32_t level=0,int32_t cap=48500){Facility f;f.hash=hash;f.points=points;f.level=level;f.cap=cap;f.floor=10;f.resource=0x1000+hash;return f;}
int main(){
    // The reader: every listed facility with its record, sorted by name hash, with the game's clamps.
    { World w;auto f=readFacilities(w.sampler,World::Base);
      CHECK(f.size()==3&&f[0].hash==0x1111&&f[0].points==250&&f[0].level==1&&f[0].floor==10&&f[0].cap==1500+94*500);
      CHECK(f.size()==3&&f[1].hash==0x2222&&f[1].points==10&&f[2].hash==0x3333&&f[2].level==99);
      CHECK(f.size()==3&&f[0].resource==World::resource(0)&&f[0].record==World::entry(5)+0x10);
      w.put<uint64_t>(World::Base+0x7bfd948,0);CHECK(readFacilities(w.sampler,World::Base).empty()); }
    // A resource of another class is not a facility's (its record is left alone).
    { World w;w.put<uint64_t>(World::resource(1),World::Vtable+0x40);auto f=readFacilities(w.sampler,World::Base);CHECK(f.size()==2&&f[1].hash==0x3333);
      w.put<uint64_t>(World::resource(1),World::Heap+0x40);CHECK(readFacilities(w.sampler,World::Base).size()==2); }
    // A record whose resource the system does not list, or whose key is not that resource's name, is not reported.
    { World w;uint64_t e=World::entry(12);w.put<uint32_t>(e,0x80004444u);w.put<uint32_t>(e+8,0x4444u);w.put<uint64_t>(e+0x10,World::Heap+0x5000);w.put<int32_t>(e+0x18,300);
      CHECK(readFacilities(w.sampler,World::Base).size()==3);
      World v;v.put<uint32_t>(World::entry(9)+8,0x9999u);CHECK(readFacilities(v.sampler,World::Base).size()==2); }
    // Numbers out of the game's range are not a connection total.
    { World w;w.put<int32_t>(World::entry(5)+0x18,48501);CHECK(readFacilities(w.sampler,World::Base).size()==2);
      World v;v.put<int32_t>(World::entry(9)+0x1c,100);CHECK(readFacilities(v.sampler,World::Base).size()==2);
      World u;u.put<int32_t>(World::entry(2)+0x18,-1);CHECK(readFacilities(u.sampler,World::Base).size()==2); }
    // The porter's totals come out of the game's masks; a state that does not unmask to the game's range is unreadable.
    { World w;auto p=readPorter(w.sampler,World::Base);
      CHECK(p.readable&&p.likes==1234&&p.likesOnline==77&&p.experience[0]==5000&&p.experience[2]==123456789&&p.experience[4]==999999999999ull);
      CHECK(p.level[0]==12&&p.level[1]==0&&p.level[2]==60&&p.level[4]==400);
      CHECK(mask64(0)==0&&mask64(2)==((2ull<<31)|2)&&mask64(3)==(((0x120400002ull^2)<<31)|3));
      w.put<uint32_t>(World::Grade+0x8d8,w.key(0x8d8)^0x55aa0000u);CHECK(!readPorter(w.sampler,World::Base).readable);
      World v;v.put<uint64_t>(World::Base+0x7bfd960,0);CHECK(!readPorter(v.sampler,World::Base).readable); }
    // The wire: a total and a level, whole and in range; nothing and nonsense are refused.
    { News n{0xdeadbeef,70123,7};auto e=encode(n);CHECK(e.type==WireType&&e.type<=ds::CargoTypeMax);
      for(double x:e.position)CHECK(x>=0&&x<=100000);
      auto d=decode(e);CHECK(d&&d->hash==0xdeadbeef&&d->points==70123&&d->level==7);
      auto bad=e;bad.type=12;CHECK(!decode(bad));bad=e;bad.ref=0;CHECK(!decode(bad));bad=e;bad.ref=1ull<<32;CHECK(!decode(bad));
      bad=e;bad.position={0,0,0};CHECK(!decode(bad));                                              // zero is never written
      bad=e;bad.item=100;CHECK(!decode(bad));bad=e;bad.position[0]=1.5;CHECK(!decode(bad));bad=e;bad.position[0]=-1;CHECK(!decode(bad));
      bad=e;bad.position[1]=double(MaxPoints>>16)+1;CHECK(!decode(bad));bad=e;bad.position[0]=65536;CHECK(!decode(bad));
      CHECK(decode(encode({1,MaxPoints,MaxLevel}))&&decode(encode({1,1,0}))); }
    // A rise here is news once, with the new total; the first look learns; a fall is not news.
    { Sync s;CHECK(s.observe({facility(1,250),facility(2,10)}).empty());
      auto a=s.observe({facility(1,340,2),facility(2,10)});CHECK(a.size()==1&&a[0].hash==1&&a[0].points==340&&a[0].level==2);
      CHECK(s.observe({facility(1,340,2),facility(2,10)}).empty());
      CHECK(s.observe({facility(1,300,2),facility(2,10)}).empty());
      auto b=s.observe({facility(1,300,2),facility(2,10),facility(3,90)});CHECK(b.empty());             // first seen: learnt
      s.forget();CHECK(s.observe({facility(1,900)}).empty()); }
    // What is added here for the partner is not sent back; a rise past his total is news again.
    { Sync s;s.observe({facility(1,250)});s.heard({1,400,3});
      CHECK(s.observe({facility(1,400,3)}).empty());
      auto a=s.observe({facility(1,460,3)});CHECK(a.size()==1&&a[0].points==460);
      s.heard({1,300,2});CHECK(s.observe({facility(1,470,3)}).size()==1);                              // an older total of his does not lower what is known
      s.unlinked();s.heard({1,480,3});CHECK(s.observe({facility(1,480,3)}).empty()); }
    // Many facilities rising in one look is a loaded save: nothing is sent, and it is learnt.
    { Sync s;s.observe({facility(1,10),facility(2,10),facility(3,10),facility(4,10),facility(5,10)});
      CHECK(s.observe({facility(1,500),facility(2,500),facility(3,500),facility(4,500),facility(5,10)}).empty());
      auto a=s.observe({facility(1,500),facility(2,500),facility(3,500),facility(4,500),facility(5,60)});CHECK(a.size()==1&&a[0].hash==5); }
    // Topping up: only what the local facility is short of, never past the game's cap, never down.
    { CHECK(Sync::owed(facility(1,250),{1,400,3})==150);
      CHECK(Sync::owed(facility(1,400),{1,400,3})==0);CHECK(Sync::owed(facility(1,500),{1,400,3})==0);
      CHECK(Sync::owed(facility(1,250,0,300),{1,400,3})==50);CHECK(Sync::owed(facility(1,300,0,300),{1,400,3})==0);
      CHECK(Sync::owed(facility(2,250),{1,400,3})==0);CHECK(Sync::owed(facility(1,250),{1,0,0})==0); }
    if(failures){std::fprintf(stderr,"reward oracle: %d failures\n",failures);return 1;}
    std::puts("rewards ok");return 0;
}
