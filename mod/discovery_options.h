#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace mod::discovery {
constexpr const char* PinnedHash="9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b";
struct Options {
    unsigned hz=10, maxMiB=32, animationSlots=0;
    std::string sha256;
    // Reviewed image-relative POINTER SLOTS, never guessed object offsets.
    std::map<std::string,uint64_t> roots;
};
}
