#pragma once
// The world server's HTTP surface, without sockets: one parsed request in, one reply out.
//   GET  [/PREFIX][/k/KEY]/auth/ds?provider=steam&display_name=NAME&code=TICKET   sign-in
//   GET  [/PREFIX]/t/TICKET/auth/ds?...                                           sign-in with a launcher's ticket (identity keys)
//   ANY  [/PREFIX]/gate[/e]/NAME   with "Authorization: Bearer TOKEN"             one gateway call
//   POST [/PREFIX]/link/pull       with "Authorization: Link KEY"                 one page for a linked server (link.h)
//   ANY  [/PREFIX]/coop/NAME                                                      the launcher's API (api.h)
//   GET  [/PREFIX]/coop/notices?since=VERSION   with "X-Coop-Token: TOKEN"         the mod's read of its player's objects (world.h)
// PREFIX is the group's secret path element (optional): a request without it is answered 404 and changes nothing.
// The account is KEY when the address carries one (two copies under one Steam account), else the Steam id inside the
// ticket. The ticket is not checked with Steam: whoever can reach this server and knows the prefix is a player, unless
// the policy's identity is keys: then only a launcher's ticket signs in (world.h), and the API takes signed calls
// (identity.h; the signature of every /coop/ call that carries one is checked here, before the API sees it).
#include "link.h"
#include "signatures.h"
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace world {
struct Request { std::string method,target,body;std::map<std::string,std::string> headers; };   // header names lower-case
// The 64-bit Steam id in a session ticket's hex text ("" when it is not there).
std::string steamIdOf(const std::string& ticketHex);
class Front {
public:
    // `links`: the servers that may ask this one for objects, each known by its key. None: /link/ does not exist.
    Front(World& w,std::string prefix,std::vector<LinkConfig> links={}):world_(w),prefix_(std::move(prefix)),links_(std::move(links)){}
    Reply handle(const Request&,int64_t nowSeconds);
    // Route challenges (challenges.h): the /coop/ calls challengeCall() names go here, after the signature check, when
    // set (coop-world sets it; without it they are not found).
    std::function<Reply(const std::string& method,const std::string& call,const std::string& body,const std::string& signer,int64_t now)> challenges;
private:
    World& world_;std::string prefix_;std::vector<LinkConfig> links_;identity::Checker signatures_;
};
}
