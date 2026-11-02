#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
unsigned failures=0;volatile LONG nested=0;
void check(bool result,const char* name) {if(!result){std::cerr<<"trace probe oracle: "<<name<<'\n';++failures;}}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM a,LPARAM b) {
    if(message==WM_APP+7) {MSG msg{};PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);InterlockedIncrement(&nested);return 19;}
    return DefWindowProcA(window,message,a,b);
}
struct Worker {HWND window;HANDLE done;DWORD tid=0;bool ok=false;};
DWORD WINAPI worker(void* argument) {
    auto& context=*static_cast<Worker*>(argument);context.tid=GetCurrentThreadId();
    MSG msg{};PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);
    bool posted=PostThreadMessageA(context.tid,WM_APP+3,17,31)!=0;
    bool got=posted && GetMessageA(&msg,nullptr,0,0)>0 && msg.message==WM_APP+3 && msg.wParam==17 && msg.lParam==31;
    context.ok=got && SendMessageA(context.window,WM_APP+7,0,0)==19;
    SetEvent(context.done);return 0;
}
uintptr_t import_address(const char* name,HMODULE module=nullptr) {
    auto base=reinterpret_cast<BYTE*>(module?module:GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;descriptor->Name;++descriptor) {
        if(!descriptor->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+descriptor->OriginalFirstThunk);
        auto addresses=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+descriptor->FirstThunk);
        for(;names->u1.AddressOfData;++names,++addresses) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto imported=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(!strcmp(reinterpret_cast<char*>(imported->Name),name))return addresses->u1.Function;
        }
    }
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    bool enabled=!wcscmp(argv[2],L"on");
    auto before_peek=import_address("PeekMessageA"),before_get=import_address("GetMessageA");
    using Peek=BOOL(WINAPI*)(LPMSG,HWND,UINT,UINT,UINT);
    using Get=BOOL(WINAPI*)(LPMSG,HWND,UINT,UINT);
    auto original_peek=reinterpret_cast<Peek>(before_peek);auto original_get=reinterpret_cast<Get>(before_get);
    check(before_peek&&before_get,"main imports both APIs");
    if(!LoadLibraryW(argv[1]))return 3;
    check((import_address("PeekMessageA")!=before_peek)==enabled,"Peek IAT opt-in");
    check((import_address("GetMessageA")!=before_get)==enabled,"Get IAT opt-in");
    // GetProcAddress must still return native APIs: observer scope is main IAT only.
    check(reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"PeekMessageA"))==before_peek,"dynamic Peek untouched");
    // The initial LoadLibrary call may be cached by the optimizing compiler.
    // Resolve the filtered loader afresh so this really exercises late-IAT patching.
    auto load=reinterpret_cast<decltype(&LoadLibraryW)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
    auto late=load(argv[3]);check(late!=nullptr,"late module loads");
    if(late) {
        check(import_address("PeekMessageA",late)==before_peek,"late Peek import stays native");
        check(import_address("GetMessageA",late)==before_get,"late Get import stays native");
        auto call=reinterpret_cast<int(*)()>(GetProcAddress(late,"probe_message_imports"));
        check(call&&call()==1,"late native message calls");
        FreeLibrary(late);
    }
    MSG msg{};PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);
    check(PostThreadMessageA(GetCurrentThreadId(),WM_APP+1,123,456)!=0,"post to own queue");
    check(PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE)>0 && msg.message==WM_APP+1 && msg.wParam==123 && msg.lParam==456,"Peek preserves message and flags");
    check(GetMessageA(&msg,nullptr,0,0)>0 && msg.message==WM_APP+1 && msg.wParam==123 && msg.lParam==456,"Get preserves message");
    PostQuitMessage(73);
    check(GetMessageA(&msg,nullptr,0,0)==0 && msg.message==WM_QUIT && msg.wParam==73,"Get WM_QUIT zero");
    auto invalid=reinterpret_cast<HWND>(uintptr_t(0x1234));
    SetLastError(777);auto native_error_result=original_get(&msg,invalid,0,0);auto native_error=GetLastError();
    SetLastError(777);auto hooked_error_result=GetMessageA(&msg,invalid,0,0);auto hooked_error=GetLastError();
    check(native_error_result==-1 && hooked_error_result==-1 && native_error==hooked_error,"Get negative result and LastError");
    SetLastError(777);auto native_empty=original_peek(&msg,nullptr,0,0,PM_NOREMOVE);auto native_last=GetLastError();
    SetLastError(777);auto hooked_empty=PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);auto hooked_last=GetLastError();
    check(native_empty==hooked_empty && native_last==hooked_last,"Peek result and LastError");
    auto instance=GetModuleHandleW(nullptr);WNDCLASSA cls{};cls.lpfnWndProc=window_proc;cls.hInstance=instance;
    cls.lpszClassName="CoopThreadTraceProbe";check(RegisterClassA(&cls)!=0,"register message-only window");
    auto window=CreateWindowExA(0,cls.lpszClassName,"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    check(window!=nullptr,"message-only window");
    Worker context{window,CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    HANDLE thread=window&&context.done?CreateThread(nullptr,0,worker,&context,0,nullptr):nullptr;
    check(thread!=nullptr,"worker started");
    auto start=GetTickCount64();
    while(thread && WaitForSingleObject(context.done,0)!=WAIT_OBJECT_0 && GetTickCount64()-start<5000) {
        PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);Sleep(1);
    }
    if(thread) {check(WaitForSingleObject(thread,100)==WAIT_OBJECT_0,"worker finished");CloseHandle(thread);}
    check(context.ok&&nested>0,"nested message dispatch observed");
    if(context.done)CloseHandle(context.done);
    if(window)DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName,instance);
    FILETIME created{},ended{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user);
    std::cout<<"{\"pid\":"<<GetCurrentProcessId()<<",\"start\":"<<((uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime)
        <<",\"main_tid\":"<<GetCurrentThreadId()<<",\"worker_tid\":"<<context.tid
        <<",\"nested\":"<<nested<<",\"failures\":"<<failures<<"}\n"<<std::flush;
    // The parent reads the observer from another process while this owned probe
    // waits. It never needs a remote thread or a game instance.
    char finish=0;std::cin.get(finish);
    return failures?1:0;
}
