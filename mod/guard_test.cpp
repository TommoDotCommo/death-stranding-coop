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
    // The culling job's read of a drawable that left the tree: only those instructions, only with the null they
    // fault on; resumes at the function's epilogue.
    { const uint64_t base=0x7ff700000000ull,out=base+0x1d5cc09;
      CHECK(cullResume(0xc0000005u,0,base+0x1d5cb8a,0,3,base)==out);
      CHECK(cullResume(0xc0000005u,0,base+0x1d5cb94,0x1234,0,base)==out&&cullResume(0xc0000005u,0,base+0x1d5cb9a,0x1234,0,base)==out);
      CHECK(!cullResume(0xc0000005u,0,base+0x1d5cb8a,0x10,3,base));        // not the null leaf
      CHECK(!cullResume(0xc0000005u,0,base+0x1d5cb94,0,0x40,base));        // a block that is there
      CHECK(!cullResume(0xc0000005u,0,base+0x1d5cb8d,0,0,base));           // another instruction
      CHECK(!cullResume(0xc0000005u,1,base+0x1d5cb8a,0,3,base));           // a write
      CHECK(!cullResume(0x80000003u,0,base+0x1d5cb8a,0,3,base));           // another exception
      CHECK(!cullResume(0xc0000005u,0,base+0x1d5cb8a,0,3,0)); }            // not installed
    if(!failures)std::puts("creation callback guard checks passed");
    return failures?1:0;
}
