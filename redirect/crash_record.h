#pragma once
// Opt-in crash and hang recorder (COOP_CRASH_RECORD). Diagnostic only: it never changes what happens to an
// exception. Its vectored handler always answers EXCEPTION_CONTINUE_SEARCH, its unhandled-exception filter always
// hands over to the filter the game installed, and the hang watchdog only suspends the main thread long enough to
// read its stack.
//
// Why: the game's own ERROR dialog prints module names without bases, so a fault address cannot be placed. The
// recorder writes `crash-<pid>-<process start>.log` (UTF-16LE, `KIND<TAB>detail<TAB>detail`, like the audit log)
// with every address as module+offset and the module map (name, base, size).
//
// The part above `#ifdef _WIN32` is pure and host-tested (crash_record_test.cpp): option parsing, the recorded
// exception codes, deduplication and caps, module lookup and allocation-free text formatting.
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace redirect::crash {
constexpr uint32_t FirstChanceCap=32;   // distinct first-chance fault addresses recorded per process
constexpr uint32_t UnhandledCap=4;      // unhandled records per process (normally one: the process then ends)
constexpr uint32_t StackFrames=32;
constexpr uint32_t ModuleCap=512;
constexpr uint32_t ModuleName=64;
constexpr uint64_t MaxLogBytes=1u<<20;  // the log never grows past 1 MiB
constexpr uint32_t DefaultHangSeconds=15;

// COOP_CRASH_RECORD: absent, empty or "0" = off; "1" = records, module map, hang note and a minidump from the
// unhandled-exception filter; "2" = as 1, and the minidump is also written at the FIRST recorded first-chance
// fault (for a game whose own vectored handler ends the process before any unhandled filter runs). Anything else
// is invalid and refuses startup, as the other COOP_* switches do.
inline bool parse(const wchar_t* value,unsigned& level) noexcept {
    level=0;
    if(!value || !*value || !std::wcscmp(value,L"0")) return true;
    if(!std::wcscmp(value,L"1")) {level=1;return true;}
    if(!std::wcscmp(value,L"2")) {level=2;return true;}
    return false;
}
// COOP_CRASH_HANG_SECONDS: absent/empty = 15; "0" = no hang watchdog; 1-600 seconds without the main thread's
// message pump before the hang note. Digits only.
inline bool parse_hang_seconds(const wchar_t* value,uint32_t& seconds) noexcept {
    seconds=DefaultHangSeconds;
    if(!value || !*value) return true;
    uint32_t n=0;size_t i=0;
    for(;value[i];++i) {
        if(value[i]<L'0' || value[i]>L'9' || i>=3) return false;
        n=n*10+uint32_t(value[i]-L'0');
    }
    if(n>600) return false;
    seconds=n;return true;
}
// The exception codes that are recorded at first chance: the fatal class. Everything else (C++ throws, thread-name
// and debug-print exceptions, guard pages, breakpoints) returns at once. nullptr = not recorded.
inline const char* fatal_name(uint32_t code) noexcept {
    switch(code) {
    case 0xC0000005u: return "access violation";
    case 0xC0000006u: return "in-page error";
    case 0xC000001Du: return "illegal instruction";
    case 0xC0000096u: return "privileged instruction";
    case 0xC00000FDu: return "stack overflow";
    case 0xC0000094u: return "integer divide by zero";
    case 0xC0000374u: return "heap corruption";
    case 0xC0000409u: return "fail-fast";
    default: return nullptr;
    }
}
// Distinct keys (fault addresses), first come first served, capped. Used under the recorder's own lock.
class Seen {
    uint64_t keys_[FirstChanceCap]{};
    uint32_t count_=0;
public:
    enum class Verdict {fresh, repeat, full};
    Verdict claim(uint64_t key) noexcept {
#ifndef MUTANT_CRASH_NO_DEDUPE
        for(uint32_t i=0;i<count_;++i) if(keys_[i]==key) return Verdict::repeat;
#endif
#ifndef MUTANT_CRASH_NO_CAP
        if(count_>=FirstChanceCap) return Verdict::full;
#else
        if(count_>=FirstChanceCap) return Verdict::fresh;
#endif
        keys_[count_++]=key;return Verdict::fresh;
    }
    uint32_t count() const noexcept {return count_;}
};
struct Module {uint64_t base=0, size=0; wchar_t name[ModuleName]{};};
class ModuleTable {
    Module modules_[ModuleCap];
    uint32_t count_=0;
public:
    void clear() noexcept {count_=0;}
    // `name` need not be terminated; it is cut to ModuleName-1 characters. False when full or empty.
    bool add(uint64_t base,uint64_t size,const wchar_t* name,size_t length) noexcept {
        if(count_>=ModuleCap || !base || !size) return false;
        auto& m=modules_[count_++];m.base=base;m.size=size;
        size_t n=length<ModuleName-1?length:ModuleName-1;
        for(size_t i=0;i<n;++i) m.name[i]=name[i];
        m.name[n]=0;return true;
    }
    const Module* find(uint64_t address) const noexcept {
        for(uint32_t i=0;i<count_;++i) {
            const auto& m=modules_[i];
#ifndef MUTANT_CRASH_FIND_EDGE
            if(address>=m.base && address-m.base<m.size) return &m;
#else
            if(address>=m.base && address-m.base<=m.size) return &m;
#endif
        }
        return nullptr;
    }
    uint32_t size() const noexcept {return count_;}
    const Module& at(uint32_t i) const noexcept {return modules_[i];}
};
// Fixed-size text; never allocates. Overflow truncates and is remembered.
template<size_t N> class Text {
    wchar_t data_[N];
    size_t length_=0;
    bool overflow_=false;
public:
    void clear() noexcept {length_=0;overflow_=false;}
    Text& put(wchar_t c) noexcept {if(length_<N-1) data_[length_++]=c; else overflow_=true;return *this;}
    Text& add(const char* s) noexcept {for(;s && *s;++s) put(wchar_t((unsigned char)*s));return *this;}
    Text& add(const wchar_t* s) noexcept {for(;s && *s;++s) put(*s);return *this;}
    Text& hex(uint64_t v,unsigned digits=1) noexcept {
        add("0x");char b[16];unsigned n=0;
        do {b[n++]="0123456789abcdef"[v&15];v>>=4;} while(v);
        for(;n<digits && n<16;) b[n++]='0';
        while(n) put(wchar_t(b[--n]));
        return *this;
    }
    Text& dec(uint64_t v,unsigned digits=1) noexcept {
        char b[20];unsigned n=0;
        do {b[n++]=char('0'+v%10);v/=10;} while(v);
        for(;n<digits && n<20;) b[n++]='0';
        while(n) put(wchar_t(b[--n]));
        return *this;
    }
    // module+0xoffset, or ?0xaddress when no listed module contains it.
    Text& location(const ModuleTable& t,uint64_t address) noexcept {
        if(auto m=t.find(address)) return add(m->name).put(L'+').hex(address-m->base);
        return put(L'?').hex(address);
    }
    const wchar_t* data() noexcept {data_[length_]=0;return data_;}
    size_t size() const noexcept {return length_;}
    bool overflow() const noexcept {return overflow_;}
};
}

