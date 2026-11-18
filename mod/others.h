#pragma once
// Others: more remote players than the one partner, presence only. Each other player has a 64-bit id (given by
// whatever carries the datagrams), a presence-core session of its own (core/core.h: handshake, clock, interpolation,
// animation samples) and a body of its own (an OwnBody driven by a Double, double.h). Nothing of the two-player layer
// that changes the world exists for them: no cargo put down or picked up, structures, enemies, orders, story, vehicles.
// What is only shown can be: the gear a player wears and the cases a player carries (Settings::gear, Settings::carry),
// from the same reliable events the partner path uses, kept per player and drawn on that player's body.
//
// Three parts, all game-free:
//   Body, Bodies   one more Sam and a keyed set of them (also what the lab command `crowd N` uses);
//   Others         the sessions: who is here, what each one's core says, what to send them (the relay's pairs mode;
//                  Players is what the mod asks of it, and of Crowd, crowd.h, the broadcast mode);
//   wearers, carriers   who wears what and whose cases are drawn, for gear::Mirrors and stack::Stacks;
//   MemoryHub      a link in memory that joins several Others, for tests.
// Nothing here opens a socket. The datagrams travel through a Link the caller supplies (CORE.md, "Several peers").
#include "../core/core.h"
#include "double.h"
#include "nametag.h"
#include "gear.h"
#include "carry_visual.h"
#include "carry_place.h"
#include "cargo_stack.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mod::others {
constexpr size_t MaxOthers=32;            // the most sessions (config `others` is capped here)
constexpr uint64_t MakeEveryMs=300;       // one new body at a time: making one takes the game a while

// ---- Bodies ---------------------------------------------------------------------------------------------------------
// What the game side gives for one more Sam. own: how a body is made, dressed, parked and removed. drive: the calls that
// move it; `find` is replaced (the Double drives this body, never the game's resident double). settle: optional, after
// every drive of a body that exists (the mod keeps the body's inventory component quiet and joins it to the porter's
// update group there).
struct BodyParts {
    puppet::OwnBodyApi own;puppet::DoubleApi drive;
    std::function<void(uint64_t body,uint64_t porter,uint64_t now)> settle;
};
class Body {
public:
    Body(BodyParts parts,std::function<void(const std::string&)> log);
    Body(const Body&)=delete;Body& operator=(const Body&)=delete;
    // One game-thread tick. The body is made when the porter's world allows it (once per world: OwnBody) and mayMake
    // says so, then driven to goal with that speed, those actions and that animation sample. goal==nullptr: hidden and
    // asleep where it stands, still the mod's (no new acquire when it is shown again). Returns the body's entity, 0
    // while there is none.
    uint64_t tick(const puppet::Anchor& porter,const puppet::Transform* goal,const ds::Animation* sample,bool fresh,double speed,uint32_t actions,uint64_t now,bool mayMake=true);
    uint64_t creations() const {return creations_;}   // entities the game made for this body (one per world)
    void remove();                          // handed back and removed by the game
    uint64_t entity() const {return entity_;}
    bool givenUp() const {return own_.exhausted();}
    bool driving() const {return drive_.stage()==puppet::Double::Stage::Driving;}
private:
    std::function<void(uint64_t,uint64_t,uint64_t)> settle_;uint64_t entity_=0,porter_=0,creations_=0;
    puppet::OwnBody own_;puppet::Double drive_;
};
// What one body is to do this tick. The pointers must stay valid until drive() returns.
struct Want {uint64_t key=0;const puppet::Transform* goal=nullptr;const ds::Animation* sample=nullptr;bool fresh=false;double speed=0;uint32_t actions=0;};
// A set of bodies by key (a player id, or an index for the crowd). drive(): a body whose key is no longer wanted is
// removed; a wanted key without a body gets one; every body is driven with its own Want and nothing else. The game is
// asked for at most one new entity every MakeEveryMs, also when a new world needs every body made again.
class Bodies {
public:
    using Make=std::function<std::unique_ptr<Body>(uint64_t key)>;
    explicit Bodies(Make make):make_(std::move(make)){}
    bool available() const {return bool(make_);}
    void drive(const std::vector<Want>& wants,const puppet::Anchor& porter,uint64_t now);
    size_t size() const {return bodies_.size();}
    uint64_t entity(uint64_t key) const {auto it=bodies_.find(key);return it==bodies_.end()?0:it->second->entity();}
    size_t made() const {return made_;}           // bodies that exist in the game, as of the last drive()
    size_t driving() const {return driving_;}
    size_t givenUp() const {return givenUp_;}
    uint64_t makes() const {return makes_;}       // entities the game was asked for and made
    uint64_t removes() const {return removes_;}
private:
    Make make_;std::map<uint64_t,std::unique_ptr<Body>> bodies_;uint64_t madeAt_=0,makes_=0,removes_=0;bool madeOnce_=false;
    size_t made_=0,driving_=0,givenUp_=0;
};

