#pragma once
// A narrow guard for one known fault of the game while the mod is in it (game_offsets.md, "Creation callback with
// no function"): the game's entity creation runs its completion callbacks through 0x22bdee0, which calls the
// callback's function pointer unchecked. With a partner connected a callback with a target and no function was
// seen there during the game's own events (a worker thread jumped to address zero and the game hung). The guard
// answers exactly that: an execute fault at address zero whose return address is that call site returns to the
// caller, as the missing function would have done. Everything else is left to the game and the crash recorder.
#include <cstdint>
namespace mod::guard {
// Live 2026-11-13: the callback belonged to an object that had been freed (an ArtPartsShaderVariableComponentRep of
// the partner body, after the player moved far at once): its function pointer read as zero at one call and as heap
// garbage at the next, 1.7 s later, and that second fault is what stopped the game. So the guard answers an execute
// fault at the very address called from that site whenever the address is not code of the game (image..imageEnd).
inline bool matches(uint64_t code,uint64_t kind,uint64_t address,uint64_t rip,uint64_t returnAddress,uint64_t callSite,uint64_t image=0,uint64_t imageEnd=0){
#ifdef MUTANT_GUARD_ANY_FAULT
    (void)code;(void)kind;(void)address;(void)rip;(void)image;(void)imageEnd;return returnAddress==callSite||callSite!=0;
#else
    if(!callSite||code!=0xc0000005u||kind!=8||rip!=address||returnAddress!=callSite)return false;
#ifndef MUTANT_GUARD_IN_IMAGE
    if(address>=image&&address<imageEnd)return false;                   // a fault inside the game's own code is not this
#endif
    return true;
#endif
}
// A second known fault (game_offsets.md, "Culling job and a drawable that left the tree"): the render culling job
// (0x1d5ca70, on worker threads) reads a drawable's leaf pointer without the null check the game makes everywhere
// else, and takes no lock against the tree changing. A drawable removed or moved between its two reads (the mod
// removes and shows entities, the game does too) leaves that pointer null and the read faults; the game then sits in
// its crash handler. The answer is the game's own for a drawable that is not in the tree: skip it. The function has
// no loop and has saved every register it changed by then, so execution resumes at its epilogue.
// Returns where to resume, 0 when the fault is not this one.
inline uint64_t cullResume(uint64_t code,uint64_t kind,uint64_t rip,uint64_t rax,uint64_t rcx,uint64_t base){
    if(!base||code!=0xc0000005u||kind!=0)return 0;
#ifdef MUTANT_GUARD_CULL_ANY
    (void)rax;(void)rcx;if(rip>=base+0x1d5cb8a&&rip<=base+0x1d5cb9a)return base+0x1d5cc09;
#else
    if(rip==base+0x1d5cb8a&&rax==0)return base+0x1d5cc09;                              // mov rcx,[rax]: no leaf
    if((rip==base+0x1d5cb94||rip==base+0x1d5cb9a)&&rcx==0)return base+0x1d5cc09;       // the leaf's item block is gone
#endif
    return 0;
}
}
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <atomic>
#include <cstdint>
namespace mod::guard {
inline std::atomic<uint64_t> site{0},imageSize{0x8200000};
inline std::atomic<unsigned> skipped{0};
// Evidence for the first hits: the callback's target object (rcx at the fault) and the first word it holds (its
// vtable, when it is an object of the game), and the two return addresses above the call site.
struct Seen { uint64_t target=0,vtable=0,caller=0,caller2=0; };
inline Seen seen[8];
inline std::atomic<uint64_t> cullBase{0};
inline std::atomic<unsigned> cullSkipped{0};
// Evidence: the first few distinct drawables met (the cull sub-object r14, its vtable, index and flags), how often.
struct CullSeen { std::atomic<uint64_t> object{0}; uint64_t vtable=0; uint32_t index=0,flags=0; std::atomic<unsigned> hits{0}; };
inline CullSeen cullSeen[8];
inline void cullNote(uint64_t object){
    for(auto& s:cullSeen){
        uint64_t have=s.object.load(std::memory_order_acquire);
        if(have==object){s.hits.fetch_add(1,std::memory_order_relaxed);return;}
        if(have)continue;
        uint64_t none=0;
        if(!s.object.compare_exchange_strong(none,object)){if(none==object){s.hits.fetch_add(1,std::memory_order_relaxed);return;}continue;}
        if(!IsBadReadPtr(reinterpret_cast<void*>(object),0x30)){s.vtable=*reinterpret_cast<uint64_t*>(object);s.index=*reinterpret_cast<uint32_t*>(object+0x10);s.flags=*reinterpret_cast<uint32_t*>(object+0x28);}
        s.hits.fetch_add(1,std::memory_order_relaxed);return;
    }
}
inline LONG CALLBACK handler(EXCEPTION_POINTERS* e){
    auto* r=e->ExceptionRecord;auto* c=e->ContextRecord;uint64_t s=site.load(std::memory_order_acquire);
    if(uint64_t b=cullBase.load(std::memory_order_acquire);b&&r->NumberParameters>=2)
        if(uint64_t to=cullResume(r->ExceptionCode,r->ExceptionInformation[0],c->Rip,c->Rax,c->Rcx,b)){
            cullSkipped.fetch_add(1,std::memory_order_relaxed);cullNote(c->R14);c->Rip=to;return EXCEPTION_CONTINUE_EXECUTION;}
    if(!s||r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||r->NumberParameters<2||r->ExceptionInformation[0]!=8)return EXCEPTION_CONTINUE_SEARCH;
    if(IsBadReadPtr(reinterpret_cast<void*>(c->Rsp),8))return EXCEPTION_CONTINUE_SEARCH;
    uint64_t back=*reinterpret_cast<uint64_t*>(c->Rsp),image=s-0x22bdf1a;
    if(!matches(r->ExceptionCode,r->ExceptionInformation[0],r->ExceptionInformation[1],c->Rip,back,s,image,image+imageSize.load(std::memory_order_acquire)))return EXCEPTION_CONTINUE_SEARCH;
    unsigned n=skipped.fetch_add(1,std::memory_order_relaxed);
    if(n<8){
        Seen& e=seen[n];e.target=c->Rcx;
        if(c->Rcx&&!IsBadReadPtr(reinterpret_cast<void*>(c->Rcx),8))e.vtable=*reinterpret_cast<uint64_t*>(c->Rcx);
        // 0x22bdee0 keeps 0x28 bytes of frame: its own return address is at rsp+8+0x28, the next one is searched for
        auto* sp=reinterpret_cast<uint64_t*>(c->Rsp);
        if(!IsBadReadPtr(sp,0x100)){e.caller=sp[6];for(unsigned i=7;i<32;++i)if(sp[i]>s-0x22bdf1a&&sp[i]<s-0x22bdf1a+0x8000000){e.caller2=sp[i];break;}}
    }
    c->Rip=back;c->Rsp+=8;
    return EXCEPTION_CONTINUE_EXECUTION;
}
// base: the game image. Installed once, ahead of the other handlers.
inline void install(uint64_t base){
    if(site.exchange(base+0x22bdf1a,std::memory_order_acq_rel))return;
    // The culling guard only on the build whose code it was read from: the two reads and the epilogue it resumes at.
    static const uint8_t reads[14]={0x49,0x8b,0x46,0x08,0x48,0x8b,0x08,0x49,0x8b,0x06,0x48,0x83,0xe1,0xfc};
    static const uint8_t leave[9]={0xc5,0xf8,0x28,0xb4,0x24,0x80,0x00,0x00,0x00};
    if(!IsBadReadPtr(reinterpret_cast<void*>(base+0x1d5cb86),0x90)&&!std::memcmp(reinterpret_cast<void*>(base+0x1d5cb86),reads,sizeof reads)
       &&!std::memcmp(reinterpret_cast<void*>(base+0x1d5cc09),leave,sizeof leave))cullBase.store(base,std::memory_order_release);
    AddVectoredExceptionHandler(1,handler);
}
}
#endif
