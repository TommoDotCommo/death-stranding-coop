#include "passenger.h"
#include <cstdio>
using namespace mod::passenger;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"passenger oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Look near(bool key=false){Look l;l.permitted=true;l.partnerInTruck=true;l.truck=0x7000;l.distance=2;l.key=key;return l;}
int main(){
    // Getting on: only with the key, next to the partner's truck, permitted, mover free.
    { Rider r;CHECK(r.tick(near())==Do::Nothing);
      Look far=near(true);far.distance=Reach+0.5;CHECK(r.tick(far)==Do::Nothing);
      Look walking=near(true);walking.partnerInTruck=false;CHECK(r.tick(walking)==Do::Nothing);
      Look busy=near(true);busy.permitted=false;CHECK(r.tick(busy)==Do::Nothing);
      Look held=near(true);held.mover=Held;CHECK(r.tick(held)==Do::Nothing);
      CHECK(r.tick(near(true))==Do::GetOn&&r.riding()&&r.truck()==0x7000); }
    // Getting off: the key, the partner out, the truck replaced, not permitted; the game letting go ends it quietly.
    { auto on=[]{Rider r;r.tick(near(true));return r;};
      { Rider r=on();Look l=near();l.mover=Held;CHECK(r.tick(l)==Do::Nothing);CHECK(r.tick(near(true))==Do::GetOff&&!r.riding()); }
      { Rider r=on();Look l=near();l.partnerInTruck=false;CHECK(r.tick(l)==Do::GetOff&&r.why()=="the partner got out"); }
      { Rider r=on();Look l=near();l.truck=0x8000;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.truck=0;l.partnerInTruck=false;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.permitted=false;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.distance=50;l.mover=Held;CHECK(r.tick(l)==Do::Nothing&&r.riding()); }   // far from where he got on: still riding
      { Rider r=on();Look l=near();CHECK(r.tick(l)==Do::Nothing&&r.tick(l)==Do::Nothing&&r.riding());      // still blending on: not over
        l.mover=Held;r.tick(l);l.mover=Free;CHECK(r.tick(l)==Do::Nothing&&r.riding());CHECK(r.tick(l)==Do::Nothing&&!r.riding()&&r.why()=="the game let go"); }
      { Rider r=on();r.forget();CHECK(!r.riding()&&r.tick(near())==Do::Nothing); } }
    if(failures){std::fprintf(stderr,"passenger oracle: %d failures\n",failures);return 1;}
    std::puts("passenger ok");return 0;
}
