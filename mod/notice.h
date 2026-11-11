#pragma once
// Notices: what happens to the session, told in the game's own info log (the message feed at the side of the screen).
// The partner joined, the partner left, and, once, that nobody has arrived yet (the usual cause on a first session is
// a firewall that does not let the game through); and when the partner disappears into a cutscene or private room
// and comes back, so a hidden partner is explained. The game's info-log queue takes a finished text
// (game_offsets.md "Info log"); the mod adds an entry there and nothing else: no table item, no saved state.
#include "discovery.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
namespace mod::notice {
struct Api {
    // true: queued in the game's info log; false: no HUD now. icon: PlainIcon or LikeIcon (the game's own icon numbers).
    std::function<bool(const std::u16string& text,uint8_t icon)> show;
};
enum class Kind : uint8_t {Joined,Left,Alone,Away,Back,Liked,Down,Up,Caught,Free,Call};
constexpr uint8_t PlainIcon=0,LikeIcon=3;   // "20 minutes have passed." / "{1} Like(s) received from {0}."
uint8_t icon(Kind);
// The partner's name is cleaned like the name tag's (printable, no markup characters); "Your partner" without one.
std::u16string text(Kind,const std::string& partnerName);
class Notifier {
public:
    // A join waits NameMs for the partner's name. Alone: in the world this long with no partner since the mod started
    // (a minute: the two games of a session can finish loading well apart).
    // Notices are GapMs apart; one the HUD does not take is tried again every RetryMs and dropped after GiveUpMs.
    // Away: the partner has been in a game event or a private room (where his body is hidden) for AwayMs; Back when he
    // returns after that was told. A partner who connects while his game is still loading (he reports the same state)
    // is announced when he is in the world, or after JoinHoldMs; that first absence is not told as "away".
    static constexpr uint64_t NameMs=3000,AloneMs=60000,AwayMs=2000,JoinHoldMs=120000,GapMs=2500,RetryMs=1000,GiveUpMs=30000;static constexpr size_t MaxPending=4;
    Notifier(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)){}
    // One call per game-thread tick. inWorld: the porter is in the world and no game event runs (nothing is shown
    // otherwise, but what happened meanwhile is told afterwards). connected: the session with the partner is up.
    // partnerAway: the partner reports a game event or private room (presence flag PrivateRoom).
    void tick(uint64_t nowMs,bool inWorld,bool connected,const std::string& partnerName,bool partnerAway=false);
    // What has happened to the partner (presence flags Down and Caught, vitals.h): told once when it starts and once
    // when it is over, only while connected; a partner who leaves in that state just leaves.
    void status(uint64_t nowMs,bool connected,bool down,bool caught);
    // The partner calls out (presence flag Call): told when it starts, at most once every CallGapMs.
    static constexpr uint64_t CallGapMs=8000;
    void call(uint64_t nowMs,bool connected,bool calling);
    // Something the partner did that is worth a line of its own (Liked: his like has just been given to this porter).
    void event(Kind kind,const std::string& partnerName,uint64_t nowMs){add(kind,partnerName.empty()?lastName_:partnerName,nowMs);}
    size_t pending() const {return pending_.size();}
    unsigned shown() const {return shown_;}
private:
    struct Entry {Kind kind;std::string name;uint64_t since=0,nextTry=0;bool named=false,held=false;};
    void add(Kind kind,const std::string& name,uint64_t now);
    Api api_;std::function<void(const std::string&)> log_;std::deque<Entry> pending_;
    bool started_=false,connected_=false,everConnected_=false,aloneTold_=false,away_=false,awayTold_=false,joining_=false;uint64_t worldSince_=0,lastShown_=0,awaySince_=0;bool inWorld_=false;unsigned shown_=0;
    std::string lastName_;bool down_=false,caught_=false,calling_=false,callTold_=false;uint64_t lastCall_=0;
};
// Pinned build: the UI owner global and the UI clock global (its double at +0xd0), DSUIInfoLogHUDLogic's vtable, its
// queue (100 entries of 0x28 bytes, count at +0xfa0) and the slim lock (SRWLOCK) the game takes around an add. A notice
// looks like the game's plain messages ("20 minutes have passed."): icon 0 (3 for a like), colour 0, sound 3, shown in the same
// states; NoticeId is the id the mod's entries carry (no table item has it).
constexpr uint64_t UiOwnerSlot=0x7c00128,UiClockSlot=0x7be6318,HudVtable=0x3e53850,Queue=0x28,QueueLock=0xfd0;
constexpr uint8_t Colour=0,Sound=3;constexpr uint32_t States=0x8201,NoticeId=0x434f4f50;
// The game's info-log queue for the pinned build (signatures checked); throws when it cannot be verified.
Api nativeApi(discovery::Sampler&);
}
