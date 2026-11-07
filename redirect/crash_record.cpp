// Crash and hang recorder: see crash_record.h. Windows only.
//
// Rules for everything reachable from the vectored handler and the unhandled filter (the faulting thread may hold
// the heap lock, the loader lock or the audit log's lock):
// - no allocation: every buffer is static and used under the recorder's own owner word;
// - no lock another thread or the faulting thread may hold: no heap, no loader calls (GetModuleFileName,
//   GetModuleHandle, toolhelp), no CRT printf, no audit-log note();
// - every read of memory the recorder does not own goes through ReadProcessMemory on its own process, which fails
//   instead of faulting: the module list (PEB loader list), PE headers, .pdata and stack slots;
// - unwinding uses the module's .pdata found by the recorder (not RtlLookupFunctionEntry, whose function-table lock
//   a suspended main thread could hold) and RtlVirtualUnwind, which only reads, within the thread's stack bounds.
#include "crash_record.h"
#include <dbghelp.h>
#include <intrin.h>
#include <atomic>
#include <cstring>

namespace redirect::crash {
namespace {
HANDLE log_file=INVALID_HANDLE_VALUE;
uint64_t log_written=0;
bool log_full=false;
wchar_t log_name[MAX_PATH], dump_name[MAX_PATH];
unsigned level=0;
uint32_t hang_seconds=0;
HMODULE (*load_dbghelp)(wchar_t*,size_t)=nullptr;

// The recorder's owner word: the thread id that may use the static buffers below. No waiting on anything else.
std::atomic<DWORD> owner{0};
std::atomic<uint32_t> busy_drops{0};
Seen first_seen;
uint32_t repeats=0, unhandled_records=0;
bool cap_noted=false, map_at_record=false;
ModuleTable modules;
Text<4096> line;
Text<16384> block;
alignas(16) CONTEXT walk;
uint64_t frames[StackFrames];

std::atomic<PVOID> veh_handle{nullptr};
[[maybe_unused]] SRWLOCK front_lock=SRWLOCK_INIT;   // registration hooks only, never the exception path
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> chained{nullptr};

DWORD main_tid=0;
HANDLE main_thread=nullptr;
uint64_t main_teb=0;
std::atomic<uint64_t> last_pump{0};
std::atomic<bool> pump_waiting{false};

using DumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
std::atomic<DumpFn> dump_fn{nullptr};
std::atomic<bool> dbghelp_tried{false};
std::atomic<bool> dump_taken{false};
HANDLE dump_request=nullptr, dump_done=nullptr;
EXCEPTION_POINTERS* dump_pointers=nullptr;
DWORD dump_tid=0, watchdog_tid=0;

bool acquire(DWORD tid,uint64_t budget_ms) noexcept {
    uint64_t start=GetTickCount64();
    for(;;) {
        DWORD expected=0;
        if(owner.compare_exchange_strong(expected,tid,std::memory_order_acquire)) return true;
        if(expected==tid) return false;           // a fault inside the recorder itself: never recurse
        if(GetTickCount64()-start>=budget_ms) {busy_drops.fetch_add(1,std::memory_order_relaxed);return false;}
        Sleep(1);
    }
}
void release() noexcept {owner.store(0,std::memory_order_release);}

bool peek(uint64_t address,void* out,size_t n) noexcept {
    SIZE_T got=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<LPCVOID>(address),out,n,&got) && got==n;
}

template<size_t N> void emit(Text<N>& t) noexcept {
    if(log_file==INVALID_HANDLE_VALUE || !t.size()) return;
    uint64_t bytes=t.size()*sizeof(wchar_t);
    if(log_full) return;
    if(log_written+bytes>MaxLogBytes-256) {
        log_full=true;
        static const wchar_t full[]=L"CRASH_LOG_FULL\tlog size cap reached\tnothing further is recorded\r\n";
        DWORD n=0;WriteFile(log_file,full,sizeof(full)-sizeof(wchar_t),&n,nullptr);
        return;
    }
    DWORD n=0;
    if(WriteFile(log_file,t.data(),DWORD(bytes),&n,nullptr)) log_written+=n;
}
// Module list from the PEB (x64 layout: PEB+0x18 Ldr, Ldr+0x10 InLoadOrderModuleList; entry +0x30 DllBase,
// +0x40 SizeOfImage, +0x58 BaseDllName). Lock-free and fault-free; bounded by ModuleCap.
void refresh_modules() noexcept {
    modules.clear();
    uint64_t peb=__readgsqword(0x60), ldr=0, link=0;
    if(!peek(peb+0x18,&ldr,8) || !peek(ldr+0x10,&link,8)) return;
    const uint64_t head=ldr+0x10;
    for(uint32_t i=0;i<ModuleCap && link && link!=head;++i) {
        uint64_t base=0,buffer=0;uint32_t size=0;uint16_t length=0;
        if(!peek(link+0x30,&base,8) || !peek(link+0x40,&size,4) || !peek(link+0x58,&length,2) || !peek(link+0x60,&buffer,8)) return;
        wchar_t name[ModuleName]{};size_t n=length/sizeof(wchar_t);if(n>ModuleName-1) n=ModuleName-1;
        if(!n || !peek(buffer,name,n*sizeof(wchar_t))) {name[0]=L'?';n=1;}
        modules.add(base,size,name,n);
        if(!peek(link,&link,8)) return;
    }
}
void write_map(const char* when) noexcept {
    block.clear();
    for(uint32_t i=0;i<modules.size();++i) {
        const auto& m=modules.at(i);
        if(block.size()>sizeof(block)/sizeof(wchar_t)-512) {emit(block);block.clear();}
        block.add("CRASH_MODULE\t").add(m.name).add("\tbase=").hex(m.base,16).add(" size=").hex(m.size).add(" (").add(when).add(")\r\n");
    }
    block.add("CRASH_MODULES\t").dec(modules.size()).add(" modules\t").add(when).add("\r\n");
    emit(block);
}
// Finds the .pdata entry for pc in its module. False: no module or no entry (a leaf function).
bool function_entry(uint64_t pc,uint64_t& image,RUNTIME_FUNCTION& found) noexcept {
    auto m=modules.find(pc);
    if(!m) return false;
    image=m->base;
    uint32_t pe=0,rva=0,size=0;
    if(!peek(image+0x3c,&pe,4) || !peek(image+pe+24+112+3*8,&rva,4) || !peek(image+pe+24+112+3*8+4,&size,4)) return false;
    uint32_t target=uint32_t(pc-image),lo=0,hi=size/sizeof(RUNTIME_FUNCTION);
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;RUNTIME_FUNCTION f{};
        if(!peek(image+rva+uint64_t(mid)*sizeof f,&f,sizeof f)) return false;
        if(target<f.BeginAddress) hi=mid;
        else if(target>=f.EndAddress) lo=mid+1;
        else {found=f;return true;}
    }
    return false;
}
// Walks `c` (a copy) within [low, high). The first frame is the context's own instruction pointer.
uint32_t unwind(CONTEXT& c,uint64_t low,uint64_t high) noexcept {
    uint32_t count=0;bool refreshed=false;
    for(;count<StackFrames;) {
        uint64_t pc=c.Rip;
        if(!pc) break;
        frames[count++]=pc;
        if(c.Rsp<low || c.Rsp+8>high) break;
        if(!modules.find(pc) && !refreshed) {refresh_modules();refreshed=true;}
        // A caller frame's pc is a return address, which can lie just past its function (a call as the last
        // instruction): look the function up at pc-1. The faulting or sampled instruction itself is exact.
        uint64_t image=0;RUNTIME_FUNCTION f{};
        if(function_entry(count==1?pc:pc-1,image,f)) {
            PVOID data=nullptr;DWORD64 frame=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,pc,&f,&c,&data,&frame,nullptr);
        } else if(modules.find(pc) || count==1) {
            // A leaf function, or a call to a bad pointer (the return address is on top of the stack).
            uint64_t ret=0;if(!peek(c.Rsp,&ret,8)) break;
            c.Rip=ret;c.Rsp+=8;
        } else break;   // unknown code (JIT, freed module): no reliable way on
    }
    return count;
}
void stack_line(const char* kind,uint32_t count,DWORD tid) noexcept {
    line.clear();line.add(kind).put(L'\t');
    for(uint32_t i=0;i<count;++i) {if(i) line.put(L' ');line.put(L'#').dec(i).put(L' ').location(modules,frames[i]);}
    line.add("\ttid=").dec(tid).add(tid==main_tid?" main":"").add("\r\n");
    emit(line);
}
void record(const char* stage,EXCEPTION_POINTERS* e,DWORD tid) noexcept {
    if(!map_at_record) {map_at_record=true;refresh_modules();write_map("first record");}
    auto r=e->ExceptionRecord;
    uint64_t at=reinterpret_cast<uint64_t>(r->ExceptionAddress);
    if(!modules.find(at)) refresh_modules();
    auto name=fatal_name(r->ExceptionCode);
    line.clear();
    line.add("CRASH\t").add(stage).put(L' ').hex(r->ExceptionCode,8).put(L' ').add(name?name:"exception");
    if((r->ExceptionCode==0xC0000005u || r->ExceptionCode==0xC0000006u) && r->NumberParameters>=2) {
        auto kind=r->ExceptionInformation[0];
        line.add(kind==0?" read ":kind==1?" write ":kind==8?" execute ":" access ").hex(r->ExceptionInformation[1],16);
    }
    line.add(" at ").location(modules,at).add("\ttid=").dec(tid).add(tid==main_tid?" main":"")
        .add(" t=").dec(GetTickCount64()).add(" first_chance_records=").dec(first_seen.count())
        .add(" repeats=").dec(repeats).add(" busy_drops=").dec(busy_drops.load(std::memory_order_relaxed)).add("\r\n");
    emit(line);
    walk=*e->ContextRecord;
    uint64_t high=__readgsqword(0x08),low=__readgsqword(0x10);
    stack_line("CRASH_STACK",unwind(walk,low,high),tid);
}
// Hands one dump to the watchdog thread and waits a bounded time. Never called while holding the owner word.
void request_dump(EXCEPTION_POINTERS* e,DWORD tid) noexcept {
    if(!dump_fn.load(std::memory_order_acquire) || tid==watchdog_tid || dump_taken.exchange(true)) return;
    dump_pointers=e;dump_tid=tid;
    SetEvent(dump_request);
    WaitForSingleObject(dump_done,30000);
}
void write_dump() noexcept {
    auto fn=dump_fn.load(std::memory_order_acquire);
    HANDLE f=CreateFileW(dump_name,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    BOOL ok=FALSE;DWORD error=0;
    if(f!=INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION x{dump_tid,dump_pointers,FALSE};
        auto type=MINIDUMP_TYPE(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules);
        ok=fn(GetCurrentProcess(),GetCurrentProcessId(),f,type,&x,nullptr,nullptr);
        if(!ok) error=GetLastError();
        CloseHandle(f);
    } else error=GetLastError();
    if(acquire(GetCurrentThreadId(),2000)) {
        line.clear();line.add("CRASH_DUMP\t").add(dump_name).add(ok?"\twritten":"\tfailed error=");
        if(!ok) line.dec(error);
        line.add(" tid=").dec(dump_tid).add("\r\n");emit(line);release();
    }
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* e) noexcept {
    if(!fatal_name(e->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    DWORD tid=GetCurrentThreadId();
    if(!acquire(tid,200)) return EXCEPTION_CONTINUE_SEARCH;
    bool fresh=false;
    switch(first_seen.claim(reinterpret_cast<uint64_t>(e->ExceptionRecord->ExceptionAddress))) {
    case Seen::Verdict::fresh: record("first chance",e,tid);fresh=true;break;
    case Seen::Verdict::repeat: ++repeats;break;
    case Seen::Verdict::full:
        if(!cap_noted) {
            cap_noted=true;line.clear();
            line.add("CRASH_CAP\t").dec(FirstChanceCap).add(" distinct first-chance addresses recorded\tlater ones are not\r\n");emit(line);
        }
        break;
    }
    release();
    if(fresh && level>=2) request_dump(e,tid);
    return EXCEPTION_CONTINUE_SEARCH;   // never handled here, whatever happens above
}
LONG WINAPI on_unhandled(EXCEPTION_POINTERS* e) {
    DWORD tid=GetCurrentThreadId();
    if(acquire(tid,500)) {
        if(unhandled_records<UnhandledCap) {++unhandled_records;record("unhandled",e,tid);}
        release();
    }
    request_dump(e,tid);
#ifdef MUTANT_CRASH_NO_CHAIN
    return EXCEPTION_CONTINUE_SEARCH;
#else
    auto next=chained.load(std::memory_order_acquire);
    return next?next(e):EXCEPTION_CONTINUE_SEARCH;
#endif
}

enum class Hang {watching, reported, over};
Hang hang_state=Hang::watching;
uint64_t hang_seen=0;
void check_hang() noexcept {
    uint64_t last=last_pump.load(std::memory_order_acquire);
    if(!hang_seconds || !last || hang_state==Hang::over) return;
    if(hang_state==Hang::reported) {
        if(last<=hang_seen || !acquire(GetCurrentThreadId(),200)) return;
        line.clear();line.add("HANG_END\tmain thread message pump resumed\tsilent for ").dec((last-hang_seen)/1000).add(" s\r\n");
        emit(line);release();hang_state=Hang::over;return;
    }
    if(pump_waiting.load(std::memory_order_acquire)) return;
    uint64_t now=GetTickCount64();
    if(now<last || now-last<uint64_t(hang_seconds)*1000) return;
    if(!acquire(GetCurrentThreadId(),200)) return;
    hang_state=Hang::reported;hang_seen=last;
    uint32_t count=0;DWORD sampled=main_tid;bool got=false;
#ifndef MUTANT_CRASH_HANG_SELF
    // Suspended: only syscalls, the recorder's own tables and RtlVirtualUnwind. Nothing that could wait.
    if(SuspendThread(main_thread)!=DWORD(-1)) {
        walk.ContextFlags=CONTEXT_FULL;
        if(GetThreadContext(main_thread,&walk)) {
            uint64_t high=0,low=0;
            if(peek(main_teb+0x08,&high,8) && peek(main_teb+0x10,&low,8)) {count=unwind(walk,low,high);got=true;}
        }
        ResumeThread(main_thread);
    }
#else
    // Mutant: the watchdog's own stack instead of the main thread's.
    RtlCaptureContext(&walk);sampled=GetCurrentThreadId();
    count=unwind(walk,__readgsqword(0x10),__readgsqword(0x08));got=true;
#endif
    line.clear();line.add("HANG\tmain thread message pump silent for ").dec((now-last)/1000).add(" s")
        .add(got?"\tstack follows":"\tstack unavailable").add(" tid=").dec(sampled).add(" t=").dec(now).add("\r\n");
    emit(line);
    if(got) stack_line("HANG_STACK",count,sampled);
    release();
}
#ifdef CRASH_RECORD_RESEARCH
// RESEARCH BUILD ONLY (make build/research-crash-fault.dll; the default proxy has none of this). A stamp-guarded
// command file <folder>\crash.fault, polled once a second by the watchdog, starts one research thread that faults:
//   divide <stamp>  RaiseException(integer divide by zero) on that thread, recovered by a last-position research
//                   handler that only accepts that thread and a magic parameter: the process goes on
//   access <stamp>  a write to address 0 on that thread: a real crash, nobody recovers it
// <stamp> is the log name's `<pid>-<process start>` (`<pid>` in the fixture). The file is deleted when read; a wrong
// stamp or kind is noted and ignored. One fault per process.
constexpr ULONG_PTR research_magic=0x5245534541524348ull;   // "RESEARCH"
std::atomic<DWORD> research_tid{0};
bool research_done=false;
LONG CALLBACK research_recover(EXCEPTION_POINTERS* e) {
    auto r=e->ExceptionRecord;
    if(r->ExceptionCode==0xC0000094u && r->NumberParameters==1 && r->ExceptionInformation[0]==research_magic &&
       GetCurrentThreadId()==research_tid.load()) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}
__attribute__((noinline)) DWORD WINAPI research_fault(void* kind) {
    research_tid.store(GetCurrentThreadId());
    if(kind) {*static_cast<volatile int*>(nullptr)=1;return 1;}
    ULONG_PTR magic=research_magic;
    RaiseException(0xC0000094u,0,1,&magic);
    if(acquire(GetCurrentThreadId(),2000)) {line.clear();line.add("CRASH_RESEARCH	divide fault recovered	thread continued\r\n");emit(line);release();}
    return 0;
}
void research_poll() {
    if(research_done) return;
    Text<MAX_PATH> path;path.add(log_name);
    wchar_t command[MAX_PATH];lstrcpynW(command,path.data(),MAX_PATH);
    wchar_t* slash=wcsrchr(command,L'\\');if(!slash) return;
    lstrcpynW(slash+1,L"crash.fault",int(MAX_PATH-(slash+1-command)));
    HANDLE f=CreateFileW(command,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return;
    char text[128]{};DWORD n=0;ReadFile(f,text,sizeof text-1,&n,nullptr);CloseHandle(f);DeleteFileW(command);
    while(n && (text[n-1]=='\r' || text[n-1]=='\n' || text[n-1]==' ')) text[--n]=0;
    // The stamp is the log stem after "crash-".
    const wchar_t* stem=wcsrchr(log_name,L'\\');stem=stem?stem+1:log_name;
    char stamp[64]{};size_t s=0;
    for(const wchar_t* p=stem+6;*p && *p!=L'.' && s<sizeof stamp-1;++p) stamp[s++]=char(*p);
    bool divide=!strncmp(text,"divide ",7) && !strcmp(text+7,stamp), access=!strncmp(text,"access ",7) && !strcmp(text+7,stamp);
    if(!acquire(GetCurrentThreadId(),2000)) return;
    line.clear();line.add("CRASH_RESEARCH	").add(text).add(divide||access?"\tstarting the fault thread\r\n":"\tignored: expected divide|access <stamp>\r\n");
    emit(line);release();
    if(!divide && !access) return;
    research_done=true;
    if(divide) AddVectoredExceptionHandler(0,research_recover);
    HANDLE thread=CreateThread(nullptr,0,research_fault,access?reinterpret_cast<void*>(1):nullptr,0,nullptr);
    if(thread) CloseHandle(thread);
}
#endif
DWORD WINAPI watchdog(void*) {
    wchar_t reason[200]=L"no loader";
    HMODULE help=load_dbghelp?load_dbghelp(reason,200):nullptr;
    if(help) {
        auto p=reinterpret_cast<DumpFn>(reinterpret_cast<void*>(GetProcAddress(help,"MiniDumpWriteDump")));
        if(p) dump_fn.store(p,std::memory_order_release); else lstrcpynW(reason,L"MiniDumpWriteDump missing",200);
    }
    dbghelp_tried.store(true,std::memory_order_release);
    if(acquire(GetCurrentThreadId(),2000)) {
        line.clear();line.add("CRASH_DUMP_READY\t").add(dump_fn.load()?(level>=2?"on (unhandled and first recorded fault)":"on (unhandled)"):"off").put(L'\t')
            .add(dump_fn.load()?dump_name:reason).add("\r\n");
        emit(line);release();
    }
    for(;;) {
        if(WaitForSingleObject(dump_request,1000)==WAIT_OBJECT_0) {write_dump();SetEvent(dump_done);continue;}
        check_hang();
#ifdef CRASH_RECORD_RESEARCH
        research_poll();
#endif
    }
}
}

const wchar_t* log_path() noexcept {return log_name;}
bool dump_ready() noexcept {return dbghelp_tried.load(std::memory_order_acquire) && dump_fn.load(std::memory_order_acquire);}

void heartbeat(DWORD tid) noexcept {
    if(tid==main_tid && main_tid) last_pump.store(GetTickCount64(),std::memory_order_release);
}
void pump_blocking(DWORD tid,bool blocking) noexcept {
    if(tid!=main_tid || !main_tid) return;
    pump_waiting.store(blocking,std::memory_order_release);
    last_pump.store(GetTickCount64(),std::memory_order_release);
}

PVOID WINAPI hook_AddVectoredExceptionHandler(ULONG first,PVECTORED_EXCEPTION_HANDLER handler) {
    PVOID result=::AddVectoredExceptionHandler(first,handler);
#ifndef MUTANT_CRASH_BEHIND
    if(first && result && veh_handle.load(std::memory_order_acquire)) {
        // Move the recorder back in front: add the new registration before removing the old one, so there is no
        // moment without it (a doubled call is harmless: the second sees a repeat address).
        DWORD error=GetLastError();
        AcquireSRWLockExclusive(&front_lock);
        if(PVOID fresh=::AddVectoredExceptionHandler(1,on_exception)) {
            if(PVOID old=veh_handle.exchange(fresh,std::memory_order_acq_rel)) ::RemoveVectoredExceptionHandler(old);
        }
        ReleaseSRWLockExclusive(&front_lock);
        SetLastError(error);
    }
#endif
    return result;
}
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI hook_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    // The recorder's filter stays installed; the caller's becomes the one it hands over to. The caller sees the
    // value it would have seen (the previous logical filter), never the recorder's own, so no loop can form.
    if(!veh_handle.load(std::memory_order_acquire)) return ::SetUnhandledExceptionFilter(filter);
    if(filter==on_unhandled) filter=nullptr;
    return chained.exchange(filter,std::memory_order_acq_rel);
}

bool install(const Options& o) noexcept {
#ifdef MUTANT_CRASH_UNINSTALLED
    (void)o;return true;
#else
    if(!o.level || !o.folder || !o.stem || veh_handle.load()) return false;
    level=o.level;hang_seconds=o.hang_seconds;load_dbghelp=o.load_dbghelp;
    Text<MAX_PATH> path;path.add(o.folder).put(L'\\').add(o.stem).add(".log");
    if(path.overflow()) return false;
    lstrcpynW(log_name,path.data(),MAX_PATH);
    path.clear();path.add(o.folder).put(L'\\').add(o.stem).add(".dmp");
    if(path.overflow()) return false;
    lstrcpynW(dump_name,path.data(),MAX_PATH);
    log_file=CreateFileW(log_name,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log_file==INVALID_HANDLE_VALUE) return false;
    main_tid=GetCurrentThreadId();main_teb=__readgsqword(0x30);
    main_thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,main_tid);
    dump_request=CreateEventW(nullptr,FALSE,FALSE,nullptr);dump_done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!main_thread || !dump_request || !dump_done) {CloseHandle(log_file);log_file=INVALID_HANDLE_VALUE;return false;}
    SYSTEMTIME now;GetSystemTime(&now);
    line.clear();
    line.add("CRASH_RECORD\tv1 level=").dec(level).add(" hang=").dec(hang_seconds).add("s pid=").dec(GetCurrentProcessId())
        .add(" main_tid=").dec(main_tid).add("\tstarted ").dec(now.wYear).put(L'-').dec(now.wMonth,2).put(L'-').dec(now.wDay,2)
        .put(L' ').dec(now.wHour,2).put(L':').dec(now.wMinute,2).put(L':').dec(now.wSecond,2).add(" UTC t=").dec(GetTickCount64()).add("\r\n");
    emit(line);
    refresh_modules();write_map("startup");
    PVOID handle=::AddVectoredExceptionHandler(1,on_exception);
    if(!handle) {CloseHandle(log_file);log_file=INVALID_HANDLE_VALUE;return false;}
    veh_handle.store(handle,std::memory_order_release);
    chained.store(::SetUnhandledExceptionFilter(on_unhandled),std::memory_order_release);
    // Default stack: the dbghelp loader runs the proxy's module checks, which keep 64 KiB path buffers on the stack
    // (a 64 KiB watchdog stack overflowed there; the recorder recorded its own overflow).
    HANDLE thread=CreateThread(nullptr,0,watchdog,nullptr,0,&watchdog_tid);
    if(thread) CloseHandle(thread);
    else {line.clear();line.add("CRASH_DUMP_READY\toff\tno watchdog thread; no dump or hang note\r\n");emit(line);}
    return true;
#endif
}
}
