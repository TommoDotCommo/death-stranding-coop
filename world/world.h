#pragma once
// The players' own server for the game's online layer (the shared world: structures, signs, likes, lost cargo).
// Game-free core: sign-in and one gateway call at a time on JSON values; `http.cpp` puts it on a socket. The
// message shapes are the game's own (mod/game_offsets.md, "Online protocol"); nothing here talks to the publisher's
// service or knows its keys. One World is one co-op group's shared world and is saved as one JSON document.
#include "identity.h"
#include "json.h"
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace world {
constexpr int VersionMajor=0,VersionMinor=12;      // what this game build requires of getVersion
constexpr size_t MaxObjectsPerPlayer=4000,MaxPlayers=16,MaxNameBytes=64,MaxKeyBytes=96,MaxLikes=2000;
// Linked servers (README, "Linked servers"): at most this many objects held from one other server, this many bytes
// for one object's data on the way, and this much for one page and one request of a live link.
constexpr size_t MaxImportedPerServer=20000,MaxSharedPostBytes=64*1024,LinkPageObjects=500,LinkPageBytes=1024*1024,MaxLinkRequestBytes=4096;
constexpr size_t MaxWorldFileBytes=512u*1024*1024;
// Identity keys (README, "Identity"): how long a sign-in ticket and a relay token are good. A ticket: until its first
// sign-in, at most TicketUnusedSeconds after it was made; then TicketGraceSeconds more (a sign-in that is asked again).
// A relay token: until its first HELLO, at most RelayTokenUnusedSeconds; then while HELLOs keep coming, until
// RelayTokenIdleSeconds after the last one. A new one of either kind ends the player's older one.
constexpr int64_t TicketUnusedSeconds=1800,TicketGraceSeconds=120,RelayTokenUnusedSeconds=1800,RelayTokenIdleSeconds=300;
constexpr size_t PassBytes=32;                      // a ticket or relay token: at most this many letters and digits
struct Reply { int status=200;std::string body="{}"; };
// What may travel between two servers. Text form: "all", "none", or terms separated by ';':
//   types=l,rope,e   owners=NAME,zygo_...   areas=226,227   max-age=30d (s, m, h, d)   likes=no   imported=yes
// A term that is left out does not narrow. `imported=yes` also passes objects that are themselves imports.
struct Filter {
    bool none=false;
    std::set<std::string> types,owners;std::set<int64_t> areas;
    int64_t maxAgeSeconds=-1;
    bool likes=true,imported=false;
};
Filter parseFilter(const std::string& text);        // throws std::runtime_error with the reason
// The names a game shows for a builder (README, "Builder names"). Each world may have a short display name (`--name`):
// 1..MaxHomeChars characters, at most MaxHomeBytes bytes of UTF-8, no control characters, no space at either end.
// `lookup` answers this server's own players with their name only, and the builder of an imported object with
// "Name · Home", Home the display name of the server it was first built on; the whole label is at most MaxLabelChars
// characters and MaxLabelBytes bytes (the game's label holds no more), and only the name part is cut to fit.
constexpr size_t MaxHomeChars=12,MaxHomeBytes=16,MaxLabelChars=24,MaxLabelBytes=32,HomeIdChars=8;
bool serverNameText(const std::string&);
std::string cutText(const std::string&,size_t chars,size_t bytes);   // whole UTF-8 characters, then no space at the end
std::string builderLabel(const std::string& name,const std::string& home);   // "Name · Home"; `home` "": the name only
// One object on its way between servers: `server` and `id` are where it was first built and its id there; `home` that
// server's display name ("" when it has none: the form from before, which the taking side names itself).
struct Shared {
    std::string server,id,creator,name,home;int64_t updated=0,likes=0;json::Value post;
    std::string type() const {return post.get("t").string();}
    int64_t area() const {return post.get("q").integer();}
};
Shared readShared(const json::Value&);              // strict: throws for anything but exactly a well-formed object
json::Value writeShared(const Shared&);
bool passes(const Filter&,const Shared&,bool imported,int64_t nowSeconds);
bool serverIdText(const std::string&);              // 4..32 letters and digits
bool objectIdText(const std::string&);              // 2..64 letters and digits
struct ImportResult { size_t added=0,updated=0,unchanged=0,removed=0,filtered=0,own=0; };
// The server's policy (README, "Server policy"): a preset and single settings, saved with the world. Values:
//   join invite|open (recorded only)   build everyone|members|nobody   accept_offers auto|review|never
//   limit N (objects per player, 0 = none)   withdrawal stay|go   players N (1..MaxPlayers)
//   offer_builds yes|no|ask, offer_old yes|no: what a player who chose nothing has chosen
//   order newest|liked, per_builder N (0 = none), spread METRES (0 = off): which objects a game is handed (curate)
//   identity none|keys: keys refuses every sign-in but a launcher's ticket, and every API call that is not signed
struct Policy {
    std::string preset="friends",join="invite",build="everyone",acceptOffers="auto",withdrawal="go",offerBuilds="yes",offerOld="no",order="newest",identity="none";
    int64_t limit=0,players=int64_t(MaxPlayers),perBuilder=0,spread=20;
};
Policy presetPolicy(const std::string& name);       // friends, solo or public; throws for anything else
void setPolicy(Policy&,const std::string& keyValue); // "KEY=VALUE"; throws with the reason and leaves it as it was
std::vector<std::pair<std::string,std::string>> policyItems(const Policy&);   // key and value, in a fixed order
std::string heldText(const std::string& why);       // why an object is held (build, member, limit), in words
// Which objects of one area a player's game is handed (README, "Which objects a game is handed"). Pure: the World
// gives it the area's objects, what was handed to that player there before, and the policy.
struct Candidate {
    std::string id,creator,type;int64_t area=0,updated=0,likes=0;
    bool shared=true;                                // handed to every player (not kept, held or waiting)
    bool placed=false;double x=0,y=0,z=0;           // its position in metres, when it has one
};
struct Curated { std::vector<std::string> handed,remember; };
constexpr size_t MaxPickAreas=128,MaxPicksPerArea=1000;   // what is remembered per player, and per player and area
// `counts`: the number the game asks per kind (nullptr: no number, every kind); `previous`: what was remembered for this
// player and area, most recently handed first; `friends`: the asker's friends. Returns the answer (`handed`) and what
// to remember for the next one: what was handed, then what was remembered before and is still there.
Curated curate(const std::string& player,int64_t area,const std::map<std::string,int64_t>* counts,const std::vector<Candidate>& candidates,
               const std::vector<std::string>& previous,const Policy& policy,const std::set<std::string>& friends={});
std::string typeName(const std::string& letter);    // "ladder" for "l"; the letter itself for a kind without a name
// A gateway request body as the game sends it with plain bodies: {"data":"<json, quotes not escaped>"}, or plain JSON.
std::string unwrap(const std::string& body);
class World {
public:
    explicit World(std::function<std::string()> randomToken={});
    // Sign-in. `key` names the account (a Steam id read from the ticket, or a key in the address); the same key is
    // always the same player. Returns the game's auth response, with a fresh session token and `gateway` as given.
    // Refused (403) under identity keys, and for an account of the key form (identity.h) whatever the policy.
    Reply signIn(const std::string& key,const std::string& displayName,const std::string& gateway);
    // Identity (README, "Identity"). A sign-in with a ticket the launcher took for its player (apiTicket): that
    // player's session, while the ticket is good.
    Reply signInTicket(const std::string& ticket,const std::string& displayName,const std::string& gateway,int64_t now);
    // For a signed call of the player whose account (identity::account) this is, under identity keys: a sign-in ticket,
    // or a relay token for his relay id. Makes the player when the account is new (the `players` setting applies).
    // Throws std::runtime_error with the reason when refused, and then changed nothing.
    json::Value apiTicket(const std::string& account,int64_t now);
    json::Value apiRelayToken(const std::string& account,uint64_t relayId,int64_t now);
    // May this HELLO's (or BYE's) token speak for relay id `id`? identity none: the token is the group's key
    // (`groupKey`); keys: a good relay token of that id (a HELLO it admits keeps it good).
    bool relayAdmit(uint64_t id,const std::string& token,const std::string& groupKey,int64_t now);
    // The owner's account key (identity::account of the host's launcher key; "" none): his signed calls are the
    // owner's actions in the API. Saved with the world.
    void setOwner(const std::string& account);
    std::string owner() const {std::lock_guard<std::mutex> g(lock_);return owner_;}
    // One gateway call: `name` is the last path element ("createObject"), `body` the request body as received.
    Reply call(const std::string& token,const std::string& name,const std::string& body,int64_t nowSeconds);
    json::Value snapshot() const;
    void restore(const json::Value&);
    bool dirty() const {std::lock_guard<std::mutex> g(lock_);return dirty_;}
    void saved(){std::lock_guard<std::mutex> g(lock_);dirty_=false;}
    std::function<void(const std::string&)> log;   // one line per notable event (unknown calls, refusals)
    // Linked servers. This world's own id is made once and saved with it.
    std::string serverId() const {std::lock_guard<std::mutex> g(lock_);return serverId_;}
    void setServerId(const std::string& id);
    // This world's display name ("" none; serverNameText, else it throws). Saved with the world and given with what it
    // exports. `setLinkName`: the name a live link gives the server it takes from (--link NAME), the name its builders'
    // home has here while that server gives none of its own. Not saved: the link names it again at every start.
    std::string name() const {std::lock_guard<std::mutex> g(lock_);return name_;}
    void setName(const std::string&);
    void setLinkName(const std::string& server,const std::string& name);
    std::string homeName(const std::string& server) const;   // a server's display name here: its own, else its link's, else its id's first characters
    // Changes whenever anything an export could hold changes, and with every start of the process.
    std::string version() const {std::lock_guard<std::mutex> g(lock_);return versionLocked();}
    // The objects that pass `f`, as a document: the ones after id `after`, at most `limit` and about `bytes` of them
    // ("more" and "next" say whether and where to go on). Never an object that came from `forServer`.
    json::Value exportObjects(const Filter& f,const std::string& forServer,int64_t now,const std::string& after={},size_t limit=size_t(-1),size_t bytes=size_t(-1)) const;
    // Takes a document made by exportObjects. All of it is checked first: a document that is refused (it throws)
    // changed nothing. With `prune`, objects held from that server which the document no longer lists are removed.
    ImportResult importObjects(const json::Value& document,const Filter& f,bool prune,int64_t now);
    ImportResult importShared(const std::string& via,const std::vector<Shared>& list,const Filter& f,bool prune,int64_t now);
    size_t forget(const std::string& server);       // everything first built on, or received from, that server
    // One page for a linked server that asks (body: {"format":1,"server":ID,"have":VERSION,"after":ID}).
    Reply linkPull(const Filter& exportFilter,const std::string& body,int64_t now) const;
    struct Origin { std::string server,via;size_t objects=0; };
    std::vector<Origin> origins() const;
    size_t imported() const {std::lock_guard<std::mutex> g(lock_);return byOrigin_.size();}
    // Server policy and the players' choices (README, "Server policy"). The calls below that change something throw
    // std::runtime_error with the reason when they are refused, and then changed nothing.
    Policy policy() const {std::lock_guard<std::mutex> g(lock_);return policy_;}
    void setPolicy(const Policy&);
    // A player's own choices, "KEY=VALUE": offer_builds yes|no|ask|default, offer_old yes|no|default, member yes|no.
    void setPlayer(const std::string& id,const std::string& keyValue);
    // One line per player of this world, then one per builder known here only from imports (known=false).
    // `key`: his account key; `likes`: on his objects built here that are still his (a remnant's are nobody's).
    struct PlayerInfo { std::string id,name,key,offerBuilds,offerOld;bool known=true,member=false;size_t shared=0,pending=0,kept=0,held=0,remnants=0,imported=0;int64_t likes=0; };
    std::vector<PlayerInfo> playerList() const;
    // The objects that wait: for their builder's answer (offer_builds ask), or for the owner (accept_offers review),
    // and the ones held because the policy does not let them be shared (`held`: build, member or limit).
    struct Waiting { std::string id,creator,name,type,held;int64_t area=0,updated=0;bool forBuilder=false; };
    std::vector<Waiting> waiting() const;
    // The owner: an object that waits for him is shared (accept) or kept for its builder only (reject). A held object
    // is shared only once the policy allows it (and its builder, if he was to be asked, said yes). With `player`,
    // every object of that player that waits for the owner, or is held and now allowed, is accepted. Returns how many.
    size_t review(const std::string& object,bool accept);
    size_t reviewPlayer(const std::string& player);
    // A player's answer for what waits for him or is held ("" = all of it): yes goes on as accept_offers says (a held
    // object only once the policy allows it), no keeps it his.
    size_t offer(const std::string& player,const std::string& object,bool yes);
    // A player takes back his objects ("" = all): with withdrawal "stay" what was shared stays as a remnant under his
    // name that no account owns; anything else, and everything with "go", is removed.
    struct Withdrawn { size_t remnants=0,removed=0; };
    Withdrawn withdraw(const std::string& player,const std::string& object);
    // The owner removes everything one builder has here (built, waiting, kept, remnants, and what came from him
    // through linked servers). Returns how many objects went.
    size_t removePlayer(const std::string& id);
    // The launcher's API (api.h): one account, by the key it signs in with. The documents are the API's answers;
    // apiChoices throws std::runtime_error with the reason when it is refused, and then changed nothing. It makes the
    // player when the key is new (as a sign-in would: the `players` setting applies), so his choices are in place
    // before his game first signs in.
    json::Value apiPolicy(int64_t now=0) const;          // with the server's clock (`time`), for the launcher's signatures
    json::Value apiPlayers() const;                       // the owner's: playerList and waiting as documents
    json::Value apiWaiting() const;
    json::Value apiMe(const std::string& key) const;
    json::Value apiMine(const std::string& key) const;
    void apiChoices(const std::string& key,const std::string& offerBuilds,const std::string& offerOld);
    // For tests.
    size_t objects() const {std::lock_guard<std::mutex> g(lock_);return objects_.size();}
    std::string playerOf(const std::string& token) const;
private:
    // offerBuilds, offerOld: "" when the player chose nothing (the policy's value applies).
    struct Player { std::string id,name,key,offerBuilds,offerOld;bool member=false; };
    // An imported object keeps where it was first built (originServer, originId), which server handed it over (via),
    // its builder's name there and the like count it arrived with; `likes` is that count plus what was given here.
    // `name` is the builder's name when it was built (kept for a remnant); `share` is "" for an object every player
    // is handed, else "kept" (its builder's only), "ask" (waits for its builder), "review" (waits for the owner) or
    // "held" (its builder's only because the policy does not let it be shared: `held` says why, build, member or
    // limit; `heldAsk`: its builder is still to be asked).
    // A remnant was withdrawn by its builder and stayed: nobody changes it, and its likes go to no one.
    // `home`: the display name of its first server as the record that brought it gave it ("" none given).
    struct Object { std::string id,creator;json::Value post;int64_t updated=0;int64_t likes=0;
                    std::string originServer,originId,via,originName,home;int64_t originLikes=0;
                    std::string name,share,held;bool remnant=false,heldAsk=false; };
    struct Like { std::string to,from,object,type;int64_t time=0,points=0,autoPoints=0; };   // to: the object's builder
    mutable std::mutex lock_;
    std::function<std::string()> random_;
    std::map<std::string,Player> players_;          // by key
    std::map<std::string,std::string> tokens_;      // token -> player id
    std::map<std::string,Object> objects_;          // by object id
    std::vector<Like> likes_;                       // oldest first, at most MaxLikes
    uint64_t nextObject_=1;bool dirty_=false;
    std::string serverId_,epoch_;uint64_t changes_=0;
    std::string name_;std::map<std::string,std::string> linkNames_;   // this world's display name; server id -> link name
    std::string homeLocked(const std::string& server,const std::string& given) const;
    std::map<std::pair<std::string,std::string>,std::string> byOrigin_;   // (origin server, id there) -> id here
    Policy policy_;
    // Identity: the owner's account, and the tickets and relay tokens the launchers took (by text).
    struct Pass { std::string account;uint64_t relayId=0;int64_t issued=0,used=0; };   // used: first sign-in; a token's last HELLO
    std::string owner_;std::map<std::string,Pass> tickets_,relayTokens_;
    std::string pass(std::map<std::string,Pass>& passes,const std::string& account,uint64_t relayId,int64_t now);
    Player& playerFor(const std::string& account,const char* why);   // made when new, within `players`
    Reply signInLocked(const std::string& key,const std::string& displayName,const std::string& gateway);
    // What each player's game was handed per area (curate): kept so the same structures come back, saved with the world.
    struct Picks { int64_t asked=0;std::vector<std::string> ids; };
    std::map<std::string,std::map<int64_t,Picks>> picks_;   // player id -> area -> picks
    Candidate candidateOf(const Object&) const;
    void remember(const std::string& player,int64_t area,const std::vector<std::string>& ids,int64_t now);
    std::set<std::string> friendsOf(const std::string&) const {return {};}   // no friend list exists yet
    std::string versionLocked() const {return epoch_+"-"+std::to_string(changes_);}
    bool servedTo(const Object&,const std::string& player) const;          // may this player be handed it
    std::string shareAfterOffer() const;                                   // what an offered upload becomes
    std::string blocked(const Player&,bool counted) const;                 // why this player may not share one more ("": he may)
    size_t offered(const std::string& player) const;                      // his objects that count for the limit
    Object& ownObject(const std::string& player,const std::string& object);
    std::string importedId(char type,const std::pair<std::string,std::string>& origin) const;
    Shared sharedOf(const Object&) const;
    json::Value objectResponse(const Object&) const;
    Reply gateway(const Player&,const std::string& name,const json::Value& in,int64_t now);
    const Player* playerById(const std::string& id) const;
    void note(const std::string& line) const {if(log)log(line);}
};
// The player id for an account key: "zygo_" and 27 characters, the form the game's own ids have.
std::string playerId(const std::string& key);
}
