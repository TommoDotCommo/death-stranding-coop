#pragma once
// Likes for the partner (opt-in, config key `partner_likes`; game_offsets.md "Likes for the partner"). Pressing the
// like key (L: free in the game's default bindings) sends the partner one like as a CARGO event of type 9; the
// partner's game gives its porter the like the game's own way (DSLikeCommand_sReceiveLike, reason Other, the like
// counter popup). Likes are the game's real likes: they are saved and count toward the porter's evaluation and the
// like trophies, which is why the feature is off unless asked for. Both sides rate-limit: a held or hammered key
// sends at most one like per MinGapMs, and a receiver gives at most PerMinute likes a minute whatever arrives.
#include "discovery.h"
#include "../core/core.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
namespace mod::likes {
constexpr uint8_t WireType=9;
constexpr uint32_t MaxPerEvent=1;
ds::CargoEvent encode(uint32_t count,uint32_t serial);
std::optional<uint32_t> decode(const ds::CargoEvent&);   // the like count, or nullopt when malformed
class Sender {
public:
    static constexpr uint64_t MinGapMs=1500;
    std::optional<ds::CargoEvent> press(uint64_t now,bool connected);
private:
    uint64_t last_=0;bool sent_=false;uint32_t serial_=0;
};
class Receiver {
public:
    static constexpr uint64_t MinGapMs=1000;static constexpr unsigned PerMinute=10;
    bool accept(uint64_t now);   // whether a like that arrived now is given
private:
    std::deque<uint64_t> given_;
};
struct Api { std::function<bool(uint32_t)> give; };   // false: the game's likes state is not there (no like given)
// Signature-checked native call (Windows, pinned build); throws elsewhere or when the image differs.
Api nativeApi(discovery::Sampler&);
}
