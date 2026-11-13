#pragma once
// Riding along on the partner's truck (passenger.cpp). When the partner drives a truck, this game shows it as the
// mod's display truck (ride.h). Standing next to it, the player presses the passenger key and his porter is held on
// the truck's side with the game's own "follow a helper" mode of the player mover, the one the game uses to carry the
// player on a buddy bot (game_offsets.md, "Riding along"): no seat, no physics, no driver change; the truck goes where
// the partner drives it and the porter goes with it. He gets off with the same key, and is let go by himself when the
// partner gets out, the truck goes, a game event starts or the link drops; he is then set down beside the truck.
#include "puppet.h"
#include <cstdint>
#include <functional>
#include <string>
namespace mod::passenger {
using puppet::Transform;
constexpr double Reach=4.5;                          // metres from the porter to the truck to get on
constexpr unsigned Key=0x4a;                         // J: the default passenger key (config passenger_key)
// What the game says about this porter's mover: 0 free, 1 blending onto the helper, 2 held, 3 blending off.
constexpr uint8_t Free=0,Held=2;
struct Look {
    bool permitted=false;          // partner connected, no game event here, this porter on foot
    bool partnerInTruck=false;     // the partner drives a truck and this game shows it
    uint64_t truck=0;              // the display truck's entity (0: none)
    double distance=1e9;           // from this porter to the truck
    bool key=false;                // the passenger key was pressed (an edge)
    uint8_t mover=Free;            // the mover's follow state
};
enum class Do { Nothing, GetOn, GetOff };
class Rider {
public:
    // One call per tick. GetOn: hold the porter on `truck()`; GetOff: let go and set him down beside it.
    Do tick(const Look&);
    bool riding() const {return truck_!=0;}
    uint64_t truck() const {return truck_;}
    const std::string& why() const {return why_;}   // why the last GetOff happened
    void forget(){truck_=0;heldSeen_=false;}         // the world was replaced: nothing to release
private:
    uint64_t truck_=0;bool heldSeen_=false;unsigned freeLooks_=0;std::string why_;
};
// The game's routines (Windows only): hold `porter` on `helper` of `vehicle`, let it go, read the follow state.
struct Api {
    std::function<bool(uint64_t porter,uint64_t vehicle,const char* helper)> attach;
    std::function<void(uint64_t porter)> release;
    std::function<uint8_t(uint64_t porter)> state;
};
Api nativeApi(discovery::Sampler&);
}
