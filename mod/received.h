#pragma once
// Received structures built (config `build_received`, off by default; game_offsets.md, "Received structures built").
// A structure the game makes from a server's answer is a Net construction record (owner byte +0xb2 == 2) that starts in
// state 0 with object +0xaa 0x20 (still needs the online placement check) and without 0x10 (skip the range test). The
// game's pending-list walker builds it only when the porter is far enough away (1000 m for bridges, safe houses and
// jump stands, 50 m for the rest), then runs the placement check, and a failed check marks it for removal (+0xaa |=
// 0x40, +0xab |= 1). So a served bridge stays pending while the porter is near it, and a served rain shelter is taken
// down within seconds. With the option the mod sets +0xaa = (+0xaa | 0x10) & ~0x20 on such a record as soon as it
// sees it: the walker builds it at once (live, a 30 m bridge, by an outside reader). Both checks are skipped, so a
// received structure may overlap one of this world. Only the one byte is written, on the game thread, once per record.
// What it leaves alone: Player and Stage records, the save's records (they carry 0x10 already), records marked for
// removal, records in any state but 0, and Net records without an online id (the mod's own copies of a partner's
// structures, made with CreateConstruction, are not from a server).
#include "own_strand.h"
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace mod::received {
constexpr uint64_t Flags=0xaa,Removal=0xab;          // object offsets (object = record - 0x18)
constexpr uint8_t Placement=0x20,NoRange=0x10,Removing=0x40,RemovingBit=1;
// Whether the mod looks at all: the option on, a server in the config (`online_server`, the players' own: a modded copy
// never signs in anywhere else) and the game signed in to it.
bool active(bool option,bool server,bool signedIn);
// The cheap part, before the object is read (own_strand::Select): state 0.
bool pending(const structures::Record&);
// Owner and flags, before the online id is read: Net, +0xaa 0x20 without 0x10 and 0x40, +0xab bit 0 clear.
bool candidate(const own_strand::Structure&);
// A record the game made from a server's answer and has not dealt with yet: both of the above and an online id.
bool fresh(const own_strand::Structure&);
constexpr uint8_t built(uint8_t flags){return uint8_t((flags|NoRange)&~Placement);}
struct Change { uint64_t object=0; uint32_t id=0; uint8_t type=0; std::string online; uint8_t before=0,after=0; };
class Builder {
public:
    using Write=std::function<bool(uint64_t address,uint8_t value)>;
    // One look at the construction records: the fresh ones to change, each record once (by record id and online id).
    std::vector<Change> look(const std::vector<own_strand::Structure>&);
    // The glue's step on the game thread: reads the pending records (own_strand::readStructures), decides, reads each
    // chosen record's two flag bytes again and writes +0xaa with `write` only while they are as they were seen.
    // Returns what was written.
    std::vector<Change> step(const discovery::Sampler&,uint64_t base,uint64_t imageSize,const Write&);
    unsigned changed() const {return changed_;}
    unsigned skipped() const {return skipped_;}   // chosen, then not written: the bytes had changed or the write failed
    // The log line for a change.
    static std::string line(const Change&);
private:
    std::set<std::pair<uint32_t,std::string>> done_;unsigned changed_=0,skipped_=0;
};
// What a construction type is called in the log ("bridge", "rain shelter"...; "structure" when not known).
const char* name(uint8_t constructionType);
// One byte into committed read-write memory (Windows); elsewhere every write fails.
Builder::Write nativeWrite();

// From the first frame (game_offsets.md, "Received structures kept from their first frame"). The game makes a record
// from a server's answer and walks it in the same frame, and a rain shelter is refused in that walk, before any tick of
// the mod. So the mod also changes the request the record is made from, in the game's code: the builder of requests
// for a findQpidObjects answer (AnswerBuilder, one request per accepted object, nothing else uses it) sets request
// +0x62d = 1 (object +0xaa 0x20) and copies the answer's byte +0x110, which is 0 for every downloaded object, into
// request +0x63d (object +0xaa 0x10). Two bytes of that code: its constant 1 for +0x62d becomes 0, and its store into
// +0x63d goes to +0x629 instead, where the same byte was just stored, so +0x63d keeps the request constructor's 1. The
// record is then made with 0x10 set and 0x20 clear, the flags the scan writes, and built as it is made, as the game
// builds the save's records of other players. +0x629 (object +0xaf bit 7, which holds the build until another online
// callback clears it) keeps the answer's byte. Nothing else of the game's code changes: the porter's builds,
// CreateConstruction, the save's records and every other request come from other code.
constexpr uint64_t AnswerBuilder=0x2c5f850,AnswerFlags=0x2c601c0;
constexpr size_t AnswerFlagsSize=0x28;
// Patched (written now or found so), Differs (the code is not this build's: nothing written), Failed (a write did not
// take; what was written is put back).
enum class Early{Off,Patched,Already,Differs,Failed};
// Whether to change the code: the option and a server in the config. It is changed before the game signs in (no answer
// can arrive before that) and stays so: answers only come from the server the config names.
bool early(bool option,bool server);
// Checks the builder's first bytes and the 0x28 bytes at AnswerFlags, then writes the two bytes with `code` and reads
// them back.
Early patchAnswers(const discovery::Sampler&,uint64_t base,const Builder::Write& code);
const char* label(Early);   // "patched", "already", "differs", "failed", "off"
std::string line(Early);    // the log line
// One byte into the game's code (Windows: the page made writable for the write, the instruction cache flushed);
// elsewhere every write fails.
Builder::Write nativeCodeWrite();
}
