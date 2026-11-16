#pragma once
// The player's side of the relay's broadcast mode (broadcast.h; CORE.md, "Broadcast envelope"), without sockets and
// without a clock of its own, like the core: the adapter gives it a send function for the relay's address, hands it
// only datagrams from that address, and calls tick(now) with its monotonic milliseconds from one thread.
//
// Up: HELLO every helloMs (keep-alive, position, and the clock's ping: the WELCOME carries the server's time); one
// PUBLISH a tick at most, with this porter's presence (at most 20 a second, on a change that shows on the wire, and every
// 500 ms unchanged) and the animation sample offered since the last one; STATE for each kind of shown state when it
// changes, repeated after every WELCOME that shows the server holds an older version.
// Down: the roster (ENTER, LEAVE: a reliable stream, as in version 1); FRAME entries of the players near, interpolated
// on the server's clock; SHARE, the state of a player near, asked for again (FETCH) when an entry shows a generation
// this side does not have.
//
// The source of every entry is the server's own record of who sent it; a source that is not in the roster is dropped.
// The checks the core makes on a session's presence are made here per source: the core's decoder for every value,
// order and freshness, impossible motion (with a fresh baseline after a second of it, where the core would have
// reconnected) and one teleport every five seconds.
#include "broadcast.h"
#include "core.h"
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace ds::broadcast {
struct ClientConfig {
    uint64_t id=0; std::string token,nickname;
    uint32_t game=GameBuild; BuildId build{};   // the adapter's wire identity: the server puts only the same build near
    uint32_t helloMs=500;      // how often HELLO is sent (the clock is sampled with it)
    uint32_t silentMs=5000;    // no WELCOME for this long: the relay is gone and so is everyone near
    uint32_t delayMs=120;      // the least presentation delay (the core's Config::delayMs)
};
struct Counters {
    uint64_t sent=0,sentBytes=0,received=0,receivedBytes=0;   // datagrams to and from the relay, and their sizes
    uint64_t published=0,presences=0,animations=0,states=0,fetches=0;
    uint64_t frames=0,entries=0,accepted=0,animationsTaken=0,shares=0;
    uint64_t unknown=0;        // an entry or share from a player who is not in the roster
    uint64_t malformed=0;      // a datagram or an entry the decoders refused
    uint64_t refused=0;        // an entry refused for its order, time, motion or teleport rate
    uint64_t rebased=0;        // a source taken from a fresh baseline after a second of impossible motion
};
class Client {
public:
    using Send=std::function<void(const uint8_t*,size_t)>;
    Client(ClientConfig,Send);
    // Where the player is, for the server's who-is-near (HELLO). false (nothing reported) for a place the wire cannot carry.
    bool setPosition(const std::array<double,3>&,uint32_t area=0);
    void clearPosition();
    // This porter for the next PUBLISH. sampledAt: when the pose was read (this side's clock). jumped: the provider's
    // teleport flag rose; it goes out on the next PUBLISH that carries presence and is then cleared.
    void setLocal(const ds::State&,bool available,bool gated,uint64_t sampledAt,bool jumped);
    // This porter's animation controls for the next PUBLISH: true when taken (the caller then gathers rose again);
    // at most every 50 ms, an unchanged sample at most every 500 ms, nothing while offline, alone or gated.
    bool offerAnimation(const Animation&,uint64_t now);
    // What this porter shows of one kind (the adapter's bytes, 1..StateMax): a new version when the bytes change.
    // false for bytes the wire cannot carry.
    bool setState(StateKind,const std::vector<uint8_t>&);
    void tick(uint64_t now);
    void receive(const uint8_t*,size_t,uint64_t now);
    void bye();
    std::vector<relay::RosterEvent> takeEvents();   // ENTER and LEAVE since the last call, in order
    const std::map<uint64_t,std::string>& nearby() const {return near_;}
    bool online() const {return online_;}
    Refusal refusal() const {return refusal_;}
    const Welcome& welcome() const {return welcome_;}
    uint64_t id() const {return config_.id;}
    // The server's clock: valid from the first WELCOME; offset = server minus this side, moving by at most 1 % of
    // elapsed time towards the estimate of the quickest of the last 16 round trips.
    bool clockValid() const {return clockValid_;}
    double offsetMs() const {return offset_;}
    double rttMs() const {return rtt_;}
    uint64_t serverNow(uint64_t now) const;
    // One player near, as received. render: interpolated on the server's clock at that player's presentation delay.
    Render render(uint64_t source,uint64_t now) const;
    std::optional<AnimationShown> takeAnimation(uint64_t source,uint64_t now);
    bool live(uint64_t source,uint64_t now) const;            // a presence accepted within the last 3 s
    uint32_t presentationDelayMs(uint64_t source) const;
    // The source's state of one kind: nullptr when none has arrived. revision changes whenever the bytes do.
    const std::vector<uint8_t>* state(uint64_t source,StateKind,uint64_t* revision=nullptr) const;
    uint16_t generation(uint64_t source) const;               // the state generation this side has complete
    const Counters& counters() const {return counters_;}
    Versions versions() const {return mine_;}                 // this porter's own state versions
private:
    struct Blob {std::vector<uint8_t> bytes;uint32_t version=0;uint16_t generation=0;bool have=false;uint64_t revision=0;};
    struct AnimationSample {Animation animation;uint64_t time=0;uint32_t sequence=0;};
    struct Source {
        std::string nickname;std::deque<Snapshot> snapshots;
        bool haveEntry=false;uint32_t sequence=0;uint64_t stamp=0,acceptedAt=0;bool everAccepted=false;
        bool haveTeleport=false;uint64_t teleportAt=0;bool refusing=false;uint64_t refusingSince=0;
        std::deque<int32_t> lateness;std::deque<uint32_t> intervals;double delay=0;uint64_t delayAt=0;
        std::deque<AnimationSample> animations;uint32_t taken=0;
        std::array<Blob,StateKinds> blobs{};uint16_t generation=0;bool wantFetch=false;uint64_t mismatchSince=0,fetchAt=0;
    };
    void sendDatagram(const Buffer&);
    void sendHello(uint64_t now);
    void publish(uint64_t now);
    void sendState(size_t kind);
    void entry(const Update&,uint64_t now);
    void share(const Share&,uint64_t now);
    void adaptDelay(Source&,uint64_t now);
    bool answer(uint32_t sequence);
    void leaveAll();
    void offline();
    void clockSample(uint64_t t0,uint64_t serverTime,uint64_t now);
    ClientConfig config_;Send send_;Counters counters_;
    std::array<double,3> position_{};uint32_t area_=0;bool have_=false;
    bool online_=false,sentHello_=false;uint64_t nextHello_=0,lastWelcome_=0;
    uint32_t helloSequence_=0,lastAnswer_=0,stream_=0,applied_=0;std::deque<std::pair<uint32_t,uint64_t>> helloTimes_;
    Refusal refusal_=Refusal::None;Welcome welcome_{};
    std::map<uint64_t,std::string> near_;std::vector<relay::RosterEvent> events_;std::map<uint64_t,Source> sources_;
    bool clockValid_=false;double offset_=0,rtt_=-1;uint64_t offsetAt_=0;std::deque<std::pair<double,double>> pongs_;
    // This porter.
    ds::State local_{};bool available_=false,gated_=true,jump_=false;uint64_t sampledAt_=0;
    bool havePublished_=false,haveSentPresence_=false;uint32_t sequence_=0;uint64_t lastStamp_=0,lastPresenceAt_=0,lastSample_=0;ds::State sentState_{};
    std::optional<Animation> pendingAnimation_;Animation sentAnimation_{};bool sentAnimationOnce_=false;uint64_t animationAt_=0;
    std::array<std::vector<uint8_t>,StateKinds> state_{};Versions mine_{};
};
}
