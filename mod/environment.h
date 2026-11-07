#pragma once
#include "discovery.h"
#include "../core/core.h"
#include <functional>
#include <optional>
#include <string>

namespace mod::environment {
// Build 13300582, static evidence only. The reader never calls an export or writes memory.
// Fingerprints cover complete reviewed instruction spans, including branch/RIP targets.
struct Code {uint64_t rva;size_t size;uint64_t fingerprint;};
struct Sites {
    uint64_t application=0x7be6318, weather=0x7bf83b8,mapScale=0x7bf8a10;
    Code getTime{0x365d470,31,0x921c919d6e4d8b1dULL};
    Code getCycle{0x365d4a0,34,0x313753f8ebeb0ef4ULL};
    Code setTime{0x365d960,30,0x10a2614226f90e96ULL};
    Code setTimeBody{0x3631f90,252,0x1eb804441e914972ULL};
    Code getRain{0x2e1f6e0,25,0x719033d04c8f2c80ULL};
    Code rainBody{0x2e0c3f0,211,0xa9856258cee75e6eULL};
    Code mapBody{0x2e0b490,558,0xa03f27038a1185d4ULL};
};
struct Observation {
    bool available=false, cycle=false, weatherAvailable=false;
    float hour=0, forecastTime=0, rainFromMap=0;
    uint64_t application=0, worldTime=0, weather=0;
    std::string reason="not read",weatherReason="not read";
    // rainFromMap is the native signed map condition (4*channel.z-2), NOT a rain percentage.
};
class Reader {
    const discovery::Sampler& memory_;Sites sites_;
public:
    explicit Reader(const discovery::Sampler& s,Sites sites={}):memory_(s),sites_(sites) {}
    Observation read(const std::array<double,3>& gamePosition) const;
    bool setterVerified() const;
    uint64_t setterAddress() const {return memory_.base+sites_.setTime.rva;}
};
struct Input {
    bool enabled=false,guest=false,gameThread=false,networkEnforced=false,gameplay=false,connected=false;
    uint64_t nowMs=0;
    std::optional<ds::Environment> host; // Core has authenticated host/session/nonce/sequence/time and bounds.
    std::array<double,3> gamePosition{};
};
// The callback is injected for host tests. Production supplies only the reviewed native export.
// No direct data writes and no weather setter: regional forecast ownership is still unknown.
class Sync {
    Reader reader_;std::function<void(float,float)> setter_;
    bool applied_=false;uint64_t last_=0;
public:
    static constexpr uint64_t PeriodMs=30000;
    Sync(const discovery::Sampler& s,std::function<void(float,float)> setter,Sites sites={}):reader_(s,sites),setter_(std::move(setter)) {}
    std::string tick(const Input&); // Empty if gated/not due; one ENVIRONMENT applied/refused row otherwise.
};
// Strict stand-in fixture parser: finite decimal hours [0,24), including no trailing text.
uint32_t fixedTime(const std::string&);
bool readCommand(const std::string& content,const std::string& runStamp);
std::string row(const Observation&);
}
