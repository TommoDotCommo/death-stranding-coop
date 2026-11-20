// Pings and likes in group play (grouppings.h), without a game: pings shown once each for 30 s, one per sender, at
// most 16, as HUD marks with ids of their own; the like key's target; which likes that arrive are given; the line.
#include "grouppings.h"
#include <cmath>
#include <cstdio>
#include <set>
using namespace mod::grouppings;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"grouppings oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
int main(){
    {   // Shown once per (sender, serial), newest first, for ShowMs; a sender's newer ping replaces his older one.
        Pings p;const uint64_t t=1000000;
        CHECK(p.take(11,1,{1,2,3},t)&&!p.take(11,1,{1,2,3},t+10)&&p.shown(t+10)==1);   // a repeat is not shown again
        CHECK(p.take(12,7,{40,0,5},t+20));
        auto m=p.marks(t+30);CHECK(m.size()==2&&m[0].x==40&&m[1].x==1&&m[1].y==2&&m[1].z==3);
        CHECK(m[0].id==markId(12,7)&&m[1].id==markId(11,1)&&m[0].id!=m[1].id);
        CHECK(p.take(11,2,{9,9,9},t+40)&&p.shown(t+40)==2);   // Ann's second ping: hers moves, Bob's stays
        m=p.marks(t+40);CHECK(m.size()==2&&m[0].x==9&&m[1].x==40);
        CHECK(p.shown(t+20+ShowMs-1)==2&&p.shown(t+20+ShowMs)==1&&p.shown(t+40+ShowMs)==0&&p.marks(t+40+ShowMs).empty());
        CHECK(!p.take(11,2,{9,9,9},t+50+ShowMs));   // remembered after it went
        CHECK(!p.take(13,1,{std::nan(""),0,0},t)&&p.take(13,2,{0,0,0},t+60+ShowMs));
        // At most MaxPings from different senders: the oldest goes.
        Pings q;for(uint64_t s=1;s<=MaxPings+4;++s)q.take(s,1,{double(s),0,0},t+s);
        auto all=q.marks(t+100);CHECK(all.size()==MaxPings&&all.front().x==MaxPings+4&&all.back().x==5);}
    {   // Ids: a tag of their own, never the far players' (0x4855) or the map's (0x4d50), stable, by sender and serial.
        std::set<uint64_t> ids;
        for(uint64_t s=1;s<=50;++s)for(uint32_t n=1;n<=20;++n){auto id=markId(s,n);CHECK((id>>48)==0x5047);ids.insert(id);}
        CHECK(ids.size()==1000&&markId(5,5)==markId(5,5)&&markId(5,6)!=markId(6,5));}
    {   // The like key: the nearest shown player within 25 m; nobody shown or near, nobody.
        std::vector<Candidate> c{{1,{30,0,0},true},{2,{0,0,10},true},{3,{3,0,0},false},{4,{0,24,0},true},{5,{0,0,-6},true}};
        CHECK(likeTarget({0,0,0},c)==std::optional<uint64_t>(5));
        CHECK(likeTarget({30,0,1},c)==std::optional<uint64_t>(1));
        CHECK(!likeTarget({0,0,0},{{1,{30,0,0},true},{3,{3,0,0},false}}).has_value());
        CHECK(!likeTarget({0,0,0},{}).has_value()&&likeTarget({0,0,0},{{9,{0,25,0},true}})==std::optional<uint64_t>(9));}
    {   // Likes that arrive: each once; at most 3 a minute from one sender; one a second and ten a minute in all.
        LikeGate g;const uint64_t t=500000;
        CHECK(g.accept(1,1,t)&&!g.accept(1,1,t+5000));                    // a repeat
        CHECK(!g.accept(2,1,t+500));                                       // one a second in all
        CHECK(g.accept(1,2,t+2000)&&g.accept(1,3,t+4000)&&!g.accept(1,4,t+6000));   // three a minute from Ann
        CHECK(g.accept(2,1,t+8000));                                       // Bob's still counts
        CHECK(g.accept(1,5,t+60000+1));                                    // a minute after her first
        unsigned given=0;for(uint32_t i=0;i<40;++i)given+=g.accept(100+i,1,t+200000+1100*i);
        CHECK(given==10);}                                                 // ten a minute in all
    {   CHECK(pingLine("Ann",120.4)==u"Ann pinged a place 120 m away."&&pingLine("Ann",2)==u"Ann pinged a place here.");
        CHECK(pingLine("Bea",2340)==u"Bea pinged a place 2.3 km away."&&pingLine("",-1)==u"A player pinged a place.");
        CHECK(pingLine("<b>Cy</b>",10).find(u'<')==std::u16string::npos);
        Serials s(0xffffffffu);CHECK(s.next()==0xffffffffu&&s.next()==1);Serials z(0);CHECK(z.next()==1);}
    if(failures){std::fprintf(stderr,"%u grouppings oracle failures\n",failures);return 1;}
    std::puts("grouppings oracles passed");return 0;
}
