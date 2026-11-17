#pragma once
// The launcher's Challenges tab (../docs/ROUTES.md, "The launcher"), without the window: the routes the mod recorded and
// the results it wrote (the co-op data folder's routes\), editing a route's name and rules (a new DSR1 string), reading a
// pasted string, choosing the challenge the next game runs (Session::route), and the group server's challenges
// (../world/challenges.h) through the same signed calls as every launcher call (strand.h).
#include "core.h"
#include "strand.h"
#include "../mod/route.h"
#include <filesystem>
#include <string>
#include <vector>
namespace launcher::challenges {
std::filesystem::path folder(const std::filesystem::path& data);   // data\routes
struct Recorded { std::filesystem::path file;std::string text;mod::route::Route route; };
// The recordings (recorded-*.txt), newest first; a file that holds no good string is left out.
std::vector<Recorded> recordings(const std::filesystem::path& data);
struct Result { std::filesystem::path file;std::string route,outcome,reason,track;uint64_t ms=0;bool casesVerified=false;std::string name; };
// The results (result-*.json), newest first; a file that is not a result is left out.
std::vector<Result> results(const std::filesystem::path& data);
// One line for a list or the paste box: "Ridge by Ann: 5 m wide, 5 s grace, no vehicles; 1.2 km, 41 points", or why
// the text is not a challenge.
std::string describe(const std::string& text);
std::string describe(const Result&);
// The string in its own form (upper case, groups of five): what the config takes. Throws with the reason.
std::string canonical(const std::string& text);
// A route's name and rules changed: the new string. Throws std::invalid_argument with the reason (a name of printable
// characters, the ranges of docs/ROUTES.md); the points stay as recorded.
struct Edit { std::string name,author;mod::route::Rules rules; };
Edit editOf(const std::string& text);                     // what a string holds now
std::string edited(const std::string& text,const Edit&);
// The challenge for the next game, kept in launcher.ini ([launcher] challenge=...): `choose` checks and canonicalises
// it ("" clears it).
std::string chosen(const std::string& ini);
std::string choose(const std::string& ini,const std::string& text);

// ---- The group server's challenges -------------------------------------------------------------------------------
struct Board { std::string name,key,time;int64_t place=0,ms=0; };
struct Listed { std::string id,name,author,authorKey,route,rulesText;int64_t length=0,runs=0;std::vector<Board> board;std::string text() const; };
std::vector<Listed> parseList(const std::string& body);   // GET /coop/challenges; throws for anything else
bool fetchList(const strand::Target&,const strand::Transport&,std::vector<Listed>&,std::string& why);
// Publish a string under this player (`t.account`) with his launcher name; `id` the server's id for it.
bool publish(const strand::Target&,const strand::Transport&,const std::string& text,std::string& id,std::string& why);
// Send a finished run of one of the server's challenges (`id`); `place` on its board. A failed run is not sent.
bool submit(const strand::Target&,const strand::Transport&,const std::string& id,const Result&,int64_t& place,std::string& why);
// The server's challenge a result belongs to: the one whose string has the same route (by its CRC), or "".
std::string challengeOf(const std::vector<Listed>&,const Result&);
}
