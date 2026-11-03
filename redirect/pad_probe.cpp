// Game-free Windows probe for the virtual pad: loads the proxy, starts the mod through a compiler export,
// then resolves XInputGetState the way the game does (LoadLibrary + GetProcAddress by name) and checks the
// scripted presses arrive for player 1 only. The harness writes the pad script between "ready" and "go".
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include <iostream>
#include <string>
namespace {
unsigned failures=0;std::string failed;
void check(bool ok,const char* name){if(!ok){std::cerr<<"pad probe oracle: "<<name<<'\n';failed+=std::string(failed.empty()?"":",")+name;++failures;}}
struct Gamepad{WORD buttons;BYTE lt,rt;SHORT lx,ly,rx,ry;};
struct State{DWORD packet;Gamepad pad;};
using GetStateFn=DWORD(WINAPI*)(DWORD,State*);
HMODULE owner(const void* p){HMODULE m=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(p),&m);return m;}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    auto proxy=LoadLibraryW(argv[1]);if(!proxy)return 3;
    using BlobFn=HRESULT(WINAPI*)(SIZE_T,IUnknown**);
    auto blob=reinterpret_cast<BlobFn>(reinterpret_cast<void*>(GetProcAddress(proxy,"D3DCreateBlob")));if(!blob)return 4;
    IUnknown* b=nullptr;check(blob(16,&b)==S_OK&&b,"compiler export");if(b)b->Release();   // starts the mod
    std::cout<<"{\"ready\":true,\"pid\":"<<GetCurrentProcessId()<<"}"<<std::endl;
    std::string go;std::getline(std::cin,go);
    auto xinput=LoadLibraryW(L"xinput1_4.dll");check(xinput!=nullptr,"xinput loads");if(!xinput)return 5;
    auto get=reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(GetProcAddress(xinput,"XInputGetState")));
    auto set=reinterpret_cast<void*>(GetProcAddress(xinput,"XInputSetState"));
    check(get&&owner(reinterpret_cast<void*>(get))==proxy,"XInputGetState wrapped by proxy");
    check(set&&owner(set)==xinput,"XInputSetState untouched");
    bool sawA=false,sawStick=false,otherUser=false;DWORD firstPacket=0,lastPacket=0;
    auto until=GetTickCount64()+6000;
    while(GetTickCount64()<until&&!(sawA&&sawStick)){
        State s{};auto r=get(0,&s);
        if(r==ERROR_SUCCESS&&s.pad.buttons==0x1000){sawA=true;if(!firstPacket)firstPacket=s.packet;}
        if(r==ERROR_SUCCESS&&s.pad.ly==32767&&s.pad.buttons==0){sawStick=true;lastPacket=s.packet;}
        State o{};if(get(1,&o)==ERROR_SUCCESS&&o.pad.buttons==0x1000)otherUser=true;
        Sleep(10);
    }
    check(sawA,"scripted A reaches player 1");check(sawStick,"scripted stick reaches player 1");
    check(lastPacket>firstPacket,"packet advances with content");check(!otherUser,"player 2 untouched");
    Sleep(800);State s{};auto r=get(0,&s);
    check(!(r==ERROR_SUCCESS&&(s.pad.buttons==0x1000||s.pad.ly==32767)),"script end returns the real pad");
    std::cout<<"{\"failures\":"<<failures<<",\"failed\":\""<<failed<<"\",\"real_after\":"<<r<<"}"<<std::endl;
    return failures?1:0;
}
