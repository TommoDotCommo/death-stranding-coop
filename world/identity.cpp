#include "identity.h"
#include "../vendor/monocypher/monocypher-ed25519.h"
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif
namespace world::identity {
namespace {
const std::string Magic="coop-world request 1";
}
bool randomBytes(uint8_t* out,size_t n){
#ifdef _WIN32
    return BCryptGenRandom(nullptr,out,ULONG(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#else
    FILE* f=std::fopen("/dev/urandom","rb");if(!f)return false;
    bool ok=std::fread(out,1,n,f)==n;std::fclose(f);return ok;
#endif
}
Keys keysFromSeed(const Seed& seed){
    Keys k;k.seed=seed;uint8_t secret[64],copy[32];
    for(size_t i=0;i<32;++i)copy[i]=seed[i];   // the library wipes the seed it is given
    crypto_ed25519_key_pair(secret,k.pub.data(),copy);crypto_wipe(secret,sizeof secret);return k;
}
Keys makeKeys(){
    Seed seed;if(!randomBytes(seed.data(),seed.size()))throw std::runtime_error("the system's random number generator gave nothing");
    auto k=keysFromSeed(seed);crypto_wipe(seed.data(),seed.size());return k;
}
Signature sign(const Keys& k,const std::string& message){
    uint8_t secret[64],copy[32];Signature s;PublicKey pub;
    for(size_t i=0;i<32;++i)copy[i]=k.seed[i];
    crypto_ed25519_key_pair(secret,pub.data(),copy);
    crypto_ed25519_sign(s.data(),secret,reinterpret_cast<const uint8_t*>(message.data()),message.size());
    crypto_wipe(secret,sizeof secret);return s;
}
bool verify(const PublicKey& k,const std::string& message,const Signature& s){
    return crypto_ed25519_check(s.data(),k.data(),reinterpret_cast<const uint8_t*>(message.data()),message.size())==0;}
std::string sha512Hex(const std::string& text){
    uint8_t h[64];crypto_sha512(h,reinterpret_cast<const uint8_t*>(text.data()),text.size());return hex(h,sizeof h);}
std::string hex(const uint8_t* p,size_t n){
    static const char* d="0123456789abcdef";std::string s;s.reserve(2*n);
    for(size_t i=0;i<n;++i){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
bool unhex(const std::string& s,uint8_t* out,size_t n){
    if(s.size()!=2*n)return false;
    auto v=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(size_t i=0;i<n;++i){int hi=v(s[2*i]),lo=v(s[2*i+1]);if(hi<0||lo<0)return false;out[i]=uint8_t(hi*16+lo);}
    return true;}
std::string account(const PublicKey& k){return AccountPrefix+hex(k);}
bool keyOf(const std::string& a,PublicKey& k){return accountIsKey(a)&&unhex(a.substr(8),k.data(),k.size());}
std::string shortText(const PublicKey& k){auto h=hex(k);return h.substr(0,4)+" "+h.substr(4,4)+" "+h.substr(8,4)+" "+h.substr(12,4);}
uint64_t relayId(const PublicKey& k){
    std::string text="coop relay id ";text.append(reinterpret_cast<const char*>(k.data()),k.size());
    uint8_t h[64];crypto_sha512(h,reinterpret_cast<const uint8_t*>(text.data()),text.size());
    uint64_t v=0;for(int i=0;i<8;++i)v|=uint64_t(h[i])<<(8*i);
    return (v&((uint64_t(1)<<49)-1))+1;
}
std::string newNonce(){
    uint8_t n[NonceBytes];if(!randomBytes(n,sizeof n))throw std::runtime_error("the system's random number generator gave nothing");
    return hex(n,sizeof n);}
std::string requestText(const std::string& method,const std::string& target,const std::string& server,int64_t time,const std::string& nonce,const std::string& body){
    std::string out=Magic+"\n"+method+"\n";
#ifndef MUTANT_SIGN_TARGET
    out+=target;
#else
    (void)target;
#endif
    out+="\n";
#ifndef MUTANT_SIGN_SERVER
    out+=server;
#else
    (void)server;
#endif
    out+="\n"+std::to_string(time)+"\n"+nonce+"\n";
#ifndef MUTANT_SIGN_BODY
    out+=sha512Hex(body);
#else
    (void)body;
#endif
    return out;
}
std::vector<std::pair<std::string,std::string>> signRequest(const Keys& k,const std::string& method,const std::string& target,const std::string& server,
                                                            const std::string& body,int64_t time,const std::string& nonce){
    auto s=sign(k,requestText(method,target,server,time,nonce,body));
    return {{"X-Coop-Key",hex(k.pub)},{"X-Coop-Time",std::to_string(time)},{"X-Coop-Nonce",nonce},{"X-Coop-Signature",hex(s)}};
}
}
