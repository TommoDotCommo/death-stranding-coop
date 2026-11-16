#pragma once
// The launcher's API (README, "The launcher's API"): beside the game's gateway, under the same secret prefix, for the
// launcher and never the game. Without sockets: one call in, one reply out (front.h puts it on the path).
//   GET  [/PREFIX]/coop/policy                                     the policy, one line in words per setting
//   GET  [/PREFIX]/coop/me?player=KEY                              that player's choices, membership and counts
//   GET  [/PREFIX]/coop/mine?player=KEY                            his objects built here: state and likes
//   POST [/PREFIX]/coop/choices   {"player":KEY,"offer_builds":"yes|no|ask|default","offer_old":"yes|no|default"}
//   POST [/PREFIX]/coop/offer     {"player":KEY,"object":ID} or {"player":KEY,"all":true}, and "answer":"no" to keep
//   POST [/PREFIX]/coop/withdraw  {"player":KEY,"object":ID} or {"player":KEY,"all":true}
// KEY is the account key his game signs in with: the NAME of a /k/NAME address, or the 64-bit Steam id. There is no
// identity: whoever has the prefix can act for any player here, as he can sign in as any player today. The owner's
// actions (policy, review, remove-player, member) are not here: they stay commands on the stopped server.
#include "world.h"
namespace world {
bool accountKeyText(const std::string&);    // 1..MaxKeyBytes letters, digits, '_', '-', '.'
// `call` is what follows /coop/ ("policy"); `player` is the address's ?player= (decoded), for the two GET calls.
Reply launcherApi(World&,const std::string& method,const std::string& call,const std::string& player,const std::string& body);
// One setting's value in words, for a player deciding whether to join (`players`: how many the world has now).
std::string settingText(const std::string& key,const std::string& value,size_t players);
}
