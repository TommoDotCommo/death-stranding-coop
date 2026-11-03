#include "virtual_pad.h"
#include <cstdio>
using namespace redirect::pad;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"pad oracle: %s\n",#x);++failures;}}while(0)
int main(){
    bool on=true;
    CHECK(parse(nullptr,on)&&!on);CHECK(parse(L"",on)&&!on);CHECK(parse(L"0",on)&&!on);CHECK(parse(L"1",on)&&on);
    CHECK(!parse(L"yes",on));CHECK(!parse(L"10",on));
    CHECK(replaces(L"xinput1_4.dll","XInputGetState"));CHECK(replaces(L"XINPUT1_4.DLL","XInputGetState"));
    CHECK(!replaces(L"xinput1_4.dll","XInputSetState"));CHECK(!replaces(L"xinput1_4.dll","XInputGetStateEx"));
    CHECK(!replaces(L"xinput1_3.dll","XInputGetState"));CHECK(!replaces(L"kernel32.dll","XInputGetState"));
    CHECK(!replaces(L"xinput1_4.dll.bak","XInputGetState"));CHECK(!replaces(nullptr,"XInputGetState"));
    CHECK(!replaces(L"xinput1_4.dll",reinterpret_cast<const char*>(uintptr_t(100))));   // ordinal 100 (GetStateEx)
    Overlay o;State s{};Gamepad a;a.buttons=0x1000;
    CHECK(!o.apply(0,false,a,s)&&s.packet==0);
    CHECK(!o.apply(1,true,a,s)&&s.packet==0);
    CHECK(o.apply(0,true,a,s)&&s.packet==1&&s.pad.buttons==0x1000);
    CHECK(o.apply(0,true,a,s)&&s.packet==1);
    Gamepad b;b.ly=32767;CHECK(o.apply(0,true,b,s)&&s.packet==2&&s.pad.ly==32767&&s.pad.buttons==0);
    Gamepad n;CHECK(o.apply(0,true,n,s)&&s.packet==3);
    if(!failures)std::puts("virtual pad lookup and overlay checks passed");
    return failures?1:0;
}
