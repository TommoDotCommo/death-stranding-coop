#pragma once
// Other players' vehicles in group play (opt-in `others_vehicles`, on in the launcher's group play). Every presence
// sample through the relay carries the vehicle its player sits on (core.h State: kind, offset, angles, variant, paint);
// for each other player drawn here who rides, this world shows a vehicle of that kind under him, as the partner's
// vehicle is shown in two-player play (ride.h PartnerVehicle: made from the vehicle unit table, moved by its mover's
// transform playback, parked where he got off). One PartnerVehicle per player, each with a pool of its own (a pool
// holds four; the game's creator does not check its capacity), at most Max at once; a pool is used again after its
// vehicle went. A player not seen for GoneMs takes his vehicle with him.
//
// Riding along: next to another player's truck or bike while he drives, the passenger key holds this porter on it
// (passenger.h, as on the partner's truck). Which one: the nearest within reach whose driver is in it; while riding,
// the same one until he gets out or goes.
#include "ride.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace mod::othervehicles {
constexpr size_t Max=16;
constexpr uint64_t GoneMs=3000;
struct Seen { uint64_t id=0; bool visible=false; uint8_t kind=0; ride::Transform at; ride::Look look; };
class Fleet {
public:
    using Make=std::function<std::optional<ride::Api>()>;   // a fresh pool's calls; nullopt when the game's code differs
    Fleet(Make make,std::function<void(const std::string&)> log):make_(std::move(make)),log_(std::move(log)){}
    // permitted=false (a game event, no world): every vehicle goes. `seen`: the other players drawn now.
    void tick(uint64_t now,bool permitted,const std::vector<Seen>& seen);
    // The world was replaced: the entities went with it; forget them without calling the game.
    void forget(const char* why);
    struct Vehicle { uint64_t id=0,entity=0; uint8_t kind=0; bool driven=false; };
    std::vector<Vehicle> vehicles() const;
    size_t size() const {return rides_.size();}
    size_t pools() const {return made_;}
    bool unavailable() const {return unavailable_;}
private:
    struct Entry { std::unique_ptr<ride::PartnerVehicle> vehicle; ride::Api api; uint64_t seen=0; bool driven=false; };
    Make make_;std::function<void(const std::string&)> log_;std::map<uint64_t,Entry> rides_;std::vector<ride::Api> spare_;
    size_t made_=0;bool unavailable_=false;
};
// The vehicle this porter gets on: the nearest within reach (metres, ground distance) of the vehicles whose driver is
// in them; `sticky` (riding already) keeps that one while it is there and driven.
struct Candidate { uint64_t id=0,entity=0; uint8_t kind=0; bool driven=false; std::array<double,3> at{}; };
std::optional<Candidate> choose(const std::vector<Candidate>&,const std::array<double,3>& porter,double reach,uint64_t sticky);
}
