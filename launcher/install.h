#pragma once
// The one install logic of the co-op mod (../install/README.md): what goes where, the state Setup's one button shows,
// install, update, repair and uninstall with receipts, and finding the game in every Steam library. Portable C++17 on
// std::filesystem, no Windows calls, so it builds and is tested on Linux (install_test.cpp, with mutants). Its two
// callers: the launcher's window (offer to install, About "Remove co-op from the game") and Setup, which runs
// `deathstranding-coop.exe --setup status|install|uninstall` (main.cpp) and draws the result (../dist/installer-gui.ps1).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace launcher::install {
namespace fs=std::filesystem;
// Exit codes every co-op Setup uses (co-op mod standard J9).
enum Exit : int {Ok=0,Failed=1,Usage=2,UnsupportedBuild=3,GameRunning=4};
struct Error : std::runtime_error {int code;Error(const std::string& m,int c=Failed):std::runtime_error(m),code(c){}};
inline constexpr const char* Id="death-stranding-coop";
inline constexpr const char* ManifestName="coop-mod.json";
inline constexpr const char* ReceiptSuffix=".coop-receipt.json";
inline constexpr const char* GameExe="ds.exe";
inline constexpr const char* SteamApp="1850570";
// Every build of the co-op proxy carries this text (the start-ticket keys, ../redirect/startup_policy.h): an avrt.dll
// with it and without a receipt is an older co-op proxy (the launcher installed without receipts before), not another mod's.
inline constexpr const char* ProxyMarker="COOP_REDIRECT_INSTANCE";

// coop-mod.json (../tools/mod_manifest.py, standard J5). `files` names where each file goes: root "game" (the game
// folder) or "data" (%LOCALAPPDATA%\DeathStrandingCoop). A data path starting "app/" is the launcher's own folder; its
// file comes from the package without the "app/" (the package is the launcher's folder, so the installed launcher sees
// its own package and the game's proxy and mod compare against it).
struct Entry {std::string root,path,sha256;uint64_t size=0;};
struct Manifest {std::string id,version;std::vector<std::string> builds;std::vector<Entry> files;};
Manifest readManifest(const fs::path& package);   // Error(Usage) when it is missing or not a schema 1 manifest of this mod

// A receipt sits beside every installed file (<file>.coop-receipt.json): schema 1, id, version, the SHA-256 Setup put
// there ("installed") and, while a replacement is under way, the one it is putting there ("pending").
struct Receipt {bool present=false,valid=false;std::string version,installed,pending;};
Receipt readReceipt(const fs::path& target);

// One file of the plan, as found on disk.
enum class File : uint8_t {
    Missing,     // not there
    Current,     // the package's bytes
    Outdated,    // what an earlier Setup put there (its receipt says so): Update replaces it
    Ours,        // no receipt, but ours: an older co-op proxy (ProxyMarker), or a file in the co-op data folder
    Adoptable,   // the package's bytes without a receipt (copied by hand, or by a mod manager)
    Modified,    // has a receipt, but the player (or something) changed it since: never replaced or removed
    Foreign      // another program's file with our name (another mod's avrt.dll): never replaced or removed
};
const char* fileName(File);
File classify(bool exists,const std::string& observed,const Receipt&,const std::string& expected,bool ours);

// What Setup's one button says (standard G2) and `status` reports (J9 state names).
enum class State : uint8_t {NotAGameFolder,UnsupportedBuild,NotInstalled,UpToDate,UpdateAvailable,NeedsRepair,Conflict,ByModManager};
const char* stateName(State);
const char* button(State);   // Install, Update, Repair, Up to date, Needs attention
// `linked`: a file without a receipt has more than one hard link (a mod manager's deployment, Vortex);
// `versions`: a receipt names another version than the package's.
State decide(const std::vector<File>& files,bool linked,bool versions);

// Where things go. `app`: Setup's install also puts the launcher's folder into data/app; the launcher's own window
// leaves it out (the package is the folder it runs from).
struct Layout {fs::path package,game,data;bool app=true;};
struct Item {std::string root,path;fs::path source,target;std::string expected,observed;Receipt receipt;File state=File::Missing;};
struct Report {
    State state=State::NotInstalled;
    fs::path game;
    std::string version,installedVersion,gameSha256,build,installedBy;
    std::vector<Item> items;std::vector<std::string> problems;
};
Report status(const Layout&);   // reads only
std::string reportJson(const Report&);
// Install, update or repair: all or nothing. Refuses (Error with the J9 code) a folder without ds.exe, another build,
// a conflict (a foreign or changed file is never replaced), a running game and a damaged package. Changes nothing
// when the state is up to date or a mod manager's.
Report install(const Layout&,const std::function<bool()>& running);
// Remove what is ours: files whose receipt still matches them, and ours without a receipt (Ours, Adoptable). Changed
// files are kept (`kept`, exit 1); another program's files are left alone (`foreign`). The co-op data (saves,
// settings, logs) stays; only the launcher's folder (data/app) goes when it is empty.
struct Removed {std::vector<std::string> removed,kept,foreign;};
Removed uninstall(const Layout&,const std::function<bool()>& running);

// Finding the game (standard G1). Steam roots come from the registry (SteamPath, InstallPath); every library they list in
// steamapps/libraryfolders.vdf (both the current and the old format) is searched for appmanifest_1850570.acf, whose
// "installdir" names the game folder under steamapps/common. `extra`: folders known otherwise (Steam's uninstall
// entry). Only folders holding ds.exe are returned, each once (Windows paths compare without letter case).
std::vector<std::string> libraryFolders(const std::string& vdf);
std::string installDir(const std::string& acf);
std::vector<fs::path> findGames(const std::vector<fs::path>& steamRoots,const std::vector<fs::path>& extra);
std::string sha256File(const fs::path&);
}
