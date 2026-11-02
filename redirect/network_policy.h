#pragma once
// Outbound network policy for a modded instance: the legal and fairness gate.
//
// Death Stranding's online features (the Social Strand System) put one player's structures, signs and likes into
// other players' worlds through the publisher's servers. A modded client, especially one whose memory the co-op mod
// writes (puppets, shared cargo), must never talk to that service: it could upload state the unmodified game could
// not produce, into worlds of people who did not opt in. So an instance under the proxy may reach only loopback and
// the co-op peers its owner listed; every other destination, host name lookup and WinHTTP connection is refused, as
// if the PC had no internet. `log_only` records the same verdicts without refusing, for discovery runs only.
//
// Pure code (no Windows headers): redirect.cpp converts sockaddr/host arguments and calls these.
#include <cstdint>
#include <string>
#include <vector>

namespace redirect {
enum class NetMode { enforce, log_only };
struct NetPolicy {
    NetMode mode = NetMode::enforce;
    std::vector<uint32_t> peers; // IPv4 unicast addresses, host byte order (a.b.c.d == a<<24|b<<16|c<<8|d)
};
// Windows address family values (AF_INET, AF_INET6), so the classifier tests run on Linux.
constexpr int kAfInet = 2, kAfInet6 = 23;
struct Endpoint {
    int family = 0;
    uint8_t addr[16] = {}; // network byte order as in sockaddr: 4 bytes for IPv4, 16 for IPv6
};
// local: loopback (127.0.0.0/8, ::1, ::ffff:127.x.y.z). peer: an owner-listed co-op peer.
// passive: no destination at all (a resolver asked for the wildcard address to bind). deny: everything else.
enum class NetVerdict { local, peer, passive, deny };
NetVerdict classify_endpoint(const NetPolicy&, const Endpoint&);
// A host name given to a resolver, a connect-by-name call or WinHTTP. Only "localhost" and numeric loopback or peer
// addresses pass. Anything not understood (other names, shorthand or hex IPv4, other IPv6) is denied: the safe side.
NetVerdict classify_host(const NetPolicy&, const std::wstring& host);
// Strict dotted quad: four decimal parts 0..255, no leading zeros, nothing else.
bool parse_ipv4(const std::wstring&, uint32_t& out);
// "a.b.c.d|e.f.g.h"; empty items dropped. False for a malformed address, loopback (always allowed anyway),
// the unspecified, broadcast or a multicast address, or more than 16 peers.
bool parse_net_peers(const std::wstring& text, std::vector<uint32_t>& out);
// "enforce" or "log-only".
bool parse_net_mode(const std::wstring&, NetMode& out);
// True when the call must fail: a deny verdict under enforce.
bool net_refuses(const NetPolicy&, NetVerdict);
const wchar_t* verdict_name(NetVerdict);
}
