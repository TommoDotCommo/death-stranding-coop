#include "environment.h"
#include "config.h"
#include "fake_memory.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace mod::environment;
namespace {
void check(bool ok,const char* text) {if(!ok)throw std::runtime_error(std::string("environment oracle: ")+text);}
#define CHECK(x) check(bool(x),#x)
struct World {
    fakemem::Fake f;Sites sites;unsigned calls=0;
    std::function<void(uint64_t,size_t)> after;
    mod::discovery::Sampler s;
    World():s([this](uint64_t a,void* p,size_t n){bool ok=f.read(a,p,n);if(after)after(a,n);return ok;},f.base) {
        s.scan(mod::discovery::PinnedHash,mod::discovery::PinnedHash);
        sites.application=0x3600;sites.weather=0x3610;sites.mapScale=0x3620;
        unsigned k=0;
        for(auto* c:{&sites.getTime,&sites.getCycle,&sites.setTime,&sites.setTimeBody,&sites.getRain,&sites.rainBody,&sites.mapBody}) {
            c->rva=0x1100+0x20*k;c->size=3;f.data[c->rva]=0x90;f.data[c->rva+1]=uint8_t(++k);f.data[c->rva+2]=0xc3;
            c->fingerprint=14695981039346656037ULL;
            for(unsigned j=0;j<3;++j)c->fingerprint=(c->fingerprint^f.data[c->rva+j])*1099511628211ULL;
        }
        f.put<uint64_t>(sites.application,f.base+0x5000);f.put<uint64_t>(0x5058,f.base+0x6000);
        f.put<float>(0x6020,8);f.data[0x602c]=1;
        f.put<uint64_t>(sites.weather,f.base+0x7000);f.put<float>(0x7000+0x64a8,1234);
        f.put<double>(0x7008,100);f.put<double>(0x7010,200);f.put<double>(0x7018,30);
        f.put<int32_t>(0x7038,2);f.put<int32_t>(0x703c,2);f.put<int32_t>(0x7000+0x6564,1);
        f.put<float>(0x7070,1);f.put<float>(0x7074,1);
        f.put<uint64_t>(0x7000+0x64f0,f.base+0xe000);
        f.put<float>(sites.mapScale,1);f.put<float>(sites.mapScale+4,1);
        float z[]={0,.25,.5,1};for(unsigned j=0;j<4;++j)f.put<float>(0xe008+16*j,z[j]);
    }
    Observation read() {return Reader(s,sites).read({101,201,30});}
    void refused() {auto o=read();CHECK(!o.available&&!o.weatherAvailable&&o.hour==0&&o.worldTime==0);}
    Sync sync() {return Sync(s,[this](float hour,float seconds){CHECK(hour>=0&&hour<24&&seconds==0);++calls;f.put<float>(0x6020,hour);},sites);}
    Input input() {Input in;in.enabled=in.guest=in.gameThread=in.networkEnforced=in.gameplay=in.connected=true;in.nowMs=1000;in.host=ds::Environment{43200000};in.gamePosition={101,201,30};return in;}
};
using Test=std::pair<const char*,std::function<void()>>;
std::vector<Test> tests={
    {"read",[]{World w;auto o=w.read();CHECK(o.available&&o.hour==8&&o.cycle&&o.worldTime==w.f.base+0x6000);CHECK(o.weatherAvailable&&o.forecastTime==1234&&std::abs(o.rainFromMap+.25f)<1e-6);}},
    {"sampler",[]{World w;w.s.signatures.clear();w.refused();}},
    {"section",[]{World w;w.s.sections.back().executable=true;w.refused();}},
    {"getter",[]{for(bool cycle:{false,true}){World w;w.f.data[cycle?w.sites.getCycle.rva:w.sites.getTime.rva]^=1;w.refused();}}},
    {"null",[]{for(size_t at:{size_t(0x3600),size_t(0x5058)}){World w;w.f.put<uint64_t>(at,0);w.refused();}}},
    {"canonical",[]{World w;w.f.put<uint64_t>(0x5058,0x800000006000ULL);w.refused();}},
    {"alignment",[]{World w;w.f.put<uint64_t>(0x5058,w.f.base+0x6001);w.f.put<float>(0x6021,8);w.refused();}},
    {"range",[]{for(float x:{-1.f,24.f,25.f,std::numeric_limits<float>::infinity(),std::nanf("")}){World w;w.f.put<float>(0x6020,x);w.refused();}}},
    {"cycle",[]{World w;w.f.data[0x602d]=1;CHECK(!w.read().cycle);w.f.data[0x602d]=2;w.refused();}},
    {"unreadable",[]{World w;w.f.blocked=w.f.base+0x6020;w.refused();}},
    {"torn",[]{World w;w.f.torn=true;w.refused();}},
    {"coherent",[]{World w;unsigned n=0;w.after=[&](uint64_t a,size_t){if(a==w.f.base+0x602d&&++n==2)w.f.put<uint64_t>(0x3600,w.f.base+0x5800);};w.refused();}},
    {"weather_missing",[]{World w;w.f.put<uint64_t>(0x3610,0);auto o=w.read();CHECK(o.available&&!o.weatherAvailable&&o.rainFromMap==0);CHECK(row(o).find("\"rain_from_map\":null")!=std::string::npos);}},
    {"weather_signature",[]{World w;w.f.data[w.sites.mapBody.rva]^=1;CHECK(!w.read().weatherAvailable);}},
    {"grid_cap",[]{World w;w.f.put<int32_t>(0x7038,4097);w.f.put<int32_t>(0x703c,1);CHECK(!Reader(w.s,w.sites).read({100,200,30}).weatherAvailable);}},
    {"weather_coherent",[]{World w;unsigned n=0;w.after=[&](uint64_t a,size_t){if(a==w.f.base+0xe038&&++n==2)w.f.put<uint64_t>(0x3610,w.f.base+0x7100);};CHECK(!w.read().weatherAvailable);}},
    {"grid",[]{for(int value:{0,-1,4097}){World w;w.f.put<int32_t>(0x7038,value);CHECK(!w.read().weatherAvailable);}World w;w.f.put<int32_t>(0xd564,17);CHECK(!w.read().weatherAvailable);}},
    {"weather_cache",[]{World w;w.f.put<float>(0x3620,0);CHECK(!w.read().weatherAvailable);}},
    {"weather_channel",[]{World w;w.f.put<float>(0xe008,std::nanf(""));CHECK(!w.read().weatherAvailable);}},
    {"weather_position",[]{World w;CHECK(!Reader(w.s,w.sites).read({std::nan(""),0,0}).weatherAvailable);}},
    {"apply",[]{World w;auto sync=w.sync();CHECK(sync.tick(w.input()).find("ENVIRONMENT applied hour=12.")==0&&w.calls==1);}},
    {"enable",[]{for(int field=0;field<2;++field){World w;auto sync=w.sync();auto in=w.input();(field?in.guest:in.enabled)=false;CHECK(sync.tick(in).empty()&&w.calls==0);}}},
    {"thread",[]{for(int field=0;field<2;++field){World w;auto sync=w.sync();auto in=w.input();(field?in.networkEnforced:in.gameThread)=false;CHECK(sync.tick(in).empty()&&w.calls==0);}}},
    {"gameplay",[]{for(int field=0;field<3;++field){World w;auto sync=w.sync();auto in=w.input();if(field==0)in.gameplay=false;if(field==1)in.connected=false;if(field==2)in.host.reset();CHECK(sync.tick(in).empty()&&w.calls==0);}}},
    {"rate",[]{World w;auto sync=w.sync();auto in=w.input();sync.tick(in);CHECK(w.calls==1);in.host=ds::Environment{3600000};in.nowMs=30999;sync.tick(in);CHECK(w.calls==1);in.nowMs=999;sync.tick(in);CHECK(w.calls==1);in.nowMs=31000;sync.tick(in);CHECK(w.calls==2);}},
    {"setter_signature",[]{for(bool body:{false,true}){World w;auto sync=w.sync();w.f.data[body?w.sites.setTimeBody.rva:w.sites.setTime.rva]^=1;CHECK(sync.tick(w.input()).find("refused")!=std::string::npos&&w.calls==0);}}},
    {"local_read",[]{World w;auto sync=w.sync();w.f.put<uint64_t>(0x3600,0);sync.tick(w.input());CHECK(w.calls==0);}},
    {"host_range",[]{World w;auto sync=w.sync();auto in=w.input();in.host=ds::Environment{ds::EnvironmentDayMs};sync.tick(in);CHECK(w.calls==0);}},
    {"midnight",[]{World w;auto sync=w.sync();auto in=w.input();in.host=ds::Environment{ds::EnvironmentDayMs-1};sync.tick(in);CHECK(w.calls==1);}},
    {"drift",[]{World w;auto sync=w.sync();auto in=w.input();in.host=ds::Environment{28800001};CHECK(sync.tick(in).empty()&&w.calls==0);}},
    {"stamp",[]{CHECK(readCommand("read 123-456\n","123-456"));for(auto x:{"read 123-457\n","read 123-456","watch 123-456\n","read 123-456\nextra"})CHECK(!readCommand(x,"123-456"));CHECK(!readCommand("read \n",""));}},
    {"fixed",[]{CHECK(fixedTime("0")==0&&fixedTime("12.5")==45000000&&fixedTime("23.99999999")<ds::EnvironmentDayMs);for(auto x:{"","-1","24","nan","inf","12x","1e1","1.2.3"," 12","12 "}){bool refused=false;try{fixedTime(x);}catch(const std::exception&){refused=true;}CHECK(refused);}}},
    {"config",[]{
        const std::string base=R"({"role":"guest","peer":"127.0.0.1","slot":0,"peer_slot":1,"nickname":"test","provider":"game","session":123,"game_thread":true,"required":true,"puppet_probe":true,"discovery_sha256":")"+std::string(mod::discovery::PinnedHash)+"\"";
        CHECK(mod::parseConfig(base+"}").shareEnvironment=="off");CHECK(mod::parseConfig(base+",\"share_environment\":\"host\"}").shareEnvironment=="host");CHECK(mod::parseConfig(base+",\"share_environment\":\"rain\"}").shareEnvironment=="rain");
        for(auto value:{"true","1","\"on\"","\"guest\""}){bool refused=false;try{mod::parseConfig(base+",\"share_environment\":"+value+"}");}catch(const std::exception&){refused=true;}CHECK(refused);}
        for(auto key:{"\"game_thread\":true","\"required\":true","\"puppet_probe\":true","\"role\":\"guest\""}){auto bad=base;auto at=bad.find(key);CHECK(at!=std::string::npos);bad.replace(at,std::strlen(key),std::string(key).substr(0,std::string(key).find(':')+1)+(std::string(key).find("role")!=std::string::npos?"\"none\"":"false"));bool refused=false;try{mod::parseConfig(bad+",\"share_environment\":\"host\"}");}catch(const std::exception&){refused=true;}CHECK(refused);}
    }},
};
}
int main(int argc,char** argv) {
    try {unsigned count=0;for(const auto& t:tests)if(argc==1||t.first==std::string(argv[1])){t.second();++count;}CHECK(count);std::printf("environment %u oracles passed\n",count);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
