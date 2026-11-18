#pragma once
// The player's side of a group server's rules (README, "A group on one server"). Before his game connects, the
// launcher reads the server's policy (../world/api.h, GET /coop/policy) and shows it with his own choices for that
// server; it remembers them per server in launcher.ini and sends them (POST /coop/choices), then starts the game.
// "My strand on this server" lists what he built there (GET /coop/mine) and offers or withdraws it. Portable
// (Winsock on Windows, POSIX sockets elsewhere), so the game-free tests run it against the built coop-world.
#include "core.h"
#include "../world/identity.h"
#include <filesystem>
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
    std::string name;   // the player's name in the launcher: the server shows it, never the Steam name
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
std::string base(const Target&);                       // "/PREFIX", or "" without a prefix
// One call to the API (path with the prefix): signed with the target's key when `sign` is set (identify(), or the
// owner's calls: owner.h), on the server's clock. false when no answer in HTTP form came.
bool call(const Target&,const Transport&,const std::string& method,const std::string& path,const std::string& body,Http& out);
std::string message(const Http&);                      // the server's reason in its answer, else its status
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
// `noticeToken`: what the game is told (../mod/README.md "Strand notices"), under every identity: the token its mod reads
// this player's builds' states with. Taken after the passes; a server that gives none (an older one) only means no
// such notices, never a game that does not start.
struct Passes { std::string ticket,relayToken;uint64_t relayId=0;std::string noticeToken; };
bool takePasses(const Target&,const Transport&,Passes&,std::string& why);
bool takeNoticeToken(const Target&,const Transport&,std::string& token,std::string& why);
void usePasses(Session&,const Passes&);   // the game's config then signs in with the ticket and says HELLO with the token
void useNoticeToken(Session&,const Passes&);   // the game's config then reads its builds' states (any identity)
// What the server's rules ask of every game there: test_mode on starts the game's mod in test mode (Session::
// serverTestMode). A server without the setting (older) or without rules: off.
void usePolicy(Session&,const Policy&);
// The host's own game, on the server his launcher just started: read its policy (up to `tries` times, half a second
// apart, while it starts), on a server that takes keys take the passes, and a notice token. `keyed`: whether it does.
bool hostPasses(Target&,const Transport&,unsigned tries,Passes&,bool& keyed,std::string& why,Policy* read=nullptr);   // read: the policy as read
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
// Who he sees of the players near him on the server's relay (../world/README.md, "How many players one player sees"):
// his choice `show` (everyone, friends, nobody; "" = his game's own setting, everyone when it has none) and his friends.
// A friend is added by the short form of his key ("a1b2 c3d4 e5f6 a7b8", which the server finds among its players) or
// by his whole account key, and removed the same way.
struct Friend { std::string key,shortKey,name;bool known=false;std::string text() const; };
struct Friends { std::string show;std::vector<Friend> list; };
bool fetchFriends(const Target&,const Transport&,Friends&,std::string& why);
// One change: add and remove (lists of keys), show ("" leaves it, "default" goes back to his game's). `out` is the list
// as the server then has it; `why` the server's reason when it refuses.
bool changeFriends(const Target&,const Transport&,const std::vector<std::string>& add,const std::vector<std::string>& remove,const std::string& show,Friends& out,std::string& why);
std::string shortKeyText(const std::string& typed);   // "A1B2C3D4 e5f6a7b8" -> "a1b2 c3d4 e5f6 a7b8"; "" when it is no short key
// Offer (`call` "offer") or withdraw ("withdraw") one of his objects, or all of them (`object` ""). `result` is what
// happened, or why it was refused.
bool act(const Target&,const Transport&,const std::string& call,const std::string& object,std::string& result);
// Collections (../docs/COLLECTIONS.md): a DSK string made by the server from his own objects (named rows, or everything
// within `nearMetres` of where his game is), and a DSK string imported as his structures, at the original place or
// here. `relayId`: his game's relay id (the launcher's player number; under keys the server takes his key's own).
struct Made { std::string text,description;int64_t count=0;std::vector<std::string> leftOut;std::string message() const; };
struct Imported { int64_t imported=0,shared=0,held=0,kept=0,waiting=0;std::string source,description;std::vector<std::string> leftOut;std::string message() const; };
bool makeCollection(const Target&,const Transport&,const std::vector<std::string>& objects,int64_t nearMetres,const std::string& name,uint64_t relayId,Made&,std::string& why);
bool importCollection(const Target&,const Transport&,const std::string& text,const std::string& place,uint64_t relayId,Imported&,std::string& why);
// A pasted text or a .dsk file's contents: the DSK string in it, checked, and what it holds in words. false with `why`.
bool readCollection(const std::string& pasted,std::string& text,std::string& description,std::string& why);
// A .dsk file in `folder` (made when missing): "<name or collection>-<UTC yyyymmdd-hhmmss>.dsk", the string and a line
// break, written whole (temporary file, then rename). Returns the path, or "" with `why`.
std::string saveCollection(const std::filesystem::path& folder,const std::string& text,const std::string& name,int64_t utcSeconds,std::string& why);
bool loadCollection(const std::filesystem::path& file,std::string& text,std::string& description,std::string& why);
std::string collectionFileName(const std::string& name,int64_t utcSeconds);
}
