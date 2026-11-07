#include "environment.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mod::environment {
namespace {
void require(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
bool pointer(uint64_t p) {return p>=0x10000&&p<0x0000800000000000ULL&&p%8==0;}
uint64_t add(uint64_t p,uint64_t offset) {
    require(pointer(p)&&offset<0x0000800000000000ULL-p,"invalid pointer");return p+offset;
}
bool code(const discovery::Sampler& s,Code site) {
    if(!site.size||site.size>1024||site.rva>UINT64_MAX-s.base)return false;
    uint64_t at=s.base+site.rva;bool executable=false;
    for(auto section:s.sections)if(section.executable&&at>=section.address&&at-section.address<section.size&&site.size<=section.size-(at-section.address))executable=true;
    if(!executable)return false;
    std::vector<unsigned char> bytes(site.size);
    if(!s.read(at,bytes.data(),bytes.size()))return false;
    uint64_t h=14695981039346656037ULL;for(auto b:bytes)h=(h^b)*1099511628211ULL;
    return h==site.fingerprint;
}
struct Copies {
    const discovery::Sampler& s;
    struct Copy {uint64_t at;std::vector<unsigned char> bytes;};
    std::vector<Copy> copies;
    void read(uint64_t at,void* out,size_t size) {
        require(s.read(at,out,size),"memory unreadable or torn");
        auto* b=static_cast<unsigned char*>(out);copies.push_back({at,{b,b+size}});
    }
    template<class T> T value(uint64_t at) {T v{};read(at,&v,sizeof v);return v;}
    uint64_t ptr(uint64_t at) {auto p=value<uint64_t>(at);require(pointer(p),"invalid pointer");return p;}
    bool unchanged() const {
        for(const auto& c:copies){std::vector<unsigned char> bytes(c.bytes.size());if(!s.read(c.at,bytes.data(),bytes.size())||bytes!=c.bytes)return false;}
        return true;
    }
};
void validated(const discovery::Sampler& s) {require(s.signatures.size()==6,"sampler not validated");}
}
Observation Reader::read(const std::array<double,3>& position) const {
    Observation o;const auto& s=memory_;const auto& p=sites_;
    try {
        validated(s);
        require(s.region(s.base+p.application,8,true),"application slot outside image data");
        require(code(s,p.getTime)&&code(s,p.getCycle),"time getter signature differs");
        Copies c{s,{}};
        o.application=c.ptr(s.base+p.application);o.worldTime=c.ptr(add(o.application,0x58));
        o.hour=c.value<float>(add(o.worldTime,0x20));
        require(std::isfinite(o.hour)&&o.hour>=0&&o.hour<24,"time outside [0,24)");
        auto enabled=c.value<uint8_t>(add(o.worldTime,0x2c)),paused=c.value<uint8_t>(add(o.worldTime,0x2d));
        require(enabled<=1&&paused<=1,"cycle flags invalid");o.cycle=enabled&&!paused;
        require(c.unchanged(),"time observation changed");
        o.available=true;o.reason="static time path; live meaning unverified";
    } catch(const std::exception& e) {Observation unavailable;unavailable.reason=e.what();return unavailable;}
    // Independent weather subsystem: a failure cannot manufacture a dry-weather observation.
    try {
        require(s.region(s.base+p.weather,8,true),"weather slot outside image data");
        require(code(s,p.getRain)&&code(s,p.rainBody)&&code(s,p.mapBody),"weather getter signature differs");
        Copies c{s,{}};o.weather=c.ptr(s.base+p.weather);
        o.forecastTime=c.value<float>(add(o.weather,0x64a8));
        require(std::isfinite(o.forecastTime)&&o.forecastTime>=0,"forecast time invalid");
        for(double x:position)require(std::isfinite(x)&&std::abs(x)<=100000,"position invalid");
        // GetRainyConditionFromMap projects game x/y (z is up), clamps, then bilinearly samples float4.z.
        double origin[3];float scale[4],offset[4];
        c.read(add(o.weather,8),origin,sizeof origin);c.read(add(o.weather,0x70),scale,sizeof scale);c.read(add(o.weather,0x80),offset,sizeof offset);
        auto width=c.value<int32_t>(add(o.weather,0x38)),height=c.value<int32_t>(add(o.weather,0x3c));
        auto factor=c.value<int32_t>(add(o.weather,0x6564));auto map=c.ptr(add(o.weather,0x64f0));
        require(width>0&&height>0&&width<=4096&&height<=4096&&factor>0&&factor<=16&&int64_t(width)*height*factor*factor<=16777216,"weather grid invalid");
        require(s.region(s.base+p.mapScale,8,true),"weather cache outside image data");
        float cached[2];c.read(s.base+p.mapScale,cached,sizeof cached);
        require(cached[0]==factor&&cached[1]==factor,"weather sampler cache uninitialized or different");
        int dims[2]={width*factor,height*factor},lo[2],hi[2];float fraction[2];
        for(int k=0;k<2;++k){
            require(std::isfinite(origin[k])&&std::isfinite(scale[k])&&std::isfinite(offset[k]),"weather mapping non-finite");
            float local=float(position[k]-origin[k]);float cell=std::clamp(local*scale[k]+offset[k],0.f,float((k?height:width)-1));
            cell=std::clamp(cell*factor-.5f,0.f,float(dims[k]-1));
            require(std::isfinite(cell),"weather coordinate non-finite");
            lo[k]=int(std::floor(cell));hi[k]=std::min(lo[k]+1,dims[k]-1);fraction[k]=cell-lo[k];
        }
        float channel=0;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x){
            uint64_t index=uint64_t(y?hi[1]:lo[1])*dims[0]+unsigned(x?hi[0]:lo[0]);
            float z=c.value<float>(add(map,16*index+8));
            require(std::isfinite(z)&&z>=0&&z<=1,"weather channel invalid");
            channel+=z*(x?fraction[0]:1-fraction[0])*(y?fraction[1]:1-fraction[1]);
        }
        o.rainFromMap=channel*4-2;
        require(c.unchanged(),"weather observation changed");
        o.weatherAvailable=true;o.weatherReason="signed map condition; visual rain/timefall mapping unverified";
    } catch(const std::exception& e) {o.weather=0;o.forecastTime=o.rainFromMap=0;o.weatherReason=e.what();}
    return o;
}
bool Reader::setterVerified() const {
    try {validated(memory_);return code(memory_,sites_.setTime)&&code(memory_,sites_.setTimeBody);}catch(const std::exception&){return false;}
}
std::string Sync::tick(const Input& in) {
#ifndef MUTANT_ENV_ENABLE
    if(!in.enabled||!in.guest)return {};
#endif
#ifndef MUTANT_ENV_THREAD
    if(!in.gameThread||!in.networkEnforced)return {};
#endif
#ifndef MUTANT_ENV_GAMEPLAY
    if(!in.gameplay||!in.connected||!in.host)return {};
#endif
    if(!in.host)return {};
#ifndef MUTANT_ENV_RATE
    if(applied_&&(in.nowMs<last_||in.nowMs-last_<PeriodMs))return {};
#endif
    if(in.host->timeOfDayMs>=ds::EnvironmentDayMs)return "ENVIRONMENT refused: host time out of range\n";
    auto local=reader_.read(in.gamePosition);
#ifndef MUTANT_ENV_READ
    if(!local.available)return "ENVIRONMENT refused: local time unavailable\n";
#endif
#ifndef MUTANT_ENV_SIGNATURE
    if(!reader_.setterVerified())return "ENVIRONMENT refused: setter signature differs\n";
#endif
    if(!setter_)return "ENVIRONMENT refused: setter unavailable\n";
    float hour=std::min(std::nextafter(24.f,0.f),float(in.host->timeOfDayMs)/3600000.f);
    // Avoid repeatedly advancing the native day counter for tiny backwards clock corrections.
    if(std::abs(std::remainder(double(local.hour)-hour,24.))<=.01)return {};
    applied_=true;last_=in.nowMs;
    setter_(hour,0.f);
    std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<"ENVIRONMENT applied hour="<<hour<<" previous="<<local.hour<<" weather=unknown (time only)\n";
    return out.str();
}
uint32_t fixedTime(const std::string& s) {
    if(s.empty()||s.size()>24||s.find_first_not_of("0123456789.")!=std::string::npos||std::count(s.begin(),s.end(),'.')>1)throw std::invalid_argument("environment time requires decimal hours [0,24)");
    size_t used=0;double hour=std::stod(s,&used);
    if(used!=s.size()||!std::isfinite(hour)||hour<0||hour>=24)throw std::invalid_argument("environment time outside [0,24)");
    return std::min(ds::EnvironmentDayMs-1,uint32_t(std::llround(hour*3600000)));
}
bool readCommand(const std::string& content,const std::string& runStamp) {
    return !runStamp.empty()&&content=="read "+runStamp+"\n";
}
std::string row(const Observation& o) {
    std::ostringstream out;out<<std::setprecision(9)<<"ENVIRONMENT {\"available\":"<<(o.available?"true":"false")<<",\"hour\":";
    if(o.available)out<<o.hour;else out<<"null";
    out<<",\"cycle\":";if(o.available)out<<(o.cycle?"true":"false");else out<<"null";
    out<<",\"weather_available\":"<<(o.weatherAvailable?"true":"false")<<",\"forecast_time\":";
    if(o.weatherAvailable)out<<o.forecastTime;else out<<"null";
    out<<",\"rain_from_map\":";if(o.weatherAvailable)out<<o.rainFromMap;else out<<"null";
    out<<",\"reason\":\""<<o.reason<<"\",\"weather_reason\":\""<<o.weatherReason<<"\",\"provisional\":true}\n";return out.str();
}
}
