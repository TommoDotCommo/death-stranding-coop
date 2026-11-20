#pragma once
// Boss Battle replays played together, in group play (opt-in `group_replays`, with `group_enemies`; the server's rule
// `enemies`). The Director's Cut replays a story boss from the private room's Boss Battle menu: the menu's code (0x1f +
// row) goes to the room's blackboard and the room's script starts the replay's mission (game_offsets.md, "Boss
// replays"). Each player fights in his own game; what is shared:
//
// - The start. When a replay starts in this game by the player's own choice (fact 12 rises, the started mission is one
//   of the 17 replay missions), it is said once as `replay` (key: the mission and a fight number this game picks;
//   value: the menu code). A player who opted in, stands in his own private room and fights nothing is started on the
//   same code within KeepMs of hearing it (once per heard start); everyone else is told. A replay the mod started is
//   never said again, so it never pulls the starter back in after his fight.
// - The boss's life, while the fight runs (one shared fight: the starter's fight number). Each game says its boss's
//   life per part (BT bosses: each catcher in record order) as `bosslife` in permille, when it changes and again every
//   RepeatMs; each game takes its parts down to the lowest life heard for this fight (a joiner hears the starter's
//   fight from before he joined too): never up, never below Floor (the game lets a boss die only on a hit of its own),
//   never from another fight.
//
// A replay the mod started that turns out to be another mission (the menu's rows can differ between players' games)
// is fought alone: nothing is shared. No game, server or clock here: the mod hands in what it read and what was heard,
// and does what tick() returns.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mod::groupreplay {
constexpr uint64_t KeepMs=300000;          // a heard start is joined this long (the server keeps it as long)
constexpr uint64_t StartWaitMs=30000;      // a start the mod asked for that has not begun by then is given up
constexpr uint64_t AfterFightMs=10000;     // no join this soon after a fight here ended
constexpr uint64_t SayEveryMs=1000,RepeatMs=4000;
constexpr float Floor=0.05f;               // percent: the lowest life the mod writes (as bts::HealthSync::Floor)
constexpr float Least=0.5f;                // percent: a smaller difference is not written
constexpr int32_t FirstCode=0x1f,LastCode=0x2b;   // the Boss Battle menu's codes (rows 0x1f.., ranking 0x24..0x2b)
constexpr size_t MaxParts=8;
// The 17 boss-replay missions (the game's own list, 0x33cb020).
bool replayMission(int64_t id);
// Server keys: a start is mission << 16 | fight; a part's life mission << 24 | fight << 8 | part (both below 2^40).
int64_t startKey(int64_t mission,uint16_t fight);
int64_t lifeKey(int64_t mission,uint16_t fight,uint8_t part);
struct Event { std::string kind; int64_t key=0,value=0; bool operator==(const Event& o) const {return kind==o.kind&&key==o.key&&value==o.value;} };
// One look at this game.
struct Local {
    bool available=false;      // the porter is read and the game runs (not a title or loading screen)
    bool inRoom=false;         // in his private room (the game-flow manager's +0x58 bit 25)
    bool running=false;        // fact 12: a boss replay runs here
    bool boss=false;           // a boss fight runs here (game state BossBattle)
    int64_t mission=-1;        // the mission whose start last passed the game's check (-1: none seen)
    int32_t code=-1;           // the Boss Battle code accepted last (-1: none seen)
    std::vector<float> lives;  // while running: the boss's life in percent per part (empty: unknown)
};
struct Settings { bool join=false; };      // start a heard replay when in the private room
struct Hurt { size_t part=0; float percent=0; };
struct Actions {
    std::vector<Event> say;                 // to the server
    std::optional<int32_t> start;           // write this code to the private room's blackboard
    std::vector<Hurt> hurt;                 // write these lives
    std::vector<std::string> notices;       // tell the player
};
struct Counters { uint64_t said=0,heard=0,started=0,joined=0,givenUp=0,mismatched=0,written=0,lifeSaid=0; };
class Group {
public:
    // fight: a fresh fight number for this game's own start (never 0).
    Group(Settings,std::function<uint16_t()> fight,std::function<void(const std::string&)> log);
    void heard(const Event&,uint64_t now);
    Actions tick(const Local&,uint64_t now);
    const Counters& counters() const {return counters_;}
    // The fight shared now (mission, fight; 0 when none).
    int64_t mission() const {return mission_;}
    uint16_t fight() const {return fight_;}
private:
    struct Start { int64_t mission=0; uint16_t fight=0; int32_t code=0; uint64_t heardAt=0; bool used=false,told=false; };
    Settings settings_;std::function<uint16_t()> next_;std::function<void(const std::string&)> log_;Counters counters_;
    std::map<int64_t,Start> starts_;        // heard starts by key
    bool primed_=false,running_=false;uint64_t endedAt_=0;
    // A start the mod asked for: the heard start's key, when.
    std::optional<int64_t> pending_;uint64_t pendingAt_=0;
    // The fight running here: its mission and fight number (0: not shared), when it began, what was said.
    int64_t mission_=0;uint16_t fight_=0;uint64_t fightAt_=0;
    std::map<int64_t,std::pair<int64_t,uint64_t>> lives_;   // heard life keys -> the lowest permille heard, when last heard
    std::map<uint8_t,std::pair<int64_t,uint64_t>> said_;   // part -> permille said, when
};
}
