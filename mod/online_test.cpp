// A private server's address (online.h): only a numeric loopback or partner address is accepted, and the game is
// asked to sign in only while its login task is idle, a bounded number of times.
#include "online.h"
#include <cstdio>
using namespace mod::online;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"online oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    const uint32_t peer=0xc0a80114;   // 192.168.1.20
    CHECK(mod::onlineAddress("http://127.0.0.1:8089/auth/ds",peer));
    CHECK(mod::onlineAddress("http://192.168.1.20:8089/auth/ds",peer));
    CHECK(!mod::onlineAddress("http://192.168.1.21:8089/auth/ds",peer));           // not the partner
    CHECK(!mod::onlineAddress("http://8.8.8.8:80/auth",0x7f000001));
    CHECK(!mod::onlineAddress("https://127.0.0.1:8089/auth/ds",peer));             // no TLS to a private server
    CHECK(!mod::onlineAddress("http://prod-pc-15.wws-gs2.com:80/auth/ds",peer));   // names are refused
    CHECK(!mod::onlineAddress("http://127.0.0.1/auth/ds",peer));                   // a port is required
    CHECK(!mod::onlineAddress("http://127.0.0.1:0/auth",peer));
    CHECK(!mod::onlineAddress("http://127.0.0.1:65536/auth",peer));
    CHECK(!mod::onlineAddress("http://127.0.0.1:8089",peer));                      // a path is required
    CHECK(!mod::onlineAddress("http://127.0.0.1:8089/a@8.8.8.8/x",peer));          // nothing that could redirect the host
    CHECK(!mod::onlineAddress("http://127.0.0.1:8089/a?b=c",peer));
    CHECK(!mod::onlineAddress("http://127.0.0.1:8089/"+std::string(mod::OnlineAddressMax,'a'),peer));
    State none;Requester r;CHECK(!r.tick(1000,none));CHECK(r.asks()==0);
    State idle;idle.present=true;
    CHECK(r.tick(1000,idle));CHECK(!r.tick(1000+Requester::Every-1,idle));CHECK(r.tick(1000+Requester::Every,idle));
    State busy=idle;busy.taskState=2;busy.step=3;Requester b;CHECK(!b.tick(5000,busy));
    State in=idle;in.taskState=2;in.step=7;CHECK(in.signedIn());CHECK(!b.tick(5000,in));
    State failed=idle;failed.taskState=2;failed.step=8;CHECK(b.tick(5000,failed));   // a failed sign-in is asked again
    Requester m;unsigned asked=0;for(uint64_t t=0;t<100*Requester::Every;t+=Requester::Every)asked+=m.tick(1000+t,idle);
    CHECK(asked==Requester::MaxAsks);
    if(failures)return 1;
    std::puts("online tests passed");return 0;
}
