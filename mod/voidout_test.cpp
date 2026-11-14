#include "voidout.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace mod::voidout;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"voidout oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Crater crater(uint32_t id,double x){Crater k;k.id=id;k.position={x,200,10};k.radius=40;k.area=3;k.yaw=1.5f;return k;}
static Table table(uint32_t counter,std::vector<Crater> v){Table t;t.readable=true;t.counter=counter;t.craters=std::move(v);return t;}
static void nativeWrites(){
    constexpr uint64_t base=0x100000;
    alignas(16) uint8_t manager[0x300]{},records[0x600]{};
    uint64_t c=reinterpret_cast<uint64_t>(manager),list=reinterpret_cast<uint64_t>(records);
    auto put=[&](size_t at,auto value){std::memcpy(manager+at,&value,sizeof value);};
    put(0,base+0x3daf258);put(0x2a0,uint64_t(1));put(0x294,int32_t(2));put(0x298,list);
    bool unreadable=false;
    mod::discovery::Sampler s([&](uint64_t at,void* out,size_t n){
        if(at==base+0x7bee060&&n==8){std::memcpy(out,&c,8);return true;}
        if(unreadable&&at==c)return false;
        if(at>=c&&at-c<=sizeof manager&&n<=sizeof manager-(at-c)){std::memcpy(out,manager+(at-c),n);return true;}
        return false;
    },base);
    uint64_t denied=0;unsigned grows=0,logs=0;
    auto writable=[&](uint64_t at,size_t n){return at!=denied&&((at==c&&n<=sizeof manager)||(at==list&&n<=sizeof records));};
    auto grow=[&](uint64_t,uint64_t,int32_t){++grows;return list;};
    auto add=craterAdder(s,writable,grow,[&](const std::string&){++logs;});
    auto k=crater(0,12);CHECK(add(k)==1);uint32_t id=0;std::memcpy(&id,records+0x48,4);
    CHECK(id==1&&manager[0x290]==1&&manager[0x2ac]==1&&grows==0&&logs==0);
    double position[3]{};float radius=0,yaw=0;uint32_t area=0;
    std::memcpy(position,records+0x18,24);std::memcpy(&radius,records+0x30,4);
    std::memcpy(&area,records+0x50,4);std::memcpy(&yaw,records+0x54,4);
    CHECK(position[0]==12&&position[1]==200&&position[2]==10&&radius==40&&area==3&&yaw==1.5f&&records[0x3c]==1);
    auto refuse=[&]{
        uint8_t before[sizeof manager],old[sizeof records];std::memcpy(before,manager,sizeof manager);std::memcpy(old,records,sizeof records);
        auto g=grows;CHECK(add(k)==0);CHECK(!std::memcmp(before,manager,sizeof manager)&&!std::memcmp(old,records,sizeof records)&&grows==g);
    };
    put(0,uint64_t(0));refuse();refuse();CHECK(logs==1);put(0,base+0x3daf258);
    unreadable=true;refuse();unreadable=false;
    denied=c;refuse();denied=list;refuse();CHECK(logs==1);denied=0;
    // A grow is permitted only with a writable manager and old list, and its result is checked again.
    put(0x294,int32_t(1));denied=c;refuse();denied=list;refuse();denied=0;
    auto badGrow=craterAdder(s,writable,[&](uint64_t,uint64_t,int32_t){++grows;return list+0x60;},[&](const std::string&){++logs;});
    CHECK(badGrow(k)==0&&manager[0x290]==1&&manager[0x2a8]==1&&grows==1);
    CHECK(add(k)==2&&manager[0x290]==2&&grows==2);
}
int main(){
    nativeWrites();
    // New craters are news once; the first look learns; a load (the counter going down) learns again.
    { Sync s;CHECK(s.observe(table(4,{crater(3,0),crater(4,100)})).empty());
      auto a=s.observe(table(5,{crater(3,0),crater(4,100),crater(5,300)}));CHECK(a.size()==1&&a[0].id==5);
      CHECK(s.observe(table(5,{crater(3,0),crater(4,100),crater(5,300)})).empty());
      CHECK(s.observe(table(2,{crater(1,0),crater(2,100)})).empty());                       // a load
      CHECK(s.observe(table(3,{crater(1,0),crater(2,100),crater(3,500)})).size()==1); }
    // What the mod added for the partner is not sent back.
    { Sync s;s.observe(table(4,{}));s.applied(5);CHECK(s.observe(table(6,{crater(5,0),crater(6,50)})).size()==1); }
    // The partner's crater is added only where none stands within 5 m.
    { auto t=table(2,{crater(1,0)});CHECK(!Sync::wanted(t,crater(9,3)));CHECK(Sync::wanted(t,crater(9,6)));CHECK(!Sync::wanted(Table{},crater(9,60))); }
    // The wire keeps place, radius (cm), area, the disabled bit and the yaw.
    { auto k=crater(7,1234.56);k.disabled=true;k.radius=42.37f;auto e=encode(k);auto d=decode(e);
      CHECK(d&&std::fabs(d->radius-42.37f)<0.01f&&d->area==3&&d->disabled&&std::fabs(d->position[0]-1234.56)<1e-9&&std::fabs(d->yaw-1.5f)<1e-6);
      ds::CargoEvent bad=e;bad.item=0;CHECK(!decode(bad));bad=e;bad.type=6;CHECK(!decode(bad)); }
    if(failures){std::fprintf(stderr,"voidout oracle: %d failures\n",failures);return 1;}
    std::puts("voidout ok");return 0;
}
