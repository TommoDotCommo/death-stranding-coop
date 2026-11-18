#include "freebuild.h"
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod::freebuild;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"free build oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
constexpr uint64_t Base=0x140000000;
struct Game {
    std::map<uint64_t,uint8_t> m;unsigned writes=0;int failAt=-1;
    Game(){for(auto& p:Patches){for(unsigned i=0;i<4;++i)m.emplace(Base+p.rva-4+i,p.before[i]);m[Base+p.rva]=p.off;for(unsigned i=0;i<5;++i)m.emplace(Base+p.rva+1+i,p.after[i]);}
        for(auto& p:Patches)m[Base+p.rva]=p.off;}
    Read reader(){return [this](uint64_t a,uint8_t* o,size_t n){for(size_t i=0;i<n;++i){auto it=m.find(a+i);if(it==m.end())return false;o[i]=it->second;}return true;};}
    Write writer(){return [this](uint64_t a,uint8_t v){if(failAt>=0&&int(writes)==failAt){++writes;return false;}++writes;m[a]=v;return true;};}
    bool all(bool on){for(auto& p:Patches)if(m[Base+p.rva]!=(on?p.on:p.off))return false;return true;}
};
int main(){
    {   Game g;CHECK(apply(Base,true,g.reader(),g.writer())==State::On&&g.all(true)&&g.writes==Patches.size());
        CHECK(apply(Base,true,g.reader(),g.writer())==State::On&&g.writes==Patches.size());   // nothing left to do
        CHECK(apply(Base,false,g.reader(),g.writer())==State::Off&&g.all(false));}
    {   // Another build: a byte differs anywhere near any place, and nothing is written.
        for(auto& p:Patches)for(int at:{-4,-1,0,1,5}){Game g;const uint64_t a=Base+p.rva+at;bool neighbour=false;for(auto& q:Patches)neighbour=neighbour||(at&&Base+q.rva==a);if(neighbour)continue;
            g.m[a]=uint8_t(g.m[a]^0x5a);if(at==0&&(g.m[a]==p.on||g.m[a]==p.off))g.m[a]=0xcc;CHECK(apply(Base,true,g.reader(),g.writer())==State::Differs&&!g.writes);}
        Game g;g.m.erase(Base+Patches.back().rva+5);CHECK(apply(Base,true,g.reader(),g.writer())==State::Differs&&!g.writes);}
    {   // A write refused half way: everything back as it was.
        Game g;g.failAt=5;CHECK(apply(Base,true,g.reader(),g.writer())==State::Failed&&g.all(false));}
    if(!failures)std::puts("free build checks passed");
    return failures?1:0;
}
