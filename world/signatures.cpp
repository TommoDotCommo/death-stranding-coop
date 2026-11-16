#include "signatures.h"
namespace world::identity {
namespace {
bool digits(const std::string& s){if(s.empty()||s.size()>12)return false;for(char c:s)if(c<'0'||c>'9')return false;return true;}
const std::string& header(const std::map<std::string,std::string>& h,const char* name){static const std::string none;auto it=h.find(name);return it==h.end()?none:it->second;}
}
Checker::Result Checker::check(const std::map<std::string,std::string>& h,const std::string& method,const std::string& target,const std::string& body,
                               const std::string& server,int64_t now,std::string& who,std::string& why){
    const std::string &key=header(h,"x-coop-key"),&time=header(h,"x-coop-time"),&nonce=header(h,"x-coop-nonce"),&signature=header(h,"x-coop-signature");
    if(key.empty()&&time.empty()&&nonce.empty()&&signature.empty())return Result::Unsigned;
    PublicKey k;Signature s;uint8_t n[NonceBytes];
    if(!unhex(key,k.data(),k.size())||!digits(time)||!unhex(nonce,n,sizeof n)||!unhex(signature,s.data(),s.size())){why="a signed call needs X-Coop-Key, X-Coop-Time, X-Coop-Nonce and X-Coop-Signature";return Result::Bad;}
    const int64_t t=std::stoll(time);
#ifndef MUTANT_SIGN_WINDOW
    if(t<now-WindowSeconds||t>now+WindowSeconds){why="the call's time is not the server's (more than "+std::to_string(WindowSeconds)+" s apart)";return Result::Bad;}
#endif
#ifndef MUTANT_SIGN_NO_VERIFY
    if(!verify(k,requestText(method,target,server,t,nonce,body),s)){why="the signature does not match the call";return Result::Bad;}
#endif
    const std::string id=hex(k),seen=id+hex(n,sizeof n);
    std::lock_guard<std::mutex> g(lock_);
    // Forget what can no longer be taken anyway: a nonce needs remembering only while its time is in the window.
    while(!byExpiry_.empty()&&byExpiry_.begin()->first<now){
        auto& e=*byExpiry_.begin();seen_.erase(e.second);
        auto pk=perKey_.find(e.second.substr(0,2*KeyBytes));if(pk!=perKey_.end()&&--pk->second==0)perKey_.erase(pk);
        byExpiry_.erase(byExpiry_.begin());}
#ifndef MUTANT_SIGN_REPLAY
    if(seen_.count(seen)){why="this call was taken already (its nonce was used)";return Result::Bad;}
#endif
#ifndef MUTANT_SIGN_NONCE_CAP
    // Bounded: when full the server refuses signed calls rather than forget a nonce that could still be replayed.
    if(seen_.size()>=most_||perKey_[id]>=perKeyMost_){if(!perKey_[id])perKey_.erase(id);why="too many signed calls in the last minutes; try again later";return Result::Bad;}
#endif
    if(seen_.insert(seen).second){byExpiry_.insert({t+WindowSeconds,seen});++perKey_[id];}
    who=AccountPrefix+id;return Result::Ok;
}
}
