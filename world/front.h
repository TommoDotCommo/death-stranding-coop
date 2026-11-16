#pragma once
// The world server's HTTP surface, without sockets: one parsed request in, one reply out.
//   GET  [/PREFIX][/k/KEY]/auth/ds?provider=steam&display_name=NAME&code=TICKET   sign-in
//   ANY  [/PREFIX]/gate[/e]/NAME   with "Authorization: Bearer TOKEN"             one gateway call
//   POST [/PREFIX]/link/pull       with "Authorization: Link KEY"                 one page for a linked server (link.h)
//   ANY  [/PREFIX]/coop/NAME                                                      the launcher's API (api.h)
// PREFIX is the group's secret path element (optional): a request without it is answered 404 and changes nothing.
// The account is KEY when the address carries one (two copies under one Steam account), else the Steam id inside the
// ticket. The ticket is not checked with Steam: whoever can reach this server and knows the prefix is a player.
#include "link.h"
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
private:
    World& world_;std::string prefix_;std::vector<LinkConfig> links_;
};
}
