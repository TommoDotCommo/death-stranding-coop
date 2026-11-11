#include "puppet.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <vector>
using namespace mod::puppet;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"puppet oracle: %s\n",#x);++failures;}}while(0)
struct Scene {
    Anchor anchor;mod::Pose actor;uint64_t resource=0x2000,created=0x3000;
    unsigned resources=0,creates=0,places=0,removes=0;bool alter=false,wrongReadback=false;
    std::vector<std::string> events;
    Scene(){anchor.entity=0x1000;anchor.pose.available=anchor.pose.coherent=true;anchor.pose.position={10,20,30};anchor.pose.rotation={1,0,0,0,1,0,0,0,1};}
    Api api(){return {[this]{return anchor;},[this](uint64_t){auto p=actor;if(wrongReadback)p.position[0]+=100;return p;},
        [this](uint64_t){++resources;return resource;},
        [this](uint64_t,const Transform& t){++creates;actor.available=actor.coherent=true;actor.position=t.position;actor.rotation=t.rotation;if(alter)anchor.entity=0x999;return created;},
        [this](uint64_t,const Transform& t){++places;actor.position=t.position;},[this](uint64_t){++removes;},[](uint64_t){return true;}};}
    Probe probe(){return Probe(api(),[this](const std::string& row){events.push_back(row);});}
};
template<class F> bool refused(F f){try{f();return false;}catch(const std::exception&){return true;}}
uint64_t creator(uint64_t resource,const double* p,const Rotation* r,const char* name,bool place,uint64_t instigator,uint64_t attack,const Tagged* tag){
    CHECK(resource==0x2000&&p&&p[0]==10&&p[1]==20&&p[2]==30);
    CHECK(r&&r->rows[0][0]==1&&r->rows[1][1]==1&&r->rows[2][2]==1&&r->rows[0][3]==0);
    CHECK(name&&std::string(name)=="coop_puppet_probe");CHECK(place&&!instigator&&!attack);
    CHECK(tag&&!tag->tag&&!tag->pointer);return 0x3000;
}
void remover(uint64_t actor,bool immediate){CHECK(actor==0x3000&&!immediate);}
static void laidTests(){
    using mod::puppet::Transform;Transform t;t.position={10,20,30};t.rotation={1,0,0, 0,1,0, 0,0,1};   // facing +y, head up
    auto l=mod::puppet::laidDown(t);
    CHECK(l.rotation[0]==1&&l.rotation[1]==0&&l.rotation[2]==0);                 // right unchanged
    CHECK(l.rotation[3]==0&&l.rotation[4]==0&&l.rotation[5]==1);                 // the face to the sky
    CHECK(l.rotation[6]==0&&l.rotation[7]==-1&&l.rotation[8]==0);                // the head where the back was turned
    CHECK(l.position[0]==10&&l.position[1]==20&&l.position[2]>30.1&&l.position[2]<30.4);   // on the ground, not in it
}
int main(){
    laidTests();
    Scene scene;auto p=scene.probe();p.tick(0,true);CHECK(scene.resources==0);p.arm();p.tick(0,false);CHECK(scene.creates==0&&scene.resources==0);
    p.tick(10,true);CHECK(scene.creates==1&&scene.resources==1);CHECK(scene.actor.position[1]==23&&scene.anchor.pose.position[1]==20);
    p.tick(100,true);CHECK(scene.places==0);p.tick(1010,true);CHECK(scene.places==1&&scene.actor.position[0]==11);p.tick(2010,true);CHECK(scene.removes==1);
    p.tick(10000,true);p.arm();p.tick(20000,true);CHECK(scene.creates==1&&scene.places==1&&scene.removes==1);
    auto t=transform(scene.anchor.pose);CHECK(invokeCreate(creator,0x2000,t)==0x3000);invokeRemove(remover,0x3000);auto r=padded(t);CHECK(r.rows[0][0]==1&&r.rows[1][1]==1&&r.rows[2][2]==1&&r.rows[0][3]==0&&r.rows[2][3]==0);
    {Scene s;auto probe=s.probe();probe.arm();s.anchor.entity=0;s.anchor.pose.available=false;bool threw=refused([&]{probe.tick(0,true);});CHECK(!threw&&s.resources==0&&s.creates==0);}
    {Scene s;s.resource=0;auto probe=s.probe();probe.arm();CHECK(refused([&]{probe.tick(0,true);}));CHECK(s.creates==0);}
    {Scene s;s.created=s.anchor.entity;auto probe=s.probe();probe.arm();CHECK(refused([&]{probe.tick(0,true);}));CHECK(s.places==0&&s.removes==0);}
    {Scene s;s.alter=true;auto probe=s.probe();probe.arm();CHECK(refused([&]{probe.tick(0,true);}));CHECK(s.places==0&&s.removes==0);}
    {Scene s;s.wrongReadback=true;auto probe=s.probe();probe.arm();CHECK(refused([&]{probe.tick(0,true);}));CHECK(s.places==0&&s.removes==0);}
    {Scene s;auto probe=s.probe();probe.arm();probe.tick(0,true);s.anchor.entity++;probe.tick(1000,true);CHECK(s.places==0&&s.removes==0);}
    for(bool absent:{false,true}){
        Scene s;auto api=s.api();api.canCreate=absent?std::function<bool(uint64_t)>{}:[](uint64_t){return false;};
        Probe probe(api,[&](const std::string& row){s.events.push_back(row);});
        probe.arm();probe.tick(0,true);probe.arm();probe.tick(5000,true);
        CHECK(s.resources==1&&s.creates==0&&s.places==0&&s.removes==0);
        CHECK(std::string(probe.stage())=="resource_rejected");
        CHECK(s.events.back().find("resource_rejected")!=std::string::npos);
    }
    if(!failures)std::puts("one-shot puppet lifecycle and ABI checks passed");
    return failures?1:0;
}
