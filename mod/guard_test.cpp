#include "guard_win.h"
#include <cstdio>
using namespace mod::guard;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"guard oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    const uint64_t site=0x7ff700000000ull+0x22bdf1a;
    CHECK(matches(0xc0000005u,8,0,0,site,site));                 // execute fault at zero, returning to the call site
    CHECK(!matches(0xc0000005u,8,0,0,site+1,site));              // another caller
    CHECK(!matches(0xc0000005u,0,0,0,site,site));                // a read fault
    CHECK(!matches(0xc0000005u,8,0x10,0,site,site));             // another address
    CHECK(!matches(0xc0000005u,8,0,0x1234,site,site));           // not at zero
    CHECK(!matches(0x80000003u,8,0,0,site,site));                // another exception
    CHECK(!matches(0xc0000005u,8,0,0,site,0));                   // not installed
    if(!failures)std::puts("creation callback guard checks passed");
    return failures?1:0;
}
