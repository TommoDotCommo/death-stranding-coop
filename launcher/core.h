#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "vendor/coop_discover.h"
#include "../mod/config.h"
#include "../redirect/startup_policy.h"

namespace launcher {
inline constexpr const char* GameId="DSDC";
inline constexpr const char* Prefix="DS";
struct Session {
    bool host=true;
    std::string peer="127.0.0.1",name="Porter";
    unsigned port=49000;
    uint32_t secret=1;
    bool solo=false;   // "Play solo": nobody is expected, so the session notices (and their "no partner yet" hint) stay off
    // Opt-in: the game's own online layer (other players' structures, signs, likes) on the group's own server
    // (world/README.md). The host's launcher runs the server; both games get its address as `online_server`.
    bool sharedWorld=false;
};
// The shared world's server for a session: its TCP port (49100 + the session's slot), the secret path element both
// players derive from the session secret, the address a game signs in to (the host's game on loopback, the guest's at
// the host; on one PC, where both games are the same Steam account, the address names the player), and the server's
// command line (host only; it listens beyond loopback only when the partner is on another PC).
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
void atomicWrite(const std::filesystem::path&,const std::string&);
void writeConfig(const std::filesystem::path& data,const Session&);
std::string ticket(const release_startup::Values&,const std::string& target,uint64_t now);
void backup(const std::filesystem::path& data,const std::filesystem::path& destination);
// A plain-text report for a bug report: what the launcher knows (`header` lines from the caller), the mod config, the
// files in the co-op data folder and the ends of the newest mod, proxy and crash logs. Nothing private leaves in it:
// the session secret and invite codes are hidden, other people's addresses keep their first two numbers only, and the
// Windows user name in paths becomes <user>. At most DiagnosticsMax bytes. Reads only inside `data`.
// Will Windows Firewall let the game receive the partner's packets? Rules as Windows lists them; the verdict follows
// Windows: among the enabled inbound rules that apply to the game's executable, UDP (or any protocol), the port and
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
inline bool backupAllowed(bool inLobby,bool running) {
#ifdef MUTANT_BACKUP_RUNNING
    (void)running;return !inLobby;
#else
    return !inLobby&&!running;
#endif
}
}
