#pragma once
#include "mod.h"
#include <cstddef>
namespace mod::discovery {class Sampler;}
#include <array>
#include <cstdint>
namespace mod::dispatch {
constexpr uint64_t PeekReturn=0x1a19387, FrameReturn=0x263d2ae;
constexpr std::array<uint64_t,5> FrameStack={PeekReturn,0x1a17aec,FrameReturn,0x263a869,0x263753d};
struct Sites {uint64_t peekCall,getCall,framePattern,outerCall,loopCall,peekIat,getIat,outerTarget,loopTarget,applicationGlobal;};
constexpr Sites PinnedSites{0x1a19381,0x1a19398,0x263d29e,0x263a864,0x2637538,0x3b3a7b8,0x3b3a7c0,0x263cfe0,0x263a5d0,0x577d3f0};
#ifdef MOD_FAKE_NETWORK
constexpr Sites FixtureSites{0x1800,0x1820,0x1840,0x1860,0x1880,0x3700,0x3708,0x18a0,0x18c0,0x36f0};
#endif
// Hash-gated sampler first; validate the unique main-loop call signature and
// reviewed call instructions in executable sections before accepting callbacks.
void validateSites(const discovery::Sampler&,const Sites&);
class Gate {
    uint64_t caller_; uint32_t thread_=0;
public:
    explicit Gate(uint64_t caller=FrameReturn):caller_(caller) {}
    uint32_t thread() const {return thread_;}
    bool accept(const mod_game_thread_context& c,uint32_t actual) {
#ifndef MUTANT_DISPATCH_ABI
        if(c.size!=sizeof(c)||c.version!=1||c.reserved)return false;
#endif
#ifndef MUTANT_DISPATCH_THREAD
        if(!actual||actual!=c.thread_id||(thread_&&thread_!=actual))return false;
#endif
#ifndef MUTANT_DISPATCH_CALLER
        if(c.caller_rva!=caller_)return false;
#endif
#ifndef MUTANT_DISPATCH_DEPTH
        if(c.depth!=1)return false;
#endif
#ifndef MUTANT_DISPATCH_NETWORK
        if(c.network_enforced!=1)return false;
#endif
        thread_=actual;return true;
    }
};
inline bool frameStack(const uint64_t* frames,size_t count,uint64_t base,uint64_t size) {
    size_t index=0;
    for(size_t i=0;i<count;++i) {
        if(frames[i]<base || frames[i]-base>=size)continue;
#ifndef MUTANT_DISPATCH_STACK
        if(index>=FrameStack.size() || frames[i]-base!=FrameStack[index])return false;
#endif
        if(++index==FrameStack.size())return true;
    }
    return false;
}
}
