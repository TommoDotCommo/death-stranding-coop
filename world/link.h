#pragma once
// Linked servers, the live side, without sockets (README.md, "Linked servers"). A link is one other server, a key both
// know and two filters: what that server may take from here, and what is taken from it. Objects only ever move
// because the taking side asks (POST [/PREFIX]/link/pull with "Authorization: Link KEY"): a server that is given
// the other's address asks every few seconds, page by page, and makes what it holds from that server equal to what
// passed both filters: new objects appear, changed ones change, ones that are gone or no longer pass are removed.
//
// What it holds to:
//   - nothing is served and nothing is asked without a key of 8 to 64 letters, digits, - and _;
//   - an answer is taken whole or not at all: every page must be well formed and from the same server and the same
//     version of its world, or nothing changes and the next round starts again;
//   - at most MaxImportedPerServer objects are held from one server, a page is at most LinkPageObjects objects.
#include "world.h"
#include <functional>
#include <string>
namespace world {
struct LinkConfig {
    std::string name,key,url;          // url: the other server, when this one asks it ("" = it only answers)
    Filter exportFilter,importFilter;  // both pass nothing until they are set
    int everySeconds=5;
    LinkConfig(){exportFilter.none=true;importFilter.none=true;}
};
bool linkKeyText(const std::string&);                       // 8..64 letters, digits, - and _
bool sameKey(const std::string& a,const std::string& b);    // the same time whatever differs
// "http://A.B.C.D:PORT" or "http://A.B.C.D:PORT/PREFIX": a numeric IPv4 address, as --bind takes.
struct LinkEndpoint { std::string host;int port=0;std::string prefix; };
bool parseLinkUrl(const std::string& url,LinkEndpoint& out);
constexpr int64_t LinkFullEverySeconds=600;                 // a round that takes everything again, changed or not
constexpr size_t LinkMaxPages=4000;
class Puller {
public:
    // One request to the other server's /link/pull: false when it could not be reached or did not answer 200 (a
    // server answers 404 to a key it does not know, as it does to any path it does not have).
    using Fetch=std::function<bool(const std::string& request,std::string& response)>;
    enum class Result { Same, Synced, Unreachable, Refused, Changed, TooMany };
    Puller(World& w,LinkConfig c,Fetch fetch):world_(w),config_(std::move(c)),fetch_(std::move(fetch)){}
    Result poll(int64_t nowSeconds);                         // one round
    const ImportResult& last() const {return last_;}         // what the last Synced round did
    const std::string& peer() const {return peer_;}          // the other server's id, once it answered
    const std::string& why() const {return why_;}            // the reason of the last Refused
    static const char* text(Result r){
        return r==Result::Same?"in step":r==Result::Synced?"synced":r==Result::Unreachable?"no answer (not running, or not the address or key it expects)":r==Result::Refused?"refused":r==Result::Changed?"changed while asking":"too many objects";}
private:
    World& world_;LinkConfig config_;Fetch fetch_;
    std::string version_,peer_,why_;int64_t lastFull_=0;bool haveFull_=false;ImportResult last_;
};
}
