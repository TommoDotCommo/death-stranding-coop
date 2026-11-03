#include "body.h"
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod::body;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"body oracle: %s\n",#x);++failures;}}while(0)
// Fake memory: 8-byte words by address.
struct World {
    std::map<uint64_t,uint64_t> mem;std::vector<std::string> log;unsigned creates=0;std::vector<uint64_t> removed;
    uint64_t lastResource=0;mod::puppet::Transform lastAt{};
    static constexpr uint64_t Twin=0x10000,Resource=0x20000,Request=0x30000,Spawned=0x40000,Other=0x50000,Vt=0x3d7de48,OtherVt=0x1234;
    World(){mem[Twin]=Vt;mem[Twin+ResourceOffset]=Resource;mem[Spawned]=Vt;mem[Other]=OtherVt;}
    bool read(uint64_t a,void* b,size_t n){for(size_t i=0;i<n;i+=8){auto it=mem.find(a+i);uint64_t v=it==mem.end()?0:it->second;std::memcpy(static_cast<char*>(b)+i,&v,std::min<size_t>(8,n-i));}return true;}
    Api api(){return {[this](const mod::puppet::Transform& t,uint64_t r,const std::array<uint64_t,2>&){++creates;lastResource=r;lastAt=t;return Request;},
                      [this](uint64_t e){removed.push_back(e);},[this](uint64_t a,void* b,size_t n){return read(a,b,n);}};}
    bool said(const char* s) const {for(auto& l:log)if(l.find(s)!=std::string::npos)return true;return false;}
};
int main(){
    {   // Pure scan: only objects of the entity class, never the double itself, no duplicates, no junk pointers.
        auto vt=[](uint64_t p){return p==0x1000||p==0x3000?uint64_t(7):p==0x2000?uint64_t(9):uint64_t(0);};
        auto f=entitiesIn({0,0x1000,0x2000,0x1000,0x3000,0x1003,0xffff800000000000ULL},7,0x3000,vt);
        CHECK(f.size()==1&&f[0]==0x1000);
    }
    {   // The request uses the double's own resource and the given place; the entity appears a few ticks later.
        World w;Probe p(w.api(),World::Vt,[&](const std::string& s){w.log.push_back(s);});
        mod::puppet::Transform at;at.position={1,2,3};
        CHECK(p.create(World::Twin,at,{1,2})&&w.creates==1&&w.lastResource==World::Resource&&w.lastAt.position[2]==3);
        CHECK(!p.create(World::Twin,at,{3,4})&&w.creates==1);                       // one at a time
        w.mem[World::Request+0x190]=World::Twin;w.mem[World::Request+0x198]=World::Other;   // the double and another class: not it
        p.tick();p.tick();CHECK(!p.entity());
        w.mem[World::Request+0x1a0]=World::Spawned;p.tick();
        CHECK(p.entity()==World::Spawned&&w.said("BODY spawned"));
        CHECK(p.remove()&&w.removed.size()==1&&w.removed[0]==World::Spawned&&!p.entity());
        CHECK(!p.remove()&&w.removed.size()==1);
        for(unsigned i=0;i<Probe::WatchTicks;++i)p.tick();
        CHECK(!p.watching()&&p.create(World::Twin,at,{5,6})&&w.creates==2);      // a new request once the last is done
    }
    {   // No resource: refused, nothing called.
        World w;w.mem.erase(World::Twin+ResourceOffset);Probe p(w.api(),World::Vt,[&](const std::string& s){w.log.push_back(s);});
        CHECK(!p.create(World::Twin,{},{1,2})&&w.creates==0&&w.said("refused"));
        CHECK(!p.create(0,{},{1,2})&&w.creates==0);
    }
    {   // Nothing ever appears: said once at the end.
        World w;Probe p(w.api(),World::Vt,[&](const std::string& s){w.log.push_back(s);});p.create(World::Twin,{},{1,2});
        for(unsigned i=0;i<Probe::WatchTicks;++i){p.tick();}
        CHECK(!p.entity()&&w.said("no entity"));
    }
    if(!failures)std::puts("body request, scan and remove checks passed");
    return failures?1:0;
}
