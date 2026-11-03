#include "cargo.h"
#include <cstdio>
using namespace mod::cargo;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"cargo oracle: %s\n",#x);++failures;}}while(0)
Record rec(uint32_t slot,uint32_t hash,double x,double y=0){Record r;r.slot=slot;r.hash=hash;r.position={x,y,100};return r;}
int main(){
    std::array<double,3> porter{0,0,100};
    // Existing cargo is learnt, never announced.
    { Sync s;CHECK(s.observe({rec(1,10,1),rec(2,20,500)},porter,0).empty());
      CHECK(s.observe({rec(1,10,1),rec(2,20,500)},porter,5000).empty()); }
    // New cargo near the porter (dropped from a menu, or created): announced once after it settles.
    { Sync s;s.observe({},porter,0);
      CHECK(s.observe({rec(3,30,1.5)},porter,100).empty());                          // not settled yet
      auto a=s.observe({rec(3,30,1.5)},porter,1200);CHECK(a.size()==1&&a[0].hash==30&&a[0].position[0]==1.5);
      CHECK(s.observe({rec(3,30,1.5)},porter,3000).empty());                         // once
      CHECK(s.observe({rec(3,30,9)},porter,4000).empty()&&s.observe({rec(3,30,9)},porter,6000).empty()); }   // moved later: still once
    // Cargo the porter carries moves with him: nothing. He walks off and it stays put: announced.
    { Sync s;s.observe({rec(4,40,0.5)},porter,0);
      for(uint64_t t=100;t<=2000;t+=100){std::array<double,3> p{double(t)/100,0,100};CHECK(s.observe({rec(4,40,p[0]+0.5)},p,t).empty());}
      std::array<double,3> walked{30,0,100};
      CHECK(s.observe({rec(4,40,20.5)},walked,2100).empty());                         // just put down: settling
      auto a=s.observe({rec(4,40,20.5)},walked,3200);CHECK(a.size()==1&&a[0].hash==40); }
    // Held but still: the porter standing with it does not drop it.
    { Sync s;s.observe({rec(5,50,1)},porter,0);for(uint64_t t=100;t<=5000;t+=100)CHECK(s.observe({rec(5,50,1)},porter,t).empty()); }
    // The partner's cargo created here is never sent back.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100}},100);
      s.observe({rec(6,60,2.3)},porter,200);CHECK(s.observe({rec(6,60,2.3)},porter,2000).empty()&&s.mirrored()==1);
      Sync u;u.observe({},porter,0);u.expect({60,{2,0,100}},100);
      u.observe({rec(6,60,2.3)},porter,20000);CHECK(u.observe({rec(6,60,2.3)},porter,22000).size()==1); }   // expectation expired: ours
    // New cargo far away (streaming in) is not ours; a slot reused for another item is new.
    { Sync s;s.observe({rec(7,70,1)},porter,0);
      s.observe({rec(8,80,400)},porter,100);CHECK(s.observe({rec(8,80,400)},porter,2000).empty());
      s.observe({rec(7,71,1)},porter,2100);CHECK(s.observe({rec(7,71,1)},porter,3200).size()==1); }
    if(!failures)std::puts("cargo placement, settle, held, mirror and far checks passed");
    return failures?1:0;
}
