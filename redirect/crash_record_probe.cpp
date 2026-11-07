// Native Windows fixture for the crash recorder, game-free (run directly, also under WSL interop).
//
//   crash_record_probe.exe <folder> <mode>
//     off          recorder not installed; a handled access violation still continues
//     handled      a handled access violation twice at one address (one record), then 40 faults at distinct
//                  addresses (the cap); every fault is recovered by a "game" handler and the process exits 0
//     handled-dump as handled once, with level 2: the minidump is written at the first record and the process goes on
//     unhandled    an access violation nobody handles: record, module map, dump, then the "game" filter runs
//     hang         the main thread's "message pump" stops for 3.5 s (hang note after 1 s) and then resumes
//     research     writes crash.fault twice (a wrong stamp, then "divide <pid>") and waits: in a research build
//                  (CRASH_RECORD_RESEARCH) a recovered divide fault is recorded; a default build must ignore it
//   crash_record_probe.exe --interop-check   exits 0 (lets a WSL Makefile tell "cannot run .exe" from a failure)
//
// Like the game, the fixture registers its own first-position vectored handler and an unhandled filter AFTER the
// recorder is installed, through the recorder's import hooks (as the proxy patches the game's imports).
// stdout: one JSON line with the function RVAs, then one JSON line with the result (not for "unhandled").
#include "crash_record.h"
#include <cstdio>
#include <cstring>
#include <cwchar>

extern "C" int fault_read(const volatile int* p);   // movl (%rcx),%eax: a 2-byte faulting instruction
extern "C" void fault_write(volatile int* p);       // movl $1,(%rcx)
asm(".text\n"
    ".globl fault_read\nfault_read:\n movl (%rcx), %eax\n ret\n"
    ".globl fault_write\nfault_write:\n movl $1, (%rcx)\n ret\n");

