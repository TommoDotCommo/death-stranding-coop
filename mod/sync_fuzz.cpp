// Seeded property tests for the shared-world logic (cargo.h, structures.h): random worlds over many steps.
// Cargo: placement ids are unique and nonzero; a partner's item is never announced back under its own id; each id is
// reported taken at most once and only if it was ours or the partner's; partnerTook gives an index at most once.
// Structures: only built structures with a player id and a Net variant are announced; per id, announcements and
// removals alternate (built, then removed, then built again...).
#include "cargo.h"
#include "structures.h"
#include <cstdio>
#include <map>
#include <random>
#include <set>
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"fuzz oracle: %s (seed %u step %u)\n",#x,seed,step);++failures;return;}}while(0)
using namespace mod;
void cargoRun(unsigned seed){
    std::mt19937 rng(seed);unsigned step=0;
    cargo::Sync s;s.setIdBase(uint64_t(seed)<<20);
    struct Item{cargo::Record r;bool partners=false;uint64_t partnerId=0;};
    std::map<uint32_t,Item> world;uint32_t nextSlot=1;std::array<double,3> porter{0,0,100};uint64_t now=0;
    std::set<uint64_t> announced,taken,expectedIds,removedOnce;std::set<uint32_t> partnerCarried,seenSlots;std::set<uint64_t> mustTake,gone;
    auto records=[&]{std::vector<cargo::Record> v;for(auto& [k,i]:world){(void)k;v.push_back(i.r);}return v;};
    for(step=0;step<400;++step){
        now+=250;int a=int(rng()%10);
        if(a==0&&world.size()<40){   // our cargo appears near the porter, in the world
            cargo::Record r;r.slot=nextSlot++;r.hash=7;r.index=r.slot;r.state=cargo::InWorld<<8|cargo::SharedKind;r.position={porter[0]+double(rng()%5),porter[1],100};
            world[r.slot]={r,false,0};
        } else if(a==1&&world.size()<40){   // the partner's item is created here
            uint64_t id=(uint64_t(seed+1)<<21)+step;cargo::Record r;r.slot=nextSlot++;r.hash=9;r.index=r.slot;r.state=cargo::InWorld<<8|cargo::SharedKind;
            r.position={porter[0]+double(rng()%20)-10,porter[1],100};s.expect({9,r.position,id},now);world[r.slot]={r,true,id};expectedIds.insert(id);
        } else if(a==2&&!world.empty()){   // pick something up / put it down
            auto it=std::next(world.begin(),long(rng()%world.size()));auto& st=it->second.r.state;
            bool wasWorld=(st>>8)==cargo::InWorld;st=((st>>8)==cargo::InWorld?cargo::OnPorter:cargo::InWorld)<<8|cargo::SharedKind;if((st>>8)==cargo::OnPorter){it->second.r.position=porter;if(it->second.partners)partnerCarried.insert(it->first);}
            // A partner's item already seen here, picked up from the world for the first time: must be reported taken.
            if(wasWorld&&it->second.partners&&seenSlots.count(it->first)&&!partnerCarried.count(it->first+1000000)&&!gone.count(it->second.partnerId)){mustTake.insert(it->second.partnerId);partnerCarried.insert(it->first+1000000);}
        } else if(a==3&&!world.empty()){   // something vanishes
            world.erase(std::next(world.begin(),long(rng()%world.size())));
        } else if(a==4){porter[0]+=double(rng()%7)-3;}
        else if(a==5&&!announced.empty()){   // the partner takes one of our placements
            auto id=*std::next(announced.begin(),long(rng()%announced.size()));auto idx=s.partnerTook(id);
            if(idx){CHECK(!removedOnce.count(id));removedOnce.insert(id);for(auto it=world.begin();it!=world.end();++it)if(it->second.r.index==*idx){world.erase(it);break;}}
        }
        for(auto& p:s.observe(records(),porter,now)){
            CHECK(p.id!=0);CHECK(!announced.count(p.id));CHECK(!expectedIds.count(p.id));announced.insert(p.id);
            CHECK(p.hash!=9||!partnerCarried.empty());   // a partner's item goes back only after this porter carried it
        }
        for(auto tk:s.takeTaken()){auto id=tk.id;if(!id)continue;CHECK(!taken.count(id));CHECK(announced.count(id)||expectedIds.count(id));CHECK(!removedOnce.count(id));taken.insert(id);gone.insert(id);}
        for(auto id:mustTake){CHECK(taken.count(id));}
        mustTake.clear();
        for(auto& [k,i]:world){(void)i;seenSlots.insert(k);}
    }
}
void structuresRun(unsigned seed){
    std::mt19937 rng(seed^0x5eed);unsigned step=0;structures::Sync s;
    std::map<uint32_t,structures::Record> world;std::map<uint32_t,bool> standing,announcedOpen;   // built and known; announced and not yet removed
    for(step=0;step<400;++step){
        int a=int(rng()%6);
        if(a<=1){structures::Record r;uint32_t band=rng()%3;r.id=band==0?uint32_t(rng()%5000):band==1?10000+uint32_t(rng()%200):100000+uint32_t(rng()%200);
            r.type=uint8_t(rng()%46);r.state=uint8_t(rng()%3);r.at.rotation=structures::rotation(0,0,0);world[r.id]=r;}
        else if(a==2&&!world.empty()){auto it=std::next(world.begin(),long(rng()%world.size()));it->second.state=uint8_t(rng()%3);}
        else if(a==3&&!world.empty()){world.erase(std::next(world.begin(),long(rng()%world.size())));}
        std::vector<structures::Record> v;for(auto& [k,r]:world){(void)k;v.push_back(r);}
        for(auto& b:s.observe(v)){
            CHECK(b.id>=structures::PlayerFirst&&b.id<structures::NetFirst);CHECK(world.count(uint32_t(b.id))&&world[uint32_t(b.id)].state==2);
            CHECK(structures::netVariant(b.type).has_value());CHECK(!announcedOpen[uint32_t(b.id)]);announcedOpen[uint32_t(b.id)]=true;
        }
        for(auto& rm:s.takeRemoved()){CHECK(standing[rm.id]);standing[rm.id]=false;announcedOpen[rm.id]=false;CHECK(structures::netVariant(rm.type).has_value());}
        for(auto& r:v)if(r.id>=structures::PlayerFirst&&r.id<structures::NetFirst&&r.state==2&&structures::netVariant(r.type))standing[r.id]=true;
    }
}
int main(){
    for(unsigned seed=1;seed<=300&&!failures;++seed){cargoRun(seed);structuresRun(seed);}
    if(!failures)std::puts("cargo and structure sync fuzz: 300 seeds x 400 steps passed");
    return failures?1:0;
}
