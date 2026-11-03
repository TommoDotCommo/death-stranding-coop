#pragma once
// auto_continue (mod.json): from the title screen, load the last save with the game's own
// Application_sExportedContinueLastSave (0x2642a00, no arguments), so a test copy reaches gameplay without menu input or
// window focus. The game itself refuses the request whenever a continue is not allowed (a load already requested, or a
// state that cannot change to "continue": 0x26376e0), so asking every couple of seconds is safe. It stops for good once
// the porter is in the world: the mod never loads a save during play.
#include <cstdint>
namespace mod {
class AutoContinue {
public:
    static constexpr uint64_t FirstAfter=5000,Every=2000,GiveUpAfter=600000;   // ms since the mod started
    explicit AutoContinue(bool enabled=false):enabled_(enabled){}
    // One call per game-thread tick. True: ask the game to continue now.
    bool tick(uint64_t sinceStart,bool porterInWorld){
        if(!enabled_||done_)return false;
#ifndef MUTANT_AUTO_ONCE_IN_GAME
        if(porterInWorld){done_=true;return false;}
#endif
        if(sinceStart<FirstAfter||sinceStart>GiveUpAfter)return false;
        if(asked_&&sinceStart-last_<Every)return false;
        asked_=true;last_=sinceStart;++calls_;return true;
    }
    bool done() const {return done_;}
    unsigned calls() const {return calls_;}
private:
    bool enabled_=false,done_=false,asked_=false;uint64_t last_=0;unsigned calls_=0;
};
}
