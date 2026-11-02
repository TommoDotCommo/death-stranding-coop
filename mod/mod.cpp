#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <random>
using SOCKET=int;
constexpr int INVALID_SOCKET=-1;
#define closesocket close
#endif
#include "mod.h"
#include "config.h"
#include "discovery.h"
#include "providers.h"
#include "timing.h"
#include "identity.h"
#include "pose.h"
#ifdef MOD_FAKE_NETWORK
#include "fake_memory.h"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {
std::string error;
std::mutex lifecycle;
void checksum(ds::Bytes& b) {
    uint32_t h=2166136261u;for(size_t i=0;i<b.size();++i) {h^=i>=120&&i<124?0:b[i];h*=16777619u;}
    for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));
}
// No config-specified write paths. All writes are exclusive files beside config.
void safePath(const std::filesystem::path& p) {
    if(!p.is_absolute()) throw std::runtime_error("absolute config path required");
    for(auto q=p;!q.empty();q=q.parent_path()) {
#ifdef _WIN32
        auto attr=GetFileAttributesW(q.c_str());
        if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("reparse path refused");
#else
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(q))) throw std::runtime_error("symlink path refused");
#endif
        if(q==q.parent_path()) break;
    }
    if(std::filesystem::exists(p)&&std::filesystem::hard_link_count(p)!=1) throw std::runtime_error("hard-linked config/log refused");
}
std::string readConfig(const std::filesystem::path& p) {
    safePath(p);
#ifdef _WIN32
    HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) throw std::runtime_error("cannot read mod config");
    char b[4097];DWORD n=0;BOOL ok=ReadFile(f,b,sizeof(b),&n,nullptr);CloseHandle(f);
    if(!ok||n>4096) throw std::runtime_error("config read/size failure");
    return {b,n};
#else
    std::ifstream f(p);std::string s((std::istreambuf_iterator<char>(f)),{});
    if(!f || s.size()>4096) throw std::runtime_error("config read/size failure");
    return s;
#endif
}
uint64_t nonce() {
    uint64_t n=0;
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&n),sizeof(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) throw std::runtime_error("OS entropy failed");
#else
    std::random_device r;n=(uint64_t(r())<<32)^r();
#endif
    if(!n) throw std::runtime_error("zero OS nonce");
    return n;
}
struct Cache final:ds::StateProvider {
    uint64_t sampledAt=0;
    uint64_t localSampleTime(uint64_t) const override {return sampledAt;}
    ds::State local; ds::Render remote;bool available=false, gated=true, stickyTeleport=false;
    bool readLocal(ds::State& s) override {s=local;return available;}
    bool isInCutsceneOrLoading() const override {return gated;}
    void applyRemote(const ds::Render& r) override {remote=r;}
};
struct Runtime final:ds::Transport {
    mod::Config config;
    std::unique_ptr<mod::discovery::Worker> discovery;
    mod::TimerResolution timerResolution;
    mod::Histogram networkTick, providerTick, sendInterval, sampleAge, receiveApply, deliveryLateness;
    uint64_t lastNetwork=0,lastProvider=0,lastPresence=0,unappliedReceive=0;
    uint64_t pingSent=0,pongReceived=0,firstPing=0,lastPing=0,firstPong=0,lastPong=0;
    uint64_t observedClockStep=0;double measuredRtt=-1;

