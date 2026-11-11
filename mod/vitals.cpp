#include "vitals.h"
namespace mod::vitals {
bool control(uint8_t b){
    if(b&2)return true;
#ifndef MUTANT_VITALS_BIT4
    if(b&4)return (b&0x10)!=0;
    return (b&8)!=0;
#else
    return (b&0x10)!=0;
#endif
}
State read(const discovery::Sampler& s,uint64_t entity){
    State out;if(!entity)return out;
    uint64_t word=0;uint8_t dead=0,drag=0,crouch=0,gazer=0;
    if(!s.read(entity+DeadWord,&word,8)||!s.read(entity+Controls+ControlStride*IsDead,&dead,1)||!s.read(entity+Controls+ControlStride*IsDrag,&drag,1)||
       !s.read(entity+Controls+ControlStride*IsCrouch,&crouch,1)||!s.read(entity+Controls+ControlStride*GazerCatch,&gazer,1)){
#ifndef MUTANT_VITALS_UNREADABLE_DEAD
        return out;
#else
        out.known=true;out.dead=true;return out;
#endif
    }
    out.known=true;out.dead=((word>>7)&1)||control(dead);out.caught=control(drag)||control(gazer);out.crouch=control(crouch);
    return out;
}
bool Reporter::Edge::step(uint64_t now,bool value){
    if(value!=seen){seen=value;since=now;}
#ifndef MUTANT_VITALS_NO_HOLD
    uint64_t need=value?HoldMs:ClearMs;
#else
    uint64_t need=0;
#endif
    if(value!=raised&&now-since>=need)raised=value;
    return raised;
}
uint16_t Reporter::flags(uint64_t now,const State& st){
    uint16_t f=0;
    if(st.known){down_.step(now,st.dead);caught_.step(now,st.caught&&!st.dead);}
    if(down_.raised)f|=ds::Down;
    if(caught_.raised)f|=ds::Caught;
    return f;
}
}
