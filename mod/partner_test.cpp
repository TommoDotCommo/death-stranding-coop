#include "partner.h"
#include "identity.h"
#include <algorithm>
#include <cstdio>
using namespace mod::partner;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"partner oracle: %s\n",#__VA_ARGS__);++failures;}}while(0)
static double flat(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
static ds::State porterAt(double x,double z,double yaw,double y=140){ds::State s;s.position={x,y,z};s.yaw=yaw;return s;}
int main(){
    {   // Recorded animation samples, on the walk's time base (ms from the first available local row).
        ds::Animation a;a.held[2]=4;a.floats[7]=1.5f;auto pay=ds::encodeAnimationPayload(a);std::string h;
        for(auto b:pay){char x[3];std::snprintf(x,3,"%02x",b);h+=x;}
        std::string text="{\"t_us\":500,\"animation\":\""+h+"\"}\n"                      // before any walk: no time base
            "{\"t_us\":1000000,\"local\":{\"available\":true,\"x\":1,\"y\":2,\"z\":3,\"yaw_deg\":0}}\n"
            "{\"t_us\":1250000,\"animation\":\""+h+"\"}\n"
            "{\"t_us\":1300000,\"animation\":\"zz\"}\n"                                       // malformed
            "{\"t_us\":1400000,\"animation\":\""+h.substr(0,h.size()-2)+"\"}\n";             // cut short
        auto r=parseAnimations(text);
        CHECK(r.size()==1&&r[0].first==250&&r[0].second==a);
        CHECK(parseRecording(text).size()==1);   // the walk parser skips the animation rows
    }
    {CHECK(progressValue("unknown")==ds::UnknownProgress);CHECK(progressValue("3")==3);CHECK(progressValue("4294967294")==4294967294u);
        for(auto value:{"", "-1", "1.0", "4294967295", "4294967296", "unknowns"}){bool refused=false;try{progressValue(value);}catch(const std::exception&){refused=true;}CHECK(refused);}
        ds::State state;state.position={1,2,3};auto shifted=offsetTarget(state,{600,0,0});CHECK(shifted&&shifted->position==std::array<double,3>({601,2,3}));CHECK(!offsetTarget({},{}));
        bool refused=false;try{offsetTarget(state,{10001,0,0});}catch(const std::exception&){refused=true;}CHECK(refused);
    }

    {   // Facing yaw 0 is +z, so his right is +x: the place is 3 m to +x and 1 m to -z.
        auto b=beside(porterAt(10,20,0));CHECK(std::fabs(b[0]-13)<1e-9&&std::fabs(b[2]-19)<1e-9&&b[1]==140);
        auto c=beside(porterAt(0,0,Pi/2));CHECK(std::fabs(c[0]+1)<1e-9&&std::fabs(c[2]+3)<1e-9);   // facing +x: right is -z
    }
    {   // Nothing is sent before the porter is known; the first state appears beside him, flagged as a jump.
        Follower f;CHECK(!f.step(std::nullopt,.05));
        auto s=f.step(porterAt(0,0,0),.05);CHECK(s&&(s->flags&ds::Teleport)&&flat(s->position,beside(porterAt(0,0,0)))<1e-9);
        auto t=f.step(porterAt(0,0,0),.05);CHECK(t&&!(t->flags&ds::Teleport)&&f.speed()==0);
        CHECK(f.step(std::nullopt,.05).has_value());   // the porter's packets pause: the partner stays where he is
    }
    {   // The porter walks off: the partner speeds up gradually, follows at walking pace, faces where he goes, and arrives.
        Follower f;double z=0;f.step(porterAt(0,z,0),.05);double maxSpeed=0,lastSpeed=0;bool smooth=true;
        for(int i=0;i<200;++i){z+=1.5*.05;auto s=f.step(porterAt(0,z,0),.05);double v=f.speed();
            if(v-lastSpeed>Accel*.05+1e-9){smooth=false;}
            lastSpeed=v;maxSpeed=std::max(maxSpeed,v);
            if(i>40)CHECK(std::fabs(s->yaw)<.3);}
        CHECK(smooth&&maxSpeed<=Run+1e-9);
        for(int i=0;i<200;++i)f.step(porterAt(0,z,0),.05);
        auto s=f.step(porterAt(0,z,0),.05);CHECK(flat(s->position,beside(porterAt(0,z,0)))<=Arrived+1e-9&&f.speed()==0);
    }
    {   // Far behind: sprint (flagged), then run, then walk; a jump across the map is a teleport.
        Follower f;f.step(porterAt(0,0,0),.05);
        bool sprinted=false;for(int i=0;i<40;++i){auto s=f.step(porterAt(0,40,0),.05);if(s->flags&ds::Sprint)sprinted=true;}
        CHECK(sprinted&&f.speed()>Run);
        auto t=f.step(porterAt(500,500,1),.05);CHECK((t->flags&ds::Teleport)&&flat(t->position,beside(porterAt(500,500,1)))<1e-9);
        auto u=f.step(porterAt(500,500,1),.05);CHECK(!(u->flags&ds::Teleport));
    }
    {   // Velocity matches the movement, so the receiving side animates the right gait.
        Follower f;f.step(porterAt(0,0,0),.05);for(int i=0;i<30;++i)f.step(porterAt(0,6,0),.05);
        auto a=f.step(porterAt(0,6,0),.05);double v=std::hypot(a->velocity[0],a->velocity[2]);CHECK(v>1&&std::fabs(v-f.speed())<.2);
    }
    {   // Recording: only available local poses, time from the first row, degrees to radians; replay loops.
        std::string text=
            "{\"t_us\":1000000,\"local\":{\"available\":false},\"remote\":{}}\n"
            "{\"t_us\":2000000,\"local\":{\"available\":true,\"x\":10,\"y\":5,\"z\":20,\"yaw_deg\":90,\"speed\":0},\"remote\":{\"visible\":false}}\n"
            "garbage\n"
            "{\"t_us\":3000000,\"local\":{\"available\":true,\"x\":12,\"y\":5,\"z\":20,\"yaw_deg\":90,\"speed\":2},\"remote\":{\"visible\":true,\"x\":1,\"y\":2,\"z\":3,\"yaw_deg\":4}}\n"
            "{\"t_us\":3000000,\"local\":{\"available\":true,\"x\":99,\"y\":5,\"z\":20,\"yaw_deg\":90}}\n"
            "{\"t_us\": 3500000, \"local\": {\"available\": true, \"x\": 12, \"y\": 5, \"z\": 22, \"yaw_deg\": 0}}\n"
            "{\"t_us\":4000000,\"local\":{\"available\":true,\"x\":12,\"y\":5,\"z\":24,\"yaw_deg\":0}}\n";
        auto rows=parseRecording(text);CHECK(rows.size()==4);   // spaced JSON (Python's json.dumps) parses too
        rows.erase(rows.begin()+2);
        CHECK(rows.size()==3&&rows[0].t==0&&rows[1].t==1000&&rows[2].t==2000&&std::fabs(rows[0].yaw-Pi/2)<1e-9&&rows[1].position[0]==12);
        Replay r(rows);CHECK(r.usable()&&r.span()==2000&&r.first()[0]==10);
        CHECK(std::fabs(r.at(0,r.first()).position[0]-10)<1e-9);   // played where it was recorded
        auto a=r.at(500,{0,0,0});CHECK(std::fabs(a.position[0]-1)<1e-9&&std::fabs(a.velocity[0]-2)<1e-9&&std::fabs(a.yaw-Pi/2)<1e-9);
        auto b=r.at(2500,{100,0,0});CHECK(std::fabs(b.position[0]-101)<1e-9);   // looped
        auto c=r.at(1500,{0,0,0});CHECK(std::fabs(c.position[2]-2)<1e-9&&std::fabs(c.yaw-Pi/4)<1e-9);
    }
    {   // wire identity: what the partner sends carries the mod's adapter identity; what it accepts is turned back
        ds::Packet q;q.kind=ds::Kind::Hello;q.session=7;auto core=ds::encode(q);
        auto wire=core;toWire(wire);
        CHECK(std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),wire.begin()+16));
        auto back=wire;CHECK(fromWire(back.data(),back.size())&&back==core);           // the mod's frames reach the core intact
        auto plain=core;CHECK(!fromWire(plain.data(),plain.size())&&plain==core);       // a core-identity frame is refused
        auto broken=wire;broken[40]^=1;CHECK(fromWire(broken.data(),broken.size()));
        CHECK(std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),broken.begin()+16)); // damaged frames are not repaired
        CHECK(!fromWire(wire.data(),wire.size()-1));
    }
    if(!failures)std::puts("partner follow and replay checks passed");
    return failures?1:0;
}
