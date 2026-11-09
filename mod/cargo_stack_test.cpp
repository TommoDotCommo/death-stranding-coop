// The partner's cargo stack (cargo_stack.h) against a fake engine: cases of mixed sizes packed on the lowest column
// along the anchor's axes, made a few per tick from each item's case (looked up once), replaced from the first changed
// item up, removed when the cargo or the body goes, forgotten (never released) with the world, and a refused or
// replaced mesh never makes the stack touch the game again.
#include "cargo_stack.h"
#include <cmath>
#include <cstdio>
#include <map>
using namespace mod::stack;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"stack oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
struct Engine {
    struct Made{Case c;Transform at;};
    std::map<uint64_t,Made> live;uint64_t next=0x1000;unsigned creates=0,releases=0,lookups=0,refuse=0;bool noModel=false;
    std::map<uint64_t,bool> foreign;
    Api api(){Api a;
        // Item ids: 0 = plain M case; 10..13 = volume 0..3 with their own model and look; 99 unknown -> nullopt.
        a.caseFor=[this](uint32_t item,bool variant)->std::optional<Case>{++lookups;if(noModel||item==99)return std::nullopt;
            Case c;c.model=0x500+item+(variant?0x80:0);c.look=item?0x900+item:0;c.volume=item>=10?uint8_t(item-10):1;return c;};
        a.create=[this](const Case& c,const Transform& t)->uint64_t{++creates;if(refuse){--refuse;return 0;}live[next+=0x100]={c,t};return next;};
        a.place=[this](uint64_t m,const Transform& t){if(!live.count(m)||foreign.count(m))return false;live[m].at=t;return true;};
        a.release=[this](uint64_t m){++releases;live.erase(m);};
        return a;}
};
bool near(double a,double b){return std::fabs(a-b)<1e-6;}
std::vector<Item> I(std::initializer_list<uint32_t> ids){std::vector<Item> v;for(auto x:ids)v.push_back({x,false});return v;}
}
int main(){
    CHECK(near(height(0),0.132)&&near(height(1),0.132)&&near(height(2),0.264)&&near(height(3),0.396));
    // Anchor turned 90 degrees: right = +y, forward = -x, up = +z.
    Transform anchor{{10,20,30},{0,1,0, -1,0,0, 0,0,1}};
    Layout one;one.offset={0.1,0.2,0.3};one.columns=1;
    auto p=place(anchor,one,{1,2,3});
    CHECK(p.size()==3&&near(p[0].position[0],10-0.2)&&near(p[0].position[1],20+0.1));
    CHECK(near(p[0].position[2],30.3+0.066)&&near(p[1].position[2],30.3+0.132+0.132)&&near(p[2].position[2],30.3+0.396+0.198));
    CHECK(p[2].rotation==anchor.rotation);
    // Two columns: each case goes on the lower column (left on a tie), half a column either side of the centre.
    Layout two;two.columns=2;two.across={0.4,0,0};
    auto q=place(anchor,two,{3,1,1,1});
    CHECK(near(q[0].position[1],20-0.2)&&near(q[0].position[2],30+0.198));        // LL on the left
    CHECK(near(q[1].position[1],20+0.2)&&near(q[1].position[2],30+0.066));        // M on the right
    CHECK(near(q[2].position[1],20+0.2)&&near(q[2].position[2],30+0.198));        // right again (0.132 < 0.396)
    CHECK(near(q[3].position[1],20+0.2)&&near(q[3].position[2],30+0.330));        // right is still lower (0.264)
    Engine e;std::vector<std::string> log;Stack s(e.api(),[&](const std::string& m){log.push_back(m);});s.layout=two;
    // Five items: two per tick, each item's own case, every case placed every tick; one lookup per item.
    auto five=I({12,10,10,13,0});
    s.tick(true,five,&anchor);CHECK(s.shown()==2&&e.creates==2);
    s.tick(true,five,&anchor);s.tick(true,five,&anchor);CHECK(s.shown()==5&&e.live.size()==5&&e.lookups==4);
    {auto want=place(anchor,two,{2,0,0,3,1});unsigned k=0;
     for(auto& [m,made]:e.live){CHECK(near(made.at.position[2],want[k].position[2])&&near(made.at.position[1],want[k].position[1]));++k;}
     CHECK(e.live.begin()->second.c.model==0x500+12&&e.live.begin()->second.c.look==0x900+12);}
    Transform moved=anchor;moved.position[0]+=5;s.tick(true,five,&moved);
    for(auto& [m,made]:e.live)CHECK(near(made.at.position[0],moved.position[0]));
    // A changed item: the meshes from it up are replaced, the ones below kept.
    auto before=e.releases;auto firstTwo=std::vector<uint64_t>{e.live.begin()->first,std::next(e.live.begin())->first};
    auto changed=I({12,10,11,13,0});
    for(int i=0;i<3;++i)s.tick(true,changed,&anchor);
    CHECK(s.shown()==5&&e.releases==before+3&&e.live.count(firstTwo[0])&&e.live.count(firstTwo[1]));
    // Fewer: the top cases go; capped at MaxCases.
    before=e.releases;s.tick(true,I({12,10,11}),&anchor);CHECK(s.shown()==3&&e.live.size()==3&&e.releases==before+2);
    for(int i=0;i<20;++i)s.tick(true,std::vector<Item>(40),&anchor);
    CHECK(s.shown()==Stack::MaxCases);
    // No anchor or not permitted: all released.
    s.tick(false,I({1,1}),&anchor);CHECK(s.shown()==0&&e.live.empty());
    s.tick(true,I({0,0}),&anchor);CHECK(s.shown()==2);s.tick(true,I({0,0}),nullptr);CHECK(s.shown()==0&&e.live.empty());
    // World replaced: forgotten without a release; the cases are looked up again.
    s.tick(true,I({0,0}),&anchor);before=e.releases;auto looked=e.lookups;s.forget("load");CHECK(s.shown()==0&&e.releases==before);
    e.live.clear();s.tick(true,I({0}),&anchor);CHECK(e.lookups==looked+1);
    // A mesh that is no longer the one made: dropped, never released; the ones above it released and made again.
    s.clear();e.live.clear();for(int i=0;i<3;++i)s.tick(true,I({0,0,0}),&anchor);CHECK(s.shown()==3);
    e.foreign[std::next(e.live.begin())->first]=true;before=e.releases;s.tick(true,I({0,0,0}),&anchor);
    CHECK(e.releases==before+1&&s.shown()==1);
    // The same item with its record's variant bit is another case: looked up on its own, made with its own model.
    s.clear();e.live.clear();looked=e.lookups;
    for(int i=0;i<2;++i)s.tick(true,std::vector<Item>{{10,false},{10,true}},&anchor);
    CHECK(s.shown()==2&&e.lookups==looked+2);
    {bool plainSeen=false,variantSeen=false;for(auto& [m,made]:e.live){plainSeen|=made.c.model==0x500+10;variantSeen|=made.c.model==0x500+10+0x80;}
     CHECK(plainSeen&&variantSeen);}
    before=e.releases;s.tick(true,std::vector<Item>{{10,false},{10,false}},&anchor);CHECK(e.releases==before+1);
    // Refusals and unknown items: give up after MaxFailures, then nothing more is asked of the game.
    Engine r;r.refuse=99;Stack f(r.api(),[](const std::string&){});
    for(int i=0;i<10;++i)f.tick(true,I({0,0,0,0}),&anchor);
    CHECK(f.shown()==0&&r.creates==Stack::MaxFailures&&f.failures()==Stack::MaxFailures);
    Engine z;z.noModel=true;Stack g(z.api(),[](const std::string&){});
    for(int i=0;i<10;++i)g.tick(true,I({0,0}),&anchor);
    CHECK(g.shown()==0&&z.creates==0&&z.lookups==Stack::MaxFailures);
    if(failures)return 1;
    std::puts("stack tests passed");return 0;
}
