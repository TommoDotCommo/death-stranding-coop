#pragma once
#include "discovery_options.h"
#include <optional>
#include <string>
#include <cstdint>
namespace mod {
struct Config {
    unsigned slot=0, peerSlot=1;
    std::string role="none", progressMismatch="warn", joinMove="log", peer, nickname, provider, fault="none", bind;   // bind: local IPv4 for a non-loopback peer (empty = the route's)
    uint32_t peerAddress=0x7f000001, bindAddress=0;                       // host byte order; 0 = choose the route's local address
    uint64_t session=0;
    bool logging=true, required=false, impairment=false, discovery=false, selfTick=false, gameThread=false, puppetProbe=false, virtualPad=false, autoContinue=false, joinAtHost=false;
    // How the partner is shown from the start, without a test command: "off" (default), "peer" (the game's resident
    // double) or "own" (a Sam the mod makes; the double until it exists). Requires puppet_probe and an explicit role.
    std::string partner="off";
    bool partnerMarker=false; // the partner's map pin and the partner's pings from the start (marker.h); no ping key
    bool partnerGear=false;   // the porter's visible gear copied onto the partner's own body (gear.h)
    bool carryVisual=false;   // ds3: send what the porter carries, show what the partner carries (carry_visual.h)
    unsigned presenceLogMaxMiB=8;
    std::string shareEnvironment="off"; // host-authoritative time only; weather contract remains unknown
    discovery::Options discoveryOptions;
};
// Deliberately small strict flat JSON schema. No paths from JSON are used for writes.
Config parseConfig(const std::string&);
// A numeric IPv4 address a co-op peer may have: loopback 127.0.0.0/8, or any unicast address (LAN, a VPN such as
// Tailscale, or public). Refuses names, 0.0.0.0/8, broadcast, multicast and reserved 240/4. Host byte order.
std::optional<uint32_t> peerAddress(const std::string&);
}
