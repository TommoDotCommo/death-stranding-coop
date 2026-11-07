#pragma once
#include "../core/core.h"
#include "discovery.h"
#include <optional>
#include <string>
namespace mod::join {
enum class Role { None, Host, Guest };
struct Input {
    bool enabled=false, gameThread=false, connected=false, localGameplay=false;
    Role role=Role::None;
    uint64_t connection=0;
    ds::State local{};
    ds::Render host{}; // latest accepted presence, without interpolation; stale samples cannot authorize a join
};
// A plan, not permission to call SetWorldTransform on Sam. No native mover/controller contract is approved.
struct Plan { std::array<double,3> position{}; double yaw=0, distance=0; };
class Decision {
    uint64_t first_=0;
    bool finished_=false;
public:
    std::optional<Plan> tick(const Input&);
    bool finished() const { return finished_; }
};
std::string wouldMove(const Plan&); // deliberately no game write callback

// Read-only progress discovery for the pinned build. These sites describe statically reviewed code/data;
// callers must use the hash-validated Sampler. Tests supply a small fake image with the identical layout.
struct ProgressSites {
    uint64_t missionSlot=0x7c0b000, factsSlot=0x5794720, scope=0x4dd9600;
    uint64_t missionCode=0x335f0f0, integerCode=0x1afc710;
};
struct ProgressObservation {
    ds::Progress value{}; // episode = episode-resource array index; main-order/chapter id remains unknown
    uint32_t episodeHash=ds::UnknownProgress;
    std::string reason="unknown";
};
class ProgressReader {
    const discovery::Sampler& memory_;
    ProgressSites sites_;
public:
    explicit ProgressReader(const discovery::Sampler& s,ProgressSites sites={}):memory_(s),sites_(sites) {}
    ProgressObservation read() const;
};
}
