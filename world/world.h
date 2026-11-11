#pragma once
// The players' own server for the game's online layer (the shared world: structures, signs, likes, lost cargo).
// Game-free core: sign-in and one gateway call at a time on JSON values; `http.cpp` puts it on a socket. The
// message shapes are the game's own (mod/game_offsets.md, "Online protocol"); nothing here talks to the publisher's
// service or knows its keys. One World is one co-op group's shared world and is saved as one JSON document.
#include "json.h"
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
namespace world {
constexpr int VersionMajor=0,VersionMinor=12;      // what this game build requires of getVersion
constexpr size_t MaxObjectsPerPlayer=4000,MaxPlayers=16,MaxNameBytes=64,MaxKeyBytes=96;
struct Reply { int status=200;std::string body="{}"; };
// A gateway request body as the game sends it with plain bodies: {"data":"<json, quotes not escaped>"}, or plain JSON.
std::string unwrap(const std::string& body);
class World {
public:
    explicit World(std::function<std::string()> randomToken={});
    // Sign-in. `key` names the account (a Steam id read from the ticket, or a key in the address); the same key is
    // always the same player. Returns the game's auth response, with a fresh session token and `gateway` as given.
    Reply signIn(const std::string& key,const std::string& displayName,const std::string& gateway);
    // One gateway call: `name` is the last path element ("createObject"), `body` the request body as received.
    Reply call(const std::string& token,const std::string& name,const std::string& body,int64_t nowSeconds);
    json::Value snapshot() const;
    void restore(const json::Value&);
    bool dirty() const {std::lock_guard<std::mutex> g(lock_);return dirty_;}
    void saved(){std::lock_guard<std::mutex> g(lock_);dirty_=false;}
    std::function<void(const std::string&)> log;   // one line per notable event (unknown calls, refusals)
    // For tests.
    size_t objects() const {std::lock_guard<std::mutex> g(lock_);return objects_.size();}
    std::string playerOf(const std::string& token) const;
private:
    struct Player { std::string id,name,key; };
    struct Object { std::string id,creator;json::Value post;int64_t updated=0;int64_t likes=0; };
    mutable std::mutex lock_;
    std::function<std::string()> random_;
    std::map<std::string,Player> players_;          // by key
    std::map<std::string,std::string> tokens_;      // token -> player id
    std::map<std::string,Object> objects_;          // by object id
    uint64_t nextObject_=1;bool dirty_=false;
    json::Value objectResponse(const Object&) const;
    Reply gateway(const Player&,const std::string& name,const json::Value& in,int64_t now);
    const Player* playerById(const std::string& id) const;
    void note(const std::string& line) const {if(log)log(line);}
};
// The player id for an account key: "zygo_" and 27 characters, the form the game's own ids have.
std::string playerId(const std::string& key);
}
