#include "enemies.h"
#include <cstdio>
using namespace mod::enemies;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"enemy oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Mule mule(uint8_t n,uint32_t state,uint32_t flags=0){Mule m;m.address=0x1000+n;m.uuid[0]=n;m.uuid[9]=n;m.state=state;m.flags=flags;return m;}
int main(){
    CHECK(condition(1,0)==Up&&condition(2,0x2040000)==Up&&condition(5,0)==Up);
    CHECK(condition(4,1u<<19)==Knocked&&condition(4,1u<<20)==Fainted&&condition(4,1u<<21)==Asleep&&condition(3,0)==Dead);
    { std::array<uint8_t,16> u{};for(int i=0;i<16;++i)u[size_t(i)]=0xff;CHECK(key(u)==(uint64_t(1)<<52)-1); }
    // Only a MULE seen standing and then down is news; the first look learns; each defeat once.
    { Sync s;CHECK(s.observe({mule(1,1),mule(2,4,1u<<20)}).empty());
      CHECK(s.observe({mule(1,1),mule(2,4,1u<<20),mule(3,3)}).empty());             // 3 was never seen standing
      CHECK(s.observe({mule(1,4,1u<<21),mule(2,4,1u<<20)}).empty());                 // down at one look: not yet
      auto d=s.observe({mule(1,4,1u<<21),mule(2,4,1u<<20)});CHECK(d.size()==1&&d[0].key==key(mule(1,1).uuid)&&d[0].kind==Asleep);
      CHECK(s.observe({mule(1,4,1u<<21)}).empty());
      CHECK(s.observe({mule(1,1)}).empty());CHECK(s.observe({mule(1,4,1u<<19)}).empty());CHECK(s.observe({mule(1,1)}).empty());   // down for a moment only: never news
      CHECK(s.observe({mule(1,3)}).empty());auto e=s.observe({mule(1,3)});CHECK(e.size()==1&&e[0].kind==Dead); }   // up again, down again
    // What this side put down for the partner is not sent back; the partner's report is applied to a standing MULE only.
    { Sync s;std::vector<Mule> v{mule(1,1),mule(2,4,1u<<19)};s.observe(v);
      Down a{key(mule(1,1).uuid),Fainted},b{key(mule(2,1).uuid),Fainted},c{key(mule(9,1).uuid),Fainted};
      auto* m=s.wanted(v,a);CHECK(m&&m->address==0x1001);CHECK(!s.wanted(v,b));CHECK(!s.wanted(v,c));
      s.applied(a.key);CHECK(s.observe(v).empty());CHECK(s.observe({mule(1,4,1u<<20),mule(2,4,1u<<19)}).empty());
      CHECK(s.observe({mule(1,4,1u<<20),mule(2,4,1u<<19)}).empty());CHECK(s.observe({mule(1,4,1u<<20),mule(2,4,1u<<19)}).empty());
      CHECK(s.observe({mule(1,1),mule(2,4,1u<<19)}).empty());s.observe({mule(1,4,1u<<19),mule(2,4,1u<<19)});auto d=s.observe({mule(1,4,1u<<19),mule(2,4,1u<<19)});CHECK(d.size()==1); }   // later, by this player: news
    // On joining: the MULEs that are down now, with how.
    { auto all=Sync::everything({mule(1,1),mule(2,4,1u<<20),mule(3,3),mule(4,5)});
      CHECK(all.size()==2&&all[0].key==key(mule(2,1).uuid)&&all[0].kind==Fainted&&all[1].kind==Dead); }
    // Camps: one that goes on alert after being seen calm is news, once; the partner's alert applies to a calm camp only.
    { auto camp=[](uint8_t n,uint32_t phase){Camp c;c.address=0x2000+n;c.uuid[1]=n;c.phase=phase;return c;};
      CampSync s;CHECK(s.observe({camp(1,0),camp(2,5)}).empty());
      auto a=s.observe({camp(1,5),camp(2,5),camp(3,5)});CHECK(a.size()==1&&a[0]==key(camp(1,0).uuid));
      CHECK(s.observe({camp(1,5),camp(2,5)}).empty());
      std::vector<Camp> v{camp(1,5),camp(2,0)};CHECK(!s.wanted(v,key(camp(1,0).uuid)));CHECK(!s.wanted(v,key(camp(7,0).uuid)));
      auto* w=s.wanted(v,key(camp(2,0).uuid));CHECK(w&&w->address==0x2002);
      s.observe(v);s.applied(key(camp(2,0).uuid));CHECK(s.observe(v).empty());        // still calm right after the request
      CHECK(s.observe({camp(1,5),camp(2,3)}).empty());CHECK(s.observe({camp(1,5),camp(2,5)}).empty());
      s.observe({camp(1,5),camp(2,0)});auto b=s.observe({camp(1,5),camp(2,5)});CHECK(b.size()==1); }   // calm, then alerted by this player: news
    CHECK(!alarmed(0)&&!alarmed(2)&&alarmed(3)&&alarmed(5)&&!alarmed(6));
    if(!failures)std::puts("enemy condition, key and sync checks passed");
    return failures?1:0;
}
