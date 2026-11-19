// Roads paved together in group play (roadshare.h), without a game: what the server says the others put in, less what
// the game took and what the mod gave before, never past what the paver misses, never into a built road; the ledger.
#include "roadshare.h"
#include <cstdio>
using namespace mod::roadshare;using mod::roads::Paver;using mod::roads::Set;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"roadshare oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static Paver paver(uint32_t id,uint32_t level){Paver p;p.id=id;p.level=level;p.address=0x1000+id;return p;}
int main(){
    Set lack{};lack[0]=800;lack[2]=500;lack[3]=300;
    auto missing=[&](uint64_t){return lack;};
    Ledger l;
    // Paver 2042: the others put in 600 crystals and 400 metal; the game took 100 crystals. Paver 252 is built.
    std::vector<Asked> asked{{2042,0,100,600},{2042,2,0,400},{252,0,0,999}};
    auto g=plan(asked,{paver(2042,1),paver(252,2)},missing,l);
    CHECK(g.size()==1&&g[0].id==2042&&g[0].set[0]==500&&g[0].set[2]==400&&g[0].set[3]==0);
    // Asked again, nothing new: nothing given twice.
    CHECK(plan(asked,{paver(2042,1)},missing,l).empty());
    // The others put in more: only the difference; never past what is missing.
    asked={{2042,0,100,2000},{2042,3,0,1000}};
    g=plan(asked,{paver(2042,1)},missing,l);
    CHECK(g.size()==1&&g[0].set[0]==800&&g[0].set[3]==300);
    // The ledger survives a restart as text.
    auto again=Ledger::load(l.dump());CHECK(again.given(2042,0)==1300&&again.given(2042,2)==400&&again.given(2042,3)==300&&again.given(9,0)==0);
    CHECK(Ledger::load("1 9 5\n2 0 -4\nx").empty());   // a material that is none, a negative amount, junk: nothing
    if(failures){std::fprintf(stderr,"%u roadshare oracle failures\n",failures);return 1;}
    std::puts("roadshare oracles passed");return 0;
}
