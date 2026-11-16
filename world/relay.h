#pragma once
// The relay for the real-time layer, without sockets: one datagram in, datagrams out through `send`, time passed in.
// Players say HELLO with the group's token and where they are (core/relay.h has the envelope); the relay works out who
// is near whom, tells both sides (ENTER, LEAVE) and passes core datagrams on between players who are near each other
// and nobody else. It never looks inside a core datagram.
//
// What it holds to:
//   - a player's address is learned from a HELLO with the right token and nothing else; while the player is known, a
//     HELLO for the same id from another address is refused, and DATA from an address that has not said HELLO is dropped;
//   - two players are near from `enterMetres` or closer until they are farther than `leaveMetres` apart (straight-line
//     distance in three dimensions), in the same area, both with a position; DATA goes only between players who are near;
//   - a player who says no HELLO for `timeoutMs` is gone (LEAVE for everyone near);
//   - at most `maxPlayers` players, `ratePerPeer` datagrams a second per player for each player near him (at least one
//     player's worth; a second's worth can be saved up), and no datagram longer than the envelope allows.
// One datagram in makes at most one DATA datagram out; the relay answers nothing to a sender without the token.
//
// Broadcast mode (envelope version 2, core/broadcast.h), for players whose HELLO says so: a player publishes one update
// a tick and the relay copies it to every player near him, the source id taken from its own record of the sender's
// address and the stamp checked against its own clock (WELCOME tells each player the server's time). Updates for one
// receiver are packed into FRAMEs of at most `budget` bytes, each sent within `flushMs`. Interest: every update of a
// player within `fullMetres`, beyond that at most one every `intervalMs` (a teleport always), animation only within
// `animationMetres` (the one-frame pulses of the updates left out are carried by the next one sent). The state a player
// only shows (name, gear, carried cases and their places) is kept, the latest version of each kind, and handed out: to
// a player who comes near, to everyone near when it changes, and again to a player who asks (FETCH) for a player near.
// Players of the two modes are never near each other, nor broadcast players of different builds.
#include "../core/broadcast.h"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
namespace world {
struct RelayAddress {
    uint32_t ip=0; uint16_t port=0;   // host byte order
    bool operator<(const RelayAddress& o) const {return ip!=o.ip?ip<o.ip:port<o.port;}
    bool operator==(const RelayAddress& o) const {return ip==o.ip&&port==o.port;}
};
constexpr size_t RelayPlayersMax=1024,RelayPendingMax=4096;
constexpr uint32_t BroadcastStampAheadMs=500,BroadcastStampBehindMs=2000,BroadcastFetchEveryMs=250;
struct RelayConfig {
    std::string token;                       // the group's secret; every HELLO must carry it
    // Set: it decides instead of `token` whether a HELLO's (or BYE's) token may speak for that player id (the server's
    // identity: World::relayAdmit, a relay token of the player under identity keys).
    std::function<bool(uint64_t id,const std::string& token)> admit;
    double enterMetres=150,leaveMetres=200;  // 1..65535, leave >= enter
    uint32_t timeoutMs=10000;
    size_t maxPlayers=64;                    // 1..RelayPlayersMax
    uint32_t ratePerPeer=200;                // datagrams a second, for each player near the sender
    uint32_t seed=1;                         // roster stream numbers start here: random in the server, fixed in tests
    // Broadcast mode.
    double fullMetres=30;                    // every update of a player this near (0..65535)
    uint32_t intervalMs=200;                 // farther: at most one update in this many ms (0..5000; 0: every update)
    double animationMetres=50;               // animation samples only this near (0..65535)
    size_t budget=1200;                      // the most bytes in one FRAME (256..1200)
    uint32_t flushMs=25;                     // the longest an update waits for others to share its datagram (0..200; 0: at once)
    uint32_t broadcastRate=100;              // datagrams a second a broadcast player may send (10..100000)
    bool pinBuild=false;uint32_t game=0;ds::broadcast::BuildId build{};   // set: broadcast players of another build are refused
};
struct RelayStats {
    uint64_t received=0,malformed=0,badToken=0,unknownSource=0,rateDropped=0,notNear=0,forwarded=0,refused=0,timedOut=0;
    // Broadcast mode.
    uint64_t wrongMode=0;        // a message of the other mode from a known player
    uint64_t spoofed=0;          // a PUBLISH, STATE or FETCH naming another player than its address belongs to
    uint64_t published=0,staleUpdates=0,badStamps=0;   // updates taken; refused for their order; for their stamp
    uint64_t entries=0,thinned=0,frames=0,frameBytes=0;   // entries sent; left out by interest; FRAMEs and their bytes
    uint64_t states=0,staleStates=0,shares=0,fetches=0;
};
class Relay {
public:
    using Send=std::function<void(const RelayAddress&,const uint8_t*,size_t)>;
    Relay(RelayConfig,Send);   // throws invalid_argument for a configuration outside the ranges above
    void receive(const RelayAddress& from,const uint8_t* data,size_t size,uint64_t nowMs);
    void tick(uint64_t nowMs); // drops the players who went silent; call it a few times a second
    size_t players() const {return players_.size();}
    bool known(uint64_t id) const {return players_.count(id)!=0;}
    bool areNear(uint64_t a,uint64_t b) const;
    void flush(uint64_t nowMs);       // sends every FRAME that has waited flushMs (tick does it too)
    uint64_t flushDue() const;        // when the next FRAME is due (UINT64_MAX: none waiting)
    const RelayStats& stats() const {return stats_;}
    std::function<void(const std::string&)> log;   // one line when a player joins or goes
private:
    struct Player {
        uint64_t id=0; RelayAddress address; std::string nickname;
        uint64_t lastSeen=0,bucketAt=0; double tokens=0;
        uint32_t lastHello=0,area=0; bool have=false; std::array<double,3> position{};
        std::set<uint64_t> nearby;
        uint32_t stream=0,acked=0; std::deque<ds::relay::RosterEvent> pending;   // pending.front() has sequence acked+1
        // Broadcast mode (mode 2).
        uint16_t mode=1; uint32_t game=0; ds::broadcast::BuildId build{};
        bool havePublish=false,haveAt=false; uint32_t publishSequence=0; uint64_t publishStamp=0; std::array<double,3> at{};
        struct Blob {uint32_t version=0; ds::relay::Buffer bytes;};
        std::array<Blob,ds::broadcast::StateKinds> states; uint16_t generation=0;
        // What this player has been sent of each source near him: when, and the pulses of the samples left out since.
        struct Feed {uint64_t lastSent=0,fetchAt=0; bool sent=false; std::array<uint8_t,32> rose{}; uint8_t roseBytes=0;};
        std::map<uint64_t,Feed> feeds;
        ds::relay::Buffer batch; uint64_t batchSince=0;
    };
    bool allow(Player&,uint64_t now);
    bool rightToken(const std::string&,uint64_t id) const;
    void hello(const RelayAddress&,const ds::relay::Hello&,uint64_t now,const ds::broadcast::Hello* v2=nullptr);
    void refuse(const RelayAddress&,uint64_t id,uint32_t sequence,uint8_t reason,uint16_t mode);
    bool compatible(const Player&,const Player&) const;
    void receiveBroadcast(const RelayAddress&,Player*,const uint8_t*,size_t,uint64_t now);
    void publish(Player&,const ds::broadcast::Update&,uint64_t now);
    void state(Player&,const ds::broadcast::StatePost&);
    void fetch(Player&,uint64_t source,uint64_t now);
    void share(Player& to,const Player& from,size_t kind);
    void enqueue(Player&,const ds::broadcast::Update&,uint64_t now);
    void sendBatch(Player&,uint64_t now);
    std::array<double,3> where(const Player& p) const {return p.haveAt?p.at:p.position;}
    void remove(uint64_t id,const char* why);
    void restartStream(Player&);
    void event(Player& to,bool enter,const Player& who);
    void flush(Player&);
    void place(Player&);
    void note(const std::string& s) const {if(log)log(s);}
    RelayConfig config_;Send send_;RelayStats stats_;uint32_t streams_=0;
    std::vector<std::pair<uint64_t,uint64_t>> entered_;   // (to, from) pairs that came near in this HELLO: states to hand out
    std::map<uint64_t,Player> players_;std::map<RelayAddress,uint64_t> byAddress_;ds::relay::Buffer out_;
};
}
