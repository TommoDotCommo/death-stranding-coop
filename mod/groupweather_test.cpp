// Timefall shared in group play (groupweather.h), without a game: cells, what is said and when, what is placed, and
// that the mod's own rain is never reported back.
#include "groupweather.h"
#include <cstdio>
using namespace mod::groupweather;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"groupweather oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
int main(){
    CHECK(cellKey(10,10)==cellKey(499,1)&&cellKey(10,10)!=cellKey(510,10)&&cellKey(-1,0)!=cellKey(1,0));
    {auto c=cellCentre(cellKey(1234,-777));CHECK(c[0]==1250&&c[1]==-750);}
    CHECK(cellKey(1e9,-1e9)>=0&&cellKey(1e9,-1e9)<(int64_t(1)<<52));
    CHECK(wetness(-2)==1000&&wetness(-1)==500&&wetness(0)==0&&wetness(0.9f)==0);
    Rain r;const std::array<double,3> here{100,100,50};
    // Dry, or unreadable: nothing said. Wet: said once, again only after ResendMs.
    CHECK(!r.report(0,here,0.8f)&&!r.report(0,here,std::nullopt));
    auto s=r.report(1000,here,-1.0f);CHECK(s&&s->first==cellKey(100,100)&&s->second==500);
    CHECK(!r.report(2000,here,-1.0f)&&r.report(1000+ResendMs,here,-2.0f));
    // Heard wet 600 m away and 3 km away: only the near one is placed, renewed after RenewMs, lapsed after WetForMs.
    Rain o;const int64_t nearCell=cellKey(700,100),farCell=cellKey(3100,100);
    o.heard(nearCell,500,0);o.heard(farCell,1000,0);o.heard(cellKey(0,0),0,0);
    auto p=o.apply(10,here);CHECK(p.size()==1&&p[0].cell==nearCell&&p[0].at[2]==50);
    CHECK(o.apply(20,here).empty()&&o.apply(10+RenewMs,here).size()==1);
    CHECK(o.apply(WetForMs+1,here).empty());
    // The mod's own rain where this porter stands is not reported back.
    Rain e;e.heard(cellKey(100,100),500,0);CHECK(e.apply(0,here).size()==1&&e.holds(cellKey(100,100),1)&&!e.report(1,here,-1.0f));
    // At most Slots cells at once, the nearest first.
    Rain m;for(int i=0;i<12;++i)m.heard(cellKey(100+i*60.0,600),500,0);   // a handful of cells within reach
    for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j)m.heard(cellKey(100+i*500.0,100+j*500.0),500,0);
    auto many=m.apply(0,here);CHECK(many.size()<=Slots&&!many.empty()&&many[0].cell==cellKey(100,100));
    if(failures){std::fprintf(stderr,"%u groupweather oracle failures\n",failures);return 1;}
    std::puts("groupweather oracles passed");return 0;
}
