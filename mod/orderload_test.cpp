#include "orderload.h"
#include <cstdio>
using namespace mod::orderload;
namespace cargo=mod::cargo;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"order load oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static cargo::Record rec(uint64_t mission,uint8_t unit,uint8_t where,uint32_t index){cargo::Record r;r.mission=mission;r.unit=unit;r.slot=handle(mission,unit);r.state=uint32_t(where)<<8|1;r.index=index;r.hash=77;return r;}
static Case made(uint64_t mission,uint8_t unit,uint64_t id){Case c;c.tag.mission=mission;c.tag.unit=unit;c.tag.type=TypeStandard;c.tag.item=31;c.id=id;c.name=77;return c;}
int main(){
    const uint64_t M=0x372d7;
    // The tag: a standard order's case travels and comes back the same; story cargo, another type, ids that do not fit
    // and numbers no sender makes are refused.
    { cargo::Placement p;p.mission=M;p.unit=3;p.type=2;p.item=0xabc;p.flags=0x280000;p.flags2=0x1002;p.life[0]=900;p.life[1]=450;p.id=0x1234567890123ull;
      auto t=tagOf(p);CHECK(t&&t->unit==3&&t->flags==(FlagStandard|FlagResourceA|FlagResourceB)&&t->link==0x0123);
      auto w=encode(*t);CHECK(w.ref<(1ull<<53)&&w.position[0]<=100000);
      auto d=decode(w);CHECK(d&&d->mission==M&&d->unit==3&&d->type==2&&d->item==0xabc&&d->flags==t->flags&&d->life[0]==900&&d->life[1]==450&&d->link==0x0123);
      auto q=p;q.flags|=0x100000;CHECK(!tagOf(q));q=p;q.type=1;CHECK(!tagOf(q));q=p;q.mission=0;CHECK(!tagOf(q));q=p;q.mission=1ull<<62|5;CHECK(!tagOf(q));q=p;q.item=0;CHECK(!tagOf(q));
      q=p;q.life[0]=-5;q.life[1]=1e9f;auto u=tagOf(q);CHECK(u&&u->life[0]==-1&&u->life[1]==-1);
      auto bad=w;bad.position[0]=3+64*FlagStory+1024*2;CHECK(!decode(bad));           // story cargo
      bad=w;bad.position[0]=3+1024*5;CHECK(!decode(bad));                             // another type
      bad=w;bad.position[0]=3.5+1024*2;CHECK(!decode(bad));bad=w;bad.position[0]=70000;CHECK(!decode(bad));bad=w;bad.position[0]=-1;CHECK(!decode(bad));
      bad=w;bad.position[1]=-3;CHECK(!decode(bad));bad=w;bad.ref=0;CHECK(!decode(bad));bad=w;bad.ref|=1ull<<50;CHECK(!decode(bad));bad=w;bad.item=0;CHECK(!decode(bad));
      CHECK(belongs(*d,p.id)&&!belongs(*d,p.id+1));
      CHECK(handle(M,3)==0x30000000372d7ull); }
    // Strip: in the game that took the order on for the partner, the game's own cases go while they lie in the world;
    // one on the porter stays; the taker's own load is never touched; a case the partner put down is never removed.
    { Loads l;std::vector<cargo::Record> v{rec(M,0,cargo::InWorld,10),rec(M,1,cargo::InWorld,11),rec(M,2,cargo::OnPorter,12),rec(9,0,cargo::InWorld,13),rec(0,0,cargo::InWorld,14)};
      CHECK(l.strip(v,0).empty()&&!l.own(v[0]));                                       // nothing known as mirrored
      l.started(M,false);CHECK(l.strip(v,0).empty());                                  // this player took it: his load
      l.started(M,true);unsigned left=0;auto a=l.strip(v,1000,&left);
      CHECK(a.size()==2&&a[0]==10&&a[1]==11&&left==1&&l.own(v[0])&&l.own(v[2])&&!l.own(v[3])&&!l.own(v[4]));
      CHECK(l.strip(v,1500).empty());                                                  // asked a moment ago
      CHECK(l.strip(v,1000+Loads::AskAgain).size()==2);                                // still there: asked again
      // the partner puts unit 0 down: made here once this game's own unit 0 is gone, then it is shared cargo
      l.want(made(M,0,501),3000);CHECK(l.due(v,3000).empty());                         // own record still exists
      std::vector<cargo::Record> gone{v[1],v[2]};auto d=l.due(gone,3100);CHECK(d.size()==1&&d[0].id==501);
      l.tried(501,false,3100);CHECK(l.due(gone,3200).empty());CHECK(l.due(gone,3100+Loads::Retry).size()==1);   // refused: again later
      l.tried(501,true,3700);CHECK(l.waiting()==0&&l.due(gone,5000).empty());
      std::vector<cargo::Record> with{rec(M,0,cargo::InWorld,20),v[1]};
      auto b=l.strip(with,9000);CHECK(b.size()==1&&b[0]==11&&!l.own(with[0])&&l.own(with[1]));
      // the order ends; taken again by this player himself, nothing of it is removed
      l.ended(M);CHECK(l.strip(with,20000).empty()&&!l.mirrored(M));
      l.started(M,true);l.ended(M);l.started(M,false);CHECK(l.strip(with,30000).empty()); }
    // An order that ended unseen (the partner was away) is no longer mirrored; one just started is given time to start.
    { Loads l;l.started(M,true,1000);l.underWay({},2000);CHECK(l.mirrored(M));l.underWay({M},9000);CHECK(l.mirrored(M));
      l.underWay({7},9000+Loads::Grace);CHECK(!l.mirrored(M)&&l.strip({rec(M,0,cargo::InWorld,1)},20000).empty()); }
    // A case that cannot be made (this game still holds the unit) is given up after a while; one the partner took
    // back before it was made is dropped; a newer case of a unit replaces the waiting one.
    { Loads l;std::vector<cargo::Record> mine{rec(M,1,cargo::OnPorter,5)};
      l.want(made(M,1,601),0);CHECK(l.due(mine,100).empty()&&l.due(mine,Loads::GiveUp).empty()&&l.takeGivenUp().empty());
      CHECK(l.due(mine,Loads::GiveUp+1).empty());auto g=l.takeGivenUp();CHECK(g.size()==1&&g[0].id==601&&l.waiting()==0);
      l.want(made(M,2,602),0);CHECK(l.cancel(602)&&!l.cancel(602)&&l.due({},100).empty());
      l.want(made(M,3,603),0);l.want(made(M,3,604),50);auto d=l.due({},100);CHECK(d.size()==1&&d[0].id==604);
      l.want(made(M,4,605),0);l.ended(M);CHECK(l.waiting()==0); }
    // A handed-in case is not a put-down: its unit is delivered here. And a case of a unit that is delivered in this
    // game is not made, whatever the partner sent.
    { cargo::Placement p;p.mission=M;p.unit=3;p.type=TypeStandard;std::set<uint64_t> done{handle(M,3)};
      CHECK(handedIn(p,done)&&!handedIn(p,{})&&!handedIn(p,{handle(M,2)}));p.unit=2;CHECK(!handedIn(p,done));
      cargo::Placement plain;CHECK(!handedIn(plain,done)&&!handedIn(plain,{0}));
      Loads l;l.want(made(M,3,701),0);l.want(made(M,1,702),0);auto d=l.due({},100,done);
      CHECK(d.size()==1&&d[0].id==702&&l.waiting()==1);auto dropped=l.takeDelivered();CHECK(dropped.size()==1&&dropped[0].id==701&&l.takeDelivered().empty()); }
    if(!failures)std::puts("order load checks passed");
    return failures?1:0;
}
