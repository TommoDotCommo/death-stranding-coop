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
#include <sys/syscall.h>
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
#include "game_thread.h"
#include "puppet.h"
#include "double.h"
#include "body.h"
#include "auto_continue.h"
#include "pad.h"
#include "cargo.h"
#include "structures.h"
#ifdef MOD_FAKE_NETWORK
#include "fake_memory.h"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <cstring>
#include <deque>
#include <optional>
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
uint32_t currentThread() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return uint32_t(syscall(SYS_gettid));
#endif
}
// Process-lifetime pad state shared with the proxy's input-thread wrapper; never destroyed with a Runtime.
struct PadShared {std::mutex lock;mod::pad::Player player;std::atomic<bool> enabled{false};std::atomic<uint64_t> calls{0},active{0};};
PadShared padShared;
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
    mod::dispatch::Gate gameGate;
    uint64_t gameRejected=0;bool dispatchVerified=false,staleCommandNoted=false;
    std::filesystem::path puppetCommand;
    std::unique_ptr<mod::puppet::Probe> puppet;
    // The game's resident Sam double, borrowed as the partner's body (double.h). Armed only by a command for this process.
    enum class TwinMode {None,Echo,Lead,Peer};
    std::filesystem::path twinCommand;std::unique_ptr<mod::puppet::Double> twin;TwinMode twinMode=TwinMode::None;
    std::filesystem::path cargoCommand;std::function<void(uint32_t,const mod::puppet::Transform&)> cargoCreate;
    std::filesystem::path bodyCommand;std::unique_ptr<mod::body::Probe> body;   // own-body experiment (body.h)
    std::function<void(uint32_t)> cargoGive,cargoDelete;std::function<void(uint8_t,const mod::puppet::Transform&,bool)> construct;
    std::function<void(const std::array<float,4>&,const std::array<float,4>&,const mod::puppet::Transform&,bool,bool,bool)> unconstruct;
    std::function<void()> continueLastSave;mod::AutoContinue autoContinue;bool autoNoted=false;
    std::function<bool()> gameEvent;bool inEvent=false;std::atomic<bool> fakeEvent{false};
    // Shared cargo (cargo.h): placements read from the game's cargo map go to the partner; theirs are created here.
    mod::cargo::Sync cargoSync;uint64_t cargoReadAt=0,cargoManagerSlot=0,cargoManagerVtable=0,cargoSent=0,cargoMade=0,cargoTaken=0,cargoRemoved=0;
    bool cargoIdsSet=false;
    void tickCargo(uint64_t now) {
        if(!core||!core->connected())return;
        // Shared ids below 2^53 (a wire t-field): 32 random bits above a 20-bit counter.
        if(!cargoIdsSet){cargoSync.setIdBase((nonce()>>32)<<20);cargoIdsSet=true;}
        auto incoming=core->takeCargo();
        bool coop=twinMode==TwinMode::Peer&&cargoCreate&&cargoManagerSlot;
        for(auto& e:incoming) {
            if(e.type==3){structIn.push_back(e);continue;}   // a structure (tickStructures)
            if(!coop){note("CARGO partner event ignored (not in peer mode)\n");continue;}
            if(e.type==2&&structMirrors.count(e.ref)){structGone.push_back(e.ref);continue;}   // a dismantled structure
            if(e.type==2) {
                // The partner picked up (or used) a shared item: remove our copy if it lies in the world here.
                auto index=cargoSync.partnerTook(e.ref);std::ostringstream o;
                if(index&&cargoDelete){cargoDelete(*index);++cargoRemoved;o<<"CARGO partner took "<<e.ref<<": removed record "<<*index<<"\n";}
                else o<<"CARGO partner took "<<e.ref<<": nothing to remove here\n";
                note(o.str().c_str());continue;
            }
            if(e.type!=1){note("CARGO unknown partner event type ignored\n");continue;}
            auto t=mod::puppet::fromState(e.position,e.yaw);
            cargoSync.expect({e.item,t.position,e.ref},now);cargoCreate(e.item,t);++cargoMade;
            std::ostringstream o;o<<"CARGO created partner item "<<e.item<<" ref "<<e.ref<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
        }
        if(!coop||now-cargoReadAt<250)return;
        cargoReadAt=now;uint64_t manager=0;if(!sampler->read(cargoManagerSlot,&manager,8))return;
        auto anchor=mod::puppet::readLocalAnchor(*sampler);if(!anchor.entity||!anchor.pose.available)return;
        auto records=mod::cargo::readRecords(*sampler,manager,cargoManagerVtable);if(records.empty())return;
        for(auto& p:cargoSync.observe(records,anchor.pose.position,now)) {
            ds::CargoEvent e;e.type=1;e.item=p.hash;e.ref=p.id;e.position={p.position[0],p.position[2],p.position[1]};   // game -> adapter axes
            core->queueCargo(e);++cargoSent;
            std::ostringstream o;o<<"CARGO placed item "<<p.hash<<" ref "<<p.id<<" at "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<" -> partner\n";note(o.str().c_str());
        }
        for(auto id:cargoSync.takeTaken()) {
            ds::CargoEvent e;e.type=2;e.ref=id;e.position=core?std::array<double,3>{anchor.pose.position[0],anchor.pose.position[2],anchor.pose.position[1]}:std::array<double,3>{};
            core->queueCargo(e);++cargoTaken;
            note(("CARGO took shared "+std::to_string(id)+" -> partner\n").c_str());
        }
    }
    // Shared structures (structures.h): what this porter builds goes to the partner; theirs appear here as Net structures.
    mod::structures::Sync structSync;uint64_t structReadAt=0,structSent=0,structMade=0,structIdBase=0,imageSize=0;
    void tickStructures(uint64_t now) {
        if(!core||!core->connected()||!sampler)return;
        bool coop=twinMode==TwinMode::Peer&&construct;
        // Incoming structures arrive with the cargo events (type 3); tickCargo hands them over in structIn.
        uint64_t manager=0;std::vector<mod::structures::Record> records;
        auto table=[&]()->const std::vector<mod::structures::Record>&{
            if(records.empty()&&sampler->read(sampler->base+0x7bf4960,&manager,8)&&manager){
                if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
                records=mod::structures::readTable(*sampler,manager,sampler->base,imageSize);}
            return records;};
        for(auto& e:structIn) {
            if(!coop){note("STRUCT partner structure ignored (not in peer mode)\n");continue;}
            std::array<double,3> game{e.position[0],e.position[2],e.position[1]};   // adapter -> game axes
            auto b=mod::structures::unpack(e.item,game,e.yaw);auto net=mod::structures::netVariant(b.type);
            if(!net){note(("STRUCT partner type "+std::to_string(b.type)+" has no Net variant; ignored\n").c_str());continue;}
            structMirrors[e.ref]=b.position;
#ifndef MOD_MUTANT_STRUCTURE_TWICE
            if(mod::structures::standing(table(),*net,b.position)){note(("STRUCT partner structure ref "+std::to_string(e.ref)+" already stands here\n").c_str());continue;}
#endif
            mod::puppet::Transform t;t.position=b.position;t.rotation=mod::structures::rotation(b.yaw,b.pitch,b.roll);
            construct(*net,t,false);++structMade;
            std::ostringstream o;o<<"STRUCT created partner type "<<int(b.type)<<" as "<<int(*net)<<" ref "<<e.ref<<" at "<<b.position[0]<<","<<b.position[1]<<","<<b.position[2]<<"\n";note(o.str().c_str());
        }
        structIn.clear();
        // The partner dismantled a structure: remove our Net copy (owner 2 only, a 3 m box around where it stands).
        for(auto ref:structGone) {
            auto at=structMirrors.find(ref);if(at==structMirrors.end()||!unconstruct)continue;
            mod::puppet::Transform t;t.position=at->second;t.rotation={1,0,0,0,1,0,0,0,1};
            unconstruct({-1.5f,-1.5f,-3,0},{1.5f,1.5f,3,0},t,false,false,true);++structRemoved;
            std::ostringstream o;o<<"STRUCT partner removed ref "<<ref<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
            structMirrors.erase(at);
        }
        structGone.clear();
        if(!coop||now-structReadAt<500)return;
        structReadAt=now;
        if(!structIdBase)structIdBase=(nonce()>>32)<<20;
        records.clear();if(table().empty())return;
        for(auto& b:structSync.observe(records)) {
            ds::CargoEvent e;e.type=3;e.item=mod::structures::pack(b);e.ref=structIdBase+b.id;e.yaw=b.yaw;
            e.position={b.position[0],b.position[2],b.position[1]};   // game -> adapter axes
            core->queueCargo(e);++structSent;
            std::ostringstream o;o<<"STRUCT built type "<<int(b.type)<<" id "<<b.id<<" at "<<b.position[0]<<","<<b.position[1]<<","<<b.position[2]<<" -> partner\n";note(o.str().c_str());
        }
        for(auto id:structSync.takeRemoved()) {
            ds::CargoEvent e;e.type=2;e.ref=structIdBase+id;core->queueCargo(e);
            note(("STRUCT dismantled id "+std::to_string(id)+" -> partner\n").c_str());
        }
    }
    std::map<uint64_t,std::array<double,3>> structMirrors;std::vector<uint64_t> structGone;uint64_t structRemoved=0;
    std::vector<ds::CargoEvent> structIn;
    std::deque<std::pair<uint64_t,ds::State>> echo;
    mod::puppet::ActionCache actionCache;uint64_t animatorVtable=0;
    std::filesystem::path padCommand;bool padRunning=false,posePublished=false,poseLogged=false;
    // pad.script: a stamp-guarded script (pad.h) that replaces any running one; consumed when read.
    void tickPad() {
        if(!config.virtualPad)return;
        if(std::filesystem::exists(padCommand)) {
            auto text=readConfig(padCommand);
            if(!std::filesystem::remove(padCommand))throw std::runtime_error("cannot consume pad script");
            try {
                auto steps=mod::pad::parse(text,mod::runStamp());auto n=steps.size();
                {std::lock_guard<std::mutex> l(padShared.lock);padShared.player.load(std::move(steps));}
                padRunning=true;note(("PAD script started steps="+std::to_string(n)+"\n").c_str());
            } catch(const std::exception& e) {note((std::string("PAD script refused: ")+e.what()+"\n").c_str());}
        }
        if(padRunning) {bool done;{std::lock_guard<std::mutex> l(padShared.lock);done=padShared.player.done();}
            if(done){padRunning=false;note("PAD script finished\n");}}
    }
    // One line per change, so an outside driver can wait for gameplay by reading the log.
    void notePose() {
        if(!game||(poseLogged&&posePublished==poseAvailable))return;
        poseLogged=true;posePublished=poseAvailable;
        note((std::string("POSE ")+(poseAvailable?"available":"unavailable")+" reason="+poseReason+"\n").c_str());
    }
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
            if(config.gameThread) {
#ifdef MOD_FAKE_NETWORK
                const auto sites=mod::dispatch::FixtureSites;
                fake->dispatchSites(sites);
#else
                const auto sites=mod::dispatch::PinnedSites;
#endif
                mod::dispatch::validateSites(*sampler,sites);gameGate=mod::dispatch::Gate(sites.framePattern+16);dispatchVerified=true;
            }
            if(config.puppetProbe) {
                puppetCommand=path.parent_path()/"puppet.spawn";
#ifdef MOD_FAKE_NETWORK
                mod::puppet::Api api;
                api.local=[this]{return mod::puppet::readLocalAnchor(*sampler);};
                api.read=[this](uint64_t e){return mod::puppet::readEntityPose(*sampler,e);};
                api.canCreate=[](uint64_t){return true;};
                api.resource=[this](uint64_t){return fake->base+0x9000;};
                api.create=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0x8000,t.position.data(),t.rotation.data());return fake->base+0x8000;};
                api.place=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0x8000,t.position.data(),t.rotation.data());};
                api.remove=[this](uint64_t){fake->mutate([&]{fake->blocked=fake->base+0x80c8;});};
