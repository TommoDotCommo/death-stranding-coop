// Other players' vehicles in group play (othervehicles.h) against a fake game: a vehicle for each player who rides,
// parked when he gets off, gone with him, at most Max at once, pools used again, all gone in a game event, forgotten
// when the world drops them; and which vehicle a passenger gets on.
#include "othervehicles.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace mod::othervehicles;
using mod::ride::Api;using mod::ride::Transform;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"othervehicles oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
struct Game {
    std::map<uint64_t,uint8_t> vehicles;uint64_t next=0x1000;unsigned spawns=0,removes=0,pools=0;bool broken=false;
    std::optional<Api> make(){
        if(broken)return std::nullopt;
        ++pools;Api a;
        a.spawn=[this](uint8_t kind,const Transform&,mod::ride::Look)->uint64_t{++spawns;auto e=next+=0x1000;vehicles[e]=kind;return e;};
        a.remove=[this](uint64_t e){++removes;vehicles.erase(e);};
        a.place=[this](uint64_t e,const Transform&){return vehicles.count(e)>0;};
        a.alive=[this](uint64_t e,uint8_t kind){auto it=vehicles.find(e);return it!=vehicles.end()&&it->second==kind;};
        return a;}
};
static Seen rider(uint64_t id,uint8_t kind,double x=0){Seen s;s.id=id;s.visible=true;s.kind=kind;s.at.position={x,0,0};s.at.rotation={1,0,0,0,1,0,0,0,1};return s;}
static void fleet(){
    Game g;std::vector<std::string> log;Fleet f([&]{return g.make();},[&](const std::string& l){log.push_back(l);});
    // On foot: nothing. A truck: one vehicle. A bike for another: a second, each with its own pool.
    f.tick(0,true,{rider(1,0)});CHECK(f.size()==0&&g.spawns==0);
    f.tick(100,true,{rider(1,2)});CHECK(f.size()==1&&g.spawns==1&&g.pools==1);
    f.tick(200,true,{rider(1,2),rider(2,1)});CHECK(f.size()==2&&g.spawns==2&&g.pools==2&&g.vehicles.size()==2);
    auto vs=f.vehicles();CHECK(vs.size()==2&&vs[0].id==1&&vs[0].kind==2&&vs[0].driven&&vs[1].kind==1&&vs[1].at.position[0]==0);
    f.tick(250,true,{rider(1,2,7),rider(2,1)});CHECK(f.vehicles()[0].at.position[0]==7);   // where it was last put
    // He gets off: it stays where it is, not driven.
    f.tick(300,true,{rider(1,0),rider(2,1)});CHECK(f.size()==2&&g.vehicles.size()==2&&!f.vehicles()[0].driven);
    // Player 2 not seen: his vehicle stays a moment, then goes with him; its pool is used for the next one.
    f.tick(1000,true,{rider(1,0)});CHECK(g.vehicles.size()==2);
    f.tick(300+GoneMs+1,true,{rider(1,0)});CHECK(f.size()==1&&g.vehicles.size()==1&&g.removes==1);
    f.tick(5000,true,{rider(1,0),rider(3,2)});CHECK(f.size()==2&&g.pools==2&&g.spawns==3);
    // A game event: every vehicle goes; afterwards they come back with their riders.
    f.tick(6000,false,{rider(1,0),rider(3,2)});CHECK(g.vehicles.empty());
    f.tick(6100,true,{rider(3,2)});CHECK(g.vehicles.size()==1);
    // A load: forgotten without calling the game, pools kept for later.
    unsigned removes=g.removes;f.forget("load");CHECK(f.size()==0&&g.removes==removes);
    // At most Max at once.
    std::vector<Seen> many;for(uint64_t i=0;i<Max+5;++i)many.push_back(rider(100+i,2,double(i)));
    f.tick(7000,true,many);CHECK(f.size()==Max);
    // The game's code differs: said once, nothing made.
    Game b;b.broken=true;std::vector<std::string> l2;Fleet none([&]{return b.make();},[&](const std::string& l){l2.push_back(l);});
    none.tick(0,true,{rider(1,2)});none.tick(10,true,{rider(2,2)});CHECK(none.size()==0&&none.unavailable()&&l2.size()==1);
}
static Candidate truck(uint64_t id,double x,bool driven=true){Candidate c;c.id=id;c.entity=id*16;c.kind=2;c.driven=driven;c.at={x,0,0};return c;}
static void choosing(){
    std::vector<Candidate> cs{truck(1,3),truck(2,1.5),truck(3,1,false),truck(4,9)};
    auto c=choose(cs,{0,0,0},4.5,0);CHECK(c&&c->id==2);              // the nearest driven one in reach
    CHECK(!choose({truck(4,9)},{0,0,0},4.5,0));                         // none in reach
    CHECK(!choose({truck(3,1,false)},{0,0,0},4.5,0));                   // parked: nobody drives it
    c=choose(cs,{0,0,0},4.5,4);CHECK(c&&c->id==4);                     // riding already: the same one, wherever it is
    CHECK(!choose({truck(4,9,false)},{0,0,0},4.5,4));                   // he got out: off
}
int main(){
    fleet();choosing();
    if(failures){std::fprintf(stderr,"%u othervehicles oracle failures\n",failures);return 1;}
    std::puts("othervehicles oracles passed");return 0;
}