#ifdef _WIN32
#include <windows.h>
namespace redirect::crash {
struct Options {
    unsigned level=1;            // from parse(); 0 is not passed to install
    uint32_t hang_seconds=0;     // 0 = no hang watchdog (the caller has no message-pump heartbeat)
    const wchar_t* folder=nullptr;  // existing folder for the log and the dump
    const wchar_t* stem=nullptr;    // file stem: <folder>\<stem>.log and <stem>.dmp
    // Called once on the watchdog thread (never under the loader lock or in a handler) to load System32's
    // dbghelp.dll. Returns nullptr and a reason when the module policy cannot admit it: then no dump is written.
    HMODULE (*load_dbghelp)(wchar_t* reason,size_t capacity)=nullptr;
};
// Call once, on the process's main thread (the thread whose stack the hang note samples). DllMain-safe: registers
// the vectored handler and the unhandled-exception filter, opens the log, writes the startup module map and starts
// the watchdog thread. False when the log or a registration failed (nothing then stays registered).
bool install(const Options&) noexcept;
const wchar_t* log_path() noexcept;
// Import hooks, to be placed in the game's import tables. They keep the recorder in FRONT of what the game registers
// (the game adds its own first-position vectored handler and an unhandled filter after this proxy is loaded) and
// otherwise behave exactly like the APIs: the game's handler is registered as asked, and the filter the game sets
// is called after the recorder's, with the previous value returned to the game as before.
PVOID WINAPI hook_AddVectoredExceptionHandler(ULONG first,PVECTORED_EXCEPTION_HANDLER handler);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI hook_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);
// Message-pump heartbeat from the main thread (other threads are ignored). `blocking` brackets a GetMessage call
// that may wait legitimately for input; the watchdog does not count that wait as a hang.
void heartbeat(DWORD tid) noexcept;
void pump_blocking(DWORD tid,bool blocking) noexcept;
// For fixtures: true once the watchdog has loaded dbghelp (false when no dump will be possible).
bool dump_ready() noexcept;
}
#endif
