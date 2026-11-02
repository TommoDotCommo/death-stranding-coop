#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace redirect {
// Deduplicating, rate-limited audit lines for DENY and DEVICE records. The first
// occurrence of a distinct (kind, from, to) is written at once and given an id.
// Later identical records are only counted. At most once per interval (and at
// shutdown) ONE line `REPEATS\tid:count id:count ...` lists the counts since the
// last flush. Time comes from the caller, so tests use a fake clock.
// Not thread safe: the caller holds its log lock.
class Throttle {
public:
    using Sink = std::function<void(const std::wstring& line)>;
    explicit Throttle(uint64_t interval_ms=5000, size_t max_keys=2048);
    static bool throttled(const std::wstring& kind) { return kind==L"DENY" || kind==L"DEVICE" || kind==L"REDIRECT_READ" ||
        kind==L"NET_ALLOW" || kind==L"NET_DENY" || kind==L"NET_LOG"; }
    void record(const std::wstring& kind, const std::wstring& from, const std::wstring& to, uint64_t now_ms, const Sink&);
    void flush(uint64_t now_ms, const Sink&, bool force=false);
    size_t distinct() const { return keys_.size(); }
private:
    struct Key { unsigned id; uint64_t pending, total; };
    uint64_t interval_, last_flush_=0;
    size_t max_keys_;
    bool started_=false;
    std::unordered_map<std::wstring, Key> keys_;
    uint64_t overflow_=0;
};
}
