#include "groupweather.h"
#include <algorithm>
#include <cmath>
namespace mod::groupweather {
namespace {constexpr int64_t Half=int64_t(1)<<25;}
int64_t cellKey(double x,double y){
    const int64_t i=int64_t(std::floor(x/CellMetres))+Half,j=int64_t(std::floor(y/CellMetres))+Half;
    return (std::clamp<int64_t>(i,0,2*Half-1)<<26)|std::clamp<int64_t>(j,0,2*Half-1);
}
std::array<double,2> cellCentre(int64_t key){
    const int64_t i=(key>>26)-Half,j=(key&((int64_t(1)<<26)-1))-Half;
    return {(double(i)+0.5)*CellMetres,(double(j)+0.5)*CellMetres};
}
int64_t wetness(float c){if(!std::isfinite(c))return 0;if(c< -1.5f)return 1000;if(c< -0.5f)return 500;return 0;}
bool Rain::holds(int64_t cell,uint64_t now) const {
    for(auto& h:held_)if(h.used&&h.cell==cell&&now-h.placed<HoldMs)return true;
    return false;
}
std::optional<std::pair<int64_t,int64_t>> Rain::report(uint64_t now,const std::array<double,3>& here,std::optional<float> condition){
    if(!condition)return std::nullopt;
    const int64_t cell=cellKey(here[0],here[1]),w=wetness(*condition);
    if(!w)return std::nullopt;
#ifndef MUTANT_GROUPWEATHER_ECHO
    if(holds(cell,now))return std::nullopt;   // the map includes the mod's own rain
#endif
    auto it=said_.find(cell);
    if(it!=said_.end()&&now-it->second<ResendMs)return std::nullopt;
    said_[cell]=now;
    for(auto s=said_.begin();s!=said_.end();)if(now-s->second>WetForMs)s=said_.erase(s);else ++s;
    return std::make_pair(cell,w);
}
void Rain::heard(int64_t cell,int64_t value,uint64_t now){if(value>0)wet_[cell]=now;}
std::vector<Place> Rain::apply(uint64_t now,const std::array<double,3>& here){
    for(auto it=wet_.begin();it!=wet_.end();)if(now-it->second>WetForMs)it=wet_.erase(it);else ++it;
    std::vector<std::pair<double,int64_t>> near;
    for(auto& w:wet_){auto c=cellCentre(w.first);const double d=std::hypot(c[0]-here[0],c[1]-here[1]);
#ifndef MUTANT_GROUPWEATHER_ANY_DISTANCE
        if(d>ReachMetres)continue;
#endif
        near.push_back({d,w.first});}
    std::sort(near.begin(),near.end());
    std::vector<Place> out;
    for(auto& n:near){
        size_t slot=Slots;for(size_t i=0;i<Slots;++i)if(held_[i].used&&held_[i].cell==n.second)slot=i;
        if(slot<Slots&&now-held_[slot].placed<RenewMs)continue;   // renewed recently
        if(slot==Slots){   // a free slot, or the oldest one whose rain has lapsed
            for(size_t i=0;i<Slots&&slot==Slots;++i)if(!held_[i].used||now-held_[i].placed>=HoldMs)slot=i;
            if(slot==Slots)continue;   // every slot holds a nearer cell
        }
        held_[slot]={n.second,now,true};auto c=cellCentre(n.second);
        out.push_back({n.second,{c[0],c[1],here[2]},slot});}
    return out;
}
}
