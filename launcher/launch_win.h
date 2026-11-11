#pragma once
#include "core.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <ctime>
#include <tlhelp32.h>
namespace launcher {
namespace fs=std::filesystem;
inline std::wstring wtext(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring v(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),v.data(),n);return v;
}
inline std::string asciiPath(const fs::path& p) {std::string s;for(auto c:p.wstring()){if(c<32||c>126)throw std::runtime_error("The current proxy needs an ASCII-only data and game path. Choose another folder.");s+=char(c);}return s;}
inline fs::path environment(const wchar_t* key) {wchar_t b[32768];auto n=GetEnvironmentVariableW(key,b,32768);return n&&n<32768?fs::path(b):fs::path();}
inline fs::path known(REFKNOWNFOLDERID key) {PWSTR s=nullptr;if(FAILED(SHGetKnownFolderPath(key,KF_FLAG_DONT_VERIFY,nullptr,&s)))throw std::runtime_error("Windows could not locate a required user folder.");fs::path p=s;CoTaskMemFree(s);return p;}
inline fs::path registryPath(HKEY hive,const wchar_t* subkey,const wchar_t* value) {wchar_t b[32768];DWORD bytes=sizeof(b);return RegGetValueW(hive,subkey,value,RRF_RT_REG_SZ,nullptr,b,&bytes)==ERROR_SUCCESS?fs::path(b):fs::path();}
inline fs::path steamRoot() {return registryPath(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath");}
inline fs::path steamGame() {return registryPath(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1850570",L"InstallLocation");}
inline bool equalPath(const fs::path& a,const fs::path& b) {return redirect::canonical(a.wstring())==redirect::canonical(b.wstring())&&!redirect::canonical(a.wstring()).empty();}
inline bool gameRunning(const fs::path& game) {
    auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check whether the game is running. Close the game and try again.");
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);bool found=false;
    if(!Process32FirstW(snap,&process)){CloseHandle(snap);throw std::runtime_error("Cannot enumerate game processes.");}
    do {
        if(_wcsicmp(process.szExeFile,L"ds.exe"))continue;
        auto h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process.th32ProcessID);
        wchar_t path[32768];DWORD count=32768;
        // An unreadable ds.exe is not proof that backing up or starting another copy is safe.
        if(!h||!QueryFullProcessImageNameW(h,0,path,&count))found=true;
        else found=found||equalPath(path,game/L"ds.exe");
        if(h)CloseHandle(h);
    }while(Process32NextW(snap,&process));
    CloseHandle(snap);return found;
}
inline std::string fileHash(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0))throw std::runtime_error("Cannot open Windows SHA-256.");
    auto cleanup=[&]{if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);};
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)){cleanup();throw std::runtime_error("Cannot create SHA-256.");}
    std::ifstream f(path,std::ios::binary);std::vector<char> bytes(1024*1024);bool ok=bool(f);
    while(f){f.read(bytes.data(),bytes.size());auto n=f.gcount();if(n&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(n),0)){ok=false;break;}}
    unsigned char result[32];ok=ok&&f.eof()&&!BCryptFinishHash(hash,result,sizeof(result),0);cleanup();
    if(!ok)throw std::runtime_error("Cannot read the game executable. Check the game folder and try again.");
    std::string out;for(auto b:result){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
struct LaunchOptions {fs::path game,data,world;bool direct=false,prepareOnly=false;};   // world: coop-world.exe (beside the launcher)
inline release_startup::Values launchValues(const LaunchOptions& o,const Session& s) {
    release_startup::Values v;
    auto put=[&](const char* key,const fs::path& fallback) {auto p=o.direct?environment(release_startup::wide(key).c_str()):fs::path();v[key]=asciiPath(p.empty()?fallback:p);};
    put("COOP_MODULE_INSTANCE",o.data);put("COOP_REDIRECT_INSTANCE",o.data/"redirect-data");
    if(!equalPath(release_startup::wide(v.at("COOP_MODULE_INSTANCE")),o.data))throw std::runtime_error("The data folder differs from COOP_MODULE_INSTANCE. Choose the test copy's data folder.");
    put("COOP_REDIRECT_STEAM",steamRoot());
    put("COOP_REAL_LOCAL",known(FOLDERID_LocalAppData));put("COOP_REAL_ROAMING",known(FOLDERID_RoamingAppData));put("COOP_REAL_DOCUMENTS",known(FOLDERID_Documents));put("COOP_REAL_SAVEDGAMES",known(FOLDERID_SavedGames));
    v["COOP_MODULE_POLICY"]="allowlist";v["COOP_NETWORK_POLICY"]="enforce";v["COOP_NETWORK_PEERS"]=s.peer;
    if(o.direct)for(auto key:{"COOP_REAL_LOCAL","COOP_REAL_ROAMING","COOP_REAL_DOCUMENTS","COOP_REAL_SAVEDGAMES"}) {
        auto instance=redirect::canonical(wtext(v.at("COOP_REDIRECT_INSTANCE"))),real=redirect::canonical(wtext(v.at(key)));
        if(redirect::within(instance,real))throw std::runtime_error("Lab/direct copies need --data-dir outside the real user folders. The LocalAppData layout is reserved for release tickets.");
    }
    return v;
}
inline void launchGame(const LaunchOptions& o,Session s,bool solo=false) {
    s.solo=solo;
    auto exe=o.game/L"ds.exe";
    if(gameRunning(o.game))throw std::runtime_error("This game copy is already running. Close it before starting a new session.");
    if(fileHash(exe)!=mod::discovery::PinnedHash)throw std::runtime_error("This game build is unsupported. Use Steam Director's Cut build 13300582.");
    if(!o.direct&&!equalPath(o.game,steamGame()))throw std::runtime_error("Steam is registered to another game folder, or its install path is unavailable. Select the registered Steam install in Settings. Test copies require --dev-direct.");
    if(!fs::is_regular_file(o.data/"redirect-data/mod/mod.dll"))throw std::runtime_error("The co-op mod is missing. Install or repair the package first.");
    // Solo is isolated too: a host with no partner. Ordinary Steam starts remain pass-through.
    auto values=launchValues(o,s);auto bytes=ticket(values,asciiPath(exe),uint64_t(std::time(nullptr)));
    writeConfig(o.data,s);
    for(auto name:{"local","roaming","documents","savedgames","profile"})fs::create_directories(o.data/"redirect-data"/name);
    if(o.direct) {
        for(auto& e:values)if(!SetEnvironmentVariableW(wtext(e.first).c_str(),wtext(e.second).c_str()))throw std::runtime_error("Cannot set the isolated child environment.");
    } else atomicWrite(o.data/"launcher-session.ini",bytes);
    if(o.prepareOnly)return;
    // Shared world: the host's launcher runs the group's server (a second start cannot take the port and leaves).
    if(auto arguments=worldArguments(s,asciiPath(o.data/"world"/"world.json"));!arguments.empty()) {
        if(!fs::is_regular_file(o.world))throw std::runtime_error("The shared-world server (coop-world.exe) is missing beside the launcher. Repair the package or turn the shared world off in Settings.");
        fs::create_directories(o.data/"world");
        std::wstring command=L"\""+o.world.wstring()+L"\"";for(auto& a:arguments)command+=L" \""+wtext(a)+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(o.world.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Windows could not start the shared-world server.");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
    if(o.direct) {
        std::wstring command=L"\""+exe.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,o.game.c_str(),&startup,&process))throw std::runtime_error("Windows could not start the test copy. Check the folder and try again.");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    } else if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",L"steam://run/1850570",nullptr,nullptr,SW_SHOWNORMAL))<=32) {
        std::error_code ec;fs::remove(o.data/"launcher-session.ini",ec);throw std::runtime_error("Steam could not open. Start Steam and try again.");
    }
}
inline void registerHandler(const fs::path& executable) {
    const std::wstring base=L"Software\\Classes\\deathstranding-coop";
    auto put=[](const std::wstring& sub,const wchar_t* name,const std::wstring& value){HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,sub.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("Cannot register invite links for this Windows user.");auto code=RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);if(code!=ERROR_SUCCESS)throw std::runtime_error("Cannot write invite registration.");};
    put(base,nullptr,L"URL:Death Stranding co-op");put(base,L"URL Protocol",L"");
    put(base+L"\\shell\\open\\command",nullptr,L"\""+executable.wstring()+L"\" \"%1\"");
}
}