#else
                auto api=mod::puppet::nativeApi(*sampler);
#endif
                puppet=std::make_unique<mod::puppet::Probe>(std::move(api),[this](const std::string& row){note(row.c_str());});
                twinCommand=path.parent_path()/"puppet.double";cargoCommand=path.parent_path()/"puppet.cargo";bodyCommand=path.parent_path()/"puppet.body";
#ifdef MOD_FAKE_NETWORK
                // Fake tree: player entity 0x6000 -> CameraEntity 0xa000 -> parked double 0xa800.
                fake->mutate([&]{auto B=fake->base;fake->put<uint64_t>(0x6078,B+0xa000);
                    fake->put<uint64_t>(0xa000,B+0xff00);fake->put<uint64_t>(0xa070,B+0x6000);fake->put<uint64_t>(0xa078,B+0xa800);
                    fake->put<uint64_t>(0xa800,B+0xff10);fake->put<uint64_t>(0xa870,B+0xa000);fake->put<uint64_t>(0xa888,mod::puppet::FlagSleeping|0x551);
                    const double origin[3]{};const float identity[9]{1,0,0,0,1,0,0,0,1};
                    memcpy(fake->data.data()+0xa8c8,origin,24);memcpy(fake->data.data()+0xa8e0,identity,36);});
                mod::puppet::DoubleApi twinApi;
                twinApi.local=[this]{return mod::puppet::readLocalAnchor(*sampler);};
                twinApi.find=[this](uint64_t p){return mod::puppet::findDouble(*sampler,p,fake->base+0xff00,fake->base+0xff10);};
                twinApi.flags=[this](uint64_t e){return mod::puppet::readFlags(*sampler,e);};
                twinApi.read=[this](uint64_t e){return mod::puppet::readEntityPose(*sampler,e);};
                auto flag=[this](uint64_t bit,bool on){fake->mutate([&]{uint64_t f=0;memcpy(&f,fake->data.data()+0xa888,8);fake->put<uint64_t>(0xa888,on?f|bit:f&~bit);});};
                twinApi.sleep=[flag](uint64_t,bool b){flag(mod::puppet::FlagSleeping,b);};
                twinApi.visible=[flag](uint64_t,bool b){flag(mod::puppet::FlagVisible,b);};
                twinApi.place=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0xa800,t.position.data(),t.rotation.data());};
                twinApi.anchor=[this](uint64_t,const mod::puppet::Transform& t){fake->mutate([&]{memcpy(fake->data.data()+0xab58,&t,60);});};
                twinApi.animate=[](uint64_t,double,uint32_t){};
                twinApi.upgrade=[](uint64_t,uint64_t){return uint64_t(0);};twinApi.downgrade=[](uint64_t,uint64_t){};
                twinApi.effects=[](uint64_t,bool){};
