#pragma once
// Players' keys (README, "Identity"). Every launcher makes one Ed25519 key pair (RFC 8032) once and keeps it; the
// public key is the player on a server whose policy says `identity` keys, and the secret half never leaves his PC.
// The launcher signs its calls to the server's API (api.h): the method, the address it asks, the world's server id,
// the time, a fresh nonce and a hash of the body. The server takes a signed call once, within a time window, and only
// for the player the key belongs to. The game's own HTTP calls cannot be signed: the launcher asks for a short-lived
// sign-in ticket and a relay token instead (world.h). The server's check is signatures.h. The crypto is Monocypher's
// (../vendor/monocypher).
//
// Signed request headers (names lower-case on the server side):
//   X-Coop-Key        64 hex digits: the public key
//   X-Coop-Time       the signer's clock, Unix seconds (the launcher corrects it by the server's `time` in /coop/policy)
//   X-Coop-Nonce      32 hex digits, random, never used twice
//   X-Coop-Signature  128 hex digits: Ed25519 over requestText()
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace world::identity {
constexpr size_t KeyBytes=32,SignatureBytes=64,NonceBytes=16;
using PublicKey=std::array<uint8_t,KeyBytes>;
using Seed=std::array<uint8_t,KeyBytes>;
using Signature=std::array<uint8_t,SignatureBytes>;
constexpr int64_t WindowSeconds=300;                  // a signed call's time is at most this far from the server's clock
// The account key of a player who has a key: "ed25519-" and the public key in 64 lower-case hex digits. No other
// account (a Steam id, a /k/NAME) has this form, and the server makes one only for a call signed with that key.
inline constexpr const char* AccountPrefix="ed25519-";
inline bool accountIsKey(const std::string& a){
    if(a.size()!=8+2*KeyBytes||a.compare(0,8,AccountPrefix)!=0)return false;
    for(size_t i=8;i<a.size();++i)if(!((a[i]>='0'&&a[i]<='9')||(a[i]>='a'&&a[i]<='f')))return false;
    return true;}
bool randomBytes(uint8_t*,size_t);                    // the system's generator; false when it gave nothing
struct Keys { Seed seed{};PublicKey pub{}; };         // seed: the secret half (RFC 8032's private key)
Keys keysFromSeed(const Seed&);
Keys makeKeys();                                      // throws std::runtime_error without the system's generator
Signature sign(const Keys&,const std::string& message);
bool verify(const PublicKey&,const std::string& message,const Signature&);
std::string sha512Hex(const std::string&);
std::string hex(const uint8_t*,size_t);
template<size_t N> std::string hex(const std::array<uint8_t,N>& a){return hex(a.data(),N);}
bool unhex(const std::string&,uint8_t* out,size_t bytes);   // exactly 2*bytes hex digits
std::string account(const PublicKey&);                // "ed25519-<hex>"
bool keyOf(const std::string& account,PublicKey&);    // the key of such an account
std::string shortText(const PublicKey&);              // the first 16 hex digits in fours, for a screen: "a1b2 c3d4 e5f6 a7b8"
// The relay id of a player with a key (core/relay.h HELLO): made from the key, so nobody can take another's id.
// 1..2^49, inside the 15 digits the mod's config takes.
uint64_t relayId(const PublicKey&);
std::string newNonce();                               // 32 hex digits from the system's generator
// What is signed: every part of the call that the server acts on, one per line.
std::string requestText(const std::string& method,const std::string& target,const std::string& server,int64_t time,const std::string& nonce,const std::string& body);
// The four headers of a signed call (names as sent).
std::vector<std::pair<std::string,std::string>> signRequest(const Keys&,const std::string& method,const std::string& target,const std::string& server,
                                                            const std::string& body,int64_t time,const std::string& nonce);
}
