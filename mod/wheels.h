#pragma once
// The partner's vehicle turns its wheels (with `partner_vehicle`). The game poses a vehicle's wheels (spin, steering,
// suspension) by writing joint transforms straight into the model's current pose record from its Havok wheel state
// (game_offsets.md, "Wheels"); the display vehicle has no physics, so the game never poses it. The driver's game reads
// its own vehicle's wheel joints and their parents from that record and sends them with the animation sample (core
// VehicleJoint); the partner's game writes them into the display vehicle's record, blended between samples.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
namespace mod::wheels {
// Entity layout (static, research-wheels): model +b8; the model's current record index +250 (0/1), records at
// +170 + 70*k (skeleton +20, joint array +30); the skeleton's joint count +20 and joint table +28 (stride 10, parent
// int16 at +8); a joint is 30 bytes (rotation quaternion xyzw +0, translation +10, scale +20).
constexpr uint64_t Model=0xb8,RecordIndex=0x250,Records=0x170,RecordStride=0x70,RecordSkeleton=0x20,RecordJoints=0x30,
                   SkeletonCount=0x20,SkeletonTable=0x28,SkeletonStride=0x10,SkeletonParent=0x8,JointStride=0x30;
// Wheel joint indices the game stores on the entity from the vehicle's resource (int32 each).
constexpr uint64_t BikeWheels=0xcb0,TruckWheels=0xc60;constexpr unsigned BikeWheelCount=3,TruckWheelCount=4;
constexpr unsigned MaxJoints=512,Ancestors=3;
struct Joint {uint16_t index=0;std::array<float,4> rotation{0,0,0,1};std::array<float,3> position{};};
// The joints to send: each wheel joint and up to `Ancestors` of its parents (never the root, joint 0), increasing,
// at most ds::AnimationJoints. parents[j] = joint j's parent (-1 for none); wheels out of range are left out.
std::vector<uint16_t> chain(const std::vector<int32_t>& wheels,const std::vector<int16_t>& parents);
// Wire form (quaternion int16 / 32767, millimetres) and back.
ds::VehicleJoint toWire(const Joint&);
Joint fromWire(const ds::VehicleJoint&);
// Between two samples: the same joints move towards the next sample (quaternion the short way, normalised).
std::vector<Joint> blend(const std::vector<Joint>& from,const std::vector<Joint>& to,float fraction);
#ifdef _WIN32
// Reads only: the vehicle's wheel joint indices (kind ds::Motorbike / ds::Truck), its skeleton's parents and joint
// count, and the chosen joints' current locals. Nothing when unreadable or out of range.
struct Read {uint16_t skeleton=0;std::vector<Joint> joints;};
std::optional<Read> read(const discovery::Sampler&,uint64_t vehicle,uint8_t kind);
// Writes the joints into the vehicle's current pose record (rotation and translation; scale kept) when its skeleton
// has `skeleton` joints and each index is in range, then marks the entity changed (+88 bit 0, as the game's own
// transform writer does). False (nothing written) otherwise.
bool write(const discovery::Sampler&,uint64_t vehicle,uint16_t skeleton,const std::vector<Joint>&);
#endif
}
