#pragma once
// A world that is the server's (config `server_world`, the server's id; the launcher sets it from the server's rule
// `world`): structures that did not come from this server stay out of the world while the player is here. They are not
// removed: the game's records stay as they are, so the save still holds them and writes them whole. A record is refused at
// the one test every record passes to leave the game's Inactive state (0x2c35fb0; game_offsets.md, "Keeping structures out
// of the world"), so it never gets a body, a collision shape or a map marker. The game's own places (delivery terminals,
// road rebuilders) have owner 0 and are never touched.
//
// What stands: a structure with an id this server gave (../world/idtag.h), one with an id the server lists
// (GET .../coop/world-ids, with the notice token), the mod's copies of another player's structures (owner Net, no id), and
// what the player builds in this session (owner Player, no id yet, a record number newer than the save's).
//
// The save keeps only what is the player's: a structure the server handed (owner Net with an id of this server or one it
// lists) is left out of every save written here (0x2ccec10 skips a record with bit 8 of +0xad, which is set on those
// records only while it runs; game_offsets.md). The player's own records, and all records of the game, are saved whole.
#include "discovery.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace mod::serverworld {
constexpr uint32_t Gate=0x2c35fb0,GateStub=0x2c362f0,LoadSite=0x2e11b75,Load=0x2c9bc70,LoadStub=0x2c35420;
inline constexpr std::array<uint32_t,6> GateSites={0x2c65993,0x2c8653b,0x2c87fee,0x2c88142,0x2c8a8b1,0x2c923d1};
constexpr uint32_t SaveSite=0x2ccef1f,Save=0x2ccec10,SaveStub=0x2c92480;   // the one call of the records' save writer
constexpr size_t ObjectsCountAt=0x56a258,ObjectsAt=0x56a260,SkipAt=0xad;constexpr uint8_t SkipBit=8;   // the manager's records; the writer's skip bit
constexpr uint32_t ManagerSlot=0x7bf4960,OwnCounterAt=0x572d34;   // the manager; its count of the player's own records (saved)
constexpr size_t OwnerAt=0xb2,OnlineIdAt=0xb8,RecordIdAt=0x30;    // in a construction object
constexpr uint32_t OwnIdsFrom=10000;                              // the player's own records are numbered from here
constexpr uint8_t OwnerGame=0,OwnerPlayer=1,OwnerNet=2;
constexpr uint64_t RetryMs=5000,SlowRetryMs=30000,MinWaitMs=5000,MaxWaitMs=600000;
struct Answer {bool read=false;std::string server,world;uint64_t waitMs=0;std::vector<std::string> untagged;};
Answer parse(const std::string& body);   // never throws
// Decides for one record, from any thread, without a lock and without allocating.
class Filter {
public:
    explicit Filter(std::string key):key_(std::move(key)) {}
    Filter(const Filter&)=delete;
    void publish(std::vector<std::string> untagged);     // the ids the server lists besides its own; one writer
    void loadStarts();                                   // no record without an id is this session's while a save loads
    void loadEnded(uint32_t ownCounter);                 // what is numbered after this was built after the load
    bool allows(uint8_t owner,const char* id,uint32_t idChars,uint32_t recordId) const;
    bool allowsObject(const uint8_t* object) const;      // the same, read from a construction object
    bool fromServer(uint8_t owner,const char* id,uint32_t idChars) const;   // handed by the server: left out of the save
    bool fromServerObject(const uint8_t* object) const;
    size_t listed() const;
private:
    struct List {std::string ids;size_t count=0;};       // sorted, each idtag::IdChars long
    bool listedHas(const char* id) const;
    std::string key_;
    std::atomic<const List*> list_{nullptr};
    std::array<std::unique_ptr<const List>,4> kept_;size_t next_=0;   // old lists live on for a while: a reader may be in one
    std::atomic<uint32_t> newAfter_{0};                  // before any load: whatever the player builds is new
};
enum class State {On,Differs,Failed};
const char* label(State);
using Read=std::function<bool(uint64_t address,uint8_t* out,size_t size)>;
using Write=std::function<bool(uint64_t address,const uint8_t* bytes,size_t size)>;
// Every place is checked first and nothing is written unless all are as expected; a write that fails puts back what was
// written. gateHook, loadHook and saveHook are the absolute addresses the three stubs jump to.
State apply(uint64_t base,uint64_t gateHook,uint64_t loadHook,uint64_t saveHook,const Read&,const Write&);
struct Result {int status=0;std::string body;};
using Get=std::function<Result()>;
// Reads the list on a thread of its own, at the pace the server asks for.
class Link {
public:
    using Schedule=std::function<void(uint64_t)>;
    Link(Get,Filter&,std::string key,std::function<void(const std::string&)> log);
    ~Link();
    uint64_t reads() const {return reads_.load(std::memory_order_relaxed);}
private:
    void run();
    Get get_;Filter& filter_;std::string key_;std::function<void(const std::string&)> log_;
    std::atomic<uint64_t> reads_{0};std::mutex lock_;std::condition_variable wake_;bool stop_=false;std::thread thread_;
};
uint64_t delayAfter(const Answer&,const std::string& key,unsigned& failures);   // how long until the next read
struct Stats {uint64_t asked=0,refused=0,loads=0,saves=0,leftOut=0;};   // leftOut: records left out of the last save
void install(discovery::Sampler&,Filter&);   // throws with the reason if the game's code is not as expected
Stats stats();
}
