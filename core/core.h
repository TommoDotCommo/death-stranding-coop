#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <algorithm>
#include <vector>
#include "build_id.h"

namespace ds {
constexpr size_t WireSize = 128;
constexpr uint32_t GameBuild = 13300582;
using Bytes = std::array<uint8_t,WireSize>;
enum class Kind : uint8_t { Hello=1, Ack, Presence, Ping, Pong, Bye, Cargo, CargoAck, Environment=9, Animation=10 };
enum class Role : uint8_t { Host=1, Client=2 };
enum class Error { None, Length, Magic, Version, Checksum, Reserved, Kind, Role, Build, Session, Nonce, Range, Duplicate, Time, Speed, Teleport, NotConnected, Progress };
const char* reason(Error);
enum Flag : uint16_t { Sprint=1, Crouch=2, Cargo=4, Vehicle=8, PrivateRoom=16, Teleport=32, ProgressMismatch=64,
    Down=128,     // the porter is dead or collapsed (mod/vitals.h)
    Caught=256,   // the porter is being dragged or held by BTs
    Call=512,     // the porter is calling out (the shout key)
    Passenger=1024,     // the porter rides along on the partner's truck (mod/passenger.h)
    Rest=2048 };        // the porter sits down to rest (his own Is_TakeABreak)
constexpr uint16_t FlagMask=4095;
struct State {
    std::array<double,3> position{}, velocity{}; // metres, metres/second; Y is up in this adapter convention
    double yaw=0, phase=0; // radians [-pi,pi], normalised [0,1]
    uint16_t flags=0;
    uint8_t action=0, health=255, stamina=255;
    // Riding (Presence only): the vehicle the porter sits on. 0 on foot, else VehicleKind; flag Vehicle is set exactly
    // when it is nonzero. vehicleOffset = vehicle origin minus position (adapter axes, metres); vehicleAngles = the
    // vehicle's yaw, pitch and roll in radians (yaw like State::yaw; pitch nose up; roll right side down).
    uint8_t vehicle=0;
    std::array<double,3> vehicleOffset{}, vehicleAngles{};
    double steer=0; // the rider's Turn_Heading animation control (degrees, |x| <= SteerMax); 0 on foot
    uint8_t vehicleVariant=0; uint16_t vehicleLook=0; // its variant (< 64) and paint word (vehicle +0x4c8, +0x664); 0 on foot
};
enum VehicleKind : uint8_t { OnFoot=0, Motorbike=1, Truck=2, Car=3, VehicleKindMax=3 };
constexpr double VehicleOffsetMax=20; // metres between the rider and the vehicle origin
constexpr double SteerMax=180;
constexpr uint8_t VehicleVariantMax=63;
bool vehicleValid(const State&);
// Local inputs: reject nonfinite transforms; clamp finite values to the wire envelope.
bool sanitise(State&);
constexpr uint32_t UnknownProgress=0xffffffffu;
struct Progress { uint32_t episode=UnknownProgress, id=UnknownProgress; };
enum class ProgressPolicy : uint8_t { Warn=0, Refuse=1 };
enum class ProgressStatus { Unknown, Equal, Mismatch };
ProgressStatus compareProgress(Progress,Progress);
const char* progressReason(ProgressStatus);
// Optional animation sample (ANIMATION, the only longer packet): the porter's animation controls by the mod's own
// numbering, on the presence timeline. held = each bool's value when sampled; rose = it went from off to on at some
// frame since the previous sample (a one-frame pulse shows only here); floats are sent sparse (zero is left out).
// hands: what the porter holds in his hands, each relative to his root (millimetres; rotation packed by the adapter in
// 48 bits), so that it moves with the mirrored arms.
// joints: while the porter drives, his vehicle's wheel joints (and their parents) as his game posed them: joint index,
// local rotation (quaternion, int16 / 32767) and translation (millimetres), for a model of `skeleton` joints.
// After the 128-byte header: [u8 n][held n][rose n][u8 m][m x (u8 index, f32)][u8 k][k x (u32 item, u8 tag, i16 x3,
// u48 rotation)][u8 j][if j: u16 skeleton][j x (u16 joint, i16 x4 rotation, i16 x3 millimetres)], indices increasing.
// A datagram longer than AnimationWireMax (a 1280-byte path MTU) is refused; the sender leaves the controls out while
// it sends joints (the receiver does not mirror controls on a vehicle).
constexpr size_t AnimationBoolBytes=32, AnimationFloats=192, AnimationHands=2, AnimationHandBytes=17, AnimationJoints=24, AnimationJointBytes=16;
constexpr float AnimationFloatMax=1e6f;
constexpr int16_t AnimationHandMaxMm=3000;
constexpr size_t AnimationWireMax=1252;
struct VehicleJoint {
    uint16_t index=0; std::array<int16_t,4> rotation{}; std::array<int16_t,3> position{};
    bool operator==(const VehicleJoint& o) const { return index==o.index&&rotation==o.rotation&&position==o.position; }
};
struct HeldCase {
    uint32_t item=0; uint8_t tag=0; std::array<int16_t,3> position{}; uint64_t rotation=0;   // tag: adapter-defined
    bool operator==(const HeldCase& o) const { return item==o.item&&tag==o.tag&&position==o.position&&rotation==o.rotation; }
};
struct Animation {
    std::array<uint8_t,AnimationBoolBytes> held{}, rose{}; std::array<float,AnimationFloats> floats{};
    std::array<HeldCase,AnimationHands> hands{}; uint8_t handCount=0;
    std::array<VehicleJoint,AnimationJoints> joints{}; uint8_t jointCount=0; uint16_t skeleton=0;
    bool operator==(const Animation& o) const { return held==o.held&&rose==o.rose&&floats==o.floats&&handCount==o.handCount&&hands==o.hands
        &&jointCount==o.jointCount&&skeleton==o.skeleton&&joints==o.joints; }
    bool operator!=(const Animation& o) const { return !(*this==o); }
};
// The sample shown now (the presence delay applies), with rose gathered over every sample passed since the last take.
// next/fraction: the sample after it and how far the shown time is towards it (0..1), when one has arrived.
struct AnimationShown { Animation animation; uint32_t sequence=0; bool fresh=false; bool between=false; Animation next{}; float fraction=0; };
struct Packet {
    Kind kind=Kind::Hello; Role role=Role::Host;
    uint32_t game=GameBuild; std::array<uint8_t,16> mod=ModBuild;
    uint64_t session=1, nonce=1, echo=0, stamp=0, t0=0, t1=0, t2=0;
    uint32_t sequence=0; State state{};
    uint8_t byeReason=0;
    Progress progress{}; ProgressPolicy progressPolicy=ProgressPolicy::Warn; // HELLO/ACK only
    Animation animation{}; // ANIMATION only
};
// A shared-world cargo event (adapter axes, like State). Delivered reliably and in order, at most once per connection:
// the sender repeats the oldest unacknowledged event, the receiver accepts only the next sequence and acknowledges
// it and any repeat. item is the game's cargo item hash; type 1 = cargo placed in the world, 2 = shared cargo taken
// (picked up or gone): the partner removes its copy; 3 = a structure built, 4 = a structure dismantled, 5 = a ping
// (a map marker for the partner; ref = the ping's id) (mod-defined); 6 = one entry of what the porter carries (mod-defined). ref (t2, below 2^53) names the shared cargo across both worlds.
// id is a stable nonzero identity chosen by the sender; the receiver delivers each id once for the lifetime of the
// Core, so an event resent after a reconnection (its acknowledgement lost) is not delivered twice.
// CARGO event types 1..CargoTypeMax (2 pick-up, 3 structure, 4 dismantle, 5 ping, 6 carry summary, 7 gear, 8 outfit,
// 9 like, 10 name, 11 an enemy went down, 12 an order changed, 13 BT news, 14 road news, 15 a structure reached a level, 16 where a carried case sits, 17 a voidout crater); a receiver refuses any other type as out of range.
constexpr uint8_t CargoTypeMax=17;
struct CargoEvent { uint32_t sequence=0; uint8_t type=1; uint32_t item=0; std::array<double,3> position{}; double yaw=0; uint64_t id=0, ref=0; };
// Optional host-only sample, independent of presence/cargo and the handshake.
// Milliseconds since midnight, not an elapsed world/forecast clock. Weather is unknown.
constexpr uint32_t EnvironmentDayMs=86400000;
// rain (optional): the host's native weather-map condition where it stands (4*z-2 of the game's rain map: lower is
// wetter; about < -0.5 rain, < -1.5 heavy rain, > 0.5 clear), carried in t1 as bit 32 plus int16 thousandths.
constexpr float EnvironmentRainMax=2.5f;
struct Environment { uint32_t timeOfDayMs=0; bool haveRain=false; float rain=0; };
// Mutants exist only in test builds (-DDS_TESTING).
struct Protection {
    bool version=true, range=true, teleport=true, duplicate=true, delay=true, session=true, build=true;
    bool magic=true, checksum=true, reserved=true, nonce=true, time=true, speed=true;
    bool length=true, kind=true, role=true, handshake=true, ping=true, clamp=true, finite=true;
    bool pingRetry=true, pingHistory=true, cargoOrder=true, cargoAck=true, cargoOnce=true;
    bool progressReady=true, progress=true, progressUnknown=true, progressFlag=true, progressPolicy=true;
    bool vehicle=true, vehicleLerp=true;
    bool environmentRange=true, environmentRole=true, environmentOrder=true, environmentFresh=true, environmentRate=true;
    bool animationRange=true, animationOrder=true, animationRose=true;
    bool floor=true, extrapolate=true, stale=true, gate=true, timeout=true, buffer=true, rate=true;
    bool adapt=true, refine=true;
};
Bytes encode(Packet); // throws invalid_argument for nonfinite state
std::vector<uint8_t> encodeAnimation(const Packet&); // throws invalid_argument for a nonfinite or out-of-range control
// The payload alone (what follows the header), for recordings: the same bytes and checks.
std::vector<uint8_t> encodeAnimationPayload(const Animation&);
bool decodeAnimationPayload(const uint8_t*,size_t,Animation&);
Error decode(const uint8_t*, size_t, Packet&);
#ifdef DS_TESTING
Error decodeForTest(const uint8_t*, size_t, Packet&, Protection);
bool sanitiseForTest(State&, Protection);
#endif
struct Snapshot { State state; uint64_t time=0; uint32_t sequence=0; };
struct Render { State state; bool visible=false, stale=true, snapped=false, late=false; uint32_t delayMs=0; };   // late: shown past the newest sample (nothing to interpolate to)
// Pure frame sampler. No interpolation across teleport, private room, any quantised Y change or >8 m gap.
Render sample(const std::deque<Snapshot>&, uint64_t now, uint32_t delay=120, uint32_t extrapolate=150);
struct StateProvider {
    virtual ~StateProvider()=default;
    virtual bool readLocal(State&)=0;
    virtual Progress readProgress() const { return {}; }
    virtual bool readyForSession() const { return true; }
    // Timestamp of the pose returned by readLocal, in the caller clock domain.
    virtual uint64_t localSampleTime(uint64_t now) const { return now; }
    virtual bool isInCutsceneOrLoading() const=0;
    virtual void applyRemote(const Render&)=0;
};
// One configured peer only. Adapter must filter source endpoint before invoking receive.
struct Transport { virtual ~Transport()=default; virtual void send(const Bytes&)=0;
    virtual void sendLong(const std::vector<uint8_t>&) {} }; // ANIMATION; a transport without it sends none
// Presentation delay that follows the link: the 95th percentile of how late the last DelayWindow presence samples
// arrived (against their own mapped time) plus DelayMarginMs (two send intervals: one lost packet is bridged by
// interpolation), never under Config::delayMs, never over DelayMaxMs, moved by at most DelaySlew of elapsed time.
constexpr uint32_t DelayMaxMs=500, DelayMarginMs=100; constexpr size_t DelayWindow=64, DelayWarmup=8; constexpr double DelaySlew=0.1;
// Clock mapping: the first answered ping sets it; afterwards it moves toward the offset implied by the quickest of the
// last ClockWindow round trips (the least lopsided one), by at most ClockSlew of elapsed time, so buffered samples keep
// their order and playback speed changes by one percent at most.
constexpr size_t ClockWindow=16; constexpr double ClockSlew=0.01;
struct Config { Role role=Role::Host; uint64_t session=1, nonce=1; uint32_t delayMs=120; ProgressPolicy progressPolicy=ProgressPolicy::Warn; };
uint16_t portForSlot(unsigned slot); // throws if slot outside 0..15
class Core {
public:
    Core(Config, StateProvider&, Transport&);
#ifdef DS_TESTING
    void setProtections(Protection p) { protections_=p; }
#endif
    void tick(uint64_t now); // caller-supplied monotonic ms; serialize all calls, including receive/frame
    Error receive(const uint8_t*,size_t,uint64_t now);
    void frame(uint64_t now);
    void disconnect(uint64_t now);
    bool connected() const { return connected_; }
    ProgressStatus progressStatus() const { return progressStatus_; }
    Progress localProgress() const { return localProgress_; }
    Progress peerProgress() const { return peerProgress_; }
    uint64_t connectionSerial() const { return connectionSerial_; }
    // Freshest accepted sample without presentation delay/extrapolation, for decisions.
    Render latestPresence(uint64_t now) const;
    Error lastError() const { return error_; }
    size_t buffered() const { return snapshots_.size(); }
    uint64_t accepted() const { return accepted_; }
    double rttMs() const { return rtt_; }
    double clockOffsetMs() const { return offset_; } // remote minus local
    Render render(uint64_t now) const;
    // How far behind the partner's own time the partner is shown right now. It starts at Config::delayMs (the least)
    // and follows the link: late packets (latency, jitter, a clock mapping that is off) raise it up to DelayMaxMs,
    // slowly enough that the change does not show.
    uint32_t presentationDelayMs() const { return uint32_t(delay_+.5); }
    void queueCargo(CargoEvent);              // sequence is assigned here
    std::vector<CargoEvent> takeCargo();      // received events, each exactly once, in order
    size_t cargoPending() const { return cargoOut_.size(); }
    bool sendEnvironment(Environment,uint64_t now); // connected host only, at most once per 30 seconds
    std::optional<Environment> latestEnvironment(uint64_t now) const; // expires after 2 s; cleared on reconnect
    // Connected only, at most every 50 ms, an unchanged sample at most every 500 ms; true when it was sent (the caller
    // then starts gathering rose again).
    bool sendAnimation(const Animation&,uint64_t now);
    // The sample due now on the presence timeline; nothing when none is due or the one due is over a second old.
    std::optional<AnimationShown> takeAnimation(uint64_t now);
private:
    void send(Kind,uint64_t);
    void reset(bool rotateNonce);
    Config config_; StateProvider& provider_; Transport& transport_; Protection protections_;
    std::deque<Snapshot> snapshots_;
    std::deque<uint64_t> pendingPings_; // bounded outstanding probes; retries do not invalidate a late pong
    bool connected_=false, sentHello_=false, haveSequence_=false, haveTeleport_=false, haveSent_=false, clockValid_=false;
    uint64_t peer_=0, lastReceive_=0, nextHello_=0, nextSend_=0, nextPing_=0, lastTeleport_=0, lastStamp_=0, lastArrival_=0, accepted_=0;
    uint64_t lastLocalSample_=0;
    uint32_t outgoing_=0, incoming_=0;
    double rtt_=-1, offset_=0;
    std::deque<int32_t> lateness_; double delay_=0; uint64_t delayAt_=0;
    std::deque<std::pair<double,double>> pongs_; uint64_t offsetAt_=0;   // (round trip, offset it implies), newest last
    void adaptDelay(uint64_t now);
    State sentState_{};
    std::deque<CargoEvent> cargoOut_; std::vector<CargoEvent> cargoIn_;
    uint32_t cargoNext_=0, cargoSeen_=0; uint64_t nextCargo_=0, cargoIds_=0;
    std::deque<uint64_t> deliveredIds_;   // bounded memory of delivered event ids
    void sendCargo(const CargoEvent&,uint64_t now);
    Progress localProgress_{},peerProgress_{};
    ProgressStatus progressStatus_=ProgressStatus::Unknown;
    uint64_t connectionSerial_=0;
    Environment environment_{}; bool haveEnvironment_=false, sentEnvironment_=false;
    uint32_t environmentOut_=0, environmentIn_=0;
    uint64_t environmentReceived_=0, environmentStamp_=0, environmentSent_=0;
    struct AnimationSample { Animation animation; uint64_t time=0; uint32_t sequence=0; };
    std::deque<AnimationSample> animations_; Animation sentAnimation_{}; bool sentAnimationOnce_=false;
    uint32_t animationOut_=0, animationIn_=0, animationTaken_=0; uint64_t animationSent_=0, animationStamp_=0; bool haveAnimation_=false;
    Error error_=Error::None;
};
}
