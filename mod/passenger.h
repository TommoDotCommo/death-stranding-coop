#pragma once
// Riding along on the partner's truck (passenger.cpp). When the partner drives a truck, this game shows it as the
// mod's display truck (ride.h). Standing next to it, the player presses the passenger key and his porter is held on
// the truck's side with the game's own "follow a helper" mode of the player mover, the one the game uses to carry the
// player on a buddy bot (game_offsets.md, "Riding along"): no seat, no physics, no driver change; the truck goes where
// the partner drives it and the porter goes with it. He gets off with the same key, and is let go by himself when the
// partner gets out, the truck goes, a game event starts or the link drops; he is then set down beside the truck.
#include "puppet.h"
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
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
// The display truck has no collision (the pooled creation skips the game's physics setup; its collision parts have
// no instances: game_offsets.md, "Riding along"). The mod keeps this porter out of its box instead: a porter found
// inside is put just outside the nearest side, as a wall would stop him.
struct Box { double halfRight=1.5,halfForward=3.8,height=3.3,margin=0.15; };
// Where a rider is held on each kind of display vehicle: a helper of its model and a place from it (metres right,
// forward and up of the helper). The truck has a helper on its side; the bike only its driver's, so the rider sits
// behind the driver. Nothing for a kind nobody can ride along on.
// place: no helper is followed; the porter is held at a place worked out from the vehicle's own transform every frame
// (the game's "hold at a fixed place" form of the same mover mode). The display bike's driver helper holds a rider
// 3.6 m behind the bike whatever offset is given (live), so the bike uses this.
struct Seat { const char* helper;float right,forward,up;bool place=false; };
// The rider's transform for a `place` seat: the vehicle's, moved by the seat's offsets along its own axes.
Transform seatPlace(const Transform& vehicle,const Seat&);
// CartKind: a floating carrier another player tows (group play): the rider stands on it, held at its place.
constexpr uint8_t CartKind=9;
std::optional<Seat> seat(uint8_t vehicleKind);
// The keep-out box of a kind (the truck's, a bike's much smaller one); nothing for other kinds.
std::optional<Box> box(uint8_t vehicleKind);
// Where the driver's game shows a partner who rides along on a bike: behind the driver, from the driver's own place.
constexpr double BikeBehind=0.62,BikeUp=0.06;
// The bike's driver sits this far behind the bike's origin (live: 0.55 m), so the rider's place is behind that.
constexpr double BikeDriverBehind=0.55;
// truck: its transform (rows right, forward, up); porter: a position. The position to move him to, or nothing.
std::optional<std::array<double,3>> pushOut(const Transform& truck,const std::array<double,3>& porter,const Box& box={});
// The game's routines (Windows only): hold `porter` on `helper` of `vehicle`, let it go, read the follow state.
struct Api {
    std::function<bool(uint64_t porter,uint64_t vehicle,const Seat& seat)> attach;
    std::function<void(uint64_t porter)> release;
    // `place` seats: hold the porter at a world transform, and move that place (every frame while he is held).
    std::function<bool(uint64_t porter,const Transform& at)> hold;
    std::function<void(uint64_t porter,const Transform& at)> move;
    std::function<uint8_t(uint64_t porter)> state;
};
Api nativeApi(discovery::Sampler&);
}
