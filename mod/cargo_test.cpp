#include "cargo.h"
#include <cstdio>
using namespace mod::cargo;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"cargo oracle: %s\n",#x);++failures;}}while(0)
Record rec(uint32_t slot,uint32_t hash,double x,double y=0,uint8_t where=0){Record r;r.slot=slot;r.hash=hash;r.position={x,y,100};r.state=uint32_t(where)<<8|7;r.index=slot+100;return r;}
Record world(uint32_t slot,uint32_t hash,double x,double y=0){return rec(slot,hash,x,y,InWorld);}
Record carried(uint32_t slot,uint32_t hash,double x,double y=0){return rec(slot,hash,x,y,OnPorter);}
int main(){
    std::array<double,3> porter{0,0,100},moved{2,0,100};
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
    // Put down from the cargo menu (live 2026-11-07): carried, the record holds a carry-frame position near (100,100,20);
    // after the put-down it stays there for over a second before the game writes the real place. Announced only there.
    { Sync s;s.observe({},porter,0);Record c=carried(3,30,100);c.position={100,100,20};s.observe({c},porter,100);s.observe({c},porter,500);
      Record w=c;w.state=uint32_t(InWorld)<<8|7;
      for(uint64_t t=600;t<=3000;t+=200)CHECK(s.observe({w},porter,t).empty());     // stale carry frame: never announced
      CHECK(s.observe({world(3,30,0.8)},porter,3100).empty());
      auto a=s.observe({world(3,30,0.8)},porter,4200);CHECK(a.size()==1&&a[0].position[0]==0.8&&a[0].position[2]==100); }
    // Carried far from the porter's place (the carry frame) and still there after a pick-up of a fresh item: not ours.
    { Sync s;s.observe({},porter,0);Record c=carried(9,90,100);c.position={100,100,20};s.observe({c},porter,100);
      Record w=c;w.state=uint32_t(InWorld)<<8|7;w.position={101,100,20};   // moved within the frame, still far away
      for(uint64_t t=200;t<=4000;t+=200)CHECK(s.observe({w},porter,t).empty()); }
    // Story cargo (kind 5, the president's corpse) stays local: a put-down is not announced, a pick-up is not reported,
    // and a partner's pick-up by item and place never removes it (live 2026-11-07, after a reload).
    { Sync s;s.observe({},porter,0);Record c=world(5,55,1.0);c.state=uint32_t(InWorld)<<8|5;
      s.observe({c},porter,100);CHECK(s.observe({c},porter,1500).empty());auto k=s.takeKept();CHECK(k.size()==1&&k[0].hash==55&&k[0].kind==5&&!k[0].pickedUp);
      CHECK(s.observe({c},porter,3000).empty()&&s.takeKept().empty());                                // noted once
      CHECK(!s.partnerTook(0,55,{1.0,0,100}));
      Record h=c;h.state=uint32_t(OnPorter)<<8|5;s.observe({h},porter,3100);
      CHECK(s.takeTaken().empty());k=s.takeKept();CHECK(k.size()==1&&k[0].pickedUp); }
    { Sync s;Record c=world(5,55,1.0);c.state=uint32_t(InWorld)<<8|5;s.observe({c},porter,0);   // there at the first look
      Record h=c;h.state=uint32_t(OnPorter)<<8|5;s.observe({h},porter,100);CHECK(s.takeTaken().empty()); }
    // Picking up a shared item tells the partner once; putting it down again is a new placement with a new id.
    { Sync s;s.setIdBase(50);s.observe({},porter,0);s.observe({world(4,40,1)},porter,100);
      auto a=s.observe({world(4,40,1)},porter,1200);CHECK(a.size()==1&&a[0].id==51);
      CHECK(s.takeTaken().empty());
      s.observe({carried(4,40,0.2)},porter,1300);CHECK(s.takeTaken().empty());          // held back until he moves
      s.observe({carried(4,40,0.2)},moved,1350);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==51&&t[0].position[0]==1);
      s.observe({carried(4,40,0.2)},porter,1400);CHECK(s.takeTaken().empty()&&s.shared()==0);   // once
      s.observe({world(4,40,2)},porter,1500);auto b=s.observe({world(4,40,2)},porter,2600);CHECK(b.size()==1&&b[0].id==52); }
    // The partner's item (mirror) picked up here: taken with the partner's id.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},777},100);
      s.observe({world(6,60,2.3)},porter,200);CHECK(s.observe({world(6,60,2.3)},porter,2000).empty()&&s.mirrored()==1&&s.shared()==1);
      s.observe({carried(6,60,0.2)},porter,2100);s.observe({carried(6,60,0.2)},moved,2150);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==777&&s.mirrored()==0); }
    // The partner took an item: its record index here, once; never reported back when it disappears.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},888},100);
      s.observe({world(6,60,2.3)},porter,200);s.observe({world(6,60,2.3)},porter,2000);
      CHECK(!s.partnerTook(999)&&!s.partnerTook(0));
      auto i=s.partnerTook(888);CHECK(i&&*i==106);CHECK(!s.partnerTook(888));
      s.observe({},porter,2100);CHECK(s.takeTaken().empty()); }
    // The cargo menu stages the items next to the porter like a pick-up (location 2, carry frame) and puts back what he
    // leaves (live 2026-11-07): nothing is reported, and walking away afterwards announces nothing either.
    auto staged=[](uint32_t slot,uint32_t hash){Record c=carried(slot,hash,0);c.position={99.6,100.7,20};return c;};
    auto back=[](uint32_t slot,uint32_t hash){Record c=world(slot,hash,0);c.position={99.6,100.7,20};return c;};
    { Sync s;s.setIdBase(10);s.observe({},porter,0);s.observe({world(3,30,1)},porter,100);CHECK(s.observe({world(3,30,1)},porter,1200).size()==1);
      for(uint64_t t=1300;t<=9000;t+=500)s.observe({staged(3,30)},porter,t);              // a long look at the menu
      s.observe({back(3,30)},porter,9100);s.observe({back(3,30)},porter,10500);            // carry frame for a while
      s.observe({world(3,30,1)},porter,10600);CHECK(s.takeTaken().empty());
      std::array<double,3> away{8,0,100};
      for(uint64_t t=10700;t<=14000;t+=300)CHECK(s.observe({world(3,30,1)},away,t).empty());
      CHECK(s.takeTaken().empty()&&s.shared()==1); }                                       // still shared, same id
    { Sync s;s.observe({world(3,30,1)},porter,0);                                          // learned at the first look
      s.observe({staged(3,30)},porter,100);s.observe({world(3,30,1)},porter,200);
      std::array<double,3> away{8,0,100};
      for(uint64_t t=300;t<=5000;t+=300)CHECK(s.observe({world(3,30,1)},away,t).empty());
      CHECK(s.takeTaken().empty()); }
    // Staged, and the partner took theirs meanwhile: ours is deleted once the menu puts it back.
    { Sync s;s.setIdBase(10);s.observe({},porter,0);s.observe({world(3,30,1)},porter,100);s.observe({world(3,30,1)},porter,1200);
      s.observe({staged(3,30)},porter,1300);CHECK(!s.partnerTook(11));
      s.observe({world(3,30,1)},porter,1400);auto d=s.takeDoomed();CHECK(d.size()==1&&d[0]==103&&s.takeTaken().empty());
      CHECK(!s.partnerTook(11)); }
    // Staged and then put down elsewhere from the menu: taken from its old place, placed at the new one.
    { Sync s;s.setIdBase(10);s.observe({},porter,0);s.observe({world(3,30,1)},porter,100);s.observe({world(3,30,1)},porter,1200);
      s.observe({staged(3,30)},porter,1300);s.observe({back(3,30)},porter,1400);CHECK(s.takeTaken().empty());
      s.observe({world(3,30,0,2)},porter,1500);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==11&&t[0].position[0]==1);
      auto a=s.observe({world(3,30,0,2)},porter,2600);CHECK(a.size()==1&&a[0].id==12&&a[0].position[1]==2); }
    // Staged and used up (gone from the records): taken.
    { Sync s;s.setIdBase(10);s.observe({},porter,0);s.observe({world(3,30,1)},porter,100);s.observe({world(3,30,1)},porter,1200);
      s.observe({staged(3,30)},porter,1300);s.observe({},porter,1400);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==11); }
    // Both picked it up at once: ours stays with us (nothing to delete here).
    { Sync s;s.setIdBase(70);s.observe({},porter,0);s.observe({world(7,70,1)},porter,100);s.observe({world(7,70,1)},porter,1200);
      s.observe({carried(7,70,0.1)},porter,1300);s.takeTaken();CHECK(!s.partnerTook(71));
      s.observe({carried(7,70,0.1)},moved,1400);CHECK(s.takeTaken().empty()&&s.takeDoomed().empty()); }   // theirs is gone already
    // A shared item that vanishes near the porter (used here) is taken; far away it is not.
    { Sync s;s.setIdBase(80);s.observe({},porter,0);s.observe({world(8,80,1)},porter,100);s.observe({world(8,80,1)},porter,1200);
      s.observe({},porter,1300);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==81);
      Sync u;u.setIdBase(90);u.observe({},porter,0);u.observe({world(9,90,1)},porter,100);u.observe({world(9,90,1)},porter,1200);
      std::array<double,3> far{500,0,100};u.observe({},far,1300);CHECK(u.takeTaken().empty()); }
    // A partner item that already exists at the first look is still recognised as theirs (its pick-up is reported).
    { Sync s;s.expect({60,{2,0,100},555},0);s.observe({world(6,60,2.3)},porter,100);CHECK(s.mirrored()==1);
      s.observe({carried(6,60,0.2)},porter,200);s.observe({carried(6,60,0.2)},moved,250);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==555); }
    // Placement and pick-up close together: the copy that appears after the "taken" is deleted, and never reported.
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},444},100);CHECK(!s.partnerTook(444));
      s.observe({world(6,60,2.3)},porter,200);auto d=s.takeDoomed();CHECK(d.size()==1&&d[0]==106);
      s.observe({},porter,300);CHECK(s.takeTaken().empty()&&s.takeDoomed().empty()); }
    // After a restart (no shared ids known here): a pick-up from the ground is still reported with item and place, and
    // the partner's report finds the nearest same item on the ground within 0.5 m.
    { Sync s;s.observe({world(1,10,1),world(2,10,3)},porter,0);
      s.observe({carried(1,10,0.2),world(2,10,3)},porter,100);s.observe({carried(1,10,0.2),world(2,10,3)},moved,150);auto t=s.takeTaken();CHECK(t.size()==1&&t[0].id==0&&t[0].hash==10&&t[0].position[0]==1);
      Sync r;r.observe({world(5,10,1.2),world(6,10,3),world(7,11,1)},porter,0);
      auto i=r.partnerTook(0,10,{1,0,100});CHECK(i&&*i==105);CHECK(!r.partnerTook(0,10,{1,0,100}));   // once
      CHECK(!r.partnerTook(0,10,{9,0,100})&&!r.partnerTook(0,12,{3,0,100})); }
    // With the location byte, standing next to an item and walking away is not a put-down (no announcement).
    { Sync s;s.observe({world(9,90,1)},porter,0);std::array<double,3> away{10,0,100};
      CHECK(s.observe({world(9,90,1)},porter,500).empty()&&s.observe({world(9,90,1)},away,2000).empty()&&s.observe({world(9,90,1)},away,4000).empty()); }
    // A cancelled expectation whose copy never appears is dropped with the expectation (nothing kept for good).
    { Sync s;s.observe({},porter,0);s.expect({60,{2,0,100},333},100);s.partnerTook(333);CHECK(s.pending()==2);
      s.observe({},porter,100+Sync::ExpectFor+1);CHECK(s.pending()==0&&s.takeDoomed().empty()); }
    if(!failures)std::puts("cargo placement, settle, held, mirror, far, pick-up, put-down and taken checks passed");
    return failures?1:0;
}
