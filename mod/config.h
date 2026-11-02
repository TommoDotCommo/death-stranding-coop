#pragma once
#include "discovery_options.h"
#include <string>
#include <cstdint>
namespace mod {
struct Config {
    unsigned slot=0, peerSlot=1;
    std::string role, peer, nickname, provider, fault="none";
    uint64_t session=0;
    bool logging=true, required=false, impairment=false, discovery=false, selfTick=false, gameThread=false, puppetProbe=false;
    unsigned presenceLogMaxMiB=8;
    discovery::Options discoveryOptions;
};
// Deliberately small strict flat JSON schema. No paths from JSON are used for writes.
Config parseConfig(const std::string&);
}
