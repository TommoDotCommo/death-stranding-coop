#pragma once
// The partner's vehicle (game_offsets.md, "Vehicles: creation, resources and transform playback"). When the partner
// rides, this world shows a vehicle of the same kind under him: made with the game's pooled vehicle creator from the
// vehicle unit table (no nearby vehicle needed), moved every tick through its VehicleMover's transform playback (mode 1),
// left parked where the partner got off, replaced when he rides another kind, and removed when he leaves or the session
// or world ends. One vehicle at a time; the pool is the mod's own, never the game's.
#include "discovery.h"
#include "puppet.h"
#include <functional>
#include <string>
namespace mod::ride {
using puppet::Transform;
// What the partner's vehicle looks like: the creator's variant and paint word, filtered by paint().
struct Look { uint8_t variant=0; uint16_t paint=0; bool operator==(const Look& o) const {return variant==o.variant&&paint==o.paint;} };
// Only what ordinary player vehicles were seen to use (variant 0 trucks, 1 the bike) and plain colour indices
// (each six-bit channel below 30; 30/31 select gold/silver resources): anything else is drawn as the default.
Look paint(uint8_t kind,Look wanted);
struct Api {
    std::function<uint64_t(uint8_t kind,const Transform&,Look)> spawn;   // wire kind (ds::VehicleKind); entity or 0
    std::function<void(uint64_t entity)> remove;
    std::function<bool(uint64_t entity,const Transform&)> place;     // playback target; false = no mover to drive
    std::function<bool(uint64_t entity,uint8_t kind)> alive;         // still the vehicle class this kind made
};
// Kinds the game's vehicle unit table provides (motorbike, truck). The speed car has no known unit: never spawned.
bool supported(uint8_t kind);
class PartnerVehicle {
public:
    PartnerVehicle(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // permitted=false (no partner, game event, world change): the vehicle goes. kind 0 = the partner is on foot: the
    // vehicle stays parked where it is. `at` is the partner's vehicle transform (game axes) when kind != 0.
    void tick(uint64_t nowMs,bool permitted,uint8_t kind,const Transform* at,Look look={});
    // The world was replaced (load): the entity went with it; forget it without calling the game.
    void forget(const char* why);
    uint64_t entity() const {return entity_;}
    uint8_t kind() const {return kind_;}
    unsigned failures() const {return failures_;}
    static constexpr unsigned MaxFailures=5;
    static constexpr uint64_t RetryMs=2000;
private:
    void drop(const char* why);
    Api api_;std::function<void(const std::string&)> log_;
    uint64_t entity_=0,nextTry_=0;uint8_t kind_=0;unsigned failures_=0;bool moved_=false;Look look_{};
};
// Signature-checked native calls (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
// Game transform of the partner's vehicle from presence (adapter axes): rider position + offset, vehicle angles.
Transform vehicleTransform(const std::array<double,3>& riderPosition,const std::array<double,3>& offset,const std::array<double,3>& angles);
}
