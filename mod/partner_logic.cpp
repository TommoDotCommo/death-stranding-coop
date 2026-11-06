#include "partner.h"
#include "identity.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
namespace mod::partner {
namespace {
// Position just after `"key"` and its colon (whitespace allowed around the colon), or npos.
size_t value(const std::string& line,const char* key,size_t from=0){
    auto k=line.find(std::string("\"")+key+"\"",from);if(k==std::string::npos)return k;
    k+=std::char_traits<char>::length(key)+2;while(k<line.size()&&(line[k]==' '||line[k]=='\t'))++k;
    if(k>=line.size()||line[k]!=':')return std::string::npos;
    ++k;while(k<line.size()&&(line[k]==' '||line[k]=='\t'))++k;return k;
}
bool number(const std::string& line,const char* key,double& out){
    auto k=value(line,key);if(k==std::string::npos)return false;
    const char* s=line.c_str()+k;char* e=nullptr;out=std::strtod(s,&e);
    return e!=s&&std::isfinite(out);
}
}
std::vector<Sample> parseRecording(const std::string& text){
    std::vector<Sample> out;std::istringstream in(text);std::string line;double t0=-1;
    while(std::getline(in,line)){
        auto l=value(line,"local");if(l==std::string::npos||line[l]!='{')continue;
        auto end=line.find('}',l);if(end==std::string::npos)continue;
        auto local=line.substr(l,end-l);
        auto av=value(local,"available");if(av==std::string::npos||local.compare(av,4,"true"))continue;
        double t,x,y,z,yaw;
        if(!number(line,"t_us",t)||!number(local,"x",x)||!number(local,"y",y)||!number(local,"z",z)||!number(local,"yaw_deg",yaw))continue;
        if(t0<0)t0=t;
        if(!out.empty()&&(t-t0)/1000<=out.back().t)continue;   // time must advance
        out.push_back({(t-t0)/1000,{x,y,z},yaw*Pi/180});
    }
    return out;
}
ds::State Replay::at(double ms,const std::array<double,3>& origin) const {
    ds::State s{};if(samples_.empty())return s;
    double span=samples_.back().t;double t=span>0?std::fmod(std::max(ms,0.),span):0;
    size_t i=1;while(i<samples_.size()-1&&samples_[i].t<t)++i;
    auto& a=samples_[i-1];auto& b=samples_[i];double f=b.t>a.t?(t-a.t)/(b.t-a.t):0;
    auto& o=samples_.front().position;
    for(int k=0;k<3;++k)s.position[k]=origin[k]+a.position[k]+(b.position[k]-a.position[k])*f-o[k];
    double dt=(b.t-a.t)/1000;
    if(dt>0)for(int k=0;k<3;++k)s.velocity[k]=(b.position[k]-a.position[k])/dt;
    double dy=std::remainder(b.yaw-a.yaw,2*Pi);s.yaw=std::remainder(a.yaw+dy*f,2*Pi);
    if(std::hypot(s.velocity[0],s.velocity[2])>Run+.1)s.flags|=ds::Sprint;
    return s;
}
namespace {
void rechecksum(uint8_t* b){
    uint32_t h=2166136261u;for(size_t i=0;i<ds::WireSize;++i){h^=i>=120&&i<124?0:b[i];h*=16777619u;}
    for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));
}
bool checksumOK(const uint8_t* b){
    ds::Bytes c;std::copy(b,b+ds::WireSize,c.begin());rechecksum(c.data());return std::equal(c.begin()+120,c.begin()+124,b+120);
}
}
void toWire(ds::Bytes& b){
#ifndef MUTANT_PARTNER_WIRE
    std::copy(mod::WireBuild.begin(),mod::WireBuild.end(),b.begin()+16);rechecksum(b.data());
#else
    (void)b;
#endif
}
bool fromWire(uint8_t* b,size_t n){
    if(n!=ds::WireSize)return false;
#ifndef MUTANT_PARTNER_WIRE_ANY
    if(!std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),b+16))return false;
#endif
    // Like the mod: rewrite only checksum-valid frames, so a damaged frame still fails the core's checksum.
    if(checksumOK(b)){std::copy(ds::ModBuild.begin(),ds::ModBuild.end(),b+16);rechecksum(b);}
    return true;
}
}
