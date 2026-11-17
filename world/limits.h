#pragma once
// Limits per address for the HTTP side (README, "Limits per address"). A server that listens on the internet is
// reached by anyone: the secret prefix keeps them out of the world, but not off the port. The gate bounds what one
// IPv4 address can cost: how many connections it holds at once, how many requests a second it makes (a second's
// worth, `burst`, can be saved up), and how many wrong guesses (answers 404: a path or prefix that is no one's) it may
// make in a minute before it is blocked for a while. It never looks inside a request, and it remembers at most
// `tracked` addresses (the idle ones are forgotten first; when every one is busy a new address is turned away, so
// memory stays bounded). Loopback is exempt unless `loopback` says otherwise (the launcher's own server on one PC,
// and an administrator's call over SSH). Thread-safe; the clock is the caller's (milliseconds).
#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
namespace world {
struct GateConfig {
    int connections=8;          // connections one address holds at once (0: no cap)
    double rate=20,burst=200;   // requests a second one address may make, and how many can be saved up (rate 0: no cap)
    int strikes=30;             // answers 404 in one minute that block the address (0: never)
    uint64_t blockMs=15*60*1000;// how long a block lasts
    size_t tracked=4096;        // addresses remembered at once
    bool loopback=false;        // apply the limits to 127.0.0.0/8 as well
};
class Gate {
public:
    explicit Gate(GateConfig c={}):c_(c){}
    enum class Open : uint8_t { Ok, Busy, Blocked, Full };
    // A new connection from `ip` (host order). Ok: counted, close() when it ends. Anything else: turn it away.
    Open open(uint32_t ip,uint64_t now){
        std::lock_guard<std::mutex> g(lock_);
        if(exempt(ip))return Open::Ok;
        Entry* e=find(ip,now);
        if(!e)return Open::Full;
        e->seen=now;
#ifndef MUTANT_GATE_BLOCK
        if(e->blockedUntil>now)return Open::Blocked;
#endif
#ifndef MUTANT_GATE_CONNECTIONS
        if(c_.connections>0&&e->open>=c_.connections)return Open::Busy;
#endif
        ++e->open;return Open::Ok;}
    void close(uint32_t ip){
        std::lock_guard<std::mutex> g(lock_);
        if(exempt(ip))return;
        auto it=entries_.find(ip);if(it!=entries_.end()&&it->second.open>0)--it->second.open;}
    // One request: false when the address is over its rate (answer 429 and close) or blocked.
    bool request(uint32_t ip,uint64_t now){
        std::lock_guard<std::mutex> g(lock_);
        if(exempt(ip))return true;
        Entry* e=find(ip,now);if(!e)return false;
        e->seen=now;
        if(e->blockedUntil>now)return false;
        if(c_.rate<=0)return true;
        e->tokens=std::min(c_.burst,e->tokens+double(now-e->filled)*c_.rate/1000.0);e->filled=now;
#ifndef MUTANT_GATE_RATE
        if(e->tokens<1)return false;
#endif
        e->tokens-=1;return true;}
    // What the server answered. A 404 is a strike; `strikes` of them within a minute block the address for blockMs.
    // True when this answer blocked it (the caller logs it once).
    bool answered(uint32_t ip,int status,uint64_t now){
        std::lock_guard<std::mutex> g(lock_);
        if(exempt(ip)||status!=404||c_.strikes<=0)return false;
        Entry* e=find(ip,now);if(!e)return false;
        if(now-e->windowStart>=60000){e->windowStart=now;e->strikes=0;}
        ++e->strikes;
#ifndef MUTANT_GATE_STRIKES
        if(e->strikes>=c_.strikes&&e->blockedUntil<=now){e->blockedUntil=now+c_.blockMs;e->strikes=0;return true;}
#endif
        return false;}
    bool blocked(uint32_t ip,uint64_t now) const {
        std::lock_guard<std::mutex> g(lock_);auto it=entries_.find(ip);return it!=entries_.end()&&it->second.blockedUntil>now;}
    size_t tracked() const {std::lock_guard<std::mutex> g(lock_);return entries_.size();}
private:
    struct Entry { int open=0,strikes=0;double tokens=0;uint64_t filled=0,seen=0,windowStart=0,blockedUntil=0; };
    bool exempt(uint32_t ip) const {return !c_.loopback&&(ip>>24)==127;}
    // The entry of `ip`, made when new. When the table is full the idle address seen longest ago goes (no connection
    // open, not blocked, its bucket full again); none: nullptr.
    Entry* find(uint32_t ip,uint64_t now){
        auto it=entries_.find(ip);
        if(it!=entries_.end())return &it->second;
#ifndef MUTANT_GATE_UNBOUNDED
        if(entries_.size()>=c_.tracked){
            auto victim=entries_.end();
            for(auto e=entries_.begin();e!=entries_.end();++e){
                const Entry& x=e->second;
                bool idle=x.open==0&&x.blockedUntil<=now&&(c_.rate<=0||x.tokens+double(now-x.filled)*c_.rate/1000.0>=c_.burst);
                if(idle&&(victim==entries_.end()||x.seen<victim->second.seen))victim=e;}
            if(victim==entries_.end())return nullptr;
            entries_.erase(victim);}
#endif
        Entry e;e.tokens=c_.burst;e.filled=now;e.seen=now;e.windowStart=now;
        return &entries_.emplace(ip,e).first->second;}
    GateConfig c_;mutable std::mutex lock_;std::map<uint32_t,Entry> entries_;
};
}
