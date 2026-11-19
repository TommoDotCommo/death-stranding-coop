#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "vendor/coop_discover.h"
#include "../mod/config.h"
#include "../redirect/startup_policy.h"
#include "../world/identity.h"

namespace launcher {
inline constexpr const char* GameId="DSDC";
inline constexpr const char* Prefix="DS";
inline constexpr const char* GroupPrefix="DSG";   // a group invite: DSG-..., the same payload as a lobby invite
// Group play (README "A group on one server"): no partner and no lobby. One player hosts the group's server
// (coop-world with its relay); every player's game talks to that server only, and plays its own save.
enum class Group : uint8_t {None,Host,Join};
inline constexpr unsigned GroupOthers=15;        // other players drawn at once (each is a full body): the nearest; a server holds far more
inline constexpr unsigned GroupCarried=4;        // the nearest players whose carried cases are drawn (a full load is many meshes)
// A player's number on the relay: made once from the system's random numbers and kept in the launcher's settings, so
// it is the same in every session and says nothing about the player (never the Steam id). 1..PlayerIdMax: the mod's
// config takes integers of at most 15 digits.
inline constexpr uint64_t PlayerIdMax=999999999999999ull;
inline bool playerIdValid(uint64_t id){return id>=1&&id<=PlayerIdMax;}
inline uint64_t playerId(uint64_t random) {
#ifdef MUTANT_GROUP_ID
    return random;
#else
    return (random&((uint64_t(1)<<49)-1))+1;   // 49 random bits: 1..562949953421312
#endif
}
struct Session {
    bool host=true;
    std::string peer="127.0.0.1",name="Porter";
    unsigned port=49000;
    uint32_t secret=1;
    bool solo=false;   // "Play solo": nobody is expected, so the session notices (and their "no partner yet" hint) stay off
    // Opt-in: the game's own online layer (other players' structures, signs, likes) on the group's own server
    // (world/README.md). The host's launcher runs the server; both games get its address as `online_server`.
    bool sharedWorld=false;
    // Group play. `secret` is the group's key (session number, server path and relay key all come from it), `port`
    // 49000 + the server's slot (its TCP and UDP port is 49100 + slot), `server` the address this game reaches the
    // server at (Join; the host's own game uses loopback) and `playerId` this player's number on the relay.
    Group group=Group::None;
    std::string server="127.0.0.1";
    uint64_t playerId=0;
    // The rules of the group's server when the host's launcher makes a new world: friends, solo or public
    // (../world/README.md, "Server policy"). A world that exists keeps its own.
    std::string policy="friends";
    // Identity keys (../world/README.md, "Identity"): on a server that takes keys the game signs in with a ticket and
    // says HELLO with a relay token, both taken by the launcher's signed call just before the game starts (strand.h),
    // and its relay id (`playerId`) is the one made from the key. The host gives his server his key as the owner's.
    std::string signInTicket,relayToken,ownerKey;   // ownerKey: 64 hex digits
    // What the game is told (../mod/README.md, "Strand notices"): a notice token the launcher took for this player on the
    // group's server, under every identity; with it the game's config turns strand_notices on. Empty: none (the server
    // is older, or did not give one), and the game starts without them.
    std::string noticeToken;
    // Group play: other players' structures the server hands this game are built at once (`build_received`), skipping
    // the game's spacing and area checks for them. The player's setting, on unless he turns it off.
    bool buildReceived=true;
    // Other players drawn as full bodies at once (the nearest), the player's choice in Settings: each costs the game frame
    // time; the rest show on the map. At most the mod's 32.
    unsigned othersDrawn=GroupOthers;
    // A route challenge the player chose in Challenges (../docs/ROUTES.md): "DSR1-..." written into the game's config as
    // `route`, in every kind of session; "" none.
    std::string route;
    // Test mode (../mod/README.md "Test mode": material crates on U, unlimited stamina and blood). testMode: the player's
    // own Settings choice "Test mode (alpha)", for his solo and two-player games. serverTestMode: in group play the
    // server's rules decide (the policy setting test_mode, read before the game starts: strand::usePolicy); the
    // player's own choice does not count there.
    bool testMode=false,serverTestMode=false;
    // The server's id where its rule `world` says server: its game shows only that server's structures
    // (../mod/README.md, "A world that is the server's"); "" keeps the save's.
    std::string serverWorld;
    bool serverFreeBuild=false;   // group play: the server's rule free_build (../mod/README.md, "Free build")
};
// The mod config's test_mode object: every switch at its default (U drops crates; stamina and blood kept full).
inline constexpr const char* TestModeConfig=",\"test_mode\":{}";
// This install's key pair: made once and kept in `file` (player.key, beside launcher.ini). Its secret half is never
// sent anywhere; the public half is the player on servers that take keys. A file that is there but cannot be read is
// an error, never replaced (a new key would be a new player).
world::identity::Keys playerKeys(const std::filesystem::path& file);
// The addresses the proxy's network policy lists for the session (COOP_NETWORK_PEERS): the partner, or in group play
// the server and nothing else.
std::string networkPeers(const Session&);
// Group play: the relay's address for the mod ("ADDRESS:PORT", UDP, the same number as the server's TCP port) and
// the relay key (the server's secret path element).
std::string relayAddress(const Session&);
// A group invite: "DSG-" and the lobby invite's payload, with the group's key as the secret, the server's port
// (49100..49114, TCP and UDP) and the addresses the host can be reached at, best first. Empty when it cannot be made.
std::string groupInvite(uint32_t secret,unsigned serverPort,const std::vector<ci_route>& routes);
// One pasted text (the kit's recogniser): `group` says a group invite (DSG-...), otherwise as coop_recognise for DS.
int recognise(const std::string& text,cr_result& result,bool& group);
// Which of a group invite's routes to use: the first one, in the host's order, whose server answered. -1: none.
int groupRoute(const ci_invite&,const std::vector<bool>& answered);
// The joiner's session from a group invite and the chosen route (name and player id from `mine`). Throws on an invite
// that is not a group's (port outside 49100..49114, no such route, no key).
Session groupJoin(const ci_invite&,int route,Session mine);
// The shared world's server for a session: its TCP port (49100 + the session's slot), the secret path element both
// players derive from the session secret, the address a game signs in to (the host's game on loopback, the guest's at
// the host; on one PC, where both games are the same Steam account, the address names the player), and the server's
// command line (host only; it listens beyond loopback only when the partner is on another PC). In group play the
// host's command line also turns the relay on, with the same key, and listens on every address; a joiner on the
// host's own PC (one Steam account) signs in under a name made from its player id.
unsigned worldPort(const Session&);
std::string worldPrefix(const Session&);
std::string worldAddress(const Session&);
std::vector<std::string> worldArguments(const Session&,const std::string& dataFile);
// The session's shared-world choice is the host's: a guest takes it from the host's lobby packets, whatever its own
// setting says.
inline bool sessionWorld(bool host,bool ownSetting,bool hostSaid) {
#ifdef MUTANT_WORLD_GUEST_DECIDES
    (void)host;(void)hostSaid;return ownSetting;
#else
    return host?ownSetting:hostSaid;
#endif
}
std::string config(const Session&);
// launcher.ini with the [launcher] settings set to `values`, every other section (the choices kept per server,
// strand.h) as it was.
std::string settingsText(const std::string& ini,const std::vector<std::pair<std::string,std::string>>& values);
void atomicWrite(const std::filesystem::path&,const std::string&);
void writeConfig(const std::filesystem::path& data,const Session&);
std::string ticket(const release_startup::Values&,const std::string& target,uint64_t now);
void backup(const std::filesystem::path& data,const std::filesystem::path& destination);
// A plain-text report for a bug report: what the launcher knows (`header` lines from the caller), the mod config, the
// files in the co-op data folder and the ends of the newest mod, proxy and crash logs. Nothing private leaves in it:
// the session secret and invite codes are hidden, other people's addresses keep their first two numbers only, and the
// Windows user name in paths becomes <user>. At most DiagnosticsMax bytes. Reads only inside `data`.
// Will Windows Firewall let the game receive the partner's packets? Rules as Windows lists them; the verdict follows
// Windows: among the enabled inbound rules that name the game's executable, UDP (or any protocol), the port and
// an active network profile, a block rule wins over an allow rule; with no such rule Windows asks when the game first
// listens (and blocks until answered).
enum class Firewall : uint8_t {Unknown,Off,Allowed,Blocked,NoRule};
struct FirewallRule {std::string application;bool enabled=false,allow=false,inbound=false;int protocol=256;std::string localPorts;unsigned profiles=0;};
Firewall firewallVerdict(const std::vector<FirewallRule>& rules,const std::string& gameExe,bool firewallOn,unsigned activeProfiles,unsigned port);
const char* firewallText(Firewall);   // one sentence for the player
inline constexpr size_t DiagnosticsMax=256*1024;
std::string redact(const std::string& text);
std::string diagnostics(const std::filesystem::path& data,const std::string& header);
// Tiny launcher-only two-player rendezvous; CPQ2/CPS2 remains the standard discovery wire.
enum class Kind : uint8_t { join=1, accepted, ready, start, started, leave };
struct Packet { Kind kind=Kind::join;uint32_t secret=0,nonce=0;unsigned port=49000;bool ready=false;
    bool world=false; };   // world: the sender's "Shared world" setting; the host's decides for the session
std::string packet(const Packet&);
bool parsePacket(const std::string&,Packet&);
bool allowJoin(const Packet&,uint32_t secret,bool inviteOnly,bool occupied);
// Setup from the launcher (offer to install, About "Remove co-op from the game") and Setup itself share one install
// logic: install.h.
inline bool backupAllowed(bool inLobby,bool running) {
#ifdef MUTANT_BACKUP_RUNNING
    (void)running;return !inLobby;
#else
    return !inLobby&&!running;
#endif
}
}
