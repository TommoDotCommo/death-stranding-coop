#pragma once
// The game keeps a second Sam, DSPlayerAKukanEntity, parked asleep and hidden at the world origin as a child
// of the local player's CameraEntity (game_offsets.md, "The resident Sam double"). This backend borrows that
// entity as the partner's body instead of creating one. It never creates or removes anything. On the game
// thread it calls the game's own SetSleeping, SetVisible and SetWorldTransform, and writes one data field: the
// target transform of the double's FixedPositionMover, which otherwise puts the double back where it rests every
// frame (game_offsets.md). Everything it changes is restored on release.
#include "puppet.h"
#include <functional>
#include <cmath>
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
    std::function<void(uint64_t,float)> steer;   // optional: the rider's Turn_Heading (degrees) while the partner rides
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
// Bits 24..26: the vehicle the partner rides (ds::VehicleKind, 0 on foot): the body takes the game's riding pose.
constexpr uint32_t VehicleShift=24,VehicleMask=7u<<VehicleShift;
constexpr uint32_t ReplayedActions=FlagSprintBit|FlagCrouchBit|JumpBit|ActionMoveBit|ActionsBit|FallBit|LandedBit|VehicleMask;
// Vehicle_Type for a wire kind (static, game_offsets.md "Riding"): motorbike 0, truck 1 (doors), speed car 3; -1 = none.
float vehicleAnimationType(uint32_t kind);
// The porter's bool variable for each bit, in this order.
constexpr const char* ActionNames[]={"Sprint","Is_Crouch","Is_Jump","Action_Move","Is_Actions","Is_Fall","Is_Landed"};
constexpr uint32_t ActionBits[]={FlagSprintBit,FlagCrouchBit,JumpBit,ActionMoveBit,ActionsBit,FallBit,LandedBit};
struct ActionCache {uint64_t manager=0,entity=0;uint32_t count=0;int index[7]={-1,-1,-1,-1,-1,-1,-1};};
uint32_t readActions(const discovery::Sampler&,uint64_t entity,uint64_t animatorVtable,ActionCache&);
// One float animation control of an entity by name (the manager's slot table, as readActions); nothing when absent.
struct FloatVariable {uint64_t manager=0,entity=0;uint32_t count=0;int index=-1;};
std::optional<float> readFloatVariable(const discovery::Sampler&,uint64_t entity,uint64_t animatorVtable,const char* name,FloatVariable&);
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
// Research: the same request CreateConstruction (2cd6c00) queues, built here so that chosen flag bytes of the request
// can be set (offsets into the 0x6c8-byte request, 0x600..0x67f; game_offsets.md "Why it stays pending"). Returns the
// new construction id. Game thread only.
std::function<uint32_t(uint8_t,const Transform&,const std::vector<uint16_t>&)> nativeConstructRequest(discovery::Sampler&);
// ArtPartsData (component vtable 3cf2b18, entity+180) get-or-create accessors for the cover (22b2010, cached at +60),
// cover+animation (22b1f80, +68) and facial-rig (22b20a0, +70) model components. Game thread. False when the body's
// ArtPartsData is missing or not its own, or a component did not appear.
// Research: switch art part `index` of the body's porter-derived ArtPartsData (the one with 52 parts) on or off with the
// toggle worker 22b8fe0. False when there is no such component or index.
std::function<bool(uint64_t body,unsigned index,bool on)> nativePartToggle(discovery::Sampler&);
// Mask bits (one or two hex digits after `own`): without 8 or 0x30: 1 cover, 2 cover+anim, 4 facial (empty components).
// 8: ApplyArtPartsVariation with the resident double's art-parts resource; with 8, 2 also switches the leftover one-part
// ArtPartsData's part off and 1 removes it (hangs). Research bits (game_offsets.md, "Dressing the own body: the engine's
// model swap"): 0x10 the engine's model swap (235e010) with the resident double's ModelResource, only when that resource
// builds exactly the double's art-parts resource; 0x20 the same swap without that condition, then the variation apply
// only when the body is still not dressed like the double; 0x40 a read-only report of both bodies; 0x80 the game's own
// in-place art-parts replacement (235d320) with the double's art-parts resource, the call that dresses the resident double
// (game_offsets.md, "Who dresses the resident double"); 0x80 runs alone (only 0x40 still applies). note receives rows.
std::function<bool(uint64_t)> nativeDress(discovery::Sampler&,std::function<unsigned()> mask,std::function<void(const std::string&)> note={});
// Own-mode mask from a `puppet.double` command: "own<stamp>" = 8, "own<h><stamp>" or "own<hh><stamp>" (lowercase hex;
// own0 dresses nothing). Nothing for any other command.
std::optional<unsigned> parseOwnCommand(const std::string& command,const std::string& stamp);
constexpr unsigned DressModelExact=0x10,DressModelThenApply=0x20,DressReport=0x40;
// Layout used by the model swap (game_offsets.md): entity+b8 Model component, entity+180 the primary ArtPartsData,
// entity+90/+98 the component count and list; every component keeps its owner at +48 and its resource at +30; a
// ModelResource keeps its ArtPartsDataResource at +30.
constexpr uint64_t EntityModelOffset=0xb8,EntityArtPartsOffset=0x180,ComponentOwnerOffset=0x48,ComponentResourceOffset=0x30,ModelArtPartsOffset=0x30;
struct ModelSwapPlan {
    uint64_t model=0;      // the resident double's ModelResource to give the body; 0 = refused
    uint64_t builds=0;     // the art-parts resource that model builds (ModelResource+30)
    uint64_t wanted=0;     // the resident double's own art-parts resource (its entity+180 component's resource)
    std::string refusal;   // why model is 0
};
// Reads only. exact: refuse unless the model builds exactly the double's art-parts resource.
ModelSwapPlan planModelSwap(const discovery::Sampler&,uint64_t body,uint64_t resident,uint64_t artPartsVtable,bool exact);
// Reads only: the body has exactly one ArtPartsData of its own, it is the primary one (entity+180) and built from wanted.
bool dressedLike(const discovery::Sampler&,uint64_t body,uint64_t artPartsVtable,uint64_t wanted);
// Reads only: one row per component (vtable as image offset, resource), the primary ArtPartsData, and for every
// ArtPartsData its resource, part count and part `part`'s state byte (+2c) and name hash (CRC32C, from the resource's
// name map at +e0: 12-byte entries {used, hash, index}, capacity at +ec).
std::string describeBody(const discovery::Sampler&,const char* label,uint64_t entity,uint64_t artPartsVtable,unsigned part=20);
// The game calls behind mask 0x10 / 0x20 (game thread): SetModel 235e010(entity, ModelResource, 0) and
// ApplyArtPartsVariation 22bc100(entity, art-parts resource).
struct ModelSwapCalls {
    std::function<void(uint64_t body,uint64_t model)> setModel;
    std::function<void(uint64_t body,uint64_t artParts)> apply;
};
// Mask 0x10 / 0x20 on a made body: plan, swap, then (0x20 only) apply when the body is still not dressed like the
// double. True when the body ends dressed like the double (dressedLike). Rows go to note.
bool dressByModel(const discovery::Sampler&,uint64_t body,uint64_t resident,uint64_t artPartsVtable,unsigned mask,
                  const ModelSwapCalls&,const std::function<void(const std::string&)>& note);
