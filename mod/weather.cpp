#include "weather.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace mod::weather {
const char* name(Mode m){return m==Mode::Rain?"rain":m==Mode::Clear?"clear":"none";}
Mode decide(Mode current,float host,std::optional<float> local){
    if(!std::isfinite(host))return Mode::None;
    bool wet=host<Wet,dry=host>Dry;
#ifndef MUTANT_WEATHER_KEEP
    if(current==Mode::Rain&&wet)return Mode::Rain;     // ours makes this world wet now: keep it while the host is
    if(current==Mode::Clear&&dry)return Mode::Clear;
#endif
    if(!local||!std::isfinite(*local))return Mode::None;
#ifndef MUTANT_WEATHER_MARGIN
    if(wet&&*local>host+Margin)return Mode::Rain;
    if(dry&&*local<host-Margin)return Mode::Clear;
#else
    if(wet)return Mode::Rain;
    if(dry)return Mode::Clear;
#endif
    return Mode::None;
}
void Share::tick(uint64_t now,bool permitted,std::optional<float> host,const std::optional<std::array<double,3>>& at){
    if(!permitted||!host||!at){
        if(mode_!=Mode::None){log_(std::string("WEATHER ")+name(mode_)+" left to expire (no host sample)\n");mode_=Mode::None;}
        return;}
    if(checked_&&now>=lastCheck_&&now-lastCheck_<CheckMs)return;
    checked_=true;lastCheck_=now;
    std::optional<float> local;
    if(mode_==Mode::None)local=api_.conditionAt(*at);   // the base is only meaningful before ours is placed
    auto want=decide(mode_,*host,local);
    if(want!=mode_){
        std::ostringstream o;o<<std::setprecision(3)<<"WEATHER "<<name(mode_)<<" -> "<<name(want)<<" host="<<*host;
        if(local)o<<" here="<<*local;
        o<<"\n";log_(o.str());
        if(want==Mode::None){mode_=Mode::None;return;}   // the last influence lapses by itself
        mode_=want;lastPlaced_=0;
    }
    if(mode_==Mode::None)return;
#ifndef MUTANT_WEATHER_RENEW
    if(lastPlaced_&&now>=lastPlaced_&&now-lastPlaced_<RenewMs)return;
#endif
    if(api_.influence(mode_,*at,Radius,Lifetime)){lastPlaced_=now?now:1;++placed_;}
    else{log_("WEATHER influence refused (no weather state)\n");mode_=Mode::None;}
}
#ifdef _WIN32
namespace {
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){return s.read(a,&v,8);}
}
#endif
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native weather API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    // (W, const double* position, float radius, float lifetime, void* owner, bool detach): rain target -1 type 2 /
    // clear target +1 type 0, both into 0x2e15df0 under W's lock.
    const std::array<Entry,2> entries={{
        {0x2e14f60,{0x48,0x83,0xec,0x48,0x0f,0xb6,0x44,0x24,0x78,0xc5,0xfa,0x10,0x05,0x97,0xa3,0xd7,0x00,0x88,0x44,0x24,0x38,0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x44,0x24,0x30,0xc6}},
        {0x2e14fa0,{0x48,0x83,0xec,0x48,0x0f,0xb6,0x44,0x24,0x78,0xc5,0xfa,0x10,0x05,0xe3,0x2f,0xd7,0x00,0x88,0x44,0x24,0x38,0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x44,0x24,0x30,0xc6}}}};
    std::array<unsigned,2> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"weather code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"weather code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"weather function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"weather function signature missing/ambiguous");
    using InfluenceFn=void(*)(uint64_t,const double*,float,float,void*,bool);
    auto rain=reinterpret_cast<InfluenceFn>(s.base+entries[0].rva),clear=reinterpret_cast<InfluenceFn>(s.base+entries[1].rva);
    static char owner;   // the identity key of the mod's one influence; non-null, so the game's save writer skips it
    const uint64_t slot=s.base+0x7bf83b8;auto* sp=&s;
    Api api;
    api.influence=[rain,clear,slot,sp](Mode m,const std::array<double,3>& at,float radius,float lifetime){
        uint64_t w=0;if(!q(*sp,slot,w)||!w||m==Mode::None)return false;
        for(double x:at)if(!std::isfinite(x))return false;
        alignas(16) double position[4]={at[0],at[1],at[2],0};
        (m==Mode::Rain?rain:clear)(w,position,radius,lifetime,&owner,false);return true;};
    return api;
#endif
}
}
