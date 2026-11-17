#pragma once
// Shared, raw UTF-8 diagnostics. Redaction belongs to the report exporter.
// Chain: FNV-1a/32, continued over "<sequence> <utc-ms> <text>" (without LF).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace diag {
namespace fs = std::filesystem;
#ifdef _WIN32
class Mutex {
    SRWLOCK lock_=SRWLOCK_INIT;
public:
    void lock(){AcquireSRWLockExclusive(&lock_);}
    void unlock(){ReleaseSRWLockExclusive(&lock_);}
    bool try_lock(){return TryAcquireSRWLockExclusive(&lock_)!=FALSE;}
};
#else
using Mutex=std::mutex;
#endif
// A mod loaded behind save redirection delegates diagnostics to its proxy.
// Only these fixed-purpose operations cross the DLL boundary; no file-system
// permission or caller-selected path is granted to the game or the mod.
struct Host {
    void* context;
    bool (*open)(void*,unsigned);
    bool (*line)(void*,const char*);
    void (*flight)(void*,const char*,uint64_t);
    bool (*bugmark)(void*,uint32_t,uint64_t);
    void (*crash)(void*);
    void (*close)(void*,bool);
};
constexpr uint32_t Basis=2166136261u;
constexpr unsigned DefaultMaxLogMb=200, MinMaxLogMb=5, MaxMaxLogMb=2000;
inline bool ValidMaxLogMb(uint64_t n) {return n>=MinMaxLogMb && n<=MaxMaxLogMb;}
inline uint64_t UtcMs() {return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
inline unsigned Pid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return unsigned(getpid());
#endif
}
inline std::string FormatLine(uint32_t& chain,uint64_t n,uint64_t utc,const std::string& text) {
    std::string body=text;
    for(auto& c:body) if(c=='\r'||c=='\n') c=' ';
#ifdef MUTANT_DIAG_REDACT
    body="redacted";
#endif
    auto head=std::to_string(n)+" "+std::to_string(utc)+" ";
#ifndef MUTANT_DIAG_CHAIN
    for(unsigned char c:head+body) {chain^=c;chain*=16777619u;}
#endif
    char hex[9];std::snprintf(hex,sizeof hex,"%08x",chain);
    return head+hex+" "+body+"\n";
}
// Only our four components and exact session/sidecar names are eligible for pruning.
inline std::string KeyOf(const std::string& name) {
    auto dot=name.find('.');if(dot==std::string::npos)return {};
    auto key=name.substr(0,dot);
#ifdef MUTANT_DIAG_PRUNE_SCOPE
    return key;
#endif
    if(key.size()<23||key[8]!='-'||key[15]!='_')return {};
    for(size_t i=0;i<15;++i)if(i!=8&&(key[i]<'0'||key[i]>'9'))return {};
    auto p=key.find("_p",16);if(p==std::string::npos)return {};
    auto kind=key.substr(16,p-16);
    if(kind!="game"&&kind!="proxy"&&kind!="launcher"&&kind!="world")return {};
    if(p+2==key.size()||key.find_first_not_of("0123456789",p+2)!=std::string::npos)return {};
    auto ext=name.substr(dot);
    if(ext!=".log"&&ext!=".crash"&&ext!=".dmp") {
        if(ext.compare(0,8,".flight-")||ext.size()<13||ext.substr(ext.size()-4)!=".txt")return {};
        auto num=ext.substr(8,ext.size()-12);if(num.empty()||num.find_first_not_of("0123456789")!=std::string::npos)return {};
    }
    return key;
}
inline size_t Prune(const fs::path& dir,uint64_t cap,const std::string& current) {
    struct Entry {fs::path path;std::string key;uint64_t bytes;};
    std::vector<Entry> entries;std::vector<std::string> crashed;uint64_t total=0;size_t removed=0;
    std::error_code ec;
    for(fs::directory_iterator it(dir,ec),end;!ec&&it!=end;it.increment(ec)) {
        if(!it->is_regular_file(ec)||it->is_symlink(ec))continue;
        auto key=KeyOf(it->path().filename().string());if(key.empty())continue;
        auto size=it->file_size(ec);if(ec){ec.clear();continue;}
        total+=size;entries.push_back({it->path(),key,size});
        if(it->path().extension()==".crash")crashed.push_back(key);
    }
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){
#ifdef MUTANT_DIAG_PRUNE_ORDER
        return a.key>b.key;
#else
        return a.key<b.key;
#endif
    });
    for(const auto& e:entries) {
        if(total<=cap)break;
        if(e.key==current)continue;
#ifndef MUTANT_DIAG_PRUNE_ACTIVE
        // Protect every sidecar of an active run, not just its open .log.
        auto logPath=dir/(e.key+".log");
#ifdef _WIN32
        HANDLE probe=CreateFileW(logPath.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(probe==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION)continue;
        if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);
#else
        int probe=::open(logPath.c_str(),O_WRONLY|O_NOFOLLOW);
        if(probe>=0){bool active=flock(probe,LOCK_EX|LOCK_NB)<0;::close(probe);if(active)continue;}
#endif
#endif
#ifndef MUTANT_DIAG_PRUNE_CRASH
        if(std::find(crashed.begin(),crashed.end(),e.key)!=crashed.end())continue;
#endif
        // A live process holds this file against deletion on Windows. On POSIX,
        // its exclusive advisory lock supplies the same protection.
#ifndef _WIN32
        int fd=::open(e.path.c_str(),O_WRONLY|O_NOFOLLOW);if(fd<0)continue;
#ifndef MUTANT_DIAG_PRUNE_ACTIVE
        if(flock(fd,LOCK_EX|LOCK_NB)<0){::close(fd);continue;}
#endif
#endif
        if(fs::remove(e.path,ec)){total-=e.bytes;++removed;}
#ifndef _WIN32
        ::close(fd);
#endif
        ec.clear();
    }
    return removed;
}
inline fs::path DefaultLogsDir() {
#ifdef _WIN32
    // Inside a game the proxy names its co-op folder (COOP_MODULE_INSTANCE): for a player that is
    // %LOCALAPPDATA%\DeathStrandingCoop itself, for a developer's test copy the copy's own folder, so test runs never
    // write into the player's logs (and into his diagnostics).
    wchar_t inst[32768];auto m=GetEnvironmentVariableW(L"COOP_MODULE_INSTANCE",inst,32768);
#ifndef MUTANT_DIAG_LOGS_SHARED
    if(m&&m<32768)return fs::path(inst)/L"logs";
#else
    (void)m;
#endif
    wchar_t local[32768];auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    return n&&n<32768?fs::path(local)/L"DeathStrandingCoop"/L"logs":fs::path();
#else
    if(auto p=std::getenv("XDG_STATE_HOME"))return fs::path(p)/"DeathStrandingCoop"/"logs";
    if(auto p=std::getenv("HOME"))return fs::path(p)/".local"/"state"/"DeathStrandingCoop"/"logs";
    return {};
#endif
}
// Exclusive creation, never truncate a previous run or follow a link.
class File {
#ifdef _WIN32
    HANDLE h_=INVALID_HANDLE_VALUE;
#else
    int h_=-1;
#endif
public:
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
    ~File(){Close();}
    bool Ready()const {
#ifdef _WIN32
        return h_!=INVALID_HANDLE_VALUE;
#else
        return h_!=-1;
#endif
    }
    bool Open(const fs::path& p) {
#ifdef _WIN32
        h_=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
#ifdef MUTANT_DIAG_EXCLUSIVE
            CREATE_ALWAYS,
#else
            CREATE_NEW,
#endif
            FILE_ATTRIBUTE_NORMAL,nullptr);
#else
        h_=::open(p.c_str(),O_CREAT|O_WRONLY|O_NOFOLLOW|
#ifdef MUTANT_DIAG_EXCLUSIVE
            O_TRUNC,
#else
            O_EXCL,
#endif
            0600);
        if(h_!=-1&&flock(h_,LOCK_EX|LOCK_NB)<0){::close(h_);h_=-1;}
#endif
        return Ready();
    }
    bool Write(const std::string& s) {
#ifdef _WIN32
        DWORD n=0;return Ready()&&WriteFile(h_,s.data(),DWORD(s.size()),&n,nullptr)&&n==s.size();
#else
        return Ready()&&::write(h_,s.data(),s.size())==ssize_t(s.size());
#endif
    }
    void Close() {
#ifdef _WIN32
        if(Ready())CloseHandle(h_);
        h_=INVALID_HANDLE_VALUE;
#else
        if(Ready())::close(h_);
        h_=-1;
#endif
    }
};
class Session {
    Mutex mutex_;File file_;fs::path base_;std::string key_;const Host* host_=nullptr;
    uint32_t chain_=Basis;uint64_t n_=0,bytes_=0,cap_=0;std::atomic<bool> crashed_{false};
    struct Sample {uint64_t at;std::string line;};std::deque<Sample> ring_;size_t ringBytes_=0;
    void Trim(uint64_t now) {
#ifdef MUTANT_DIAG_FLIGHT
        (void)now;return;
#endif
        while(!ring_.empty()&&(now-ring_.front().at>60000||ringBytes_>8*1024*1024)) {ringBytes_-=ring_.front().line.size();ring_.pop_front();}
    }
    bool Write(const std::string& text,bool closing=false) {
        if(!file_.Ready())return false;
        auto next=chain_;auto line=FormatLine(next,n_,UtcMs(),text);
#ifndef MUTANT_DIAG_CAP
        if(bytes_+line.size()+(closing?0:128)>cap_)return false;
#endif
        if(!file_.Write(line))return false;
        bytes_+=line.size();chain_=next;++n_;return true;
    }
public:
    void Bind(const Host* host){
#ifndef MUTANT_DIAG_HOST
        host_=host;
#else
        (void)host;
#endif
    }
    bool Open(const fs::path& dir,const char* component,unsigned maxMb=DefaultMaxLogMb,uint64_t testCap=0) {
        if(host_)return host_->open(host_->context,maxMb);
        if(dir.empty()||!ValidMaxLogMb(maxMb))return false;
        std::error_code ec;
        // Trusted private data directory; reject symlinks/reparse ancestors.
#ifndef MUTANT_DIAG_PATH
        for(auto p=dir;!p.empty();p=p.parent_path()) {
#ifdef _WIN32
            auto a=GetFileAttributesW(p.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#else
            if(fs::is_symlink(fs::symlink_status(p,ec)))return false;
            ec.clear();
#endif
            if(p==p.parent_path())break;
        }
#endif
        fs::create_directories(dir,ec);if(ec)return false;
        auto now=std::time(nullptr);std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc,&now);
#else
        gmtime_r(&now,&utc);
#endif
        char stamp[32];std::strftime(stamp,sizeof stamp,"%Y%m%d-%H%M%S",&utc);
        key_=std::string(stamp)+"_"+component+"_p"+std::to_string(Pid());base_=dir/key_;
        if(KeyOf(key_+".log").empty())return false;
        cap_=testCap?testCap:uint64_t(maxMb)*1024*1024;
        Prune(dir,cap_,key_);
        if(!file_.Open(fs::path(base_).concat(".log")))return false;
        PrepareCrashPath();
        return Write(std::string("session start: ")+component);
    }
    const fs::path& Base()const{return base_;}
    const std::string& Key()const{return key_;}
    bool Line(const std::string& text) {
        if(host_)return host_->line(host_->context,text.c_str());
        std::lock_guard<Mutex> l(mutex_);return Write(text);
    }
    void Flight(const std::string& text,uint64_t now=UtcMs()) {
        if(host_){host_->flight(host_->context,text.c_str(),now);return;}
        std::lock_guard<Mutex> l(mutex_);ring_.push_back({now,text});ringBytes_+=text.size();Trim(now);
    }
    bool Bugmark(uint32_t mark,uint64_t now=UtcMs()) {
        if(host_)return host_->bugmark(host_->context,mark,now);
        std::lock_guard<Mutex> l(mutex_);
        if(!mark||!file_.Ready())return false;
        Write("BUGMARK "+std::to_string(mark));Trim(now);
        File flight;if(!flight.Open(fs::path(base_).concat(".flight-"+std::to_string(mark)+".txt")))return false;
        std::string text="BUGMARK "+std::to_string(mark)+"\n";
        for(auto& row:ring_)text+=std::to_string(row.at)+" "+row.line+(row.line.empty()||row.line.back()!='\n'?"\n":"");
#ifdef MUTANT_DIAG_FLIGHT_CAP
        return flight.Write(text);
#else
        return flight.Write(text.substr(0,size_t(cap_)));
#endif
    }
    // Exception path: no log mutex, allocation or CRT. Paths prepared at Open.
    void MarkCrashed() noexcept {
        if(host_){host_->crash(host_->context);return;}
        if(base_.empty())return;
        crashed_=true;
#ifdef _WIN32
        HANDLE f=CreateFileW(crashPath_,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE){const char s[]="unhandled exception\n";DWORD n;WriteFile(f,s,sizeof(s)-1,&n,nullptr);CloseHandle(f);}
#else
        File f;if(!base_.empty()&&f.Open(fs::path(base_).concat(".crash")))f.Write("unhandled exception\n");
#endif
    }
    void PrepareCrashPath() {
#ifdef _WIN32
        auto p=fs::path(base_).concat(".crash").wstring();if(p.size()<32768)std::wcscpy(crashPath_,p.c_str());
#endif
    }
    void Close(bool nonblocking=false) {
        if(host_){host_->close(host_->context,nonblocking);return;}
        std::unique_lock<Mutex> l(mutex_,std::defer_lock);
        if(nonblocking){if(!l.try_lock())return;}else l.lock();
#ifndef MUTANT_DIAG_CLEAN
        if(!crashed_)Write("session end: clean exit",true);
#endif
        file_.Close();
    }
private:
#ifdef _WIN32
    wchar_t crashPath_[32768]{};
#endif
};
} // namespace diag
