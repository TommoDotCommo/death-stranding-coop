#include "game_thread.h"
#include "fake_memory.h"
#include <cstdio>
using namespace mod::dispatch;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"dispatch oracle: %s\n",#x);++failures;}}while(0)
template<class F> bool refused(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    mod_game_thread_context c{sizeof(c),1,7,1,FrameReturn,1,0};Gate gate;
    CHECK(gate.accept(c,7));CHECK(gate.thread()==7);
    auto bad=c;bad.version=2;CHECK(!gate.accept(bad,7));bad=c;bad.size=1;CHECK(!gate.accept(bad,7));bad=c;bad.reserved=1;CHECK(!gate.accept(bad,7));
    bad=c;bad.thread_id=8;CHECK(!gate.accept(bad,7));CHECK(!gate.accept(bad,8));CHECK(!gate.accept(c,0));
    bad=c;bad.caller_rva++;CHECK(!gate.accept(bad,7));bad=c;bad.depth=2;CHECK(!gate.accept(bad,7));bad=c;bad.network_enforced=0;CHECK(!gate.accept(bad,7));
    uint64_t frames[8]={0x999,0x100000+FrameStack[0],0x100000+FrameStack[1],0x100000+FrameStack[2],0x100000+FrameStack[3],0x100000+FrameStack[4],0,0};
    CHECK(frameStack(frames,8,0x100000,0x8000000));CHECK(!frameStack(frames,4,0x100000,0x8000000));frames[3]++;CHECK(!frameStack(frames,8,0x100000,0x8000000));
    fakemem::Fake f;auto s=f.sampler();s.scan(mod::discovery::PinnedHash,mod::discovery::PinnedHash);
    Sites sites{0x1800,0x1820,0x1840,0x1860,0x1880,0x3700,0x3708,0x18a0,0x18c0,0x36f0};
    f.dispatchSites(sites);validateSites(s,sites);
    f.data[sites.peekCall]=0x90;CHECK(refused([&]{validateSites(s,sites);}));f.dispatchSites(sites);
    f.put<int32_t>(sites.peekCall+2,int32_t(sites.getIat-(sites.peekCall+6)));CHECK(refused([&]{validateSites(s,sites);}));f.dispatchSites(sites);
    std::copy(f.data.begin()+sites.framePattern,f.data.begin()+sites.framePattern+24,f.data.begin()+0x1a00);
    CHECK(refused([&]{validateSites(s,sites);}));
    if(!failures)std::puts("dispatch guards and mapped sites passed");
    return failures?1:0;
}
