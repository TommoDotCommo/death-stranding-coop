#include "link.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"oracle failed: %s (line %d)\n",#x,__LINE__);exit(1);}}while(0)
using namespace mod::link;
int main() {
    Profile none,internet,poor,bad;
    CHECK(profile("none",none)&&profile("internet",internet)&&profile("poor",poor)&&!profile("lan",bad)&&!profile("",bad));
    // No profile, no impairment: every packet at once. The older impairment key keeps its exact schedule (the timing
    // analysis depends on it) and touches presence only.
    for(uint64_t n=1;n<2000;++n) {
        auto a=fate(none,false,n,true);CHECK(!a.lost&&a.delayMs==0);
        auto b=fate(none,true,n,true);CHECK(!b.lost&&b.delayMs==15+(n*17)%51);
        auto c=fate(none,true,n,false);CHECK(!c.lost&&c.delayMs==0);
    }
    // A profile: loss near its rate, delays inside the range and using all of it, the same for every kind of packet.
    for(auto* p:{&internet,&poor}) for(bool presence:{true,false}) {
        unsigned lost=0,total=20000;uint64_t low=~0ull,high=0;bool reordered=false;uint64_t previousDue=0;
        for(uint64_t n=1;n<=total;++n) {
            auto f=fate(*p,false,n,presence);lost+=f.lost;
            CHECK(f.delayMs>=p->delayMinMs&&f.delayMs<p->delayMinMs+p->delaySpanMs);
            if(f.delayMs<low) low=f.delayMs;
            if(f.delayMs>high) high=f.delayMs;
            uint64_t due=n*8+f.delayMs;if(due<previousDue)reordered=true;previousDue=due;   // packets 8 ms apart
            auto again=fate(*p,true,n,presence);CHECK(again.lost==f.lost&&again.delayMs==f.delayMs);   // repeatable
        }
        double rate=lost*1000.0/total;
        CHECK(rate>p->lossPerMille*0.7&&rate<p->lossPerMille*1.3);
        CHECK(low==p->delayMinMs&&high==p->delayMinMs+p->delaySpanMs-1);
        CHECK(reordered);
    }
    CHECK(poor.lossPerMille>internet.lossPerMille&&poor.delayMinMs>internet.delayMinMs);
    puts("link test ok");
}
