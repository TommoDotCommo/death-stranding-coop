#include "virtual_kbm.h"
#include <cstdio>
#include <vector>
using namespace redirect::kbm;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"kbm oracle: %s\n",#x);++failures;}}while(0)
std::vector<Event> drain(Feed& f){std::vector<Event> out;Event e;while(f.pop(e))out.push_back(e);return out;}
int main(){
    // Only the proxy's own handle is answered; every real raw-input handle goes to Windows.
    CHECK(serves(Handle));CHECK(!serves(0));CHECK(!serves(0x10041));CHECK(!serves(Handle+1));
    // The foreground answer: only the keyboard update's call, only while the script holds something.
    CHECK(answersForeground(true,ForegroundReturn));CHECK(!answersForeground(false,ForegroundReturn));
    CHECK(!answersForeground(true,ForegroundReturn+1));CHECK(!answersForeground(true,0x1a1871f));CHECK(!answersForeground(false,0));
    // The pointer answer: only while the script holds something; the middle, pushed a third of the height the way the mouse moves.
    CHECK(answersCursor(true));CHECK(!answersCursor(false));
    {Point m;m.x=1000;m.y=500;auto c=cursor(m,900,0,0);CHECK(c.x==1000&&c.y==500);
     c=cursor(m,900,3000,0);CHECK(c.x==1300&&c.y==500);c=cursor(m,900,0,-50);CHECK(c.x==1000&&c.y==200);
     c=cursor(m,900,-300,-400);CHECK(c.x==820&&c.y==260);}
    Feed f;Input none;
    f.frame(false,none,1000);CHECK(!f.pending()&&!f.holding());
    Input w;w.keys[0]=0x57;w.keys[1]=0x10;w.keyCount=2;
    f.frame(true,w,2000);auto e=drain(f);
    CHECK(e.size()==2&&!e[0].mouse&&!e[0].up&&e[0].vk==0x10&&e[1].vk==0x57&&!e[1].up);CHECK(f.holding());
    f.frame(true,w,3000);CHECK(!f.pending());                      // held: no repeat events
    Input s;s.keys[0]=0x57;s.keyCount=1;
    f.frame(true,s,4000);e=drain(f);CHECK(e.size()==1&&e[0].vk==0x10&&e[0].up);   // SHIFT released, W still down
    f.frame(false,s,5000);e=drain(f);CHECK(e.size()==1&&e[0].vk==0x57&&e[0].up);  // script over: everything released
    CHECK(!f.holding());
    // Mouse: speed times the time since the frame before, the fraction carried; a stalled frame is capped.
    Input m;m.mouseX=1000;m.mouseY=-500;
    f.frame(false,none,999000);CHECK(!f.pending());
    f.frame(true,m,1000000);e=drain(f);CHECK(e.size()==1&&e[0].mouse&&e[0].dx==1&&e[0].dy==0&&e[0].buttonFlags==0);
    f.frame(true,m,1016500);e=drain(f);CHECK(e.size()==1&&e[0].dx==16&&e[0].dy==-8);    // 16.5 and -8.75 with the carry
    f.frame(true,m,1033000);e=drain(f);CHECK(e.size()==1&&e[0].dx==17&&e[0].dy==-9);
    f.frame(true,m,3033000);e=drain(f);CHECK(e.size()==1&&e[0].dx==100&&e[0].dy==-50);  // 2 s stall: 100 ms of travel
    f.frame(false,none,3050000);CHECK(!f.pending());
    f.frame(true,none,3066000);CHECK(!f.pending());                                     // a still mouse reports nothing
    // Buttons: one report per transition, released when the script ends.
    Input c;c.buttons=1|2;
    f.frame(true,c,4000000);e=drain(f);CHECK(e.size()==1&&e[0].mouse&&e[0].buttonFlags==(0x1|0x4)&&f.holding());
    c.buttons=2;f.frame(true,c,4016000);e=drain(f);CHECK(e.size()==1&&e[0].buttonFlags==0x2);
    f.frame(false,none,4032000);e=drain(f);CHECK(e.size()==1&&e[0].buttonFlags==0x8&&!f.holding());
    // A key slot of zero and counts past the limit are ignored.
    Input z;z.keyCount=200;z.keys[0]=0;z.keys[7]=0x41;
    f.frame(true,z,5000000);e=drain(f);CHECK(e.size()==1&&e[0].vk==0x41);
    f.frame(false,none,5016000);drain(f);
    // The wheel: once per step that asks (120 a notch, positive away from the player), not again while it lasts.
    { Feed w;Input in;in.wheel=-2;in.wheelSeq=5;w.frame(true,in,10000000);auto ev=drain(w);
      CHECK(ev.size()==1&&ev[0].mouse&&(ev[0].buttonFlags&0x0400)&&ev[0].wheel==-240);
      w.frame(true,in,10016000);CHECK(!w.pending());                                        // the same step: once
      in.wheel=1;in.wheelSeq=6;w.frame(true,in,10032000);ev=drain(w);CHECK(ev.size()==1&&ev[0].wheel==120);
      Input none;w.frame(true,none,10048000);CHECK(!w.pending());w.frame(false,in,10064000);CHECK(!w.pending()); }
    if(!failures)std::puts("virtual keyboard and mouse checks passed");
    return failures?1:0;
}
