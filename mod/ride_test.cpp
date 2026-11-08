// The partner's vehicle lifecycle against a fake game: made on the first ride, driven every tick, parked when the
// partner gets off, replaced on another kind, removed when the partner goes, forgotten when the world drops it.
#include "ride.h"
#include "providers.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mod::ride;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(std::string("ride oracle: ")+text);}
#define CHECK(x) check(bool(x),#x)
struct Game {
    std::map<uint64_t,uint8_t> vehicles;uint64_t next=0x1000;unsigned spawns=0,removes=0,places=0;bool refuse=false,noMover=false;Look lastLook{};
    std::vector<Transform> placed;
    Api api(){
        Api a;
        a.spawn=[this](uint8_t kind,const Transform&,Look look)->uint64_t{++spawns;lastLook=look;if(refuse)return 0;auto e=next+=0x1000;vehicles[e]=kind;return e;};
        a.remove=[this](uint64_t e){++removes;vehicles.erase(e);};
        a.place=[this](uint64_t e,const Transform& t){if(noMover||!vehicles.count(e))return false;++places;placed.push_back(t);return true;};
        a.alive=[this](uint64_t e,uint8_t kind){auto it=vehicles.find(e);return it!=vehicles.end()&&it->second==kind;};
        return a;
    }
};
Transform at(double x){Transform t;t.position={x,0,0};t.rotation={1,0,0,0,1,0,0,0,1};return t;}
}
int main(){
    try{
        std::vector<std::string> log;auto note=[&](const std::string& l){log.push_back(l);};
        {   // first ride makes one vehicle, every tick drives it, getting off parks it, leaving removes it
            Game g;PartnerVehicle v(g.api(),note);auto t=at(1);
            v.tick(0,true,0,nullptr);CHECK(g.spawns==0);                         // on foot: nothing made
            v.tick(0,true,2,&t);CHECK(g.spawns==1&&v.entity()&&v.kind()==2&&g.places==1);
            auto u=at(2);v.tick(50,true,2,&u);CHECK(g.spawns==1&&g.places==2&&g.placed.back().position[0]==2);
            v.tick(100,true,0,nullptr);CHECK(g.places==2&&g.removes==0&&v.entity());  // parked where he got off
            v.tick(150,true,2,&u);CHECK(g.spawns==1&&g.places==3);                // gets back on: same vehicle
            v.tick(200,false,2,&u);CHECK(g.removes==1&&!v.entity()&&g.vehicles.empty());   // partner gone
        }
        {   // another kind replaces the vehicle
            Game g;PartnerVehicle v(g.api(),note);auto t=at(1);
            v.tick(0,true,2,&t);auto first=v.entity();v.tick(50,true,1,&t);
            CHECK(g.removes==1&&g.spawns==2&&v.entity()!=first&&v.kind()==1&&g.vehicles.size()==1);
        }
        {   // the world dropped it (load): forget, never remove a stranger, make a new one on the next ride
            Game g;PartnerVehicle v(g.api(),note);auto t=at(1);
            v.tick(0,true,2,&t);g.vehicles.clear();v.tick(50,true,0,nullptr);
            CHECK(!v.entity()&&g.removes==0);v.tick(100,true,2,&t);CHECK(g.spawns==2&&v.entity());
        }
        {   // a refused spawn retries slowly and gives up after MaxFailures
            Game g;g.refuse=true;PartnerVehicle v(g.api(),note);auto t=at(1);
            for(uint64_t ms=0;ms<60000;ms+=50)v.tick(ms,true,2,&t);
            CHECK(g.spawns==PartnerVehicle::MaxFailures&&!v.entity());
        }
        {   // the speed car has no unit: never spawned
            Game g;PartnerVehicle v(g.api(),note);auto t=at(1);v.tick(0,true,3,&t);CHECK(g.spawns==0);CHECK(!supported(3)&&supported(1)&&supported(2));
        }
        {   // no mover to drive: the vehicle is removed, not left standing
            Game g;g.noMover=true;PartnerVehicle v(g.api(),note);auto t=at(1);v.tick(0,true,2,&t);CHECK(g.removes==1&&!v.entity());
        }
        {   // the partner's paint: passed to the creator; a different paint makes the vehicle again; unusual looks are default
            Game g;PartnerVehicle v(g.api(),note);auto t=at(1);
            v.tick(0,true,2,&t,{0,uint16_t(5|(7<<6))});CHECK(g.spawns==1&&g.lastLook.variant==0&&g.lastLook.paint==(5|(7<<6)));
            v.tick(50,true,2,&t,{0,uint16_t(5|(7<<6))});CHECK(g.spawns==1);
            v.tick(100,true,2,&t,{0,9});CHECK(g.spawns==2&&g.removes==1&&g.lastLook.paint==9);
            CHECK(paint(2,{6,9}).variant==0&&paint(2,{6,9}).paint==0);          // truck variant 6 (special): default
            CHECK(paint(1,{1,12}).variant==1&&paint(1,{1,12}).paint==12);       // the bike's variant 1 is kept
            CHECK(paint(2,{0,30}).paint==0&&paint(2,{0,uint16_t(31<<6)}).paint==0&&paint(2,{0,0x1000}).paint==0);   // gold/silver, upper bits
        }
        {   // presence to game transform: rider + offset in game axes, the vehicle's rotation
            auto t=vehicleTransform({10,5,20},{1,-2,3},{0.5,0,0});
            CHECK(t.position[0]==11&&t.position[1]==23&&t.position[2]==3);
            auto r=mod::rotationFromAngles({0.5,0,0});CHECK(t.rotation==r);
        }
        std::printf("ride: partner vehicle oracles passed\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