// ---- Sessions -------------------------------------------------------------------------------------------------------
// The seam to the transport. send: one datagram for player `to` (called from tick, animation, entered and left; never
// kept). The transport calls Others::entered / left / received. Every call into Others must come from one thread at a
// time (the mod makes them under its runtime lock).
struct Link {std::function<void(uint64_t to,const uint8_t* data,size_t size)> send;};
// This porter, once per game-thread tick. jumped: this sample is a jump (the provider's teleport flag on the tick it
// rose); each session keeps it until a presence packet carrying it has gone to that player.
struct Local {ds::State state;bool available=false,gated=true,jumped=false;uint64_t sampledAt=0;uint32_t area=0;};   // area: the map he is in (0: not known)
struct Settings {
    uint64_t self=0;                     // this player's id on the link; nonzero
    uint64_t session=0;                  // the group's shared number (config `session`); nonzero
    size_t cap=0;                        // the most other players at once; 0 = off
    std::array<uint8_t,16> build{};      // the adapter's wire identity (identity.h), bytes 16..31 of every datagram
    std::string nickname;
    uint32_t delayMs=120;
    // The reliable events others take besides the name (everything else is acknowledged and dropped):
    bool gear=false;                     // type 7: what a player wears (gear.h). Also: this porter's set is sent
    bool carry=false;                    // types 6 and 16: what a player carries and where each case sits
                                         // (carry_visual.h, carry_place.h). Also: this porter's are sent
};
// The core session two players share: the same on both sides, different for every pair, never zero.
uint64_t pairSession(uint64_t session,uint64_t a,uint64_t b);
// The lower id is the core's host of the pair (the core wants opposite roles; no role means more than that here).
inline ds::Role pairRole(uint64_t self,uint64_t other){return self<other?ds::Role::Host:ds::Role::Client;}
// One other player for this game-thread tick. visible: connected, fresh enough and not in a private room or cutscene
// (the core's rule) and this game is not gated; otherwise the body is hidden.
struct Shown {
    uint64_t id=0;bool connected=false,visible=false;ds::Render render;std::string name;
    std::optional<ds::AnimationShown> animation;   // the sample due now, when one is
    // The latest complete sets that player sent (none: nothing has arrived, or the setting is off). What is carried is
    // forgotten when the session drops; what is worn stays until another set arrives.
    std::optional<gear::WornSet> worn;
    std::optional<carry::Summary> carried;
    std::optional<std::vector<carryplace::Placed>> places;
};
// How a body shows a player: where it stands, how fast, which of the game's own poses.
struct Drive {bool shown=false;puppet::Transform goal;double speed=0;uint32_t actions=0;bool mirror=false;};
Drive drive(const Shown&,bool poses=true);
// The Wants for these players, one each, in the same order (a hidden player's has no goal). plan keeps what the Wants
// point at. show=false: nobody is shown (a local game event). animation=false: no controls are mirrored.
struct Plan {std::vector<Drive> drives;std::vector<std::optional<ds::Animation>> samples;std::vector<Want> wants;};
void plan(const std::vector<Shown>&,bool show,bool poses,bool animation,Plan&);
// The labels for these players (nametag.h, Tags): one for every player whose body exists, keyed by the player's id, with
// the name that player sent ("Porter" until one has arrived). Whether a label is shown is the body's affair: Tags asks
// where the body is and draws nothing over a hidden one.
std::vector<nametag::Named> labels(const std::vector<Shown>&,const class Bodies&);
// Who wears gear (gear.h, Mirrors): one wearer for every player whose body exists, keyed by the player's id, with the
// set that player sent. Without a set nothing is made for that body: this porter's gear says nothing about a player
// with a save of their own. Whether the copies show is the body's affair (Mirrors asks whether the body is visible).
std::vector<gear::Wearer> wearers(const std::vector<Shown>&,const class Bodies&);
// A body that still wears copies when its player has left (or is no longer listed): the copies come off first. Given
// the keys that wear copies now, returns those whose player is not in `shown`, and keeps each one's body for this tick,
// hidden (a Want without a goal). The caller removes those keys' copies now; next tick they wear nothing and their
// bodies go. So the game is asked to remove a body's attached copies one tick before the body itself, never after.
std::vector<uint64_t> undress(const std::vector<Shown>&,const std::vector<uint64_t>& dressed,Plan&);
// Whose carried cases are drawn (cargo_stack.h, Stacks). A full backpack is up to 24 meshes, so only the nearest few
// players get theirs: CarriersAtOnce by default, at most MaxCarriers. A player who has them keeps them until another is
// KeepMetres nearer (nametag.h), so two at about the same distance do not swap every tick.
constexpr size_t CarriersAtOnce=4,MaxCarriers=8;
class Nearest {
public:
    // Which of these (key, distance) are among the `limit` nearest, a holder counted KeepMetres nearer; remembers them.
    std::vector<bool> choose(const std::vector<std::pair<uint64_t,double>>&,size_t limit);
    bool holds(uint64_t key) const {return held_.count(key)!=0;}
private:
    std::set<uint64_t> held_;
};
// One player's cases: on `pack`, the copy of the backpack that player's body wears. placed: where each case sits
// relative to it, as that player's game measured it (then `items` are those cases, hips and arms included); without
// it `items` are the backpack's cases from the summary, packed by the stack's layout.
struct Carrier {uint64_t key=0,body=0,pack=0;std::vector<stack::Item> items;std::optional<std::vector<puppet::Transform>> placed;};
// The carriers among these players: connected, visible, with a body, with a copy of the backpack on it (pack(id), 0:
// none; without one there is nothing to hang the cases on) and carrying something; of those the `limit` nearest to
// `viewer` (this porter, in the wire's axes; without it, as listed). show=false (a local game event): nobody.
std::vector<Carrier> carriers(const std::vector<Shown>&,const class Bodies&,const std::function<uint64_t(uint64_t id)>& pack,bool show,
                              const std::array<double,3>* viewer,size_t limit,Nearest&);
