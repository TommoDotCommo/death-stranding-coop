#pragma once
// Offering the player's existing strand (the structures his save already holds) to the players' own server (config
// `offer_strand`, off by default; game_offsets.md, "Offering the player's existing strand"). The game itself does the
// upload when asked on its game thread while signed in:
// - an own record (owner byte 1) with no online id: 0x2c9ed40(object) queues the game's own createObject (the uploader's
//   slot 0x28) and sets the record's retry timer to 120 s; the answer's id lands at object +0xb8. The game does the same
//   by itself when the porter is within 15 m of such a record (live 2026-11-17: a 30 m bridge uploaded within 40 s).
// - an own record with an id of the original online service: the uploader's slot 0x68 (0x3484f80) queues the game's
//   "re-create", the same body plus "id" (the old one), whose answer the game ignores (it keeps the old id). The
//   players' server keeps an id of the game's shape (world/README.md, "An object's own id").
// The uploader is [image+0x7c18670] (the object 0x2c9ed40 reaches through the pointer at 0x4df9df0; vtable 0x3edecc8).
// With the option the mod reads the own records (own_strand.h), picks them (plan), and asks the game for one at a
// time (Offerer): the next only once the last has its online id, has left the game's queues, or a minute has passed;
// only while the game is signed in to the server the config names and that server says this player's older strand may
// be offered ("offer_old" in its notices answer, strand_notices.h). Each record is asked for once per run.
// Also here: for a mod that does not call the game, the body the game would send, made from the record's memory
// (body; not for climbing ropes, whose "ri" comes from a call), with the map cell the game computes (cell). The game
// also sends "mt", "rmt", "tags" and "ci" when a record has them; the body made here leaves them out, and "lp" (likes)
// is an input.
#include "discovery.h"
#include "own_strand.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mod::offer_strand {
constexpr uint64_t QueueUpload=0x2c9ed40;            // (object): the game's own retry call
constexpr uint64_t UploaderGlobal=0x7c18670;         // the uploader (not the area lists' manager at 0x7c1abf0)
constexpr unsigned SlotUpload=0x28,SlotRecreate=0x68;   // its virtual slots, (uploader, object)
constexpr uint8_t Player=1;                          // owner byte of a Player variant (built by this porter)
constexpr unsigned MaxId=64;
// What the game's body is made from, read from a construction object (the table record - 0x18).
struct Object {
    uint8_t type=0,owner=0,state=0;                  // +0x34, +0xb2, record state (+0x36)
    bool held=false;                                 // +0xab bit 7: the game refuses to upload it
    uint32_t level=0;                                // +0x84
    std::array<double,3> position{};                 // +0x40
    std::array<float,9> rotation{};                  // +0x58, rows right, forward, up
    float angle=0;                                   // a bridge's, chiral bridge's or jump stand's own angle (radians)
    std::string online;                              // +0xb8, empty when on no server
};
std::optional<Object> read(const discovery::Sampler&,uint64_t object);
// The server letter the game's body carries for a construction type (Player variants and the post's Net variant, as
// the game's switch has them); 0 for a type it sends as "unknown".
char letter(uint8_t type);
enum class Plan { Skip, Upload, Recreate };
struct Decision { Plan plan=Plan::Skip; const char* why=""; };
Decision plan(const Object&);
// The map cell (x, y) of a position: tile size and tile counts are the game's data (game_offsets.md).
std::array<int32_t,2> cell(double x,double y,int32_t tile,int32_t width,int32_t height);
// The rest of the body that the game takes from its own functions, not from the record.
struct Placement { int32_t x=0,y=0,map=0,area=0,priority=0,alternative=0; int64_t likes=0; };
// The game's angles for its rotation rows, in its integer form (degrees x 10^5).
std::array<int32_t,3> angles(const std::array<float,9>&);
// The createObject body the game would send for this record ("id" added for a re-create); empty when it cannot be
// made from memory (a climbing rope, an unknown kind).
std::string body(const Object&,const Placement&,bool recreate=false);

