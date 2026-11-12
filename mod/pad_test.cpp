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
    // Keyboard and mouse steps.
    { auto k=parse("pad S1\nkey 1+LMB 300 mouse -3000 0\nkey RMB+LMB 50\nmouse 0 9 10 RMB\n","S1");
      CHECK(k.size()==3&&k[0].keys.count==1&&k[0].keys.vk[0]=='1'&&k[0].keys.mouseButtons==1&&k[0].keys.mouseX==-3000);
      CHECK(k[1].keys.count==0&&k[1].keys.mouseButtons==3);CHECK(k[2].keys.mouseButtons==2&&k[2].keys.mouseY==9); }
    { auto k=parse("pad S1\nkey W+SHIFT 1500\ntap E\nmouse 400 -30 500 W\nclick R 80\nkey x4C 300 mouse -200 0\nkey F5+ESC+W+W 10\n","S1");
      CHECK(k.size()==8);
      CHECK(k[0].keys.count==2&&k[0].keys.vk[0]==0x57&&k[0].keys.vk[1]==0x10&&k[0].ms==1500&&k[0].pad.buttons==0);
      CHECK(k[1].keys.count==1&&k[1].keys.vk[0]==0x45&&k[1].ms==120);CHECK(k[2].keys.count==0&&k[2].ms==ReleaseGapMs);
      CHECK(k[3].keys.mouseX==400&&k[3].keys.mouseY==-30&&k[3].keys.count==1&&k[3].ms==500);
      CHECK(k[4].keys.mouseButtons==2&&k[4].ms==80);CHECK(k[5].keys.mouseButtons==0);
      CHECK(k[6].keys.vk[0]==0x4c&&k[6].keys.mouseX==-200&&k[6].keys.mouseY==0);
      CHECK(k[7].keys.count==3&&k[7].keys.vk[0]==0x74&&k[7].keys.vk[1]==0x1b);   // a repeated key counts once
      Player q;Gamepad g;Keys held;q.load(k);
      CHECK(q.sample(100,g,&held)&&held.count==2&&g.buttons==0);CHECK(q.sample(1700,g,&held)&&held.count==1&&held.vk[0]==0x45);
      CHECK(q.sample(1730,g,&held)&&held.count==0);CHECK(q.sample(100000,g,&held)==false&&held.count==0&&held.mouseX==0); }
    for(const char* bad:{"pad S1\nkey 10\n","pad S1\nkey WW 10\n","pad S1\nkey x01 10\n","pad S1\nkey xff 10\n","pad S1\nkey F13 10\n",
                         "pad S1\nkey A+B+C+D+E+F+G+H+I 10\n","pad S1\nkey W 10 mouse 1\n","pad S1\nmouse 30000 0 10\n","pad S1\nmouse 1.5 0 10\n",
                         "pad S1\nmouse - 0 10\n","pad S1\nclick X\n","pad S1\nclick L 0\n","pad S1\ntap\n","pad S1\nkey w 10\n"})
        CHECK(refused([&]{parse(bad,"S1");}));
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
