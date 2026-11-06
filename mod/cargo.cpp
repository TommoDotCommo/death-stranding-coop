#include "cargo.h"
#include <algorithm>
#include <cstring>
#include <cmath>
namespace mod::cargo {
namespace {
constexpr uint64_t Records=0x20,Stride=0x240;constexpr unsigned Slots=511;
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
bool placed(const std::array<double,3>& p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&(p[0]!=0||p[1]!=0||p[2]!=0);}
}
std::vector<Record> readRecords(const discovery::Sampler& s,uint64_t manager,uint64_t vtable){
    std::vector<Record> out;uint64_t v=0;
    if(!manager||!s.read(manager,&v,8)||v!=vtable)return out;
    std::vector<uint8_t> block(Stride);
    for(unsigned i=0;i<Slots;++i){
        if(!s.read(manager+Records+Stride*i,block.data(),block.size()))break;
        uint64_t item=0;std::memcpy(&item,block.data()+0x28,8);if(!item)continue;
        Record r;std::memcpy(&r.slot,block.data()+0x18,4);std::memcpy(&r.state,block.data()+0x20,4);std::memcpy(&r.hash,block.data()+0xc8,4);
        std::memcpy(r.position.data(),block.data()+0x38,24);std::memcpy(&r.holder,block.data()+0x1c,4);out.push_back(r);
    }
    return out;
}
void Sync::expect(const Placement& p,uint64_t now){expected_.push_back({p,now});}
size_t Sync::mirrored() const {return size_t(std::count_if(tracks_.begin(),tracks_.end(),[](auto& t){return t.second.mirror;}));}
std::vector<Placement> Sync::observe(const std::vector<Record>& records,const std::array<double,3>& porter,uint64_t now){
    std::vector<Placement> out;
    expected_.erase(std::remove_if(expected_.begin(),expected_.end(),[&](auto& e){return now-e.second>ExpectFor;}),expected_.end());
    std::map<uint32_t,Track> next;
    for(auto& r:records){
        auto old=tracks_.find(r.slot);bool same=old!=tracks_.end()&&old->second.hash==r.hash;
        Track t=same?old->second:Track{};
        if(!same){t.hash=r.hash;t.position=r.position;t.stillSince=now;t.fresh=primed_;}
        // Moved: not settled. A placement is announced once; picking it up again is not synchronised yet, so a later
        // drop of the same record must not create a second copy in the partner's world.
        if(distance(t.position,r.position)>Still){t.position=r.position;t.stillSince=now;}
        if(placed(r.position)&&distance(r.position,porter)<=HeldRange)t.heldAt=now;
        if(!same&&primed_){
            // Created for a partner placement: same item, appearing where it was asked for.
            for(auto it=expected_.begin();it!=expected_.end();++it)
#ifndef MUTANT_CARGO_ECHO
                if(it->first.hash==r.hash&&distance(it->first.position,r.position)<=MatchRange){t.mirror=true;expected_.erase(it);break;}
#else
                {(void)it;break;}
#endif
        }
        bool settled=placed(r.position)&&now-t.stillSince>=Settle;
        bool ours=(t.fresh&&distance(r.position,porter)<=NearRange)||(t.heldAt&&now-t.heldAt<=HeldMemory);
#ifndef MUTANT_CARGO_HELD
        bool letGo=distance(r.position,porter)>=AwayRange||t.fresh;
#else
        bool letGo=true;
#endif
        if(primed_&&!t.mirror&&!t.announced&&settled&&ours&&letGo){out.push_back({r.hash,r.position});t.announced=true;t.fresh=false;}
        next[r.slot]=t;
    }
    tracks_.swap(next);primed_=true;
    return out;
}
}
