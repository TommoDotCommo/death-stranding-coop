#pragma once
// The players' own server for the game's online layer (the shared world: structures, signs, likes, lost cargo).
// Game-free core: sign-in and one gateway call at a time on JSON values; `http.cpp` puts it on a socket. The
// message shapes are the game's own (mod/game_offsets.md, "Online protocol"); nothing here talks to the publisher's
// service or knows its keys. One World is one co-op group's shared world and is saved as one JSON document.
#include "identity.h"
#include "json.h"
#include <array>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace world {
constexpr int VersionMajor=0,VersionMinor=12;      // what this game build requires of getVersion
constexpr size_t MaxObjectsPerPlayer=4000,MaxAccounts=65536,MaxNameBytes=64,MaxKeyBytes=96,MaxLikes=2000;
// MaxAccounts: the most accounts one world keeps, every player who ever joined it (the policy's `players` is at most
// this). How many play at once is the relay's limit (RelayPlayersMax, 1024).
// Linked servers (README, "Linked servers"): at most this many objects held from one other server, this many bytes
// for one object's data on the way, and this much for one page and one request of a live link.
constexpr size_t MaxImportedPerServer=20000,MaxSharedPostBytes=64*1024,LinkPageObjects=500,LinkPageBytes=1024*1024,MaxLinkRequestBytes=4096;
constexpr size_t MaxWorldFileBytes=512u*1024*1024;
// Identity keys (README, "Identity"): how long a sign-in ticket and a relay token are good. A ticket: until its first
// sign-in, at most TicketUnusedSeconds after it was made; then TicketGraceSeconds more (a sign-in that is asked again).
// A relay token: until its first HELLO, at most RelayTokenUnusedSeconds; then while HELLOs keep coming, until
// RelayTokenIdleSeconds after the last one. A new one of either kind ends the player's older one.
constexpr int64_t TicketUnusedSeconds=1800,TicketGraceSeconds=120,RelayTokenUnusedSeconds=1800,RelayTokenIdleSeconds=300;
// A notice token (README, "What the game is told"): the mod's read of its player's own objects' states, under every
// identity. Good until its first read, at most NoticeTokenUnusedSeconds; then while reads keep coming, until
// NoticeTokenIdleSeconds after the last. The time of the last read is saved at most every NoticeTokenSaveSeconds, so a
// token outlives a short restart of the server. A new one ends the player's older one.
constexpr size_t ChatKept=200,ChatBatch=50,ChatMaxChars=160,ChatMaxBytes=640,ChatPerWindow=5;constexpr int64_t ChatWindowSeconds=10;
constexpr int64_t NoticeTokenUnusedSeconds=1800,NoticeTokenIdleSeconds=900,NoticeTokenSaveSeconds=300;
constexpr size_t PassBytes=32;                      // a ticket or relay token: at most this many letters and digits
// Who a player sees (README, "How many players one player sees"): at most MaxFriends on his friend list, and a cap of
// at most MaxNearCap players near him shown at once (the relay's own limit of players).
constexpr size_t MaxFriends=256;constexpr int64_t MaxNearCap=1024;
// Roads paved together (README, "Roads paved together"): what each player put into the game's auto-pavers, by the
// paver's construction id (the same in every game, 1..MaxPaverId) and resource (0..HighwayResources-1). One call holds
// at most MaxHighwayEntries entries, one entry 1..MaxHighwayAmount; a player keeps at most MaxHighwayKeys pairs of
// paver and resource, each total at most MaxHighwayTotal (more is not counted). Likes for them: at most MaxHighwayAmount
// in one entry.
constexpr size_t MaxHighwayEntries=64,MaxHighwayKeys=1024;
constexpr int64_t MaxPaverId=999999,HighwayResources=6,MaxHighwayAmount=100000,MaxHighwayTotal=1000000000;
struct Reply { int status=200;std::string body="{}"; };
// A launcher's call refused with this status and reason (the collection calls: 400, 403 or 409).
struct Refused : std::runtime_error { int status;Refused(int s,const std::string& why):std::runtime_error(why),status(s){} };
// Collections (../docs/COLLECTIONS.md): where a player's game last was, as the relay heard it (World::noteSpot). Game
// axes in metres (x, y on the ground, z up); `yaw` the porter's facing in radians (0: +y, pi/2: +x), when known.
struct Spot { double x=0,y=0,z=0;bool haveYaw=false;double yaw=0;int64_t at=0;std::string name;uint32_t area=0; };
constexpr size_t MaxSpots=1024;
constexpr int64_t ChatWaitMinMs=2000,ChatWaitMaxMs=10000;   // how often a game reads the chat: every 2 s, up to 10 s in a crowd
constexpr int64_t MapFreshSeconds=5,MapWaitMinMs=2000,MapWaitMaxMs=30000;constexpr size_t MapPlayersMax=128,WorldIdsWaitMs=60000;   // who counts as there for the map, how many it lists, how often to ask
constexpr int64_t SpotFreshSeconds=600,MaxNearMetres=2000,AreaSearchMetres=1500;
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
//   limit N (objects per player, 0 = none)   withdrawal stay|go   players N (1..MaxAccounts)
//   offer_builds yes|no|ask, offer_old yes|no: what a player who chose nothing has chosen
//   order newest|liked, per_builder N (0 = none), spread METRES (0 = off): which objects a game is handed (curate)
//   identity none|keys: keys refuses every sign-in but a launcher's ticket, and every API call that is not signed
//   near_cap N (0 = none): the most players near one broadcast player he is shown on the relay
//   challenges everyone|members|owner|nobody: who may publish a route challenge (challenges.h)
//   import everyone|members|owner|nobody: who may import a collection (../docs/COLLECTIONS.md); import_cost free|materials
//   (materials: what an imported structure carried is recorded on it; nothing is taken yet)
//   chat on|off: players' text chat on this server (World::chatSay); off in the public preset until it has more moderation
//   world save|server: server: a game shows only what is on this server and hides what its save holds, which stays in
//     the save (public preset); save: a game keeps its save's structures and adds the server's.
//   map on|off: every player here is shown on the others' map (World::mapRead); off in the public preset, where strangers
//   would follow each other across the world
//   test_mode off|on: every player's co-op mod starts in test mode (unlimited materials, stamina and blood; for alpha
//   testers); the launcher writes it into the game's config, the server itself does nothing with it
struct Policy {
    std::string preset="friends",join="invite",build="everyone",acceptOffers="auto",withdrawal="go",offerBuilds="yes",offerOld="no",order="newest",identity="none",challenges="everyone";
    std::string importWho="everyone",importCost="free",chat="on",map="on",world="save";
#ifndef MUTANT_POLICY_TEST_DEFAULT
    std::string testMode="off";
#else
    std::string testMode="on";
#endif
    int64_t limit=0,players=int64_t(MaxAccounts),perBuilder=0,spread=20,nearCap=32;
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
    // `creator` is the builder the games are told; `owner` the player it belongs to ("": the creator). They differ
    // only for a structure imported from a collection: told as its importer's collection builder, owned by him, and
    // handed to him whatever its state (../docs/COLLECTIONS.md, "Who the games are told built it").
    std::string owner;
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
    json::Value apiTicket(const std::string& account,int64_t now,const std::string& name={});   // name: the launcher's name for him
    json::Value apiRelayToken(const std::string& account,uint64_t relayId,int64_t now);
    // May this HELLO's (or BYE's) token speak for relay id `id`? identity none: the token is the group's key
    // (`groupKey`); keys: a good relay token of that id (a HELLO it admits keeps it good).
    bool relayAdmit(uint64_t id,const std::string& token,const std::string& groupKey,int64_t now);
    // What the game is told (README, "What the game is told"). A notice token for the player of this account (made
    // when new, as by choices; any identity: the API's own rules say who may ask), and the read it allows: the states of
    // his own objects built here and nothing else. `since` is the `version` of his last answer: the objects whose state
    // changed after it, or every one of his objects (`full`) when it is not a version of this process.
    json::Value apiNoticeToken(const std::string& account,int64_t now);
    Reply notices(const std::string& token,const std::string& since,int64_t now);
    // Text chat (README, "Text chat"): a game's notice token names the player. chatRead: `on`, `next` (what to ask for next
    // time) and the lines from `since` on, at most ChatBatch; without `since`, none (a game joining sees no history).
    // chatSay {"text":...}: one line under the player's name. Kept in memory only, the last ChatKept lines, and written
    // to the server's event log for its owner. 403 off or no good token, 400 no usable text, 429 too fast.
    Reply chatRead(const std::string& token,const std::string& since,int64_t now);
    Reply chatSay(const std::string& token,const std::string& body,int64_t now);
    // The other players for this player's map (README, "Players on the map"): with a notice token, `on` and, while the policy
    // says on, the players the relay placed in the last MapFreshSeconds: `id`, `name`, x, y, z (game axes), `yaw` and `area` when
    // known; the MapPlayersMax nearest to (x, y) when the game says where it is, only those of its `area` when it says that.
    // `wait_ms`: how long the game should wait before it asks again, longer the more players are placed.
    // Under identity keys his own entry is left out and his choice of who he is shown holds; elsewhere the game knows its id.
    Reply mapRead(const std::string& token,int64_t now,const std::string& x="",const std::string& y="",const std::string& area="");
    // What a game needs to show only this server's structures (policy `world`; ../mod/serverworld.h): the server's id, which
    // is the key of the ids it gives, and the ids of what this player may be handed that do not carry it (built before the
    // ids did, kept from an upload, or from a linked server). With the notice token.
    Reply worldIds(const std::string& token,int64_t now);
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
    struct PlayerInfo { std::string id,name,key,offerBuilds,offerOld;bool known=true,member=false,banned=false;size_t shared=0,pending=0,kept=0,held=0,remnants=0,imported=0;int64_t likes=0; };
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
    // The owner ends a player's sessions and passes (README, "Kick and ban"): his game's session, its sign-in
    // ticket, relay token and notice token. Under identity keys he is back only after a new start from his launcher.
    // Returns how many ended. `ban` does the same and, while it holds, refuses him everything that makes a session or a
    // pass and every launcher call (403); `ban(id,false)` lifts it. Saved with the world. Both throw for an unknown id.
    size_t kick(const std::string& id);
    void ban(const std::string& id,bool yes);
    bool banned(const std::string& account) const;     // by account key
    // The accounts of keys kicked or banned since the last call (under identity keys a relay id is someone's: made from
    // his key): the relay takes them off at once (relayEvictions in api.h, RelayConfig::evicted), not after its timeout.
    std::vector<std::string> takeEvictions(){std::lock_guard<std::mutex> g(lock_);auto out=std::move(evictions_);evictions_.clear();return out;}
    // The launcher's API (api.h): one account, by the key it signs in with. The documents are the API's answers;
    // apiChoices throws std::runtime_error with the reason when it is refused, and then changed nothing. It makes the
    // player when the key is new (as a sign-in would: the `players` setting applies), so his choices are in place
    // before his game first signs in.
    // Who is on the relay now (RelayConfig::online), for the policy's `online`. Not saved; without a relay the policy has none.
    void setOnline(std::vector<std::string> names){std::lock_guard<std::mutex> g(lock_);online_=std::move(names);onlineKnown_=true;}
    json::Value apiPolicy(int64_t now=0) const;          // with the server's clock (`time`), for the launcher's signatures
    json::Value apiPlayers() const;                       // the owner's: playerList and waiting as documents
    json::Value apiWaiting() const;
    json::Value apiMe(const std::string& key) const;
    json::Value apiMine(const std::string& key) const;
    void apiChoices(const std::string& key,const std::string& offerBuilds,const std::string& offerOld);
    // Who a player sees on the relay (README, "How many players one player sees"): his friend list (account keys; a
    // short key "a1b2 c3d4 e5f6 a7b8" names the one player of this world whose key begins so) and his choice
    // `show_others` everyone|friends|nobody|default ("" leaves it). Throws std::runtime_error with the reason when
    // refused, and then changed nothing; makes the player when the key is new, as apiChoices does.
    json::Value apiFriends(const std::string& key) const;
    // Collections (../docs/COLLECTIONS.md; collections.cpp). `relayId`: the player's relay id (made from his key under
    // identity keys, else the one his launcher names; 0 none), for his spot. Both throw Refused with the status and the
    // reason, and then changed nothing. `owner`: the call is signed with the owner's key (the import setting `owner`).
    json::Value apiCollection(const std::string& key,const std::vector<std::string>& objects,int64_t nearMetres,const std::string& name,uint64_t relayId,int64_t now);
    json::Value apiImport(const std::string& key,const std::string& text,const std::string& place,uint64_t relayId,bool owner,int64_t now);
    // Where a player's game is, from the relay (about once a second for each player who says where he is). Not saved.
    void noteSpot(uint64_t relayId,const Spot&);
    bool spotOf(uint64_t relayId,Spot&) const;
    void apiSetFriends(const std::string& key,const std::vector<std::string>& add,const std::vector<std::string>& remove,const std::string& show);
    // What the relay holds for a broadcast player (relay.h RelayViewer), by his relay id. Only under identity keys is a
    // relay id someone's (made from his key: identity::relayId): there, his choice ("" none) and the relay ids of the
    // friends on his list who have keys. Elsewhere nothing but the cap. `cap`: the policy's near_cap.
    struct Viewer { std::string show;std::set<uint64_t> friends;int64_t cap=0; };
    Viewer relayViewer(uint64_t relayId) const;
    // For tests.
    size_t objects() const {std::lock_guard<std::mutex> g(lock_);return objects_.size();}
    std::string playerOf(const std::string& token) const;
private:
    // offerBuilds, offerOld: "" when the player chose nothing (the policy's value applies).
    // friends: account keys; showOthers: everyone|friends|nobody, "" when he chose nothing here.
    struct Player { std::string id,name,key,offerBuilds,offerOld,showOthers;bool member=false,banned=false;std::set<std::string> friends; };
    // An imported object keeps where it was first built (originServer, originId), which server handed it over (via),
    // its builder's name there and the like count it arrived with; `likes` is that count plus what was given here.
    // `name` is the builder's name when it was built (kept for a remnant); `share` is "" for an object every player
    // is handed, else "kept" (its builder's only), "ask" (waits for its builder), "review" (waits for the owner) or
    // "held" (its builder's only because the policy does not let it be shared: `held` says why, build, member or
    // limit; `heldAsk`: its builder is still to be asked).
    // A remnant was withdrawn by its builder and stayed: nobody changes it, and its likes go to no one.
    // `home`: the display name of its first server as the record that brought it gave it ("" none given).
    // `kept`: why a kept object is kept: "choice" (its builder's), "owner" (the owner's review), "server" (accept_offers
    // never); "" in a world file from before. `stamp`: when its state last changed (stamps_; not saved).
    struct Object { std::string id,creator;json::Value post;int64_t updated=0;int64_t likes=0;
                    std::string originServer,originId,via,originName,home;int64_t originLikes=0;
                    std::string name,share,held,kept;bool remnant=false,heldAsk=false;uint64_t stamp=0;
                    std::string collection;bool costed=false;std::array<int64_t,6> cost{}; };   // imported from a collection: its source, what it carried
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
    std::string owner_;std::map<std::string,Pass> tickets_,relayTokens_,noticeTokens_;
    uint64_t stamps_=0;void touch(Object& o){o.stamp=++stamps_;}   // an object's state changed (what notices reads)
    std::string pass(std::map<std::string,Pass>& passes,const std::string& account,uint64_t relayId,int64_t now);
    Player& playerFor(const std::string& account,const char* why);   // made when new, within `players`
    size_t kickLocked(Player&);
    std::vector<std::string> evictions_,online_;bool onlineKnown_=false;
    struct ChatLine {uint64_t n=0;std::string name,text;};
    std::deque<ChatLine> chat_;uint64_t chatNext_=1;std::map<std::string,std::deque<int64_t>> chatTimes_;
    const Player* chatPlayer(const std::string& token,int64_t now);
    static Reply noticeTokenRefused();
    uint64_t relayIdOf(const std::string& account) const;   // under identity keys; 0 none
    Viewer viewerOf(uint64_t relayId) const;
    Reply signInLocked(const std::string& key,const std::string& displayName,const std::string& gateway);
    // What each player's game was handed per area (curate): kept so the same structures come back, saved with the world.
    struct Picks { int64_t asked=0;std::vector<std::string> ids; };
    std::map<std::string,std::map<int64_t,Picks>> picks_;   // player id -> area -> picks
    // Roads paved together: player id -> (paver, resource) -> what he put in; and the likes the players' paver
    // contributions earned (player id -> likes). Saved with the world.
    std::map<std::string,std::map<std::pair<int64_t,int64_t>,int64_t>> devoted_;
    std::map<std::string,int64_t> highwayLikes_;
    bool contributes(const Player&) const;          // do the other players get what he puts into pavers, now
    Candidate candidateOf(const Object&) const;
    void remember(const std::string& player,int64_t area,const std::vector<std::string>& ids,int64_t now);
    std::set<std::string> friendsOf(const std::string& player) const;   // the player ids of a player's friends (curate)
    std::string friendEntry(const std::string& key,const std::string& entry) const;   // an account key for a list entry, or it throws
    mutable std::map<std::string,uint64_t> relayIds_;    // account of a key -> its relay id (relayViewer)
    std::map<uint64_t,Spot> spots_;                       // relay id -> where his game last was (collections)
    std::string freshId(const std::string& type,const std::string& creator);
    std::string versionLocked() const {return epoch_+"-"+std::to_string(changes_);}
    bool servedTo(const Object&,const std::string& player) const;          // may this player be handed it
    bool handsBack(const Object&,const std::string& player) const;         // his own, handed to him because the world is the server's
    std::string shareAfterOffer() const;                                   // what an offered upload becomes
    std::string blocked(const Player&,bool counted) const;                 // why this player may not share one more ("": he may)
    size_t offered(const std::string& player) const;                      // his objects that count for the limit
    Object& ownObject(const std::string& player,const std::string& object);
    std::string importedId(char type,const std::pair<std::string,std::string>& origin) const;
    Shared sharedOf(const Object&) const;
    json::Value objectResponse(const Object&) const;
    // An upload's body as it is stored and served: the account names in its materials ("mt", "rmt": "c" of each
    // contributor) kept only for players of this world, every other one "" (README "An object's own id").
    json::Value servedPost(const json::Value& post) const;
    Reply gateway(const Player&,const std::string& name,const json::Value& in,int64_t now);
    const Player* playerById(const std::string& id) const;
    std::string shownCreator(const Object&) const;   // the builder the games are told (its collection builder for an import from a collection)
    void note(const std::string& line) const {if(log)log(line);}
};
// The player id for an account key: "zygo_" and 27 characters, the form the game's own ids have.
std::string playerId(const std::string& key);
// The builder the games are told for what a player imported from a collection (../docs/COLLECTIONS.md): a player id of
// the same form, one per importer and always the same for him (made from his player id), never a player's. `lookup`
// names it "Name · collection" (CollectionHome in the place of a server's name).
std::string collectionBuilderId(const std::string& importer);
constexpr const char* CollectionHome="collection";
}
