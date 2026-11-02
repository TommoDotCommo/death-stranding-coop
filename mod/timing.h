#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif
#ifndef _WIN32
#include <fstream>
#include <string>
#include <unistd.h>
#endif
namespace mod {
// "<pid>-<process start>": the stem of every exclusively created log. Process ids are reused (live 2026-11-02 a game
// got the id of an earlier probe whose log was still there and the exclusive create refused startup), the start time
// makes the stem unique per process. Windows: creation FILETIME in hex; Linux tests: /proc start time in ticks.
inline std::string runStamp() {
#ifdef _WIN32
    FILETIME c{},e{},k{},u{};
    if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)) throw std::runtime_error("process creation time unavailable");
    char b[40];snprintf(b,sizeof b,"%lu-%08lx%08lx",GetCurrentProcessId(),c.dwHighDateTime,c.dwLowDateTime);return b;
#else
    std::ifstream f("/proc/self/stat");std::string stat((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    auto close=stat.rfind(')');if(close==std::string::npos) throw std::runtime_error("process start time unavailable");
    std::istringstream rest(stat.substr(close+2));std::string field;
    for(int i=3;i<=22;++i) if(!(rest>>field)) throw std::runtime_error("process start time unavailable");
    return std::to_string(getpid())+"-"+field;
#endif
}
// QPC's boot-relative epoch is shared by processes; never subtract a per-DLL origin.
inline uint64_t clockFrequency() {
#ifdef _WIN32
    static const uint64_t f=[] { LARGE_INTEGER v; if(!QueryPerformanceFrequency(&v)||v.QuadPart<=0) throw std::runtime_error("QPC unavailable"); return uint64_t(v.QuadPart); }();
    return f;
#else
    return 1000000000;
#endif
}
inline uint64_t nowUs() {
#ifdef _WIN32
    LARGE_INTEGER v;if(!QueryPerformanceCounter(&v)) throw std::runtime_error("QPC failed");
    auto f=clockFrequency(), q=uint64_t(v.QuadPart);return q/f*1000000+(q%f)*1000000/f;
#else
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
// Own exactly one timer-resolution request, including exception/unload paths.
struct TimerResolution {
#ifdef _WIN32
    TimerResolution() {if(timeBeginPeriod(1)!=TIMERR_NOERROR) throw std::runtime_error("timeBeginPeriod(1) failed");}
    ~TimerResolution() {timeEndPeriod(1);}
#else
    TimerResolution()=default;
#endif
    TimerResolution(const TimerResolution&)=delete;
    TimerResolution& operator=(const TimerResolution&)=delete;
};
// Absolute cadence, skip missed deadlines instead of accumulating sleep/work drift
// or sending bursts after a scheduler stall. Shared with the deterministic tests.
inline uint64_t nextDeadline(uint64_t previous,uint64_t now,uint64_t period) {
    return previous+((now>=previous?(now-previous)/period:0)+1)*period;
}
// Fixed-rate schedule WITHOUT catch-up: after a stall exactly one tick is due, then the next deadline lies in the future.
struct FixedRate {
    uint64_t period, deadline=0; bool started=false;
    explicit FixedRate(uint64_t us):period(us) {}
    bool due(uint64_t now) {
        if(!started) {started=true;deadline=now+period;return true;}
        if(now<deadline) return false;
        deadline=nextDeadline(deadline,now,period);return true;
    }
};
struct Cadence {
    uint64_t period, deadline;
    explicit Cadence(uint64_t us):period(us),deadline(nowUs()) {}
    void wait() {
        deadline=nextDeadline(deadline,nowUs(),period);
        for(auto now=nowUs();now<deadline;now=nowUs()) {
#ifdef _WIN32
            Sleep(DWORD(std::max<uint64_t>(1,(deadline-now)/1000)));
#else
            std::this_thread::sleep_for(std::chrono::microseconds(deadline-now));
#endif
        }
    }
};
struct Histogram {
    // Microseconds, fixed buckets; allocation-free hot path, inclusive upper limits.
    static constexpr std::array<uint64_t,12> limits={1000,2000,4000,8000,12000,16000,20000,32000,50000,80000,120000,250000};
    std::array<uint64_t,13> buckets{};uint64_t count=0,sum=0,max=0;
    void add(uint64_t us) {++count;sum+=us;max=std::max(max,us);++buckets[std::lower_bound(limits.begin(),limits.end(),us)-limits.begin()];}
    std::string json() const {std::ostringstream o;o<<"{\"count\":"<<count<<",\"mean_us\":"<<(count?double(sum)/count:0)<<",\"max_us\":"<<max<<",\"buckets\":[";for(size_t i=0;i<buckets.size();++i)o<<(i?",":"")<<buckets[i];o<<"]}";return o.str();}
};
}
