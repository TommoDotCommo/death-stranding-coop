#pragma once
// The partner's gear (opt-in, config key `partner_gear`, off by default; game_offsets.md "Gear on the partner body").
//
// The porter's backpack, pouches, cuffs and other gear are child entities of the porter (entity +78 first child,
// +80 next sibling), each moved by an `AttachToHelperMover` (entity +b0, its helper name at mover +b0, a C string)
// that pins it to a named helper of the parent (`HLP_Backpack`, `HLP_Hcuf`, `HLP_PodPouch_2nd`, ...). Gear the
// porter does not show is a child too, kept invisible (entity flag bit 1 clear). The body the mod makes for the
// partner has none of it.
//
// The mirror copies the visible gear onto the partner body the engine's way: each piece is a new entity made from
// the source piece's own entity resource (EntityResource_ExportedCreateEntity, as the body itself is made), then
// parented to the body at the same helper with Entity_ExportedSetParentAndAttachToHelper. The resource brings the
// piece's own children in their default state (the copied backpack's pouches are the resource's, not the porter's). Only plain gear entities and the backpack are copied; cargo
// (DSBaggage2Entity: its record is real, saved cargo), the Odradek (the body's resource brings its own) and anything
// without a named helper are left out. The copies follow the body's visibility, so a parked or hidden body shows no
// gear. The source is the local porter: both porters share the story state, so their gear matches closely; sending
// the partner's own gear over the wire is not done.
#include "discovery.h"
#include "puppet.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace mod::gear {
constexpr uint64_t FirstChild=0x78,NextSibling=0x80,Flags=0x88,Mover=0xb0,HelperName=0xb0;
constexpr uint64_t FlagVisible=1ull<<1;
// Image offsets of build 13300582 (live 2026-11-08, the porter's children).
constexpr uint64_t EntityVtable=0x3d04050,BackpackVtable=0x3dcb9e0,HelperMoverVtable=0x3d0b140,BackpackMoverVtable=0x3ded730;
constexpr unsigned MaxNodes=128,MaxDepth=3,MaxSiblings=64,MaxPieces=24,MaxHelper=63;
struct Node {
    uint64_t entity=0,vtable=0,flags=0,moverVtable=0;
    std::string helper;   // empty: no helper mover, or an unreadable name
    int parent=-1;        // index of the parent node, -1 for a direct child of the porter
};
// The porter's subtree, depth first (a parent before its children), bounded by MaxNodes/MaxDepth/MaxSiblings.
// Unreadable links end a branch; the porter itself is not included.
std::vector<Node> snapshot(const discovery::Sampler&,uint64_t porter);
struct Piece {
    uint64_t source=0;
    int parent=-1;        // index into the plan, -1 for the body
    std::string helper;
};
// The pieces worth copying: the porter's direct children that are visible, a plain gear entity or the backpack, and
// attached to a named helper. Nested gear is not copied: a piece made from its resource brings the resource's own
// children (the backpack brings pouches, a rain cover and a cylinder), so copying the porter's nested pieces as well
// would double them. At most MaxPieces. `image` is the module base.
std::vector<Piece> plan(const std::vector<Node>&,uint64_t image);
struct Api {
    std::function<uint64_t(uint64_t entity)> resource;                               // the entity's EntityResource
    std::function<uint64_t(uint64_t resource,const puppet::Transform&)> create;      // 0 when refused
    std::function<void(uint64_t entity,uint64_t parent,const std::string& helper)> attach;
    std::function<void(uint64_t entity,bool)> visible;
    std::function<void(uint64_t entity)> remove;                                     // the game's queued removal
    std::function<bool(uint64_t entity)> alive;                                      // still the entity we made
};
// Makes and keeps the copies on one body. Call tick once per game-thread tick.
class Mirror {
public:
    static constexpr unsigned PerTick=2,MaxFailures=3;
    Mirror(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // body: the partner body (0: none); world: the porter entity (a new one means a new world, whose entities
    // replaced the old ones: the old copies are forgotten, never touched). pieces: the plan, taken when a body is
    // first seen. at: where new copies are made before they are attached. shown: the body is visible.
    void tick(uint64_t body,uint64_t world,const std::function<std::vector<Piece>()>& pieces,const puppet::Transform& at,bool shown);
    // Removes every copy still alive (the body stays). The next tick on a body makes them again.
    void clear();
    void stop(){clear();stopped_=true;}
    void start(){stopped_=false;}
    size_t made() const;
    bool done() const {return body_&&next_>=pieces_.size();}
private:
    struct Made {uint64_t entity=0;bool shown=false;};
    Api api_;std::function<void(const std::string&)> log_;
    uint64_t body_=0,world_=0;std::vector<Piece> pieces_;std::vector<Made> made_;size_t next_=0;unsigned failures_=0;bool stopped_=false;
};
// A backpack carries a cargo carrier component (DSBaggage2CarrierComponent, vtable image+3dd9750). The game's cargo
// manager gives the porter's backpack its carrier (component +50); a copy gets none, and the backpack's update
// (2be3a84) passes that carrier to the slot lookup 2c52010 (slot count +10, slot array +18, slot type at slot +1)
// without a null check and writes the found slot's +70: live 2026-11-08 a job faulted reading address 0x18 and the
// game waited for it forever. The mirror gives each backpack copy an empty carrier the mod owns: one slot for every
// slot type, no stacks, no items, unknown to the cargo manager, so the copy draws no cargo. The carrier (+a0) and every
// slot (+78) hold a Windows CRITICAL_SECTION the game enters (2c5ce00); a zeroed one faulted live (write to 0x24), so
// both are initialised.
constexpr uint64_t CarrierComponentVtable=0x3dd9750,ComponentCount=0x90,ComponentArray=0x98,ComponentCarrier=0x50;
constexpr uint64_t CarrierLock=0xa0,SlotLock=0x78,LockSize=0x28;
constexpr unsigned CarrierSlotTypes=32,MaxComponents=256;
struct EmptyCarrier {
    alignas(16) unsigned char carrier[0x100]{};
    uint64_t slots[CarrierSlotTypes]{};
    alignas(16) unsigned char slot[CarrierSlotTypes][0x100]{};
    EmptyCarrier();   // count and array filled in; slot i has type i
    uint64_t address() const {return reinterpret_cast<uint64_t>(carrier);}
};
// The carrier component among the entity's components (+90 count, +98 array, bounded), 0 when absent.
uint64_t carrierComponent(const discovery::Sampler&,uint64_t entity);
// Signature-checked native calls (Windows); throws elsewhere or when the pinned image differs.
Api nativeApi(discovery::Sampler&);
}
