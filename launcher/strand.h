#pragma once
// The player's side of a group server's rules (README, "A group on one server"). Before his game connects, the
// launcher reads the server's policy (../world/api.h, GET /coop/policy) and shows it with his own choices for that
// server; it remembers them per server in launcher.ini and sends them (POST /coop/choices), then starts the game.
// "My strand on this server" lists what he built there (GET /coop/mine) and offers or withdraws it. Portable
// (Winsock on Windows, POSIX sockets elsewhere), so the game-free tests run it against the built coop-world.
#include "core.h"
#include "../world/identity.h"
#include <functional>
namespace launcher::strand {
// The server and the account a session's game signs in as there. The account key is the NAME of a /k/NAME sign-in
// address (two games on one PC, one Steam account), else the 64-bit Steam id of the Steam account (`steamAccount`,
// the 32-bit account number; 0 = not known, and then the key is empty). On a server whose identity is keys
// (identify()) it is the account of this install's key instead, and every call is signed with that key.
struct Target {
    std::string address,prefix,account;unsigned port=0;
    const world::identity::Keys* keys=nullptr;   // this install's key pair (player.key); its secret half is never sent
    bool sign=false;std::string server;int64_t clockOffset=0;   // signing: the world's id, the server's clock minus ours
};
std::string account(const Session&,uint32_t steamAccount);
Target target(const Session&,uint32_t steamAccount);
// The server's policy as it shows it: every setting with its value, the preset's value and one line in words.
struct Setting { std::string key,value,preset,text; };
struct Policy {
    std::string server,preset;int64_t players=0,limit=0,time=0;std::vector<Setting> settings;   // time: the server's clock
    bool keys() const {return value("identity")=="keys";}   // players are known by their keys there
    std::string value(const std::string& key) const;
    std::string text() const;   // lines for the screen
};
Policy parsePolicy(const std::string& body);   // throws for anything but a well-formed policy document
// One HTTP exchange: method, path (with the prefix), extra headers (a signed call's), body; false when no answer in
// HTTP form came.
struct Http { int status=0;std::string body; };
using Headers=std::vector<std::pair<std::string,std::string>>;
using Transport=std::function<bool(const std::string& method,const std::string& path,const Headers& headers,const std::string& body,Http& out)>;
std::string request(const Target&,const std::string& method,const std::string& path,const Headers& headers,const std::string& body);
bool parseResponse(const std::string& raw,Http& out);
Transport transport(const Target&,unsigned ms=3000);   // a TCP connection per exchange, at most `ms` for each step
// Reading the policy. Old: the server answers the game (its gateway knows the key) but has no /coop/ (a coop-world
// from before the launcher's API): it shares what everyone builds and has no choices. WrongKey: it does not know the
// group's key. Unreachable: no answer. Bad: an answer this launcher cannot read.
enum class Fetch : uint8_t {Ok,Old,WrongKey,Unreachable,Bad};
const char* fetchText(Fetch);
Fetch fetchPolicy(const Target&,const Transport&,Policy&,std::string* raw=nullptr);
// After the policy is read: on a server whose identity is keys, the calls from now on are signed with `t.keys` (its
// clock corrected by the server's `time`) and act for that key's account. false, with `why`, when the server takes keys
// and this launcher has none. A server without keys leaves the target as it was.
bool identify(Target&,const Policy&,int64_t localNow,std::string& why);
// What a game needs on a server that takes keys (world/README.md, "Identity"): a sign-in ticket for its online_server
// address and a relay token for its relay key, both short-lived and taken by a signed call just before the game
// starts; the relay id is the one made from the key (identity::relayId), checked against the server's.
struct Passes { std::string ticket,relayToken;uint64_t relayId=0; };
bool takePasses(const Target&,const Transport&,Passes&,std::string& why);
void usePasses(Session&,const Passes&);   // the game's config then signs in with the ticket and says HELLO with the token
// The host's own game, on the server his launcher just started: read its policy (up to `tries` times, half a second
// apart, while it starts), and on a server that takes keys take the passes. `keyed`: whether it does.
bool hostPasses(Target&,const Transport&,unsigned tries,Passes&,bool& keyed,std::string& why);
// The player's choices for one server: offer_builds yes|no|ask, offer_old yes|no.
struct Choices { std::string offerBuilds,offerOld; };
bool validChoices(const Choices&);
Choices defaults(const Policy&);                                     // what the server gives a player who chose nothing
std::string choicesText(const Choices&);
// launcher.ini: [launcher] holds the settings, [server-ID] the choices for the server whose world has that id.
std::string iniGet(const std::string& ini,const std::string& section,const std::string& key);
std::string iniSet(const std::string& ini,const std::string& section,const std::string& key,const std::string& value);
Choices remembered(const std::string& ini,const Policy&);          // this server's remembered choices, else its defaults
std::string remember(const std::string& ini,const std::string& server,const Choices&);
bool sendChoices(const Target&,const Transport&,const Choices&,std::string& why);
// The join after the screen (or without one): read the policy, take the remembered choices with `given` over them,
// remember and send them; on a server that takes keys sign them and take the game's passes. `start`: whether the game
// may start; `message`: what to tell the player; `target`: the target the calls went to (identified).
struct Join { Fetch fetched=Fetch::Bad;Policy policy;Choices choices;std::string ini,message,raw;bool start=false,keyed=false;Target target;Passes passes; };
// `game` false: the choices only (My strand on this server, while the game may be running: new passes would end its own).
Join join(const Target&,const Transport&,const std::string& ini,const Choices& given,bool game=true);
// What the player has on the server.
struct Me {
    bool known=false,member=false;std::string id,offerBuilds,offerOld;   // his choices ("" = the server's)
    int64_t shared=0,waitingYou=0,waitingOwner=0,held=0,kept=0,remnants=0,likes=0;
    std::string text() const;
};
struct Object { std::string id,kind,state,waitingFor,held;int64_t area=0,likes=0;std::string text() const; };
bool fetchMe(const Target&,const Transport&,Me&,std::string& why);
bool fetchMine(const Target&,const Transport&,std::vector<Object>&,std::string& why);
// Offer (`call` "offer") or withdraw ("withdraw") one of his objects, or all of them (`object` ""). `result` is what
// happened, or why it was refused.
bool act(const Target&,const Transport&,const std::string& call,const std::string& object,std::string& result);
}
