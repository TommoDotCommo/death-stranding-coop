// Native Windows, game-free: many threads write import-style slots on one read-only page through write_import.
// Unlocked (MUTANT_IAT_UNLOCKED), one thread's protection restore lands inside another's write and it faults.
#include "iat_patch.h"
#include <atomic>
#include <cstdio>
#include <vector>

static LONG CALLBACK on_fault(EXCEPTION_POINTERS* e) {
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        std::fputs("iat race: access violation on an import write\n",stderr);std::fflush(stderr);ExitProcess(3);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {
    AddVectoredExceptionHandler(1,on_fault);
    auto page=static_cast<ULONG_PTR*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    DWORD old=0;
    if(!page||!VirtualProtect(page,4096,PAGE_READONLY,&old)) {std::fputs("iat race: setup failed\n",stderr);return 2;}
    const unsigned threads=8,rounds=20000;
    static std::atomic<unsigned> failures{0};
    static ULONG_PTR* shared=page;
    struct Arg {unsigned t,rounds;};
    std::vector<Arg> args(threads);std::vector<HANDLE> pool;
    for(unsigned t=0;t<threads;++t) {
        args[t]={t,rounds};
        pool.push_back(CreateThread(nullptr,0,[](LPVOID p)->DWORD{
            auto a=static_cast<Arg*>(p);
            for(unsigned i=0;i<a->rounds;++i)
                if(redirect::write_import(shared+a->t*8,(ULONG_PTR(a->t)<<32)|i)!=redirect::IatWrite::ok) ++failures;
            return 0;
        },&args[t],0,nullptr));
    }
    WaitForMultipleObjects(threads,pool.data(),TRUE,INFINITE);
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(page,&info,sizeof info);
    if(failures||info.Protect!=PAGE_READONLY) {std::fprintf(stderr,"iat race: %u failed writes, protection 0x%lx\n",failures.load(),info.Protect);return 1;}
    for(unsigned t=0;t<threads;++t) if(page[t*8]!=((ULONG_PTR(t)<<32)|(rounds-1))) {std::fputs("iat race: lost write\n",stderr);return 1;}
    std::puts("iat race ok");
    return 0;
}
