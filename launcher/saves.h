#pragma once
// Co-op saves the launcher moves around: the guest can start from the host's save, and a player can copy the ordinary
// saves into the co-op data once. Portable and game-free; the window code and the lobby socket only carry the bytes.
//
// Save container and stream cipher: after mi5hmash/DeathStrandingSaveDataResigner (MIT), re-implemented here.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace launcher::saves {
inline constexpr size_t MaxSave=16u<<20,ChunkBytes=1024,TitleMax=160;
inline constexpr const char* InstallFolder="manualsave0";
uint32_t crc32(const std::string& bytes);

// ---- the file ----
// A checkpoint is three decimal sizes, a text header, a picture and the encrypted block; a profile is the block alone.
// The block names the account it belongs to ("steam_<64-bit id>") and the game refuses another account's file.
struct Info { bool checkpoint=false;std::string user,title; };   // title: the header's Title, decoded, at most TitleMax bytes
Info inspect(const std::string& file);                            // throws when this is not a save
std::string resign(const std::string& file,const std::string& user);
std::string userForAccount(uint32_t account);                     // the folder name under DeathStrandingDC is the account
bool saveFolder(const std::string& name);                         // autosaveN, manualsaveN, quicksaveN

// ---- folders ----
// <local>/KojimaProductions/DeathStrandingDC, whatever the letter case on disk (the co-op tree is lower case).
std::filesystem::path gameFolder(const std::filesystem::path& local);
std::vector<uint32_t> accounts(const std::filesystem::path& gameFolder);
struct Checkpoint { std::string folder,bytes,title; };
std::optional<Checkpoint> newest(const std::filesystem::path& account);   // by the checkpoint's last write time
// That one folder's checkpoint, or nullopt with the reason (never another folder's: the caller asked for this one).
std::optional<Checkpoint> named(const std::filesystem::path& account,const std::string& folder,std::string* why=nullptr);
// Guest: the host's checkpoint becomes this account's only save (InstallFolder), re-signed for `accountId`. The saves
// that were there move to `aside`, never deleted. Returns how many moved; -1 when the same save was already in place.
int install(const std::filesystem::path& account,uint32_t accountId,const std::string& hostFile,const std::filesystem::path& aside);
// Copy the ordinary saves (read only) into the co-op account folder; what was there moves to `aside`. The profile
// (settings and key bindings) is copied only when the co-op side has none. Returns the checkpoints copied.
int importOrdinary(const std::filesystem::path& from,const std::filesystem::path& to,const std::filesystem::path& aside);

// ---- the transfer, on the lobby's own socket ----
// The guest pulls: query -> offer (size, checksum, title), then get(index) -> chunk. The host keeps no state per guest,
// so loss, repeats and reordering only cost a repeated request. Every datagram carries the lobby's secret and nonce.
class Sender {
    std::string file,title;uint32_t sum=0;
public:
    void offer(const std::optional<Checkpoint>& c);               // nothing to offer: the answer says size 0
    bool offering() const {return !file.empty();}
    std::string answer(const std::string& datagram,uint32_t secret,uint32_t nonce) const;   // "" = not ours, say nothing
};
class Receiver {
    std::vector<uint8_t> have;size_t got=0;uint32_t sum=0,cursor=0;std::string file;
public:
    bool offered=false,done=false;uint32_t size=0;std::string title;
    // What to send now: the query until the host has answered, then (only when `wanted`) requests for up to `budget`
    // missing chunks.
    std::vector<std::string> poll(uint32_t secret,uint32_t nonce,bool wanted,int budget);
    void take(const std::string& datagram,uint32_t secret,uint32_t nonce);
    int percent() const {return have.empty()?0:int(got*100/have.size());}
    const std::string& bytes() const {return file;}                // complete and checked once `done`
};
}
