// The porter's vitals (vitals.h): the game's control predicate, the dead bit, unknown is never dead, and the presence
// flags only for a state that lasts.
#include "vitals.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace mod::vitals;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"vitals oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    CHECK(control(2)&&control(3));CHECK(control(4|0x10)&&!control(4)&&!control(4|8));CHECK(control(8)&&!control(0)&&!control(0x10));
    const uint64_t base=0x10000,entity=base+0x100;std::vector<uint8_t> mem(0x6000);bool deny=false;
    mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){if(deny||a<base||a-base>mem.size()||n>mem.size()-(a-base))return false;std::memcpy(p,mem.data()+(a-base),n);return true;},base};
    auto set=[&](unsigned controlIndex,uint8_t v){mem[0x100+Controls+ControlStride*controlIndex]=v;};
    auto st=read(s,entity);CHECK(st.known&&!st.dead&&!st.caught&&!st.crouch);
    set(IsCrouch,8);CHECK(read(s,entity).crouch&&!read(s,entity).dead);
    set(IsDead,2);CHECK(read(s,entity).dead);set(IsDead,0);
    mem[0x100+DeadWord]=0x80;CHECK(read(s,entity).dead);mem[0x100+DeadWord]=0x7f;CHECK(!read(s,entity).dead);mem[0x100+DeadWord]=0;
    set(IsDrag,8);CHECK(read(s,entity).caught);set(IsDrag,4);CHECK(!read(s,entity).caught);set(GazerCatch,4|0x10);CHECK(read(s,entity).caught);set(GazerCatch,0);
    CHECK(!read(s,0).known&&!read(s,0).dead);deny=true;CHECK(!read(s,entity).known&&!read(s,entity).dead);deny=false;
    State alive;alive.known=true;State dead=alive;dead.dead=true;State caught=alive;caught.caught=true;State unknown;
    Reporter r;CHECK(r.flags(1000,alive)==0);
    CHECK(r.flags(2000,dead)==0);CHECK(r.flags(2000+Reporter::HoldMs-1,dead)==0);CHECK(r.flags(2000+Reporter::HoldMs,dead)==ds::Down);
    CHECK(r.flags(3000,unknown)==ds::Down);                               // a reload does not raise or drop anything by itself
    CHECK(r.flags(4000,alive)==ds::Down);CHECK(r.flags(4000+Reporter::ClearMs,alive)==0);
    Reporter f;f.flags(1000,alive);f.flags(2000,dead);CHECK(f.flags(2100,alive)==0&&f.flags(2100+Reporter::HoldMs,alive)==0);   // a flicker
    Reporter c;c.flags(1000,alive);c.flags(2000,caught);CHECK(c.flags(2000+Reporter::HoldMs,caught)==ds::Caught);
    State both=dead;both.caught=true;c.flags(5000,both);CHECK(c.flags(5000+Reporter::ClearMs,both)==ds::Down);   // dead wins
    Reporter u;CHECK(u.flags(1000,unknown)==0&&u.flags(9000,unknown)==0);
    // A call-out: only the shout's own pair of values; raised at once, held, and once per call.
    CHECK(calling(10.f,4.f));CHECK(!calling(10.f,0.f));CHECK(!calling(3.f,4.f));CHECK(!calling(std::nullopt,4.f));CHECK(!calling(10.f,std::nullopt));
    CallReporter cr;CHECK(cr.flags(1000,false)==0);CHECK(cr.flags(2000,true)==ds::Call);CHECK(cr.flags(2100,false)==ds::Call);   // a 0.1 s call still shows
    CHECK(cr.flags(2000+CallReporter::CallHoldMs,false)==0);
    CHECK(cr.flags(5000,true)==ds::Call);CHECK(cr.flags(5000+CallReporter::CallHoldMs,true)==0);   // a held key is one call
    CHECK(cr.flags(9000,false)==0);CHECK(cr.flags(9100,true)==ds::Call);
    if(failures)return 1;
    std::puts("vitals tests passed");return 0;
}
