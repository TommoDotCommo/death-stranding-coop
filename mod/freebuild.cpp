#include "freebuild.h"
namespace mod::freebuild {
const char* label(State s){return s==State::On?"on":s==State::Off?"off":s==State::Differs?"differs":"failed";}
namespace {
const Patch* site(uint32_t rva){for(auto& p:Patches)if(p.rva==rva)return &p;return nullptr;}
bool same(uint32_t rva,uint8_t want,uint8_t got){
    if(auto* p=site(rva))return got==p->off||got==p->on;   // a neighbour's place, in either position
    return got==want;
}
}
State apply(uint64_t base,bool on,const Read& read,const Write& write){
#ifndef MUTANT_FREEBUILD_NOCHECK
    for(auto& p:Patches){
        std::array<uint8_t,10> now{};if(!read(base+p.rva-4,now.data(),now.size()))return State::Differs;
        for(unsigned i=0;i<4;++i)if(!same(p.rva-4+i,p.before[i],now[i]))return State::Differs;
        if(now[4]!=p.off&&now[4]!=p.on)return State::Differs;
        for(unsigned i=0;i<5;++i)if(!same(p.rva+1+i,p.after[i],now[5+i]))return State::Differs;
    }
#endif
    std::array<uint8_t,Patches.size()> was{};
    for(size_t k=0;k<Patches.size();++k)if(!read(base+Patches[k].rva,&was[k],1))return State::Differs;
    for(size_t k=0;k<Patches.size();++k){
        auto& p=Patches[k];const uint8_t value=on?p.on:p.off;
        if(was[k]==value)continue;
        if(!write(base+p.rva,value)){
#ifndef MUTANT_FREEBUILD_ROLLBACK
            for(size_t b=0;b<k;++b)if(was[b]!=(on?Patches[b].on:Patches[b].off))write(base+Patches[b].rva,was[b]);
#endif
            return State::Failed;
        }
    }
    return on?State::On:State::Off;
}
}