struct Counters {
    uint64_t sent=0,received=0,accepted=0;
    uint64_t unknown=0;        // from an id that has not entered
    uint64_t malformed=0;      // not a datagram of the presence core (size)
    uint64_t foreignBuild=0;   // another adapter identity
    uint64_t kind=0;           // a kind others never carry (environment, enemies)
    uint64_t refused=0;        // refused by the session's core
    uint64_t full=0;           // entered over the cap
    uint64_t events=0;         // reliable events others do not take: acknowledged and dropped
    uint64_t gear=0,carried=0; // complete sets taken: what a player wears; what a player carries or where it sits
};
// What the mod asks of the other players, whichever way their datagrams travel: Others (below: the relay's pairs mode,
// one presence-core session per player) or Crowd (crowd.h: the relay's broadcast mode, one client for everybody). The
// bodies, labels, gear and cases (Bodies, plan, labels, wearers, carriers) take the Shown either one gives.
class Players {
public:
    virtual ~Players()=default;
    // local: before tick and frame of the same game-thread tick. tick: the network thread's pass. animation: this
    // porter's controls (rose = the pulses since the previous call); true when any was sent. frame: every player as
    // shown now, by id.
    virtual void local(const Local&)=0;
    virtual void tick(uint64_t nowMs)=0;
    virtual bool animation(const ds::Animation&,uint64_t nowMs)=0;
    // What this porter wears and carries, whenever the mod has read it again (nothing: the read failed); `placed` only
    // on the passes that measured the places. Without Settings::gear / Settings::carry nothing is sent. Returns the
    // events queued.
    virtual size_t wears(const std::optional<gear::WornSet>&,uint64_t nowMs)=0;
    virtual size_t carries(const std::optional<carry::Summary>&,const std::optional<std::vector<carryplace::Placed>>&,uint64_t nowMs)=0;
    virtual std::optional<std::array<double,3>> here() const=0;   // this porter (wire axes)
    virtual std::vector<Shown> frame(uint64_t nowMs)=0;
    virtual void clear(uint64_t nowMs)=0;   // everybody leaves (the link went away)
    virtual size_t size() const=0;          // the players taken (at most Settings::cap)
    virtual size_t connected() const=0;     // of those, the ones heard from now
    virtual const Counters& counters() const=0;
    virtual const Settings& settings() const=0;
};
class Others final:public Players {
public:
    Others(Settings,Link,std::function<uint64_t()> nonce,std::function<void(const std::string&)> log);
    ~Others() override;
    Others(const Others&)=delete;Others& operator=(const Others&)=delete;
    // From the link. entered: false when refused (off, zero, this player's own id, already here, or full; a player
    // refused because it was full is taken when somebody leaves).
    bool entered(uint64_t id,uint64_t nowMs);
    void left(uint64_t id,uint64_t nowMs);
    ds::Error received(uint64_t from,const uint8_t* data,size_t size,uint64_t nowMs);   // None = taken by that player's core
    // From the mod. local: before tick and frame of the same game-thread tick. tick: handshakes, pings, presence and
    // names for every session. animation: this porter's controls (rose = the pulses since the previous call) to every
    // connected session at the core's rate; true when any was sent. frame: every player as shown now, by id.
    void local(const Local&) override;
    void tick(uint64_t nowMs) override;
    bool animation(const ds::Animation&,uint64_t nowMs) override;
    // What this porter wears and carries, whenever the mod has read it again (nothing: the read failed). Each session
    // sends it by the partner path's own rules (gear.h WornSender, carry_visual.h Sender, carry_place.h Sender: once
    // it has held still, on every new connection, not too often), as the same events. Without Settings::gear /
    // Settings::carry nothing is sent. `placed` is given on the passes that measured the places (nothing: not this
    // pass). Returns the events queued.
    size_t wears(const std::optional<gear::WornSet>&,uint64_t nowMs) override;
    size_t carries(const std::optional<carry::Summary>&,const std::optional<std::vector<carryplace::Placed>>&,uint64_t nowMs) override;
    std::optional<std::array<double,3>> here() const override {if(!local_.available)return std::nullopt;return local_.state.position;}   // this porter (wire axes)
    std::vector<Shown> frame(uint64_t nowMs) override;
    void clear(uint64_t nowMs) override; // everybody leaves (the link went away)
    size_t size() const override {return sessions_.size();}
    size_t connected() const override;
    bool here(uint64_t id) const {return sessions_.count(id)!=0;}
    const Counters& counters() const override {return counters_;}
    const Settings& settings() const override {return settings_;}
private:
    struct Session;
    Settings settings_;Link link_;std::function<uint64_t()> nonce_;std::function<void(const std::string&)> log_;
    Local local_;Counters counters_;std::map<uint64_t,std::unique_ptr<Session>> sessions_;std::deque<uint64_t> waiting_;
    void out(uint64_t to,std::vector<uint8_t> datagram);
};

// ---- A link in memory -----------------------------------------------------------------------------------------------
// Joins several Others as a relay would: everybody who has joined is "near" everybody else. Datagrams are queued and
// handed over by deliver(), each after delayMs; drop decides per datagram whether it is lost. Every player may have a
// clock of its own: `clock` is added to the hub's time for every call into that player.
class MemoryHub {
public:
    uint32_t delayMs=0;
    std::function<bool(uint64_t from,uint64_t to,const std::vector<uint8_t>&)> drop;
    Link link(uint64_t self);                               // the Link for the player with this id
    void join(uint64_t id,Others& player,uint64_t nowMs,uint64_t clock=0);   // ENTER both ways with everybody already here
    void leave(uint64_t id,uint64_t nowMs);                 // LEAVE both ways
    void deliver(uint64_t nowMs);
    size_t queued() const {return queue_.size();}
    struct Datagram {uint64_t from=0,to=0,due=0;std::vector<uint8_t> bytes;};
    std::vector<Datagram> sentLog;                          // every datagram handed to the hub while record is set
    bool record=false;
private:
    struct Player {Others* others=nullptr;uint64_t clock=0;};
    std::map<uint64_t,Player> players_;std::deque<Datagram> queue_;uint64_t now_=0;
};
}
