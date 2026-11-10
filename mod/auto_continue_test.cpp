#include "auto_continue.h"
#include <cstdio>
using mod::AutoContinue;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"auto continue oracle: %s\n",#x);++failures;}}while(0)
int main(){
    {AutoContinue a;for(uint64_t t=0;t<20000;t+=50)CHECK(!a.tick(t,false));}            // off by default
    {AutoContinue a(true);unsigned n=0;for(uint64_t t=0;t<5000;t+=50)n+=a.tick(t,false);CHECK(n==0);   // not during start-up
     n=0;for(uint64_t t=5000;t<15000;t+=50)n+=a.tick(t,false);CHECK(n==5);                // every 2 s
     CHECK(!a.tick(15000,true)&&a.done());                                                 // in the world: done
     n=0;for(uint64_t t=15050;t<60000;t+=50)n+=a.tick(t,false);CHECK(n==0);               // never again (death, menus, reloads)
     CHECK(a.calls()==5);}
    {AutoContinue a(true);unsigned n=0;for(uint64_t t=0;t<700000;t+=1000)n+=a.tick(t,false);CHECK(n==298);   // gives up after 10 min
    }
    {   // A test sends the game back to the title and asks again: one more cycle from that moment, then done for good.
        AutoContinue a(true);unsigned n=0;for(uint64_t t=5000;t<9000;t+=50)n+=a.tick(t,false);CHECK(!a.tick(9000,true)&&a.done()&&n==2);
        n=0;for(uint64_t t=9050;t<100000;t+=50)n+=a.tick(t,false);CHECK(n==0);
        a.again(100000);CHECK(!a.done());
        n=0;for(uint64_t t=100000;t<106000;t+=50)n+=a.tick(t,false);CHECK(n==3);        // at once, then every 2 s
        CHECK(!a.tick(106000,true)&&a.done());
        n=0;for(uint64_t t=106050;t<200000;t+=50)n+=a.tick(t,false);CHECK(n==0);
        AutoContinue off;off.again(5000);for(uint64_t t=5000;t<20000;t+=50)CHECK(!off.tick(t,false));   // never without the config
    }
    if(!failures)std::puts("auto continue checks passed");
    return failures?1:0;
}
