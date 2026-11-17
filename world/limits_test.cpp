// Limits per address (limits.h, README "Limits per address"): connections at once, requests a second with a burst,
// a block after too many wrong guesses (404) in a minute, loopback exempt unless asked, and a bounded table of
// addresses that forgets idle ones first and turns new ones away when every one is busy.
#include "limits.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"limits oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
constexpr uint32_t A=0x0a000001,B=0x0a000002,Loop=0x7f000001;
int main(){
    {   // Connections at once: the cap per address; another address is not affected; a closed one frees its place.
        Gate g(GateConfig{3,0,0,0,1000,16,false});
        for(int i=0;i<3;++i)CHECK(g.open(A,0)==Gate::Open::Ok);
        CHECK(g.open(A,0)==Gate::Open::Busy);
        CHECK(g.open(B,0)==Gate::Open::Ok);
        g.close(A);CHECK(g.open(A,0)==Gate::Open::Ok);
        CHECK(g.open(A,0)==Gate::Open::Busy);}
    {   // Rate: the burst at once, then the rate; one second later the rate's worth again.
        Gate g(GateConfig{0,10,20,0,1000,16,false});
        int taken=0;for(int i=0;i<100;++i)taken+=g.request(A,1000);
        CHECK(taken==20);
        CHECK(!g.request(A,1000));
        taken=0;for(int i=0;i<100;++i)taken+=g.request(A,2000);
        CHECK(taken==10);
        CHECK(g.request(B,2000));   // another address has its own bucket
        // Ten seconds of quiet fill the bucket to the burst, not beyond it.
        taken=0;for(int i=0;i<100;++i)taken+=g.request(A,60000);
        CHECK(taken==20);}
    {   // Strikes: 404s in a minute block the address (connections and requests), only that one, and only for blockMs.
        Gate g(GateConfig{0,0,0,5,10000,16,false});
        for(int i=0;i<4;++i)CHECK(!g.answered(A,404,100));
        CHECK(!g.blocked(A,100));
        CHECK(g.answered(A,404,100));   // the fifth blocks
        CHECK(g.blocked(A,100));
        CHECK(g.open(A,200)==Gate::Open::Blocked);
        CHECK(!g.request(A,200));
        CHECK(g.open(B,200)==Gate::Open::Ok);
        CHECK(g.open(A,10200)==Gate::Open::Ok);   // over
        // Other answers are no strikes; strikes spread over more than a minute do not add up.
        Gate h(GateConfig{0,0,0,3,10000,16,false});
        for(int i=0;i<10;++i){h.answered(A,200,0);h.answered(A,403,0);h.answered(A,401,0);}
        CHECK(!h.blocked(A,0));
        h.answered(A,404,0);h.answered(A,404,30000);h.answered(A,404,61000);h.answered(A,404,90000);
        CHECK(!h.blocked(A,90000));}
    {   // Loopback is exempt by default, and limited when asked.
        Gate g(GateConfig{1,1,1,1,10000,16,false});
        for(int i=0;i<5;++i)CHECK(g.open(Loop,0)==Gate::Open::Ok);
        for(int i=0;i<5;++i)CHECK(g.request(Loop,0));
        CHECK(!g.answered(Loop,404,0));
        Gate h(GateConfig{1,1,1,1,10000,16,true});
        CHECK(h.open(Loop,0)==Gate::Open::Ok);CHECK(h.open(Loop,0)==Gate::Open::Busy);}
    {   // The table: at most `tracked` addresses. An idle one is forgotten for a new one; when every one holds a
        // connection the new address is turned away (Full) and nothing is forgotten.
        Gate g(GateConfig{4,10,10,0,1000,2,false});
        CHECK(g.open(A,0)==Gate::Open::Ok);
        CHECK(g.open(B,0)==Gate::Open::Ok);
        CHECK(g.open(0x0a000003,0)==Gate::Open::Full);
        CHECK(g.tracked()==2);
        g.close(B);
        CHECK(g.open(0x0a000003,5000)==Gate::Open::Ok);   // B was idle: forgotten
        CHECK(g.tracked()==2);
        // A blocked address is never forgotten early (forgetting would lift its block).
        Gate h(GateConfig{4,0,0,1,100000,1,false});
        CHECK(h.answered(A,404,0));
        CHECK(h.open(B,10)==Gate::Open::Full);
        CHECK(h.blocked(A,10));}
    if(failures){std::fprintf(stderr,"%u limits checks failed\n",failures);return 1;}
    std::puts("limits: ok");return 0;
}
