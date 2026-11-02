#include "audit_throttle.h"
#include <algorithm>
namespace redirect {
Throttle::Throttle(uint64_t interval_ms,size_t max_keys):interval_(interval_ms),max_keys_(max_keys) {}
void Throttle::record(const std::wstring& kind,const std::wstring& from,const std::wstring& to,uint64_t now,const Sink& sink) {
    if(!started_) {started_=true;last_flush_=now;}
    auto key=kind+L"\t"+from+L"\t"+to;
    auto it=keys_.find(key);
#ifdef MUTANT_NO_DEDUP
    it=keys_.end(); // every record is written, as before
#endif
    if(it==keys_.end()) {
        if(keys_.size()>=max_keys_) ++overflow_;
        else {
            unsigned id=(unsigned)keys_.size()+1;
            keys_[key]=Key{id,0,1};
            sink(key+L"\tid="+std::to_wstring(id)+L"\r\n");
        }
    } else {
        ++it->second.pending; ++it->second.total;
    }
#ifndef MUTANT_NO_RATE
    if(now-last_flush_>=interval_) flush(now,sink);
#else
    flush(now,sink,true);
#endif
}
void Throttle::flush(uint64_t now,const Sink& sink,bool force) {
    if(!force && now-last_flush_<interval_) return;
    last_flush_=now;
    std::vector<std::pair<unsigned,uint64_t>> counts;
    for(auto& kv:keys_) if(kv.second.pending) {counts.push_back({kv.second.id,kv.second.pending});kv.second.pending=0;}
    if(counts.empty() && !overflow_) return;
    std::sort(counts.begin(),counts.end());
    std::wstring line=L"REPEATS\t";
    for(auto& c:counts) line+=std::to_wstring(c.first)+L":"+std::to_wstring(c.second)+L" ";
    if(overflow_) {line+=L"overflow:"+std::to_wstring(overflow_);overflow_=0;}
    sink(line+L"\r\n");
}
}
