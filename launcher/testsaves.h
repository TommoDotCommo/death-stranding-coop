#pragma once
// Test saves: a pack of ready-made co-op saves the player can start from (Saves tab, "Test saves"). The pack is a
// separate download beside each release (deathstranding-coop-test-saves-<version>.zip), never part of the repository;
// tools/make_test_saves.py builds it. Portable and game-free; the window and the command line only call it.
//
// The pack: test-saves.json at its root (schema 1, below) and one folder per set holding checkpoint folders
// (<set id>/<autosaveN|manualsaveN|quicksaveN>/checkpoint.dat), nothing else. Read straight from the zip (entries
// stored, not compressed: the saves are encrypted and do not compress) or from an extracted folder. Every file is
// checked against the manifest's size and SHA-256 before it is used.
//
//   {"schema":1,"version":"0.1.0-alpha.1","sets":[{"id":"ep3-28h","title":"...","description":"...","episode":3,
//    "play_time":"28 h","source":"...","files":[{"path":"ep3-28h/manualsave0/checkpoint.dat","sha256":"...",
//    "size":123}]}]}
//
// The first file of a set is the checkpoint Continue should load: it is written last.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace launcher::testsaves {
inline constexpr const char* Manifest="test-saves.json";
inline constexpr const char* FolderName="test-saves";                       // <launcher folder>\test-saves\ (extracted)
inline constexpr const char* ZipPrefix="deathstranding-coop-test-saves-";   // <prefix><version>.zip beside the launcher
inline constexpr const char* ReleasePage="https://github.com/TommoDotCommo/death-stranding-coop/releases";
inline constexpr size_t ManifestMax=1u<<20,SetsMax=200,FilesMax=64,DescriptionMax=2000;

// A pack that cannot be used as it is (missing pieces, a file that does not match the manifest, a file the manifest
// does not list): the command line's exit code 3.
struct PackError:std::runtime_error {using std::runtime_error::runtime_error;};

std::string sha256(const std::string& bytes);   // lower-case hex

struct File {std::string path,sha256;uint64_t size=0;};
struct Set {std::string id,title,description,playTime,source;int episode=0;std::vector<File> files;};   // episode 0: not given
class Pack {
    struct Entry {uint64_t offset=0,size=0;uint32_t crc=0;};
    std::filesystem::path where;bool zip=false;std::map<std::string,Entry> entries;
    std::string raw(const std::string& path) const;
public:
    std::string version;std::vector<Set> sets;
    // A zip (by its extension) or a folder with test-saves.json. Throws PackError when it is not a usable pack.
    static Pack open(const std::filesystem::path&);
    const std::filesystem::path& location() const {return where;}
    const Set* find(const std::string& id) const;
    std::string read(const File&) const;   // the bytes, size and SHA-256 checked (PackError otherwise)
};
// The manifest alone (also what the pack's files are checked against). Throws PackError.
void parseManifest(const std::string& text,std::string& version,std::vector<Set>& sets);
// Where the pack is: <launcher folder>\test-saves\ with its manifest, else the newest-version
// deathstranding-coop-test-saves-*.zip beside the launcher. nullopt: there is none.
std::optional<std::filesystem::path> find(const std::filesystem::path& launcherFolder);
// "0.1.0-alpha.10" after "0.1.0-alpha.9": digit runs compare as numbers.
bool versionBefore(const std::string& a,const std::string& b);

// The co-op account folder test saves go into: <data>\redirect-data\local\KojimaProductions\DeathStrandingDC\<account>.
// Never the ordinary saves under %LOCALAPPDATA%\KojimaProductions (`local` is that folder's parent, only for the check).
std::filesystem::path coopAccount(const std::filesystem::path& data,const std::filesystem::path& local,uint32_t accountId);

// One set, installed: its checkpoints re-signed for `accountId` replace the co-op saves of `account`, which move to
// `aside` first (never deleted). Every file is read and checked before anything changes. Returns the saves moved.
int use(const Pack&,const Set&,const std::filesystem::path& account,uint32_t accountId,const std::filesystem::path& aside);

// The command line (and the window's) steps: exit codes 0 done, 1 failed, 3 pack missing or damaged, 4 the game (or
// a lobby) is open. `allowed` is backupAllowed(inLobby, gameRunning) from the caller. `now` names the backup folder.
inline constexpr int ExitOk=0,ExitFailed=1,ExitPack=3,ExitRunning=4;
struct Outcome {int code=ExitFailed;std::string message;std::filesystem::path aside;int moved=0;};
Outcome useCommand(const std::optional<std::filesystem::path>& pack,const std::string& id,const std::filesystem::path& data,
                   const std::filesystem::path& local,uint32_t accountId,bool allowed,long long now);
Outcome listCommand(const std::optional<std::filesystem::path>& pack);   // message: "id<TAB>title" lines
}
