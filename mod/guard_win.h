#pragma once
// A narrow guard for one known fault of the game while the mod is in it (game_offsets.md, "Creation callback with
// no function"): the game's entity creation runs its completion callbacks through 0x22bdee0, which calls the
// callback's function pointer unchecked. With a partner connected a callback with a target and no function was
// seen there during the game's own events (a worker thread jumped to address zero and the game hung). The guard
// answers exactly that: an execute fault at address zero whose return address is that call site returns to the
// caller, as the missing function would have done. Everything else is left to the game and the crash recorder.
#include <cstdint>
namespace mod::guard {
inline bool matches(uint64_t code,uint64_t kind,uint64_t address,uint64_t rip,uint64_t returnAddress,uint64_t callSite){
#ifdef MUTANT_GUARD_ANY_FAULT
    (void)code;(void)kind;(void)address;(void)rip;return returnAddress==callSite||callSite!=0;
#else
    return callSite&&code==0xc0000005u&&kind==8&&address==0&&rip==0&&returnAddress==callSite;
#endif
}
}
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <cstdint>
namespace mod::guard {
inline std::atomic<uint64_t> site{0};
inline std::atomic<unsigned> skipped{0};
inline LONG CALLBACK handler(EXCEPTION_POINTERS* e){
    auto* r=e->ExceptionRecord;auto* c=e->ContextRecord;uint64_t s=site.load(std::memory_order_acquire);
    if(!s||r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||r->NumberParameters<2||c->Rip!=0)return EXCEPTION_CONTINUE_SEARCH;
    if(IsBadReadPtr(reinterpret_cast<void*>(c->Rsp),8))return EXCEPTION_CONTINUE_SEARCH;
    uint64_t back=*reinterpret_cast<uint64_t*>(c->Rsp);
    if(!matches(r->ExceptionCode,r->ExceptionInformation[0],r->ExceptionInformation[1],c->Rip,back,s))return EXCEPTION_CONTINUE_SEARCH;
    c->Rip=back;c->Rsp+=8;skipped.fetch_add(1,std::memory_order_relaxed);
    return EXCEPTION_CONTINUE_EXECUTION;
}
// base: the game image. Installed once, ahead of the other handlers.
inline void install(uint64_t base){
    if(site.exchange(base+0x22bdf1a,std::memory_order_acq_rel))return;
    AddVectoredExceptionHandler(1,handler);
}
}
#endif
