#pragma once
// The game keeps a second Sam, DSPlayerAKukanEntity, parked asleep and hidden at the world origin as a child
// of the local player's CameraEntity (game_offsets.md, "The resident Sam double"). This backend borrows that
// entity as the partner's body instead of creating one. It never creates or removes anything. On the game
// thread it calls the game's own SetSleeping, SetVisible and SetWorldTransform, and writes one data field: the
// target transform of the double's FixedPositionMover, which otherwise puts the double back where it rests every
// frame (game_offsets.md). Everything it changes is restored on release.
#include "puppet.h"
#include <functional>
#include <optional>
#include <string>
namespace mod::puppet {
constexpr uint64_t FlagVisible=1ull<<1,FlagSleeping=1ull<<8;
// Entity tree links, read by the engine's own transform propagation (0x235bf60).
constexpr uint64_t ParentOffset=0x70,FirstChildOffset=0x78,NextSiblingOffset=0x80,FlagsOffset=0x88;
struct DoubleApi {
    std::function<Anchor()> local;
    std::function<uint64_t(uint64_t player)> find;   // 0 when absent or ambiguous
    std::function<uint64_t(uint64_t)> flags;
    std::function<Pose(uint64_t)> read;
    std::function<void(uint64_t,bool)> sleep;        // SetSleeping(entity, sleep, 0)
    std::function<void(uint64_t,bool)> visible;      // SetVisible(entity, visible)
    std::function<void(uint64_t,const Transform&)> place;
    std::function<void(uint64_t,const Transform&)> anchor;   // the FixedPositionMover's stored transform
    std::function<void(uint64_t,double,uint32_t)> animate;   // speed (m/s) and packed actions into the double's animation network
    // Optional: give the double an instance of the porter's animation network (the double's own network has no
    // walking clips). upgrade returns the double's original network instance (0 = not done); downgrade puts it back.
    std::function<uint64_t(uint64_t,uint64_t)> upgrade;
    std::function<void(uint64_t,uint64_t)> downgrade;
    // Private-room floor effects (DSPlayerAKukanEffFloorEntity children): hide(false) records which were visible and
    // hides them; show(true) restores exactly those.
    std::function<void(uint64_t,bool)> effects;
};
constexpr double MovingSpeed=.3;   // m/s; below this the double plays its standing animation
constexpr uint64_t MoverOffset=0xb0,MoverOwnerOffset=0x48,MoverTransformOffset=0x58;
// Exactly one DSPlayerAKukanEntity among the children of the player's CameraEntity children,
// every link checked back through the child's parent slot. Bounded walk; 0 otherwise.
uint64_t findDouble(const discovery::Sampler&,uint64_t player,uint64_t cameraVtable,uint64_t doubleVtable);
uint64_t readFlags(const discovery::Sampler&,uint64_t entity);
// The porter's own animation variables say what he is doing; Sprint and Is_Crouch become ds::Sprint / ds::Crouch.
// Read-only. The variable indices are looked up by name once per manager and variable count.
// Packed actions: the low 16 bits are ds::Flag bits (Sprint, Crouch); bits 16..20 carry the wire `action` field.
constexpr uint32_t FlagSprintBit=1,FlagCrouchBit=2;   // ds::Sprint, ds::Crouch (core/core.h)
constexpr uint32_t JumpBit=1u<<16,ActionMoveBit=1u<<17,ActionsBit=1u<<18,FallBit=1u<<19,LandedBit=1u<<20;
constexpr uint32_t ReplayedActions=FlagSprintBit|FlagCrouchBit|JumpBit|ActionMoveBit|ActionsBit|FallBit|LandedBit;
// The porter's bool variable for each bit, in this order.
constexpr const char* ActionNames[]={"Sprint","Is_Crouch","Is_Jump","Action_Move","Is_Actions","Is_Fall","Is_Landed"};
constexpr uint32_t ActionBits[]={FlagSprintBit,FlagCrouchBit,JumpBit,ActionMoveBit,ActionsBit,FallBit,LandedBit};
struct ActionCache {uint64_t manager=0;uint32_t count=0;int index[7]={-1,-1,-1,-1,-1,-1,-1};};
uint32_t readActions(const discovery::Sampler&,uint64_t entity,uint64_t animatorVtable,ActionCache&);
// Adapter state (Y up) to a game transform (Z up), facing yaw; inverse of stateFromPose.
Transform fromState(const std::array<double,3>& position,double yaw);
DoubleApi nativeDoubleApi(discovery::Sampler&);
// Shared-cargo experiment: "create <type id> <run stamp>" (one command per file write). Returns the id, or nothing for a
// malformed or foreign command. The id is the DSBaggage2 record id (+20, e.g. 519 for resins).
std::optional<uint32_t> parseCargoCommand(const std::string& text,const std::string& stamp,const char* verb="create");
// DSBaggage2Manager_sExportedCreateBaggage (2cd6b20): queues cargo of that type at the transform; asynchronous.
std::function<void(uint32_t,const Transform&)> nativeCargoCreate(discovery::Sampler&);
// DSGameState_sExportedIsStoppingGameEvent (2dd3760): true while a game event or cutscene holds gameplay. Game thread.
std::function<bool()> nativeGameEvent(discovery::Sampler&);
// Application_sExportedContinueLastSave (2642a00): asks the game to load the last save; refused by the game when not allowed.
std::function<void()> nativeContinueLastSave(discovery::Sampler&);
// DSBaggage2Manager_sExportedCreateAndAddBaggageToPlayer (2cd6a60): queues cargo of that id into the porter's load.
std::function<void(uint32_t)> nativeCargoGive(discovery::Sampler&);
// DSBaggage2Manager_sExportedDeleteBaggage (2cd6d40): queues a kind-2 request whose +8 is the argument; the create
// request keeps 0x1fffff there, so the argument is probably a 21-bit cargo id (record +20 & 0x1fffff). Unverified live.
std::function<void(uint32_t)> nativeCargoDelete(discovery::Sampler&);
// DSConstructionPointManager_sExportedCreateConstruction (2cd1870 -> 2cd6c00): (EDSConstructionPointType as u8, 60-byte
// world transform, bool) queues a creation request on the construction manager (global 7bf4960 + 56a278, under its
// lock); asynchronous. Types: Stage/Player/Net variants, e.g. 24..26 Ladder, 27..29 FieldRope, 30..32 Bridge30m
// (game_offsets.md, "Structures"). The bool is stored at request +640; its meaning is unknown.
std::function<void(uint8_t,const Transform&,bool)> nativeConstruct(discovery::Sampler&);
class Double {
public:
    enum class Stage {Off,Parked,Driving,Hidden,Yielded,Failed};
    Double(DoubleApi api,std::function<void(const std::string&)> record):api_(std::move(api)),record_(std::move(record)) {}
    void enable(){if(stage_==Stage::Off)stage_=Stage::Parked;}
    void walk(bool on){walk_=on;}   // use upgrade/downgrade on the next acquire
    // True when the game itself is using this porter's double (it does so for the private room): the double has been
    // yielded to the game, or it is awake or shown while we are not driving it. Reads only; game thread.
    bool gameUsesDouble(uint64_t porter);
    // target==nullptr releases the double to the game's parked state. Calls happen only when permitted.
    // speed is the partner's horizontal speed in m/s; it drives the double's walk/run animation.
    // hold: the partner is only missing for now (late packets, private room, loading): the double is hidden and put
    // to sleep where it stands but stays borrowed, so it comes back without a new acquire. Each acquire with walk
    // builds an animation-network instance the game never frees, so a flickering partner must not re-acquire.
    void tick(bool permitted,const Transform* target,double speed=0,uint32_t actions=0,bool hold=false);
    Stage stage() const {return stage_;}
    const char* name() const;
    uint64_t entity() const {return double_;}
private:
    DoubleApi api_;std::function<void(const std::string&)> record_;
    Stage stage_=Stage::Off;uint64_t player_=0,double_=0,parkedFlags_=0;
    Transform parked_{},placed_{};
    double speed_=0;uint32_t flags_=0;bool walk_=false,hidEffects_=false;uint64_t original_=0;
    void restoreNetwork();
    void acquire(const Anchor&,const Transform&);
    void drive(const Anchor&,const Transform&);
    void release(const Anchor&,bool force);
    void hide(const Anchor&);
    void reveal(const Anchor&,const Transform&);
    bool stillOurs(const Anchor&);
    void forget(const char* why);
    void checkLocal(const Anchor& before);
    void note(const char* event,uint64_t flags,const Pose& readback);
};
}
