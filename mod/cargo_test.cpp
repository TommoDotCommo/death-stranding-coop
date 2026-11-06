#include "cargo.h"
#include <cstdio>
using namespace mod::cargo;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"cargo oracle: %s\n",#x);++failures;}}while(0)
Record rec(uint32_t slot,uint32_t hash,double x,double y=0,uint8_t where=0){Record r;r.slot=slot;r.hash=hash;r.position={x,y,100};r.state=uint32_t(where)<<8|7;r.index=slot+100;return r;}
Record world(uint32_t slot,uint32_t hash,double x,double y=0){return rec(slot,hash,x,y,InWorld);}
Record carried(uint32_t slot,uint32_t hash,double x,double y=0){return rec(slot,hash,x,y,OnPorter);}
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
    // Shared ids: each placement gets the next id from the base.
    { Sync s;s.setIdBase(1000);s.observe({},porter,0);s.observe({world(1,10,1),world(2,20,-1)},porter,100);
      auto a=s.observe({world(1,10,1),world(2,20,-1)},porter,1200);CHECK(a.size()==2&&a[0].id==1001&&a[1].id==1002); }
    // A real put-down (porter -> world): announced once it settles, without walking away.
    { Sync s;s.observe({carried(3,30,0.3)},porter,0);s.observe({carried(3,30,0.3)},porter,500);
      CHECK(s.observe({world(3,30,0.8)},porter,600).empty());
      auto a=s.observe({world(3,30,0.8)},porter,1700);CHECK(a.size()==1&&a[0].hash==30&&a[0].id); }
    // Picking up a shared item tells the partner once; putting it down again is a new placement with a new id.
    { Sync s;s.setIdBase(50);s.observe({},porter,0);s.observe({world(4,40,1)},porter,100);
      auto a=s.observe({world(4,40,1)},porter,1200);CHECK(a.size()==1&&a[0].id==51);
      CHECK(s.takeTaken().empty());
      s.observe({carried(4,40,0.2)},porter,1300);auto t=s.takeTaken();CHECK(t.size()==1&&t[0]==51);
      s.observe({carried(4,40,0.2)},porter,1400);CHECK(s.takeTaken().empty()&&s.shared()==0);   // once
      s.observe({world(4,40,2)},porter,1500);auto b=s.observe({world(4,40,2)},porter,2600);CHECK(b.size()==1&&b[0].id==52); }
    // The partner's item (mirror) picked up here: taken with the partner's id.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},777},100);
      s.observe({world(6,60,2.3)},porter,200);CHECK(s.observe({world(6,60,2.3)},porter,2000).empty()&&s.mirrored()==1&&s.shared()==1);
      s.observe({carried(6,60,0.2)},porter,2100);auto t=s.takeTaken();CHECK(t.size()==1&&t[0]==777&&s.mirrored()==0); }
    // The partner took an item: its record index here, once; never reported back when it disappears.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},888},100);
      s.observe({world(6,60,2.3)},porter,200);s.observe({world(6,60,2.3)},porter,2000);
      CHECK(!s.partnerTook(999)&&!s.partnerTook(0));
      auto i=s.partnerTook(888);CHECK(i&&*i==106);CHECK(!s.partnerTook(888));
      s.observe({},porter,2100);CHECK(s.takeTaken().empty()); }
    // Both picked it up at once: ours stays with us (nothing to delete here).
    { Sync s;s.setIdBase(70);s.observe({},porter,0);s.observe({world(7,70,1)},porter,100);s.observe({world(7,70,1)},porter,1200);
      s.observe({carried(7,70,0.1)},porter,1300);s.takeTaken();CHECK(!s.partnerTook(71)); }
    // A shared item that vanishes near the porter (used here) is taken; far away it is not.
    { Sync s;s.setIdBase(80);s.observe({},porter,0);s.observe({world(8,80,1)},porter,100);s.observe({world(8,80,1)},porter,1200);
      s.observe({},porter,1300);auto t=s.takeTaken();CHECK(t.size()==1&&t[0]==81);
      Sync u;u.setIdBase(90);u.observe({},porter,0);u.observe({world(9,90,1)},porter,100);u.observe({world(9,90,1)},porter,1200);
      std::array<double,3> far{500,0,100};u.observe({},far,1300);CHECK(u.takeTaken().empty()); }
    if(!failures)std::puts("cargo placement, settle, held, mirror, far, pick-up, put-down and taken checks passed");
    return failures?1:0;
}
