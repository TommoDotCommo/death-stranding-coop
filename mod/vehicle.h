#pragma once
#include "discovery.h"
#include <array>
#include <string>

namespace mod::vehicle {
// Pinned build 13300582. Static evidence only: see game_offsets.md, "Vehicles".
// No engine calls, heap scan, writes, actor creation, or protocol changes.
enum class Kind {Unknown, None, Motorbike, Truck};
enum class Seat {Unknown, None, Driver, Passenger};
struct Observation {
    bool available=false, coherent=false, riding=false, speedAvailable=false;
    Kind kind=Kind::Unknown;
    Seat seat=Seat::Unknown;
    int seatIndex=-1; // Static helper selection: 0 driver; 1..4 passenger. Not a wire enum.
    uint64_t sampledAtUs=0, porter=0, component=0, vehicle=0;
    std::array<double,3> position{}; // Vehicle world transform, game axes (z up).
    std::array<float,9> rotation{};  // Row-major, same layout as Entity.
    double speed=0;                 // Magnitude of position delta / time; never native velocity.
    std::string reason="not read";
};

class Reader {
    const discovery::Sampler& sampler_;
    uint64_t previousPorter_=0, previousComponent_=0, previousVehicle_=0, previousSeat_=0, previousTime_=0;
    int previousSeatIndex_=-1;
    std::array<double,3> previousPosition_{};
public:
    static constexpr unsigned MaxComponents=256;
    static constexpr uint64_t MinGapUs=10000, MaxGapUs=500000;
    static constexpr double MaxSpeed=200; // Reject discontinuities; do not turn a teleport into speed.
    explicit Reader(const discovery::Sampler& s):sampler_(s) {}
    Observation read(uint64_t monotonicUs);
    void reset();
};

// One-shot or bounded 60-second watch, at most four observations a second.
// A stale/malformed command cannot enable/disable a watch or cause a memory read.
class Monitor {
    Reader reader_;
    unsigned remaining_=0;
    uint64_t started_=0, last_=0;
    bool sampled_=false;
public:
    static constexpr uint64_t PeriodUs=250000, DurationUs=60000000;
    static constexpr unsigned MaxSamples=240;
    explicit Monitor(const discovery::Sampler& s):reader_(s) {}
    bool command(const std::string& content,const std::string& runStamp,uint64_t nowUs);
    std::string tick(uint64_t nowUs); // Empty when disabled/not due; otherwise one bounded "VEHICLE {json}\n" row.
};
std::string row(const Observation&);
}
