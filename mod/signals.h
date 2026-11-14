#pragma once
// The partner is alive and reachable from a distance (game_offsets.md "Shout, scan and vitals for the partner"):
//   - a shout (the game's call-out) and an Odradek scan travel as one-shot events on the ordered event stream (CARGO
//     type 20), each delivered exactly once by the core; the receiving game plays a voice line from the partner body's
//     head (`partner_shout`) and tells a scan in the info log, or tries the game's scanner pulse (`partner_scan`);
//   - the porter's blood, stamina and the "carrying too much / off balance" states travel as three bytes (CARGO type
//     20) only when what would be shown has changed, at most once every VitalsSender::MinGapMs, and are shown after
//     the partner's name on his name tag (`partner_vitals`).
// Everything here but the two native factories is game-free and pinned by signals_test.cpp.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
namespace mod::signals {
constexpr uint8_t EventWireType=20,VitalsWireType=21;
enum class Kind : uint8_t {Shout=1,Scan=2};
ds::CargoEvent encodeEvent(Kind,uint32_t serial);
std::optional<Kind> decodeEvent(const ds::CargoEvent&);   // nullopt when malformed

// Sender: one event per shout (the call going from off to on) and one per scan (the porter's scan counter going up,
// however far), each at most once every MinGapMs and only while connected. The first counter read after a start, a
// world change (the counter unreadable or lower) or a connection only learns: an old scan is never told.
class EventSender {
public:
    static constexpr uint64_t MinGapMs=1000;
    std::optional<ds::CargoEvent> shout(uint64_t now,bool connected,bool callingNow);
    std::optional<ds::CargoEvent> scan(uint64_t now,bool connected,const std::optional<uint32_t>& count);
private:
    bool calling_=false,haveCount_=false,wasConnected_=false;uint32_t count_=0,serial_=0;uint64_t lastShout_=0,lastScan_=0;bool sentShout_=false,sentScan_=false;
};
// Receiver: whatever arrives, at most one of a kind every MinGapMs and PerMinute a minute are shown.
class EventReceiver {
public:
    static constexpr uint64_t MinGapMs=1500;static constexpr unsigned PerMinute=20;
    bool accept(Kind,uint64_t now);
private:
    std::deque<uint64_t> shown_[2];
};

// The porter's scan counter, read-only: the player's state object S = *(entity + 0x54c8), checked by its back pointer
// (*(S + 0x60) == entity) and its scan state (u32 S + 0x4850, 0..8); the counter is the u32 at S + 0x4874, which the
// game's scan state machine (ds.exe 0x282b530) increments when a pulse is fired (0x282c528).
constexpr uint64_t PlayerState=0x54c8,StateOwner=0x60,ScanState=0x4850,ScanCount=0x4874;
std::optional<uint32_t> readScanCount(const discovery::Sampler&,uint64_t porterEntity);

// What a friend wants to see. blood, stamina: percent 0..100, Unknown when not read.
constexpr uint8_t Unknown=255;
struct Vitals { uint8_t blood=Unknown,stamina=Unknown;bool overloaded=false,unsteady=false;
    bool operator==(const Vitals& o) const {return blood==o.blood&&stamina==o.stamina&&overloaded==o.overloaded&&unsteady==o.unsteady;}
    bool operator!=(const Vitals& o) const {return !(*this==o);} };
ds::CargoEvent encodeVitals(const Vitals&,uint32_t serial);
std::optional<Vitals> decodeVitals(const ds::CargoEvent&);   // nullopt when malformed (a percent over 100, unknown bits)
// As read this tick: rates 0..1 (nullopt: not read), and the two states as the porter's animation controls have them.
struct Raw { std::optional<float> blood,stamina;bool overloaded=false,unsteady=false; };
// The local player's status block, read-only (the block the game's GetPlayerInfo export copies from, ds.exe 0x26e9240):
// M = *(image + 0x7bc7568); P = the player in *(M + 0x40)[0 .. i32(M + 0x38)) with u32(P + 0x88) == 0, accepted only when
// *(P + 0x70) is this porter's entity; block B = P + 0x2b0. Two value/maximum pairs of floats: B+0x28 / B+0x2c and
// B+0x30 / B+0x38. Which is blood and which is stamina is NOT established statically (game_offsets.md): the first is
// taken for blood. A pair that is not finite, has no maximum or is over it is not read.
constexpr uint64_t PlayerManagerSlot=0x7bc7568,PlayerInfo=0x2b0;
struct InfoPairs { std::optional<float> first,second; };
InfoPairs readInfo(const discovery::Sampler&,uint64_t imageBase,uint64_t porterEntity);
// Off balance: the porter's Unbalance_Rate control at or over this (its range is 0..1 by its name; unverified live).
constexpr float UnsteadyFrom=0.5f;

// Sender. A percent moves only when the value read is Step or more away from what the partner has (so a value that
// trembles around a boundary sends nothing), a state only after it has lasted HoldMs. The set is sent on every
// connection and then only when it differs from the last one sent, never more often than MinGapMs.
class VitalsSender {
public:
    static constexpr uint64_t MinGapMs=2000,HoldMs=600;static constexpr int Step=5;
    std::optional<ds::CargoEvent> tick(uint64_t now,bool connected,const Raw&);
    const Vitals& shown() const {return want_;}
private:
    struct Held { bool value=false,seen=false;uint64_t since=0;bool step(uint64_t now,bool v); };
    static uint8_t percent(uint8_t have,const std::optional<float>& rate);
    Vitals want_{},sent_{};Held over_,unsteady_;bool wasConnected_=false,haveSent_=false;uint64_t last_=0;uint32_t serial_=0;
};
// Receiver: the partner's last set; nothing once the session is down.
class VitalsReceiver {
public:
    bool feed(const ds::CargoEvent&);   // false: malformed (nothing changes)
    const Vitals* current(bool connected){if(!connected)have_=false;return have_?&now_:nullptr;}
private:
    Vitals now_{};bool have_=false;
};
// The name tag's text: the name, then what is known: "Sam  B90 S60 heavy unsteady" (B blood, S stamina, percent).
std::u16string label(const std::u16string& name,const Vitals*);
// The tag is one of the game's label records and a new text makes a new record: whatever arrives, the text changes
// at most once every GapMs (a new name at once).
class LabelPacer {
public:
    static constexpr uint64_t GapMs=2000;
    const std::u16string& text(uint64_t now,const std::u16string& name,const Vitals*);
private:
    std::u16string name_,text_;uint64_t at_=0;bool have_=false;
};

// --- Native calls (Windows, pinned build; signatures checked; both UNVERIFIED live: off unless asked for). ---
// A voice line from the partner body: the game's own "play this body's sentence" (ds.exe 0x273bb10, what the script
// export DSPlayerEntity_sExportedPlayAkukanVoice calls for the resident double): it picks a sentence at random from
// container `index` of the body's resource (DSPlayerAKukanEntityResource.SentenceResourceContainers) and sends the
// body the engine's sound message on its head joint. Which container holds what is not in the executable.
constexpr unsigned VoiceContainers=16;
struct VoiceApi { std::function<bool(uint64_t body,unsigned index)> play; };   // false: not the body class, or index out of range
VoiceApi nativeVoiceApi(discovery::Sampler&);
// The scanner's pulse is drawn from eight named global render variables that the game's scan writes when it fires
// (0x282c5cb..0x282c6e3) and its renderer reads back every frame (0x32e3d70): OdradekSensorPositionX/Y_n,
// EmitDirectionX/Y_n, AngleRadian_n, Distance_n for a slot n of 0 or 1, OdradekSensorLastIndex and ...LastIndexValid.
// show() writes one slot the same way. What starts the wave's sweep was not found: this may only move a wave that is
// already running.
struct PulseApi { std::function<bool(unsigned slot,const std::array<double,3>& at,float directionX,float directionY,float angle,float distance)> show; };
PulseApi nativePulseApi(discovery::Sampler&);
}
