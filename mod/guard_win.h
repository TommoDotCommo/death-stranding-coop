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
// What that call site really is (static, 2026-11-13): the dispatcher of an entity's queue of deferred calls on its
// components' reps (0x237e240 walks it; the queue's nodes live in a per-frame arena). A bad function pointer there
// means the queue itself dangles: its other fields are as bad, and walking on can fault again or never end (a node
// that points at itself), with the main thread waiting for the job. So when the faulting call was made from that
// walk (the walk's return address 0x30 above), the rest of the queue is abandoned: the walk resumes at its own end,
// which empties the queue (rbp = 0 is what it writes to head and tail) and returns. `walkReturn` is the stack word.
inline bool abandonsQueue(uint64_t walkReturn,uint64_t image){return image&&walkReturn==image+0x237e2b0;}
// The walk itself can fault on such a queue (live: a read of zero at 0x237e2c8, the call through a node's payload
// vtable, after five bad functions had been answered): any access violation inside the walk's loop
// (0x237e267..0x237e2d1), or an execute fault at an address outside the game called from its two payload calls
// (return 0x237e2c6 / 0x237e2ca), leaves the walk the same way. Returns how many bytes to take off the stack
// first (0 or 8), or -1 when the fault is not the walk's.
inline int walkFault(uint64_t code,uint64_t kind,uint64_t address,uint64_t rip,uint64_t returnAddress,uint64_t image,uint64_t imageEnd){
    if(!image||code!=0xc0000005u)return -1;
#ifndef MUTANT_GUARD_WALK_ANY
    if(kind!=8&&rip>=image+0x237e267&&rip<image+0x237e2d2)return 0;
#else
    if(kind!=8&&rip>=image+0x237e240&&rip<image+0x237e300)return 0;
#endif
    if(kind==8&&rip==address&&!(address>=image&&address<imageEnd)&&(returnAddress==image+0x237e2c6||returnAddress==image+0x237e2ca))return 8;
    return -1;
}
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
// The state behind that fault lasts (live: the same object met 300 times a second): the game's bounds update
// (0x1d5a950 -> 0x19402a0, no lock) worked from a stale leaf and left the drawable with no leaf and no index while its
// real slot still lists it; only a tree rebuild would mend it, and the game's later removal of the mesh would fault
// (0x194192b). The slot and leaf the job is walking are in the caller's frame, so the guard can put the two
// back-pointers right, as the game's own rebuild does (0x19415c0): index, then leaf. Only when everything agrees:
struct CullFacts {
    uint64_t object=0,stackObject=0;      // r14, and the caller's copy of it
    uint32_t slot=0,stackSlot=0;          // the slot being walked, and its low four bits as the caller keeps them
    uint64_t leaf=0,nodes=0;uint32_t nodeCount=0,leafCount=0;   // the leaf, the tree's node array, counts
    uint64_t itemPointer=0;               // what the slot lists
    uint64_t objectLeaf=0;uint32_t objectIndex=0,objectFlags=0; // the drawable's own back-pointers and flags
};
inline bool cullRepairable(const CullFacts& f){
#ifdef MUTANT_GUARD_REPAIR_ANY
    return f.object!=0;
#else
    return f.object&&f.stackObject==f.object&&f.stackSlot==(f.slot&15)
        &&f.nodes&&f.leaf>=f.nodes&&f.leaf<f.nodes+uint64_t(f.nodeCount)*24&&(f.leaf-f.nodes)%24==0
        &&f.slot<f.leafCount&&f.itemPointer==f.object+8
        &&f.objectLeaf==0&&f.objectIndex==0xffffffffu&&(f.objectFlags&(1u<<26))&&(f.objectFlags&(1u<<9));
#endif
}
}
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <atomic>
#include <cstdint>
namespace mod::guard {
inline std::atomic<uint64_t> site{0},imageSize{0x8200000};
inline std::atomic<bool> walkOk{false};
inline std::atomic<unsigned> abandoned{0},walkLeft{0};
// Evidence: the entities whose queue was left (the walk keeps the entity rep in rsi; its entity is at +0x80).
inline std::atomic<uint64_t> queueEntity[4];
inline void noteQueue(uint64_t rep){
    if(!rep||IsBadReadPtr(reinterpret_cast<void*>(rep+0x80),8))return;
    uint64_t entity=*reinterpret_cast<uint64_t*>(rep+0x80);
    for(auto& q:queueEntity){uint64_t have=q.load();if(have==entity)return;if(!have){uint64_t none=0;if(q.compare_exchange_strong(none,entity)||none==entity)return;}}
}
inline std::atomic<unsigned> skipped{0};
// Evidence for the first hits: the callback's target object (rcx at the fault) and the first word it holds (its
// vtable, when it is an object of the game), and the two return addresses above the call site.
struct Seen { uint64_t target=0,vtable=0,caller=0,caller2=0; };
inline Seen seen[8];
inline std::atomic<unsigned> cullRepaired{0};
// Reads everything through checked pointers; writes only when cullRepairable agrees.
inline void cullRepair(CONTEXT* c,uint64_t base){
    auto ok=[](uint64_t at,size_t n){return at&&!IsBadReadPtr(reinterpret_cast<void*>(at),n);};
    auto q=[](uint64_t at){return *reinterpret_cast<uint64_t*>(at);};auto d=[](uint64_t at){return *reinterpret_cast<uint32_t*>(at);};
    CullFacts f;f.object=c->R14;uint64_t sp=c->Rsp;
    if(!ok(sp,0x108)||!ok(f.object,0x30)||!ok(base+0x578c130,8))return;
    f.slot=d(sp+0x90);f.leaf=q(sp+0x100);f.stackObject=q(sp+0xc0);f.stackSlot=d(sp+0xc8);
    uint64_t manager=q(base+0x578c130);if(!ok(manager,0x30))return;
    f.nodes=q(manager+0x28);f.nodeCount=d(manager+0x20);
    if(!ok(f.leaf,0x18))return;
    f.leafCount=d(f.leaf+0xc);uint64_t block=(q(f.leaf)&~uint64_t(3))+uint64_t(f.slot>>4)*0x240;
    if(!ok(block,0x240))return;
    f.itemPointer=q(block+0x1c0+uint64_t(f.slot&15)*8);
    f.objectLeaf=q(f.object+8);f.objectIndex=d(f.object+0x10);f.objectFlags=d(f.object+0x28);
    if(!cullRepairable(f)||IsBadWritePtr(reinterpret_cast<void*>(f.object+8),0x10))return;
    *reinterpret_cast<volatile uint32_t*>(f.object+0x10)=f.slot;
    *reinterpret_cast<volatile uint64_t*>(f.object+8)=f.leaf;
    cullRepaired.fetch_add(1,std::memory_order_relaxed);
}
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
            cullSkipped.fetch_add(1,std::memory_order_relaxed);cullNote(c->R14);
            if(c->Rip==b+0x1d5cb8a)cullRepair(c,b);
            c->Rip=to;return EXCEPTION_CONTINUE_EXECUTION;}
    if(!s||r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||r->NumberParameters<2)return EXCEPTION_CONTINUE_SEARCH;
    if(IsBadReadPtr(reinterpret_cast<void*>(c->Rsp),8))return EXCEPTION_CONTINUE_SEARCH;
    uint64_t back=*reinterpret_cast<uint64_t*>(c->Rsp),image=s-0x22bdf1a;
    if(walkOk.load(std::memory_order_acquire))
        if(int pop=walkFault(r->ExceptionCode,r->ExceptionInformation[0],r->ExceptionInformation[1],c->Rip,back,image,image+imageSize.load(std::memory_order_acquire));pop>=0){
            walkLeft.fetch_add(1,std::memory_order_relaxed);noteQueue(c->Rsi);
            c->Rsp+=uint64_t(pop);c->Rbp=0;c->Rip=image+0x237e2d2;return EXCEPTION_CONTINUE_EXECUTION;}
    if(r->ExceptionInformation[0]!=8)return EXCEPTION_CONTINUE_SEARCH;
    if(!matches(r->ExceptionCode,r->ExceptionInformation[0],r->ExceptionInformation[1],c->Rip,back,s,image,image+imageSize.load(std::memory_order_acquire)))return EXCEPTION_CONTINUE_SEARCH;
    unsigned n=skipped.fetch_add(1,std::memory_order_relaxed);
    if(n<8){
        Seen& e=seen[n];e.target=c->Rcx;
        if(c->Rcx&&!IsBadReadPtr(reinterpret_cast<void*>(c->Rcx),8))e.vtable=*reinterpret_cast<uint64_t*>(c->Rcx);
        // 0x22bdee0 keeps 0x28 bytes of frame: its own return address is at rsp+8+0x28, the next one is searched for
        auto* sp=reinterpret_cast<uint64_t*>(c->Rsp);
        if(!IsBadReadPtr(sp,0x100)){e.caller=sp[6];for(unsigned i=7;i<32;++i)if(sp[i]>s-0x22bdf1a&&sp[i]<s-0x22bdf1a+0x8000000){e.caller2=sp[i];break;}}
    }
    // From the queue walk (0x237e240: push rsi, 0x20 of frame; 0x22bdee0: 0x28 of frame): leave the walk through its
    // end, which empties the queue. Otherwise (the other dispatcher, 0x23db432) the call is skipped as if it returned.
    auto* sp=reinterpret_cast<uint64_t*>(c->Rsp);
    if(walkOk.load(std::memory_order_acquire)&&!IsBadReadPtr(sp,0x40)&&abandonsQueue(sp[6],image)){
        abandoned.fetch_add(1,std::memory_order_relaxed);noteQueue(c->Rsi);c->Rsp+=0x38;c->Rbp=0;c->Rip=image+0x237e2d2;
        return EXCEPTION_CONTINUE_EXECUTION;
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
    // Leaving the queue walk through its end only where the walk is the code that was read.
    static const uint8_t call[10]={0x48,0x8d,0x4b,0x20,0xff,0xd0,0x48,0x8b,0x6b,0x08};
    static const uint8_t tail[35]={0x48,0x8b,0x7c,0x24,0x38,0x48,0x89,0xae,0x30,0x02,0x00,0x00,0x48,0x89,0xae,0x28,0x02,0x00,0x00,0x48,0x8b,0x6c,0x24,0x30,0x48,0x8b,0x5c,0x24,0x40,0x48,0x83,0xc4,0x20,0x5e,0xc3};
    if(!IsBadReadPtr(reinterpret_cast<void*>(base+0x237e2aa),0x60)&&!std::memcmp(reinterpret_cast<void*>(base+0x237e2aa),call,sizeof call)
       &&!std::memcmp(reinterpret_cast<void*>(base+0x237e2d2),tail,sizeof tail))walkOk.store(true,std::memory_order_release);
    AddVectoredExceptionHandler(1,handler);
}
}
#endif
