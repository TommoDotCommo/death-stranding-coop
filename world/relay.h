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
#include "../core/relay.h"
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
struct RelayConfig {
    std::string token;                       // the group's secret; every HELLO must carry it
    double enterMetres=150,leaveMetres=200;  // 1..65535, leave >= enter
    uint32_t timeoutMs=10000;
    size_t maxPlayers=64;                    // 1..RelayPlayersMax
    uint32_t ratePerPeer=200;                // datagrams a second, for each player near the sender
    uint32_t seed=1;                         // roster stream numbers start here: random in the server, fixed in tests
};
struct RelayStats { uint64_t received=0,malformed=0,badToken=0,unknownSource=0,rateDropped=0,notNear=0,forwarded=0,refused=0,timedOut=0; };
class Relay {
public:
    using Send=std::function<void(const RelayAddress&,const uint8_t*,size_t)>;
    Relay(RelayConfig,Send);   // throws invalid_argument for a configuration outside the ranges above
    void receive(const RelayAddress& from,const uint8_t* data,size_t size,uint64_t nowMs);
    void tick(uint64_t nowMs); // drops the players who went silent; call it a few times a second
    size_t players() const {return players_.size();}
    bool known(uint64_t id) const {return players_.count(id)!=0;}
    bool areNear(uint64_t a,uint64_t b) const;
    const RelayStats& stats() const {return stats_;}
    std::function<void(const std::string&)> log;   // one line when a player joins or goes
private:
    struct Player {
        uint64_t id=0; RelayAddress address; std::string nickname;
        uint64_t lastSeen=0,bucketAt=0; double tokens=0;
        uint32_t lastHello=0,area=0; bool have=false; std::array<double,3> position{};
        std::set<uint64_t> nearby;
        uint32_t stream=0,acked=0; std::deque<ds::relay::RosterEvent> pending;   // pending.front() has sequence acked+1
    };
    bool allow(Player&,uint64_t now);
    bool rightToken(const std::string&) const;
    void hello(const RelayAddress&,const ds::relay::Hello&,uint64_t now);
    void refuse(const RelayAddress&,const ds::relay::Hello&,ds::relay::Refusal);
    void remove(uint64_t id,const char* why);
    void restartStream(Player&);
    void event(Player& to,bool enter,const Player& who);
    void flush(Player&);
    void place(Player&);
    void note(const std::string& s) const {if(log)log(s);}
    RelayConfig config_;Send send_;RelayStats stats_;uint32_t streams_=0;
    std::map<uint64_t,Player> players_;std::map<RelayAddress,uint64_t> byAddress_;ds::relay::Buffer out_;
};
}
