#pragma once
// Route challenges on the server (../docs/ROUTES.md, "On the server"): players publish challenges (DSR1 strings) and
// send finished runs with their track (DST1), which is replayed through the mod's own checker (../mod/route.h verify)
// before it counts; one board of best times per challenge. Kept beside the world and saved in the same file
// ("challenges"); the launcher's API reaches it through the Front (front.h `challenges`):
//   GET  [/PREFIX]/coop/challenges                                         every challenge with its board
//   POST [/PREFIX]/coop/challenge-publish {"player":KEY,"route":"DSR1-...","name":TEXT}
//   POST [/PREFIX]/coop/challenge-result  {"player":KEY,"challenge":ID,"track":"DST1-...","name":TEXT}
//   POST [/PREFIX]/coop/challenge-remove  {"player":KEY,"challenge":ID}    its author, or the owner's signed call
// The same rules as every launcher call decide who may act for KEY (api.h); the policy's `challenges` who may publish.
#include "world.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace world {
constexpr size_t MaxChallenges=200,MaxChallengesPerPlayer=20,MaxBoard=100,BoardShown=10,MaxChallengeBody=16*1024;
class Challenges {
public:
    struct Run { std::string account,name;int64_t ms=0,when=0; };
    struct Entry { std::string id,route,account,name;uint32_t crc=0;int64_t published=0,runs=0;std::vector<Run> board; };   // board: best first
    // Each throws std::runtime_error with the reason when refused, and then changed nothing.
    Entry publish(const std::string& account,const std::string& name,const std::string& route,int64_t now);
    // A finished run: verified against the challenge before it counts. Returns the player's place (1 = best).
    size_t result(const std::string& account,const std::string& name,const std::string& id,const std::string& track,int64_t now);
    void remove(const std::string& id);
    bool has(const std::string& id) const;
    std::string author(const std::string& id) const;     // the publisher's account ("" for none)
    json::Value list() const;                            // what GET /coop/challenges answers
    json::Value snapshot() const;
    void restore(const json::Value&);                    // throws for a malformed list
    bool dirty() const {std::lock_guard<std::mutex> g(lock_);return dirty_;}
    void saved(){std::lock_guard<std::mutex> g(lock_);dirty_=false;}
    size_t size() const {std::lock_guard<std::mutex> g(lock_);return entries_.size();}
private:
    mutable std::mutex lock_;std::map<std::string,Entry> entries_;uint64_t next_=1;bool dirty_=false;
};
// The API calls above, after the Front checked any signature (`signer`: the verified account, "" unsigned).
Reply challengesApi(World&,Challenges&,const std::string& method,const std::string& call,const std::string& body,const std::string& signer,int64_t now);
bool challengeCall(const std::string& call);            // is this /coop/ call one of the above
std::string displayName(const std::string&);            // a board name: printable, at most 24 characters ("" refused)
}
