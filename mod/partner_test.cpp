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
    {   // the longer ANIMATION frame: the same identity both ways; another identity, a damaged frame, a wrong size
        ds::Packet q;q.kind=ds::Kind::Animation;q.session=7;q.animation.floats[5]=.5f;q.animation.held[1]=2;
        auto core=ds::encodeAnimation(q);CHECK(core.size()>ds::WireSize);
        auto wire=core;toWireLong(wire);CHECK(std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),wire.begin()+16));
        auto back=wire;CHECK(fromWireLong(back)&&back==core);
        auto plain=core;CHECK(!fromWireLong(plain)&&plain==core);                       // the core's own identity is not the mod's
        auto broken=wire;broken[130]^=1;CHECK(fromWireLong(broken)&&std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),broken.begin()+16));
        std::vector<uint8_t> small(wire.begin(),wire.begin()+ds::WireSize),big(ds::AnimationWireMax+1,0);
        std::copy(mod::WireBuild.begin(),mod::WireBuild.end(),big.begin()+16);
        CHECK(!fromWireLong(small)&&!fromWireLong(big));
    }
    {   // what other players are sent: the controls, without the hands, the wheels and the carts
        ds::Animation a;a.held[3]=1;a.rose[3]=1;a.floats[9]=2.5f;a.handCount=1;a.hands[0].item=5;a.jointCount=2;a.skeleton=40;a.cartCount=1;
        auto x=controlsOnly(a);CHECK(x.held==a.held&&x.rose==a.rose&&x.floats==a.floats&&!x.handCount&&!x.jointCount&&!x.cartCount&&!x.skeleton);
    }
    {   // the animation cursor: the newest sample not yet sent, each once per loop; an unsent one is offered again
        std::vector<std::pair<double,ds::Animation>> samples(3);samples[0].first=100;samples[1].first=200;samples[2].first=900;
        AnimationCursor c;CHECK(c.due(samples,50,1000)==-1);
        CHECK(c.due(samples,150,1000)==0);CHECK(c.due(samples,160,1000)==0);c.sent(0);CHECK(c.due(samples,170,1000)==-1);
        CHECK(c.due(samples,950,1000)==2);c.sent(2);CHECK(c.due(samples,990,1000)==-1);   // one passed over is not sent late
        CHECK(c.due(samples,1050,1000)==-1);CHECK(c.due(samples,1150,1000)==0);            // the next loop: from the start again
        AnimationCursor none;CHECK(none.due({},100,1000)==-1&&none.due(samples,100,0)==-1);
    }
    {   // a stand-in on its circle: where it starts, how far a speed takes it, which way it faces, and standing still
        CircleWalker w({100,50,-200},10,0);
        auto s=w.step(0,.1);CHECK(std::fabs(s.position[0]-100)<1e-9&&s.position[1]==50&&std::fabs(s.position[2]+190)<1e-9);
        CHECK(s.velocity==std::array<double,3>({0,0,0})&&std::fabs(std::fabs(s.yaw)-Pi)<1e-9&&w.walked()==0);   // standing: faces the centre
        for(int i=0;i<10;++i)s=w.step(1.6,.1);                                                // 1.6 m along a 10 m circle: 0.16 rad
        CHECK(std::fabs(w.walked()-1.6)<1e-9&&std::fabs(s.position[0]-(100+10*std::sin(.16)))<1e-6&&std::fabs(s.position[2]-(-200+10*std::cos(.16)))<1e-6);
        CHECK(std::fabs(std::hypot(s.velocity[0],s.velocity[2])-1.6)<1e-9&&s.velocity[0]>1.5&&!(s.flags&ds::Sprint));
        CHECK(std::fabs(std::sin(s.yaw)*1.6-s.velocity[0])<1e-9&&std::fabs(std::cos(s.yaw)*1.6-s.velocity[2])<1e-9);   // faces the way he walks
        auto walkingYaw=s.yaw;s=w.step(0,.1);CHECK(s.yaw==walkingYaw&&s.velocity[0]==0&&std::fabs(w.walked()-1.6)<1e-9);   // stops: keeps facing
        s=w.step(5.8,1);CHECK((s.flags&ds::Sprint)&&std::fabs(w.walked()-7.4)<1e-9);
        CircleWalker still({1,2,3},0,1);s=still.step(3,1);CHECK(s.position==std::array<double,3>({1,2,3})&&still.walked()==0&&s.velocity[0]==0);
    }
    if(!failures)std::puts("partner follow and replay checks passed");
    return failures?1:0;
}
