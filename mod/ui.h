#pragma once
// Which screen the game shows: a read-only sampler for scripted tests (game_offsets.md, "Which screen is up"). Nothing
// here writes to the game or calls it.
//
// Two sources, each checked by the vtable of every object on the way:
//  - the game's own state flags: *(image+0x7bf6e88) (vtable image+0x3dfedb0), 64 bits at +8 (this frame) and +0x10
//    (asked for, latched into +8 once a frame). Bits 0..30 carry the names of the game's script enum for them; bits
//    31.. the names of the second script enum (bit 31 + n).
//  - the menu: *(image+0x57965f8) (vtable image+0x3bbd988), its resource at +0x20 (vtable image+0x3e67750, 19 page
//    slots from +0x1e8, in the order of the game's page id enum), its page manager at +0xb0 (vtable image+0x3bc8820,
//    owner at +8, the open page at +0x10) and its layer manager at +0xb8 (vtable image+0x3bc88d8, two counted lists).
//    A page is a named resource: 16 identity bytes at +8, the name at +0x20.
// The highlighted entry is not known yet: `dump` writes the candidate objects for a live session to settle it.
#include "discovery.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace mod::ui {
constexpr uint64_t GameState=0x7bf6e88, GameStateVtable=0x3dfedb0, FlagsNow=0x8, FlagsNext=0x10;
constexpr uint64_t Menu=0x57965f8, MenuVtable=0x3bbd988, MenuResource=0x20, MenuPhase=0x98, MenuPages=0xb0, MenuLayers=0xb8;
constexpr uint64_t PagesVtable=0x3bc8820, Owner=0x8, PagesCurrent=0x10;
constexpr uint64_t LayersVtable=0x3bc88d8, LayerCountA=0x10, LayerArrayA=0x18, LayerCountB=0x20, LayerArrayB=0x28;
constexpr uint64_t ResourceVtable=0x3e67750, PageTable=0x1e8, PageIdentity=0x8, PageName=0x20;
constexpr unsigned PageCount=19, MaxLayers=64, MaxName=48;
// The mask of the game's own "an event is stopping the game" test (0x2dd3760).
constexpr uint64_t StoppingMask=0x184f407fe47f7full;
constexpr uint64_t bit(unsigned n){return uint64_t(1)<<n;}
constexpr uint64_t LoadingMask=bit(22)|bit(29)|bit(12)|bit(31+10);            // Loading AreaChanging FastTravel WaitCloseLoadingScreen
constexpr uint64_t PausedMask=bit(2)|bit(28);                                 // PauseMenu SystemPause
constexpr uint64_t MenuMask=bit(0)|bit(1)|bit(4)|bit(5)|bit(7)|bit(11)|bit(31+15);   // terminals, cargo, cuff links, wheel, result, signboard
constexpr uint64_t CutsceneMask=bit(6)|bit(18)|bit(19)|bit(20)|bit(31+21)|bit(31+22);
inline const char* flagName(unsigned n){
    static const char* const names[59]={"TerminalOperation","TerminalOperationSimple","PauseMenu","PrivateRoom","BaggageSelect","HandcuffDevice",
        "Cutscene","WeaponSelector","Subspace","KnotSpace","StrideAreaEvent","Result","FastTravel","GameOver","Faint","BossBattle","AfterBeatBoss",
        "Annihilation","Movie","InGameCutscene","InGameActing","RelocateBaggage","Loading","HeartmanRoom","Renovation","Onsen","Setup","CallingRadio",
        "SystemPause","AreaChanging","DirectIntoPrivateRoom",
        "Night","WW1","M650Beach","HiggsFistfight","AllowPathRecord","PathRecordStraightMode","CancelMission","EventsRestricted","StayPrivateRoom",
        "RainSheleterSkipTime","WaitCloseLoadingScreen","PlayerDied","PlayerFainted","HUDHideImmediately","HUDHideFadeout","SignboardMenu",
        "ExplosionObjectSuspendRequest","ExplosionObjectRemoveRequest","CanMoveCameraForInGameCutscene","StaySubspace","MemoriesOfCliff",
        "EndingStaffRoll","HeavyCutscene","ExplosionObjectRemoveRequestForBeforeExplosion","ExplosionObjectRemoveRequestForDetonatableState",
        "RaceMode","FiringRangeMode","AfterBeatBossForTimer"};
    return n<59?names[n]:nullptr;
}
inline const char* pageIdName(int id){
    static const char* const names[PageCount]={"DeviceMenu","GameOverMenu","DemoPauseMenu","InGamePauseMenu","SkipMenu","MovieMenu","CommonDialogueMenu",
        "LoadingScreenMenu","TitleMenu","SplashScreenMenu","SignboardMenu","BirthdayMenu","InstallMenu","PrivateRoomPhotoMenu","SDRConfigMenu",
        "HDRConfigMenu","PhotoModeMenu","SaveDataLoadMenu","KeyAssignMenu"};
    return id>=0&&id<int(PageCount)?names[id]:nullptr;
}
enum class Kind:uint8_t{Unknown,Loading,Paused,Menu,Cutscene,Busy,Frontend,Gameplay};
inline const char* kindName(Kind k){static const char* const n[]={"unknown","loading","paused","menu","cutscene","busy","frontend","gameplay"};return n[size_t(k)];}
struct State {
    bool flagsKnown=false;uint64_t flags=0,next=0;        // the game's state flags, this frame and asked for
    bool menuKnown=false;uint64_t menu=0,pages=0,layers=0; // the menu object and its two managers (addresses, for dump)
    uint64_t page=0;int pageId=-1;std::string pageName;   // the open page: 0 none; id -1 when it is not one of the 19 slots
    std::array<uint8_t,16> pageIdentity{};
    uint32_t phase=0;int32_t layerA=0,layerB=0;
    bool inWorld=false;                                   // the porter is in the world (the caller's pose reader)
    Kind kind=Kind::Unknown;
};
// From the flags alone (the open page is reported, not judged): loading, then paused, menu, cutscene, any other state
// of the game's own stopping mask (busy); with none of them, gameplay when the porter is in the world, else frontend.
inline Kind classify(bool flagsKnown,uint64_t flags,bool inWorld){
#ifndef MUTANT_UI_UNKNOWN_IS_GAMEPLAY
    if(!flagsKnown)return Kind::Unknown;
#endif
    if(flags&LoadingMask)return Kind::Loading;
#ifndef MUTANT_UI_PAUSE_ORDER
    if(flags&PausedMask)return Kind::Paused;
    if(flags&MenuMask)return Kind::Menu;
    if(flags&CutsceneMask)return Kind::Cutscene;
#else
    if(flags&MenuMask)return Kind::Menu;
    if(flags&CutsceneMask)return Kind::Cutscene;
    if(flags&PausedMask)return Kind::Paused;
#endif
    if(flags&StoppingMask)return Kind::Busy;
    return inWorld?Kind::Gameplay:Kind::Frontend;
}
inline std::string flagNames(uint64_t flags){
    std::string out;
    for(unsigned n=0;n<64;++n)if(flags&bit(n)){if(!out.empty())out+='+';auto name=flagName(n);out+=name?name:"bit"+std::to_string(n);}
    return out.empty()?"-":out;
}
// One line, the same text while nothing changed:
//   menu flags=TerminalOperation next=TerminalOperation page=0:DeviceMenuPage phase=0 layers=1/0 world=1 item=unknown
inline std::string describe(const State& s){
    std::string out=kindName(s.kind);
    out+=" flags="+(s.flagsKnown?flagNames(s.flags):std::string("unknown"));
    if(s.flagsKnown&&s.next!=s.flags)out+=" next="+flagNames(s.next);
    if(!s.menuKnown)out+=" page=unknown";
    else if(!s.page)out+=" page=none phase="+std::to_string(s.phase)+" layers="+std::to_string(s.layerA)+"/"+std::to_string(s.layerB);
    else out+=" page="+(s.pageId>=0?std::to_string(s.pageId):std::string("other"))+":"+(s.pageName.empty()?"?":s.pageName)+" phase="+std::to_string(s.phase)+
              " layers="+std::to_string(s.layerA)+"/"+std::to_string(s.layerB);
    out+=std::string(" world=")+(s.inWorld?"1":"0")+" item=unknown";
    return out;
}
// A state expression for `waitui` and `puppet.ui wait`: alternatives joined by '|', each a '+'-joined list of terms that
// must all hold. A term is a kind (gameplay paused menu cutscene loading busy frontend), a flag name (PauseMenu),
// page:none, page:any, page:N (slot 0..18), page:NAME (the slot's enum name or the page's own name); '!' in front
// negates it. A term about something the reader could not read is false, negated or not.
namespace detail {
inline bool kindOf(const std::string& w,Kind& k){for(unsigned i=1;i<8;++i)if(w==kindName(Kind(i))){k=Kind(i);return true;}return false;}
inline int flagOf(const std::string& w){for(unsigned n=0;n<59;++n)if(w==flagName(n))return int(n);return -1;}
inline bool wordOk(const std::string& w){
    if(w.empty()||w.size()>MaxName)return false;
    for(char c:w)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
// 1 holds, 0 does not, -1 not a term. With s null only the form is checked.
inline int term(std::string w,const State* s){
    bool negate=!w.empty()&&w[0]=='!';if(negate)w.erase(0,1);
    Kind k;int result;
    if(kindOf(w,k)){if(!s)return 1;if(s->kind==Kind::Unknown)return 0;result=s->kind==k;}
    else if(int f=flagOf(w);f>=0){if(!s)return 1;if(!s->flagsKnown)return 0;result=(s->flags>>f)&1;}
    else if(w.compare(0,5,"page:")==0){
        auto p=w.substr(5);if(!wordOk(p))return -1;
        bool number=p.find_first_not_of("0123456789")==std::string::npos;
        if(number&&(p.size()>2||std::stoi(p)>=int(PageCount)))return -1;
        if(!s)return 1;
        if(!s->menuKnown)return 0;
        if(p=="none")result=!s->page;else if(p=="any")result=s->page!=0;
        else if(number)result=s->page&&s->pageId==std::stoi(p);
        else {auto slot=pageIdName(s->pageId);result=s->page&&((slot&&p==slot)||(!s->pageName.empty()&&p==s->pageName));}
    } else return -1;
#ifndef MUTANT_UI_NEGATE
    return negate?!result:result;
#else
    return result;
#endif
}
inline int all(const std::string& list,const State* s){
    size_t at=0;int result=1;
    for(;;){auto end=list.find('+',at);int t=term(list.substr(at,end==std::string::npos?end:end-at),s);if(t<0)return -1;if(!t)result=0;
        if(end==std::string::npos)return result;
        at=end+1;}
}
inline int any(const std::string& expr,const State* s){
    if(expr.empty()||expr.size()>120)return -1;
    size_t at=0;int result=0;
    for(;;){auto end=expr.find('|',at);int t=all(expr.substr(at,end==std::string::npos?end:end-at),s);if(t<0)return -1;if(t)result=1;
        if(end==std::string::npos)return result;
        at=end+1;}
}
}
inline bool validExpression(const std::string& expr){return detail::any(expr,nullptr)>=0;}
inline bool matches(const State& s,const std::string& expr){return detail::any(expr,&s)==1;}
// Every read is guarded: an object whose vtable is not the pinned one leaves its part unknown.
State read(const discovery::Sampler&,uint64_t base,bool inWorld);
// The candidate memory for the highlighted entry, as text rows (addresses, vtables as image offsets, the first bytes
// of the page manager, the layer manager and the first entries of its two lists). Read-only.
std::vector<std::string> dump(const discovery::Sampler&,uint64_t base);
// A line when the description changed, at most one look every Every ms (unless forced).
class Watch {
public:
    static constexpr uint64_t Every=250;
    bool due(uint64_t now) const {return !looked_||now-last_>=Every;}
    // True when the text differs from the last one given: log it.
    bool changed(const State& s,uint64_t now){looked_=true;last_=now;auto text=describe(s);
#ifndef MUTANT_UI_LOG_EVERY
        if(text==text_)return false;
#endif
        text_=text;return true;}
    const std::string& text() const {return text_;}
private:
    bool looked_=false;uint64_t last_=0;std::string text_;
};
}