namespace {
wchar_t folder[MAX_PATH];
unsigned char* bad_code=nullptr;   // reserved, no access: calls into it fault at distinct addresses
const size_t bad_size=64*1024;
volatile LONG recovered=0;

LONG CALLBACK game_handler(EXCEPTION_POINTERS* e) {
    auto r=e->ExceptionRecord;auto c=e->ContextRecord;
    if(r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    if(c->Rip==reinterpret_cast<DWORD64>(&fault_read)) {c->Rip+=2;c->Rax=42;InterlockedIncrement(&recovered);return EXCEPTION_CONTINUE_EXECUTION;}
    auto pc=reinterpret_cast<unsigned char*>(c->Rip);
    if(bad_code && pc>=bad_code && pc<bad_code+bad_size) {
        c->Rip=*reinterpret_cast<DWORD64*>(c->Rsp);c->Rsp+=8;c->Rax=7;   // return to the caller of the bad pointer
        InterlockedIncrement(&recovered);return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG WINAPI game_filter(EXCEPTION_POINTERS*) {
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\game-filter.txt",folder);
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE) {DWORD n;WriteFile(f,"game filter called\n",19,&n,nullptr);CloseHandle(f);}
    return EXCEPTION_EXECUTE_HANDLER;   // ends the process quietly, as a crash reporter does after its dialog
}
HMODULE load_dbghelp(wchar_t* reason,size_t capacity) {
    wchar_t path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n || n>MAX_PATH-20) {lstrcpynW(reason,L"no system directory",int(capacity));return nullptr;}
    wcscat(path,L"\\dbghelp.dll");
    HMODULE m=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!m) lstrcpynW(reason,L"dbghelp load failed",int(capacity));
    return m;
}
__attribute__((noinline)) int do_handled(const volatile int* p) {
    int v=fault_read(p);
    asm volatile("" ::: "memory");   // keeps the call a real call (a return address inside this function)
    return v;
}
__attribute__((noinline)) void hang_here() {
    Sleep(3500);
    asm volatile("" ::: "memory");
}
unsigned long long rva(const void* p) {return reinterpret_cast<unsigned long long>(p)-reinterpret_cast<unsigned long long>(GetModuleHandleW(nullptr));}
bool wait_dump_ready() {
    for(int i=0;i<200 && !redirect::crash::dump_ready();++i) Sleep(25);
    return redirect::crash::dump_ready();
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2 && !wcscmp(argv[1],L"--interop-check")) return 0;
    if(argc!=3) {std::fputs("usage: crash_record_probe.exe <folder> <off|handled|handled-dump|unhandled|hang>\n",stderr);return 2;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    lstrcpynW(folder,argv[1],MAX_PATH);
    const wchar_t* mode=argv[2];
    bool off=!wcscmp(mode,L"off");
    std::printf("{\"pid\":%lu,\"main_tid\":%lu,\"fault_read\":%llu,\"fault_write\":%llu,\"do_handled\":%llu,\"hang_here\":%llu}\n",
                GetCurrentProcessId(),GetCurrentThreadId(),rva(reinterpret_cast<const void*>(&fault_read)),
                rva(reinterpret_cast<const void*>(&fault_write)),rva(reinterpret_cast<const void*>(&do_handled)),
                rva(reinterpret_cast<const void*>(&hang_here)));
    std::fflush(stdout);
    if(!off) {
        redirect::crash::Options o;
        o.level=!wcscmp(mode,L"handled-dump")?2:1;
        o.hang_seconds=!wcscmp(mode,L"hang")?1:0;
        o.folder=folder;
        wchar_t stem[64];swprintf(stem,64,L"crash-%lu",GetCurrentProcessId());o.stem=stem;
        o.load_dbghelp=load_dbghelp;
        if(!redirect::crash::install(o)) {std::fputs("install failed\n",stderr);return 3;}
        // The "game" registers after the proxy, through the patched imports.
        redirect::crash::hook_AddVectoredExceptionHandler(1,game_handler);
        redirect::crash::hook_SetUnhandledExceptionFilter(game_filter);
    } else {
        AddVectoredExceptionHandler(1,game_handler);SetUnhandledExceptionFilter(game_filter);
    }
    DWORD tid=GetCurrentThreadId();
    if(off || !wcscmp(mode,L"handled") || !wcscmp(mode,L"handled-dump")) {
        bool dump=!wcscmp(mode,L"handled-dump");
        bool ready=dump?wait_dump_ready():false;
        int a=do_handled(nullptr),b=dump?42:do_handled(nullptr);
        unsigned sevens=0,calls=dump?0:40;
        if(calls) {
            bad_code=static_cast<unsigned char*>(VirtualAlloc(nullptr,bad_size,MEM_RESERVE,PAGE_NOACCESS));
            if(!bad_code) return 4;
            for(unsigned i=0;i<calls;++i) {
                auto f=reinterpret_cast<int(*)()>(bad_code+i*64);
                if(f()==7) ++sevens;
            }
        }
        std::printf("{\"continued\":true,\"first\":%d,\"second\":%d,\"bad_calls\":%u,\"bad_recovered\":%u,\"recovered\":%ld,\"dump_ready\":%s}\n",
                    a,b,calls,sevens,recovered,ready?"true":"false");
        return 0;
    }
    if(!wcscmp(mode,L"unhandled")) {
        if(!wait_dump_ready()) {std::fputs("dbghelp not ready\n",stderr);return 5;}
        fault_write(nullptr);
        return 6;   // not reached
    }
    if(!wcscmp(mode,L"research")) {
        wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\crash.fault",folder);
        auto command=[&](const char* text) {
            HANDLE f=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE) {DWORD n;WriteFile(f,text,DWORD(strlen(text)),&n,nullptr);CloseHandle(f);}
        };
        command("divide 1-0000000000000000");
        for(int i=0;i<40 && GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;++i) Sleep(100);
        char text[64];snprintf(text,sizeof text,"divide %lu\n",GetCurrentProcessId());command(text);
        for(int i=0;i<40 && GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;++i) Sleep(100);
        Sleep(1000);
        std::printf("{\"continued\":true,\"command_left\":%s}\n",GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES?"true":"false");
        return 0;
    }
    if(!wcscmp(mode,L"hang")) {
        for(int i=0;i<50;++i) {redirect::crash::heartbeat(tid);Sleep(10);}
        hang_here();
        for(int i=0;i<250;++i) {redirect::crash::heartbeat(tid);Sleep(10);}
        std::printf("{\"continued\":true}\n");
        return 0;
    }
    return 2;
}
