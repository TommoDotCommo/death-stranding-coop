#pragma once
// Opt-in virtual gamepad (COOP_VIRTUAL_PAD=1) so tests can drive a disposable copy without a person or a focused
// window. The game loads xinput1_4.dll itself and resolves XInputGetState through GetProcAddress, which the proxy
// already hooks. Only that one by-name lookup is replaced; the wrapper calls the real function and, for user 0 while
// the mod reports an active pad script, substitutes the scripted gamepad. Pure logic here, pinned by pad_test.cpp.
#include <cstdint>
#include <cstring>
#include <cwchar>
namespace redirect::pad {
struct Gamepad {uint16_t buttons=0;uint8_t leftTrigger=0,rightTrigger=0;int16_t lx=0,ly=0,rx=0,ry=0;};
struct State {uint32_t packet=0;Gamepad pad;};   // XINPUT_STATE layout
static_assert(sizeof(Gamepad)==12&&sizeof(State)==16,"XInput layout");
// Absent, empty or "0" = off; "1" = on; anything else refuses startup.
inline bool parse(const wchar_t* v,bool& on){
    if(!v||!*v||!std::wcscmp(v,L"0")){on=false;return true;}
    if(!std::wcscmp(v,L"1")){on=true;return true;}
    return false;
}
inline bool sameName(const wchar_t* a,const wchar_t* b){
    for(;*a&&*b;++a,++b){auto x=*a>=L'A'&&*a<=L'Z'?*a+32:*a,y=*b>=L'A'&&*b<=L'Z'?*b+32:*b;if(x!=y)return false;}
    return *a==*b;
}
// moduleBase is the loaded module's file name without directory. Ordinal lookups are never replaced.
inline bool replaces(const wchar_t* moduleBase,const char* name){
    if(!name||(reinterpret_cast<uintptr_t>(name)>>16)==0||!moduleBase)return false;
#ifndef MUTANT_PAD_ANY_NAME
    if(std::strcmp(name,"XInputGetState"))return false;
#endif
#ifndef MUTANT_PAD_ANY_MODULE
    if(!sameName(moduleBase,L"xinput1_4.dll"))return false;
#endif
    return true;
}
// Fills out when the pad is active for this user. The packet number advances whenever the content changes,
// as XInput requires for the game to notice.
class Overlay {
    Gamepad last_{};uint32_t packet_=0;
public:
    bool apply(uint32_t user,bool active,const Gamepad& pad,State& out){
#ifndef MUTANT_PAD_ALL_USERS
        if(user!=0)return false;
#endif
        if(!active)return false;
#ifndef MUTANT_PAD_STATIC_PACKET
        if(std::memcmp(&pad,&last_,sizeof pad)||!packet_)++packet_;
#endif
        last_=pad;out.packet=packet_;out.pad=pad;return true;
    }
};
}
