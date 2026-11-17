#pragma once
// The launcher's API (README, "The launcher's API"): beside the game's gateway, under the same secret prefix, for the
// launcher and never the game. Without sockets: one call in, one reply out (front.h puts it on the path).
//   GET  [/PREFIX]/coop/policy                                     the policy, one line in words per setting
//   GET  [/PREFIX]/coop/me?player=KEY                              that player's choices, membership and counts
//   GET  [/PREFIX]/coop/mine?player=KEY                            his objects built here: state and likes
//   POST [/PREFIX]/coop/choices   {"player":KEY,"offer_builds":"yes|no|ask|default","offer_old":"yes|no|default"}
//   POST [/PREFIX]/coop/offer     {"player":KEY,"object":ID} or {"player":KEY,"all":true}, and "answer":"no" to keep
//   POST [/PREFIX]/coop/withdraw  {"player":KEY,"object":ID} or {"player":KEY,"all":true}
//   GET  [/PREFIX]/coop/friends?player=KEY                         his friend list and his choice of who he sees
//   POST [/PREFIX]/coop/friends   {"player":KEY,"add":[KEY|SHORT,...],"remove":[...],"show_others":"everyone|friends|nobody|default"}
//   POST [/PREFIX]/coop/ticket       {"player":KEY}   signed, identity keys: a sign-in ticket for his game (world.h)
//   POST [/PREFIX]/coop/relay-token  {"player":KEY}   signed, identity keys: a relay token for his relay id (identity.h)
//   POST [/PREFIX]/coop/notice-token {"player":KEY}   a notice token for his game's mod (world.h; signed under keys, as any change)
// The mod's one call, with its notice token and no signature (front.h): GET [/PREFIX]/coop/notices?since=VERSION.
// The owner's calls, signed with the owner's key (World::owner):
//   GET  [/PREFIX]/coop/owner/players | owner/review                    every player; what waits or is held
//   POST [/PREFIX]/coop/owner/review  {"accept":ID} | {"reject":ID} | {"accept_player":PLAYER-ID}
//   POST [/PREFIX]/coop/owner/remove-player {"player":PLAYER-ID}     POST [/PREFIX]/coop/owner/member {"player":PLAYER-ID,"member":"yes|no"}
//   POST [/PREFIX]/coop/owner/policy  {"preset":"friends|solo|public","set":["KEY=VALUE",...]} (one or both)
// KEY is the account key his game signs in with: the NAME of a /k/NAME address, the 64-bit Steam id, or under identity
// keys the account of his key (identity.h). Under identity none an unsigned call may act for any player (there is no
// identity). A signed call (identity.h) acts for its signer only; under identity keys every call but the policy is
// signed. `signer` is the account of the verified key ("" for an unsigned call; Front checks the signature).
#include "relay.h"
#include "world.h"
namespace world {
bool accountKeyText(const std::string&);    // 1..MaxKeyBytes letters, digits, '_', '-', '.'
// `call` is what follows /coop/ ("policy"); `player` is the address's ?player= (decoded), for the two GET calls.
Reply launcherApi(World&,const std::string& method,const std::string& call,const std::string& player,const std::string& body,const std::string& signer={},int64_t now=0);
// What the relay holds for a broadcast player (relay.h RelayViewer), from what the world holds (World::relayViewer):
// the relay's `viewer`. `capGiven`: the relay was given its cap (--broadcast-near), else the policy's near_cap is it.
RelayViewer relayViewer(const World&,uint64_t relayId,bool capGiven);
// The relay ids of the players the owner kicked or banned since the last call (World::takeEvictions, made from their
// keys): RelayConfig::evicted (README, "Kick and ban").
std::vector<uint64_t> relayEvictions(World&);
// One setting's value in words, for a player deciding whether to join (`players`: how many the world has now).
std::string settingText(const std::string& key,const std::string& value,size_t players);
}
