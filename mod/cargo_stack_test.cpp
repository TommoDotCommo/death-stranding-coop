// The partner's cargo stack (cargo_stack.h) against a fake engine: cases follow the wanted count a few per tick,
// sit along the anchor's axes, go when the cargo or the body goes, are forgotten (never released) with the world,
// and a refused or replaced mesh never makes the stack touch the game again.
#include "cargo_stack.h"
#include <cmath>
#include <cstdio>
#include <map>
using namespace mod::stack;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"stack oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
struct Engine {
    std::map<uint64_t,Transform> live;uint64_t next=0x1000,model=0x77;unsigned creates=0,releases=0,refuse=0;std::map<uint64_t,bool> foreign;
    Api api(){Api a;
        a.model=[this]{return model;};
        a.create=[this](uint64_t m,const Transform& t)->uint64_t{++creates;if(m!=model||refuse){if(refuse)--refuse;return 0;}live[next+=0x100]=t;return next;};
        a.place=[this](uint64_t m,const Transform& t){if(!live.count(m)||foreign.count(m))return false;live[m]=t;return true;};
        a.release=[this](uint64_t m){++releases;live.erase(m);};
        return a;}
};
bool near(double a,double b){return std::fabs(a-b)<1e-6;}
}
int main(){
    // Anchor turned 90 degrees: right = +y, forward = -x, up = +z.
    Transform anchor{{10,20,30},{0,1,0, -1,0,0, 0,0,1}};
    Layout l;l.offset={0.1,0.2,0.3};l.step={0,0,0.5};l.columns=1;
    auto t=place(anchor,l,2);
    CHECK(near(t.position[0],10-0.2)&&near(t.position[1],20+0.1)&&near(t.position[2],30+0.3+1.0));
    // Two columns: case 3 is row 1, the right-hand column (half a column right of the centre).
    {Layout w=l;w.columns=2;w.across={0.4,0,0};auto r=place(anchor,w,3);
     CHECK(near(r.position[1],20+0.1+0.2)&&near(r.position[2],30+0.3+0.5));
     auto left=place(anchor,w,2);CHECK(near(left.position[1],20+0.1-0.2)&&near(left.position[2],30+0.3+0.5));}
    CHECK(t.rotation==anchor.rotation);
    Engine e;std::vector<std::string> log;Stack s(e.api(),[&](const std::string& m){log.push_back(m);});s.layout=l;
    // Five wanted: two per tick, every case placed every tick at its level.
    s.tick(true,5,&anchor);CHECK(s.shown()==2&&e.creates==2);
    s.tick(true,5,&anchor);s.tick(true,5,&anchor);CHECK(s.shown()==5&&e.live.size()==5);
    {unsigned k=0;for(auto& [m,p]:e.live){CHECK(near(p.position[2],place(anchor,l,k).position[2]));++k;}}
    Transform moved=anchor;moved.position[0]+=5;s.tick(true,5,&moved);
    for(auto& [m,p]:e.live)CHECK(near(p.position[0],moved.position[0]-0.2));
    // Fewer: the top cases go; capped at MaxCases.
    s.tick(true,3,&anchor);CHECK(s.shown()==3&&e.live.size()==3&&e.releases==2);
    for(int i=0;i<20;++i)s.tick(true,99,&anchor);
    CHECK(s.shown()==Stack::MaxCases);
    // No anchor or not permitted: all released.
    s.tick(false,4,&anchor);CHECK(s.shown()==0&&e.live.empty());
    s.tick(true,2,&anchor);CHECK(s.shown()==2);s.tick(true,2,nullptr);CHECK(s.shown()==0&&e.live.empty());
    // World replaced: forgotten without a release.
    s.tick(true,2,&anchor);auto before=e.releases;s.forget("load");CHECK(s.shown()==0&&e.releases==before);
    // A mesh that is no longer the one made: dropped, never released; the others stay.
    e.live.clear();s.tick(true,3,&anchor);s.tick(true,3,&anchor);CHECK(s.shown()==3);
    e.foreign[e.live.begin()->first]=true;before=e.releases;s.tick(true,3,&anchor);CHECK(e.releases==before&&s.shown()==2);
    // Refusals: give up after MaxFailures, then nothing more is asked of the game.
    Engine r;r.refuse=99;Stack f(r.api(),[](const std::string&){});
    for(int i=0;i<10;++i)f.tick(true,4,&anchor);
    CHECK(f.shown()==0&&r.creates==Stack::MaxFailures&&f.failures()==Stack::MaxFailures);
    Engine z;z.model=0;Stack g(z.api(),[](const std::string&){});
    for(int i=0;i<10;++i)g.tick(true,4,&anchor);
    CHECK(g.shown()==0&&z.creates==0);
    if(failures)return 1;
    std::puts("stack tests passed");return 0;
}
