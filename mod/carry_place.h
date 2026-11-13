#pragma once
// Where the partner's carried cases sit (with `carry_visual`). The summary (carry_visual.h) says what is carried; this
// says where each case is, measured on the carrier's own game: every case the game draws on the porter has its world
// transform in its cargo record (+0x38, written by the game's packing each frame; game_offsets.md, "Carried cargo:
// where the game puts each case"), and the porter's backpack is an entity. Each case goes out as its transform
// relative to that backpack; the partner's game puts its copy at the same place relative to the copy of the backpack
// the body wears. So the back stack is packed as the game packed it, and the cases on the hips and arms are shown too.
// Read-only on the sender; the receiver only places the meshes cargo_stack.h already makes.
#include "discovery.h"
#include "puppet.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
namespace mod::carryplace {
using puppet::Transform;
constexpr uint8_t WireType=16,CartWireType=17;   // 17: the cases on the cart the porter tows, relative to it
constexpr unsigned MaxPlaced=24;                     // cases sent (the game's back stack alone seldom passes a dozen)
constexpr uint64_t RecordWorld=0x38;                 // the case's world transform in its record (3 f64, 9 f32)
struct Placed { uint32_t item=0; bool variant=false; uint8_t slot=0; Transform rel; };
// Transforms: position, and rotation rows = the images of right, forward, up (as the game keeps them).
Transform relative(const Transform& anchor,const Transform& world);   // world seen from anchor
Transform compose(const Transform& anchor,const Transform& rel);      // and back
// A rotation in 47 bits (a unit quaternion's three smaller components, 15 bits each, and which one was left out).
uint64_t packRotation(const std::array<float,9>&);
std::array<float,9> unpackRotation(uint64_t);
// Every case on the porter's cargo slots (back, arms, hips: slot types 1..5), relative to `anchor` (the porter's
// backpack). nullopt when the chain is unreadable; cases further than 3 m from the anchor are left out (a record the
// game has not placed yet).
// hands: the cases in his hands instead (slot types 6, 7), for the animation sample (core HeldCase).
// slots (optional, hands only): the hand slots found last time, used for up to 2 s while the carrier and its slot
// array are the same (every read is guarded; the full slot walk costs about a millisecond).
struct SlotCache {uint64_t carrier=0,array=0,at=0;std::vector<uint64_t> slots;};
std::optional<std::vector<Placed>> readPlaced(const discovery::Sampler&,uint64_t porterEntity,uint64_t componentVtable,uint64_t manager,const Transform& anchor,bool hands=false,
                                              SlotCache* slots=nullptr,uint64_t now=0);
// A case in a hand relative to the porter's root, as the animation sample carries it (millimetres, packed rotation;
// tag = slot type | variant << 7), and back.
// The cases on any carrier (a floating carrier's: every slot), relative to `anchor`, within `reach` m.
std::optional<std::vector<Placed>> readCarrier(const discovery::Sampler&,uint64_t carrier,uint64_t manager,const Transform& anchor,double reach=4.0);
ds::HeldCase toHeld(const Placed&);
Placed fromHeld(const ds::HeldCase&);
// Wire: one event per case (none carried: one event with total 0). item = the item id; ref = rotation (47 bits),
// index (5), variant (1); position = the relative position in units of 0.1 mm... sent times 100 (the wire keeps
// centimetres); yaw carries total (5 bits) and the list's generation (8) as a whole number of wire steps.
std::vector<ds::CargoEvent> encode(const std::vector<Placed>&,uint8_t generation,uint8_t type=WireType);
class Receiver {
public:
    explicit Receiver(uint8_t type=WireType):type_(type){}
    // A complete list when its last event arrives; a broken run is dropped.
    std::optional<std::vector<Placed>> feed(const ds::CargoEvent&);
    void reset(){pending_.clear();active_=false;}
private:
    std::vector<Placed> pending_;bool active_=false;uint8_t generation_=0,total_=0,type_=WireType;
};
// Sends the list when the cases are somewhere else than last sent (another item, or one moved more than Moved) and
// have stayed where they are for two looks (within Still: the stack sways while the porter moves), at most once per
// MinGap ms, and again on every new connection.
class Sender {
public:
    explicit Sender(uint8_t type=WireType):type_(type){}
    static constexpr double Moved=0.10,Still=0.012;static constexpr uint64_t MinGap=3000;   // the hips move a few centimetres against the back while he stands
    std::vector<ds::CargoEvent> observe(const std::optional<std::vector<Placed>>& read,bool connected,uint64_t now);
private:
    static bool alike(const std::vector<Placed>&,const std::vector<Placed>&,double);
    std::vector<Placed> last_,sent_;bool haveLast_=false,haveSent_=false,wasConnected_=false;uint64_t sentAt_=0;uint8_t generation_=0,type_=WireType;
};
}
