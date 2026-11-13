#pragma once
// Floating carriers (config `partner_cart`, opt-in): the carts a porter tows are drawn behind the partner body in the
// partner's game, as real carts made by the game's own creator and moved there every tick.
//
// Read side (live 2026-11-14, game_offsets.md "Floating carriers"): the cargo manager *(image+0x7bf4968) lists every
// carrier (+0x46d280 count, +0x46d288 array). A cart's carrier has type 2 (u32 at +0), an id whose bits 13..20 are
// the GameActor unit 9 (+0x38), flags at +0x88 (bit 0 set while it is attached to the porter) and its entity at
// +0x20 - 0x20 (DSHoveringCartEntity, vtable image+0x3d5fed0), whose world transform is at +0xc8 (3 f64) and +0xe0
// (9 f32, rows).
//
// Show side: 0x2669710(&id, request) makes a cart through the GameActor manager (actor type 9); a teleport message
// (vtable image+0x3dd96b8, sent with 0x235c990, as the game's own cart placement 0x2cb2e00 does) moves it; and
// 0x22fbd10(GameActor manager, &id) removes it, as the cargo manager does when a cart's record goes away.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>
namespace mod::carts {
constexpr uint64_t Manager=0x7bf4968, CarrierCount=0x46d280, CarrierArray=0x46d288;
constexpr uint64_t CarrierId=0x38, CarrierFlags=0x88, CarrierEntity=0x20, EntityVtable=0x3d5fed0, Position=0xc8, Rotation=0xe0;
constexpr uint32_t CarrierCart=2, CartUnit=9, Attached=1;
constexpr unsigned MaxCarriers=4096;
inline bool cartId(uint32_t id){return ((id>>13)&0xff)==CartUnit;}
struct Cart { uint32_t id=0; uint64_t entity=0; bool attached=false; std::array<double,3> position{}; std::array<float,9> rotation{}; };
// Every cart of this world with a sane transform.
std::vector<Cart> read(const discovery::Sampler&,uint64_t base);
// The carts this porter tows (attached, not one of `shown`, the partner's carts drawn here), nearest first, at most
// ds::AnimationCarts, as offsets from his root in mm; a cart further than the wire allows is left out.
std::vector<ds::CartPose> towed(const std::vector<Cart>&,const std::array<double,3>& porter,const std::set<uint32_t>& shown);
struct Place { std::array<double,3> position{}; std::array<float,9> rotation{}; };
// Where one of the partner's carts goes: his body plus the offset.
Place place(const ds::CartPose&,const std::array<double,3>& body);
// The carts made here for the partner. show(): make what is missing (one at a time: the game makes a cart a few
// frames after it is asked), move the rest, remove the extra ones. clear(): remove every one.
struct Api {
    std::function<uint32_t(const Place&)> make;            // the new cart's actor id, 0 when refused
    std::function<bool(uint32_t,const Place&)> move;       // false while the cart does not exist (yet)
    std::function<void(uint32_t)> remove;
};
class Display {
public:
    static constexpr uint64_t MakeWaitMs=5000;   // a cart asked for and not there after this is given up (removed)
    void show(const std::vector<Place>&,Api&,uint64_t now);
    void clear(Api&);
    std::set<uint32_t> ids() const;
    size_t count() const {return carts_.size();}
private:
    struct Shown { uint32_t id=0; uint64_t asked=0; bool seen=false; };
    std::vector<Shown> carts_;
};
#ifdef _WIN32
Api nativeApi(discovery::Sampler&);   // throws when the game's functions differ from the pinned image
#endif
}
