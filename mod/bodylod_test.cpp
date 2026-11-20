// Cheaper bodies far away (bodylod.h): the distances with their margin, a body that comes near again put back as the
// game made it, a body hidden for its distance counted as a far player, and the lab's switches first.
#include "bodylod.h"
#include <cmath>
#include <cstdio>
using namespace mod::bodylod;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"bodylod oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
const std::array<double,3> Here{100,5,-40};
Body at(uint64_t key,double metres){return {key,std::array<double,3>{Here[0]+metres*0.6,Here[1],Here[2]+metres*0.8}};}
}
int main(){
    // The margin: on beyond the distance, off again only 5 m inside it; never with 0; at most half a short distance.
    CHECK(!beyond(false,30,30)&&beyond(false,30.1,30));
    CHECK(beyond(true,29,30)&&beyond(true,25.1,30)&&!beyond(true,25,30)&&!beyond(true,10,30));
    CHECK(!beyond(false,1e6,0)&&!beyond(true,1e6,0));
    CHECK(beyond(false,4.1,4)&&beyond(true,2.1,4)&&!beyond(true,1.9,4));   // 4 m: back within 2 m, not never

    // A body walking out and back: reduced beyond 30 m, kept so down to 25 m, full detail again inside it.
    {Policy p;Settings s;
     CHECK(s.detailMetres==30&&s.hideMetres==0);
     for(double m:{10.0,29.0,30.0}){p.tick({at(7,m)},&Here,s);CHECK(!p.state(7).reduced);}
     p.tick({at(7,31)},&Here,s);CHECK(p.state(7).reduced&&p.reduced()==1);
     for(double m:{29.5,30.5,26.0,29.9,25.5}){p.tick({at(7,m)},&Here,s);CHECK(p.state(7).reduced);}   // at the boundary: no flip
     p.tick({at(7,24.9)},&Here,s);CHECK(!p.state(7).reduced&&p.reduced()==0);
     p.tick({at(7,29)},&Here,s);CHECK(!p.state(7).reduced);
     // Never hidden by default.
     p.tick({at(7,5000)},&Here,s);CHECK(p.state(7).reduced&&!p.hidden(7)&&p.hiddenCount()==0);}

    // Hiding, when asked for: beyond its distance, with the same margin; reduced as well.
    {Policy p;Settings s;s.hideMetres=80;
     p.tick({at(1,79),at(2,81)},&Here,s);CHECK(!p.hidden(1)&&p.hidden(2)&&p.hiddenCount()==1&&p.state(1).reduced);
     p.tick({at(1,79),at(2,76)},&Here,s);CHECK(p.hidden(2));
     p.tick({at(1,79),at(2,74)},&Here,s);CHECK(!p.hidden(2)&&p.state(2).reduced);}

    // Detail off: nothing ever reduced.
    {Policy p;Settings s;s.detailMetres=0;p.tick({at(3,900)},&Here,s);CHECK(!p.state(3).reduced);}

    // This porter not known (loading), or a body the plan hides: the state stays as it was.
    {Policy p;Settings s;s.hideMetres=50;
     p.tick({at(4,60)},&Here,s);CHECK(p.state(4).reduced&&p.hidden(4));
     p.tick({at(4,1)},nullptr,s);CHECK(p.state(4).reduced&&p.hidden(4));
     p.tick({Body{4,std::nullopt}},&Here,s);CHECK(p.state(4).reduced&&p.hidden(4));
     p.tick({at(4,1)},&Here,s);CHECK(!p.state(4).reduced&&!p.hidden(4));
     // And a body near first, then unknown: still full detail.
     p.tick({at(4,1)},nullptr,s);CHECK(!p.state(4).reduced);}

    // A player who left is forgotten: the same id back at 27 m starts fresh (full detail), not reduced from before.
    {Policy p;Settings s;
     p.tick({at(5,40),at(6,1)},&Here,s);CHECK(p.state(5).reduced);
     p.tick({at(6,1)},&Here,s);CHECK(!p.state(5).reduced&&p.reduced()==0);
     p.tick({at(5,27),at(6,1)},&Here,s);CHECK(!p.state(5).reduced);}

    // The HUD's far players: a body hidden for its distance is not drawn; one without a body is not drawn either.
    {Policy p;Settings s;s.hideMetres=100;
     p.tick({at(11,10),at(12,150),at(13,20)},&Here,s);
     auto d=drawn({{11,0x1000},{12,0x2000},{13,0}},p);
     CHECK(d.count(11)&&!d.count(12)&&!d.count(13)&&d.size()==1);
     p.tick({at(11,10),at(12,50),at(13,20)},&Here,s);
     CHECK(drawn({{11,0x1000},{12,0x2000}},p).count(12));}

    // Look: reduced is no shadow and no cloth; the lab's switches come first, for its own distant bodies only.
    {State near,far;far.reduced=true;
     auto a=look(near,{},false);CHECK(a.shadow&&!a.cloth);
     auto b=look(far,{},false);CHECK(!b.shadow&&b.cloth&&*b.cloth==ClothOff);
     auto c=look(far,Lab{1,999},false);CHECK(c.shadow&&c.cloth);        // lab shadows on: on, cloth still the policy's
     auto d=look(near,Lab{0,999},true);CHECK(!d.shadow&&!d.cloth);      // lab shadows off for its distant bodies
     auto e=look(far,Lab{0,999},false);CHECK(e.shadow);                 // ...and the game's own for its near ones
     auto f=look(near,Lab{9,50},true);CHECK(f.shadow&&f.cloth&&std::fabs(*f.cloth-0.5f)<1e-6f);
     auto g=look(far,Lab{9,50},false);CHECK(!g.shadow&&!g.cloth);}      // lab cloth: the game's own when not distant

    // Applied: the calls for one entity. A fresh body that is to look as the game made it is not touched.
    {Applied a;auto c=a.step(Look{},1.5f);CHECK(!c.shadow&&!c.cloth&&a.ownCloth()&&*a.ownCloth()==1.5f);
     c=a.step(Look{},1.5f);CHECK(!c.shadow&&!c.cloth);
     // Far: shadow off and cloth off, once.
     Look off{false,ClothOff};
     c=a.step(off,1.5f);CHECK(c.shadow&&!*c.shadow&&c.cloth&&*c.cloth==ClothOff);
     c=a.step(off,ClothOff);CHECK(!c.shadow&&!c.cloth);
     // Near again: the game's own value back (1.5, not what the body reads now), shadow on; then nothing more.
     c=a.step(Look{},ClothOff);CHECK(c.shadow&&*c.shadow&&c.cloth&&*c.cloth==1.5f);
     c=a.step(Look{},1.5f);CHECK(!c.shadow&&!c.cloth);
     CHECK(*a.ownCloth()==1.5f);}
    // The game's own cloth value not readable or not sane: cloth is never set (it could not be put back); shadow is.
    {Applied a;Look off{false,ClothOff};
     auto c=a.step(off,std::nullopt);CHECK(c.shadow&&!c.cloth);
     c=a.step(off,std::nanf(""));CHECK(!c.cloth);c=a.step(off,1e9f);CHECK(!c.cloth&&!a.ownCloth());
     c=a.step(off,0.8f);CHECK(c.cloth&&*c.cloth==ClothOff&&*a.ownCloth()==0.8f);
     // Near again while the model is not readable: shadow back now, cloth on the next tick it reads.
     c=a.step(Look{},std::nullopt);CHECK(c.shadow&&*c.shadow&&!c.cloth);
     c=a.step(Look{},ClothOff);CHECK(c.cloth&&*c.cloth==0.8f&&!c.shadow);}
    // A fresh body far at once: off on its first tick, the own value from the same read.
    {Applied a;auto c=a.step(Look{false,ClothOff},2.0f);CHECK(c.shadow&&!*c.shadow&&c.cloth&&*c.cloth==ClothOff&&*a.ownCloth()==2.0f);}

    if(failures){std::fprintf(stderr,"bodylod: %d failures\n",failures);return 1;}
    std::puts("bodylod: OK");return 0;
}