    SOCKET socket=INVALID_SOCKET;sockaddr_in peer{};
    std::mutex mutex;std::atomic<bool> running{false};std::thread worker;
#ifdef MOD_FAKE_NETWORK
    std::unique_ptr<fakemem::Fake> fake;   // declared before the sampler that reads it
#endif
    std::unique_ptr<mod::discovery::Sampler> sampler;
    std::unique_ptr<mod::PresenceLog> presence;
    mod::GameProvider* game=nullptr;       // alias of provider when provider is "game"
    std::thread ticker;uint64_t ticks=0;bool poseAvailable=false;std::string poseReason="not read";
    Cache cache;std::unique_ptr<ds::StateProvider> provider;std::unique_ptr<ds::Core> core;
    uint64_t sent=0,received=0,dropped=0,wrongAccepted=0,buildRefused=0,versionRefused=0,sessionRefused=0;
    bool paired=false,failed=false;std::string failure;uint64_t startedAt=0;std::vector<double> errors;
#ifdef MOD_FAKE_NETWORK
    std::deque<ds::Bytes> outgoing;
#endif
#ifdef _WIN32
    HANDLE log=INVALID_HANDLE_VALUE,presenceLog=INVALID_HANDLE_VALUE;bool wsa=false;
#else
    int log=-1,presenceLog=-1;
#endif
    struct Pending {uint64_t receivedUs;uint64_t due;ds::Bytes b;size_t size;};std::vector<Pending> pending;
    void note(const char* s) {
        std::cerr<<"mod: "<<s<<'\n';
#ifdef _WIN32
        OutputDebugStringA(s);if(log!=INVALID_HANDLE_VALUE) {DWORD n=0;auto len=DWORD(strlen(s));if(!WriteFile(log,s,len,&n,nullptr)||n!=len||!FlushFileBuffers(log)) throw std::runtime_error("mod log write failure");}
#else
        if(log!=-1 && write(log,s,strlen(s))!=static_cast<ssize_t>(strlen(s))) throw std::runtime_error("mod log write failure");
#endif
    }
    void send(const ds::Bytes& input) override {
        auto clock=mod::nowUs();
        if(input[6]==uint8_t(ds::Kind::Presence)) {
            if(lastPresence)sendInterval.add(clock-lastPresence);
            lastPresence=clock;
            sampleAge.add((clock/1000-cache.sampledAt)*1000);
            if(input[60]&ds::Teleport)cache.stickyTeleport=false;   // the jump has been sent
        }
        if(input[6]==uint8_t(ds::Kind::Ping)) {
            ++pingSent;lastPing=clock/1000-startedAt;if(pingSent==1)firstPing=lastPing;
        }
        auto b=input;std::copy(mod::WireBuild.begin(),mod::WireBuild.end(),b.begin()+16);
        if(config.fault=="build") b[12]^=1;
        if(config.fault=="version") b[4]=2;
        if(config.fault=="session") b[32]^=128;
        checksum(b);
        #ifdef MOD_FAKE_NETWORK
        if(outgoing.size()<256) {outgoing.push_back(b);++sent;} else ++dropped;
#else
        auto n=sendto(socket,reinterpret_cast<const char*>(b.data()),b.size(),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
        if(n==static_cast<int>(b.size())) ++sent;else ++dropped;
#endif
    }
    void accept(Pending p,uint64_t now) {
        uint64_t incomingSession=0;for(unsigned i=0;i<8;++i) incomingSession|=uint64_t(p.b[32+i])<<(8*i);
        bool badBuild=!std::equal(mod::WireBuild.begin(),mod::WireBuild.end(),p.b.begin()+16);
#ifndef MOD_MUTANT_BUILD
        if(badBuild) {++buildRefused;++dropped;return;}
#endif
        // Validate the full adapter identity first, then translate to the unchanged core pin.
        // Preserve original checksum failures: only rewrite identity on checksum-valid frames.
        auto check=p.b;checksum(check);
        bool checksumOK=std::equal(check.begin()+120,check.begin()+124,p.b.begin()+120);
        if(checksumOK) {std::copy(ds::ModBuild.begin(),ds::ModBuild.end(),p.b.begin()+16);checksum(p.b);}
        auto before=core->accepted();
        auto e=core->receive(p.b.data(),p.size,now);
        deliveryLateness.add((now-p.due)*1000);
        if(core->accepted()>before && !unappliedReceive)unappliedReceive=p.receivedUs;
        if(e==ds::Error::None && p.b[6]==uint8_t(ds::Kind::Pong)) {
            measuredRtt=core->rttMs();
            ++pongReceived;lastPong=now-startedAt;if(pongReceived==1)firstPong=lastPong;
        }
        if(e!=ds::Error::None) {
            ++dropped;if(e==ds::Error::Build)++buildRefused;if(e==ds::Error::Version)++versionRefused;if(e==ds::Error::Session)++sessionRefused;
        } else if(badBuild || p.b[12]!=uint8_t(ds::GameBuild) || p.b[4]!=1 || incomingSession!=config.session) ++wrongAccepted;
        paired|=core->connected();
    }
    void loop() {
        try {
            mod::Cadence cadence(2000);
            while(running) {
                {std::lock_guard<std::mutex> guard(mutex);auto clock=mod::nowUs();auto now=clock/1000;
                if(lastNetwork)networkTick.add(clock-lastNetwork);
                lastNetwork=clock;
                #ifndef MOD_FAKE_NETWORK
                for(unsigned budget=0;budget<64;++budget) {
                    ds::Bytes b{};sockaddr_in source{};
#ifdef _WIN32
                    int size=sizeof(source);
#else
                    socklen_t size=sizeof(source);
#endif
                    // Oversize datagrams must never be silently truncated to WireSize.
                    char raw[2048];int n=recvfrom(socket,raw,sizeof(raw),0,reinterpret_cast<sockaddr*>(&source),&size);
                    if(n<0) {
#ifdef _WIN32
                        int e=WSAGetLastError();if(e==WSAEWOULDBLOCK) break;
                        // A peer may not have bound yet; ICMP port-unreachable is transient.
                        if(e==WSAECONNRESET) {++dropped;break;}
                        if(e==WSAEMSGSIZE) {++dropped;continue;}
#else
                        if(errno==EAGAIN||errno==EWOULDBLOCK) break;
#endif
                        throw std::runtime_error("UDP receive failed");
                    }
                    ++received;
                    if(n!=int(ds::WireSize)||source.sin_addr.s_addr!=peer.sin_addr.s_addr||source.sin_port!=peer.sin_port) {++dropped;continue;}
                    memcpy(b.data(),raw,b.size());
                    uint64_t delay=config.impairment && b[6]==uint8_t(ds::Kind::Presence)?15+(received*17)%51:0;
                    if(pending.size()>=256) {++dropped;continue;}
                    pending.push_back({mod::nowUs(),now+delay,b,size_t(n)});
                }
                #endif
                for(size_t i=0;i<pending.size();) if(pending[i].due<=now) {auto p=pending[i];pending.erase(pending.begin()+i);accept(p,now);} else ++i;
                core->tick(now);paired|=core->connected();}
                cadence.wait();
            }
        } catch(const std::exception& e) {std::lock_guard<std::mutex> guard(mutex);failed=true;failure=e.what();try{note(failure.c_str());}catch(...){}running=false;}
    }
    void init(const std::filesystem::path& path) {
        config=mod::parseConfig(readConfig(path));
        if(config.logging) {
#ifdef _WIN32
            auto filename=path.parent_path()/("mod-"+mod::runStamp()+".log");safePath(filename);
            log=CreateFileW(filename.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(log==INVALID_HANDLE_VALUE) throw std::runtime_error("cannot exclusively create instance mod log");
#else
            auto filename=path.parent_path()/("mod-"+mod::runStamp()+".log");safePath(filename);log=open(filename.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(log==-1)throw std::runtime_error("cannot create mod log");
#endif
        }
        if(config.discovery) {
            discovery=std::make_unique<mod::discovery::Worker>(path,config.discoveryOptions);
            note("read-only discovery worker started; no provider, network or game calls\n");
            return;
        }
        if(config.provider=="game") {
            // Real failures (cannot create the log, hash mismatch, signature scan failure) throw and refuse startup.
            // An unavailable pose (main menu, loading) is NORMAL: presence stays gated until a pose is available.
            openPresenceLog(path);
            sampler=openSampler();
            presence=std::make_unique<mod::PresenceLog>([this](const std::string& line){return writePresence(line);},uint64_t(config.presenceLogMaxMiB)*1024*1024,[this](const char* s){note(s);});
            auto* raw=sampler.get();
            auto g=std::make_unique<mod::GameProvider>([raw]{return mod::PoseReader(*raw).read();},mod::nowUs,presence.get());
            game=g.get();provider=std::move(g);
        } else provider=std::make_unique<mod::SyntheticProvider>(config.slot);
        cache.sampledAt=mod::nowMs();cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();provider->applyRemote({});
        if(game){poseAvailable=cache.available;poseReason=game->reason();}
        if(!cache.available && !game) throw std::runtime_error("provider unavailable: synthetic provider failed; mod startup refused");
#ifndef MOD_FAKE_NETWORK
#ifdef _WIN32
        WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup failed");wsa=true;
#endif
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket==INVALID_SOCKET) throw std::runtime_error("UDP socket failed");
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(ds::portForSlot(config.slot));local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))) throw std::runtime_error("slot UDP bind failed (busy?)");
        peer.sin_family=AF_INET;peer.sin_port=htons(ds::portForSlot(config.peerSlot));
        if(inet_pton(AF_INET,config.peer.c_str(),&peer.sin_addr)!=1 || peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)) throw std::runtime_error("skeleton restricts peer to numeric 127.0.0.1");
