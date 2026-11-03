#pragma once
// Opt-in background frame cap (COOP_BACKGROUND_FPS=n) for two copies on one PC. Two full copies time-slice one GPU,
// and each loses about two thirds of its frame rate even though neither fills it. With the cap, the copy whose window
// is not in the foreground waits on its own game thread, in the frame's empty PeekMessage, until 1/n s has passed since
// its last frame; the copy in the foreground is never delayed. Only the game thread waits: the mod's network worker
// keeps running at full rate. Pure logic here, pinned by frame_cap_test.cpp.
#include <cstdint>
#include <cwchar>
namespace redirect::frame_cap {
constexpr uint32_t MaxFps=240;
// Absent, empty or "0" = off; 1..240 = frames per second while in the background; anything else refuses startup.
inline bool parse(const wchar_t* v,uint32_t& fps){
    fps=0;if(!v||!*v)return true;
    uint32_t n=0;
    for(auto p=v;*p;++p){if(*p<L'0'||*p>L'9'||p-v>=3)return false;n=n*10+uint32_t(*p-L'0');}
    if(n>MaxFps)return false;
    fps=n;return true;
}
class Cap {
    uint32_t fps_=0;uint64_t last_=0;bool started_=false;
public:
    explicit Cap(uint32_t fps=0):fps_(fps){}
    uint32_t fps() const {return fps_;}
    // Called once per game frame. Returns how long to wait (microseconds) before letting the frame continue.
    uint64_t delay(uint64_t nowUs,bool foreground){
        if(!fps_||!started_){started_=true;last_=nowUs;return 0;}
#ifndef MUTANT_CAP_FOCUSED
        if(foreground){last_=nowUs;return 0;}
#endif
        uint64_t period=1000000/fps_,due=last_+period;
        // A frame that is already late runs now. The schedule restarts from it: a stall is never paid back with a burst.
#ifndef MUTANT_CAP_BURST
        if(nowUs>=due){last_=nowUs;return 0;}
#else
        if(nowUs>=due){last_=due;return 0;}
#endif
        last_=due;return due-nowUs;
    }
};
}
