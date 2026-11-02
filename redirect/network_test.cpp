// Game-free tests for the outbound network policy (network_policy.h). Each MUTANT_NET_* build must fail.
#include "network_policy.h"
#include <cstdio>
#include <cstring>

using namespace redirect;
static int failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while(0)

static Endpoint v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Endpoint e; e.family = kAfInet; e.addr[0] = a; e.addr[1] = b; e.addr[2] = c; e.addr[3] = d; return e;
}
static Endpoint v6(const uint8_t (&bytes)[16]) { Endpoint e; e.family = kAfInet6; std::memcpy(e.addr, bytes, 16); return e; }

int main() {
    NetPolicy p;
    CHECK(p.mode == NetMode::enforce); // the default refuses
    CHECK(parse_net_peers(L"192.168.1.20|100.64.0.7", p.peers));
    CHECK(p.peers.size() == 2 && p.peers[0] == 0xc0a80114u);

    // Endpoints: loopback, listed peers, everything else.
    CHECK(classify_endpoint(p, v4(127, 0, 0, 1)) == NetVerdict::local);
    CHECK(classify_endpoint(p, v4(127, 4, 5, 6)) == NetVerdict::local);
    CHECK(classify_endpoint(p, v4(192, 168, 1, 20)) == NetVerdict::peer);
    CHECK(classify_endpoint(p, v4(100, 64, 0, 7)) == NetVerdict::peer);
    CHECK(classify_endpoint(p, v4(192, 168, 1, 21)) == NetVerdict::deny);  // a neighbour on the LAN is not a peer
    CHECK(classify_endpoint(p, v4(192, 0, 2, 1)) == NetVerdict::deny);     // TEST-NET-1, stands for any server
    CHECK(classify_endpoint(p, v4(0, 0, 0, 0)) == NetVerdict::deny);
    CHECK(classify_endpoint(p, v4(255, 255, 255, 255)) == NetVerdict::deny);
    const uint8_t one[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
    const uint8_t mapped_public[16] = {0,0,0,0, 0,0,0,0, 0,0,0xff,0xff, 192,0,2,1};
    const uint8_t mapped_loop[16] = {0,0,0,0, 0,0,0,0, 0,0,0xff,0xff, 127,0,0,1};
    const uint8_t mapped_peer[16] = {0,0,0,0, 0,0,0,0, 0,0,0xff,0xff, 192,168,1,20};
    const uint8_t global6[16] = {0x20,0x01,0x0d,0xb8, 0,0,0,0, 0,0,0,0, 0,0,0,1};
    const uint8_t any6[16] = {};
    CHECK(classify_endpoint(p, v6(one)) == NetVerdict::local);
    CHECK(classify_endpoint(p, v6(mapped_loop)) == NetVerdict::local);
    CHECK(classify_endpoint(p, v6(mapped_peer)) == NetVerdict::peer);
    CHECK(classify_endpoint(p, v6(mapped_public)) == NetVerdict::deny);
    CHECK(classify_endpoint(p, v6(global6)) == NetVerdict::deny);
    CHECK(classify_endpoint(p, v6(any6)) == NetVerdict::deny);
    Endpoint other; other.family = 32; // AF_HYPERV
    CHECK(classify_endpoint(p, other) == NetVerdict::deny);

    // Host names.
    CHECK(classify_host(p, L"") == NetVerdict::passive);
    CHECK(classify_host(p, L"localhost") == NetVerdict::local);
    CHECK(classify_host(p, L"LocalHost.") == NetVerdict::local);
    CHECK(classify_host(p, L"127.0.0.1") == NetVerdict::local);
    CHECK(classify_host(p, L"::1") == NetVerdict::local);
    CHECK(classify_host(p, L"[::1]") == NetVerdict::local);
    CHECK(classify_host(p, L"192.168.1.20") == NetVerdict::peer);
    CHECK(classify_host(p, L"localhost.example.com") == NetVerdict::deny);
    CHECK(classify_host(p, L"localhostx") == NetVerdict::deny);
    CHECK(classify_host(p, L"api.example.com") == NetVerdict::deny);
    CHECK(classify_host(p, L"127.1") == NetVerdict::deny);        // shorthand: not understood, so refused
    CHECK(classify_host(p, L"0x7f000001") == NetVerdict::deny);
    CHECK(classify_host(p, L"2130706433") == NetVerdict::deny);
    CHECK(classify_host(p, L"192.168.1.020") == NetVerdict::deny); // leading zero (octal in some parsers)
    CHECK(classify_host(p, L"2001:db8::1") == NetVerdict::deny);

    // Parsing.
    uint32_t a = 0;
    CHECK(parse_ipv4(L"10.0.0.255", a) && a == 0x0a0000ffu);
    for(auto bad : {L"", L"1.2.3", L"1.2.3.4.5", L"1.2.3.256", L"1.2.3.4 ", L" 1.2.3.4", L"1..3.4", L"1.2.3.-4", L"1.2.3.04", L"1234.1.1.1"})
        CHECK(!parse_ipv4(bad, a));
    std::vector<uint32_t> peers;
    CHECK(parse_net_peers(L"", peers) && peers.empty());
    CHECK(parse_net_peers(L"|10.0.0.2||", peers) && peers.size() == 1);
    for(auto bad : {L"127.0.0.2", L"0.0.0.0", L"255.255.255.255", L"224.0.0.1", L"239.1.1.1", L"example.com", L"10.0.0.1|x"})
        CHECK(!parse_net_peers(bad, peers));
    std::wstring many;
    for(int i = 1; i <= 17; ++i) many += L"10.0.0." + std::to_wstring(i) + L"|";
    CHECK(!parse_net_peers(many, peers));
    NetMode m;
    CHECK(parse_net_mode(L"enforce", m) && m == NetMode::enforce);
    CHECK(parse_net_mode(L"log-only", m) && m == NetMode::log_only);
    CHECK(!parse_net_mode(L"off", m) && !parse_net_mode(L"Enforce", m) && !parse_net_mode(L"", m));

    // Refusal: only a deny under enforce. Log-only never refuses (discovery), loopback/peers never.
    CHECK(net_refuses(p, NetVerdict::deny));
    CHECK(!net_refuses(p, NetVerdict::local) && !net_refuses(p, NetVerdict::peer) && !net_refuses(p, NetVerdict::passive));
    NetPolicy observe; observe.mode = NetMode::log_only;
    CHECK(!net_refuses(observe, NetVerdict::deny));

    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("network policy: all checks passed\n");
    return 0;
}
