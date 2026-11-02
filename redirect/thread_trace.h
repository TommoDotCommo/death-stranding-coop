#pragma once
// Diagnostic storage only. This never invokes a game function or reads an MSG.
// Records are append-only and published once. The external reader double-reads
// them; a reserved but unpublished slot is incomplete, never a fabricated event.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <type_traits>

namespace redirect::trace {
constexpr uint32_t Capacity=2048, StackFrames=16, ThreadSlots=64;
enum class Api:uint32_t {peek=0, get=1};
enum class Phase:uint32_t {enter=1, leave=2};
struct Record {
    std::atomic<uint32_t> committed{0};
    uint32_t tid=0;
    uint64_t call=0, timeMs=0;
    uint32_t api=0, phase=0, depth=0;
    int32_t result=0;
    uint64_t caller=0, frames[StackFrames]{};
    uint32_t frameCount=0, padding=0;
};
struct Buffer {
    char magic[8]{'D','S','T','H','R','D','0','1'};
    uint32_t version=1, capacity=Capacity, recordSize=sizeof(Record), headerSize=80;
    uint64_t processStart=0, hostBase=0, hostSize=0;
    uint32_t pid=0, padding=0;
    std::atomic<uint32_t> reserved{0}, dropped{0};
    std::atomic<uint64_t> calls[2]{};
    Record records[Capacity];
};
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::is_standard_layout<Buffer>::value && std::is_standard_layout<Record>::value);
static_assert(sizeof(Record)==184 && offsetof(Record,frames)==48 && offsetof(Record,frameCount)==176);
static_assert(offsetof(Buffer,records)==80 && sizeof(Buffer)==80+184*Capacity);

struct Thread {
    std::atomic<uint32_t> tid{0};
    // Only the owning Windows thread accesses these fields, including nested
    // dispatch on that thread. A reused thread ID is not a lifetime guarantee.
    uint32_t depth=0, warmup[2]{}, nestedWarmup[2]{};
    uint64_t next[2]{}, nestedNext[2]{};
};
struct Call {
    Thread* thread=nullptr;
    Api api=Api::peek;
    uint32_t ticket=Capacity, tid=0, depth=0;
    uint64_t id=0, caller=0;
    bool sampled() const noexcept {return ticket<Capacity;}
};
class Observer {
    Thread threads_[ThreadSlots];
public:
    Buffer buffer;
    static bool parse(const wchar_t* value,bool& enabled) noexcept {
        enabled=false;
        if(!value || !*value || !std::wcscmp(value,L"0")) return true;
        if(!std::wcscmp(value,L"1")) {enabled=true;return true;}
        return false;
    }
    Call begin(Api api,uint32_t tid,uint64_t now,uint64_t caller) noexcept {
        Call call;call.api=api;call.tid=tid;call.caller=caller;
        auto index=static_cast<unsigned>(api);
        call.id=buffer.calls[index].fetch_add(1,std::memory_order_relaxed)+1;
        if(!tid) {buffer.dropped.fetch_add(1,std::memory_order_relaxed);return call;}
        for(auto& thread:threads_) {
            auto owner=thread.tid.load(std::memory_order_relaxed);
            if(!owner) thread.tid.compare_exchange_strong(owner,tid,std::memory_order_relaxed);
            if(owner!=tid && thread.tid.load(std::memory_order_relaxed)!=tid) continue;
            call.thread=&thread;call.depth=++thread.depth;
#ifdef MUTANT_TRACE_DEPTH
            call.depth=1;
#endif
            auto& warmup=call.depth>1?thread.nestedWarmup[index]:thread.warmup[index];
            auto& next=call.depth>1?thread.nestedNext[index]:thread.next[index];
            bool sample=warmup<4 || now>=next;
#ifdef MUTANT_TRACE_RATE
            sample=true;
#endif
            if(!sample) return call;
            if(warmup<4) ++warmup;
            next=now>UINT64_MAX-1000?UINT64_MAX:now+1000;
            auto ticket=buffer.reserved.load(std::memory_order_relaxed);
            // One bounded CAS attempt: under contention it is fine to lose a
            // diagnostic pair. Never wait inside a game message callback.
#ifndef MUTANT_TRACE_CAP
            if(ticket>Capacity-2) {buffer.dropped.fetch_add(1,std::memory_order_relaxed);return call;}
#else
            if(ticket>Capacity-2) {call.ticket=0;return call;}
#endif
            if(!buffer.reserved.compare_exchange_strong(ticket,ticket+2,std::memory_order_relaxed)) {
                buffer.dropped.fetch_add(1,std::memory_order_relaxed);return call;
            }
            call.ticket=ticket;return call;
        }
        buffer.dropped.fetch_add(1,std::memory_order_relaxed);return call;
    }
    void publish(const Call& call,Phase phase,uint64_t now,int32_t result,
                 const uint64_t* frames=nullptr,uint32_t count=0) noexcept {
        if(!call.sampled()) return;
        auto index=call.ticket+(phase==Phase::leave?1:0);
        auto& record=buffer.records[index];
        record.tid=call.tid;record.call=call.id;record.timeMs=now;
        record.api=static_cast<uint32_t>(call.api);record.phase=static_cast<uint32_t>(phase);
        record.depth=call.depth;record.result=result;record.caller=call.caller;
        record.frameCount=frames?(count<StackFrames?count:StackFrames):0;
        for(uint32_t i=0;i<record.frameCount;++i)record.frames[i]=frames[i];
#ifndef MUTANT_TRACE_PUBLISH
        record.committed.store(1,std::memory_order_release);
#endif
    }
    void finish(Call& call,uint64_t now,int32_t result) noexcept {
        publish(call,Phase::leave,now,result);
        if(call.thread) {--call.thread->depth;call.thread=nullptr;}
    }
};
} // namespace redirect::trace
