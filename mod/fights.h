#pragma once
// Fights with visitors (README.md, "Fights with visitors"; config host_fights, join_fights; off by default). One game
// owns a fight and other players join it as visitors, over the broadcast relay's fight channel (core/broadcast.h types
// 11..16, CORE.md "Fight channel"). The subset of the two-player BT sharing (bts.h) that carries over: the catcher fight.
//
// The owner's game runs the fight and is the only one that decides anything about it. It announces the fight when one
// starts here (kind Catcher, its place, bts::FightRange), keeps it while it runs, and posts its state four times a second:
// the phase and each catcher's life in percent. When the fight ends it posts the end (cleared or lost) for a second and
// closes it. A visitor's report is a request the owner checks against its own fight: damage is taken off its catcher
// (never below bts::HealthSync::Floor: the game lets a catcher die only on a real hit), a gazer the visitor destroyed is
// destroyed here if it lives here, and the visitor's kill ends the fight with a cleared area only when the catcher here
// is at the floor too.
//
// A visitor's game shows the owner's fight with a fight of its own, the shown copy: started with the game's catcher
// command when the owner's state says the fight runs, its catchers' life taken down to the owner's (never up, never
// below the floor), ended with the cleared command only when the owner's state says the area was cleared. Every other
// end (the owner closed it without clearing, left, went silent, the visitor went out of reach) releases the visitor and
// writes nothing: the shown copy is then his game's fight, which he leaves as in the game. A visitor reports, all
// counted from the start of his visit: the life his own hits took off his shown copy (never what the mod wrote), the
// gazers destroyed in his world, how often he was caught, and whether his hit killed his copy. A visitor never
// announces a fight, never destroys a gazer, never writes his world for anything but the shown copy, and a fight the mod
// started as a shown copy is never announced, even after the visit.
//
// What a visitor's own story has of the same boss is not decided (README). Today's rule keeps both choices open: a
// game that runs a fight of its own joins none (its own comes first), and a fight of the visitor's own that starts
// while he visits ends his visit before anything is written to it.
//
// No game, socket or clock here: the mod hands in what it read (Local), what to do to the game (Api) and the broadcast
// client. Sandbox is a game without the game for the tests and the fake world.
#include "bts.h"
#include "../core/broadcast_client.h"
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mod::fights {
constexpr uint8_t Catcher=1;                       // the FIGHT kind: a catcher fight in a BT area
constexpr size_t MaxCatchers=8,MaxGazers=16;
constexpr uint64_t StateEveryMs=250,ReportEveryMs=250,ReportAgainMs=1000,LingerMs=1000,RejoinQuietMs=10000;
enum class Phase : uint8_t { Running=1, Cleared=2, Lost=3 };
// The owner's state (POST payload): u8 format (1) | u8 phase | u8 n catchers (0..8) | u8 reserved | n x u16 life
// (percent x 100, 0..10000).
struct State { Phase phase=Phase::Running; std::vector<float> lives; };
std::vector<uint8_t> encode(const State&);
std::optional<State> decodeState(const std::vector<uint8_t>&);
// A visitor's report: u8 format (1) | u8 flags (1: his hit killed his copy) | u8 n catchers (0..8) | u8 m gazers (0..16)
// | u16 caught | u16 reserved | n x u32 damage (percent x 1000, counted since he joined) | m x u64 gazer keys.
struct Report { bool killed=false; uint16_t caught=0; std::vector<float> damage; std::vector<uint64_t> gazers; bool operator==(const Report&) const; };
std::vector<uint8_t> encode(const Report&);
std::optional<Report> decodeReport(const std::vector<uint8_t>&);
// One look at this game.
struct Local {
    bts::Fights fights; std::vector<bts::Catcher> catchers; std::vector<bts::Gazer> gazers;
    bool available=false;            // the porter is read and in play (not loading, no cutscene)
    std::array<double,3> at{};       // this porter, in the core's axes (the relay's)
    bool dead=false,caught=false;    // vitals.h
    bool boss=false;                 // a story boss fight runs here (the game's BossBattle state, ui.h): never offered, never joined
};
// What the fights do to this game: the catcher command (0 starts a fight here, 1 ends it with the area cleared), a
// catcher's life in percent, a gazer destroyed (bts.h natives).
struct Api {
    std::function<void(uint8_t)> command;
    std::function<void(const bts::Catcher&,float)> hurt;
    std::function<void(uint64_t actor)> destroy;
};
struct Settings { bool host=false,join=false; uint8_t most=9; };   // most: the visitors this game's fights take (9: ten Sams)
struct Counters {
    uint64_t hosted=0,joined=0,released=0,states=0,reports=0;          // fights owned, visits, visits ended, posts
    uint64_t statesTaken=0,reportsTaken=0,damageApplied=0,gazersApplied=0,killsTaken=0,killsRefused=0,malformed=0;
    uint64_t commands=0,writes=0;                                      // what this side did to its game
    uint64_t ownFirst=0;                                               // offers not taken (a fight of its own runs here), visits left for one
};
enum class Role : uint8_t { None, Owner, Visitor };
class Fights {
public:
    Fights(Settings,Api,std::function<void(const std::string&)> log);
    // One look (game thread, under the lock the client is used under): what runs here, the offers, the news.
    void tick(const Local&,ds::broadcast::Client&,uint64_t now);
    Role role() const {return role_;}
    uint64_t owner() const {return owner_;}         // the visited fight's owner (a visitor), this player (an owner)
    uint32_t fight() const {return fight_;}
    bool shown() const {return copy_;}              // the fight running here is a shown copy the mod started
    bool joined() const {return role_==Role::Visitor&&joined_;}
    const Counters& counters() const {return counters_;}
private:
    void host(const Local&,ds::broadcast::Client&,uint64_t now);
    void visit(const Local&,ds::broadcast::Client&,uint64_t now);
    void news(const Local&,ds::broadcast::Client&,uint64_t now);
    void report(const ds::broadcast::FightNews&,const Local&);
    void applyState(const State&,const Local&,ds::broadcast::Client&,uint64_t now);
    void release(ds::broadcast::Client&,uint64_t now,const char* why,bool tellServer);
    void observeVisit(const Local&,ds::broadcast::Client&,uint64_t now);
    Settings settings_;Api api_;std::function<void(const std::string&)> log_;Counters counters_;
    Role role_=Role::None;uint64_t owner_=0;uint32_t fight_=0,nextFight_=0;
    // This game's fight as last seen, and what changed at this look: it started, it stopped, a sphere was cleared.
    bool bossNoted_=false;
    bool primed_=false,active_=false;int32_t cleared_=0;bool rose_=false,fell_=false,won_=false;
    std::vector<bts::Catcher> catchers_;   // this look's catchers, with what this side wrote since
    bool ownTold_=false,offTold_=false;uint8_t visitorsTold_=0;
    // Owner.
    ds::broadcast::FightInfo info_{};Phase phase_=Phase::Running;uint64_t stateAt_=0,endAt_=0;
    struct Visitor {std::vector<float> damage;std::set<uint64_t> gazers;uint16_t caught=0;bool killed=false;};
    std::map<uint64_t,Visitor> visitors_;bool claimed_=false;
    // Visitor.
    bool joined_=false,copy_=false,shownOnce_=false,commanded_=false;uint64_t copyAt_=0;std::optional<State> state_;
    std::vector<float> last_;Report mine_;Report sent_;bool sentOnce_=false;uint64_t reportAt_=0;
    std::map<uint64_t,bool> alive_;bool caught_=false;
    std::map<std::pair<uint64_t,uint32_t>,uint64_t> leftAt_;   // fights left, not joined again for RejoinQuietMs
    std::pair<uint64_t,uint32_t> lastVisit_{0,0};
};
// A game without the game: one catcher manager (a fight with its catchers, the cleared spheres), gazers and the porter,
// and a record of everything the mod did to it. The catcher command starts a fight of one catcher at full life (0) or
// ends the running one with a cleared sphere (1), as live; a hit that takes a catcher to zero kills it and the fight
// ends cleared, as a real hit does.
class Sandbox {
public:
    void start(size_t catchers=1);                 // a fight starts here by itself (the porter was caught)
    void hit(size_t ordinal,float percent);        // this porter's own hit: the catcher's life down by percent
    void end(bool cleared);                        // the fight ends by itself: cleared, or lost (got away, voided out)
    void gazer(uint64_t key,float life);           // a gazer of this world (its key as bts::key gives it)
    void kill(uint64_t key);                       // this porter destroys a gazer
    bool caught=false,dead=false,boss=false;
    Local local(const std::array<double,3>& at,bool available=true) const;
    Api api();
    // What the mod did: commands in order, catcher life writes, gazers destroyed.
    std::vector<uint8_t> commands;uint64_t writes=0,unkillable=0;std::vector<uint64_t> destroyed;
    bool active() const {return active_;}
    int32_t cleared() const {return cleared_;}
    const std::vector<float>& lives() const {return lives_;}
    float life(uint64_t key) const;                // a gazer's life (-1: unknown)
private:
    bool active_=false;int32_t cleared_=0;std::vector<float> lives_;std::map<uint64_t,float> gazers_;
};
}
