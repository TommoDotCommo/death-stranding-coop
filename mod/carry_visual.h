#pragma once
// What the partner carries (opt-in, config key `carry_visual`, off by default).
//
// Read side (read-only, static layout of build 13300582, game_offsets.md "Carried cargo"; not yet confirmed live):
//   porter entity (DSPlayerEntity) +54d0 -> its DSBaggage2CarrierComponent (vtable image+3dd9750)
//   component +50 -> the carrier: +10 slot count (i32), +18 slot pointer array
//   slot: +1 slot type (EDSBaggage2SlotType: 1 BackPack, 2/3 left/right arm, 4/5 waist, 6/7 left/right hand ...),
//         +18 stack count (i32), +20 stack pointer array
//   stack: +10 record count (i32), +18 pointer array to DSBaggage2 records (bottom first is assumed, unconfirmed)
//   record: one of the manager's inline records (manager+20+index*240); +c8 item hash, +20 kind, +90 the carrier
//   that holds it (checked here: a record that names another carrier is skipped).
// The game walks the same chain itself (DSPlayerSystem DoesHaveBaggage 26e8610 -> 2c532f0 -> 2c53340 -> 2c53640).
//
// Show side: only a summary travels (item, slot, stack column and level, count; at most MaxEntries entries) as
// CARGO events of type 6, and the receiver keeps the latest complete summary. It never creates or moves a cargo
// record: a record held by the partner body would be the game's real cargo (saved, counted by the game's
// "does the porter have this item" checks, which walk every carrier). Drawing the summary on the partner body is
// left to a Sink; the mod's sink only logs until a purely visual attachment is proven live.
#include "discovery.h"
#include "../core/core.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace mod::carry {
constexpr uint64_t PorterCarrier=0x54d0, CarrierObject=0x50, Slots=0x10, SlotArray=0x18, SlotType=0x1,
                   Stacks=0x18, StackArray=0x20, Items=0x10, ItemArray=0x18,
                   RecordBase=0x20, RecordStride=0x240, RecordHash=0xc8, RecordKind=0x20, RecordHolder=0x90,   // see SlotCarrier
                   RecordItem=0x28, ItemId=0x34;   // the record's list item and its id: used when the record's own id is 0
constexpr uint64_t ComponentVtable=0x3dd9750;   // image offset of DSBaggage2CarrierComponent's vtable
// The game scans 0x1e98 records (0x2c55180); live 2026-11-09 the porter's stack used records up to index 557.
constexpr unsigned RecordCount=0x1e98;
// A record's holder (+0x90) is its SLOT, whose +0x28 is the carrier (native append 0x2c2b530; live 2026-11-09).
constexpr uint64_t SlotCarrier=0x28;
// Record +204 bit 5: the case's variant (its darker model and look; 0x2c488c0 passes it to the model selectors). Live
// 2026-11-09 every carried record of the Episode 3 save had it.
constexpr uint64_t RecordFlags=0x204;constexpr uint32_t RecordVariant=0x20;
constexpr size_t MaxSlots=48, MaxStacks=16, MaxLevels=32, MaxItems=64, MaxEntries=16;
constexpr uint8_t WireType=6;
// Slots that hold cargo on the body (EDSBaggage2SlotType 1..9); gear slots (weapons, shoes, suit, gloves, mask) and
// storage slots are not cargo on the porter.
inline bool cargoSlot(uint8_t s){
#ifndef MUTANT_CARRY_SLOT
    return s>=1&&s<=9;
#else
    return s!=0;
#endif
}
const char* slotName(uint8_t);
struct Item { uint32_t hash=0,index=0; uint8_t slot=0,column=0,level=0,kind=0; bool variant=false; };
// One run of identical items in one stack: where it starts and how many.
struct Entry { uint32_t hash=0; uint8_t slot=0,column=0,level=0,count=1; bool variant=false;
               bool operator==(const Entry& o) const {return hash==o.hash&&slot==o.slot&&column==o.column&&level==o.level&&count==o.count&&variant==o.variant;}
               bool operator!=(const Entry& o) const {return !(*this==o);} };
using Summary=std::vector<Entry>;
// Every carried item, in slot, stack and level order. nullopt when the chain is not readable or not what it should
// be (wrong vtable, counts out of range): the caller keeps what it had.
std::optional<std::vector<Item>> readCarried(const discovery::Sampler&,uint64_t porterEntity,uint64_t componentVtable,uint64_t manager);
Summary summarise(const std::vector<Item>&);   // cargo slots only; runs grouped; at most MaxEntries entries
std::string describe(const Summary&);
// Wire: one type-6 CARGO event per entry (an empty summary is one event with total 0). item = hash; ref packs
// index (4 bits), total (5), slot (7), column (4), level (6), count (8), the summary's generation (16) and the
// variant (1): 51 bits.
std::vector<ds::CargoEvent> encode(const Summary&,uint16_t generation);
// Receiver: entries of one summary arrive in order (the core delivers in order, exactly once); the latest complete
// summary replaces the previous one. A broken run (a lost reconnect, a restarted sender) is dropped.
class Receiver {
public:
    std::optional<Summary> feed(const ds::CargoEvent&);
    void reset(){pending_.clear();active_=false;}
private:
    Summary pending_; bool active_=false; uint16_t generation_=0; uint8_t total_=0;
};
// Sender: sends the summary once it has stayed the same for Stable ms (the cargo menu stages items for a moment),
// at most once per MinGap ms, and again on every new connection (the partner may have restarted).
class Sender {
public:
    static constexpr uint64_t Stable=500, MinGap=1000;
    std::vector<ds::CargoEvent> observe(const std::optional<Summary>& read,bool connected,uint64_t now);
    const Summary& sent() const {return sent_;}
private:
    Summary candidate_,sent_; uint64_t since_=0,lastSend_=0; bool haveCandidate_=false,haveSent_=false,wasConnected_=false,resend_=false;
    uint16_t generation_=0;
};
// Where a summary is shown. attach: draw these items on the partner body (purely visual). hold: make a real cargo
// record held by the partner body; never used by View (kept so tests can prove it stays unused).
struct Sink {
    virtual ~Sink()=default;
    virtual void attach(const Summary&)=0;
    virtual void detach()=0;
    virtual void hold(uint32_t hash,uint8_t slot)=0;
};
class View {
public:
    void partner(const Summary&,Sink&);   // a complete summary from the partner
    void lost(Sink&);                     // the partner disconnected or is hidden
    const Summary& shown() const {return shown_;}
private:
    Summary shown_; bool attached_=false;
};
// One side of the exchange, as the mod and coop-partner run it: what this porter carries goes out, the partner's
// summary is shown. incoming() takes a received CARGO event and returns false for any other type (left to the cargo
// code). tick() returns the events to queue; when the connection drops, the partner's items are taken off.
class Exchange {
public:
    bool incoming(const ds::CargoEvent&,Sink&);
    std::vector<ds::CargoEvent> tick(const std::optional<Summary>& read,bool connected,uint64_t now,Sink&);
    const View& view() const {return view_;}
    const Sender& sender() const {return sender_;}
private:
    Sender sender_; Receiver receiver_; View view_; bool connected_=false;
};
// coop-partner --carry: "hash[:slot[:count]],..." (slot defaults to 1 BackPack, count to 1); "none" is empty.
std::optional<Summary> parseSpec(const std::string&);
}