#ifdef _WIN32
        u_long nonblock=1;if(ioctlsocket(socket,FIONBIO,&nonblock)) throw std::runtime_error("nonblocking UDP failed");
#else
        if(fcntl(socket,F_SETFL,O_NONBLOCK)<0) throw std::runtime_error("nonblocking UDP failed");
#endif
        #endif
        ds::Config c;c.role=config.role=="host"?ds::Role::Host:ds::Role::Client;c.session=config.session;c.nonce=nonce();
        core=std::make_unique<ds::Core>(c,cache,*this);
#ifdef MOD_MUTANT_BUILD
        ds::Protection protections;protections.build=false;core->setProtections(protections);
#endif
#ifdef MOD_MUTANT_INTERPOLATION
        ds::Protection protections;protections.delay=false;core->setProtections(protections);
#endif
        startedAt=mod::nowMs();
        auto prev=mod::nowUs();for(unsigned i=0;i<4096;++i) {auto t=mod::nowUs();if(t>prev && (!observedClockStep || t-prev<observedClockStep))observedClockStep=t-prev;prev=t;}
        note(game?"network ready; game provider (read-only pose, guarded copies, no writes, no actor); no game hook installed\n":"network ready; synthetic provider; no game hook installed\n");running=true;worker=std::thread([this]{loop();});
        if(config.selfTick)ticker=std::thread([this]{tickLoop();});
    }
    void openPresenceLog(const std::filesystem::path& path) {
#ifdef _WIN32
        auto filename=path.parent_path()/("presence-"+mod::runStamp()+".jsonl");safePath(filename);
        presenceLog=CreateFileW(filename.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(presenceLog==INVALID_HANDLE_VALUE) throw std::runtime_error("cannot exclusively create presence log");
#else
        auto filename=path.parent_path()/("presence-"+mod::runStamp()+".jsonl");safePath(filename);
        presenceLog=open(filename.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(presenceLog==-1)throw std::runtime_error("cannot create presence log");
#endif
    }
    bool writePresence(const std::string& line) {
#ifdef _WIN32
        DWORD n=0;return WriteFile(presenceLog,line.data(),DWORD(line.size()),&n,nullptr)&&n==line.size()&&FlushFileBuffers(presenceLog);
#else
        return write(presenceLog,line.data(),line.size())==static_cast<ssize_t>(line.size());
#endif
    }
    std::unique_ptr<mod::discovery::Sampler> openSampler() {
#ifdef MOD_FAKE_NETWORK
        // Test build: fake process memory in the live layout. It starts in the main menu (manager null).
        fake=std::make_unique<fakemem::Fake>();fake->select(2);fake->menu();
        auto* memory=fake.get();
        auto s=std::make_unique<mod::discovery::Sampler>([memory](uint64_t a,void* p,size_t n){return memory->readLocked(a,p,n);},fake->base);
        s->scan(mod::discovery::PinnedHash,config.discoveryOptions.sha256);return s;
#else
        return mod::discovery::openHostSampler(config.discoveryOptions.sha256);
#endif
    }
    // The body of mod_tick, shared with the self-tick thread. Takes only the runtime mutex (never the lifecycle lock).
    bool tickOnce(std::string& err) {
        std::lock_guard<std::mutex> guard(mutex);
        if(failed) {err=failure;return false;}
        try {
            auto clock=mod::nowUs();auto now=clock/1000;
            if(lastProvider)providerTick.add(clock-lastProvider);
            lastProvider=clock;++ticks;
            cache.sampledAt=now;cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();
            if(cache.available) {
                // A jump flag must survive until a presence packet carrying it has been sent.
                if(cache.local.flags&ds::Teleport)cache.stickyTeleport=true;else if(cache.stickyTeleport)cache.local.flags|=ds::Teleport;
            }
            if(game){poseAvailable=cache.available;poseReason=game->reason();}
            // Render at the actual apply time, not the preceding network iteration.
            core->frame(now);provider->applyRemote(cache.remote);
            if(unappliedReceive) {receiveApply.add(clock-unappliedReceive);unappliedReceive=0;}
            if(!cache.available && !game) {err="provider unavailable during tick";note(err.c_str());return false;}
            if(now-startedAt>1000 && config.provider=="synthetic" && cache.remote.visible && core->connected()) {
                auto truth=mod::trajectory(now>120?now-120:0,config.peerSlot);double e=0;
                for(unsigned i=0;i<3;++i) e+=std::pow(cache.remote.state.position[i]-truth.position[i],2);
                if(errors.size()<100000)errors.push_back(std::sqrt(e));
            }
            return true;
        }catch(const std::exception& e){err=e.what();return false;}
    }
    // Stand-in for a game-thread hook: fixed 20 Hz, a stall costs ticks, it never replays them.
    void tickLoop() {
        mod::FixedRate schedule(50000);
        while(running) {
            auto now=mod::nowUs();
            if(schedule.due(now)) {
                std::string err;
                if(!tickOnce(err)) {std::lock_guard<std::mutex> guard(mutex);if(!failed){failed=true;failure=err;}running=false;try{note(err.c_str());}catch(...){}break;}
            }
            auto wait=schedule.deadline>mod::nowUs()?schedule.deadline-mod::nowUs():0;
            std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(std::max<uint64_t>(wait,500),5000)));
        }
    }
    ~Runtime() {
        running=false;if(ticker.joinable())ticker.join();if(worker.joinable())worker.join();
        if(core && socket!=INVALID_SOCKET) {try{core->disconnect(mod::nowMs());}catch(...){}}
        if(socket!=INVALID_SOCKET)closesocket(socket);
#ifdef _WIN32
        if(wsa)WSACleanup();
        if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
        if(presenceLog!=INVALID_HANDLE_VALUE)CloseHandle(presenceLog);
#else
        if(log!=-1)close(log);
        if(presenceLog!=-1)close(presenceLog);
#endif
    }
};
// Deliberately no static Runtime destructor: Windows process detach is under
// loader lock. Explicit shutdown owns cleanup; process exit lets the OS reclaim.
Runtime* runtime=nullptr;
}
extern "C" {
#ifdef MOD_FAKE_NETWORK
// Test-only datagram boundary. Production DLLs never export these functions.
// Move the fake player (game axes, z up; rotation row-major) and enter the world; menu=1 returns to the main menu.
MOD_API const char* mod_test_run_stamp() {static std::string stamp=mod::runStamp();return stamp.c_str();}
MOD_API void mod_test_set_pose(const double* position,const float* rotation,int menu) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime||!runtime->fake)return;
    auto* f=runtime->fake.get();
    f->mutate([&]{f->pose(position[0],position[1],position[2],rotation);if(menu)f->menu();else f->game();});
}
MOD_API int mod_test_drain(unsigned char* bytes) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;
    auto& r=*runtime;std::lock_guard<std::mutex> guard(r.mutex);
    if(r.discovery) {if(r.discovery->failed()){error="read-only discovery failed; inspect discovery JSONL and debug output";return 0;}return 1;}
    if(r.outgoing.empty())return 0;
    memcpy(bytes,r.outgoing.front().data(),ds::WireSize);r.outgoing.pop_front();return 1;
}
MOD_API void mod_test_deliver(const unsigned char* bytes) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;
    auto& r=*runtime;std::lock_guard<std::mutex> guard(r.mutex);++r.received;
    if(r.pending.size()>=256){++r.dropped;return;}
    ds::Bytes b;memcpy(b.data(),bytes,b.size());
    uint64_t delay=r.config.impairment&&b[6]==uint8_t(ds::Kind::Presence)?15+(r.received*17)%51:0;
    r.pending.push_back({mod::nowUs(),mod::nowMs()+delay,b,b.size()});
}
#endif
int mod_init(const char* path) {
    std::lock_guard<std::mutex> lock(lifecycle);
    try {if(runtime) throw std::runtime_error("mod already initialized");if(!path) throw std::runtime_error("null config path");auto r=std::make_unique<Runtime>();r->init(std::filesystem::path(path));runtime=r.release();error.clear();return 1;}
    catch(const std::exception& e) {error=e.what();std::cerr<<"mod_init FAILED: "<<error<<'\n';return 0;}
}
int mod_tick(double dt) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if(!runtime || !std::isfinite(dt)||dt<0||dt>1) {error="invalid tick or uninitialized mod";return 0;}
    auto& r=*runtime;
    if(r.config.selfTick) {   // the mod ticks itself; an external tick is a validated no-op, never a second tick source
        std::lock_guard<std::mutex> guard(r.mutex);
        if(r.failed) {error=r.failure;return 0;}
        return 1;
    }
    std::string err;
    if(!r.tickOnce(err)) {error=err;return 0;}
    return 1;
}
void mod_shutdown() {std::lock_guard<std::mutex> lock(lifecycle);auto* r=runtime;runtime=nullptr;delete r;}
const char* mod_last_error() {
    thread_local std::string copy;
    std::lock_guard<std::mutex> lock(lifecycle);
    if(runtime) {std::lock_guard<std::mutex> guard(runtime->mutex);copy=runtime->discovery&&runtime->discovery->failed()?"read-only discovery failed; inspect discovery JSONL and debug output":runtime->failed?runtime->failure:error;} else copy=error;
    return copy.c_str();
}
unsigned mod_status(char* out,unsigned cap) {
    std::lock_guard<std::mutex> lock(lifecycle);std::string s="{}";
    if(runtime && runtime->discovery) {
        s=std::string("{\"discovery\":true,\"failed\":")+(runtime->discovery->failed()?"true":"false")+"}";
    } else if(runtime) {auto& r=*runtime;std::lock_guard<std::mutex> guard(r.mutex);auto errors=r.errors;std::sort(errors.begin(),errors.end());
        auto p=[&](double q){return errors.empty()?-1.:errors[size_t((errors.size()-1)*q)];};
        std::ostringstream o;o<<"{\"handshake\":"<<(r.paired?"true":"false")<<",\"connected\":"<<(r.core->connected()?"true":"false")<<",\"sent\":"<<r.sent<<",\"received\":"<<r.received<<",\"dropped\":"<<r.dropped<<",\"accepted\":"<<r.core->accepted()<<",\"wrong_accepted\":"<<r.wrongAccepted<<",\"build_refused\":"<<r.buildRefused<<",\"version_refused\":"<<r.versionRefused<<",\"session_refused\":"<<r.sessionRefused<<",\"rtt_ms\":"<<r.measuredRtt<<",\"current_rtt_ms\":"<<r.core->rttMs()<<",\"samples\":"<<errors.size()<<",\"error_p50\":"<<p(.5)<<",\"error_p95\":"<<p(.95)<<",\"error_p99\":"<<p(.99)<<",\"error_max\":"<<p(1)<<",\"failed\":"<<(r.failed?"true":"false")<<",\"self_tick\":{\"enabled\":"<<(r.config.selfTick?"true":"false")<<",\"ticks\":"<<r.ticks<<"},\"pose\":{\"available\":"<<(r.poseAvailable?"true":"false")<<",\"reason\":\""<<r.poseReason<<"\"}";
        o<<",\"timing\":{\"clock\":\""
#ifdef _WIN32
        <<"QueryPerformanceCounter"
#else
        <<"steady_clock"
#endif
        <<"\",\"frequency_hz\":"<<mod::clockFrequency()<<",\"clock_quantum_us\":"<<1000000./mod::clockFrequency()<<",\"observed_step_us\":"<<r.observedClockStep<<",\"wire_quantum_us\":1000,\"interpolation_ms\":120,\"extrapolation_cap_ms\":150,\"bucket_upper_us\":[";
        for(size_t i=0;i<mod::Histogram::limits.size();++i)o<<(i?",":"")<<mod::Histogram::limits[i];
        o<<"],\"network_tick\":"<<r.networkTick.json()<<",\"provider_tick\":"<<r.providerTick.json()<<",\"presence_send\":"<<r.sendInterval.json()<<",\"sample_age\":"<<r.sampleAge.json()<<",\"receive_to_apply\":"<<r.receiveApply.json()<<",\"delivery_lateness\":"<<r.deliveryLateness.json()
        <<",\"ping_sent\":"<<r.pingSent<<",\"pong_received\":"<<r.pongReceived<<",\"first_ping_ms\":"<<(r.pingSent?int64_t(r.firstPing):-1)<<",\"last_ping_ms\":"<<(r.pingSent?int64_t(r.lastPing):-1)<<",\"first_pong_ms\":"<<(r.pongReceived?int64_t(r.firstPong):-1)<<",\"last_pong_ms\":"<<(r.pongReceived?int64_t(r.lastPong):-1)<<"}}";s=o.str();}
    if(out&&cap>=s.size()+1)memcpy(out,s.c_str(),s.size()+1);
    return unsigned(s.size()+1);
}
}
