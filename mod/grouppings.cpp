#include "grouppings.h"
#include "nametag.h"
#include <algorithm>
#include <cmath>
namespace mod::grouppings {
uint64_t markId(uint64_t sender,uint32_t serial){
    uint64_t h=1469598103934665603ull;
    for(unsigned i=0;i<8;++i){h^=uint8_t(sender>>(8*i));h*=1099511628211ull;}
    for(unsigned i=0;i<4;++i){h^=uint8_t(serial>>(8*i));h*=1099511628211ull;}
#ifndef MUTANT_GROUPPINGS_IDS
    return 0x5047000000000000ull|(h&0xffffffffffffull);
#else
    return 0x4855000000000000ull|(h&0xffffffffffffull);
#endif
}
void Pings::expire(uint64_t now){
#ifndef MUTANT_GROUPPINGS_LIFE
    while(!live_.empty()&&(now<live_.front().since||now-live_.front().since>=ShowMs))live_.pop_front();
#else
    (void)now;
#endif
}
bool Pings::take(uint64_t sender,uint32_t serial,const std::array<double,3>& at,uint64_t now){
    for(double x:at)if(!std::isfinite(x))return false;
    const auto key=std::make_pair(sender,serial);
#ifndef MUTANT_GROUPPINGS_DEDUPE
    if(std::find(seen_.begin(),seen_.end(),key)!=seen_.end())return false;
#endif
    seen_.push_back(key);if(seen_.size()>Remembered)seen_.pop_front();
    expire(now);
    // A sender's newer ping replaces his older one: one place per player, as on the partner's map pin.
    live_.erase(std::remove_if(live_.begin(),live_.end(),[&](const Ping& p){return p.sender==sender;}),live_.end());
    live_.push_back({sender,serial,at,now});
    while(live_.size()>MaxPings)live_.pop_front();
    return true;
}
std::vector<hudmarkers::Mark> Pings::marks(uint64_t now){
    expire(now);std::vector<hudmarkers::Mark> out;
    for(auto it=live_.rbegin();it!=live_.rend()&&out.size()<hudmarkers::MaxMarks;++it){
        hudmarkers::Mark m;m.x=float(it->at[0]);m.y=float(it->at[1]);m.z=float(it->at[2]);m.id=markId(it->sender,it->serial);out.push_back(m);}
    return out;
}
size_t Pings::shown(uint64_t now){expire(now);return live_.size();}
std::optional<uint64_t> likeTarget(const std::array<double,3>& here,const std::vector<Candidate>& all,double range){
    std::optional<uint64_t> best;double bestD=0;
    for(auto& c:all){
        if(!c.shown)continue;
        double d=0;for(int i=0;i<3;++i)d+=(c.at[i]-here[i])*(c.at[i]-here[i]);
        if(!std::isfinite(d))continue;
#ifndef MUTANT_GROUPPINGS_RANGE
        if(d>range*range)continue;
#else
        (void)range;
#endif
#ifndef MUTANT_GROUPPINGS_NEAREST
        if(!best||d<bestD){best=c.id;bestD=d;}
#else
        if(!best||d>bestD){best=c.id;bestD=d;}
#endif
    }
    return best;
}
bool LikeGate::accept(uint64_t sender,uint32_t serial,uint64_t now){
    const auto key=std::make_pair(sender,serial);
    if(std::find(seen_.begin(),seen_.end(),key)!=seen_.end())return false;
    auto& times=by_[sender];
    while(!times.empty()&&(now<times.front()||now-times.front()>=60000))times.pop_front();
#ifndef MUTANT_GROUPPINGS_GATE
    if(times.size()>=PerSenderPerMinute)return false;
#endif
    if(!all_.accept(now))return false;
    times.push_back(now);seen_.push_back(key);if(seen_.size()>Remembered)seen_.pop_front();
    if(by_.size()>Remembered){for(auto it=by_.begin();it!=by_.end();){if(it->second.empty())it=by_.erase(it);else ++it;}}
    return true;
}
std::u16string pingLine(const std::string& name,double metres){
    auto n=nametag::fromUtf8(name);
    while(!n.empty()&&n.back()==u' ')n.pop_back();
    if(n.empty())n=u"A player";
    if(metres<0||!std::isfinite(metres))return n+u" pinged a place.";
    if(metres<5)return n+u" pinged a place here.";
    std::string d=metres<1000?std::to_string(long(std::lround(metres)))+" m":std::to_string(long(std::lround(metres/100)))+" km";
    if(metres>=1000)d.insert(d.size()-4,".");   // tenths of a kilometre
    return n+u" pinged a place "+std::u16string(d.begin(),d.end())+u" away.";
}
}
