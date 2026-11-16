#pragma once
// The server owner's side (README, "Server owner"). The host whose launcher started the group's server gave it his
// player key as the owner's (--owner); only calls signed with that key are the owner's actions on the running server
// (../world/README.md, "Identity": /coop/owner/...), under every policy. His launcher's "Server owner" window lists the
// players and what waits, and makes those calls: members, removing all of a player's objects, the review queue and
// the rules. Portable (strand.h's transport), so the game-free tests run it against the built coop-world.
#include "strand.h"
#include "../world/json.h"
namespace launcher::owner {
// Whether this launcher holds the owner key of the session's server: it hosts that server, started with its own key.
bool available(const Session&,const world::identity::Keys&);
// A player as the server lists him: `key` his key short ("" for an account that is no key: those are not shown),
// counts of his objects built here and the likes on what is still his. `known` false: a builder known here only from
// linked servers (his name is "Name · Home").
struct Player {
    std::string id,name,key,offerBuilds,offerOld;bool known=true,member=false;
    int64_t shared=0,waiting=0,held=0,kept=0,remnants=0,imported=0,likes=0;
    std::string text() const;
};
// One object that waits or is held: for the owner, or first for its builder's answer (`waitingFor` "builder").
struct Waiting { std::string id,creator,name,kind,held,waitingFor;int64_t area=0,updated=0;std::string text() const; };
std::vector<Player> parsePlayers(const std::string& body);   // throw for anything but the server's list
std::vector<Waiting> parseWaiting(const std::string& body);
// Reads the policy and makes the target sign every call with its key, on the server's clock, whatever the server's
// identity setting. false, with `why`, when there is no server, no key, or an older server without the owner's calls.
bool connect(strand::Target&,const strand::Transport&,strand::Policy&,std::string& why);
struct View { strand::Policy policy;std::string name;std::vector<Player> players;std::vector<Waiting> waiting;json::Value policyDoc,playersDoc,waitingDoc; };
bool fetch(const strand::Target&,const strand::Transport&,View&,std::string& why);   // after connect
// One of the owner's actions. kind: member (target a player id, value yes|no), remove (player), accept or reject (an
// object that waits for the owner), accept-player or reject-player (everything of that player that waits for the
// owner), policy (a preset, settings KEY=VALUE, or both).
struct Action { std::string kind,target,value,preset;std::vector<std::string> sets; };
bool validAction(const Action&);
struct Done { bool ok=false;std::string message; };
Done act(const strand::Target&,const strand::Transport&,const Action&);
// The rules for the screen, one row per setting: its value, the preset's where it differs, and its words.
std::vector<std::string> rules(const strand::Policy&);
// The question before removing all of a player's objects: what goes, what this server's withdrawal rule does when a
// player takes back his own (stay or go) and that removal is neither, and that it is not a ban.
std::string removalText(const strand::Policy&,const Player&);
// Without the window (--dev-direct --owner-...): connect, the action ("list": none), then the view after it, as one
// JSON report: ok, action, message, server, name, policy, players, waiting.
json::Value run(strand::Target,const strand::Transport&,const Action&);
}
