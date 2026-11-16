#pragma once
// The server's check of a signed call (identity.h has the request's form): the signature, the time window, and the
// nonce remembered until the call's time leaves the window, so the same call is taken once. Thread-safe.
#include "identity.h"
#include <map>
#include <mutex>
#include <set>
namespace world::identity {
constexpr size_t NoncesMax=65536,NoncesPerKey=1024;   // nonces remembered at once, in all and for one key
// `headers` as the server parsed them (lower-case names).
class Checker {
public:
    explicit Checker(size_t most=NoncesMax,size_t perKey=NoncesPerKey):most_(most),perKeyMost_(perKey){}
    enum class Result : uint8_t { Unsigned, Ok, Bad };
    // Ok: `account` is the signer's account key. Bad: `why` says why (nothing is remembered for a call that is refused
    // before its signature is checked). Unsigned: none of the four headers.
    Result check(const std::map<std::string,std::string>& headers,const std::string& method,const std::string& target,const std::string& body,
                 const std::string& server,int64_t now,std::string& account,std::string& why);
    size_t remembered() const {std::lock_guard<std::mutex> g(lock_);return seen_.size();}
private:
    mutable std::mutex lock_;size_t most_,perKeyMost_;
    std::set<std::string> seen_;                            // key hex + nonce
    std::set<std::pair<int64_t,std::string>> byExpiry_;     // (the last second it can still be taken, key hex + nonce)
    std::map<std::string,size_t> perKey_;
};
}
