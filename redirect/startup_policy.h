#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <cstdint>
#include "redirect.h"

// The release proxy is inert without an explicit, short-lived launcher ticket.
// Explicit environment configuration always selects the existing lab path.
namespace release_startup {
enum class Mode { passthrough, environment, ticket };
using Values = std::map<std::string,std::string>;
inline const char* const Keys[] = {
    "COOP_MODULE_INSTANCE", "COOP_REDIRECT_INSTANCE", "COOP_REDIRECT_STEAM",
    "COOP_REAL_LOCAL", "COOP_REAL_ROAMING", "COOP_REAL_DOCUMENTS", "COOP_REAL_SAVEDGAMES",
    "COOP_MODULE_POLICY", "COOP_NETWORK_POLICY", "COOP_NETWORK_PEERS"
};
inline std::wstring wide(const std::string& s) { return {s.begin(),s.end()}; }
inline Values parse(const std::string& text) {
    if(text.size()>16384) throw std::runtime_error("launcher ticket too large");
    Values out;
    size_t at=0;
    while(at<text.size()) {
        auto end=text.find('\n',at);if(end==std::string::npos)end=text.size();
        auto line=text.substr(at,end-at);at=end+1;
        auto eq=line.find('=');
        if(eq==std::string::npos||!eq||eq+1==line.size())throw std::runtime_error("invalid launcher ticket");
        for(unsigned char c:line)if(c<32||c>126)throw std::runtime_error("ticket requires ASCII paths");
        auto key=line.substr(0,eq);bool known=key=="target"||key=="expires";
        for(auto k:Keys)known|=key==k;
        if(!known||!out.emplace(key,line.substr(eq+1)).second)throw std::runtime_error("duplicate/unknown ticket field");
    }
    return out;
}
inline bool valid(const Values& v,const std::wstring& exe,uint64_t now) {
    try {
        if(v.size()!=12)return false;
        auto time=v.at("expires");
        if(time.empty()||time.size()>12||time.find_first_not_of("0123456789")!=std::string::npos)return false;
        auto expires=std::stoull(time);
#ifndef MUTANT_TICKET_EXPIRY
        if(expires<now||expires-now>180)return false;
#else
        (void)expires;(void)now;
#endif
        auto target=redirect::canonical(wide(v.at("target")));
#ifndef MUTANT_TICKET_TARGET
        if(target.empty()||target!=redirect::canonical(exe))return false;
#else
        (void)exe;
#endif
        for(auto k:Keys)if(v.at(k).empty())return false;
        auto root=redirect::canonical(wide(v.at("COOP_MODULE_INSTANCE")));
        if(root.empty()||redirect::canonical(wide(v.at("COOP_REDIRECT_INSTANCE")))!=root+L"\\redirect-data")return false;
#ifndef MUTANT_TICKET_NETWORK
        if(v.at("COOP_NETWORK_POLICY")!="enforce"||v.at("COOP_MODULE_POLICY")!="allowlist")return false;
#endif
        // Redirect roots must be real absolute folders, never inside our output.
        for(auto k:{"COOP_REAL_LOCAL","COOP_REAL_ROAMING","COOP_REAL_DOCUMENTS","COOP_REAL_SAVEDGAMES","COOP_REDIRECT_STEAM"}) {
            auto p=redirect::canonical(wide(v.at(k)));
            if(p.empty()||redirect::within(p,root))return false;
        }
        return true;
    } catch(...) {return false;}
}
inline Mode decide(bool release,bool explicitEnvironment,bool validTicket) {
    if(!release||explicitEnvironment)return Mode::environment;
#ifdef MUTANT_MISSING_CONFIG_CLOSES
    (void)validTicket;
    return Mode::environment;
#else
    return validTicket?Mode::ticket:Mode::passthrough;
#endif
}
}