#else
                auto twinApi=mod::puppet::nativeDoubleApi(*sampler);
                cargoCreate=mod::puppet::nativeCargoCreate(*sampler);cargoGive=mod::puppet::nativeCargoGive(*sampler);cargoDelete=mod::puppet::nativeCargoDelete(*sampler);construct=mod::puppet::nativeConstruct(*sampler);unconstruct=mod::puppet::nativeRemoveConstructions(*sampler);
                gameEvent=mod::puppet::nativeGameEvent(*sampler);
                if(config.autoContinue){continueLastSave=mod::puppet::nativeContinueLastSave(*sampler);autoContinue=mod::AutoContinue(true);}
                body=std::make_unique<mod::body::Probe>(mod::body::nativeApi(*sampler),sampler->base+0x3d7de48,[this](const std::string& row){note(row.c_str());});
#endif
#ifdef MOD_FAKE_NETWORK
                gameEvent=[this]{return fakeEvent.load();};
#endif
                twin=std::make_unique<mod::puppet::Double>(std::move(twinApi),[this](const std::string& row){note(row.c_str());});
                twin->walk(true);   // borrow the porter's animation network so the partner walks (game_offsets.md)
            }
            presence=std::make_unique<mod::PresenceLog>([this](const std::string& line){return writePresence(line);},uint64_t(config.presenceLogMaxMiB)*1024*1024,[this](const char* s){note(s);});
            auto* raw=sampler.get();
            auto g=std::make_unique<mod::GameProvider>([raw]{return mod::PoseReader(*raw).read();},mod::nowUs,presence.get());
            game=g.get();provider=std::move(g);
        } else provider=std::make_unique<mod::SyntheticProvider>(config.slot);
        padCommand=path.parent_path()/"pad.script";padShared.enabled=config.virtualPad;
        if(sampler)animatorVtable=sampler->base+0x3d09910;   // MorphemeAnimationManager (pinned build)
