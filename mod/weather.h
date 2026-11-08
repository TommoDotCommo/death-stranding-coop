#pragma once
// Shared rain (game_offsets.md, "Weather replication follow-up"). The host sends the game's weather-map condition
// where it stands (ds::Environment::rain: lower is wetter). The guest compares its own map at the host's position and,
// when they disagree, places the game's own timed local weather influence there: rain (0x2e14f60) or clear
// (0x2e14fa0), keyed by an owner the mod holds, so the game does not save it (its writer keeps only ownerless ones).
// Once started, the influence is renewed while the host stays wet (or dry) and left to expire when that ends.
// Forecast records, the save and story state are never written.
#include "discovery.h"
#include <array>
#include <functional>
#include <optional>
#include <string>
namespace mod::weather {
enum class Mode {None, Rain, Clear};
const char* name(Mode);
struct Api {
    // The local map condition at a game-axis position (nothing when unreadable).
    std::function<std::optional<float>(const std::array<double,3>&)> conditionAt;
    // Place or renew the mod's influence (rain or clear) there; false when the game's weather state is missing.
    std::function<bool(Mode,const std::array<double,3>&,float radius,float lifetime)> influence;
};
// The host's class: wet below Wet, dry above Dry, between them nothing is forced.
constexpr float Wet=-0.5f, Dry=0.5f, Margin=0.5f;
// Pure decision: start an influence only when the host is wet/dry and this world disagrees by more than Margin;
// keep the current one while the host stays in that class.
Mode decide(Mode current,float host,std::optional<float> local);
class Share {
public:
    static constexpr float Radius=150.f, Lifetime=45.f;   // metres, seconds (renewed well before it lapses)
    static constexpr uint64_t RenewMs=15000, CheckMs=2000;
    Share(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // Game thread. permitted: sharing on, connected, normal gameplay. host: the host's latest sample with rain;
    // at: where the host is (game axes). Nothing is placed without both.
    void tick(uint64_t nowMs,bool permitted,std::optional<float> host,const std::optional<std::array<double,3>>& at);
    Mode mode() const {return mode_;}
    unsigned placed() const {return placed_;}
private:
    Api api_;std::function<void(const std::string&)> log_;
    Mode mode_=Mode::None;uint64_t lastPlaced_=0,lastCheck_=0;unsigned placed_=0;bool checked_=false;
};
Api nativeApi(discovery::Sampler&);   // Windows, pinned build; throws when the image differs
}
