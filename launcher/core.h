#pragma once
#include <filesystem>
#include <string>
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
};
std::string config(const Session&);
void atomicWrite(const std::filesystem::path&,const std::string&);
void writeConfig(const std::filesystem::path& data,const Session&);
std::string ticket(const release_startup::Values&,const std::string& target,uint64_t now);
void backup(const std::filesystem::path& data,const std::filesystem::path& destination);
// Tiny launcher-only two-player rendezvous; CPQ2/CPS2 remains the standard discovery wire.
enum class Kind : uint8_t { join=1, accepted, ready, start, started, leave };
struct Packet { Kind kind=Kind::join;uint32_t secret=0,nonce=0;unsigned port=49000;bool ready=false; };
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
