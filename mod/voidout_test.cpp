#include "voidout.h"
#include <cmath>
#include <cstdio>
using namespace mod::voidout;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"voidout oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Crater crater(uint32_t id,double x){Crater k;k.id=id;k.position={x,200,10};k.radius=40;k.area=3;k.yaw=1.5f;return k;}
static Table table(uint32_t counter,std::vector<Crater> v){Table t;t.readable=true;t.counter=counter;t.craters=std::move(v);return t;}
int main(){
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
