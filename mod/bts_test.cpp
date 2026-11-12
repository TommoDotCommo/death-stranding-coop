#include "bts.h"
#include <cstdio>
using namespace mod::bts;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"bt oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Gazer gazer(uint8_t n,float life,uint32_t flags=0){Gazer g;g.address=0x1000+n;g.uuid[0]=n;g.uuid[9]=n;g.life=life;g.flags=flags;return g;}
int main(){
    // A gazer seen alive and then destroyed is news once; the first look learns; one never seen alive is not news.
    { GazerSync s;CHECK(s.observe({gazer(1,300),gazer(2,0)}).empty());
      CHECK(s.observe({gazer(1,300),gazer(2,0),gazer(3,0)}).empty());
      auto d=s.observe({gazer(1,0),gazer(2,0),gazer(3,0)});CHECK(d.size()==1&&d[0]==key(gazer(1,1).uuid));
      CHECK(s.observe({gazer(1,0)}).empty());
      CHECK(s.observe({gazer(1,800)}).empty());CHECK(s.observe({gazer(1,-1)}).size()==1); }      // back, destroyed again
    // The game's own short-lived gazers are not shared; a whole area gone at once is not a kill.
    { GazerSync s;s.observe({gazer(1,300,1u<<20),gazer(2,300)});auto d=s.observe({gazer(1,0,1u<<20),gazer(2,0)});CHECK(d.size()==1&&d[0]==key(gazer(2,1).uuid)); }
    { GazerSync s;std::vector<Gazer> a,b;for(uint8_t i=1;i<=MassDeath+1;++i){a.push_back(gazer(i,300));b.push_back(gazer(i,0));}
      s.observe(a);CHECK(s.observe(b).empty());
      GazerSync t;a.pop_back();b.pop_back();t.observe(a);CHECK(t.observe(b).size()==MassDeath); }
    // On joining: the gazers destroyed now, not the game's short-lived ones.
    { auto all=GazerSync::everything({gazer(1,300),gazer(2,0),gazer(3,0,1u<<20),gazer(4,-1)});
      CHECK(all.size()==2&&all[0]==key(gazer(2,1).uuid)&&all[1]==key(gazer(4,1).uuid)); }
    // The partner's kill applies to a living gazer known here.
    { std::vector<Gazer> v{gazer(1,300),gazer(2,0)};
      auto* g=GazerSync::wanted(v,key(gazer(1,1).uuid));CHECK(g&&g->address==0x1001);
      CHECK(!GazerSync::wanted(v,key(gazer(2,1).uuid)));CHECK(!GazerSync::wanted(v,key(gazer(9,1).uuid))); }
    // What this side destroyed for the partner is not sent back, once.
    { GazerSync s;s.observe({gazer(1,300),gazer(2,300)});s.applied(key(gazer(1,1).uuid));
      auto d=s.observe({gazer(1,0),gazer(2,0)});CHECK(d.size()==1&&d[0]==key(gazer(2,1).uuid));
      s.observe({gazer(1,300)});CHECK(s.observe({gazer(1,0)}).size()==1); }
    // Fights: a start is news at once and again while it runs; a newly cleared sphere is news; the first look learns.
    { FightSync s;Fights idle{true,0,2},on{true,1,2},won{true,0,3},fewer{true,0,1},blind{};
      CHECK(s.observe(on).empty());CHECK(s.observe(idle).empty());
      auto a=s.observe(on);CHECK(a.size()==1&&a[0]==FightOn);
      int repeats=0;for(int i=0;i<FightSync::RepeatLooks*2;++i)for(auto w:s.observe(on))if(w==FightOn)++repeats;
      CHECK(repeats==2);
      CHECK(s.observe(blind).empty());
      auto b=s.observe(won);CHECK(b.size()==1&&b[0]==AreaCleared);CHECK(s.observe(won).empty());
      CHECK(s.observe(fewer).empty());CHECK(s.observe(idle).size()==1); }
    // What the partner's news asks: a fight only when none runs here and the partner is near; an end only of a running one.
    { Fights idle{true,0,0},on{true,1,0},blind{};
      CHECK(FightSync::wanted(idle,FightOn,FightRange));CHECK(!FightSync::wanted(idle,FightOn,FightRange+1));CHECK(!FightSync::wanted(on,FightOn,10));
      CHECK(FightSync::wanted(on,AreaCleared,1e9));CHECK(!FightSync::wanted(idle,AreaCleared,0));
      CHECK(!FightSync::wanted(blind,FightOn,0));CHECK(!FightSync::wanted(on,GazerDead,0)); }
    // One health bar: what one catcher loses is news once; what is taken off for the partner is not sent back; a
    // catcher is never taken to zero from outside; a new fight learns first.
    { auto c=[](float p){Catcher x;x.life=0x5000;x.percent=p;return x;};
      HealthSync s;CHECK(s.observe({c(100)}).empty());CHECK(s.observe({c(100)}).empty());
      auto a=s.observe({c(80)});CHECK(a.size()==1&&a[0].ordinal==0&&a[0].percent>19.9f&&a[0].percent<20.1f);CHECK(s.observe({c(80)}).empty());
      CHECK(s.observe({c(85)}).empty());CHECK(s.observe({c(85)}).empty());                                    // life back up is not news
      auto w=s.applied({c(85)},{0,30});CHECK(w&&*w>54.9f&&*w<55.1f);CHECK(s.observe({c(55)}).empty());
      auto h=s.observe({c(50)});CHECK(h.size()==1&&h[0].percent>4.9f&&h[0].percent<5.1f);                      // the local hit after it
      auto f=s.applied({c(50)},{0,70});CHECK(f&&*f==HealthSync::Floor);
      CHECK(!s.applied({c(HealthSync::Floor)},{0,5}));CHECK(!s.applied({c(50)},{1,5}));CHECK(!s.applied({c(50)},{0,-5}));
      CHECK(s.observe({c(50),c(100)}).empty());CHECK(s.observe({c(50),c(90)}).size()==1);                      // a second catcher: learn, then news
      CHECK(s.observe({c(100)}).empty()); }
    if(failures){std::fprintf(stderr,"bt oracle: %d failures\n",failures);return 1;}
    std::puts("bts ok");return 0;
}
