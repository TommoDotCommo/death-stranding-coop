// Players' keys (identity.h): Ed25519 against RFC 8032's test vectors, SHA-512, the key and account forms, and the
// server's check of a signed call: the right key, a call changed on the way (method, address, server, time, nonce,
// body), another key, outside the time window, taken twice, and the nonces remembered within their bounds.
#include "signatures.h"
#include <cctype>
#include <cstdio>
using namespace world::identity;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"identity oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
template<size_t N> static std::array<uint8_t,N> bytes(const std::string& h){std::array<uint8_t,N> a{};CHECK(unhex(h,a.data(),N));return a;}
static std::string raw(const std::string& h){std::string s(h.size()/2,'\0');CHECK(unhex(h,reinterpret_cast<uint8_t*>(&s[0]),s.size()));return s;}
static void vectors(){
    // RFC 8032, 7.1: TEST 1, 2, 3 and SHA(abc) (whose message is SHA-512 of "abc").
    struct V{const char *secret,*pub,*message,*signature;};
    const V all[]={
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60","d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a","",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb","3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c","72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7","fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025","af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
        {"833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42","ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
         "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"}};
    for(auto& v:all){
        auto k=keysFromSeed(bytes<32>(v.secret));
        CHECK(hex(k.pub)==v.pub);
        auto s=sign(k,raw(v.message));CHECK(hex(s)==v.signature);
        CHECK(verify(k.pub,raw(v.message),s));
        // One bit off anywhere: the message, the signature, the key.
        auto m=raw(v.message)+"x";CHECK(!verify(k.pub,m,s));
        auto bad=s;bad[5]^=1;CHECK(!verify(k.pub,raw(v.message),bad));
        bad=s;bad[40]^=0x80;CHECK(!verify(k.pub,raw(v.message),bad));
        auto other=k.pub;other[0]^=1;CHECK(!verify(other,raw(v.message),s));}
    CHECK(sha512Hex("abc")=="ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}
static void forms(){
    auto a=makeKeys(),b=makeKeys();
    CHECK(a.pub!=b.pub&&a.seed!=b.seed&&keysFromSeed(a.seed).pub==a.pub);
    auto acc=account(a.pub);PublicKey back{};
    CHECK(acc.size()==72&&acc.compare(0,8,"ed25519-")==0&&accountIsKey(acc)&&keyOf(acc,back)&&back==a.pub);
    for(const char* no:{"","ed25519-","ann","76561197960287930","ED25519-00","ed25519-0000000000000000000000000000000000000000000000000000000000000000x",
                        "ed25519-000000000000000000000000000000000000000000000000000000000000000","ed25519-AAAA000000000000000000000000000000000000000000000000000000000000"})
        CHECK(!accountIsKey(no)&&!keyOf(no,back));
    CHECK(shortText(a.pub)==hex(a.pub).substr(0,4)+" "+hex(a.pub).substr(4,4)+" "+hex(a.pub).substr(8,4)+" "+hex(a.pub).substr(12,4));
    // The relay id: the same for the same key, another for another, within 1..2^49.
    CHECK(relayId(a.pub)==relayId(keysFromSeed(a.seed).pub)&&relayId(a.pub)!=relayId(b.pub));
    for(int i=0;i<200;++i){auto k=makeKeys();auto id=relayId(k.pub);CHECK(id>=1&&id<=(uint64_t(1)<<49));}
    auto n=newNonce(),m=newNonce();uint8_t x[16];CHECK(n.size()==32&&unhex(n,x,16)&&n!=m);
    uint8_t y[2];CHECK(unhex("aB0f",y,2)&&y[0]==0xab&&y[1]==0x0f&&!unhex("abc",y,2)&&!unhex("zz00",y,2)&&!unhex("00000",y,2));
}
// A request as the server parses it: header names lower-case.
struct Call { std::string method="POST",target="/w1/coop/choices",body="{\"player\":\"x\"}",server="SERVERID00000001";int64_t time=1000000;std::map<std::string,std::string> headers; };
static Call signedCall(const Keys& k,Call c={}){
    for(auto& h:signRequest(k,c.method,c.target,c.server,c.body,c.time,newNonce())){std::string name=h.first;for(auto& ch:name)ch=char(std::tolower(static_cast<unsigned char>(ch)));c.headers[name]=h.second;}
    return c;}
static Checker::Result run(Checker& checker,const Call& c,int64_t now,std::string* who=nullptr,std::string* why=nullptr){
    std::string a,w;auto r=checker.check(c.headers,c.method,c.target,c.body,c.server,now,a,w);if(who)*who=a;if(why)*why=w;return r;}
static void checks(){
    using R=Checker::Result;
    auto k=makeKeys(),other=makeKeys();
    {   // The right call is taken once, and names its signer.
        Checker c;auto call=signedCall(k);std::string who,why;
        CHECK(run(c,call,1000000,&who)==R::Ok&&who==account(k.pub)&&c.remembered()==1);
        CHECK(run(c,call,1000001,&who,&why)==R::Bad&&why.find("taken already")!=std::string::npos);
        CHECK(run(c,signedCall(k),1000001)==R::Ok&&c.remembered()==2);
        Call none;CHECK(run(c,none,1000000)==R::Unsigned);}
    {   // Anything changed on the way: refused, and nothing remembered for it.
        Checker c;auto call=signedCall(k);
        auto changed=call;changed.body="{\"player\":\"y\"}";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.target="/w1/coop/withdraw";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.method="GET";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.server="SERVERID00000002";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.headers["x-coop-time"]="1000001";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.headers["x-coop-nonce"]=newNonce();CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.headers["x-coop-key"]=hex(other.pub);CHECK(run(c,changed,1000000)==R::Bad);   // another player's key, this signature
        changed=signedCall(other,call);changed.headers["x-coop-key"]=hex(k.pub);CHECK(run(c,changed,1000000)==R::Bad);   // signed by another for this key
        CHECK(c.remembered()==0);
        // Headers missing or in the wrong form.
        for(const char* h:{"x-coop-key","x-coop-time","x-coop-nonce","x-coop-signature"}){changed=call;changed.headers.erase(h);CHECK(run(c,changed,1000000)==R::Bad);}
        changed=call;changed.headers["x-coop-time"]="-5";CHECK(run(c,changed,1000000)==R::Bad);
        changed=call;changed.headers["x-coop-nonce"]="00";CHECK(run(c,changed,1000000)==R::Bad);
        CHECK(run(c,call,1000000)==R::Ok);}
    {   // The window: at most WindowSeconds either way, by the server's clock.
        Checker c;std::string why;
        CHECK(run(c,signedCall(k),1000000+WindowSeconds)==R::Ok&&run(c,signedCall(k),1000000-WindowSeconds)==R::Ok);
        CHECK(run(c,signedCall(k),1000000+WindowSeconds+1,nullptr,&why)==R::Bad&&why.find("time")!=std::string::npos);
        CHECK(run(c,signedCall(k),1000000-WindowSeconds-1)==R::Bad);
        // A call taken once stays refused while its time is in the window; after it, the window refuses it.
        auto call=signedCall(k);CHECK(run(c,call,1000000)==R::Ok);
        for(int64_t now:{int64_t(1000000),int64_t(1000100),int64_t(1000000+WindowSeconds),int64_t(1000000+WindowSeconds+1),int64_t(1000000+10*WindowSeconds)})CHECK(run(c,call,now)==R::Bad);}
    {   // Bounded: per key, and in all; when full the call is refused (a nonce that could still be replayed is never
        // forgotten early), and room comes back as times leave the window.
        Checker c(64,16);Call quick;quick.body="";
        for(size_t i=0;i<16;++i)CHECK(run(c,signedCall(k,quick),1000000)==R::Ok);
        std::string why;CHECK(run(c,signedCall(k,quick),1000000,nullptr,&why)==R::Bad&&why.find("too many")!=std::string::npos);
        CHECK(c.remembered()==16&&run(c,signedCall(other,quick),1000000)==R::Ok);
        auto later=quick;later.time=1000000+WindowSeconds+1;CHECK(run(c,signedCall(k,later),1000000+WindowSeconds+1)==R::Ok&&c.remembered()==1);
        Checker all(64,16);size_t taken=0;
        for(size_t i=0;i<5;++i){auto one=makeKeys();for(size_t n=0;n<16;++n)taken+=run(all,signedCall(one,quick),1000000)==R::Ok;}
        CHECK(taken==64&&all.remembered()==64);
        Checker defaults;CHECK(NoncesMax>=NoncesPerKey*16&&NoncesPerKey>=256);}
}
int main(){
    vectors();forms();checks();
    if(failures)return 1;
    std::puts("identity tests passed");return 0;
}