#ifndef MOD_FAKE_NETWORK
        if(sampler){cargoManagerSlot=sampler->base+0x7bf4968;cargoManagerVtable=sampler->base+0x3dd9338;}   // DSBaggage2Manager global, vtable
#endif
        cache.sampledAt=mod::nowMs();
        if(!config.gameThread){cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();provider->applyRemote({});}
        if(game){poseAvailable=cache.available;poseReason=game->reason();}
        if(!cache.available && !game) throw std::runtime_error("provider unavailable: synthetic provider failed; mod startup refused");
#ifndef MOD_FAKE_NETWORK
#ifdef _WIN32
        WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup failed");wsa=true;
#endif
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket==INVALID_SOCKET) throw std::runtime_error("UDP socket failed");
        peer.sin_family=AF_INET;peer.sin_port=htons(ds::portForSlot(config.peerSlot));peer.sin_addr.s_addr=htonl(config.peerAddress);
        // Local address: loopback for a loopback peer; otherwise the configured bind address, or the address of the
        // interface that routes to the peer (a connected UDP socket's name; nothing is sent). Never all interfaces.
        uint32_t localAddress=(config.peerAddress>>24)==127?(config.bindAddress?config.bindAddress:0x7f000001):config.bindAddress;
        if(!localAddress) {
            auto probe=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in route{};
#ifdef _WIN32
            int size=sizeof(route);
#else
            socklen_t size=sizeof(route);
#endif
            bool ok=probe!=INVALID_SOCKET&&connect(probe,reinterpret_cast<sockaddr*>(&peer),sizeof(peer))==0&&getsockname(probe,reinterpret_cast<sockaddr*>(&route),&size)==0;
            if(probe!=INVALID_SOCKET){
#ifdef _WIN32
                closesocket(probe);
#else
                close(probe);
#endif
            }
            if(!ok||!route.sin_addr.s_addr) throw std::runtime_error("no local address routes to the peer");
            localAddress=ntohl(route.sin_addr.s_addr);
        }
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(ds::portForSlot(config.slot));local.sin_addr.s_addr=htonl(localAddress);
        if(bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))) throw std::runtime_error("slot UDP bind failed (busy?)");
        {std::ostringstream o;o<<"NET local "<<(localAddress>>24)<<'.'<<((localAddress>>16)&255)<<'.'<<((localAddress>>8)&255)<<'.'<<(localAddress&255)
                              <<':'<<ds::portForSlot(config.slot)<<" peer "<<config.peer<<':'<<ds::portForSlot(config.peerSlot)<<"\n";note(o.str().c_str());}
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
        note(config.gameThread?"network ready; awaiting validated game-thread callback\n":game?"network ready; game provider (read-only pose, guarded copies, no writes, no actor); no game hook installed\n":"network ready; synthetic provider; no game hook installed\n");running=true;worker=std::thread([this]{loop();});
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
    // echo: the double replays the local porter one second late, two metres to the right (single-instance check).
    // peer: the double shows the received partner. off: hand it back to the game. One command per file write.
    // Own-body experiment: "create <stamp>" asks the engine for a second Sam from the double's resource, 3 m to the
    // porter's right; "remove <stamp>" removes what it produced. One command per file write; the probe logs the rest.
    void tickBody() {
        if(std::filesystem::exists(bodyCommand)) {
            auto command=readConfig(bodyCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(bodyCommand))throw std::runtime_error("cannot consume body command");
            if(command=="create"+stamp) {
#ifndef MOD_BODY_CREATE_EXPERIMENT
                // Live 2026-11-07 the creator call never returned and froze the mod (game_offsets.md); refused by default.
                note("BODY refused: the direct creator route froze the mod live (build with MOD_BODY_CREATE_EXPERIMENT to retry)\n");
                return;
#endif
                auto anchor=mod::puppet::readLocalAnchor(*sampler);
                auto twinEntity=anchor.entity?mod::puppet::findDouble(*sampler,anchor.entity,sampler->base+0x3d198e8,sampler->base+0x3d7de48):0;
                if(!anchor.entity||!anchor.pose.available||!twinEntity)note("BODY refused: no porter or no double\n");
                else {
                    auto t=mod::puppet::transform(anchor.pose);double rx=t.rotation[0],ry=t.rotation[1],l=std::hypot(rx,ry);
                    if(l>.5){t.position[0]+=3*rx/l;t.position[1]+=3*ry/l;}
                    std::array<uint64_t,2> id{mod::nowUs()^0x436f6f70426f6479ULL,std::hash<std::string>{}(mod::runStamp())|1};
                    body->create(twinEntity,t,id);
                }
            } else if(command=="remove"+stamp)body->remove();
            else note("BODY ignored command for another process\n");
        }
        body->tick();
    }
    void tickTwin(uint64_t now) {
        if(std::filesystem::exists(twinCommand)) {
            auto command=readConfig(twinCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(twinCommand))throw std::runtime_error("cannot consume double command");
            if(command=="echo"+stamp)twinMode=TwinMode::Echo;else if(command=="peer"+stamp)twinMode=TwinMode::Peer;
            else if(command=="lead"+stamp)twinMode=TwinMode::Lead;
            else if(command=="still"+stamp){twin->walk(false);note("DOUBLE own (private-room) network for the next acquire\n");return;}
            else if(command=="off"+stamp)twinMode=TwinMode::None;else {note("DOUBLE ignored command for another process\n");return;}
            note(("DOUBLE command "+command.substr(0,command.find(' '))+"\n").c_str());twin->enable();echo.clear();
        }
        std::optional<mod::puppet::Transform> target;double speed=0;uint32_t actions=0;
        auto packed=[](const ds::State& s){return uint32_t(s.flags)|(uint32_t(s.action&31)<<16);};
        auto horizontal=[](const ds::State& s){return std::hypot(s.velocity[0],s.velocity[2]);};
        if(twinMode==TwinMode::Peer&&cache.remote.visible){target=mod::puppet::fromState(cache.remote.state.position,cache.remote.state.yaw);speed=horizontal(cache.remote.state);actions=packed(cache.remote.state);}
        if(twinMode==TwinMode::Echo&&cache.available) {
            echo.emplace_back(now,cache.local);
            while(echo.size()>1&&now-echo[1].first>=1000)echo.pop_front();
            if(now-echo.front().first>=1000) {
                auto s=echo.front().second;double y=s.yaw;
                // Game right vector (cos, -sin, 0) in adapter axes (x, up, z).
                s.position[0]+=2*std::cos(y);s.position[2]-=2*std::sin(y);
                target=mod::puppet::fromState(s.position,y);speed=horizontal(s);actions=packed(s);
            }
        }
        if(twinMode==TwinMode::Lead&&cache.available) {
            // Single-copy view test: the double walks 3 m ahead and 1.5 m right of the porter, in front of the camera.
            auto s=cache.local;double y=s.yaw;
            s.position[0]+=3*std::sin(y)+1.5*std::cos(y);s.position[2]+=3*std::cos(y)-1.5*std::sin(y);
            target=mod::puppet::fromState(s.position,y);speed=horizontal(s);actions=packed(s);
        }
        if(twinMode==TwinMode::None)echo.clear();
        // During a local game event or cutscene the double goes back to the game, so it never appears in one.
        bool event=gameEvent&&gameEvent();
        if(event!=inEvent){inEvent=event;note(event?"DOUBLE game event: double handed back\n":"DOUBLE game event over\n");}
        // A partner who is only missing for now (late packets, private room) is hidden, not handed back; a game event
        // or the mode switched off hands the double back.
        twin->tick(true,(target&&!event)?&*target:nullptr,speed,actions,twinMode!=TwinMode::None&&!event);
    }
    bool tickOnce(std::string& err,bool gameContext=false) {
        std::lock_guard<std::mutex> guard(mutex);
        if(failed) {err=failure;return false;}
        try {
            auto clock=mod::nowUs();auto now=clock/1000;
            if(lastProvider)providerTick.add(clock-lastProvider);
            lastProvider=clock;++ticks;
            cache.sampledAt=now;cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();
            if(game&&cache.available&&gameContext) {
                // What the porter is doing, from his own animation variables (read-only): sprint and crouch.
                auto anchor=mod::puppet::readLocalAnchor(*sampler);
                if(anchor.entity){auto a=mod::puppet::readActions(*sampler,anchor.entity,animatorVtable,actionCache);
                    cache.local.flags|=uint16_t(a&0xffff);cache.local.action=uint8_t((a>>16)&31);}
                // In the private room the game uses this porter's own double: tell the partner, whose core then hides us.
                // Same for a game event or cutscene: the partner hides us instead of showing a porter in a cutscene pose.
                if(anchor.entity&&twin&&(twin->gameUsesDouble(anchor.entity)||inEvent))cache.local.flags|=ds::PrivateRoom;
            }
            if(cache.available) {
                // A jump flag must survive until a presence packet carrying it has been sent.
                if(cache.local.flags&ds::Teleport)cache.stickyTeleport=true;else if(cache.stickyTeleport)cache.local.flags|=ds::Teleport;
            }
            if(game){poseAvailable=cache.available;poseReason=game->reason();}
            // Render at the actual apply time, not the preceding network iteration.
            core->frame(now);provider->applyRemote(cache.remote);
            if(puppet&&gameContext) {
                if(!puppet->armed()&&std::filesystem::exists(puppetCommand)) {
                    auto command=readConfig(puppetCommand);
                    if(command=="spawn "+mod::runStamp()+"\n") {
                        if(!std::filesystem::remove(puppetCommand))throw std::runtime_error("cannot consume puppet command");
                        note("PUPPET command consumed for this process; one experiment only\n");puppet->arm();
                    } else if(!staleCommandNoted){note("PUPPET ignored command for another process\n");staleCommandNoted=true;}
                }
                puppet->tick(now,true);
            }
            if(twin&&gameContext)tickTwin(now);
            if(twin&&gameContext)tickCargo(now);
            if(twin&&gameContext)tickStructures(now);
            if(cargoCreate&&gameContext&&std::filesystem::exists(cargoCommand)) {
                auto text=readConfig(cargoCommand);
                if(!std::filesystem::remove(cargoCommand))throw std::runtime_error("cannot consume cargo command");
                auto id=mod::puppet::parseCargoCommand(text,mod::runStamp());auto anchor=mod::puppet::readLocalAnchor(*sampler);
                auto give=mod::puppet::parseCargoCommand(text,mod::runStamp(),"give");
                auto erase=mod::puppet::parseCargoCommand(text,mod::runStamp(),"delete");
                auto list=mod::puppet::parseCargoCommand(text,mod::runStamp(),"list");
                if(give&&cargoGive){cargoGive(*give);note(("CARGO give id="+std::to_string(*give)+"\n").c_str());}
                else if(erase&&cargoDelete){cargoDelete(*erase);note(("CARGO delete id="+std::to_string(*erase)+"\n").c_str());}
                else if(auto mask=mod::puppet::parseCargoCommand(text,mod::runStamp(),"unbuild");mask&&unconstruct&&anchor.entity&&anchor.pose.available) {
                    // Removal experiment: "unbuild <mask> <stamp>", a 6 x 6 x 10 m box around the point 4 m in front of the
                    // porter; mask bit 1 = owner 1, bit 2 = owner 0, bit 4 = owner 2.
                    auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                    if(l>.5){t.position[0]+=4*fx/l;t.position[1]+=4*fy/l;}
                    unconstruct({-3,-3,-5,0},{3,3,5,0},t,*mask&1,*mask&2,*mask&4);
                    note(("UNBUILD mask "+std::to_string(*mask)+"\n").c_str());
                }
                else if(auto type=mod::puppet::parseCargoCommand(text,mod::runStamp(),"construct");type&&construct&&anchor.entity&&anchor.pose.available) {
                    // Structure experiment: "construct <type> <stamp>" (type < 46; +100 sets the flag) 4 m in front of the porter.
                    bool flag=*type>=100;uint32_t kind=flag?*type-100:*type;
                    if(kind>=46)note("CONSTRUCT refused: type out of range\n");
                    else {
                        auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                        if(l>.5){t.position[0]+=4*fx/l;t.position[1]+=4*fy/l;}
                        construct(uint8_t(kind),t,flag);
                        std::ostringstream o;o<<"CONSTRUCT type "<<kind<<" flag "<<flag<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
                    }
                }
                else if(list) {
                    // Every record in use within `list` metres of the porter (0 = all): slot, +1c, +20, hash, position.
                    uint64_t manager=0;std::vector<mod::cargo::Record> records;
                    if(cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8))records=mod::cargo::readRecords(*sampler,manager,cargoManagerVtable);
                    std::ostringstream o;o<<"CARGO list "<<records.size()<<" records"<<std::hex;
                    for(auto& r:records){
                        double d=anchor.pose.available?std::hypot(r.position[0]-anchor.pose.position[0],r.position[1]-anchor.pose.position[1]):0;
                        if(*list&&(!anchor.pose.available||d>*list))continue;
                        o<<" | slot="<<r.slot<<" h=0x"<<r.holder<<" s=0x"<<r.state<<" item=0x"<<r.hash<<std::dec<<" at="<<std::setprecision(7)
                         <<r.position[0]<<","<<r.position[1]<<","<<r.position[2]<<std::hex;
                    }
                    note((o.str()+"\n").c_str());
                }
                else if(!id||!anchor.entity||!anchor.pose.available)note("CARGO refused command\n");
                else {
                    // Two metres behind the porter (the side he just came from is free), half a metre up, upright.
                    auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                    if(l>.5){t.position[0]-=2*fx/l;t.position[1]-=2*fy/l;}
                    t.position[2]+=.5;
                    t.rotation={1,0,0,0,1,0,0,0,1};
                    cargoCreate(*id,t);
                    note(("CARGO create id="+std::to_string(*id)+"\n").c_str());
                }
            }
            if(continueLastSave&&gameContext) {
                if(autoContinue.tick(now-startedAt,cache.available)){continueLastSave();if(autoContinue.calls()==1)note("AUTO continue: asking the game to load the last save\n");}
                if(autoContinue.done()&&!autoNoted){autoNoted=true;note(("AUTO in game after "+std::to_string((now-startedAt)/1000)+" s, "+std::to_string(autoContinue.calls())+" requests\n").c_str());}
            }
            if(body&&gameContext)tickBody();
            tickPad();notePose();
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
MOD_API uint32_t mod_test_thread_id() {return currentThread();}
MOD_API uint64_t mod_test_frame_caller() {return mod::dispatch::FixtureSites.framePattern+16;}
MOD_API const char* mod_test_run_stamp() {static std::string stamp=mod::runStamp();return stamp.c_str();}
MOD_API void mod_test_set_event(int on) {std::lock_guard<std::mutex> lock(lifecycle);if(runtime)runtime->fakeEvent=on!=0;}
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
int mod_pad(mod_pad_state* s) {
    if(!s||s->size!=sizeof(mod_pad_state))return -1;
    const uint32_t size=s->size;*s={};s->size=size;
    if(!padShared.enabled.load(std::memory_order_acquire))return 0;
    ++padShared.calls;
    // The game's input thread must never wait: skip this poll when a script is being swapped in.
    std::unique_lock<std::mutex> l(padShared.lock,std::try_to_lock);if(!l)return 0;
    mod::pad::Gamepad g;if(!padShared.player.sample(mod::nowMs(),g))return 0;
    s->buttons=g.buttons;s->left_trigger=g.leftTrigger;s->right_trigger=g.rightTrigger;s->lx=g.lx;s->ly=g.ly;s->rx=g.rx;s->ry=g.ry;
    ++padShared.active;return 1;
}
int mod_init(const char* path) {
    std::lock_guard<std::mutex> lock(lifecycle);
    try {if(runtime) throw std::runtime_error("mod already initialized");if(!path) throw std::runtime_error("null config path");auto r=std::make_unique<Runtime>();r->init(std::filesystem::path(path));runtime=r.release();error.clear();return 1;}
    catch(const std::exception& e) {error=e.what();std::cerr<<"mod_init FAILED: "<<error<<'\n';return 0;}
}
int mod_tick(double dt) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if(!runtime || !std::isfinite(dt)||dt<0||dt>1) {error="invalid tick or uninitialized mod";return 0;}
    auto& r=*runtime;
    if(r.config.selfTick||r.config.gameThread) {   // the mod ticks itself; an external tick is a validated no-op, never a second tick source
        std::lock_guard<std::mutex> guard(r.mutex);
        if(r.failed) {error=r.failure;return 0;}
        return 1;
    }
    std::string err;
    if(!r.tickOnce(err)) {error=err;return 0;}
    return 1;
}
int mod_game_thread_tick(const mod_game_thread_context* context) {
    // A hook must never wait for bootstrap or shutdown to release lifecycle.
    std::unique_lock<std::mutex> lock(lifecycle,std::try_to_lock);if(!lock)return 0;
    if(!runtime||!context)return -1;
    auto& r=*runtime;
    if(!r.config.gameThread||!r.dispatchVerified)return -1;
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
    // Independently attest the actual stack; C ABI fields alone are not proof.
    void* raw[32]{};auto count=CaptureStackBackTrace(0,32,raw,nullptr);uint64_t frames[32]{};
    for(unsigned i=0;i<count;++i)frames[i]=reinterpret_cast<uint64_t>(raw[i]);
    uint64_t size=0;for(auto section:r.sampler->sections)size=std::max(size,section.address-r.sampler->base+section.size);
    if(!mod::dispatch::frameStack(frames,count,r.sampler->base,size)){++r.gameRejected;return 0;}
#endif
    if(!r.gameGate.accept(*context,currentThread())){++r.gameRejected;return 0;}
    std::string err;
    if(!r.tickOnce(err,true)) {
        std::lock_guard<std::mutex> guard(r.mutex);r.failed=true;r.failure=err;r.running=false;error=err;
        try{r.note(err.c_str());}catch(...){}return -1;
    }
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
        o<<",\"game_thread\":{\"enabled\":"<<(r.config.gameThread?"true":"false")<<",\"verified\":"<<(r.dispatchVerified?"true":"false")<<",\"thread_id\":"<<r.gameGate.thread()<<",\"rejected\":"<<r.gameRejected<<"},\"puppet\":{\"enabled\":"<<(r.puppet?"true":"false")<<",\"stage\":\""<<(r.puppet?r.puppet->stage():"disabled")<<"\",\"actor\":"<<(r.puppet?r.puppet->actor():0)<<"}";
        o<<",\"pad\":{\"enabled\":"<<(r.config.virtualPad?"true":"false")<<",\"running\":"<<(r.padRunning?"true":"false")<<",\"calls\":"<<padShared.calls.load()<<",\"active_calls\":"<<padShared.active.load()<<"}";
        o<<",\"event\":"<<(r.inEvent?"true":"false")<<",\"cargo\":{\"sent\":"<<r.cargoSent<<",\"created\":"<<r.cargoMade<<",\"taken\":"<<r.cargoTaken<<",\"removed\":"<<r.cargoRemoved<<",\"shared\":"<<r.cargoSync.shared()<<",\"mirrored\":"<<r.cargoSync.mirrored()<<",\"structures_sent\":"<<r.structSent<<",\"structures_created\":"<<r.structMade<<",\"pending\":"<<(r.core?r.core->cargoPending():0)<<"}";
        o<<",\"double\":{\"stage\":\""<<(r.twin?r.twin->name():"disabled")<<"\",\"mode\":\""<<(r.twinMode==Runtime::TwinMode::Echo?"echo":r.twinMode==Runtime::TwinMode::Lead?"lead":r.twinMode==Runtime::TwinMode::Peer?"peer":"none")<<"\",\"entity\":"<<(r.twin?r.twin->entity():0)<<"}";
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
