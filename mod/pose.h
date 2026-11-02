#pragma once
#include "discovery.h"
#include <array>
#include <string>
namespace mod {
// One read-only observation of the local player's pose. Game axes: x, y horizontal, z up (units consistent with metres).
// rotation is the 3x3 float32 row-major matrix at entity+0xe0; the forward vector is floats 3..5 (m10, m11, m12).
// available is true only when every check passed AND the pointer chain was unchanged after the copy (coherent).
struct Pose {
    bool available=false, coherent=false;
    std::string reason="not read";
    std::array<double,3> position{};
    std::array<float,9> rotation{};
};
// Built on the discovery Sampler: its scan result (signature tables), guarded double-read copy and chain recheck.
// These are guarded copies off the game thread, NOT typed game calls and NOT an atomic snapshot.
class PoseReader {
    const discovery::Sampler& sampler_;
public:
    explicit PoseReader(const discovery::Sampler& sampler):sampler_(sampler) {}
    Pose read() const;
};
}
