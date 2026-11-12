#include "orders.h"
#include <cstdio>
using namespace mod::orders;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"order oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Order order(uint64_t id,uint16_t state,uint8_t result=1,uint8_t detail=2){Order o;o.id=id;o.state=state;o.result=result;o.detail=detail;return o;}
int main(){
    // On connecting, every standard order under way is reported; story orders never.
    { Sync s;auto a=s.observe({order(1,Progress,2),order(2,Available),order(3,Progress,2,0),order(4,Progress,2,3)});
      CHECK(a.size()==2&&a[0].id==1&&a[0].what==Start&&a[1].id==4);
      CHECK(s.observe({order(1,Progress,2),order(2,Available),order(3,Progress,2,0),order(4,Progress,2,3)}).empty());
      auto b=s.observe({order(1,Available,Completed),order(2,Progress,2),order(3,Available,Completed,0),order(4,Available,1,3)});
      CHECK(b.size()==3&&b[0].id==1&&b[0].what==Success&&b[1].id==2&&b[1].what==Start&&b[2].id==4&&b[2].what==Fail);
      s.reset();auto c=s.observe({order(2,Progress,2)});CHECK(c.size()==1&&c[0].what==Start); }
    // The partner's change applies only where it fits; what was done for the partner is not sent back, once.
    { Sync s;std::vector<Order> v{order(1,Available),order(2,Progress,2),order(3,Available,1,0)};s.observe(v);
      CHECK(s.wanted(v,{1,Start}));CHECK(!s.wanted(v,{2,Start}));CHECK(!s.wanted(v,{3,Start}));CHECK(!s.wanted(v,{9,Start}));
      CHECK(s.wanted(v,{2,Success}));CHECK(s.wanted(v,{2,Fail}));CHECK(!s.wanted(v,{1,Success}));
      s.applied({1,Start});CHECK(s.observe({order(1,Progress,2),order(2,Progress,2)}).empty());
      s.applied({2,Success});auto d=s.observe({order(1,Available,1),order(2,Available,Completed)});
      CHECK(d.size()==1&&d[0].id==1&&d[0].what==Fail);                               // given up here by the player: news
      auto e=s.observe({order(1,Progress,2),order(2,Available,Completed)});CHECK(e.size()==1&&e[0].what==Start); }
    // Units: one that goes from not delivered to delivered in a shared order under way is news, once; the partner's
    // hand-in marks a waiting unit only, and is not sent back.
    { auto unit=[](uint64_t m,uint8_t n,uint16_t st){Unit u;u.mission=m;u.unit=n;u.state=st;u.stateAt=0x1000+n;return u;};
      UnitSync s;std::vector<uint64_t> on{7};
      CHECK(s.observe({unit(7,0,1),unit(7,1,2),unit(9,0,1)},on).empty());
      auto a=s.observe({unit(7,0,2),unit(7,1,2),unit(9,0,2)},on);CHECK(a.size()==1&&a[0].mission==7&&a[0].unit==0);
      CHECK(s.observe({unit(7,0,2),unit(7,1,2)},on).empty());
      std::vector<Unit> v{unit(7,2,1),unit(7,3,2)};
      auto* w=s.wanted(v,7,2);CHECK(w&&w->stateAt==0x1002);CHECK(!s.wanted(v,7,3));CHECK(!s.wanted(v,7,5));CHECK(!s.wanted(v,8,2));
      s.observe(v,on);s.applied(7,2);CHECK(s.observe({unit(7,2,2),unit(7,3,2)},on).empty());
      CHECK(UnitSync::key(0x372d7,3)==0x30000000372d7ull); }
    if(!failures)std::puts("order sync checks passed");
    return failures?1:0;
}
