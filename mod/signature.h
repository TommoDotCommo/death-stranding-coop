#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <stdexcept>
namespace mod {
// -1 is a wildcard, all other entries must be bytes. Match must be unique.
// Caller supplies an already validated, readable executable-section buffer.
inline std::optional<size_t> uniqueSignature(const std::vector<uint8_t>& data,const std::vector<int>& pattern) {
    if(pattern.empty()) throw std::invalid_argument("empty signature");
    for(int x:pattern) if(x < -1 || x > 255) throw std::invalid_argument("invalid signature byte");
    bool concrete=false;for(int x:pattern) concrete|=x!=-1;
    if(!concrete) throw std::invalid_argument("all-wildcard signature");
    std::optional<size_t> match;
    if(pattern.size()>data.size()) return match;
    for(size_t i=0;i<=data.size()-pattern.size();++i) {
        bool ok=true;for(size_t j=0;j<pattern.size();++j) if(pattern[j]!=-1 && data[i+j]!=pattern[j]) {ok=false;break;}
        if(ok) {if(match) return {};match=i;}
    }
    return match;
}
}
