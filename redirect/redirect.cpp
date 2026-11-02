#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <mmsystem.h>
#include "../mod/config.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstring>
#include "redirect.h"
#include "module_policy.h"
#include "audit_throttle.h"
#include "hook_list.h"
#include "objects.h"
#include "network_policy.h"
#include <set>
#include <winternl.h>
#include <atomic>
#include <intrin.h>

namespace {
redirect::Policy policy;
HMODULE own;
redirect::ModulePolicy modules;
thread_local unsigned load_depth=0;
thread_local bool draining=false;
thread_local const wchar_t* load_origin=L"native/transitive (delay/static unknown)";
HANDLE log_file=INVALID_HANDLE_VALUE;
redirect::Throttle throttle;
SRWLOCK log_lock=SRWLOCK_INIT;
auto real_gpa=::GetProcAddress;
auto real_cf=::CreateFileW;
void flush_counts();
[[noreturn]] void fatal(const char* why) {
    flush_counts();
    OutputDebugStringA(why);
    DWORD n; HANDLE err=GetStdHandle(STD_ERROR_HANDLE);
    if(err && err!=INVALID_HANDLE_VALUE) WriteFile(err,why,(DWORD)strlen(why),&n,nullptr);
    if(log_file!=INVALID_HANDLE_VALUE) {
        wchar_t message[1024];size_t i=0;for(;why[i] && i<1023;++i) message[i]=(unsigned char)why[i];
        WriteFile(log_file,message,(DWORD)(i*sizeof(wchar_t)),&n,nullptr);FlushFileBuffers(log_file);
    }
    TerminateProcess(GetCurrentProcess(),125); __builtin_unreachable();
}
void flush_counts() {
    // Best effort and never blocking: a thread killed while holding the lock must not hang the exit.
    if(log_file==INVALID_HANDLE_VALUE || !TryAcquireSRWLockExclusive(&log_lock)) return;
    throttle.flush(GetTickCount64(),[&](const std::wstring& line) {
        DWORD n=0;WriteFile(log_file,line.data(),(DWORD)(line.size()*sizeof(wchar_t)),&n,nullptr);FlushFileBuffers(log_file);
    },true);
    ReleaseSRWLockExclusive(&log_lock);
}
std::wstring env(const wchar_t* key) {
    wchar_t b[32768]; DWORD n=::GetEnvironmentVariableW(key,b,32768);
    if(!n || n>=32768) fatal("redirect: missing/oversized configuration\n");
    auto p=redirect::canonical(b); if(p.empty()) fatal("redirect: invalid configuration path\n"); return p;
}
void note(const wchar_t* kind,const std::wstring& from,const std::wstring& to) {
    bool ok=true;
    auto sink=[&](const std::wstring& line) {
        DWORD n=0,bytes=(DWORD)(line.size()*sizeof(wchar_t));
        ok=ok && WriteFile(log_file,line.data(),bytes,&n,nullptr) && n==bytes && FlushFileBuffers(log_file);
    };
    AcquireSRWLockExclusive(&log_lock);
    // DENY and DEVICE repeat thousands of times a minute (the game polls input devices every
    // frame): written once with a running count. Every other record is written in full.
    if(redirect::Throttle::throttled(kind)) throttle.record(kind,from,to,GetTickCount64(),sink);
    else {sink(std::wstring(kind)+L"\t"+from+L"\t"+to+L"\r\n");throttle.flush(GetTickCount64(),sink);}
    ReleaseSRWLockExclusive(&log_lock);
    if(!ok) fatal("redirect: audit log failed\n");
}
bool safe_tree(const std::wstring& path,bool write) {
    auto p=redirect::canonical(path); if(p.empty()) return false;
    // Reject each existing ancestor, including the leaf. A trusted quiescent
    // instance tree is required: this does not close concurrent rename races.
    for(size_t end=3;;) {
        auto part=p.substr(0,end);
        DWORD a=GetFileAttributesW(part.c_str());
        if(a==INVALID_FILE_ATTRIBUTES) {
            DWORD e=GetLastError();
            if(e!=ERROR_FILE_NOT_FOUND && e!=ERROR_PATH_NOT_FOUND) return false;
        } else {
            if(a&FILE_ATTRIBUTE_REPARSE_POINT) return false;
            if(write && !(a&FILE_ATTRIBUTE_DIRECTORY)) {
                HANDLE h=real_cf(part.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                BY_HANDLE_FILE_INFORMATION info{};
                bool ok=h!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(h,&info) && info.nNumberOfLinks==1;
                if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);
                if(!ok) return false;
            }
        }
        if(end==p.size()) break;
        auto n=p.find(L'\\',end+1); end=n==std::wstring::npos?p.size():n;
    }
    return true;
}
bool filter(LPCWSTR raw,bool write,std::wstring& out,bool device_ok=false,redirect::Action* result=nullptr) {
    std::wstring input=raw?raw:L"";
    if(input.size()>0 && input.find(L':')==std::wstring::npos && input[0]!=L'\\') {
        wchar_t cwd[32768];DWORD n=GetCurrentDirectoryW(32768,cwd);
        if(!n || n>=32768) fatal("redirect: current directory unavailable\n");
        input=std::wstring(cwd)+L"\\"+input;
    }
    auto split=redirect::split_namespace(input);
    auto r=redirect::map(policy,input,write);
    if(r.action==redirect::Action::device) {
        // Input hardware (HID interface paths) is not a file: pass through untouched, but only to opens.
        if(!device_ok) {note(L"DENY",raw?raw:L"<null>",L"device path on a non-open API");SetLastError(ERROR_ACCESS_DENIED);return false;}
        note(L"DEVICE",input,L"");out=input;if(result) *result=r.action;return true;
    }
    // The drive path behind \\?\ is what is checked on disk; the prefix is put back for the real call.
    const std::wstring& plain=split.kind==redirect::PathKind::drive?split.path:input;
    if(r.action!=redirect::Action::deny &&
       (!safe_tree(plain,false) || !safe_tree(r.path,write))) r.action=redirect::Action::deny;
    if(r.action==redirect::Action::deny) {
        note(L"DENY",raw?raw:L"<null>",L""); SetLastError(ERROR_ACCESS_DENIED); return false;
    }
    out=split.kind==redirect::PathKind::drive?L"\\\\?\\"+r.path:r.path;
    if(result) *result=r.action;
    if(r.action==redirect::Action::redirect) note(L"REDIRECT",raw,out);
    return true;
}
// Reads, existence checks, enumeration and free-space queries. Under a redirected real root the instance copy
// answers (so a missing copy is "not found" and the real tree is never read); every other path is passed
// through untouched, exactly as before these APIs were hooked.
enum class Query {pass, mapped, denied};
Query query_path(LPCWSTR raw,bool wildcard,std::wstring& out) {
    if(!raw || !*raw) return Query::pass;
    std::wstring input=raw;
    if(input.find(L':')==std::wstring::npos && input[0]!=L'\\' && input[0]!=L'/') {
        wchar_t cwd[32768];DWORD n=GetCurrentDirectoryW(32768,cwd);
        if(!n || n>=32768) {note(L"DENY",raw,L"current directory unavailable");SetLastError(ERROR_ACCESS_DENIED);return Query::denied;}
        input=std::wstring(cwd)+L"\\"+input;
    }
    auto r=redirect::map_query(policy,input,wildcard);
    if(r.action==redirect::Action::deny && r.path==L"alias") {
        // A short (8.3) name can alias a real root: expand it (a read) and decide on the long form.
        wchar_t expanded[32768];DWORD n=GetLongPathNameW(input.c_str(),expanded,32768);
        if(n && n<32768 && !wcschr(expanded,L'~')) {input=expanded;r=redirect::map_query(policy,input,wildcard);}
        if(r.action==redirect::Action::deny && r.path==L"alias") r.path.clear();
    }
    if(r.action==redirect::Action::allow) return Query::pass;
    if(r.action==redirect::Action::redirect) {
        // The pattern's wildcard leaf is not a file name: check the directory it searches.
        std::wstring checked=r.path;
        if(wildcard && checked.find_first_of(L"*?<>\"")!=std::wstring::npos) checked.resize(checked.find_last_of(L'\\'));
        if(safe_tree(checked,false)) {
            auto split=redirect::split_namespace(input);
            out=split.kind==redirect::PathKind::drive?L"\\\\?\\"+r.path:r.path;
            note(L"REDIRECT_READ",raw,out);
            return Query::mapped;
        }
    }
    note(L"DENY",raw,L"query under a redirected root");SetLastError(ERROR_ACCESS_DENIED);return Query::denied;
}
std::string narrow(const std::wstring& w) {std::string n;for(auto c:w) n+=(char)c;return n;} // mapped paths are ASCII
// ANSI front end: unconvertible bytes cannot name an ASCII save root, so they pass through.
BOOL WINAPI cf_write(DWORD access,DWORD disposition,DWORD flags) {
    return (access&(GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|MAXIMUM_ALLOWED)) || disposition!=OPEN_EXISTING || (flags&FILE_FLAG_DELETE_ON_CLOSE);
}
HANDLE WINAPI hook_CreateFileW(LPCWSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD d,DWORD f,HANDLE t) {
    std::wstring q; redirect::Action act; bool write=cf_write(a,d,f);
    if(!filter(p,write,q,true,&act)) return INVALID_HANDLE_VALUE;
    if(act==redirect::Action::device) return real_cf(p,a,s,sa,d,f,t);
    if((f&FILE_FLAG_OPEN_REPARSE_POINT) && !redirect::reparse_open_allowed(write,act)) { note(L"DENY",p,L"reparse open"); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return real_cf(q.c_str(),a,s,sa,d,f,t);
}
HANDLE WINAPI hook_CreateFile2(LPCWSTR p,DWORD a,DWORD s,DWORD d,LPCREATEFILE2_EXTENDED_PARAMETERS e) {
    std::wstring q; redirect::Action act; DWORD flags=e?e->dwFileFlags:0; bool write=cf_write(a,d,flags);
    if(!filter(p,write,q,true,&act)) return INVALID_HANDLE_VALUE;
    if(act==redirect::Action::device) return ::CreateFile2(p,a,s,d,e);
    if((flags&FILE_FLAG_OPEN_REPARSE_POINT) && !redirect::reparse_open_allowed(write,act)) { note(L"DENY",p,L"reparse open"); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return ::CreateFile2(q.c_str(),a,s,d,e);
}
BOOL WINAPI hook_CreateDirectoryW(LPCWSTR p,LPSECURITY_ATTRIBUTES a) {std::wstring q; return filter(p,true,q)?::CreateDirectoryW(q.c_str(),a):FALSE;}
BOOL WINAPI hook_DeleteFileW(LPCWSTR p) {std::wstring q; return filter(p,true,q)?::DeleteFileW(q.c_str()):FALSE;}
BOOL WINAPI hook_RemoveDirectoryW(LPCWSTR p) {std::wstring q; return filter(p,true,q)?::RemoveDirectoryW(q.c_str()):FALSE;}
BOOL WINAPI hook_SetFileAttributesW(LPCWSTR p,DWORD a) {
    std::wstring q; if(!filter(p,true,q)) return FALSE;
    return ::SetFileAttributesW(q.c_str(),a);
}
DWORD WINAPI hook_GetFileAttributesW(LPCWSTR p) {
    std::wstring q;switch(query_path(p,false,q)) {case Query::denied:return INVALID_FILE_ATTRIBUTES;case Query::mapped:return ::GetFileAttributesW(q.c_str());default:return ::GetFileAttributesW(p);}
}
BOOL WINAPI hook_GetFileAttributesExW(LPCWSTR p,GET_FILEEX_INFO_LEVELS l,LPVOID d) {
    std::wstring q;switch(query_path(p,false,q)) {case Query::denied:return FALSE;case Query::mapped:return ::GetFileAttributesExW(q.c_str(),l,d);default:return ::GetFileAttributesExW(p,l,d);}
}
HANDLE WINAPI hook_FindFirstFileW(LPCWSTR p,LPWIN32_FIND_DATAW d) {
    std::wstring q;switch(query_path(p,true,q)) {case Query::denied:return INVALID_HANDLE_VALUE;case Query::mapped:return ::FindFirstFileW(q.c_str(),d);default:return ::FindFirstFileW(p,d);}
}
HANDLE WINAPI hook_FindFirstFileExW(LPCWSTR p,FINDEX_INFO_LEVELS l,LPVOID d,FINDEX_SEARCH_OPS o,LPVOID f,DWORD fl) {
    std::wstring q;switch(query_path(p,true,q)) {case Query::denied:return INVALID_HANDLE_VALUE;case Query::mapped:return ::FindFirstFileExW(q.c_str(),l,d,o,f,fl);default:return ::FindFirstFileExW(p,l,d,o,f,fl);}
}
BOOL WINAPI hook_GetDiskFreeSpaceExW(LPCWSTR p,PULARGE_INTEGER a,PULARGE_INTEGER b,PULARGE_INTEGER c) {
    std::wstring q;switch(query_path(p,false,q)) {case Query::denied:return FALSE;case Query::mapped:return ::GetDiskFreeSpaceExW(q.c_str(),a,b,c);default:return ::GetDiskFreeSpaceExW(p,a,b,c);}
}
BOOL WINAPI hook_MoveFileExW(LPCWSTR a,LPCWSTR b,DWORD f) {
    std::wstring x,y;
    if(!filter(a,true,x) || !filter(b,true,y)) return FALSE;
    // Delayed reboot operations escape this process's lifetime and policy.
    if(f&MOVEFILE_DELAY_UNTIL_REBOOT) {note(L"DENY",a,L"delayed rename");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return ::MoveFileExW(x.c_str(),y.c_str(),f);
}
HRESULT WINAPI hook_CopyFile2(PCWSTR a,PCWSTR b,COPYFILE2_EXTENDED_PARAMETERS* e) {
    std::wstring x,y; if(!filter(a,false,x)||!filter(b,true,y)) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    return ::CopyFile2(x.c_str(),y.c_str(),e);
}
std::wstring folder(REFKNOWNFOLDERID id) {
    const wchar_t* suffix=nullptr;
    if(IsEqualGUID(id,FOLDERID_LocalAppData)) suffix=L"local";
    if(IsEqualGUID(id,FOLDERID_RoamingAppData)) suffix=L"roaming";
    if(IsEqualGUID(id,FOLDERID_Documents)) suffix=L"documents";
    if(IsEqualGUID(id,FOLDERID_SavedGames)) suffix=L"savedgames";
    if(IsEqualGUID(id,FOLDERID_Profile)) suffix=L"profile";
    return suffix?policy.instance+L"\\"+suffix:L"";
}
HRESULT WINAPI hook_SHGetKnownFolderPath(REFKNOWNFOLDERID id,DWORD f,HANDLE token,PWSTR* out) {
    auto q=folder(id);
    if(q.empty()) return ::SHGetKnownFolderPath(id,f,token,out);
    if(!out || token) return E_ACCESSDENIED;
    if(!safe_tree(q,true)) return E_ACCESSDENIED;
    *out=(PWSTR)CoTaskMemAlloc((q.size()+1)*sizeof(wchar_t)); if(!*out) return E_OUTOFMEMORY;
    wcscpy(*out,q.c_str()); note(L"REDIRECT",L"SHGetKnownFolderPath",q); return S_OK;
}
HRESULT WINAPI hook_SHGetFolderPathW(HWND w,int id,HANDLE t,DWORD f,LPWSTR out) {
    const KNOWNFOLDERID* k=nullptr;
    switch(id&0xff) {case CSIDL_LOCAL_APPDATA:k=&FOLDERID_LocalAppData;break;case CSIDL_APPDATA:k=&FOLDERID_RoamingAppData;break;case CSIDL_PERSONAL:k=&FOLDERID_Documents;break;case CSIDL_PROFILE:k=&FOLDERID_Profile;break;}
    if(!k) return ::SHGetFolderPathW(w,id,t,f,out);
    auto q=folder(*k); if(!out || t || q.size()>=MAX_PATH || !safe_tree(q,true)) return E_ACCESSDENIED;
    wcscpy(out,q.c_str()); note(L"REDIRECT",L"SHGetFolderPathW",q); return S_OK;
}
DWORD WINAPI hook_GetEnvironmentVariableW(LPCWSTR key,LPWSTR out,DWORD n) {
    const wchar_t* suffix=nullptr;
    if(key && !_wcsicmp(key,L"LOCALAPPDATA")) suffix=L"local";
    if(key && !_wcsicmp(key,L"APPDATA")) suffix=L"roaming";
    if(key && !_wcsicmp(key,L"USERPROFILE")) suffix=L"profile";
    if(!suffix) return ::GetEnvironmentVariableW(key,out,n);
    auto q=policy.instance+L"\\"+suffix;
    if(n<=q.size()) return (DWORD)q.size()+1;
    if(!out) {SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    wcscpy(out,q.c_str()); note(L"REDIRECT",key,q); return (DWORD)q.size();
}
struct Entry {const char* name; FARPROC hook; FARPROC original; HMODULE skip=nullptr;};
std::vector<Entry> entries;
size_t patch(HMODULE module) {
    if(module==own) return 0;
    auto b=(BYTE*)module;
    auto dos=(IMAGE_DOS_HEADER*)b;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) fatal("redirect: invalid loaded PE\n");
    auto nt=(IMAGE_NT_HEADERS64*)(b+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return 0;
    size_t count=0;
    for(auto d=(IMAGE_IMPORT_DESCRIPTOR*)(b+dir.VirtualAddress); d->Name; ++d) {
        auto iat=(IMAGE_THUNK_DATA64*)(b+d->FirstThunk);
        for(;iat->u1.Function;++iat) {
            for(auto& e:entries) if(module!=e.skip && iat->u1.Function==(ULONG_PTR)e.original) {
                DWORD old,ignored;
                if(!VirtualProtect(&iat->u1.Function,sizeof(void*),PAGE_READWRITE,&old)) fatal("redirect: IAT protect failed\n");
                InterlockedExchangePointer((PVOID volatile*)&iat->u1.Function,(PVOID)e.hook);
                if(iat->u1.Function!=(ULONG_PTR)e.hook) fatal("redirect: IAT write failed\n");
                ++count;
                if(!VirtualProtect(&iat->u1.Function,sizeof(void*),old,&ignored)) fatal("redirect: IAT restore failed\n");
            }
        }
    }
    return count;
}
void patch_all(bool startup=false) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snap==INVALID_HANDLE_VALUE) fatal("redirect: module enumeration failed\n");
    MODULEENTRY32W m{}; m.dwSize=sizeof(m);
    if(!Module32FirstW(snap,&m)) fatal("redirect: no modules\n");
    size_t host_count=0;
    do {
        auto count=patch(m.hModule);
        if(startup) note(L"MODULE_STARTUP",m.szExePath,L"startup snapshot; tid="+std::to_wstring(GetCurrentThreadId()));
        if(count) note(L"MODULE_PATCH",m.szExePath,L"iat="+std::to_wstring(count)+L" tid="+std::to_wstring(GetCurrentThreadId()));
        if(m.hModule==GetModuleHandleW(nullptr)) host_count=count;
    } while(Module32NextW(snap,&m));
    if(GetLastError()!=ERROR_NO_MORE_FILES) fatal("redirect: incomplete module enumeration\n");
    CloseHandle(snap);
    if(startup && !host_count) fatal("redirect: no host imports patched\n");
}
FARPROC WINAPI hook_GetProcAddress(HMODULE m,LPCSTR name) {
    auto p=real_gpa(m,name);
    for(auto& e:entries) if(p==e.original) return e.hook;
    return p;
}
// Final-path equality plus ancestor checks rejects junctions, short names and
// namespace aliases. DLLs are read-only here, so linked game libraries are valid.
bool module_evidence(const std::wstring& path) {
    auto p=redirect::canonical(path);
    if(p.empty() || !safe_tree(p,false)) return false;
    HANDLE h=real_cf(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                     nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    wchar_t final_path[32768]; DWORD n=GetFinalPathNameByHandleW(h,final_path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    CloseHandle(h);
    if(!n || n>=32768) return false;
    std::wstring final(final_path);
    if(final.compare(0,4,L"\\\\?\\")==0) final.erase(0,4);
    return redirect::canonical(final)==p;
}
void module_audit(const std::wstring& p,const wchar_t* phase,bool allowed) {
    note(allowed?L"MODULE_ALLOW":L"MODULE_REFUSE",p,
         std::wstring(phase)+L" via="+load_origin+L" tid="+std::to_wstring(GetCurrentThreadId()));
}
// Under loader lock: copy only into a bounded, process-lifetime queue. No
// allocation, filesystem, logging, patching or calls into another DLL here.
// Loader notifications precede import resolution; drain only AFTER loader return.
struct DllNotice {ULONG flags;const UNICODE_STRING* full;const UNICODE_STRING* base;PVOID address;ULONG size;};
struct PendingModule {wchar_t path[32768];DWORD thread;std::atomic<bool> ready{false};};
static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,"notification requires lock-free atomics");
PendingModule pending[256];
std::atomic<unsigned> pending_count{0}, consumed{0};
std::atomic<bool> pending_overflow{false};
unsigned drained=0;
SRWLOCK drain_lock=SRWLOCK_INIT;
void CALLBACK dll_notice(ULONG reason,const void* raw,void*) {
    if(reason!=1) return;
    auto d=static_cast<const DllNotice*>(raw);
    unsigned index=pending_count.fetch_add(1,std::memory_order_relaxed);
    if(index-consumed.load(std::memory_order_acquire)>=256) {
        pending_overflow.store(true,std::memory_order_release);return;
    }
    auto& e=pending[index%256];
    size_t n=d->full?d->full->Length/sizeof(wchar_t):0;
    if(!d->full || !d->full->Buffer || n>=32768) n=0;
    for(size_t i=0;i<n;++i) e.path[i]=d->full->Buffer[i];
    e.path[n]=0;
    // x64 TEB.ClientId.UniqueThread; avoids calling GetCurrentThreadId under lock.
    e.thread=static_cast<DWORD>(__readgsqword(0x48));
    e.ready.store(true,std::memory_order_release);
}
void drain_modules() {
    if(draining) return;
    draining=true;
    AcquireSRWLockExclusive(&drain_lock);
    unsigned count=pending_count.load(std::memory_order_acquire);
    if(pending_overflow.load(std::memory_order_acquire)) fatal("redirect: module event queue overflow\n");
    while(drained<count && pending[drained%256].ready.load(std::memory_order_acquire)) {
        auto& e=pending[drained%256];std::wstring path=e.path;
        auto canonical_path=redirect::canonical(path);
        bool trusted=canonical_path==modules.instance+L"\\d3dcompiler_47_original.dll" || canonical_path==policy.instance+L"\\mod\\mod.dll";
        auto audit=[&](bool allowed){note(allowed?L"MODULE_ALLOW":L"MODULE_REFUSE",path,
            std::wstring(L"notification; initializer already ran; correlate request by tid; native/transitive/delay unknown; tid=")+std::to_wstring(e.thread));};
        bool ok=redirect::module_event(modules,path,module_evidence(path),trusted,redirect::ModulePhase::loaded,audit,[&]{
            HMODULE m=nullptr;
            if(GetModuleHandleExW(0,path.c_str(),&m)) {
                note(L"MODULE_PATCH",path,L"iat="+std::to_wstring(patch(m))+L" tid="+std::to_wstring(e.thread));
                FreeLibrary(m);
            }
            else note(L"MODULE_UNLOADED",path,L"no live IAT");
        });
        if(!ok) fatal("redirect: dependency/native module refused after initialization; see MODULE_REFUSE\n");
        e.ready.store(false,std::memory_order_release);
        consumed.store(++drained,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&drain_lock);
    draining=false;
}
void register_notifications() {
    using Register=LONG (NTAPI*)(ULONG,void (CALLBACK*)(ULONG,const void*,void*),void*,void**);
    auto f=reinterpret_cast<Register>(real_gpa(GetModuleHandleW(L"ntdll.dll"),"LdrRegisterDllNotification"));
    static void* cookie=nullptr;
    if(!f || f(0,dll_notice,nullptr,&cookie)<0) fatal("redirect: loader notification registration failed\n");
}
std::wstring wide(LPCSTR p) {
    if(!p) return L"";
    int n=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,nullptr,0);
    if(!n) fatal("redirect: invalid ANSI module path\n");
    std::wstring q(n,L'\0'); MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,&q[0],n);q.resize(n-1);return q;
}
HMODULE checked_module(LPCWSTR name,HANDLE file,DWORD flags,const wchar_t* origin) {
    const wchar_t* previous=load_origin;load_origin=origin;
    std::wstring requested=name?name:L"<null>", path=requested;
    HMODULE existing=name?GetModuleHandleW(name):nullptr;
    wchar_t resolved[32768];
    if(existing) {
        DWORD n=GetModuleFileNameW(existing,resolved,32768);
        if(n && n<32768) path=resolved;
    } else if(name && requested.find_first_of(L"\\/:")==std::wstring::npos) {
        // Fix the chosen file before execution; do not validate one search result
        // and then let LoadLibrary choose a different file from another directory.
        if(flags & LOAD_LIBRARY_SEARCH_SYSTEM32) {
            path=modules.windows+L"\\system32\\"+requested;
            if(requested.find(L'.')==std::wstring::npos) path+=L".dll";
        } else {
            DWORD n=SearchPathW(nullptr,name,L".dll",32768,resolved,nullptr);
            if(n && n<32768) path=resolved;
        }
    }
    bool request_valid=!redirect::canonical(requested).empty() || (requested.find_first_of(L"\\/:")==std::wstring::npos && !redirect::canonical(modules.instance+L"\\"+requested).empty());
    if(redirect::canonical(path).empty() && name) {
        DWORD n=GetFullPathNameW(name,32768,resolved,nullptr);
        if(n && n<32768) path=resolved;
    }
    note(L"MODULE_REQUEST",path,std::wstring(L"requested=")+requested+L" via="+origin+L" tid="+std::to_wstring(GetCurrentThreadId()));
    DWORD attributes=GetFileAttributesW(path.c_str());
    bool file_exists=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    auto action=redirect::classify_request(modules,requested,existing!=nullptr,file_exists);
    if(action==redirect::RequestAction::not_found) {
        // No file can be loaded (the loader would answer 126, as for hkCompatFormats.dll in the live run), so no
        // code can run: answer the same without letting the loader search other directories.
        note(L"MODULE_LOAD_FAILED",path,L"error=126 unresolved, no such file; not passed to the loader via="+std::wstring(origin)+L" tid="+std::to_wstring(GetCurrentThreadId()));
        load_origin=previous;SetLastError(ERROR_MOD_NOT_FOUND);return nullptr;
    }
    bool api_set=action==redirect::RequestAction::api_set_forward;
    bool evidence=!api_set && request_valid && module_evidence(path);
    if(api_set) module_audit(requested,L"request: API-set virtual name; the host module is checked after the load",true);
    else {
        bool ok=redirect::module_event(modules,path,evidence,existing!=nullptr,redirect::ModulePhase::request,
            [&](bool allowed){module_audit(path,L"request",allowed);},[]{});
        if(!ok) fatal("redirect: dynamic module refused; see MODULE_REFUSE record\n");
    }
    // Data/image-resource mappings cannot be patched as executable PE modules.
    if(flags & (LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE|DONT_RESOLVE_DLL_REFERENCES)) {
        note(L"MODULE_UNPATCHED",path,L"non-executable loader flags=0x"+redirect::hex_flags(flags)+L"; tid="+std::to_wstring(GetCurrentThreadId()));
        if(!redirect::loader_flags_allowed(modules,flags)) {module_audit(path,L"unsupported loader flags",false);fatal("redirect: unsupported loader flags\n");}
    }
    ++load_depth;
    HMODULE m=::LoadLibraryExW(evidence && !api_set && modules.mode!=redirect::ModuleMode::log_only?path.c_str():name,file,flags);
    --load_depth;
    DWORD error=GetLastError();
    if(!load_depth) drain_modules();
    if(m && !(flags & (LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE|DONT_RESOLVE_DLL_REFERENCES))) {
        DWORD n=GetModuleFileNameW(m,resolved,32768);
        path=n && n<32768?resolved:L"<unknown>";
        bool loaded=redirect::module_event(modules,path,module_evidence(path),existing!=nullptr,redirect::ModulePhase::loaded,
            [&](bool allowed){module_audit(path,L"loaded-after-init",allowed);},[]{if(!load_depth) patch_all();});
        if(!loaded) fatal("redirect: loaded module refused; see MODULE_REFUSE record\n");
    } else if(!m) note(L"MODULE_LOAD_FAILED",path,L"error="+std::to_wstring(error)+L" via="+origin+L" tid="+std::to_wstring(GetCurrentThreadId()));
    load_origin=previous;SetLastError(error);return m;
}
HMODULE WINAPI hook_LoadLibraryW(LPCWSTR p) {return checked_module(p,nullptr,0,L"LoadLibraryW (delay-load possible)");}
HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR p,HANDLE h,DWORD f) {return checked_module(p,h,f,L"LoadLibraryExW (delay-load possible)");}
HMODULE WINAPI hook_LoadLibraryA(LPCSTR p) {auto q=wide(p);return checked_module(q.c_str(),nullptr,0,L"LoadLibraryA (delay-load possible)");}
HMODULE WINAPI hook_LoadLibraryExA(LPCSTR p,HANDLE h,DWORD f) {auto q=wide(p);return checked_module(q.c_str(),h,f,L"LoadLibraryExA (delay-load possible)");}
// Mutating APIs that are not implemented must not become an escape hatch through imports or
// GetProcAddress. The ANSI file APIs are widened and routed through the path map like their W
// twins; the rest fail with a record that names the API and its paths (live run 2026-11-01: a
// nameless record, 26,148 times). x64 has one calling convention; unused arguments are ignored.
bool ansi(LPCSTR p,std::wstring& out) {
    out.clear();if(!p) return true;
    int n=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,nullptr,0);
    if(!n) return false;
    out.resize(n);MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,&out[0],n);out.resize(n-1);return true;
}
Query query_path_a(LPCSTR raw,bool wildcard,std::string& out) {
    std::wstring w,q;
    if(!raw || !ansi(raw,w)) return Query::pass;
    auto r=query_path(w.c_str(),wildcard,q);
    if(r==Query::mapped) out=narrow(q);
    return r;
}
void refuse_api(const wchar_t* api,const std::wstring& a,const std::wstring& b=L"") {
    note(L"DENY",std::wstring(L"unsupported mutating API ")+api+L": "+a,b);SetLastError(ERROR_ACCESS_DENIED);
}
HANDLE WINAPI hook_CreateFileA(LPCSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD d,DWORD f,HANDLE t) {
    std::wstring w;if(!ansi(p,w)) {refuse_api(L"CreateFileA",L"<unconvertible path>");return INVALID_HANDLE_VALUE;}
    return hook_CreateFileW(p?w.c_str():nullptr,a,s,sa,d,f,t);
}
BOOL WINAPI hook_CreateDirectoryA(LPCSTR p,LPSECURITY_ATTRIBUTES a) {std::wstring w;if(!ansi(p,w)) {refuse_api(L"CreateDirectoryA",L"<unconvertible path>");return FALSE;}return hook_CreateDirectoryW(w.c_str(),a);}
BOOL WINAPI hook_DeleteFileA(LPCSTR p) {std::wstring w;if(!ansi(p,w)) {refuse_api(L"DeleteFileA",L"<unconvertible path>");return FALSE;}return hook_DeleteFileW(w.c_str());}
BOOL WINAPI hook_RemoveDirectoryA(LPCSTR p) {std::wstring w;if(!ansi(p,w)) {refuse_api(L"RemoveDirectoryA",L"<unconvertible path>");return FALSE;}return hook_RemoveDirectoryW(w.c_str());}
BOOL WINAPI hook_SetFileAttributesA(LPCSTR p,DWORD a) {std::wstring w;if(!ansi(p,w)) {refuse_api(L"SetFileAttributesA",L"<unconvertible path>");return FALSE;}return hook_SetFileAttributesW(w.c_str(),a);}
BOOL WINAPI hook_MoveFileExA(LPCSTR a,LPCSTR b,DWORD f) {
    std::wstring x,y;if(!ansi(a,x)||!ansi(b,y)) {refuse_api(L"MoveFileExA",L"<unconvertible path>");return FALSE;}
    return hook_MoveFileExW(x.c_str(),y.c_str(),f);
}
BOOL WINAPI hook_MoveFileW(LPCWSTR a,LPCWSTR b) {return hook_MoveFileExW(a,b,MOVEFILE_COPY_ALLOWED);}
BOOL WINAPI hook_MoveFileA(LPCSTR a,LPCSTR b) {return hook_MoveFileExA(a,b,MOVEFILE_COPY_ALLOWED);}
BOOL WINAPI hook_CopyFileW(LPCWSTR a,LPCWSTR b,BOOL fail_if_exists) {
    std::wstring x,y;if(!filter(a,false,x)||!filter(b,true,y)) return FALSE;
    return ::CopyFileW(x.c_str(),y.c_str(),fail_if_exists);
}
BOOL WINAPI hook_CopyFileA(LPCSTR a,LPCSTR b,BOOL fail_if_exists) {
    std::wstring x,y;if(!ansi(a,x)||!ansi(b,y)) {refuse_api(L"CopyFileA",L"<unconvertible path>");return FALSE;}
    return hook_CopyFileW(x.c_str(),y.c_str(),fail_if_exists);
}
BOOL WINAPI hook_CreateDirectoryExW(LPCWSTR tmpl,LPCWSTR p,LPSECURITY_ATTRIBUTES a) {
    std::wstring x,y;if(tmpl && !filter(tmpl,false,x)) return FALSE;
    if(!filter(p,true,y)) return FALSE;
    return ::CreateDirectoryExW(tmpl?x.c_str():nullptr,y.c_str(),a);
}
BOOL WINAPI hook_CreateDirectoryExA(LPCSTR t,LPCSTR p,LPSECURITY_ATTRIBUTES a) {
    std::wstring x,y;if(!ansi(t,x)||!ansi(p,y)) {refuse_api(L"CreateDirectoryExA",L"<unconvertible path>");return FALSE;}
    return hook_CreateDirectoryExW(t?x.c_str():nullptr,y.c_str(),a);
}
// All three paths (replaced, replacement, optional backup) go through the write map.
BOOL WINAPI hook_ReplaceFileW(LPCWSTR replaced,LPCWSTR replacement,LPCWSTR backup,DWORD flags,LPVOID,LPVOID) {
    std::wstring x,y,z;
    if(!filter(replaced,true,x) || !filter(replacement,true,y) || (backup && !filter(backup,true,z))) return FALSE;
    return ::ReplaceFileW(x.c_str(),y.c_str(),backup?z.c_str():nullptr,flags,nullptr,nullptr);
}
BOOL WINAPI hook_ReplaceFileA(LPCSTR a,LPCSTR b,LPCSTR c,DWORD flags,LPVOID e,LPVOID f) {
    std::wstring x,y,z;if(!ansi(a,x)||!ansi(b,y)||!ansi(c,z)) {refuse_api(L"ReplaceFileA",L"<unconvertible path>");return FALSE;}
    return hook_ReplaceFileW(x.c_str(),y.c_str(),c?z.c_str():nullptr,flags,e,f);
}
// ANSI queries: the real ANSI API is called with the (ASCII) mapped path, or with the caller's own bytes.
DWORD WINAPI hook_GetFileAttributesA(LPCSTR p) {
    std::string q;switch(query_path_a(p,false,q)) {case Query::denied:return INVALID_FILE_ATTRIBUTES;case Query::mapped:return ::GetFileAttributesA(q.c_str());default:return ::GetFileAttributesA(p);}
}
BOOL WINAPI hook_GetFileAttributesExA(LPCSTR p,GET_FILEEX_INFO_LEVELS l,LPVOID d) {
    std::string q;switch(query_path_a(p,false,q)) {case Query::denied:return FALSE;case Query::mapped:return ::GetFileAttributesExA(q.c_str(),l,d);default:return ::GetFileAttributesExA(p,l,d);}
}
HANDLE WINAPI hook_FindFirstFileA(LPCSTR p,LPWIN32_FIND_DATAA d) {
    std::string q;switch(query_path_a(p,true,q)) {case Query::denied:return INVALID_HANDLE_VALUE;case Query::mapped:return ::FindFirstFileA(q.c_str(),d);default:return ::FindFirstFileA(p,d);}
}
HANDLE WINAPI hook_FindFirstFileExA(LPCSTR p,FINDEX_INFO_LEVELS l,LPVOID d,FINDEX_SEARCH_OPS o,LPVOID f,DWORD fl) {
    std::string q;switch(query_path_a(p,true,q)) {case Query::denied:return INVALID_HANDLE_VALUE;case Query::mapped:return ::FindFirstFileExA(q.c_str(),l,d,o,f,fl);default:return ::FindFirstFileExA(p,l,d,o,f,fl);}
}
BOOL WINAPI hook_GetDiskFreeSpaceExA(LPCSTR p,PULARGE_INTEGER a,PULARGE_INTEGER b,PULARGE_INTEGER c) {
    std::string q;switch(query_path_a(p,false,q)) {case Query::denied:return FALSE;case Query::mapped:return ::GetDiskFreeSpaceExA(q.c_str(),a,b,c);default:return ::GetDiskFreeSpaceExA(p,a,b,c);}
}
// Not implemented because the second operand, a callback, a link target or an information class
// can reach a path the map has not seen. They stay denied, by name.
#define REFUSE_W(name) BOOL WINAPI hook_##name(LPCWSTR a,LPCWSTR b,void*,void*) {refuse_api(L ## #name,a?a:L"<null>",b?b:L"<null>");return FALSE;}
#define REFUSE_A(name) BOOL WINAPI hook_##name(LPCSTR a,LPCSTR b,void*,void*) {std::wstring x,y;ansi(a,x);ansi(b,y);refuse_api(L ## #name,x,y);return FALSE;}
REFUSE_W(MoveFileWithProgressW)
REFUSE_W(CopyFileExW) REFUSE_A(CopyFileExA)
REFUSE_W(CreateHardLinkW) REFUSE_A(CreateHardLinkA) REFUSE_W(CreateSymbolicLinkW) REFUSE_A(CreateSymbolicLinkA)
#undef REFUSE_W
#undef REFUSE_A
// Named kernel objects (objects.h): every distinct name is logged once as OBJECT; names listed for this slot get a
// per-slot suffix so a second instance does not find the first one's single-instance guard. The log is written
// before the real call, so the API's own last-error (ERROR_ALREADY_EXISTS) is what the caller sees.
redirect::ObjectPolicy objects;
std::set<std::wstring> seen_objects;
SRWLOCK seen_lock=SRWLOCK_INIT;
std::wstring object_for(const wchar_t* api,LPCWSTR name) {
    std::wstring n(name),used=redirect::object_name(objects,n);
    AcquireSRWLockExclusive(&seen_lock);
    bool fresh=seen_objects.insert(std::wstring(api)+L"\t"+n).second;
    ReleaseSRWLockExclusive(&seen_lock);
    if(fresh) note(L"OBJECT",std::wstring(api)+L" "+n,used==n?L"unchanged":used);
    return used;
}
bool narrow(const std::wstring& w,std::string& out) {
    int n=WideCharToMultiByte(CP_ACP,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(!n) return false;
    out.resize(n);WideCharToMultiByte(CP_ACP,0,w.c_str(),-1,&out[0],n,nullptr,nullptr);out.resize(n-1);return true;
}
// For the A variants: an unconvertible name is passed through unchanged (and logged as such).
template<class Call> auto object_a(const wchar_t* api,LPCSTR name,Call call) -> decltype(call(name)) {
    if(!name) return call(name);
    std::wstring w;
    if(!ansi(name,w)) {note(L"OBJECT",std::wstring(api)+L" <unconvertible name>",L"unchanged");return call(name);}
    auto used=object_for(api,w.c_str());
    if(used==w) return call(name);
    std::string out; if(!narrow(used,out)) return call(name);
    return call(out.c_str());
}
#define OBJECT_W(api,...) (name?::api(__VA_ARGS__):::api(__VA_ARGS__))
HANDLE WINAPI hook_CreateMutexW(LPSECURITY_ATTRIBUTES a,BOOL o,LPCWSTR name) {if(!name) return ::CreateMutexW(a,o,name);auto u=object_for(L"CreateMutexW",name);return ::CreateMutexW(a,o,u.c_str());}
HANDLE WINAPI hook_CreateMutexA(LPSECURITY_ATTRIBUTES a,BOOL o,LPCSTR name) {return object_a(L"CreateMutexA",name,[&](LPCSTR n){return ::CreateMutexA(a,o,n);});}
HANDLE WINAPI hook_CreateMutexExW(LPSECURITY_ATTRIBUTES a,LPCWSTR name,DWORD f,DWORD acc) {if(!name) return ::CreateMutexExW(a,name,f,acc);auto u=object_for(L"CreateMutexExW",name);return ::CreateMutexExW(a,u.c_str(),f,acc);}
HANDLE WINAPI hook_CreateMutexExA(LPSECURITY_ATTRIBUTES a,LPCSTR name,DWORD f,DWORD acc) {return object_a(L"CreateMutexExA",name,[&](LPCSTR n){return ::CreateMutexExA(a,n,f,acc);});}
HANDLE WINAPI hook_OpenMutexW(DWORD acc,BOOL inh,LPCWSTR name) {if(!name) return ::OpenMutexW(acc,inh,name);auto u=object_for(L"OpenMutexW",name);return ::OpenMutexW(acc,inh,u.c_str());}
HANDLE WINAPI hook_OpenMutexA(DWORD acc,BOOL inh,LPCSTR name) {return object_a(L"OpenMutexA",name,[&](LPCSTR n){return ::OpenMutexA(acc,inh,n);});}
HANDLE WINAPI hook_CreateEventW(LPSECURITY_ATTRIBUTES a,BOOL m,BOOL i,LPCWSTR name) {if(!name) return ::CreateEventW(a,m,i,name);auto u=object_for(L"CreateEventW",name);return ::CreateEventW(a,m,i,u.c_str());}
HANDLE WINAPI hook_CreateEventA(LPSECURITY_ATTRIBUTES a,BOOL m,BOOL i,LPCSTR name) {return object_a(L"CreateEventA",name,[&](LPCSTR n){return ::CreateEventA(a,m,i,n);});}
HANDLE WINAPI hook_CreateEventExW(LPSECURITY_ATTRIBUTES a,LPCWSTR name,DWORD f,DWORD acc) {if(!name) return ::CreateEventExW(a,name,f,acc);auto u=object_for(L"CreateEventExW",name);return ::CreateEventExW(a,u.c_str(),f,acc);}
HANDLE WINAPI hook_CreateEventExA(LPSECURITY_ATTRIBUTES a,LPCSTR name,DWORD f,DWORD acc) {return object_a(L"CreateEventExA",name,[&](LPCSTR n){return ::CreateEventExA(a,n,f,acc);});}
HANDLE WINAPI hook_OpenEventW(DWORD acc,BOOL inh,LPCWSTR name) {if(!name) return ::OpenEventW(acc,inh,name);auto u=object_for(L"OpenEventW",name);return ::OpenEventW(acc,inh,u.c_str());}
HANDLE WINAPI hook_OpenEventA(DWORD acc,BOOL inh,LPCSTR name) {return object_a(L"OpenEventA",name,[&](LPCSTR n){return ::OpenEventA(acc,inh,n);});}
HANDLE WINAPI hook_CreateSemaphoreW(LPSECURITY_ATTRIBUTES a,LONG i,LONG m,LPCWSTR name) {if(!name) return ::CreateSemaphoreW(a,i,m,name);auto u=object_for(L"CreateSemaphoreW",name);return ::CreateSemaphoreW(a,i,m,u.c_str());}
HANDLE WINAPI hook_CreateSemaphoreA(LPSECURITY_ATTRIBUTES a,LONG i,LONG m,LPCSTR name) {return object_a(L"CreateSemaphoreA",name,[&](LPCSTR n){return ::CreateSemaphoreA(a,i,m,n);});}
HANDLE WINAPI hook_CreateFileMappingW(HANDLE f,LPSECURITY_ATTRIBUTES a,DWORD pr,DWORD hi,DWORD lo,LPCWSTR name) {if(!name) return ::CreateFileMappingW(f,a,pr,hi,lo,name);auto u=object_for(L"CreateFileMappingW",name);return ::CreateFileMappingW(f,a,pr,hi,lo,u.c_str());}
HANDLE WINAPI hook_CreateFileMappingA(HANDLE f,LPSECURITY_ATTRIBUTES a,DWORD pr,DWORD hi,DWORD lo,LPCSTR name) {return object_a(L"CreateFileMappingA",name,[&](LPCSTR n){return ::CreateFileMappingA(f,a,pr,hi,lo,n);});}
HANDLE WINAPI hook_OpenFileMappingW(DWORD acc,BOOL inh,LPCWSTR name) {if(!name) return ::OpenFileMappingW(acc,inh,name);auto u=object_for(L"OpenFileMappingW",name);return ::OpenFileMappingW(acc,inh,u.c_str());}
HANDLE WINAPI hook_OpenFileMappingA(DWORD acc,BOOL inh,LPCSTR name) {return object_a(L"OpenFileMappingA",name,[&](LPCSTR n){return ::OpenFileMappingA(acc,inh,n);});}
#undef OBJECT_W
BOOL WINAPI hook_SetFileInformationByHandle(HANDLE h,int info_class,void*,DWORD) {
    wchar_t path[1024]=L"<unknown handle>";
    DWORD n=GetFinalPathNameByHandleW(h,path,1024,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!n || n>=1024) wcscpy(path,L"<unknown handle>");
    refuse_api(L"SetFileInformationByHandle",path,L"class="+std::to_wstring(info_class));return FALSE;
}
// Outbound network (network_policy.h): loopback and listed co-op peers only, so a modded instance never reaches the
// publisher's online service. Every verdict is audited (deduplicated: first occurrence in full, then counts) with the
// calling module; a refused call fails the way an offline PC fails (access denied, host not found, cannot connect).
redirect::NetPolicy net;
static_assert(AF_INET==redirect::kAfInet && AF_INET6==redirect::kAfInet6,"address family constants");
std::wstring caller_module(void* address) {
    HMODULE m=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)address,&m) || !m) return L"<unknown>";
    wchar_t path[MAX_PATH];DWORD n=GetModuleFileNameW(m,path,MAX_PATH);
    if(!n || n>=MAX_PATH) return L"<unknown>";
    std::wstring p(path,n);auto at=p.find_last_of(L'\\');return at==std::wstring::npos?p:p.substr(at+1);
}
redirect::Endpoint endpoint(const sockaddr* a,int len,std::wstring& text) {
    redirect::Endpoint e;text=L"<no address>";
    if(!a || len<(int)sizeof(sockaddr)) return e; // family 0: denied
    wchar_t b[128]={};
    if(a->sa_family==AF_INET && len>=(int)sizeof(sockaddr_in)) {
        auto s=(const sockaddr_in*)a;e.family=AF_INET;memcpy(e.addr,&s->sin_addr,4);
        swprintf(b,128,L"%u.%u.%u.%u:%u",e.addr[0],e.addr[1],e.addr[2],e.addr[3],ntohs(s->sin_port));
    } else if(a->sa_family==AF_INET6 && len>=(int)sizeof(sockaddr_in6)) {
        auto s=(const sockaddr_in6*)a;e.family=AF_INET6;memcpy(e.addr,&s->sin6_addr,16);
        std::wstring hex=L"[";for(int i=0;i<16;i+=2) {wchar_t g[8];swprintf(g,8,L"%x",e.addr[i]<<8|e.addr[i+1]);hex+=g;if(i<14) hex+=L":";}
        swprintf(b,128,L"%ls]:%u",hex.c_str(),ntohs(s->sin6_port));
    } else swprintf(b,128,L"family=%d len=%d",(int)a->sa_family,len);
    text=b;return e;
}
// True when the call may proceed.
bool net_audit(const wchar_t* api,redirect::NetVerdict v,const std::wstring& destination,void* caller) {
    bool refuse=redirect::net_refuses(net,v);
    const wchar_t* kind=v!=redirect::NetVerdict::deny?L"NET_ALLOW":refuse?L"NET_DENY":L"NET_LOG";
    note(kind,std::wstring(api)+L" "+destination,std::wstring(redirect::verdict_name(v))+L" caller="+caller_module(caller));
    return !refuse;
}
bool net_address(const wchar_t* api,const sockaddr* a,int len,void* caller) {
    std::wstring text;auto e=endpoint(a,len,text);
    return net_audit(api,redirect::classify_endpoint(net,e),text,caller);
}
std::wstring ascii_host(const char* h) {std::wstring w;if(h) for(;*h;++h) w+=(unsigned char)*h<128?wchar_t(*h):L'?';return w;}
bool net_host(const wchar_t* api,const std::wstring& host,void* caller) {
    return net_audit(api,redirect::classify_host(net,host),host.empty()?L"<passive>":host,caller);
}
int WSAAPI hook_connect(SOCKET s,const sockaddr* a,int len) {
    if(!net_address(L"connect",a,len,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return ::connect(s,a,len);
}
int WSAAPI hook_WSAConnect(SOCKET s,const sockaddr* a,int len,LPWSABUF out,LPWSABUF in,LPQOS q,LPQOS g) {
    if(!net_address(L"WSAConnect",a,len,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return ::WSAConnect(s,a,len,out,in,q,g);
}
// A null destination is a connected socket, already checked at connect.
int WSAAPI hook_sendto(SOCKET s,const char* b,int n,int f,const sockaddr* to,int len) {
    if(to && len && !net_address(L"sendto",to,len,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return ::sendto(s,b,n,f,to,len);
}
int WSAAPI hook_WSASendTo(SOCKET s,LPWSABUF b,DWORD c,LPDWORD sent,DWORD f,const sockaddr* to,int len,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r) {
    if(to && len && !net_address(L"WSASendTo",to,len,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return ::WSASendTo(s,b,c,sent,f,to,len,o,r);
}
using SendMsgFn=int(WSAAPI*)(SOCKET,LPWSAMSG,DWORD,LPDWORD,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
SendMsgFn real_sendmsg_export=nullptr;
int WSAAPI hook_WSASendMsg(SOCKET s,LPWSAMSG m,DWORD f,LPDWORD sent,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r) {
    if(m && m->name && m->namelen && !net_address(L"WSASendMsg",m->name,m->namelen,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return real_sendmsg_export(s,m,f,sent,o,r);
}
// Extension functions come from WSAIoctl, not an import table: the pointers handed out are wrapped. One real target
// per function is supported (TCP/IP over IPv4 and IPv6 share mswsock's); a different second target is refused under
// enforce because one wrapper cannot tell which to call.
std::atomic<LPFN_CONNECTEX> real_connectex{nullptr};
std::atomic<LPFN_WSASENDMSG> real_sendmsg{nullptr};
BOOL PASCAL hook_ConnectEx(SOCKET s,const sockaddr* a,int len,PVOID b,DWORD n,LPDWORD sent,LPOVERLAPPED o) {
    if(!net_address(L"ConnectEx",a,len,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return FALSE;}
    return real_connectex.load()(s,a,len,b,n,sent,o);
}
INT PASCAL hook_ext_WSASendMsg(SOCKET s,LPWSAMSG m,DWORD f,LPDWORD sent,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r) {
    if(m && m->name && m->namelen && !net_address(L"WSASendMsg(ext)",m->name,m->namelen,__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    return real_sendmsg.load()(s,m,f,sent,o,r);
}
template<class Fn> bool wrap_extension(std::atomic<Fn>& slot,void* out,Fn hook,const wchar_t* name,void* caller) {
    Fn real=*(Fn*)out,expected=nullptr;
    if(slot.compare_exchange_strong(expected,real) || expected==real) {*(Fn*)out=hook;return true;}
    bool allowed=net_audit(name,redirect::NetVerdict::deny,L"second extension provider",caller);
    if(allowed) return true; // log-only: hand out the unwrapped pointer, recorded above
    *(Fn*)out=nullptr;return false;
}
int WSAAPI hook_WSAIoctl(SOCKET s,DWORD code,LPVOID in,DWORD inlen,LPVOID out,DWORD outlen,LPDWORD ret,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r) {
    int result=::WSAIoctl(s,code,in,inlen,out,outlen,ret,o,r);
    if(result==0 && code==SIO_GET_EXTENSION_FUNCTION_POINTER && in && inlen>=sizeof(GUID) && out && outlen>=sizeof(void*)) {
        GUID connect_ex=WSAID_CONNECTEX,send_msg=WSAID_WSASENDMSG;bool ok=true;
        if(!memcmp(in,&connect_ex,sizeof(GUID))) ok=wrap_extension<LPFN_CONNECTEX>(real_connectex,out,hook_ConnectEx,L"ConnectEx",__builtin_return_address(0));
        else if(!memcmp(in,&send_msg,sizeof(GUID))) ok=wrap_extension<LPFN_WSASENDMSG>(real_sendmsg,out,hook_ext_WSASendMsg,L"WSASendMsg(ext)",__builtin_return_address(0));
        if(!ok) {WSASetLastError(WSAEOPNOTSUPP);return SOCKET_ERROR;}
    }
    return result;
}
INT WSAAPI hook_getaddrinfo(PCSTR node,PCSTR service,const ADDRINFOA* hints,PADDRINFOA* result) {
    if(!net_host(L"getaddrinfo",ascii_host(node),__builtin_return_address(0))) {if(result) *result=nullptr;WSASetLastError(WSAHOST_NOT_FOUND);return WSAHOST_NOT_FOUND;}
    return ::getaddrinfo(node,service,hints,result);
}
INT WSAAPI hook_GetAddrInfoW(PCWSTR node,PCWSTR service,const ADDRINFOW* hints,PADDRINFOW* result) {
    if(!net_host(L"GetAddrInfoW",node?node:L"",__builtin_return_address(0))) {if(result) *result=nullptr;WSASetLastError(WSAHOST_NOT_FOUND);return WSAHOST_NOT_FOUND;}
    return ::GetAddrInfoW(node,service,hints,result);
}
using AddrInfoExFn=INT(WSAAPI*)(PCWSTR,PCWSTR,DWORD,LPGUID,const ADDRINFOEXW*,PADDRINFOEXW*,timeval*,LPOVERLAPPED,LPLOOKUPSERVICE_COMPLETION_ROUTINE,LPHANDLE);
AddrInfoExFn real_addrinfo_ex=nullptr;
INT WSAAPI hook_GetAddrInfoExW(PCWSTR node,PCWSTR service,DWORD ns,LPGUID id,const ADDRINFOEXW* hints,PADDRINFOEXW* result,timeval* t,LPOVERLAPPED o,LPLOOKUPSERVICE_COMPLETION_ROUTINE r,LPHANDLE h) {
    if(!net_host(L"GetAddrInfoExW",node?node:L"",__builtin_return_address(0))) {if(result) *result=nullptr;WSASetLastError(WSAHOST_NOT_FOUND);return WSAHOST_NOT_FOUND;}
    return real_addrinfo_ex(node,service,ns,id,hints,result,t,o,r,h);
}
hostent* WSAAPI hook_gethostbyname(const char* name) {
    if(!net_host(L"gethostbyname",ascii_host(name),__builtin_return_address(0))) {WSASetLastError(WSAHOST_NOT_FOUND);return nullptr;}
    return ::gethostbyname(name);
}
using ByNameWFn=BOOL(WSAAPI*)(SOCKET,LPWSTR,LPWSTR,LPDWORD,LPSOCKADDR,LPDWORD,LPSOCKADDR,const timeval*,LPWSAOVERLAPPED);
using ByNameAFn=BOOL(WSAAPI*)(SOCKET,LPCSTR,LPCSTR,LPDWORD,LPSOCKADDR,LPDWORD,LPSOCKADDR,const timeval*,LPWSAOVERLAPPED);
ByNameWFn real_by_name_w=nullptr;ByNameAFn real_by_name_a=nullptr;
BOOL WSAAPI hook_WSAConnectByNameW(SOCKET s,LPWSTR node,LPWSTR service,LPDWORD ll,LPSOCKADDR la,LPDWORD rl,LPSOCKADDR ra,const timeval* t,LPWSAOVERLAPPED o) {
    if(!net_host(L"WSAConnectByNameW",node?node:L"",__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return FALSE;}
    return real_by_name_w(s,node,service,ll,la,rl,ra,t,o);
}
BOOL WSAAPI hook_WSAConnectByNameA(SOCKET s,LPCSTR node,LPCSTR service,LPDWORD ll,LPSOCKADDR la,LPDWORD rl,LPSOCKADDR ra,const timeval* t,LPWSAOVERLAPPED o) {
    if(!net_host(L"WSAConnectByNameA",ascii_host(node),__builtin_return_address(0))) {WSASetLastError(WSAEACCES);return FALSE;}
    return real_by_name_a(s,node,service,ll,la,rl,ra,t,o);
}
// WinHTTP is how the game reaches its online service (ds.exe imports it). Refusing the connection handle is what
// the API does for an unusable server; no request is ever built, so nothing is resolved or sent.
HINTERNET WINAPI hook_WinHttpConnect(HINTERNET session,LPCWSTR server,INTERNET_PORT port,DWORD reserved) {
    // An empty server is not a passive bind here: there is nothing valid to connect to.
    if(!server || !*server) {SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    if(!net_host(L"WinHttpConnect",server,__builtin_return_address(0))) {SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);return nullptr;}
    return ::WinHttpConnect(session,server,port,reserved);
}
}
void flush_redirect_log() {flush_counts();}
void install_redirect(void* self) {
    own=(HMODULE)self;
    policy.instance=env(L"COOP_REDIRECT_INSTANCE");policy.steam=env(L"COOP_REDIRECT_STEAM");
    for(auto pair:std::vector<std::pair<const wchar_t*,const wchar_t*>>{
        {L"COOP_REAL_LOCAL",L"local"},{L"COOP_REAL_ROAMING",L"roaming"},
        {L"COOP_REAL_DOCUMENTS",L"documents"},{L"COOP_REAL_SAVEDGAMES",L"savedgames"}}) {
        auto r=env(pair.first);auto t=policy.instance+L"\\"+pair.second;
        if(redirect::within(policy.instance,r) || redirect::within(policy.instance,policy.steam) || !safe_tree(t,true)) fatal("redirect: unsafe roots\n");
        policy.roots.push_back({r,t});
    }
    {
        // Driver caches (LocalLow\NVIDIA\DXCache) and the two NVIDIA settings files the driver wrote in the live
        // run move into redirect-data too. ProgramData is optional: without it only LocalLow is mapped.
        wchar_t data[32768];DWORD n=::GetEnvironmentVariableW(L"ProgramData",data,32768);
        std::wstring program_data=n&&n<32768?redirect::canonical(data):L"";
        modules.program_data=program_data;
        for(auto& m:redirect::driver_roots(policy.roots[0].real,program_data,policy.instance)) {
            if(!safe_tree(m.target,true)) fatal("redirect: unsafe driver cache target\n");
            policy.roots.push_back(m);
        }
    }
    if(!safe_tree(policy.instance,true)) fatal("redirect: unsafe instance\n");
    modules.instance=env(L"COOP_MODULE_INSTANCE");modules.windows=env(L"SystemRoot");modules.steam=policy.steam;
    if(policy.instance!=modules.instance+L"\\redirect-data" || !safe_tree(modules.instance,false)) fatal("redirect: module/redirect instance mismatch\n");
    wchar_t mode[32];DWORD mode_n=::GetEnvironmentVariableW(L"COOP_MODULE_POLICY",mode,32);
    if(!mode_n) modules.mode=redirect::ModuleMode::strict;
    else if(mode_n<32 && !wcscmp(mode,L"strict")) modules.mode=redirect::ModuleMode::strict;
    else if(mode_n<32 && !wcscmp(mode,L"allowlist")) modules.mode=redirect::ModuleMode::allowlist;
    else if(mode_n<32 && !wcscmp(mode,L"log-only")) modules.mode=redirect::ModuleMode::log_only;
    else fatal("redirect: invalid module policy\n");
    {   // Named-object isolation for this slot (objects.h). Absent list = observe only.
        auto slash=modules.instance.find_last_of(L'\\');
        objects.tag=slash==std::wstring::npos?modules.instance:modules.instance.substr(slash+1);
        if(!redirect::valid_object_tag(objects.tag)) fatal("redirect: invalid object tag\n");
        wchar_t list[8192];DWORD list_n=::GetEnvironmentVariableW(L"COOP_ISOLATE_OBJECTS",list,8192);
        if(list_n>=8192 || (list_n && !redirect::parse_isolate_list(list,objects.isolate))) fatal("redirect: invalid object isolation list\n");
    }
    // Process ids are reused: live 2026-11-02 a game got the id of an earlier probe whose log was still there, the
    // exclusive create failed and the process ended (125). The creation time makes the name unique per process.
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) fatal("redirect: process creation time unavailable\n");
    wchar_t stamp[17];swprintf(stamp,17,L"%08lx%08lx",created.dwHighDateTime,created.dwLowDateTime);
    auto log=policy.instance+L"\\redirect-"+std::to_wstring(GetCurrentProcessId())+L"-"+stamp+L".log";
    log_file=real_cf(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log_file==INVALID_HANDLE_VALUE) fatal("redirect: cannot exclusively create log\n");
#define ADD(module,name) {auto m=GetModuleHandleW(L##module); auto o=m?real_gpa(m,#name):nullptr; if(!o) fatal("redirect: missing required API " #name "\n");entries.push_back({#name,(FARPROC)hook_##name,o});}
    ADD("kernel32.dll",CreateFileW);ADD("kernel32.dll",CreateFile2);
    ADD("kernel32.dll",CreateDirectoryW);ADD("kernel32.dll",DeleteFileW);ADD("kernel32.dll",RemoveDirectoryW);
    ADD("kernel32.dll",MoveFileExW);ADD("kernel32.dll",CopyFile2);ADD("kernel32.dll",SetFileAttributesW);
    ADD("kernel32.dll",GetEnvironmentVariableW);ADD("kernel32.dll",GetProcAddress);
    ADD("kernel32.dll",LoadLibraryW);ADD("kernel32.dll",LoadLibraryExW);ADD("kernel32.dll",LoadLibraryA);ADD("kernel32.dll",LoadLibraryExA);
    ADD("shell32.dll",SHGetKnownFolderPath);ADD("shell32.dll",SHGetFolderPathW);
    // Reads, existence checks, enumeration and free space (hook_list.h): required, the same list the test pins.
#define ADDQUERY(name) ADD("kernel32.dll",name)
    REDIRECT_QUERY_HOOKS(ADDQUERY)
#undef ADDQUERY
#undef ADD
#define ADDOPT(name) {auto o=real_gpa(GetModuleHandleW(L"kernel32.dll"),#name); if(o) entries.push_back({#name,(FARPROC)hook_##name,o});}
    ADDOPT(CreateFileA);ADDOPT(CreateDirectoryA);ADDOPT(DeleteFileA);ADDOPT(RemoveDirectoryA);ADDOPT(SetFileAttributesA);
    ADDOPT(MoveFileW);ADDOPT(MoveFileA);ADDOPT(MoveFileExA);ADDOPT(CopyFileW);ADDOPT(CopyFileA);
    ADDOPT(CreateDirectoryExW);ADDOPT(CreateDirectoryExA);ADDOPT(MoveFileWithProgressW);ADDOPT(ReplaceFileW);ADDOPT(ReplaceFileA);
    ADDOPT(CopyFileExW);ADDOPT(CopyFileExA);ADDOPT(CreateHardLinkW);ADDOPT(CreateHardLinkA);ADDOPT(CreateSymbolicLinkW);ADDOPT(CreateSymbolicLinkA);
    ADDOPT(SetFileInformationByHandle);
    ADDOPT(CreateMutexW);ADDOPT(CreateMutexA);ADDOPT(CreateMutexExW);ADDOPT(CreateMutexExA);ADDOPT(OpenMutexW);ADDOPT(OpenMutexA);
    ADDOPT(CreateEventW);ADDOPT(CreateEventA);ADDOPT(CreateEventExW);ADDOPT(CreateEventExA);ADDOPT(OpenEventW);ADDOPT(OpenEventA);
    ADDOPT(CreateSemaphoreW);ADDOPT(CreateSemaphoreA);
    ADDOPT(CreateFileMappingW);ADDOPT(CreateFileMappingA);ADDOPT(OpenFileMappingW);ADDOPT(OpenFileMappingA);
#undef ADDOPT
    {   // Outbound network policy (network_policy.h). Absent = enforce: loopback and listed peers only.
        wchar_t value[4096];DWORD n=::GetEnvironmentVariableW(L"COOP_NETWORK_POLICY",value,4096);
        if(n>=4096 || (n && !redirect::parse_net_mode(value,net.mode))) fatal("redirect: invalid network policy\n");
        n=::GetEnvironmentVariableW(L"COOP_NETWORK_PEERS",value,4096);
        if(n>=4096 || (n && !redirect::parse_net_peers(value,net.peers))) fatal("redirect: invalid network peer list\n");
#ifndef MUTANT_NET_UNHOOKED
        // ws2_32 and winhttp are static imports of this proxy, so both are loaded before DllMain runs.
#define ADDNET(module,name) {auto m=GetModuleHandleW(L##module); auto o=m?real_gpa(m,#name):nullptr; if(!o) fatal("redirect: missing required network API " #name "\n");entries.push_back({#name,(FARPROC)hook_##name,o});}
        ADDNET("ws2_32.dll",connect);ADDNET("ws2_32.dll",WSAConnect);ADDNET("ws2_32.dll",sendto);ADDNET("ws2_32.dll",WSASendTo);
        ADDNET("ws2_32.dll",WSAIoctl);ADDNET("ws2_32.dll",getaddrinfo);ADDNET("ws2_32.dll",GetAddrInfoW);ADDNET("ws2_32.dll",gethostbyname);
        ADDNET("winhttp.dll",WinHttpConnect);
#undef ADDNET
        auto ws2=GetModuleHandleW(L"ws2_32.dll");
#define ADDNETOPT(name,real) {auto o=real_gpa(ws2,#name); if(o) {real=reinterpret_cast<decltype(real)>(reinterpret_cast<void*>(o));entries.push_back({#name,(FARPROC)hook_##name,o});}}
        ADDNETOPT(GetAddrInfoExW,real_addrinfo_ex);ADDNETOPT(WSAConnectByNameW,real_by_name_w);ADDNETOPT(WSAConnectByNameA,real_by_name_a);
        ADDNETOPT(WSASendMsg,real_sendmsg_export);
#undef ADDNETOPT
#endif
        note(L"NETWORK",net.mode==redirect::NetMode::enforce?L"enforce":L"log-only",L"peers="+std::to_wstring(net.peers.size()));
    }
    // System DLLs often import API-set targets in KernelBase, whereas game
    // imports target Kernel32 wrappers. Cover both resolved addresses. Keep the
    // Kernel32 -> KernelBase forwarding edge original: our real API calls use
    // those wrappers, and redirecting that edge would recurse into our hook.
    auto kernel32=GetModuleHandleW(L"kernel32.dll"), base=GetModuleHandleW(L"kernelbase.dll");
    size_t original_entries=entries.size();
    for(size_t index=0;index<original_entries;++index) {
        Entry entry=entries[index];
        auto alternate=base?real_gpa(base,entry.name):nullptr;
        if(alternate && alternate!=entry.original) entries.push_back({entry.name,entry.hook,alternate,kernel32});
    }
#ifndef MUTANT_HOOKS_DISABLED
    patch_all(true);
#endif
    register_notifications();
    note(L"READY",mode_n?mode:L"strict",policy.instance);
}

namespace {
INIT_ONCE mod_once=INIT_ONCE_STATIC_INIT;
int mod_result=0;
HMODULE mod_handle=nullptr;
BOOL CALLBACK bootstrap_mod(PINIT_ONCE,PVOID,PVOID*) {
    // Keep socket/entropy/timer dependencies loader-initialized before install_redirect.
    // These references force static PE imports; never invoke networking in DllMain.
    if(GetTickCount64()==~ULONGLONG(0)) {WSACleanup();BCryptGenRandom(nullptr,nullptr,0,0);timeGetTime();}
    auto directory=policy.instance+L"\\mod";
    auto config=directory+L"\\mod.json",dll=directory+L"\\mod.dll";
    try {
        if(GetFileAttributesW(config.c_str())==INVALID_FILE_ATTRIBUTES) {
            if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) fatal("redirect: cannot inspect mod config\n");
            note(L"MOD_ABSENT",L"optional mod config absent",directory);return TRUE;
        }
        if(!safe_tree(config,true)||!safe_tree(dll,true)) fatal("redirect: unsafe mod/config path\n");
        HANDLE f=real_cf(config.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE) fatal("redirect: cannot read mod config\n");
        char bytes[4097];DWORD n=0;bool ok=ReadFile(f,bytes,sizeof(bytes),&n,nullptr);CloseHandle(f);
        if(!ok||n>4096) fatal("redirect: mod config read/size failure\n");
        auto c=mod::parseConfig(std::string(bytes,n));
        // This single reviewed library is the only explicit loader exception.
        // Its CRT entry point has no mod startup or IO. Resolve dependencies only
        // from the DLL folder and System32. Patch BEFORE calling any mod API.
        mod_handle=::LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        drain_modules();
        auto init=mod_handle?reinterpret_cast<int(*)(const char*)>(real_gpa(mod_handle,"mod_init")):nullptr;
        auto tick=mod_handle?real_gpa(mod_handle,"mod_tick"):nullptr;
        auto stop=mod_handle?real_gpa(mod_handle,"mod_shutdown"):nullptr;
        auto error=mod_handle?reinterpret_cast<const char*(*)()>(real_gpa(mod_handle,"mod_last_error")):nullptr;
        bool started=false;
        if(init&&tick&&stop&&error) {
            patch(mod_handle); // does not weaken any file or general loader rule
            std::string ascii;for(auto ch:config) {if(ch>127) fatal("redirect: mod config requires ASCII path\n");ascii+=char(ch);}
            started=init(ascii.c_str())!=0;
            if(!started) {OutputDebugStringA(error());note(L"MOD_INIT_FAILED",L"provider/hook/load unavailable",dll);}
        }
        if(!started) {
            if(c.required) fatal("redirect: REQUIRED mod failed to load/start; provider or hook unavailable\n");
            note(L"MOD_FAILED",L"optional mod failed to load/start",dll);return TRUE;
        }
        mod_result=1;note(L"MOD_STARTED",L"network only; no game-thread hook",dll);return TRUE;
    } catch(...) {fatal("redirect: invalid mod config or mod bootstrap exception\n");}
}
}
int start_instance_mod() {
    if(!InitOnceExecuteOnce(&mod_once,bootstrap_mod,nullptr,nullptr)) fatal("redirect: mod bootstrap failed\n");
    return mod_result;
}
void* original_compiler_export(const char* name) {
    static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
    static HMODULE original=nullptr;
    auto load=[](PINIT_ONCE,PVOID,PVOID*)->BOOL {
        wchar_t path[32768];DWORD n=GetModuleFileNameW(own,path,32768);
        if(!n||n>=32768) fatal("redirect: proxy path unavailable\n");
        std::wstring p(path);auto at=p.find_last_of(L"\\");if(at==std::wstring::npos) fatal("redirect: proxy directory unavailable\n");
        p.resize(at+1);p+=L"d3dcompiler_47_original.dll";
        if(!safe_tree(p,true)) fatal("redirect: unsafe original compiler DLL\n");
        original=::LoadLibraryExW(p.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!original) fatal("redirect: private original compiler load failed\n");
        drain_modules();patch(original);return TRUE;
    };
    if(!InitOnceExecuteOnce(&once,load,nullptr,nullptr)) fatal("redirect: compiler bootstrap failed\n");
    auto f=real_gpa(original,name);if(!f) fatal("redirect: original compiler export missing\n");return reinterpret_cast<void*>(f);
}
