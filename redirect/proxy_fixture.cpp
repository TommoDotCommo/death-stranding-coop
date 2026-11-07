// Windows fixture for the avrt.dll proxy: a stand-in for ds.exe that imports avrt.dll statically. Run from a folder
// that holds the proxy as avrt.dll; prints one JSON line. Never touches a game or a real save folder.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <avrt.h>
#include <psapi.h>
#include <cstdio>
#include <string>
#include <vector>
static std::string narrow(const std::wstring& s) {
    std::string q;
    for(auto c:s) {if(c==L'\\'||c==L'"') q+='\\'; q+=c>126||c<32?'?':(char)c;}
    return q;
}
static std::wstring env(const wchar_t* k) {wchar_t b[32768];auto n=GetEnvironmentVariableW(k,b,32768);return n&&n<32768?b:L"";}
static std::wstring module_path(HMODULE m) {wchar_t b[32768];auto n=GetModuleFileNameW(m,b,32768);return n&&n<32768?std::wstring(b):L"";}
using Set=HANDLE(WINAPI*)(LPCWSTR,LPDWORD);
using Revert=BOOL(WINAPI*)(HANDLE);
using OrderEx=BOOL(WINAPI*)(PHANDLE,PLARGE_INTEGER,GUID*,PLARGE_INTEGER,LPCWSTR);
using Delete=BOOL(WINAPI*)(HANDLE);
int main() {
    std::wstring system=env(L"SystemRoot")+L"\\System32\\avrt.dll";
    // 1. Which avrt.dll did the loader bind to ds.exe-style static imports? The proxy beside the exe.
    HMODULE bound=GetModuleHandleW(L"avrt.dll");
    std::wstring bound_path=module_path(bound);
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
    std::wstring folder(exe);folder.resize(folder.find_last_of(L'\\')+1);
    bool proxy_first=!bound_path.empty() && !_wcsicmp(bound_path.c_str(),(folder+L"avrt.dll").c_str());
    // 2. Reference: the real DLL by full path, called directly.
    HMODULE real=LoadLibraryExW(system.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto rset=real?reinterpret_cast<Set>(GetProcAddress(real,"AvSetMmThreadCharacteristicsW")):nullptr;
    auto rrev=real?reinterpret_cast<Revert>(GetProcAddress(real,"AvRevertMmThreadCharacteristics")):nullptr;
    auto rord=real?reinterpret_cast<OrderEx>(GetProcAddress(real,"AvRtCreateThreadOrderingGroupExW")):nullptr;
    bool real_distinct=real && real!=bound && !_wcsicmp(module_path(real).c_str(),system.c_str());
    // 3. The same calls through the proxy (static imports) must behave exactly like the real DLL.
    // One MMCSS registration per thread: call through the proxy and revert, then repeat on the real DLL.
    DWORD idx=0,ridx=0;
    SetLastError(0);HANDLE h=AvSetMmThreadCharacteristicsW(L"Audio",&idx);DWORD e1=GetLastError();
    BOOL back=h?AvRevertMmThreadCharacteristics(h):FALSE;
    SetLastError(0);HANDLE rh=rset?rset(L"Audio",&ridx):nullptr;DWORD re1=GetLastError();
    BOOL rback=rh&&rrev?rrev(rh):FALSE;
    // bad task name: a deterministic error code
    DWORD bad=0;SetLastError(0);HANDLE hb=AvSetMmThreadCharacteristicsW(L"No Such Task Name",&bad);DWORD e2=GetLastError();
    SetLastError(0);HANDLE rb=rset?rset(L"No Such Task Name",&bad):nullptr;DWORD re2=GetLastError();
    // five arguments: the fifth goes on the stack and must survive the stub
    HANDLE g=nullptr,rg=nullptr;LARGE_INTEGER period{},timeout{};period.QuadPart=-100000;timeout.QuadPart=-100000;
    SetLastError(0);BOOL ok5=AvRtCreateThreadOrderingGroupExW(&g,&period,nullptr,&timeout,L"No Such Task Name");DWORD e5=GetLastError();
    SetLastError(0);BOOL rok5=rord?rord(&rg,&period,nullptr,&timeout,L"No Such Task Name"):FALSE;DWORD re5=GetLastError();
    bool forwarded=real_distinct && rset && ((h!=nullptr)==(rh!=nullptr)) && e1==re1 && back==rback &&
                   ((hb!=nullptr)==(rb!=nullptr)) && e2==re2 && ok5==rok5 && e5==re5 && (rh==nullptr||(h!=nullptr && idx!=0));
    // 4. Redirect initialised: known-folder environment points into the instance, and a file created under the
    // "real" folder lands in the instance instead.
    std::wstring local=env(L"LOCALAPPDATA"),instance=env(L"COOP_REDIRECT_INSTANCE"),real_local=env(L"COOP_REAL_LOCAL");
    bool env_redirected=!instance.empty() && !_wcsicmp(local.c_str(),(instance+L"\\local").c_str());
    DWORD marker=1;
    if(!real_local.empty()) {
        HANDLE f=CreateFileW((real_local+L"\\proxy-fixture-marker.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        marker=f==INVALID_HANDLE_VALUE?GetLastError():0;
        if(f!=INVALID_HANDLE_VALUE) {DWORD n=0;WriteFile(f,"x",1,&n,nullptr);CloseHandle(f);}
    }
    std::printf("{\"proxy_first\":%s,\"bound\":\"%s\",\"real_distinct\":%s,\"forwarded\":%s,\"set_handle\":%s,\"real_set_handle\":%s,"
                "\"err\":[%lu,%lu,%lu,%lu,%lu,%lu],\"env_redirected\":%s,\"marker_error\":%lu}\n",
                proxy_first?"true":"false",narrow(bound_path).c_str(),real_distinct?"true":"false",forwarded?"true":"false",
                h?"true":"false",rh?"true":"false",e1,re1,e2,re2,e5,re5,env_redirected?"true":"false",marker);
    return 0;
}
