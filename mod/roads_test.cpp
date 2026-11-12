#include "roads.h"
#include <cstdio>
using namespace mod::roads;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"road oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Paver paver(uint32_t id,uint32_t level,Set own={},Set net={}){Paver p;p.address=0x1000+id;p.id=id;p.level=level;p.own=own;p.net=net;return p;}
int main(){
    // What this player delivers is news per material, once; what others delivered is not; the first look learns.
    { Sync s;CHECK(s.observe({paver(1,1,{0,0,50,0,0,0}),paver(2,2)}).empty());
      auto a=s.observe({paver(1,1,{20,0,170,0,0,0},{0,0,500,0,0,0}),paver(2,2),paver(3,1,{9,0,0,0,0,0})});
      CHECK(a.size()==2&&a[0].id==1&&a[0].item==Material&&a[0].amount==20&&a[1].item==Material+2&&a[1].amount==120);
      CHECK(s.observe({paver(1,1,{20,0,170,0,0,0},{0,0,900,0,0,0}),paver(2,2),paver(3,1,{9,0,0,0,0,0})}).empty());
      CHECK(s.observe({paver(1,1,{20,0,100,0,0,0})}).empty());                                   // a number going down is not a delivery
      CHECK(s.observe({paver(1,1,{20,0,100+Most+1,0,0,0})}).empty()); }                           // nor is nonsense
    // Several pavers changed at one look is a loaded save, not deliveries: nothing is sent.
    { Sync s;s.observe({paver(1,1),paver(2,1),paver(3,1)});
      CHECK(s.observe({paver(1,2,{500,0,0,0,0,0}),paver(2,1,{0,0,70,0,0,0}),paver(3,1)}).empty());
      auto a=s.observe({paver(1,2,{500,0,0,0,0,0}),paver(2,1,{0,0,90,0,0,0}),paver(3,1)});CHECK(a.size()==1&&a[0].amount==20);
      s.forget();CHECK(s.observe({paver(1,2,{900,0,0,0,0,0})}).empty()); }
    // A road built here is news once; one completed here for the partner is not sent back.
    { Sync s;s.observe({paver(1,1),paver(2,1),paver(3,2)});
      s.applied(2);CHECK(s.observe({paver(1,1),paver(2,2),paver(3,2)}).empty());
      auto a=s.observe({paver(1,2),paver(2,2),paver(3,2)});CHECK(a.size()==1&&a[0].id==1&&a[0].item==Built);
      CHECK(s.observe({paver(1,2),paver(2,2),paver(3,3)}).empty());
      s.observe({paver(2,1)});CHECK(s.observe({paver(2,2)}).size()==1); }
    // The partner's news applies to a known paver; "built" only where the road is not built; amounts must make sense.
    { std::vector<Paver> v{paver(1,1),paver(2,2)};
      CHECK(Sync::wanted(v,{1,Built,0})==&v[0]);CHECK(!Sync::wanted(v,{2,Built,0}));CHECK(!Sync::wanted(v,{9,Built,0}));
      CHECK(Sync::wanted(v,{1,Material+5,10})==&v[0]);CHECK(Sync::wanted(v,{2,Material,10})==&v[1]);
      CHECK(!Sync::wanted(v,{1,Material+6,10}));CHECK(!Sync::wanted(v,{1,Material,0}));CHECK(!Sync::wanted(v,{1,Material,-5}));
      CHECK(!Sync::wanted(v,{1,Material,Most+1}));CHECK(!Sync::wanted(v,{1,7,10})); }
    if(failures){std::fprintf(stderr,"road oracle: %d failures\n",failures);return 1;}
    std::puts("roads ok");return 0;
}
