#include "objects.h"

namespace redirect {
namespace {
std::wstring lower(const std::wstring& s) {
    std::wstring r; r.reserve(s.size());
    for(auto c:s) r+=(c>=L'A'&&c<=L'Z')?wchar_t(c+32):c;
    return r;
}
}

bool valid_object_tag(const std::wstring& t) {
    if(t.empty() || t.size()>32) return false;
    for(auto c:t) if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'_'||c==L'-')) return false;
    return true;
}

bool parse_isolate_list(const std::wstring& text, std::vector<std::wstring>& out) {
    out.clear(); std::wstring item;
    auto push=[&]() {
        if(item.empty()) return true;
        if(item.size()>200) return false;
        for(auto c:item) if(c<0x20 || c==0x7f || c==L'\\') return false;
        out.push_back(item); item.clear(); return true;
    };
    for(auto c:text) { if(c==L'|') { if(!push()) return false; } else item+=c; }
    return push();
}

std::wstring object_name(const ObjectPolicy& p, const std::wstring& name) {
#ifdef MUTANT_OBJECT_NONE
    return name;
#endif
    if(name.empty()) return name;
    std::wstring prefix, base=name;
#ifndef MUTANT_OBJECT_PREFIX
    auto l=lower(name);
    for(const wchar_t* ns:{L"global\\",L"local\\"}) {
        std::wstring n(ns);
        if(l.compare(0,n.size(),n)==0) { prefix=name.substr(0,n.size()); base=name.substr(n.size()); break; }
    }
#endif
#ifdef MUTANT_OBJECT_ALL
    return prefix+base+L"-coop-"+p.tag;
#endif
    auto b=lower(base);
    for(const auto& want:p.isolate) {
#ifdef MUTANT_OBJECT_CASE
        if(want==base) return prefix+base+L"-coop-"+p.tag;
#else
        if(lower(want)==b) return prefix+base+L"-coop-"+p.tag;
#endif
    }
    return name;
}
}
