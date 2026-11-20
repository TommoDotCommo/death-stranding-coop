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
// Group play: getting on from above or below the seat. Another player's vehicle is chosen by ground distance, so a
// porter on a roof or a ledge over it can press the key. The game's hold does not bring him down (live: held 9.5 m above
// a carrier's seat on a roof): the hold writes the whole place, height too, into the mover's target (0x26ca560 copies
// +0x7c0 into +0x800, 0x268a688 makes it the position once blended in), so what keeps him up comes later (most likely
// his own collision under the roof; unknown). Within SeatStep of the seat's height he is held as before; up to
// SeatClimb above or below he is first moved to the vehicle with the game's teleport (its ground placement looks 2 m
// above and below the target, so he lands on the ground there, not on the roof), then held; further than that (a
// bridge over a road, a cliff) the key is refused, as is any gap over SeatStep when the teleport is unavailable.
constexpr double SeatStep=1.5,SeatClimb=20;
enum class Board { Hold, MoveFirst, Refuse };
Board board(double porterHeight,double seatHeight,bool canMove);
// Where a porter is put beside a vehicle (getting on a truck from above, set down after a ride): out of the vehicle's
// box (1.5 m out for a kind with none) on the side he is on, at the vehicle's own height plus SetDownLift (the ground
// placement then finds the ground within 2 m), never at his own height. Upright, facing as he does (or as the vehicle,
// when he is not read).
constexpr double SetDownLift=1.0,SetDownOut=1.0;
Transform beside(const Transform& vehicle,uint8_t kind,const std::optional<Transform>& porter);
// Where a porter who gets on from above or below is moved first: a place seat's own place (upright), else beside.
Transform boardPlace(const Transform& vehicle,uint8_t kind,const Seat&,const std::optional<Transform>& porter);
// Group play: the vehicle being ridden, as last seen. A game event or the fleet dropping the vehicle can end the ride
// in the same tick the vehicle goes; the rider is then set down beside where it last was, not let go where he is.
class Ridden {
public:
    void see(const Transform& vehicle,uint8_t kind){at_=vehicle;kind_=kind;}
    void clear(){at_.reset();kind_=0;}
    // The vehicle's place now when it is read, else its last known one; nothing when it was never seen.
    std::optional<Transform> setDown(const std::optional<Transform>& now,const std::optional<Transform>& porter) const;
private:
    std::optional<Transform> at_;uint8_t kind_=0;
};
// The game's routines (Windows only): hold `porter` on `helper` of `vehicle`, let it go, read the follow state.
struct Api {
    std::function<bool(uint64_t porter,uint64_t vehicle,const Seat& seat)> attach;
    std::function<void(uint64_t porter)> release;
    // `place` seats: hold the porter at a world transform, and move that place (every frame while he is held).
    std::function<bool(uint64_t porter,const Transform& at)> hold;
    std::function<bool(uint64_t porter,const Transform& at)> move;   // false: the game does not hold him at a place (state, fixed)
    std::function<uint8_t(uint64_t porter)> state;
};
Api nativeApi(discovery::Sampler&);
}
