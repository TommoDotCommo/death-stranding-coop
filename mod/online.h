#pragma once
// A private server for the game's online layer (opt-in, config key `online_server`; game_offsets.md "Sign-in").
// The game keeps the address it signs in to in one string, and only signs in when its title menu's login message
// arrives, which this build never sends by itself in a modded copy. With `online_server` the mod points that string
// at the players' own server, makes the gateway bodies plain JSON (the game encrypts them under a key of the
// publisher's service, which a private server neither has nor needs) and asks the game's login task to sign in, the
// same call the menu makes. The address is
// numeric and either loopback or the co-op partner's: the publisher's service is never a valid value, and the proxy's
// network policy refuses anything else whatever this says.
#include "config.h"
#include "discovery.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
namespace mod::online {
// What the game's login task says. taskState 2 = running; step 7 = signed in, 8 = failed.
struct State { bool present=false;uint32_t taskState=0;uint8_t step=0;uint32_t error=0;
    bool signedIn() const {return present&&step==7;} bool busy() const {return present&&taskState==2&&step!=7&&step!=8;}
    bool operator==(const State& o) const {return present==o.present&&taskState==o.taskState&&step==o.step&&error==o.error;} };
// When to ask: once the task exists, never while it works or is signed in, at most MaxAsks times, Every apart.
class Requester {
public:
    static constexpr uint64_t Every=30000;static constexpr unsigned MaxAsks=5;
    bool tick(uint64_t now,const State&);   // true: ask the game to sign in now
    unsigned asks() const {return asks_;}
private:
    uint64_t last_=0;unsigned asks_=0;
};
struct Api {
    std::function<State()> state;
    std::function<bool(const std::string&)> point;   // false: the game's address string is not the one this build has
    std::function<bool()> plain;                     // gateway bodies as plain JSON from now on (never undone)
    std::function<bool()> request;                   // false: no login task yet
};
// Signature-checked native calls (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
}
