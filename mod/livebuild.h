#pragma once
// What other players build, at once (opt-in with `build_received` in group play; ../world/README.md, "What others build,
// at once"). The game asks the server for other players' structures only when it loads an area, and only for some kinds,
// so what another player builds while this one plays would appear at his next load of that area, or never (a
// generator). The mod reads `…/coop/built` instead, every few seconds, with the notice token, and asks the game's own
// construction queue for each structure it lacks: the Net variant, at the server's place, with the server's online id,
// made without the range, placement and area checks as received structures are (mod/received.h). Removed ones are taken
// down (Net only). Under a world that is the server's, the copies carry this server's id, so they stand, and the save
// leaves them out; the plan builds them again after a load.
//
// Game-free here: the answer, the kinds, the rotation, and the plan (what to build, what to take down, when to give up).
#include "structures.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <set>
#include <string>
#include <vector>
namespace mod::livebuild {
constexpr uint64_t WaitMinMs=1000,WaitMaxMs=60000,FailWaitMs=10000;   // the server's pace, bounded; after a failed read
constexpr double NearMetres=2000;          // only structures within this of the porter are built (the rest wait)
constexpr unsigned BuildsPerStep=4;        // at most this many asked of the game in one step
constexpr unsigned TriesPerPiece=3;        // a structure the game does not keep after this many asks is given up
constexpr uint64_t RetryMs=10000;          // how long an asked structure has to appear before it is asked again
struct Piece { std::string id; uint8_t type=0; structures::Transform at; };
struct Answer { bool read=false,full=false,more=false; std::string version; uint64_t waitMs=3000;
                std::vector<Piece> pieces; std::vector<std::string> removed; unsigned skipped=0; };
// The Net construction type for a server letter (`st` "30" picks the 30 m bridge); 0 for a kind the mod does not build.
uint8_t netType(char letter,const std::string& st);
// The rows (right, forward, up) for the server's rx, ry, rz in degrees: the inverse of the game's decomposition at
// 0x19c1270 (mod/game_offsets.md, what the upload body holds).
std::array<float,9> rotationOf(double rxDegrees,double ryDegrees,double rzDegrees);
Answer parse(const std::string& body);
uint64_t delayAfter(const Answer&,unsigned& failures);
// A construction record the game holds, as the plan needs it.
struct Seen { std::string online; uint8_t owner=0,state=0; std::array<double,3> position{}; };
struct Step { std::vector<Piece> build; std::vector<Seen> remove; std::vector<std::string> givenUp; unsigned twice=0; };
class Plan {
public:
    void take(const Answer&);   // a read: a full one replaces what is wanted, else it adds and removes
    // What to do now: records the game holds, the porter's place (when known), the time.
    Step step(const std::vector<Seen>& records,const std::array<double,3>& porter,bool porterKnown,uint64_t now);
    size_t wanted() const {return wanted_.size();}
    bool idle() const {return wanted_.empty()&&drop_.empty();}
    const std::string& since() const {return version_;}
private:
    std::map<std::string,Piece> wanted_;std::set<std::string> drop_,givenUp_;std::map<std::string,unsigned> tries_;std::map<std::string,uint64_t> askedAt_;
    std::string version_;
};
// The reads, on a thread of their own at the server's pace: each answer read is kept for the game thread (take), and
// `since` follows the last one. If the game thread does not take them for a while, they are dropped and the next read
// is a full one.
constexpr size_t AnswersKept=16;
struct Result { int status=0; std::string body; };
class Reader {
public:
    using Get=std::function<Result(const std::string& since)>;
    Reader(Get,std::function<void(const std::string&)> log);
    ~Reader();
    std::vector<Answer> take();
    uint64_t reads() const {return reads_.load(std::memory_order_relaxed);}
private:
    void run();
    Get get_;std::function<void(const std::string&)> log_;std::vector<Answer> answers_;std::string since_;
    std::atomic<uint64_t> reads_{0};std::mutex lock_;std::condition_variable wake_;bool stop_=false;std::thread thread_;
};
}
