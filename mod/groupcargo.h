#pragma once
// Cargo shared in group play, through the server (policy `cargo`; ../world/README.md, "Cargo set down for the others").
// The same detection as two-player cargo (cargo.h Sync: what this porter set down, picked up, or saw vanish near him),
// with the server instead of the partner: a set-down is told to `…/coop/cargo` (op place) and gets the server's id
// back; a pick-up of a crate the server knows is told (op take: the first taker wins); and the crates the others set
// down are read every few seconds and made in this game near the porter (CreateBaggage), the ones taken since removed
// (DeleteBaggage). Messages and reads go through one thread of their own (Courier), so the game thread never waits.
//
// Game-free here: the answer, the plan (which local id is which server id, what to tell, what to make or remove), the
// courier with a stand-in for HTTP.
#include "cargo.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace mod::groupcargo {
constexpr uint64_t WaitMinMs=1000,WaitMaxMs=60000,FailWaitMs=10000;
constexpr double NearMetres=1500;      // others' crates farther than this from the porter wait
constexpr unsigned MakesPerStep=8;     // at most this many made in one step
struct Crate { std::string id; uint32_t hash=0; std::array<double,3> at{}; double yaw=0; int64_t area=0; };
struct Answer { bool read=false,full=false,on=true,more=false; std::string version; uint64_t waitMs=3000;
                std::vector<Crate> crates; std::vector<std::string> removed; };
Answer parse(const std::string& body);
uint64_t delayAfter(const Answer&,unsigned& failures);
// One message to the server: a set-down (place, with the local id it answers for) or a pick-up (take).
struct Say { bool take=false; uint64_t local=0; std::string id; uint32_t hash=0; std::array<double,3> at{}; int64_t area=0; };
std::string body(const Say&);   // the JSON the server takes
class Plan {
public:
    explicit Plan(uint64_t aliasBase):nextAlias_(aliasBase){}
    // This porter set something down (its Sync id), or picked up / saw vanish a shared item (Sync id).
    std::vector<Say> placed(const cargo::Placement&,int64_t area);
    std::vector<Say> taken(const cargo::Taken&);
    // The server answered a set-down: its id, or "" when refused (the item then stays this porter's alone).
    std::vector<Say> answered(uint64_t local,const std::string& id);
    void take(const Answer&);
    // What to do in the game now: others' crates to make (as Placements with a local alias id, for Sync::expect and
    // CreateBaggage), and local ids whose items the others took (for Sync::partnerTook and DeleteBaggage).
    struct Step { std::vector<cargo::Placement> make; std::vector<uint64_t> gone; };
    Step step(const std::array<double,3>& porter,bool porterKnown);
    size_t known() const {return byServer_.size();}
private:
    uint64_t nextAlias_;
    std::map<std::string,uint64_t> byServer_;std::map<uint64_t,std::string> byLocal_;   // crates made here or set down here
    std::map<std::string,Crate> waiting_;                                              // others' crates not made yet
    std::set<uint64_t> inFlight_,takeWhenAnswered_,aliases_;std::vector<uint64_t> gone_;
};
// The one thread that talks to the server: it sends the messages in order, then reads (since the last version) at the
// server's pace. Answers to set-downs and the reads are kept for the game thread.
struct Result { int status=0; std::string body; };
class Courier {
public:
    using Get=std::function<Result(const std::string& since)>;
    using Post=std::function<Result(const std::string& body)>;
    Courier(Get,Post,std::function<void(const std::string&)> log);
    ~Courier();
    void send(std::vector<Say>);
    struct Answered { uint64_t local=0; std::string id; };
    std::vector<Answered> answered();
    std::vector<Answer> reads();
    uint64_t sent() const {return sent_.load(std::memory_order_relaxed);}
private:
    void run();
    Get get_;Post post_;std::function<void(const std::string&)> log_;
    std::deque<Say> out_;std::vector<Answered> answered_;std::vector<Answer> reads_;
    std::atomic<uint64_t> sent_{0};std::mutex lock_;std::condition_variable wake_;bool stop_=false;std::thread thread_;
};
}