// --- The mod's offering (config offer_strand).
// Whether the mod offers at all: the option on, a server in the config (`online_server`, the players' own: a modded
// copy never signs in anywhere else), the game signed in to it, and that server's word for this player's older strand
// ("yes" only; "" is a server that has not said, or a token it refused).
bool active(bool option,bool server,bool signedIn,const std::string& offerOld);
// Whether the players' server keeps an id the game re-creates with: the kind's letter in lower case and 12 letters or
// digits. The original service's ids with a capital letter are left alone.
bool kept(const std::string& online,char kind);
// What plan() needs, from the structure reader's record (no further reads: owner, flags +0xab, online id).
Object of(const own_strand::Structure&);
// The structure reader's filter: built records (state 1 or 2) of this porter (owner 1).
own_strand::Select select();
// Code and the uploader on the pinned build (RVAs).
struct Sites { uint64_t upload=QueueUpload,recreate=0x3484f80,queue=0x3485040,slot=0x4df9df0,global=UploaderGlobal,vtable=0x3edecc8; };
// Empty when the image is the pinned build's: the upload call's and the re-create's code, each unique in the image at
// its address (the upload call's bytes hold the pointer it reads and the slot it calls). Otherwise why not. Reads only.
std::string verifyCode(const discovery::Sampler&,const Sites& ={});
// The uploader as the game's own call finds it ([[image+slot]], the pointer equal to image+global), checked: its class
// and both queue slots. 0 when any of that differs or cannot be read.
uint64_t uploader(const discovery::Sampler&,const Sites& ={});
// The uploader's queues: create +0x58 (count, i32) / +0x60 (pointers) / +0x68 (the one in flight), re-create
// +0x78 / +0x80 / +0x88. Whether the object is still in one: 1 yes, 0 no, -1 unreadable.
constexpr uint64_t CreateQueue=0x58,RecreateQueue=0x78;constexpr unsigned MaxQueue=4096;
int queued(const discovery::Sampler&,uint64_t uploader,uint64_t object,bool recreate);
// One record at a time, on the game thread.
class Offerer {
public:
    static constexpr uint64_t SettleMs=20000;        // after it is on (signed in, the server's yes): the game's own area requests first
    static constexpr uint64_t PollMs=500,GapMs=1000,TimeoutMs=60000,RescanMs=60000;
    struct Calls {
        std::function<std::vector<own_strand::Structure>()> scan;   // the own records now (with select())
        std::function<bool(uint64_t object,bool recreate)> ask;     // the game's call; false: not made (identity)
        std::function<int(uint64_t object,bool recreate)> queued;   // as queued() above
    };
    // One step: `on` is active() now; `ready`: the porter is in play (not loading, not in a cutscene or event), else
    // nothing is read or asked this step. Returns log lines ("OFFER ...").
    std::vector<std::string> step(bool on,bool ready,uint64_t now,const Calls&);
    struct Counters { unsigned asked=0,uploaded=0,recreated=0,refused=0,timedOut=0,unqueued=0,gone=0,left=0;bool on=false,broken=false; };
    const Counters& counters() const {return k_;}
private:
    struct Current { uint64_t object=0,since=0;uint32_t id=0;uint8_t type=0;bool recreate=false;std::string online; };
    std::optional<Current> current_;std::set<uint32_t> done_;uint64_t next_=0;bool idle_=false;Counters k_;
    std::string finish(const Current&,const char* how,uint64_t now);
};
#ifdef _WIN32
// Checks verifyCode and throws on a mismatch. The call finds and checks the uploader again (uploader()) every time and
// then calls the game's function on the calling thread: 0x2c9ed40(object), or the uploader's slot 0x68.
std::function<bool(uint64_t object,bool recreate)> nativeAsk(const discovery::Sampler&);
#endif
}
