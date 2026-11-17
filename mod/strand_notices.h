#pragma once
// What the game is told about this player's builds on the players' own server (README "Strand notices"). The game
// uploads what its porter builds and is answered as always, whatever the server's policy does with it: shared, held for
// him alone (the policy: members only, nobody, his limit), kept for him (his choice, the owner's review, a server that
// shares nothing), or waiting (for his answer, for the owner's review). With `strand_notices` the mod reads his own
// objects' states from the server (GET [/PREFIX]/coop/notices with the notice token the launcher took for him,
// world/README.md "What the game is told") and says in the game's info log, once per object, when something he built
// is not shared and why, naming the server ("Your ladder is not shared on North: only members' builds are shared
// here."), and when it becomes shared later ("Your ladder is now shared on North."). What was there before the game
// started is not told. The lines go through the notices' own queue (notice.h: at most four wait, 2.5 s apart).
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace mod::strand {
// One of his objects as the server lists it: its kind ("ladder"), state (shared, held, kept, waiting, remnant) and why
// (held: build, member, limit; kept: choice, owner, server; waiting: you, owner).
struct Item { std::string id,kind,state,why; };
// offerOld: what applies to his older strand on that server ("yes" or "no"; "" from a server that does not say), which
// `offer_strand` (offer_strand.h) acts on.
struct Answer { std::string name,version;bool full=false;std::vector<Item> items;std::string offerOld; };
constexpr size_t MaxItems=8000,MaxAnswerBytes=2*1024*1024;
// The server's answer; nullopt for anything that is not exactly one (bounded).
std::optional<Answer> parse(const std::string& body);
// One line for `count` objects of one kind of news (state and why; "shared" for one that was not and now is). `kind`
// names the object when there is one; `server` is the server's display name ("" none: "this server").
std::u16string sentence(const std::string& state,const std::string& why,const std::string& kind,size_t count,const std::string& server);
// From the answers to the lines to tell. The first answer is what was there before this game ran: nothing is told.
// After it: an object not seen before that is not shared, and a change of a known object to not shared (but not to
// kept by his own choice: he answered that in the launcher) or to shared. A remnant (his withdrawal) is never told.
// The objects of one kind of news in one answer are one line. Nothing is told twice for the same state.
class Tracker {
public:
    std::vector<std::u16string> take(const Answer&);
    const std::string& since() const {return since_;}   // the version to read from next
    size_t known() const {return seen_.size();}
private:
    bool first_=true;std::string since_;std::map<std::string,std::string> seen_;   // id -> "state/why" as last seen
};
// When to read again after an answer of `status` (0: none came; 200 read: `good`): PollMs while the server answers,
// backing off to BackoffMs after failures, and never again (0) when the token is refused (401, 403).
constexpr uint64_t PollMs=5000,BackoffMs=30000,ConnectMs=2000;
uint64_t delayAfter(int status,bool good,unsigned& failures);
// One HTTP GET to the server over a new TCP connection: at most `ms` to connect and for each read; the answer bounded.
struct Fetched { int status=0;std::string body; };   // status 0: no answer in HTTP form
Fetched get(uint32_t address,uint16_t port,const std::string& target,const std::string& token,unsigned ms);
// The reads on a thread of their own (never the game's): `fetch(target)` is `get` to the server in the mod, a function
// of the test's in a test. Lines and log rows wait until taken.
class Poller {
public:
    using Fetch=std::function<Fetched(const std::string& target)>;
    using Schedule=std::function<uint64_t(int status,bool good,unsigned& failures)>;   // delayAfter, or a test's faster one
    Poller(std::string base,Fetch fetch,Schedule schedule={});   // base: the API path ("/PREFIX" or ""), as serverApiBase (config.h) gives it
    ~Poller();
    std::vector<std::u16string> lines();
    std::vector<std::string> logs();
    struct Counters { unsigned reads=0,answers=0,failures=0,told=0;bool refused=false;size_t known=0; };
    Counters counters() const;
    // The server's last word on his older strand: the last answer's offer_old ("" before one, from a server that does
    // not say, and once the token is refused). Kept while the server is away.
    std::string offerOld() const;
private:
    void run();
    std::string base_;Fetch fetch_;Schedule schedule_;Tracker tracker_;
    mutable std::mutex lock_;std::condition_variable wake_;bool stop_=false;
    std::deque<std::u16string> lines_;std::deque<std::string> logs_;Counters counters_;std::string offerOld_;
    std::thread thread_;
};
}