// Mask 0x80. The porter's DSPlayerStreamingControlComponent (update 27caae0) dresses the resident double with
// 235d320(entity, ArtPartsDataResource, force): it announces MsgArtPartsWillChange, takes the old primary ArtPartsData's
// parts off the model (22b4310), removes it, builds one from the resource, caches it at entity+180 and announces
// MsgModelPartsChanged and MsgArtPartsChanged. The mod replays that call once on a made body.
constexpr unsigned DressArtPartsInPlace=0x80;
struct ArtPartsSwapPlan {
    uint64_t wears=0;      // the body's primary art-parts resource now
    uint64_t wanted=0;     // the resident double's (its primary ArtPartsData's resource); 0 = refused
    std::string refusal;
};
// Reads only. Refused unless the body has exactly one ArtPartsData of its own and it is the primary one, the double
// wears art parts of its own, and the body does not wear those already.
ArtPartsSwapPlan planArtPartsSwap(const discovery::Sampler&,uint64_t body,uint64_t resident,uint64_t artPartsVtable);
// Reads only: the entity the streaming control dresses as the double, 269c560(*global): the reference at +30 minus 0x20
// (global = image + 7be79b0). 0 when unreadable or empty. The 0x40 report compares it with the double the mod found.
constexpr uint64_t StreamingDoubleGlobal=0x7be79b0,StreamingDoubleRef=0x30;
uint64_t readStreamingDouble(const discovery::Sampler&,uint64_t global);
// Plan, one call of setArtParts(body, wanted), then dressedLike. True when the body ends dressed like the double.
// Parts in the entity's primary ArtPartsData (component vtable artPartsVtable at entity+180), 0 if unreadable.
unsigned wornParts(const discovery::Sampler&,uint64_t entity,uint64_t artPartsVtable);
bool dressInPlace(const discovery::Sampler&,uint64_t body,uint64_t resident,uint64_t artPartsVtable,
                  const std::function<void(uint64_t body,uint64_t resource)>& setArtParts,const std::function<void(const std::string&)>& note,uint64_t porter=0);
