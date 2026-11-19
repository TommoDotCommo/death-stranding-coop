#include "roadshare.h"
#include <algorithm>
#include <cstring>
#include <sstream>
namespace mod::roadshare {
std::vector<Asked> readAsked(const discovery::Sampler& s,uint64_t base){
    std::vector<Asked> out;uint64_t slot=0,h=0,data=0;uint32_t slots=0;
    if(!s.read(base+LedgerSlot,&slot,8)||!slot||!s.read(slot,&h,8)||!h)return out;
    if(!s.read(h+MapAt,&data,8)||!data||!s.read(h+SlotsAt,&slots,4)||!slots||slots>MaxSlots)return out;
    std::vector<uint8_t> raw(size_t(slots)*SlotBytes);
    if(!s.read(data,raw.data(),raw.size()))return out;
    for(uint32_t i=0;i<slots;++i){
        const uint8_t* e=raw.data()+size_t(i)*SlotBytes;uint32_t used=0;std::memcpy(&used,e,4);if(!used)continue;
        Asked a;std::memcpy(&a.paver,e+0x10,4);std::memcpy(&a.material,e+0x14,4);std::memcpy(&a.taken,e+0x20,8);std::memcpy(&a.total,e+0x28,8);
        if(a.material<uint32_t(roads::Kinds)&&a.total>=0&&a.total<=roads::Most&&a.taken>=0)out.push_back(a);}
    return out;
}
int64_t Ledger::given(uint32_t p,uint32_t m) const {auto it=given_.find({p,m});return it==given_.end()?0:it->second;}
std::string Ledger::dump() const {std::ostringstream o;for(auto& g:given_)o<<g.first.first<<" "<<g.first.second<<" "<<g.second<<"\n";return o.str();}
Ledger Ledger::load(const std::string& text){
    Ledger l;std::istringstream in(text);uint32_t p=0,m=0;int64_t n=0;
    while(in>>p>>m>>n)if(m<uint32_t(roads::Kinds)&&n>0&&n<=roads::Most)l.given_[{p,m}]=n;
    return l;
}
std::vector<Give> plan(const std::vector<Asked>& asked,const std::vector<roads::Paver>& pavers,const std::function<roads::Set(uint64_t)>& missing,Ledger& ledger){
    std::vector<Give> out;
    for(auto& p:pavers){
#ifndef MUTANT_ROADSHARE_BUILT
        if(p.level>=2)continue;   // its road is built: nothing to give
#endif
        Give g;g.paver=p.address;g.id=p.id;bool any=false;roads::Set lack{};bool lackRead=false;
        for(auto& a:asked){
            if(a.paver!=p.id)continue;
#ifndef MUTANT_ROADSHARE_TWICE
            int64_t due=a.total-a.taken-ledger.given(a.paver,a.material);
#else
            int64_t due=a.total-a.taken;
#endif
            if(due<=0)continue;
            if(!lackRead){lack=missing(p.address);lackRead=true;}
#ifndef MUTANT_ROADSHARE_PAST_NEED
            due=std::min<int64_t>(due,lack[a.material]);
#endif
            if(due<=0)continue;
            g.set[a.material]+=int32_t(due);any=true;}
        if(any){for(int m=0;m<roads::Kinds;++m)ledger.add(p.id,uint32_t(m),g.set[size_t(m)]);out.push_back(g);}
    }
    return out;
}
}
