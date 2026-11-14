#pragma once
// Story resync (story.cpp; opt-in story_resync, an experiment). The host's save is the story. When the host's story has
// moved (another episode, a main or sub order completed) or the guest asks with a key, the host's game writes a
// checkpoint with its own save request, the file travels over the mod's link, the guest's mod puts it in place
// re-signed for the guest's account and asks the guest's game to load it with the request the pause menu's Load Game
// makes (game_offsets.md, "Saving and loading on request"). Nothing here writes story memory.
//
// The news goes on the ordered event stream (type 22); the bytes go as the launcher's save datagrams (launcher/saves.h:
// query, offer, get, chunk) on the mod's own socket, keyed by the session and the checkpoint's generation.
#include "discovery.h"
#include "../core/core.h"
#include "../launcher/saves.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace mod::story {
constexpr uint8_t WireType=22;
enum : uint32_t { Ready=1,   // host: checkpoint `generation` can be fetched (size, sum)
                  Ask=2,     // guest: the catch-up key was pressed, please make one
                  Loaded=3,  // guest: generation loaded, the porter is back in the world
                  Failed=4 };// guest: generation could not be installed or loaded
struct News { uint32_t what=0,generation=0,size=0,sum=0; };
ds::CargoEvent encode(const News&);
std::optional<News> decode(const ds::CargoEvent&);    // nullopt: not story news, or out of range
uint32_t secretOf(uint64_t session);                  // never 0
uint32_t nonceOf(uint64_t session,uint32_t generation);

// ---- host ----
struct Stamp { std::string folder; int64_t time=0; uint64_t size=0;
    bool operator==(const Stamp& o) const {return folder==o.folder&&time==o.time&&size==o.size;} };
struct HostIn { uint64_t now=0; bool connected=false,inWorld=false,saveBusy=false,asked=false;
    uint32_t episode=ds::UnknownProgress,completed=0;   // completed: story orders seen completed (orders.h StoryWatch)
    std::optional<Stamp> newest; };                    // the newest checkpoint on disk
struct HostOut { bool save=false,offer=false; uint32_t generation=0; std::vector<std::string> log; };
class Host {
public:
    // Gap: between two checkpoints the mod asks for. SaveWait: how long the game may take to write one (it waits by
    // itself until saving is allowed). Settle: the file must have stopped changing for this long.
    static constexpr uint64_t Gap=30000,SaveWait=120000,Settle=2000;static constexpr unsigned Tries=3;
    explicit Host(bool automatic=false):automatic_(automatic){}
    HostOut tick(const HostIn&);
    void unreadable(){offered_=false;}                 // the offered file could not be read: nothing is on offer
    uint32_t generation() const {return generation_;}
    bool waiting() const {return pending_||state_==State::Saving;}   // the newest checkpoint is worth a look
private:
    enum class State{Idle,Saving} state_=State::Idle;
    bool automatic_=false,primed_=false,pending_=false,saved_=false,offered_=false;
    uint32_t episode_=ds::UnknownProgress,completed_=0,generation_=0;unsigned tries_=0;
    uint64_t savedAt_=0,since_=0,stableSince_=0;std::optional<Stamp> before_,candidate_;std::string why_;
};
// What the host serves: one checkpoint at a time.
class Serve {
public:
    void offer(uint64_t session,uint32_t generation,const launcher::saves::Checkpoint&);
    News news() const {return news_;}
    std::string answer(const std::string& datagram) const;    // "" = not for this checkpoint
private:
    launcher::saves::Sender tx_;News news_;uint32_t secret_=0,nonce_=0;
};

// ---- guest ----
class Fetch {
public:
    Fetch(uint64_t session,const News& want):want_(want),secret_(secretOf(session)),nonce_(nonceOf(session,want.generation)){}
    std::vector<std::string> poll(int budget){return rx_.poll(secret_,nonce_,true,budget);}
    void take(const std::string& datagram){rx_.take(datagram,secret_,nonce_);}
    bool done();                                       // complete, and it is the checkpoint the news named
    int percent() const {return rx_.percent();}
    const std::string& bytes() const {return rx_.bytes();}
private:
    News want_;launcher::saves::Receiver rx_;uint32_t secret_,nonce_;int verdict_=0;   // 1 the named checkpoint, -1 another
};
// inWorld: the porter is in the world and no game event runs. porter: the porter can be read at all (false: the world
// is gone, as during a load). saveBusy: the game is writing or about to write a save.
struct GuestIn { uint64_t now=0; bool connected=false,inWorld=false,porter=true,saveBusy=false,key=false,fetched=false; std::vector<News> news; };
struct GuestOut { bool ask=false,install=false,load=false,notice=false; std::optional<News> fetch,tell; std::vector<std::string> log; };
class Guest {
public:
    // AskGap: between two key presses that reach the host. LeaveWait: the world must be left this soon after the
    // load request, or the game refused it. ReturnWait: the longest load. FetchWait: the longest fetch.
    static constexpr uint64_t AskGap=10000,LeaveWait=30000,ReturnWait=600000,FetchWait=300000;
    explicit Guest(bool automatic=false):automatic_(automatic){}
    GuestOut tick(const GuestIn&);
    void installed(bool ok);                           // the answer to GuestOut::install
    GuestOut refused(uint64_t now);                    // the answer to GuestOut::load when the game was not asked
    bool busy() const {return state_!=State::Idle&&state_!=State::Offered;}
    uint32_t loaded() const {return loaded_;}
private:
    enum class State{Idle,Offered,Fetching,Fetched,Installed,Loading,Returning} state_=State::Idle;
    void take(const News&,GuestOut&);
    bool automatic_=false,asked_=false;News offer_{};std::optional<News> next_;
    uint32_t loaded_=0;uint64_t since_=0,askedAt_=0,fetchSince_=0;bool fetchStarted_=false;bool everAsked_=false,linked_=false;
};

// ---- files (the copy's own save tree only) ----
// The one account folder under <data>/local/KojimaProductions/DeathStrandingDC, where <data> is the folder that holds
// the mod's folder (the copy's redirected data). Throws when there is none or more than one.
struct Account { std::filesystem::path folder; uint32_t id=0; };
Account account(const std::filesystem::path& modFolder);
std::optional<Stamp> newest(const std::filesystem::path& account);      // stat only
// The host's checkpoint becomes manualsave0 of this account, re-signed for it; the manualsave0 that was there moves to
// <game folder>/story-backups/, never deleted. The other saves stay.
void install(const Account&,const std::string& hostFile);
constexpr uint8_t LoadType=1,LoadSlot=0;               // the game's ESaveGameType Manual, ESaveGameSlot Slot0

// ---- the game (Windows only; every address checked against the pinned code before use) ----
struct SaveState { bool readable=false,inGame=false,requests=false,pending=false; uint8_t pendingType=0xff,module=0; };
SaveState readSaveState(const discovery::Sampler&,uint64_t base);        // read-only
std::function<void()> nativeSave(discovery::Sampler&);                   // DSCreateSaveGame(Automatic)
// The pause menu's load of a slot. 0: no game running or not a numbered slot; 1: the game does not list that save;
// 2: the game was asked (it still refuses by itself while it cannot load: readSaveState tells).
std::function<int(uint8_t type,uint8_t slot)> nativeLoad(discovery::Sampler&);
}