// DSConstructionPointManager_sExportedRemoveConstructionsInArea (2cd1bf0 -> 2cd9760), read statically: (const float4*
// box min, const float4* box max, const world transform*, bool, bool, bool). It collects the structures in the box and
// marks for removal those whose owner byte (+b2) is 1 (first bool), 0 (second) or 2 (third). Unverified live.
std::function<void(const std::array<float,4>&,const std::array<float,4>&,const Transform&,bool,bool,bool)> nativeRemoveConstructions(discovery::Sampler&);
// The partner's own body: a second Sam the mod creates from the resident double's resource with the script export
// EntityResource_ExportedCreateEntity (game_offsets.md, "The partner's own body, live"), so the game's own double is
// never borrowed. The body is created visible and awake; it is parked (hidden, asleep) so a Double backend can take it.
struct OwnBodyApi {
    std::function<uint64_t(uint64_t porter)> resource;                   // the resident double's resource, 0 if none
    std::function<uint64_t(uint64_t resource,const Transform&)> create;  // 0 when the game refused
    std::function<bool(uint64_t body)> alive;                            // still the body class with the same resource
    std::function<void(uint64_t body)> park;                             // hide and put to sleep
    // Build the visible model: a made body lacks the cover, cover+animation and facial-rig components the game gives
    // its double; the body's ArtPartsData component (entity+180) creates them on request. Optional; true when done.
    std::function<bool(uint64_t body)> dress;
    std::function<void(uint64_t body)> remove;                           // the game's queued removal
    // Optional: the game has dressed the porter (his ArtPartsData holds the full set). Right after a load it has not
    // (live 2026-11-08), and a body made then cannot be dressed; creation waits, at most ReadyWaitTicks.
    std::function<bool(uint64_t porter)> ready;
};
class OwnBody {
public:
    static constexpr unsigned MaxFailures=3;
    static constexpr unsigned ReadyWaitTicks=600;   // about 30 s at the 20 Hz game-thread tick
    explicit OwnBody(OwnBodyApi api,std::function<void(const std::string&)> record):api_(std::move(api)),record_(std::move(record)) {}
    // One call per game-thread tick while own-body mode is wanted. Returns the body to drive (0: none yet, or given
    // up after MaxFailures refusals: the caller falls back to the resident double). A new porter entity means a new
    // world: the old body went with it and a new one is made.
    uint64_t tick(const Anchor& porter);
    void remove();   // leaving own-body mode: the game removes the body
    bool exhausted() const {return failures_>=MaxFailures;}
    uint64_t body() const {return body_;}
private:
    OwnBodyApi api_;std::function<void(const std::string&)> record_;uint64_t porter_=0,body_=0;unsigned failures_=0;unsigned waited_=0;
};
class Double {
public:
    enum class Stage {Off,Parked,Driving,Hidden,Yielded,Failed};
    Double(DoubleApi api,std::function<void(const std::string&)> record):api_(std::move(api)),record_(std::move(record)) {}
    void enable(){if(stage_==Stage::Off)stage_=Stage::Parked;}
    void walk(bool on){walk_=on;}   // use upgrade/downgrade on the next acquire
    void steering(double degrees){steer_=std::isfinite(degrees)?degrees:0;}   // the partner's Turn_Heading while he rides
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
    double speed_=0,steer_=0;uint32_t flags_=0;bool walk_=false,hidEffects_=false;uint64_t original_=0;
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
