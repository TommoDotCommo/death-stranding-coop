// Shared rain against a fake weather map: when the host is wet/dry and this world disagrees, the mod's influence is
// placed at the host and renewed while the host stays in that class; agreement places nothing; no sample, no place.
#include "weather.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mod::weather;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(std::string("weather oracle: ")+text);}
#define CHECK(x) check(bool(x),#x)
struct Map {
    float base=1.f;Mode placedMode=Mode::None;unsigned places=0;bool refuse=false;std::vector<std::array<double,3>> at;
    Api api(){Api a;
        a.conditionAt=[this](const std::array<double,3>&)->std::optional<float>{return placedMode==Mode::Rain?-1.f:placedMode==Mode::Clear?1.f:base;};
        a.influence=[this](Mode m,const std::array<double,3>& p,float r,float l){if(refuse)return false;check(r>0&&l>0,"radius/lifetime");placedMode=m;++places;at.push_back(p);return true;};
        return a;}
};
const std::array<double,3> Here{100,200,30};
}
int main(){
    try{
        std::vector<std::string> log;auto note=[&](const std::string& l){log.push_back(l);};
        {   // host in heavy rain, here clear: rain placed at the host, renewed every RenewMs while the host stays wet
            Map m;Share s(m.api(),note);
            s.tick(0,true,-1.8f,Here);CHECK(s.mode()==Mode::Rain&&m.places==1&&m.at.back()==Here);
            for(uint64_t t=50;t<Share::RenewMs;t+=50)s.tick(t,true,-1.8f,Here);
            CHECK(m.places==1);   // not every tick
            for(uint64_t t=Share::RenewMs;t<=4*Share::RenewMs;t+=50)s.tick(t,true,-1.6f,Here);
            CHECK(s.mode()==Mode::Rain&&m.places==4);   // kept although this world now reads wet (ours)
            s.tick(4*Share::RenewMs+Share::CheckMs,true,0.f,Here);CHECK(s.mode()==Mode::None);   // host neither: lapses
            auto before=m.places;s.tick(5*Share::RenewMs,true,0.f,Here);CHECK(m.places==before);
        }
        {   // agreement: both wet, both dry, or the host in between: nothing placed
            Map m;m.base=-1.2f;Share s(m.api(),note);s.tick(0,true,-1.5f,Here);CHECK(m.places==0);
            Map d;d.base=1.f;Share t(d.api(),note);t.tick(0,true,1.2f,Here);CHECK(d.places==0);
            Map n;n.base=1.f;Share u(n.api(),note);u.tick(0,true,0.2f,Here);CHECK(n.places==0);
        }
        {   // host clear, here raining: clear placed
            Map m;m.base=-1.5f;Share s(m.api(),note);s.tick(0,true,1.5f,Here);CHECK(s.mode()==Mode::Clear&&m.places==1);
        }
        {   // no sample, no position or not permitted: nothing; an active influence is left to expire
            Map m;Share s(m.api(),note);s.tick(0,true,std::nullopt,Here);s.tick(10,true,-1.8f,std::nullopt);s.tick(20,false,-1.8f,Here);CHECK(m.places==0);
            s.tick(5000,true,-1.8f,Here);CHECK(m.places==1);s.tick(6000,false,-1.8f,Here);CHECK(s.mode()==Mode::None&&m.places==1);
        }
        {   // the game refuses (no weather state): mode drops, no retry storm
            Map m;m.refuse=true;Share s(m.api(),note);for(uint64_t t=0;t<10000;t+=50)s.tick(t,true,-1.8f,Here);CHECK(s.mode()==Mode::None);
        }
        // decide: pure rules
        CHECK(decide(Mode::None,-1.f,0.f)==Mode::Rain&&decide(Mode::None,-1.f,-0.8f)==Mode::None&&decide(Mode::None,1.f,0.f)==Mode::Clear);
        CHECK(decide(Mode::None,NAN,0.f)==Mode::None&&decide(Mode::None,-1.f,std::nullopt)==Mode::None);
        std::printf("weather: shared rain oracles passed\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
