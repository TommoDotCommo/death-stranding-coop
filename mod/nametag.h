#pragma once
// A name over the partner (game_offsets.md "Finding the marker HUD and a custom label"): one of the game's hologram-name
// records (the labels the game shows over porters' holograms) with the partner's name, kept at a point above the
// partner body. The game's own HUD projects and draws the nearest few records; the mod only registers its record, moves
// its point every tick and unregisters it. The record and its memory are the mod's.
#include "discovery.h"
#include "../core/core.h"
#include <optional>
#include <vector>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
namespace mod::nametag {
struct Api {
    std::function<uint64_t(const std::u16string& name,const std::array<double,3>& at)> create;   // a registered record, or 0
    std::function<void(uint64_t record,const std::array<double,3>& at)> move;
    std::function<void(uint64_t record)> remove;    // unregistered and freed (only from a list still current)
};
class Tag {
public:
    static constexpr unsigned MaxFailures=3;static constexpr size_t MaxName=24;
    Tag(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // permitted=false or no point: the label goes. A new name replaces it.
    void tick(bool permitted,const std::u16string& name,const std::array<double,3>* at);
    void forget(const char* why);   // the world was replaced: the record goes back to remove(), which checks its list
    uint64_t record() const {return record_;}
private:
    Api api_;std::function<void(const std::string&)> log_;uint64_t record_=0;std::u16string name_;unsigned failures_=0;
};
// Several labels, one per body, by key (the mod uses 0 for the partner and a player's id for another player). They share
// one Api: each label is a record of its own, made after its body is there, moved with it every tick, removed when the
// body hides or its key is no longer listed. The game's HUD presents three hologram names at a time, the nearest by
// its own sort, and how many records its list takes has only been seen with one of the mod's: so the set registers at
// most `limit` records at once (LabelsAtOnce by default), key 0 first and then the bodies nearest the viewer. A body
// that has its label keeps it until another is KeepMetres nearer, so two at about the same distance do not swap every
// tick. limit 0 registers one for every shown body (not run live).
constexpr size_t LabelsAtOnce=4;constexpr double KeepMetres=2;
// Where a body's label goes (above its head); nothing while the body is hidden, gone or unreadable.
using Where=std::function<std::optional<std::array<double,3>>(uint64_t body)>;
struct Named {uint64_t key=0,body=0;std::u16string name;};
class Tags {
public:
    Tags(Api api,Where where,std::function<void(const std::string&)> log,size_t limit=LabelsAtOnce):api_(std::move(api)),where_(std::move(where)),log_(std::move(log)),limit_(limit){}
    // One game-thread tick with everybody who should have a label now. permitted=false (a game event, the setting
    // off): every label goes. viewer: where this porter is (the labels' axes), for the choice of the nearest; without
    // it the order listed decides. Log lines of key 0 read "TAG ..", of any other key "TAG player <key> ..".
    void tick(bool permitted,const std::vector<Named>&,const std::array<double,3>* viewer=nullptr);
    void limit(size_t most){limit_=most;}   // the most records at once; 0: no limit
    void forget(const char* why);   // the world was replaced: every record goes back to remove(), which checks its list
    size_t size() const {return tags_.size();}
    size_t shown() const;           // labels that exist in the game now
    uint64_t record(uint64_t key) const {auto it=tags_.find(key);return it==tags_.end()?0:it->second->record();}
private:
    Api api_;Where where_;std::function<void(const std::string&)> log_;size_t limit_;std::map<uint64_t,std::unique_ptr<Tag>> tags_;
};
std::u16string fromUtf8(const std::string&);   // printable characters only, at most Tag::MaxName; invalid bytes dropped
// The name each player chose (config `nickname`) goes to the partner as CARGO events of type 10: up to MaxBytes of
// UTF-8 in chunks of 9 bytes (item: 4 bytes; ref: chunk index (4 bits), chunk total (4 bits), 5 bytes: 48 bits), sent
// on every connection and whenever it changes; the core delivers them in order, exactly once.
constexpr uint8_t WireType=10;constexpr size_t MaxBytes=36;
std::vector<ds::CargoEvent> encode(const std::string& utf8);   // cut at MaxBytes on a character boundary
class Receiver {
public:
    std::optional<std::string> feed(const ds::CargoEvent&);   // the partner's whole name when its last chunk arrives
private:
    std::string pending_;unsigned next_=0,total_=0;
};
class Sender {
public:
    std::vector<ds::CargoEvent> tick(const std::string& name,bool connected);
private:
    std::string sent_;bool wasConnected_=false,haveSent_=false;
};
Api nativeApi(discovery::Sampler&);
}
