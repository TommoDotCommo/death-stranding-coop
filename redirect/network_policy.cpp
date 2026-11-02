#include "network_policy.h"

namespace redirect {
namespace {
bool loopback4(uint32_t a) {
#ifdef MUTANT_NET_EXACT_LOOPBACK
    return a == 0x7f000001u;
#else
    return (a >> 24) == 127;
#endif
}
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
NetVerdict classify4(const NetPolicy& p, uint32_t a) {
    if(loopback4(a)) return NetVerdict::local;
#ifndef MUTANT_NET_NO_PEERS
    for(auto peer : p.peers) if(peer == a) return NetVerdict::peer;
#else
    (void)p;
#endif
    return NetVerdict::deny;
}
std::wstring lower(const std::wstring& s) {
    std::wstring r;
    for(auto c : s) r += (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c;
    return r;
}
}

NetVerdict classify_endpoint(const NetPolicy& p, const Endpoint& e) {
#ifdef MUTANT_NET_ALLOW_ALL
    (void)p; (void)e; return NetVerdict::local;
#endif
    if(e.family == kAfInet) return classify4(p, be32(e.addr));
    if(e.family == kAfInet6) {
#ifdef MUTANT_NET_ANY_V6
        return NetVerdict::local;
#endif
        static const uint8_t one[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
        bool is_one = true;
        for(int i = 0; i < 16; ++i) if(e.addr[i] != one[i]) is_one = false;
        if(is_one) return NetVerdict::local;
        // IPv4-mapped ::ffff:a.b.c.d reaches the IPv4 address: classify it as one.
        bool mapped = e.addr[10] == 0xff && e.addr[11] == 0xff;
        for(int i = 0; i < 10; ++i) if(e.addr[i]) mapped = false;
#ifndef MUTANT_NET_MAPPED_LOCAL
        if(mapped) return classify4(p, be32(e.addr + 12));
#else
        if(mapped) return NetVerdict::local;
#endif
        return NetVerdict::deny;
    }
    return NetVerdict::deny; // other families (Bluetooth, Hyper-V sockets, ...) are not part of co-op
}

bool parse_ipv4(const std::wstring& s, uint32_t& out) {
    uint32_t value = 0; int parts = 0; size_t i = 0;
    while(parts < 4) {
        size_t start = i; uint32_t part = 0;
        while(i < s.size() && s[i] >= L'0' && s[i] <= L'9' && i - start < 3) part = part * 10 + uint32_t(s[i++] - L'0');
        size_t len = i - start;
        if(len == 0 || part > 255 || (len > 1 && s[start] == L'0')) return false;
        value = value << 8 | part; ++parts;
        if(parts < 4) { if(i >= s.size() || s[i] != L'.') return false; ++i; }
    }
    if(i != s.size()) return false;
    out = value; return true;
}

NetVerdict classify_host(const NetPolicy& p, const std::wstring& raw) {
#ifdef MUTANT_NET_ALLOW_ALL
    (void)p; (void)raw; return NetVerdict::local;
#endif
    if(raw.empty()) return NetVerdict::passive;
    auto h = lower(raw);
#ifdef MUTANT_NET_NAME_PREFIX
    if(h.compare(0, 9, L"localhost") == 0) return NetVerdict::local;
#else
    if(h == L"localhost" || h == L"localhost.") return NetVerdict::local;
#endif
    if(h == L"::1" || h == L"[::1]") return NetVerdict::local;
    uint32_t a;
    if(parse_ipv4(h, a)) return classify4(p, a);
    return NetVerdict::deny;
}

bool parse_net_peers(const std::wstring& text, std::vector<uint32_t>& out) {
    out.clear(); std::wstring item;
    auto push = [&]() {
        if(item.empty()) return true;
        uint32_t a;
        if(!parse_ipv4(item, a)) return false;
        if((a >> 24) == 127 || a == 0 || a == 0xffffffffu || (a >> 28) == 0xe) return false;
        out.push_back(a); item.clear();
        return out.size() <= 16;
    };
    for(auto c : text) { if(c == L'|') { if(!push()) return false; } else item += c; }
    return push();
}

bool parse_net_mode(const std::wstring& s, NetMode& out) {
    if(s == L"enforce") { out = NetMode::enforce; return true; }
    if(s == L"log-only") { out = NetMode::log_only; return true; }
    return false;
}

bool net_refuses(const NetPolicy& p, NetVerdict v) {
#ifdef MUTANT_NET_NEVER_REFUSE
    (void)p; (void)v; return false;
#elif defined(MUTANT_NET_REFUSE_LOG_ONLY)
    (void)p; return v == NetVerdict::deny;
#else
    return v == NetVerdict::deny && p.mode == NetMode::enforce;
#endif
}

const wchar_t* verdict_name(NetVerdict v) {
    switch(v) {
    case NetVerdict::local: return L"local";
    case NetVerdict::peer: return L"peer";
    case NetVerdict::passive: return L"passive";
    default: return L"deny";
    }
}
}
