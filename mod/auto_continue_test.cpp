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
    if(!failures)std::puts("auto continue checks passed");
    return failures?1:0;
}
