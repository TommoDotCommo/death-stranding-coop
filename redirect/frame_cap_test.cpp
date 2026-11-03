#include "frame_cap.h"
#include <cstdio>
using namespace redirect::frame_cap;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"frame cap oracle: %s\n",#x);++failures;}}while(0)
int main(){
    uint32_t fps=7;
    CHECK(parse(nullptr,fps)&&fps==0);CHECK(parse(L"",fps)&&fps==0);CHECK(parse(L"0",fps)&&fps==0);
    CHECK(parse(L"15",fps)&&fps==15);CHECK(parse(L"240",fps)&&fps==240);
    CHECK(!parse(L"241",fps));CHECK(!parse(L"1000",fps));CHECK(!parse(L"-5",fps));CHECK(!parse(L"15fps",fps));CHECK(!parse(L"1.5",fps));
    {   // Off: never waits.
        Cap c;CHECK(c.delay(0,false)==0);CHECK(c.delay(1000,false)==0);CHECK(c.delay(2000,false)==0);
    }
    {   // Foreground: never waits, however fast the frames come.
        Cap c(10);uint64_t total=0;for(uint64_t t=0;t<1000000;t+=5000)total+=c.delay(t,true);CHECK(total==0);
    }
    {   // Background at 10 fps: frames 5 ms apart are spaced to 100 ms.
        Cap c(10);CHECK(c.delay(0,false)==0);CHECK(c.delay(5000,false)==95000);
        CHECK(c.delay(100000+5000,false)==95000);
        // Simulated loop: frame work 5 ms, then the wait. Ten frames take one second.
        Cap d(10);uint64_t t=0;unsigned frames=0;while(t<1000000){t+=d.delay(t,false);++frames;t+=5000;}
        CHECK(frames>=10&&frames<=11);
    }
    {   // A late frame runs immediately, and the next one is a full period after it (no catch-up burst).
        Cap c(10);c.delay(0,false);CHECK(c.delay(350000,false)==0);CHECK(c.delay(351000,false)==99000);
    }
    {   // Coming back to the background after the foreground: spacing starts from the last foreground frame.
        Cap c(10);c.delay(0,false);c.delay(50000,true);c.delay(60000,true);CHECK(c.delay(61000,false)==99000);
    }
    if(!failures)std::puts("background frame cap checks passed");
    return failures?1:0;
}
