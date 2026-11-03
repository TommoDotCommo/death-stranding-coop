#include "pad.h"
#include <cstdio>
#include <stdexcept>
using namespace mod::pad;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"pad oracle: %s\n",#x);++failures;}}while(0)
template<class F> bool refused(F f){try{f();return false;}catch(const std::runtime_error&){return true;}}
int main(){
    auto steps=parse("# title screen\npad S1\npress A 150\nhold START+LB 300 # menu\nstick 0 1 1000 RB\nlook -1 0.5 100\nwait 50\n","S1");
    CHECK(steps.size()==6);
    CHECK(steps[0].pad.buttons==0x1000&&steps[0].ms==150);CHECK(steps[1].pad.buttons==0&&steps[1].ms==ReleaseGapMs);
    CHECK(steps[2].pad.buttons==(0x10|0x100)&&steps[2].ms==300);
    CHECK(steps[3].pad.lx==0&&steps[3].pad.ly==32767&&steps[3].pad.buttons==0x200);
    CHECK(steps[4].pad.rx==-32767&&steps[4].pad.ry==16384);
    CHECK(parse("pad S1\npress LT+RT 10\n","S1")[0].pad.leftTrigger==255);
    for(const char* bad:{"pad S2\nwait 10\n","press A 10\n","","pad S1\npress Q 10\n","pad S1\npress A\n","pad S1\nwait 0\n",
                         "pad S1\nwait 200000\n","pad S1\nstick 2 0 10\n","pad S1\nstick 0 nan 10\n","pad S1\nlook 0 0 10 A\n",
                         "pad S1\njump 10\n","pad S1\nwait -5\n","pad S1\nwait 1.5\n","pad S1\nstick 0x1 0 10\n"})
        CHECK(refused([&]{parse(bad,"S1");}));
    CHECK(refused([&]{parse("pad S1\n"+std::string(MaxBytes,' '),"S1");}));
    Player p;Gamepad g;
    CHECK(!p.sample(0,g)&&p.done());
    p.load(steps);CHECK(!p.done());
    CHECK(p.sample(1000,g)&&g.buttons==0x1000&&p.index()==0);
    CHECK(p.sample(1149,g)&&g.buttons==0x1000);
    CHECK(p.sample(1150,g)&&g.buttons==0&&p.index()==1);            // boundary belongs to the next step
    CHECK(p.sample(1350,g)&&g.buttons==0x110&&p.index()==2);
    CHECK(p.sample(2000,g)&&g.ly==32767&&p.index()==3);
    CHECK(p.sample(2700,g)&&g.rx==-32767);
    CHECK(p.sample(2799,g)&&g.buttons==0&&g.rx==0&&p.index()==5);
    CHECK(!p.sample(2800,g)&&g.buttons==0&&g.ly==0&&p.done());      // total 1800 ms
    CHECK(!p.sample(9000,g));
    p.load(steps);CHECK(p.sample(5000,g)&&g.buttons==0x1000);p.stop();CHECK(!p.sample(5001,g)&&g.buttons==0&&p.done());
    if(!failures)std::puts("pad script parse and timing checks passed");
    return failures?1:0;
}
