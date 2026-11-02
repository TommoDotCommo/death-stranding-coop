#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
std::string json(const std::wstring& s) {
    std::string q="\"";
    for(auto c:s) {if(c==L'\\'||c==L'"') q+='\\'; if(c<32||c>126) return "null"; q+=(char)c;}
    return q+'"';
}
std::wstring env(const wchar_t* k) {wchar_t b[32768];auto n=GetEnvironmentVariableW(k,b,32768);return n&&n<32768?b:L"";}
bool same(std::wstring a,std::wstring b) {return !_wcsicmp(a.c_str(),b.c_str());}
DWORD marker(const std::wstring& p) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return GetLastError();
    const char data[]="coop redirect probe\n";DWORD n=0;
    bool ok=WriteFile(h,data,sizeof(data)-1,&n,nullptr)&&n==sizeof(data)-1;
    auto error=ok?0:GetLastError();CloseHandle(h);return error;
}
std::string json_list(const std::vector<std::wstring>& names) {
    std::string q="[";
    for(size_t i=0;i<names.size();++i) q+=(i?",":"")+json(names[i]);
    return q+"]";
}
// Names FindFirstFile returns for a pattern (without . and ..). `error` is 0 or the failing GetLastError.
std::vector<std::wstring> list_names(const std::wstring& pattern,DWORD& error) {
    std::vector<std::wstring> names;error=0;
    WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern.c_str(),&d);
    if(h==INVALID_HANDLE_VALUE) {error=GetLastError();return names;}
    do {if(wcscmp(d.cFileName,L".")&&wcscmp(d.cFileName,L"..")) names.push_back(d.cFileName);} while(FindNextFileW(h,&d));
    FindClose(h);return names;
}
// 0 when the file opens for reading (a leak when it exists only in the real tree), else the error.
DWORD open_error(const std::wstring& p) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h==INVALID_HANDLE_VALUE) return GetLastError();
    CloseHandle(h);return 0;
}
bool read_back(const std::wstring& p,const char* expected,DWORD size) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    char buffer[256]={};DWORD n=0;bool ok=ReadFile(h,buffer,sizeof(buffer)-1,&n,nullptr)&&n==size&&!memcmp(buffer,expected,size);
    CloseHandle(h);return ok;
}
#ifdef PROBE_LIBRARY
// A DLL build of this same probe source: LoadLibrary(EXE) does not resolve its
// imports, so loading the executable as a DLL would prove no IAT coverage.
extern "C" __declspec(dllexport) int probe_imports() {
    return same(env(L"LOCALAPPDATA"),env(L"COOP_REDIRECT_INSTANCE")+L"\\local")?1:0;
}
#else
unsigned inspect_iat(HMODULE module,bool& ok) {
    auto b=reinterpret_cast<BYTE*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(b+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    unsigned checked=0;ok=true;
    if(!dir.VirtualAddress) return 0;
    for(auto d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b+dir.VirtualAddress);d->Name;++d) {
        if(!d->OriginalFirstThunk) continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->OriginalFirstThunk);
        auto iat=reinterpret_cast<IMAGE_THUNK_DATA64*>(b+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++iat) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+names->u1.AddressOfData)->Name;
            for(auto key:{"CreateFileW","CreateFile2","DeleteFileW","CreateDirectoryW","GetEnvironmentVariableW","GetProcAddress","LoadLibraryExW","LoadLibraryW"}) {
                if(!strcmp(reinterpret_cast<const char*>(name),key)) {
                    ++checked;
                    auto expected=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),key);
                    if(iat->u1.Function!=reinterpret_cast<ULONG_PTR>(expected)) ok=false;
                }
            }
        }
    }
    return checked;
}
struct LateTest {std::wstring path;bool loaded=false,patched=false;unsigned checked=0;DWORD thread=0;};
DWORD WINAPI late_worker(void* arg) {
    auto& t=*static_cast<LateTest*>(arg);t.thread=GetCurrentThreadId();
    auto module=LoadLibraryW(t.path.c_str());t.loaded=module!=nullptr;
    if(module) {
        t.checked=inspect_iat(module,t.patched);
        auto check=reinterpret_cast<int(*)()>(GetProcAddress(module,"probe_imports"));
        if(check && check()!=1) t.patched=false;
        FreeLibrary(module);
    }
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 && argc!=4) return 2;
    // This is the explicit LoadLibrary probe route. Resolve the API dynamically
    // too, proving GetProcAddress returns the filtered entry point.
    if(!LoadLibraryW(argv[1])) return 3;
    if(argc==4) {
        if(wcscmp(argv[2],L"--late")) return 2;
        LateTest t;t.path=argv[3];
        if(GetModuleHandleW(t.path.c_str())) return 9; // must really be late
        HANDLE worker=CreateThread(nullptr,0,late_worker,&t,0,nullptr);
        if(!worker) return 10;
        if(WaitForSingleObject(worker,15000)!=WAIT_OBJECT_0) return 11;
        CloseHandle(worker);
        bool ok=t.loaded && t.patched && t.checked>0;
        std::cout<<"{\"module_loaded\":"<<(t.loaded?"true":"false")
            <<",\"iat_patched\":"<<(t.patched?"true":"false")<<",\"iat_checked\":"<<t.checked
            <<",\"worker_thread\":"<<t.thread<<",\"main_thread\":"<<GetCurrentThreadId()<<"}\n";
        return ok?0:12;
    }
    using SH=HRESULT(WINAPI*)(REFKNOWNFOLDERID,DWORD,HANDLE,PWSTR*);
    auto sh=(SH)GetProcAddress(GetModuleHandleW(L"shell32.dll"),"SHGetKnownFolderPath");
    PWSTR p=nullptr; HRESULT hr=sh(FOLDERID_LocalAppData,0,nullptr,&p);
    std::wstring known=SUCCEEDED(hr)?p:L"";CoTaskMemFree(p);
    auto local=env(L"LOCALAPPDATA"), root=env(L"COOP_REDIRECT_INSTANCE");
    // Refuse before ANY write if hooks are disabled. The mutant never writes
    // a marker into the owner's real folders.
    bool active=same(known,root+L"\\local")&&same(local,known);
    if(!active) {std::cout<<"{\"ok\":false,\"hooks_active\":false,\"known\":"<<json(known)<<",\"env\":"<<json(local)<<"}\n";return 4;}
    auto tag=std::to_wstring(GetCurrentProcessId());
    auto dir=local+L"\\KojimaProductions";
    CreateDirectoryW(dir.c_str(),nullptr);
    auto probe=dir+L"\\_coop_probe";CreateDirectoryW(probe.c_str(),nullptr);
    auto path=probe+L"\\marker-"+tag;
    DWORD first=marker(path);
    // Hardcoded real-root name via independently supplied real LocalAppData.
    auto real=env(L"COOP_REAL_LOCAL")+L"\\KojimaProductions\\DeathStrandingDC";
    CreateDirectoryW(real.c_str(),nullptr);
    real+=L"\\_coop_probe";CreateDirectoryW(real.c_str(),nullptr);
    auto realfile=real+L"\\marker-"+tag;
    DWORD second=marker(realfile);
    auto denied=env(L"COOP_REDIRECT_STEAM")+L"\\_coop_probe-"+tag;
    DWORD deny=marker(denied);
    auto traversal=root+L"\\local\\..\\escape-"+tag;
    DWORD trav=marker(traversal);
    // Reads, existence checks and enumeration of the real-looking tree. COOP_REAL_LOCAL names a decoy tree that
    // the runner filled with a file the instance never wrote: it must not be found through the proxy.
    const char content[]="coop redirect probe\n";
    auto ds_root=env(L"COOP_REAL_LOCAL")+L"\\KojimaProductions\\DeathStrandingDC";
    auto decoy_dir=ds_root+L"\\_coop_decoy", decoy_file=decoy_dir+L"\\decoy.dat";
    DWORD decoy_read=open_error(decoy_file);
    DWORD decoy_attr=GetFileAttributesW(decoy_file.c_str()), decoy_attr_error=decoy_attr==INVALID_FILE_ATTRIBUTES?GetLastError():0;
    DWORD decoy_dir_attr=GetFileAttributesW(decoy_dir.c_str()), decoy_dir_error=decoy_dir_attr==INVALID_FILE_ATTRIBUTES?GetLastError():0;
    bool readback=read_back(realfile,content,sizeof(content)-1);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    bool have_info=GetFileAttributesExW(realfile.c_str(),GetFileExInfoStandard,&info)!=0;
    DWORD readback_size=have_info?info.nFileSizeLow:0;
    DWORD root_error=0,probe_error=0;
    auto root_names=list_names(ds_root+L"\\*",root_error);
    auto probe_names=list_names(real+L"\\*",probe_error);
    auto absent=[](DWORD e){return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND;};
    bool queries=absent(decoy_read)&&decoy_attr==INVALID_FILE_ATTRIBUTES&&absent(decoy_attr_error)&&
        decoy_dir_attr==INVALID_FILE_ATTRIBUTES&&absent(decoy_dir_error)&&readback&&readback_size==sizeof(content)-1&&
        !root_error&&!probe_error;
    bool ok=!first&&!second&&deny==ERROR_ACCESS_DENIED&&trav==ERROR_ACCESS_DENIED&&queries;
    std::cout<<"{\"ok\":"<<(ok?"true":"false")
        <<",\"hooks_active\":true,\"known\":"<<json(known)<<",\"env\":"<<json(local)
        <<",\"marker\":"<<json(path)<<",\"real_attempt\":"<<json(realfile)
        <<",\"marker_error\":"<<first<<",\"real_error\":"<<second<<",\"denied_error\":"<<deny<<",\"traversal_error\":"<<trav
        <<",\"decoy_file\":"<<json(decoy_file)<<",\"decoy_read_error\":"<<decoy_read
        <<",\"decoy_attr_found\":"<<(decoy_attr!=INVALID_FILE_ATTRIBUTES?"true":"false")<<",\"decoy_attr_error\":"<<decoy_attr_error
        <<",\"decoy_dir_found\":"<<(decoy_dir_attr!=INVALID_FILE_ATTRIBUTES?"true":"false")<<",\"decoy_dir_error\":"<<decoy_dir_error
        <<",\"readback_ok\":"<<(readback?"true":"false")<<",\"readback_size\":"<<readback_size
        <<",\"root_names\":"<<json_list(root_names)<<",\"root_find_error\":"<<root_error
        <<",\"probe_names\":"<<json_list(probe_names)<<",\"probe_find_error\":"<<probe_error<<"}\n";
    return ok?0:5;
}

#endif
