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
//
// Linked servers' presence (README.md, "Presence across linked servers"; the envelope is presence.h): the relays of two
// linked servers tell each other where their broadcast players are (cells of 8 m), and each sends the other the players
// of its own that are near one of the other's, with their updates and shown state, within a byte budget a second. A
// player of a linked server is a player here like the others, near the players here by the same rules, under an id
// of this server's (never one a player here holds: a player here who says HELLO with it takes it, and the linked one
// gets another), with his own build, and his name followed by " · " and the name this server gave the link. He is never
// sent anything, never near another linked server's player, and never sent on to a third server. The link's key makes
// a tag on every datagram; a link silent for LinkSilentMs loses its players (LEAVE to everyone near them).
#include "presence.h"
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
constexpr uint32_t LinkHelloMs=500,LinkListsMs=500,LinkSilentMs=3000,LinkReportMs=5000,LinkReceiveRate=4000;
constexpr double LinkMarginMetres=25;   // how far a place may move between two lists of cells
constexpr size_t LinksMax=16;
// A linked server whose broadcast players this relay exchanges with its own.
struct RelayLink {
    std::string name,key;   // the link's name (shown after its players' names) and key (8..64 letters, digits, - and _)
    RelayAddress peer;      // the other server's relay; ip 0: learned from its first HELLO with the right tag
    uint32_t budget=250000; // bytes a second sent to it (2000..100000000; a second's worth can be saved up)
    size_t players=64;      // players sent to it, and taken from it, at most (1..1024)
};
struct RelayLinkStats {
    uint64_t sent=0,sentBytes=0,received=0,receivedBytes=0;
    uint64_t forged=0;        // a datagram of the envelope without this link's tag
    uint64_t stale=0;         // with the tag, but from another session or address than the link's
    uint64_t replayed=0;      // with the tag, a sequence already taken
    uint64_t malformed=0,rateDropped=0,sessions=0;
    uint64_t entries=0,budgetDropped=0,stripped=0;   // updates sent; left out for the budget; sent without animation for it
    uint64_t joined=0,left=0,unknown=0,otherBuild=0,overCap=0,clockless=0,shares=0,fetches=0;
};
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
    // Linked servers' presence.
    std::vector<RelayLink> links;            // at most LinksMax, each with a name and a key of its own
    std::string serverId;                    // this world's id (world.h), told to the linked servers; required with links
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
    size_t players() const {return players_.size()-remotes_;}   // the players of this server
    size_t remotePlayers() const {return remotes_;}             // the linked servers' players held now
    bool known(uint64_t id) const {return players_.count(id)!=0;}
    // Linked servers' presence, by the link's place in RelayConfig::links.
    bool linkUp(size_t link) const;
    size_t exported(size_t link) const;                          // players of this server sent to that server now
    bool exports(size_t link,uint64_t id) const;
    uint64_t remoteId(size_t link,uint64_t theirs) const;        // this server's id for that server's player (0: none)
    const RelayLinkStats& linkStats(size_t link) const;
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
        // A linked server's player: link is 1 + the link's index (0: a player of this server), theirs his id there.
        size_t link=0; uint64_t theirs=0,placedAt=0,framedAt=0,fetchAt=0; std::string name;
        bool haveTheirGeneration=false; uint16_t theirGeneration=0;
    };
    // How much of a source a receiver at some distance is sent (squared metres).
    struct Reach {double full2=0,animation2=0;uint32_t intervalMs=0;};
    struct Link {
        RelayLink config; world::presence::Key key; bool configured=false; RelayAddress address;
        uint64_t mine=0,peer=0,candidate=0,salt=0; std::vector<uint64_t> retired; world::presence::Window window;
        uint32_t sequence=0; bool up=false; uint64_t lastHeard=0,nextHello=0,nextLists=0,probeAt=0,reportAt=0;
        std::string peerServer; world::presence::Hello reach;    // the other side's distances
        double rateTokens=LinkReceiveRate; uint64_t rateAt=0;
        // Its clock: the offset (theirs minus ours) of the quickest of the last 16 round trips.
        std::deque<uint64_t> sentT0; uint64_t echo=0,echoAt=0; std::deque<std::pair<double,double>> samples; bool clock=false; double offset=0;
        // Out: where its players are, the players of ours sent to it, its byte budget and the FRAME being filled.
        std::vector<world::presence::Cell> cells; world::presence::Assembly<world::presence::Cell> cellsIn; uint32_t lists=0;
        struct Export {Player::Feed feed; double d=0; uint64_t fetchAt=0; bool listed=false;};
        std::map<uint64_t,Export> exported; bool presentDue=false;
        double tokens=0; uint64_t bucketAt=0; ds::relay::Buffer batch; uint64_t batchSince=0;
        // In: its players held here, by their id there.
        std::map<uint64_t,uint64_t> remote; world::presence::Assembly<world::presence::Present> presentIn;
        RelayLinkStats stats,reported;
    };
    bool allow(Player&,uint64_t now);
    bool rightToken(const std::string&,uint64_t id) const;
    void hello(const RelayAddress&,const ds::relay::Hello&,uint64_t now,const ds::broadcast::Hello* v2=nullptr);
    void refuse(const RelayAddress&,uint64_t id,uint32_t sequence,uint8_t reason,uint16_t mode);
    bool compatible(const Player&,const Player&) const;
    void receiveBroadcast(const RelayAddress&,Player*,const uint8_t*,size_t,uint64_t now);
    bool publish(Player&,const ds::broadcast::Update&,uint64_t now);
    bool interest(Player::Feed&,const ds::broadcast::Update&,double d2,const Reach&,bool teleport,uint64_t now,ds::broadcast::Update& out,ds::relay::Buffer& merged,uint64_t& thinned);
    void state(Player&,const ds::broadcast::StatePost&);
    bool takeState(Player&,size_t kind,uint32_t version,const uint8_t* bytes,size_t size);
    void handOut();
    // Linked servers' presence (relay.cpp, the last part).
    void receiveLink(const RelayAddress&,const uint8_t*,size_t,uint64_t now);
    void linkHello(Link&,size_t index,const RelayAddress&,const world::presence::Header&,const uint8_t*,size_t,uint64_t now);
    void linkPresent(Link&,size_t index,const uint8_t*,size_t,uint64_t now);
    void linkFrame(Link&,const uint8_t*,size_t,uint64_t now);
    void linkShare(Link&,const uint8_t*,size_t,uint64_t now);
    void linkFetch(Link&,const uint8_t*,size_t,uint64_t now);
    void linkTick(Link&,size_t index,uint64_t now);
    void linkDown(Link&,const char* why);
    void linkExports(Link&,uint64_t now);
    void linkUpdate(Link&,Link::Export&,const Player&,const ds::broadcast::Update&,bool teleport,uint64_t now);
    void linkSendShare(Link&,const Player&,size_t kind);
    void linkSend(Link&,world::presence::Type,const uint8_t*,size_t,uint64_t receiver=0);
    void linkBatch(Link&,uint64_t now);
    void linkFetchFrom(Link&,Player&,uint64_t now);
    uint64_t localId(Link&,uint64_t theirs) const;
    std::string linkName(const Link&,const uint8_t* blob,size_t size,const std::string& nickname) const;
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
    std::vector<Link> links_;size_t remotes_=0;
};
}
