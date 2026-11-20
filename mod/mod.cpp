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
#include "join.h"
#include "puppet.h"
#include "double.h"
#include "controls.h"
#include "wheels.h"
#include "carts.h"
#include "body.h"
#include "vehicle.h"
#include "ride.h"
#include "cargo_stack.h"
#include "likes.h"
#include "signals.h"
#include "online.h"
#include "vitals.h"
#include "bts.h"
#include "roads.h"
#include "carry_place.h"
#include "passenger.h"
#include "voidout.h"
#include "enemies.h"
#include "enemy_poses.h"
#include "mule_goal.h"
#include "orders.h"
#include "orderload.h"
#include "story.h"
#include "rewards.h"
#include "guard_win.h"
#include "link.h"
#include "bugmark.h"
#include "../common/diag_log.h"
#include "nametag.h"
#include "notice.h"
#include "help.h"
#include "dialog.h"
#include "debugmenu.h"
#include "serverinfo.h"
#include "chat.h"
#include "mapicons.h"
#include "hudmarkers.h"
#include "grouppings.h"
#include "reptrace.h"
#include "repguard.h"
#include "serverworld.h"
#include "livebuild.h"
#include "groupcargo.h"
#include "othervehicles.h"
#include "groupevents.h"
#include "grouporders.h"
#include "roadshare.h"
#include "freebuild.h"
#include "strand_notices.h"
#include "received.h"
#include "offer_strand.h"
#include "weather.h"
#include "groupweather.h"
#include "groupreplay.h"
#include "replayhooks.h"
#include "environment.h"
#include "auto_continue.h"
#include "pad.h"
#include "ui.h"
#include "cargo.h"
#include "marker.h"
#include "carry_visual.h"
#include "gear.h"
#include "outfit.h"
#include "structures.h"
#include "others.h"
#include "crowd.h"
#include "fights.h"
#include "route.h"
#include "testmode.h"
#include "../core/relay.h"
#ifdef MOD_FAKE_NETWORK
#include "fake_memory.h"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <deque>
#include <optional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <cctype>
#include <chrono>
#include <mutex>
#include <random>
#include <sstream>
#include <future>
#include <thread>
// A one-off job on its own thread: the signature checks behind each native API read the whole code section (about
// 200 ms each, live 2026-11-09), which stalled the game thread when a feature first ran. They only read memory, so they
// run beside the game; the feature starts on the tick after its check is done.
template<class T> struct Background {
    std::future<T> job;bool started=false;
    bool ready(std::function<T()> make){
        if(!started){started=true;job=std::async(std::launch::async,std::move(make));}
        return job.valid()&&job.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
};
#include <vector>

// Every CARGO event type has one meaning: two features on one type swallow each other's events (cart cases once took
// the voidout's 17; the bt regression caught it).
namespace wiretypes {
constexpr uint8_t all[]={2,3,4,5,mod::carry::WireType,mod::gear::GearWireType,mod::outfit::WireType,mod::likes::WireType,mod::nametag::WireType,
    mod::enemies::WireType,mod::orders::WireType,mod::bts::WireType,mod::roads::WireType,15,mod::carryplace::WireType,mod::voidout::WireType,
    mod::carryplace::CartWireType,mod::orderload::WireType,mod::signals::EventWireType,mod::signals::VitalsWireType,mod::story::WireType,mod::rewards::WireType,mod::bugmark::WireType};
constexpr bool distinct(){for(size_t i=0;i<sizeof all;++i)for(size_t j=i+1;j<sizeof all;++j)if(all[i]==all[j])return false;return true;}
static_assert(distinct(),"two CARGO event features share a wire type");
// ... and the core delivers it: a type above its limit is refused and the ordered stream stops at that event.
constexpr bool delivered(){for(auto t:all)if(t<1||t>ds::CargoTypeMax)return false;return true;}
static_assert(delivered(),"a CARGO event type is above the core's CargoTypeMax");
}
static_assert(mod::OthersMax==mod::others::MaxOthers,"config `others` and others.h agree on the most players");
namespace {
const diag::Host* diagnosticHost=nullptr;
std::string error;
std::mutex lifecycle;
void checksum(uint8_t* b,size_t n) {
    uint32_t h=2166136261u;for(size_t i=0;i<n;++i) {h^=i>=120&&i<124?0:b[i];h*=16777619u;}
    for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));
}
void checksum(ds::Bytes& b) {checksum(b.data(),b.size());}
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
    // The kernel's pool directly: std::random_device's default source returned 0 now and then under WSL (test builds).
    for(int attempt=0;attempt<4&&!n;++attempt){std::random_device r("/dev/urandom");n=(uint64_t(r())<<32)^r();}
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
// One of the mod's own keys (like, ping): held by the player (focused window only), or by a running pad script, so an
// unattended test presses it like any other scripted key.
std::atomic<bool> chatTyping{false};   // text is being typed: letters are not the mod's keys (chat.h)
bool modKey(unsigned vk) {
#ifndef MUTANT_CHAT_KEYS
    if(chatTyping.load(std::memory_order_relaxed))return false;
#endif
    if(mod::marker::keyDown(vk))return true;
    if(!vk||!padShared.enabled.load(std::memory_order_acquire))return false;
    std::lock_guard<std::mutex> l(padShared.lock);mod::pad::Gamepad g;mod::pad::Keys k;
    if(!padShared.player.sample(mod::nowMs(),g,&k))return false;
    for(size_t i=0;i<k.count;++i)if(k.vk[i]==vk)return true;
    return false;
}
struct Cache final:ds::StateProvider {
    // Game-thread progress discovery precedes the first game-provider handshake.
    ds::Progress story{};bool progressReady=true;
    ds::Progress readProgress() const override {return story;}
    bool readyForSession() const override {return progressReady;}
    uint64_t sampledAt=0;
    uint64_t localSampleTime(uint64_t) const override {return sampledAt;}
    ds::State local; ds::Render remote;bool available=false, gated=true, stickyTeleport=false;
    bool readLocal(ds::State& s) override {s=local;return available;}
    bool isInCutsceneOrLoading() const override {return gated;}
    void applyRemote(const ds::Render& r) override {remote=r;}
};
struct Runtime final:ds::Transport {
    mod::Config config;
    diag::Session diagnostic;mod::bugmark::Button bugButton;uint64_t flightAt=0;
    // Session progress and join-at-host: a decision and a log, plus one native move only with join_move native.
    mod::join::Decision joinDecision;uint64_t nextProgressRead=0,handshakeNoted=0;bool progressRefusalNoted=false;std::string progressReason;uint64_t firstProgressRead=0;
    std::unique_ptr<mod::join::Mover> joinMover;   // join_move native only (join.h)
    void noteHandshake() {
        if(!core->connected()||handshakeNoted==core->connectionSerial())return;
        handshakeNoted=core->connectionSerial();progressRefusalNoted=false;
        auto value=[](uint32_t v){return v==ds::UnknownProgress?std::string("unknown"):std::to_string(v);};
        auto a=core->localProgress(),b=core->peerProgress();
        std::ostringstream line;line<<"HANDSHAKE progress="<<ds::progressReason(core->progressStatus())
            <<" policy="<<config.progressMismatch<<" local_episode="<<value(a.episode)<<" peer_episode="<<value(b.episode)
            <<" local_progress="<<value(a.id)<<" peer_progress="<<value(b.id)<<" wire_build="<<std::hex<<std::setfill('0');
        for(auto byte:mod::WireBuild)line<<std::setw(2)<<unsigned(byte);
        line<<"; "<<(core->progressStatus()==ds::ProgressStatus::Mismatch?"WARNING presence allowed with progress-mismatch flag":"presence allowed")<<"\n";
        note(line.str().c_str());
    }
    // End session progress block.
    std::unique_ptr<mod::discovery::Worker> discovery;
    mod::TimerResolution timerResolution;
    mod::Histogram networkTick, providerTick, sendInterval, sampleAge, receiveApply, deliveryLateness;
    uint64_t lastNetwork=0,lastProvider=0,lastPresence=0,unappliedReceive=0;
    uint64_t pingSent=0,pongReceived=0,firstPing=0,lastPing=0,firstPong=0,lastPong=0;
    uint64_t observedClockStep=0;double measuredRtt=-1;

    SOCKET socket=INVALID_SOCKET;sockaddr_in peer{};
    std::timed_mutex mutex;std::atomic<bool> running{false};std::thread worker;
    // Where the game-thread tick is, for a stall report: a native game call that faults is resumed by the game's own
    // exception handling above the mod, so the mod's lock stays held and the mod stops (live 2026-11-07).
    // Time spent in each stage of the tick; once a minute, the stages whose slowest tick took over 1 ms are logged.
    struct StageClock {
        struct Stat{uint64_t n=0,total=0,max=0,reads=0;};
        const char* name=nullptr;uint64_t at=0,since=0,readsAt=0;std::map<std::string,Stat> stats;const mod::discovery::Sampler* sampler=nullptr;
        void enter(const char* next,uint64_t us){uint64_t r=sampler?sampler->reads:0;
            if(name&&us>=at){auto& t=stats[name];auto d=us-at;++t.n;t.total+=d;t.max=std::max(t.max,d);t.reads+=r-readsAt;}
            name=next;at=us;readsAt=r;if(!since)since=us;}
        std::string report(uint64_t us){
            if(!since||us<since||us-since<60000000)return {};
            std::ostringstream o;bool any=false;o<<std::fixed<<std::setprecision(2)<<"TICK slow stages over 60 s:";
            for(auto& [k,t]:stats)if(t.max>1000){any=true;o<<' '<<k<<" max "<<t.max/1000.<<" ms avg "<<double(t.total)/t.n/1000.<<" ms "<<double(t.reads)/t.n<<" reads;";}
            stats.clear();since=us;return any?o.str()+"\n":std::string();}
    } stages;
    // The local porter's anchor and whether the game uses its double, read once per tick (every stage used to read
    // them again: hundreds of reads a tick; live 2026-11-09). tickId advances at the start of each tick.
    uint64_t tickId=1,anchorTick=0,busyTick=0,busyEntity=0;mod::puppet::Anchor anchorMemo;bool busyMemo=false;
    mod::puppet::Anchor anchorNow(){if(anchorTick!=tickId){anchorMemo=mod::puppet::readLocalAnchor(*sampler);anchorTick=tickId;}return anchorMemo;}
    bool doubleBusy(uint64_t entity){if(!twin)return false;if(busyTick!=tickId||busyEntity!=entity){busyMemo=twin->gameUsesDouble(entity);busyTick=tickId;busyEntity=entity;}return busyMemo;}
    std::atomic<const char*> stage{"idle"};bool stallNoted=false;uint64_t ownedBody=0;std::function<void(uint64_t,bool)> parkSleep,parkVisible;
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
    enum class TwinMode {None,Echo,Lead,Peer,Own};
    std::filesystem::path twinCommand;std::unique_ptr<mod::puppet::Double> twin;TwinMode twinMode=TwinMode::None;
    // Own-body mode: the partner is drawn with a Sam the mod made (OwnBody), driven by a second Double backend.
    std::unique_ptr<mod::puppet::OwnBody> own;std::unique_ptr<mod::puppet::Double> ownTwin;uint64_t ownEntity=0;unsigned dressMask=mod::puppet::DressArtPartsInPlace;
    bool partnerMode() const {return twinMode==TwinMode::Peer||twinMode==TwinMode::Own;}
    std::filesystem::path cargoCommand;std::function<void(uint32_t,const mod::puppet::Transform&)> cargoCreate;
    std::filesystem::path bodyCommand;std::unique_ptr<mod::body::Probe> body;   // own-body experiment (body.h)
    // More Sams (others.h): bodies made and driven exactly like the partner's own body. bodyParts gives the game's calls
    // for one more (native builds only). crowdBodies is the lab command "crowd N" in puppet.double: N bodies standing in
    // rings around the partner (or around this porter while no partner is shown), all with the partner's animation; it
    // answers how many Sams one game holds and nothing of it is on the wire. otherBodies are the other players' bodies.
    std::function<mod::others::BodyParts()> bodyParts;
    std::unique_ptr<mod::others::Bodies> crowdBodies,otherBodies;size_t crowdWanted=0;
    uint64_t crowdNotedAt=0,crowdWorstUs=0,crowdSumUs=0,crowdTicks=0,crowdTickAt=0,crowdGapWorstMs=0;
    // Other players (others.h), presence only: a body each, besides the one partner. Config `others` is the most there
    // may be (0 = off). By the relay's pairs mode (`pairs`: a core session each) or its broadcast mode (`crowd`, crowd.h:
    // one client for everybody); `others` is whichever of the two runs, and everything past the transport uses it.
    std::unique_ptr<mod::others::Players> others;mod::others::Others* pairs=nullptr;mod::crowd::Crowd* crowd=nullptr;mod::others::Plan othersPlan;uint64_t othersNotedAt=0;std::array<size_t,4> othersNoted{{~size_t(0),0,0,0}};
    std::vector<mod::others::Shown> othersShown;   // the other players as of this tick's tickOthers (their labels use it)
    mod::controls::Gather othersGather;std::atomic<bool> othersControls{false};   // this porter's controls for them (under controlsLock)
    // ---- SEAM: the transport for other players. ---------------------------------------------------------------------
    // Pairs mode: whatever carries the datagrams between players calls these five, always under `mutex`: the relay
    // (relayStart, relayPoll below, from the network thread's loop()), and in the fake world the test (mod_test_others_*).
    // Broadcast mode has one client (crowdStart): relayPoll hands it the relay's datagrams, and in the fake world the test
    // carries them (mod_test_relay_*).
    //   othersStart(self, link)            this player's id on the link is known; link.send(to, data, size) sends one datagram
    //   othersEntered(id, now), othersLeft(id, now)  who is near
    //   othersReceived(from, data, size, now)        one datagram from that player, as it was sent
    //   othersStop()                       the link is gone: everybody leaves
    void othersStart(uint64_t self,mod::others::Link link){
        if(!config.others||!self)return;
        mod::others::Settings s;s.self=self;s.session=config.session;s.cap=config.others;s.build=mod::WireBuild;s.nickname=config.nickname;
        s.gear=config.othersGear;s.carry=config.othersCarry!=0;   // what is only shown; everything else of theirs is dropped
        auto made=std::make_unique<mod::others::Others>(s,std::move(link),[]{return nonce();},[this](const std::string& row){note(row.c_str());});
        pairs=made.get();others=std::move(made);
        note(("OTHERS ready: up to "+std::to_string(config.others)+" other players"+(otherBodies?"\n":" (no bodies in this build)\n")).c_str());
    }
    // Broadcast mode: send(data, size) is one datagram to the relay.
    void crowdStart(uint64_t self,ds::broadcast::Client::Send send){
        if(!config.others||!self)return;
        mod::others::Settings s;s.self=self;s.session=config.session;s.cap=config.others;s.build=mod::WireBuild;s.nickname=config.nickname;
        s.gear=config.othersGear;s.carry=config.othersCarry!=0;
        ds::broadcast::ClientConfig c;c.token=config.relayKey;c.nickname=ds::relay::nicknameText(config.nickname)?config.nickname:std::string();
        c.show=config.showOthers=="nobody"?ds::broadcast::Show::Nobody:config.showOthers=="friends"?ds::broadcast::Show::Friends:ds::broadcast::Show::Everyone;
        auto made=std::make_unique<mod::crowd::Crowd>(s,c,std::move(send),[this](const std::string& row){note(row.c_str());});
        crowd=made.get();others=std::move(made);
        note(("OTHERS ready (broadcast): up to "+std::to_string(config.others)+" other players"+(otherBodies?"\n":" (no bodies in this build)\n")).c_str());
    }
    void othersStop(){if(!others)return;others->clear(mod::nowMs());pairs=nullptr;crowd=nullptr;others.reset();fights.reset();note("OTHERS stopped\n");}
    // `now` is the network loop's time for this pass, the one its tick is given. A later reading here put a session's
    // last receive after the tick's time, and the core's timeout (then an unsigned difference) fired at once (live
    // 2026-11-16: sessions dropped and reconnected all the time). The core no longer does that (CORE.md, "times out
    // of order"); one clock per pass stays the rule, so that lateness and delay are measured on one reading.
    void othersEntered(uint64_t id,uint64_t now){if(pairs)pairs->entered(id,now);}
    void othersLeft(uint64_t id,uint64_t now){if(pairs)pairs->left(id,now);}
    ds::Error othersLastError=ds::Error::None;
    void othersReceived(uint64_t from,const uint8_t* data,size_t size,uint64_t now){
        if(pairs){auto e=pairs->received(from,data,size,now);if(e!=ds::Error::None)othersLastError=e;}
#ifdef MOD_MUTANT_OTHERS_TO_PARTNER
        if(size==ds::WireSize&&pending.size()<256)pending.push_back({mod::nowUs(),now,std::vector<uint8_t>(data,data+size),size});   // mutant: one route for everybody
#endif
    }
    // The relay (core/relay.h; config relay, relay_key, player_id): a second UDP socket connected to the players' own
    // server, which says who is near and carries the datagrams of those pairs. Polled by the network thread under `mutex`.
#ifndef MOD_FAKE_NETWORK
    SOCKET relaySocket=INVALID_SOCKET;
#endif
    std::unique_ptr<ds::relay::Client> relayClient;bool relayOnline=false;uint64_t relayIn=0,relayOut=0,relayNotedAt=0;
#ifdef MOD_FAKE_NETWORK
    // The fake world has no relay socket. Pairs mode: the test is the relay; it reads what the sessions send from here
    // and hands the roster and the datagrams over through the seam (mod_test_others_*). Broadcast mode: the test is the
    // socket; it takes the client's datagrams for the relay from relayOutgoing and hands over the relay's
    // (mod_test_relay_*).
    struct OthersOut {uint64_t to;std::vector<uint8_t> bytes;};std::deque<OthersOut> othersOutgoing;std::deque<std::vector<uint8_t>> relayOutgoing;
#endif
    bool broadcastMode() const {
#ifndef MOD_MUTANT_RELAY_MODE
        return config.relayMode=="broadcast";
#else
        return false;
#endif
    }
    // Other players need a relay: `others` alone starts no session and sends nothing.
    bool relayWanted() const {
#ifndef MOD_MUTANT_OTHERS_NO_RELAY
        return !config.relay.empty()&&config.others;
#else
        return config.others!=0;
#endif
    }
    void relayStart(){
        if(!relayWanted())return;
#ifdef MOD_FAKE_NETWORK
        if(broadcastMode()){crowdStart(config.playerId?config.playerId:1,[this](const uint8_t* d,size_t n){++relayOut;if(relayOutgoing.size()<4096)relayOutgoing.emplace_back(d,d+n);});return;}
        mod::others::Link link;link.send=[this](uint64_t to2,const uint8_t* d,size_t n){++relayOut;if(othersOutgoing.size()<4096)othersOutgoing.push_back({to2,std::vector<uint8_t>(d,d+n)});};
        othersStart(config.playerId?config.playerId:1,std::move(link));
#else
        relaySocket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(relaySocket==INVALID_SOCKET)throw std::runtime_error("relay UDP socket failed");
        sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=htons(config.relayPort);to.sin_addr.s_addr=htonl(config.relayAddress);
        // Connected: the socket takes datagrams from the relay's address and port only, and is bound to the route to it.
        if(connect(relaySocket,reinterpret_cast<sockaddr*>(&to),sizeof(to)))throw std::runtime_error("relay address unreachable");
#ifdef _WIN32
        u_long nonblock=1;if(ioctlsocket(relaySocket,FIONBIO,&nonblock))throw std::runtime_error("nonblocking relay UDP failed");
#else
        if(fcntl(relaySocket,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("nonblocking relay UDP failed");
#endif
        if(broadcastMode()){
            crowdStart(config.playerId,[this](const uint8_t* d,size_t n){++relayOut;::send(relaySocket,reinterpret_cast<const char*>(d),int(n),0);});
            note(("RELAY "+config.relay+" as player "+std::to_string(config.playerId)+" (broadcast)\n").c_str());return;}
        ds::relay::ClientConfig rc;rc.id=config.playerId;rc.token=config.relayKey;rc.nickname=ds::relay::nicknameText(config.nickname)?config.nickname:std::string();
        relayClient=std::make_unique<ds::relay::Client>(rc,[this](const uint8_t* d,size_t n){::send(relaySocket,reinterpret_cast<const char*>(d),int(n),0);});
        mod::others::Link link;link.send=[this](uint64_t to2,const uint8_t* d,size_t n){if(relayClient&&relayClient->sendTo(to2,d,n))++relayOut;};
        othersStart(config.playerId,std::move(link));
        note(("RELAY "+config.relay+" as player "+std::to_string(config.playerId)+"\n").c_str());
#endif
    }
    // Broadcast mode: the relay's datagrams for the client (the client itself ticks in crowd->tick, after this), and the
    // notes. Where this porter is goes to the server from Crowd::local.
    void crowdPoll(uint64_t now){
#ifndef MOD_FAKE_NETWORK
        for(unsigned budget=0;budget<256;++budget){
            char raw[2048];int n=recv(relaySocket,raw,sizeof(raw),0);if(n<=0)break;
            ++relayIn;crowd->receive(reinterpret_cast<const uint8_t*>(raw),size_t(n),now);
        }
#endif
        auto& client=crowd->client();
        if(client.online()!=relayOnline){relayOnline=client.online();
            note(relayOnline?"RELAY online (broadcast)\n":client.refusal()==ds::broadcast::Refusal::Build?"RELAY refused this player: the server wants another build\n"
                :client.refusal()!=ds::broadcast::Refusal::None?"RELAY refused this player\n":"RELAY offline: everybody near has left\n");}
        if(now-relayNotedAt>=10000){relayNotedAt=now;auto& k=client.counters();std::ostringstream o;
            o<<"RELAY broadcast "<<(relayOnline?"online":"offline")<<" near "<<client.nearby().size()<<" taken "<<crowd->size()<<" in "<<k.received<<" ("<<k.receivedBytes<<" bytes) out "<<k.sent<<" ("<<k.sentBytes
             <<" bytes) frames "<<k.frames<<" accepted "<<k.accepted<<" refused "<<k.refused<<" unknown "<<k.unknown<<" malformed "<<crowd->counters().malformed<<" fetches "<<k.fetches
             <<" clock offset "<<std::llround(client.offsetMs())<<" ms rtt "<<std::llround(client.rttMs())<<" ms";   // llround: a 32-bit long (Windows) overflowed to its minimum
            if(client.welcome().otherBuild)o<<" near with another build "<<client.welcome().otherBuild;
            o<<"\n";note(o.str().c_str());}
    }
    void relayPoll(uint64_t now){
        if(crowd){crowdPoll(now);return;}
#ifndef MOD_FAKE_NETWORK
        if(!relayClient)return;
        // Where this porter is: the relay decides from it who is near. No place while there is no pose (loading, cutscene).
        if(cache.available&&!cache.gated)relayClient->setPosition({{cache.local.position[0],cache.local.position[1],cache.local.position[2]}},gameArea);
        else relayClient->clearPosition();
        relayClient->tick(now);
        for(unsigned budget=0;budget<256;++budget){
            char raw[2048];int n=recv(relaySocket,raw,sizeof(raw),0);if(n<=0)break;
            uint64_t from=0;const uint8_t* payload=nullptr;size_t size=0;
            if(relayClient->receive(reinterpret_cast<const uint8_t*>(raw),size_t(n),now,from,payload,size)){++relayIn;othersReceived(from,payload,size,now);}
        }
        for(auto& e:relayClient->takeEvents()){if(e.enter)othersEntered(e.id,now);else othersLeft(e.id,now);}
        if(relayClient->online()!=relayOnline){relayOnline=relayClient->online();
            note(relayOnline?"RELAY online\n":relayClient->refusal()!=ds::relay::Refusal::None?"RELAY refused this player\n":"RELAY offline: everybody near has left\n");}
        if(now-relayNotedAt>=10000){relayNotedAt=now;std::ostringstream o;o<<"RELAY "<<(relayOnline?"online":"offline")<<" near "<<relayClient->nearby().size()<<" in "<<relayIn<<" out "<<relayOut;
            if(others){auto& k=others->counters();o<<" accepted "<<k.accepted<<" refused "<<k.refused<<" unknown "<<k.unknown<<" malformed "<<k.malformed<<" other build "<<k.foreignBuild<<" other kind "<<k.kind
                <<" last refusal \""<<ds::reason(othersLastError)<<"\"";}
            o<<"\n";note(o.str().c_str());}
#else
        (void)now;
#endif
    }
    void relayStop(){
        if(crowd){try{crowd->bye();}catch(...){}}
#ifndef MOD_FAKE_NETWORK
        if(relayClient){try{relayClient->bye();}catch(...){}relayClient.reset();}
        if(relaySocket!=INVALID_SOCKET){closesocket(relaySocket);relaySocket=INVALID_SOCKET;}
#endif
    }
    // The other players' bodies (game thread, after the partner's): each goes to its own player's interpolated pose with
    // that player's animation sample; hidden while the player is stale, away or in a private room and during a local
    // game event; removed when the player has left.
    // Cheaper bodies (lab, puppet.cargo `perfrate R` 0..6 = 10/15/30/60/120/144/240 Hz and 9 = the game's own, `perfshadow
    // 0|1` and 9 = the game's own, `perfhide M` metres and 0 = none, `perffar M` metres: rate and shadow only beyond it,
    // 0 = every body): the game's per-entity update rate (0x235f5a0, entity+0x204) and shadow casting (0x234cfa0,
    // Entity_ExportedSetShadowCastingEnabled, children included), and bodies beyond M hidden and asleep (Want without a goal).
    // `perfminlod N` (99: the game's own): the body's lowest detail level, as ForceMinimumLodComponent sets it
    // (0x237c930(rep, N), rep = entity+0x60, rep+0x80 the entity); `perfpbd P` (percent, 999: the game's own): the
    // distance multiplier of its cloth simulation (SkinnedModel_ExportedSetPBDMaxDistanceMultiplier 0x239f5a0, the
    // skinned model at entity+0xb8, the value at +0x384).
    uint32_t perfMinLod=99,perfPbd=999;
    uint8_t perfRate=9,perfShadow=9;double perfHideMetres=0,perfFarMetres=0;
    struct PerfApplied{uint8_t rate=9,shadow=9,ownRate=0;uint32_t minLod=0,pbd=999;float ownPbd=-1;};std::map<uint64_t,PerfApplied> perfApplied;size_t perfHidden=0;uint64_t perfNotedAt=0;
    bool perfChecked=false,perfUsable=false;
    bool perfCallsUsable(){
        if(perfChecked)return perfUsable;perfChecked=true;
#ifdef _WIN32
        static const uint8_t rate[13]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb6,0xfa};
        static const uint8_t shadow[9]={0x48,0x85,0xc9,0x0f,0x84,0x91,0x00,0x00,0x00};
        static const uint8_t minLod[14]={0x3b,0x91,0xa0,0x01,0,0,0x74,0x10,0x89,0x91,0xa0,0x01,0,0};
        static const uint8_t pbd[9]={0x48,0x85,0xc9,0x74,0x10,0xc5,0xf8,0x57,0xc0};
        uint8_t a[13]{},b[9]{},c[14]{},d[9]{};
        perfUsable=sampler&&sampler->read(sampler->base+0x235f5a0,a,13)&&sampler->read(sampler->base+0x234cfa0,b,9)&&!std::memcmp(a,rate,13)&&!std::memcmp(b,shadow,9)
            &&sampler->read(sampler->base+0x237c930,c,14)&&sampler->read(sampler->base+0x239f5a0,d,9)&&!std::memcmp(c,minLod,14)&&!std::memcmp(d,pbd,9);
#endif
        if(!perfUsable)note("PERF body rate and shadow calls unavailable: the functions differ\n");
        return perfUsable;}
    void perfBodies(const mod::puppet::Anchor& anchor,uint64_t now){
        const bool any=perfRate!=9||perfShadow!=9||perfMinLod!=99||perfPbd!=999||!perfApplied.empty();if(!any||!otherBodies||!perfCallsUsable())return;
#ifdef _WIN32
        std::optional<std::array<double,3>> here;if(anchor.pose.available)here=mod::puppet::transform(anchor.pose).position;
        std::set<uint64_t> seen;size_t slowed=0;
        for(auto& w:othersPlan.wants){
            const uint64_t e=otherBodies->entity(w.key);if(!e)continue;seen.insert(e);if(!w.goal)continue;   // hidden: kept as it is, own rate remembered
            bool distant=perfFarMetres<=0;if(!distant&&here){double dx=w.goal->position[0]-(*here)[0],dy=w.goal->position[1]-(*here)[1],dz=w.goal->position[2]-(*here)[2];distant=dx*dx+dy*dy+dz*dz>perfFarMetres*perfFarMetres;}
            auto [it,fresh]=perfApplied.try_emplace(e);auto& a=it->second;
            if(fresh){uint8_t r=0;if(!sampler->read(e+0x204,&r,1)||r>6){perfApplied.erase(it);continue;}a.ownRate=r;}
            const uint8_t rate=distant&&perfRate<=6?perfRate:a.ownRate,shadow=distant&&perfShadow<=1?perfShadow:1;
            if(rate!=a.rate){reinterpret_cast<void(*)(uint64_t,uint8_t)>(sampler->base+0x235f5a0)(e,rate);a.rate=rate;}
            if(shadow!=a.shadow&&!(fresh&&shadow==1)){reinterpret_cast<void(*)(uint64_t,bool,bool)>(sampler->base+0x234cfa0)(e,shadow!=0,true);}
            a.shadow=shadow;if(rate!=a.ownRate)++slowed;
            {const uint32_t want=distant&&perfMinLod<99?perfMinLod:0;uint64_t r=0,back=0;
             if(want!=a.minLod&&sampler->read(e+0x60,&r,8)&&r&&sampler->read(r+0x80,&back,8)&&back==e){reinterpret_cast<void(*)(uint64_t,uint32_t)>(sampler->base+0x237c930)(r,want);a.minLod=want;}}
            {uint64_t m=0;if(sampler->read(e+0xb8,&m,8)&&m){
                if(a.ownPbd<0){float v=0;if(sampler->read(m+0x384,&v,4)&&std::isfinite(v)&&v>=0&&v<100)a.ownPbd=v;}
                const uint32_t want=distant&&perfPbd<999?perfPbd:999;
                if(want!=a.pbd&&a.ownPbd>=0){const float v=want<999?float(want)/100.0f:a.ownPbd;reinterpret_cast<void(*)(uint64_t,float)>(sampler->base+0x239f5a0)(m,v);a.pbd=want;}}}}
        for(auto it=perfApplied.begin();it!=perfApplied.end();)if(seen.count(it->first))++it;else it=perfApplied.erase(it);
        if(now-perfNotedAt>=10000){perfNotedAt=now;std::ostringstream o;o<<"PERF bodies "<<seen.size()<<" slowed "<<slowed<<" rate "<<int(perfRate)<<" shadow "<<int(perfShadow)
            <<" far "<<perfFarMetres<<" m hidden beyond "<<perfHideMetres<<" m: "<<perfHidden<<" min lod "<<perfMinLod<<" cloth "<<perfPbd<<"%";
            if(!perfApplied.empty())o<<" (first body's cloth multiplier was "<<perfApplied.begin()->second.ownPbd<<")";o<<"\n";note(o.str().c_str());}
#else
        (void)anchor;(void)now;
#endif
    }
    // Lab: who writes into other entities' rep queues from a body's update (reptrace.h; `reptrace 1`).
    bool reptraceWanted=false;uint64_t reptraceNotedAt=0;std::map<uint64_t,uint64_t> reptraceSeen;
    void tickRepTrace(const mod::puppet::Anchor& anchor,uint64_t now){
        if(!reptraceWanted||!sampler)return;
        if(!mod::reptrace::installed()){try{mod::reptrace::install(*sampler);note("REPTRACE on\n");}catch(const std::exception& e){note((std::string("REPTRACE unavailable: ")+e.what()+"\n").c_str());reptraceWanted=false;return;}}
        std::vector<uint64_t> es;if(otherBodies)for(auto& w:othersPlan.wants)if(auto e=otherBodies->entity(w.key))es.push_back(e);
        if(ownEntity)es.push_back(ownEntity);   // the partner's body
        uint64_t r=0;if(anchor.entity)sampler->read(anchor.entity+0x60,&r,8);
        mod::reptrace::watch(es,r);
        if(now<reptraceNotedAt+10000)return;reptraceNotedAt=now;
        auto all=mod::reptrace::stacks();uint64_t sum=0;for(auto& s:all)sum+=s.count;
        note(("REPTRACE body updates seen "+std::to_string(mod::reptrace::bodyUpdateCalls())+", appends during them "+std::to_string(mod::reptrace::appendsFromBodies())+"; appends "+std::to_string(mod::reptrace::appends())+", from bodies into others' queues "+std::to_string(sum)+" ("+std::to_string(all.size())+" stacks, "+std::to_string(es.size())+" bodies)\n").c_str());
        for(auto& s:all){uint64_t key=0;for(auto a:s.at)key=key*31+a;auto& seen=reptraceSeen[key];if(seen==s.count)continue;seen=s.count;
            std::ostringstream o;o<<"REPTRACE stack x"<<s.count<<(s.porter?" into THIS PORTER's queue":" into another entity's queue")<<":";
            for(auto a:s.at)if(a)o<<" 0x"<<std::hex<<a<<std::dec;o<<"\n";note(o.str().c_str());}
    }
    // The mod's bodies never run the inventory display for this porter's cargo (repguard.h): published every tick.
    bool repGuardTried=false;uint64_t repGuardNotedAt=0;
    void tickRepGuard(uint64_t now){
        if(!config.bodyRepGuard||!sampler)return;
        std::vector<uint64_t> es;
        if(otherBodies)for(auto& w:othersPlan.wants)if(auto e=otherBodies->entity(w.key))es.push_back(e);
        if(crowdBodies)for(size_t i=0;i<crowdBodies->size()&&i<64;++i)if(auto e=crowdBodies->entity(i))es.push_back(e);
        if(ownEntity)es.push_back(ownEntity);
        mod::repguard::bodies(es);
        if(!repGuardTried){repGuardTried=true;   // before any body's first update
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
            try{mod::repguard::install(*sampler);note("BODY guard on: bodies never run the inventory display for this porter's cargo\n");}catch(const std::exception& e){note((std::string("BODY guard unavailable: ")+e.what()+"\n").c_str());}
#endif
        }
        if(mod::repguard::installed()&&now>=repGuardNotedAt+60000){repGuardNotedAt=now;note(("BODY guard: "+std::to_string(mod::repguard::skipped())+" cargo display updates skipped on "+std::to_string(es.size())+" bodies\n").c_str());}
    }
    void tickOthers(const mod::puppet::Anchor& anchor,bool event,uint64_t now){
        tickRepTrace(anchor,now);
        othersShown.clear();
        if(!otherBodies||(!others&&!otherBodies->size()))return;
        if(others)othersShown=others->frame(now);
        const auto& shown=othersShown;
        mod::others::plan(shown,!event,config.partnerPoses,config.partnerAnimation,othersPlan);
        // A player who left while the body wears gear copies: the copies' removal is queued now, the body's next tick.
#ifndef MOD_MUTANT_OTHERS_UNDRESSED
        if(gears)for(auto key:mod::others::undress(shown,gears->keys(),othersPlan))gears->remove(key,anchor.entity);
#endif
        perfHidden=0;
        if(perfHideMetres>0&&anchor.pose.available){const auto here=mod::puppet::transform(anchor.pose).position;
            for(auto& w:othersPlan.wants)if(w.goal){double dx=w.goal->position[0]-here[0],dy=w.goal->position[1]-here[1],dz=w.goal->position[2]-here[2];
                if(dx*dx+dy*dy+dz*dz>perfHideMetres*perfHideMetres){w.goal=nullptr;w.sample=nullptr;++perfHidden;}}}
        otherBodies->drive(othersPlan.wants,anchor,now);
        perfBodies(anchor,now);
        tickOtherVehicles(anchor,event,now);
        tickOtherCarts(event,now);
        std::array<size_t,4> state{{shown.size(),others?others->connected():0,otherBodies->made(),otherBodies->driving()}};
        if(state!=othersNoted&&now-othersNotedAt>=2000){othersNoted=state;othersNotedAt=now;std::ostringstream o;
            o<<"OTHERS near "<<state[0]<<" connected "<<state[1]<<" bodies made "<<state[2]<<" shown "<<state[3]<<"\n";note(o.str().c_str());}
    }
    // The porter's visible gear on the partner's own body (gear.h); config partner_gear or puppet.gear "on <stamp>".
    // One mirror per body (gear.h, Mirrors): key 0 is the partner's own body, a player's id another player's body
    // (config others_gear: what that player wears, as that player sent it).
    std::filesystem::path gearCommand;std::unique_ptr<mod::gear::Mirrors> gears;bool gearOn=false;uint64_t gearPorter=0;
#ifdef MOD_FAKE_NETWORK
    // The fake world's gear: this porter has a backpack and a wrist cuff as direct children, each shown or hidden
    // (mod_test_set_gear), and the engine's side is a table of the pieces the mod made.
    struct FakePiece {uint64_t parent=0;std::string helper;bool visible=false;};std::map<uint64_t,FakePiece> fakePieces;uint64_t fakePieceNext=0x40000000,fakeOrphaned=0;
    std::atomic<unsigned> fakeGear{3};
#endif
    // This porter's direct children and theirs, as gear.h reads them.
    std::vector<mod::gear::Node> gearNodes(uint64_t porter) {
#ifdef MOD_FAKE_NETWORK
        std::vector<mod::gear::Node> out;if(!porter)return out;unsigned worn=fakeGear.load();
        auto piece=[&](uint64_t entity,uint64_t vtable,uint64_t mover,const char* helper,bool visible){
            mod::gear::Node n;n.entity=entity;n.vtable=sampler->base+vtable;n.moverVtable=sampler->base+mover;n.helper=helper;n.flags=visible?mod::gear::FlagVisible:0;out.push_back(n);};
        piece(0x7001,mod::gear::BackpackVtable,mod::gear::BackpackMoverVtable,"HLP_Backpack",worn&1);
        piece(0x7002,mod::gear::EntityVtable,mod::gear::HelperMoverVtable,"HLP_Hcuf",worn&2);
        return out;
#else
        return mod::gear::snapshot(*sampler,porter);
#endif
    }
    // Is the body visible, and where: nothing while it cannot be read (no piece is ever made at an unreadable place).
    std::optional<mod::gear::Site> gearSite(uint64_t body) {
        auto pose=mod::puppet::readEntityPose(*sampler,body);if(!(pose.available&&pose.coherent))return std::nullopt;
        mod::gear::Site site;uint64_t flags=0;site.shown=sampler->read(body+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        site.at=mod::puppet::transform(pose);return site;
    }
    void makeGears(mod::gear::Api api) {
        gears=std::make_unique<mod::gear::Mirrors>(std::move(api),[this](uint64_t body){return gearSite(body);},[this]{return gearNodes(gearPorter);},
            sampler->base,[this](const std::string& row){note(row.c_str());});
    }
    // Where the copy of a backpack is (the cases hang on it). The fake world's copies have no place of their own: the body's.
    mod::Pose packPose(uint64_t pack,uint64_t body) {
#ifdef MOD_FAKE_NETWORK
        (void)pack;return mod::puppet::readEntityPose(*sampler,body);
#else
        (void)body;return mod::puppet::readEntityPose(*sampler,pack);
#endif
    }
    uint32_t gearChurnMode=0,gearChurnLeft=0;uint64_t gearChurnAt=0;
    // The partner's own gear (gear.h, CARGO type 7): what they show, and what this porter shows sent to them.
    // The partner's outfit (outfit.h, CARGO type 8): their enabled art parts, and this porter's sent to them.
    std::optional<mod::outfit::Parts> partnerParts;mod::outfit::Sender outfitTx;uint64_t outfitAt=0;
    std::optional<mod::gear::WornSet> partnerWorn;mod::gear::WornReceiver wornRx;mod::gear::WornSender wornTx;uint64_t wornReadAt=0;
    // Read-only vehicle diagnostics. No changes to presence or actor control (vehicle.h).
    std::filesystem::path vehicleCommand;std::unique_ptr<mod::vehicle::Monitor> vehicleMonitor;
    // Optional environment sampling and host time setter. Only the verified callback can call the export.
    std::filesystem::path environmentCommand;std::unique_ptr<mod::environment::Reader> environmentReader;
    std::unique_ptr<mod::environment::Sync> environmentSync;uint64_t environmentReadAt=0,environmentApplyAt=0;bool environmentSend=false;std::string environmentLastRow;
    std::unique_ptr<mod::weather::Share> weatherShare;std::optional<float> hostRain;uint64_t hostRainAt=0;   // guest: shared rain
    std::function<void(uint32_t)> cargoGive,cargoDelete;std::function<void(uint8_t,const mod::puppet::Transform&,bool)> construct;std::function<uint32_t(uint8_t,const mod::puppet::Transform&,const std::vector<uint16_t>&)> constructRequest;std::function<bool(uint64_t,unsigned,bool)> partToggle;
    std::function<void(const std::array<float,4>&,const std::array<float,4>&,const mod::puppet::Transform&,bool,bool,bool)> unconstruct;
    std::function<void()> continueLastSave;mod::AutoContinue autoContinue;bool autoNoted=false,continueAgain=false;
    std::function<bool()> gameEvent;bool inEvent=false;uint64_t eventOverAt=0;std::atomic<bool> fakeEvent{false};
    // Shared cargo (cargo.h): placements read from the game's cargo map go to the partner; theirs are created here.
    mod::cargo::Sync cargoSync;uint64_t cargoReadAt=0,cargoManagerSlot=0,cargoManagerVtable=0,cargoSent=0,cargoMade=0,cargoTaken=0,cargoRemoved=0;
    uint64_t cargoFullAt=0;unsigned cargoLimit=mod::cargo::FirstRecords;   // a full record scan every 4 s, quick scans up to its mark
    bool cargoIdsSet=false;
    // One shared load per order (orderload.h): opt-in share_order_load.
    mod::orderload::Loads orderLoads;std::optional<mod::orderload::Tag> loadTag;std::optional<mod::orderload::Case> loadSaved;
    std::function<bool(const mod::orderload::Case&)> orderCaseCreate;bool orderCaseTried=false;uint64_t loadSent=0,loadMade=0,loadStripped=0;unsigned loadLeftNoted=0;
    void needOrderCaseCreate(){
        if(orderCaseTried||!sampler)return;
        orderCaseTried=true;try{orderCaseCreate=mod::orderload::nativeCreate(*sampler);}catch(const std::exception& e){note((std::string("ORDERLOAD unavailable: ")+e.what()+"\n").c_str());}}
    void tickCargo(uint64_t now) {
        // Watch the world even while the partner is away: what is placed or taken meanwhile is queued by the core and
        // sent when the partner connects (the core keeps undelivered events across reconnects).
        if(!core)return;
        // Shared ids below 2^53 (a wire t-field): 32 random bits above a 20-bit counter.
        if(!cargoIdsSet){cargoSync.setIdBase((nonce()>>32)<<20);cargoIdsSet=true;}
        auto incoming=core->takeCargo();
        bool coop=partnerMode()&&cargoCreate&&cargoManagerSlot;
        for(auto& e:incoming) {
            if(e.type==mod::carry::WireType){if(config.carryVisual)carry.incoming(e,carrySink);continue;}
            if(e.type==mod::carryplace::WireType){                                       // where the partner's cases sit (tickStack)
                if(config.carryVisual)if(auto list=placeRx.feed(e)){placeShown=std::move(*list);note(("CARRY the partner's "+std::to_string(placeShown->size())+" cases have their places\n").c_str());}
                continue;}   // ds3: tickCarry
            if(e.type==mod::carryplace::CartWireType){                                   // the cases on the partner's cart (tickStack)
                if(config.partnerCart)if(auto list=cartPlaceRx.feed(e)){cartPlaceShown=std::move(*list);note(("CART the partner's cart carries "+std::to_string(cartPlaceShown->size())+" cases\n").c_str());}
                continue;}
            if(e.type==3){structIn.push_back(e);continue;}   // a structure (tickStructures)
            if(e.type==mod::enemies::WireType){enemyIn.push_back(e);continue;}
            if(e.type==mod::bts::WireType){btIn.push_back(e);continue;}
            if(e.type==mod::roads::WireType){roadIn.push_back(e);continue;}
            if(e.type==mod::voidout::WireType){voidIn.push_back(e);continue;}            // a voidout crater (tickVoidouts)
            if(e.type==LevelWire){levelIn.push_back(e);continue;}                      // a structure reached a level (tickStructures)             // road news (tickRoads)                // BT news (tickBts)   // a MULE went down (tickEnemies)
            if(e.type==mod::orders::WireType){orderIn.push_back(e);continue;}    // an order changed (tickOrders)
            if(e.type==mod::orderload::WireType){                                // the next placement is an order's case
                loadTag.reset();
                if(config.shareOrderLoad){loadTag=mod::orderload::decode({e.item,e.ref,e.position});if(!loadTag)note("ORDERLOAD a case's tag was refused (not a standard order's case)\n");}
                continue;}
            if(e.type==mod::rewards::WireType){rewardIn.push_back(e);continue;}  // a facility's connection total (tickRewards)
            if(e.type==mod::story::WireType){storyIn.push_back(e);continue;}     // story resync news (tickStory)
            if(e.type==5){pingIn.push_back(e);continue;}     // a ping (tickMarkers, marker.h)
            if(e.type==mod::signals::EventWireType){signalIn.push_back(e);continue;}   // a shout or a scan (tickSignals)
            if(e.type==mod::signals::VitalsWireType){if(!vitalsRx.feed(e))note("SIGNAL vitals refused: malformed\n");continue;}
            if(e.type==mod::likes::WireType){likeIn.push_back(e);continue;}   // a like (tickLikes, likes.h)
            if(e.type==mod::nametag::WireType){if(auto n=nameRx.feed(e)){partnerName=*n;note(("TAG partner name "+std::to_string(n->size())+" bytes\n").c_str());}continue;}   // nametag.h
            if(e.type==mod::outfit::WireType){               // the partner's art parts (tickGear, outfit.h)
                if(auto p=mod::outfit::decode(e)){if(!partnerParts||*partnerParts!=*p)note(("OUTFIT the partner wears "+std::to_string(__builtin_popcountll(p->enabled))+" of "+std::to_string(p->count)+" parts\n").c_str());partnerParts=*p;}
                continue;
            }
            if(e.type==mod::gear::GearWireType){             // the gear the partner shows (tickGear, gear.h)
                if(auto s=wornRx.feed(e)){bool changed=!partnerWorn||*partnerWorn!=*s;partnerWorn=*s;
                    if(changed)note(("GEAR the partner shows "+std::to_string(s->size())+" pieces\n").c_str());}
                continue;
            }
            if(!coop){note("CARGO partner event ignored (not in peer mode)\n");continue;}
            if(e.type==4) {   // the partner dismantled a structure: by ref, or by type and place when the ref is unknown here
                if(netCopies.known(e.ref))netCopies.dismantled(e.ref,now);
                else if(auto net=mod::structures::netVariant(uint8_t(e.item)))netCopies.dismantledAt(*net,{e.position[0],e.position[2],e.position[1]},now);
                continue;
            }
            if(e.type==2) {
                // The partner picked up (or used) a shared item: remove our copy if it lies in the world here.
                std::array<double,3> at{e.position[0],e.position[2],e.position[1]};   // adapter -> game axes
                if(orderLoads.cancel(e.ref)){note(("ORDERLOAD the partner took case "+std::to_string(e.ref)+" back before it was made here\n").c_str());continue;}
                auto index=cargoSync.partnerTook(e.ref,e.item,at);std::ostringstream o;
                if(index&&cargoDelete){cargoDelete(*index);++cargoRemoved;o<<"CARGO partner took "<<e.ref<<": removed record "<<*index<<"\n";}
                else o<<"CARGO partner took "<<e.ref<<": nothing to remove here\n";
                note(o.str().c_str());continue;
            }
            if(e.type!=1){note("CARGO unknown partner event type ignored\n");continue;}
            auto t=mod::puppet::fromState(e.position,e.yaw);
            if(auto tag=std::exchange(loadTag,std::nullopt);tag&&mod::orderload::belongs(*tag,e.ref)){
                // An order's case: made by tickCargo's look below once this game holds no record of that unit.
                mod::orderload::Case c;c.tag=*tag;c.name=e.item;c.id=e.ref;c.position=t.position;c.rotation=t.rotation;orderLoads.want(c,now);
                std::ostringstream o;o<<"ORDERLOAD partner put down unit "<<int(tag->unit)<<" of "<<std::hex<<tag->mission<<std::dec<<" ref "<<e.ref<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
                continue;}
            cargoSync.expect({e.item,t.position,e.ref},now);cargoCreate(e.item,t);++cargoMade;
            std::ostringstream o;o<<"CARGO created partner item "<<e.item<<" ref "<<e.ref<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
        }
        if(!coop||now-cargoReadAt<250)return;
        cargoReadAt=now;uint64_t manager=0;if(!sampler->read(cargoManagerSlot,&manager,8))return;
        auto anchor=anchorNow();if(!anchor.entity||!anchor.pose.available)return;
        bool full=!cargoFullAt||now-cargoFullAt>=4000;auto scanStart=mod::nowUs();
        auto records=mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,full?mod::cargo::MaxRecords:cargoLimit);if(records.empty())return;
        if(full){auto limit=mod::cargo::scanLimit(records);
            if(!cargoFullAt||limit!=cargoLimit){std::ostringstream o;o<<"CARGO full scan: "<<records.size()<<" records in use, quick scans to "<<limit
                <<", took "<<(mod::nowUs()-scanStart)/1000.<<" ms\n";note(o.str().c_str());}
            cargoFullAt=now;cargoLimit=limit;}
        std::set<uint64_t> deliveredUnits;   // share_order_load: the units this game counts as delivered (their handles)
        if(config.shareOrderLoad){
            for(auto& u:mod::orders::readUnits(*sampler,sampler->base))if(u.state==mod::orders::Delivered)deliveredUnits.insert(mod::orderload::handle(u.mission,u.unit));
            // The cargo this game made for an order it took on for the partner goes (never from the porter) ...
            unsigned left=0;
            for(auto index:orderLoads.strip(records,now,&left))if(cargoDelete){cargoDelete(index);++loadStripped;
                note(("ORDERLOAD removed this game's own case of the partner's order (record "+std::to_string(index)+")\n").c_str());}
            if(left!=loadLeftNoted){loadLeftNoted=left;if(left)note(("ORDERLOAD "+std::to_string(left)+" own case(s) of the partner's order are not in the world here: left alone\n").c_str());}
            // ... and the cases the partner put down are made, each once no record of its unit is left here.
            for(auto& c:orderLoads.due(records,now,deliveredUnits)){
                needOrderCaseCreate();bool ok=false;
                if(orderCaseCreate){cargoSync.expect({c.name,c.position,c.id},now);ok=orderCaseCreate(c);}
                orderLoads.tried(c.id,ok,now);if(ok)++loadMade;
                std::ostringstream o;o<<"ORDERLOAD unit "<<int(c.tag.unit)<<" of "<<std::hex<<c.tag.mission<<std::dec<<" ref "<<c.id<<(ok?" made here as order cargo":" refused by the game (tried again)")<<"\n";note(o.str().c_str());
            }
            for(auto& c:orderLoads.takeDelivered()){std::ostringstream o;o<<"ORDERLOAD unit "<<int(c.tag.unit)<<" of "<<std::hex<<c.tag.mission<<std::dec<<" not made: it is delivered here\n";note(o.str().c_str());}
            for(auto& c:orderLoads.takeGivenUp()){std::ostringstream o;o<<"ORDERLOAD unit "<<int(c.tag.unit)<<" of "<<std::hex<<c.tag.mission<<std::dec<<" not made: this game still holds that unit\n";note(o.str().c_str());}
            // This game's own cases of a mirrored order are never shared cargo.
            records.erase(std::remove_if(records.begin(),records.end(),[&](auto& r){return orderLoads.own(r);}),records.end());
        }
        for(auto& p:cargoSync.observe(records,anchor.pose.position,now)) {
            ds::CargoEvent e;e.type=1;e.item=p.hash;e.ref=p.id;e.position={p.position[0],p.position[2],p.position[1]};   // game -> adapter axes
            if(config.shareOrderLoad&&mod::orderload::handedIn(p,deliveredUnits)){
                // Handed in at a terminal, not put down: the unit's delivery is what the partner is told (orders.h).
                std::ostringstream o;o<<"ORDERLOAD unit "<<int(p.unit)<<" of "<<std::hex<<p.mission<<std::dec<<" was handed in here: its case is not sent as cargo\n";note(o.str().c_str());continue;}
            if(p.mission&&config.shareOrderLoad){
                std::ostringstream o;o<<"ORDERLOAD unit "<<int(p.unit)<<" of "<<std::hex<<p.mission<<" put down here: name 0x"<<p.hash<<" item 0x"<<p.item<<" type "<<std::dec<<int(p.type)<<std::hex<<" flags 0x"<<p.flags<<" 0x"<<p.flags2<<std::dec<<" life "<<p.life[0]<<" "<<p.life[1];
                if(auto tag=mod::orderload::tagOf(p)){auto w=mod::orderload::encode(*tag);
                    ds::CargoEvent te;te.type=mod::orderload::WireType;te.item=w.item;te.ref=w.ref;te.position=w.position;core->queueCargo(te);++loadSent;o<<" -> partner as order cargo\n";}
                else o<<" -> partner as plain cargo (not a standard order's case)\n";
                note(o.str().c_str());}
            core->queueCargo(e);++cargoSent;
            std::ostringstream o;o<<"CARGO placed item "<<p.hash<<" ref "<<p.id<<" at "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<" -> partner\n";note(o.str().c_str());
        }
        for(auto index:cargoSync.takeDoomed()) {
            // The partner took this item before our copy of it appeared: remove the copy now.
            if(cargoDelete){cargoDelete(index);++cargoRemoved;note(("CARGO removed a copy taken before it appeared (record "+std::to_string(index)+")\n").c_str());}
        }
        for(auto& k:cargoSync.takeKept()) {
            std::ostringstream o;o<<"CARGO not shared: item "<<k.hash<<" kind "<<unsigned(k.kind)<<(k.pickedUp?" picked up":" put down")<<"\n";note(o.str().c_str());
        }
        for(auto& tk:cargoSync.takeTaken()) {
            ds::CargoEvent e;e.type=2;e.ref=cargoSync.wireRef(tk);e.item=tk.hash;e.position={tk.position[0],tk.position[2],tk.position[1]};   // game -> adapter axes
            core->queueCargo(e);++cargoTaken;
            note(("CARGO took item "+std::to_string(tk.hash)+(tk.id?" shared "+std::to_string(tk.id):std::string(" (no shared id)"))+" -> partner\n").c_str());
        }
    }
    // --- Map markers (marker.h): the partner on the map and pings. OFF until `puppet.marker` arms it for this process.
    // --- The partner's vehicle (ride.h): opt-in (partner_vehicle, or `puppet.ride on`). `lead K` is a one-copy test: the
    // vehicle of kind K drives 8 m ahead of this porter as if a partner rode it.
    std::filesystem::path rideCommand;std::unique_ptr<mod::ride::PartnerVehicle> ride;bool rideOn=false,rideConfigDone=false;
    uint8_t rideLead=0;uint64_t ridePorter=0;bool rideHideRider=false;   // test: `rider` hides the body while its vehicle is drawn
    std::function<void(uint64_t,const mod::puppet::Transform&)> rideTeleport;   // lab test: `goto X Y Z` (game axes)
    void makeRide() {
#ifdef MOD_FAKE_NETWORK
        note("RIDE unavailable: the fake world has no vehicles\n");
#else
        try{ride=std::make_unique<mod::ride::PartnerVehicle>(mod::ride::nativeApi(*sampler),[this](const std::string& row){note(row.c_str());});}
        catch(const std::exception& e){note((std::string("RIDE unavailable: ")+e.what()+"\n").c_str());}
#ifdef _WIN32
        try{rideTeleport=mod::join::nativeTeleport(*sampler);}catch(const std::exception& e){note((std::string("RIDE goto unavailable: ")+e.what()+"\n").c_str());}
#endif
#endif
    }
    // Riding along on the partner's truck (passenger.h).
    // This porter's own vehicle, as the provider last saw it (game axes): where a partner riding along is held.
    struct LocalVehicle { bool riding=false; uint8_t kind=0; std::array<double,3> position{}; std::array<float,9> rotation{}; uint64_t at=0; uint64_t entity=0; };
    LocalVehicle localVehicle;bool partnerAboard=false;mod::puppet::FloatVariable restVar;bool localResting=false;
    // The porter's own animation controls (controls.h): read every frame, sent beside presence; echo keeps its own.
    mod::controls::Binding controlBinding;mod::controls::Gather controlGather;bool echoOwn=false;uint64_t controlEntity=0;std::atomic<uint64_t> frameSamples{0};
    // Every game frame between ticks (mod_game_thread_frame): the porter's controls only, so that a one-frame pulse is
    // seen. Reads only; the porter of the last tick (readPorter checks the manager still belongs to it).
    // Its own lock (the runtime's is held by the network thread every few milliseconds); the tick says whether it is wanted.
    std::mutex controlsLock;std::atomic<bool> controlsWanted{false};
    std::atomic<uint64_t> gameFrames{0},gameFramesAt{0};
    bool sampleControls(){
        ++gameFrames;   // every game frame (the frame rate, logged each minute: FRAMES)
        if(!controlsWanted.load(std::memory_order_acquire)||!sampler)return false;
        std::unique_lock<std::mutex> g(controlsLock,std::try_to_lock);if(!g||!controlEntity)return false;
        std::array<uint8_t,ds::AnimationBoolBytes> held{};std::array<float,ds::AnimationFloats> floats{};
        if(!mod::controls::readPorter(*sampler,controlEntity,animatorVtable,controlBinding,held,floats))return false;
        controlGather.add(held,floats);if(othersControls.load(std::memory_order_relaxed))othersGather.add(held,floats);++frameSamples;return true;}
    mod::carryplace::SlotCache handSlots;uint8_t handsNoted=0;
    std::vector<mod::carryplace::Placed> partnerHands;
    // Floating carriers (carts.h): the partner's towed carts made here, and what this porter tows.
    mod::carts::Display cartDisplay;std::optional<mod::carts::Api> cartApi;bool cartTried=false;size_t cartsNoted=0,partnerCartsNoted=0;
    std::vector<mod::wheels::Joint> partnerJoints;uint16_t partnerSkeleton=0;bool wheelsReadNoted=false,wheelsWriteNoted=false;   // the partner's wheels (tickRide)   // the cases in the partner's hands, relative to his root (tickStack)
    std::deque<std::pair<uint64_t,ds::Animation>> echoControls;std::array<uint8_t,ds::AnimationBoolBytes> echoLast{};bool echoHave=false;
    std::function<void(uint64_t,const mod::puppet::Transform&)> keepOut;uint64_t keptOutNoted=0;
    mod::passenger::Rider rider;mod::passenger::Api passengerApi;bool passengerTried=false;mod::marker::KeyEdge passengerEdge;std::optional<std::array<float,3>> seatTest;std::optional<mod::passenger::Seat> heldSeat;uint64_t seatedAt=0;   // heldSeat: a `place` seat being ridden (moved every frame)uint64_t seatedAt=0;
    // A rider held at a place (the bike): the place follows the display vehicle. Every tick and every frame between.
    uint64_t heldPorter=0;
    std::array<double,3> seatTarget{};bool seatMoved=false;   // where the held rider was last put, and whether the game took it
    void movePassenger(){
        if(!heldSeat||!rider.riding()||!heldPorter||!passengerApi.move||!sampler)return;
        // Where the ridden thing is now: a cart by its carrier record, another player's vehicle by where the mod last
        // put it (the entities' own poses lag both: live), else the entity's pose.
        std::optional<mod::puppet::Transform> at;
        if(fleet)for(auto& v:fleet->vehicles())if(v.entity==rider.truck()){mod::puppet::Transform t;t.position=v.at.position;t.rotation=v.at.rotation;at=t;}
        if(!at&&!otherCarts.empty())for(auto& c:mod::carts::read(*sampler,sampler->base))if(c.entity==rider.truck()){mod::puppet::Transform t;t.position=c.position;t.rotation=c.rotation;
            for(auto& d:otherCarts)if(auto p=d.second.placed(c.id)){t.position=p->position;t.rotation=p->rotation;}   // where the mod put it last (the pose lags)
            at=t;}
        if(!at){auto p=mod::puppet::readEntityPose(*sampler,rider.truck());if(p.available)at=mod::puppet::transform(p);}
        if(at){const auto seat=mod::passenger::seatPlace(*at,*heldSeat);seatTarget=seat.position;seatMoved=passengerApi.move(heldPorter,seat);}
    }
    void tickPassenger(const mod::puppet::Anchor& anchor,bool connected,uint64_t now){
        // In group play the rider is tickGroupPassenger's: this partner path, with no partner, would let him go.
        if(config.othersVehicles&&!partnerMode())return;
        heldPorter=anchor.entity;
        if(!config.passengerKey||!ride||!anchor.entity)return;
        if(!passengerTried){passengerTried=true;try{passengerApi=mod::passenger::nativeApi(*sampler);note("PASSENGER ready: the passenger key gets on and off the partner's truck or bike\n");}catch(const std::exception& e){note((std::string("PASSENGER unavailable: ")+e.what()+"\n").c_str());}}
        if(!passengerApi.attach)return;
        const auto& r=cache.remote;mod::passenger::Look l;
        l.permitted=connected&&partnerMode()&&!inEvent&&(rider.riding()||!cache.local.vehicle);
        uint64_t truck=ride->entity();
        auto seat=mod::passenger::seat(r.state.vehicle);   // a truck's side, or behind the driver of a bike
        if(seat&&seatTest){seat->right=(*seatTest)[0];seat->forward=(*seatTest)[1];seat->up=(*seatTest)[2];}
        l.partnerInTruck=r.visible&&seat&&truck&&ride->kind()==r.state.vehicle;
        l.truck=l.partnerInTruck?truck:0;
        auto here=anchor.pose.available?std::optional<mod::puppet::Transform>(mod::puppet::transform(anchor.pose)):std::nullopt;
        std::optional<mod::puppet::Transform> at;
        if(truck){auto p=mod::puppet::readEntityPose(*sampler,truck);if(p.available)at=mod::puppet::transform(p);}
        if(here&&at){double dx=here->position[0]-at->position[0],dy=here->position[1]-at->position[1],dz=here->position[2]-at->position[2];l.distance=std::sqrt(dx*dx+dy*dy+dz*dz);}
        l.key=passengerEdge.pressed(modKey(config.passengerKey),now);
        if(l.key&&localVehicle.riding)l.key=false;   // driving: the key tows (tickTow)
        l.mover=passengerApi.state(anchor.entity);
        if(l.key&&!rider.riding()&&!(l.permitted&&l.partnerInTruck&&l.distance<=mod::passenger::Reach&&l.mover==mod::passenger::Free)){
            std::ostringstream o;o<<"PASSENGER key: not now (permitted "<<l.permitted<<", partner in a truck or on a bike "<<l.partnerInTruck<<", distance "<<l.distance<<" m, mover "<<int(l.mover)<<")\n";note(o.str().c_str());}
        switch(rider.tick(l)){
            case mod::passenger::Do::GetOn:
                if(seat&&(seat->place?(at&&passengerApi.hold(anchor.entity,mod::passenger::seatPlace(*at,*seat))):passengerApi.attach(anchor.entity,rider.truck(),*seat))){if(seat->place)heldSeat=*seat;}
                if(seat&&passengerApi.state(anchor.entity)!=mod::passenger::Free){seatedAt=now;note(r.state.vehicle==ds::Truck?"PASSENGER on the partner's truck\n":"PASSENGER on the partner's bike\n");}
                else{rider.forget();note("PASSENGER refused by the game\n");}
                break;
            case mod::passenger::Do::GetOff:{
                heldSeat.reset();passengerApi.release(anchor.entity);
                // Set down beside the truck, a step further out from its middle than he hung, on the ground.
                if(here&&at&&rideTeleport){
                    double dx=here->position[0]-at->position[0],dy=here->position[1]-at->position[1],n=std::sqrt(dx*dx+dy*dy);
                    if(n>0.1){mod::puppet::Transform t=*here;t.position[0]+=dx/n*1.5;t.position[1]+=dy/n*1.5;t.position[2]+=1.0;
                        auto porter=mod::join::readPorter(*sampler);
                        try{if(porter.readable)rideTeleport(porter.player,t);}catch(const std::exception& e){note((std::string("PASSENGER set-down failed: ")+e.what()+"\n").c_str());}}
                }
                note(("PASSENGER off: "+rider.why()+"\n").c_str());break;}
            default:break;
        }
        if(rider.riding()&&seatedAt&&now-seatedAt>=2000&&here&&at){seatedAt=0;   // where he ended up, in the vehicle's own axes (and from its driver)
            double d[3]={here->position[0]-at->position[0],here->position[1]-at->position[1],here->position[2]-at->position[2]};auto dot=[&](int row){return d[0]*at->rotation[size_t(3*row)]+d[1]*at->rotation[size_t(3*row+1)]+d[2]*at->rotation[size_t(3*row+2)];};
            auto driver=mod::puppet::fromState(r.state.position,r.state.yaw);double e[3]={here->position[0]-driver.position[0],here->position[1]-driver.position[1],here->position[2]-driver.position[2]};
            auto dotE=[&](int row){return e[0]*at->rotation[size_t(3*row)]+e[1]*at->rotation[size_t(3*row+1)]+e[2]*at->rotation[size_t(3*row+2)];};
            std::ostringstream o;o<<"PASSENGER held at right "<<dot(0)<<" forward "<<dot(1)<<" up "<<dot(2)<<" of the vehicle; right "<<dotE(0)<<" forward "<<dotE(1)<<" up "<<dotE(2)<<" of its driver\n";note(o.str().c_str());}
        if(!rider.riding())heldSeat.reset();
        movePassenger();
        if(!rider.riding()&&!rider.why().empty()&&rider.why()=="the game let go"){note("PASSENGER off: the game let go\n");rider.forget();}
        // Not riding: kept out of the partner's truck, which has no collision of its own.
        auto keepBox=truck?mod::passenger::box(ride->kind()):std::nullopt;
        if(!rider.riding()&&keepBox&&here&&at&&connected&&!inEvent&&!cache.local.vehicle){
            if(auto out=mod::passenger::pushOut(*at,here->position,*keepBox)){
#ifdef _WIN32
                try{auto porter=mod::join::readPorter(*sampler);
                    if(porter.readable){if(!keepOut)keepOut=mod::join::nativeTeleportExact(*sampler);mod::puppet::Transform t=*here;t.position=*out;keepOut(porter.player,t);
                        if(now-keptOutNoted>=10000){keptOutNoted=now;note(ride->kind()==ds::Truck?"PASSENGER kept out of the partner's truck\n":"PASSENGER kept out of the partner's bike\n");}}}
                catch(const std::exception& e){if(now-keptOutNoted>=10000){keptOutNoted=now;note((std::string("PASSENGER keep-out failed: ")+e.what()+"\n").c_str());}}
#endif
            }
        }
    }
    uint64_t passengerParent=0;uint8_t passengerSeen=0xff;
    std::string passengerTest(const std::string& args,const mod::puppet::Anchor& anchor){
#ifdef _WIN32
        if(!sampler||!anchor.entity)return "PASSENGER refused: no porter\n";
        auto same=[&](uint64_t rva,std::initializer_list<uint8_t> want){std::vector<uint8_t> got(want.size());return sampler->read(sampler->base+rva,got.data(),got.size())&&std::equal(want.begin(),want.end(),got.begin());};
        if(!same(0x26bcfa0,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x20})||!same(0x2696c50,{0xc5,0xfa,0x11,0x89,0x30,0x07,0x00,0x00})
           ||!same(0x190aa00,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9})||!same(0x190ad20,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x19})
           ||!same(0x191c790,{0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x8b,0x09}))return "PASSENGER refused: the functions differ\n";
        uint64_t mover=0,vtable=0;
        if(!sampler->read(anchor.entity+0xb0,&mover,8)||!mover||!sampler->read(mover,&vtable,8)||vtable!=sampler->base+0x3d6f018)return "PASSENGER refused: no player mover\n";
        uint8_t state=0;sampler->read(mover+0x720,&state,1);
        auto parent=reinterpret_cast<void(*)(uint64_t,uint64_t,uint64_t)>(sampler->base+0x235e990);
        if(args=="off"){
            reinterpret_cast<void(*)(uint64_t,float)>(sampler->base+0x2696c50)(mover,0.0f);
            if(passengerParent){parent(anchor.entity,0,0);passengerParent=0;}
            return "PASSENGER released (state was "+std::to_string(state)+")\n";
        }
        char helper[64]{};char extra[16]{};int n=std::sscanf(args.c_str(),"on %63s %15s",helper,extra);
        uint64_t vehicle=ride?ride->entity():0;
        if(n<1||!vehicle)return "PASSENGER refused: say on HELPER, with the mod's vehicle made (lead 2, park)\n";
        alignas(16) uint8_t name[32]{};alignas(16) uint64_t slot[4]{vehicle+0x20,0,0,0};
        alignas(16) float local[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        uint32_t joint=0;sampler->read(anchor.entity+0x934,&joint,4);
        if(n>=2&&std::string(extra)=="parent"){parent(anchor.entity,vehicle,0);passengerParent=vehicle;}
        reinterpret_cast<void(*)(void*,const char*)>(sampler->base+0x190aa00)(name,helper);
        reinterpret_cast<void(*)(void*)>(sampler->base+0x191c790)(slot);
        reinterpret_cast<void(*)(uint64_t,uint32_t,void*,void*,float*,float)>(sampler->base+0x26bcfa0)(mover,joint,slot,name,local,0.0f);
        reinterpret_cast<void(*)(void*)>(sampler->base+0x190ad20)(name);
        uint8_t after=0;sampler->read(mover+0x720,&after,1);passengerSeen=0xff;
        std::ostringstream o;o<<"PASSENGER on "<<helper<<(passengerParent?" (parented)":"")<<" joint "<<joint<<": state "<<int(state)<<" -> "<<int(after)<<"\n";return o.str();
#else
        (void)args;(void)anchor;return "PASSENGER needs the game\n";
#endif
    }
    void watchPassenger(const mod::puppet::Anchor& anchor){
        uint64_t mover=0;uint8_t state=0;
        if(!sampler||!anchor.entity||!sampler->read(anchor.entity+0xb0,&mover,8)||!mover||!sampler->read(mover+0x720,&state,1))return;
        if(state!=passengerSeen&&passengerSeen!=0xfe){
            if(passengerSeen!=0xff||state)note(("PASSENGER mover state "+std::to_string(state)+"\n").c_str());
            passengerSeen=state;}
    }
    void tickRide(const mod::puppet::Anchor& anchor,uint64_t now) {
        watchPassenger(anchor);
        auto stamp=" "+mod::runStamp()+"\n";
        if(config.partnerVehicle&&!rideConfigDone){rideConfigDone=true;if(!ride)makeRide();rideOn=ride!=nullptr;if(rideOn)note("RIDE partner vehicle on (config)\n");}
        if(!rideCommand.empty()&&std::filesystem::exists(rideCommand)) {
            auto text=readConfig(rideCommand);
            if(!std::filesystem::remove(rideCommand))throw std::runtime_error("cannot consume ride command");
            if(text=="on"+stamp){if(!ride)makeRide();rideOn=ride!=nullptr;rideLead=0;}
            else if(text=="off"+stamp){rideOn=false;rideLead=0;}
            else if(text=="park"+stamp){rideLead=0xff;}   // test: the lead vehicle stops where it is
            else if(text=="rider"+stamp){rideHideRider=!rideHideRider;}
            else if(float r=0,f=0,u=0;text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0&&std::sscanf(text.c_str(),"seat %f %f %f",&r,&f,&u)==3){
                seatTest=std::array<float,3>{r,f,u};note("RIDE seat offset set for the next ride along\n");}   // lab: where the rider is held, from the helper
            else if(text=="notice"+stamp){noticeTest=true;}           // test: one line in the game's info log
            else if(text=="continue"+stamp){continueAgain=true;}      // test: load the last save again (from the title screen)
            else if(text=="tag on"+stamp){tagTest=true;}              // test: a name over the partner body
            else if(text=="tag off"+stamp){tagTest=false;}
            else if(text=="tag all"+stamp){tagLimit=0;note("TAG test: a label for every shown body (no limit)\n");}   // lab: not run live
            else if(text=="tag near"+stamp){tagLimit=mod::nametag::LabelsAtOnce;note("TAG test: labels for the partner and the nearest others only\n");}
            else if(unsigned v=0;text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0&&std::sscanf(text.c_str(),"voice %u",&v)==1){voiceTest=int(v);}   // lab: one sentence container on the partner body
            else if(text=="pulse"+stamp){pulseTest=true;}             // lab: the scanner pulse variables at the partner body
            else if(text=="vitals"+stamp){vitalsTest=true;}           // lab: this porter's scan counter and status pairs, logged once
            else if(text=="stack off"+stamp){stackTest=-2;}            // test: no cases drawn
            else if(text=="stack auto"+stamp){stackTest=-1;}           // the partner's summary (the default)
            else if(text.rfind("stack ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test: `stack N` draws N plain cases on the body whatever the partner carries; `stack at ox oy oz ux uy uz`
                // moves them and sets the stacking direction (anchor axes right, forward, up; metres).
                mod::stack::Layout l;int n=0;
                if(std::sscanf(text.c_str()+6,"at %lf %lf %lf %lf %lf %lf",&l.offset[0],&l.offset[1],&l.offset[2],&l.up[0],&l.up[1],&l.up[2])==6){
                    auto now=stackLayout.value_or(mod::stack::Layout{});now.offset=l.offset;now.up=l.up;stackLayout=now;if(stacks)stacks->layout=now;}
                else if(unsigned cols=0;std::sscanf(text.c_str()+6,"across %lf %lf %lf %u",&l.across[0],&l.across[1],&l.across[2],&cols)==4&&cols>=1&&cols<=4){
                    auto now=stackLayout.value_or(mod::stack::Layout{});now.across=l.across;now.columns=cols;stackLayout=now;if(stacks)stacks->layout=now;}
                else if(std::sscanf(text.c_str()+6,"%d",&n)==1&&n>=0&&n<=int(mod::stack::Stack::MaxCases))stackTest=n;
                else {note("RIDE stack refused\n");return;}}
            else if(text.rfind("place ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test: put this porter exactly at X Y Z (game axes) facing HEADING degrees (0 = +x, 90 = +y), no ground placement.
#ifdef _WIN32
                double x=0,y=0,z=0,h=0;auto porter=mod::join::readPorter(*sampler);
                try{
                    if(std::sscanf(text.c_str()+6,"%lf %lf %lf %lf",&x,&y,&z,&h)==4&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::isfinite(h)&&porter.readable&&!inEvent){
                        auto exact=mod::join::nativeTeleportExact(*sampler);double r=h*3.14159265358979323846/180.0;float c=float(std::cos(r)),sn=float(std::sin(r));
                        mod::puppet::Transform t;t.position={x,y,z};t.rotation={sn,-c,0, c,sn,0, 0,0,1};   // rows: right, forward, up
                        exact(porter.player,t);note("RIDE place done\n");}
                    else note("RIDE place refused\n");
                }catch(const std::exception& e){note((std::string("RIDE place unavailable: ")+e.what()+"\n").c_str());}
#endif
                return;}
            else if(text.rfind("goto ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test: move this porter with the game's own player teleport (join.h), once, in normal gameplay.
                double x=0,y=0,z=0;auto porter=mod::join::readPorter(*sampler);
                if(rideTeleport&&std::sscanf(text.c_str()+5,"%lf %lf %lf",&x,&y,&z)==3&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)
                   &&porter.readable&&!inEvent){mod::puppet::Transform t;t.position={x,y,z};t.rotation=porter.rotation;rideTeleport(porter.player,t);
                    note("RIDE goto done\n");}
                else note("RIDE goto refused\n");
                return;}
            else if(text.rfind("passenger ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test (research-passenger): "passenger on HELPER [parent]" holds this porter at HELPER of the mod's
                // vehicle with the game's own follow-a-helper mode of the player mover (0x26bcfa0); "passenger off"
                // releases him (0x2696c50). The mover's state (+0x720: 0 off, 1 blending in, 2 attached) is logged.
                std::string rest=text.substr(10,text.size()-10-stamp.size());note(passengerTest(rest,anchor).c_str());
                return;}
            else if(auto k=mod::puppet::parseCargoCommand(text,mod::runStamp(),"lead");k&&*k<=3){if(!ride)makeRide();rideOn=ride!=nullptr;rideLead=uint8_t(*k);}
            else {note("RIDE ignored command for another process\n");return;}
            note(("RIDE command "+text.substr(0,text.find(' '))+(rideLead?" "+std::to_string(rideLead):std::string())+"\n").c_str());
        }
        if(!ride)return;
        if(anchor.entity!=ridePorter){if(ridePorter)ride->forget("world changed");ridePorter=anchor.entity;rider.forget();}
        if(!rideOn){ride->tick(now,false,0,nullptr);return;}
        if(rideLead==0xff){ride->tick(now,true,0,nullptr);return;}
        if(rideLead){
            bool ok=anchor.entity&&anchor.pose.available&&anchor.pose.coherent&&!inEvent;
            if(!ok){ride->tick(now,true,0,nullptr);return;}
            auto t=mod::puppet::transform(anchor.pose);
            // Forward is the rotation's second row (game axes, z up).
            for(int i=0;i<3;++i)t.position[i]+=8*double(t.rotation[3+i]);
            ride->tick(now,true,rideLead,&t);return;
        }
        bool connected=core&&core->connected();
        const auto& r=cache.remote;
        tickPassenger(anchor,connected,now);
        if(!connected||inEvent){ride->tick(now,false,0,nullptr);return;}
        if(!r.visible||!r.state.vehicle){ride->tick(now,true,0,nullptr);return;}
        auto t=mod::ride::vehicleTransform(r.state.position,r.state.vehicleOffset,r.state.vehicleAngles);
        ride->tick(now,true,r.state.vehicle,&t,{r.state.vehicleVariant,r.state.vehicleLook});
#ifdef _WIN32
        // His wheels, as his game posed them (wheels.h): only on the vehicle of his kind.
        if(ride->entity()&&ride->kind()==r.state.vehicle&&!partnerJoints.empty()){
            bool done=mod::wheels::write(*sampler,ride->entity(),partnerSkeleton,partnerJoints);
            if(!wheelsWriteNoted){wheelsWriteNoted=true;note(done?"WHEELS the partner's wheels turn on his vehicle\n":"WHEELS refused: the partner's vehicle has another model or no pose\n");}}
#endif
    }
    // --- The partner's carried cargo drawn on the partner body (cargo_stack.h): with carry_visual, from the partner's
    // carry summary (backpack slot); `puppet.ride` "stack N" / "stack auto" / "stack off" / "stack at ..." test it.
    // One stack per body (cargo_stack.h, Stacks): key 0 is the partner's, a player's id another player's (config
    // others_carry: the nearest N others who carry something and whose body wears a copy of the backpack).
    std::unique_ptr<mod::stack::Stacks> stacks;mod::others::Nearest othersNearest;
#ifdef MOD_FAKE_NETWORK
    struct FakeCase {uint32_t item=0;std::array<double,3> at{};};std::map<uint64_t,FakeCase> fakeCases;uint64_t fakeCaseNext=0x50000000;
#endif
    bool stackTried=false;Background<mod::stack::Api> stackJob;int stackTest=-1;uint64_t stackPorter=0;
    std::optional<mod::stack::Layout> stackLayout;
    // Where each case sits (carry_place.h): this porter's go out measured from his backpack; the partner's are drawn
    // at the same places on the copy of the backpack the body wears.
    mod::carryplace::Sender placeTx;mod::carryplace::Receiver placeRx;std::optional<std::vector<mod::carryplace::Placed>> placeShown;
    mod::carryplace::Sender cartPlaceTx{mod::carryplace::CartWireType};mod::carryplace::Receiver cartPlaceRx{mod::carryplace::CartWireType};std::optional<std::vector<mod::carryplace::Placed>> cartPlaceShown;uint64_t cartPlaceReadAt=0;
    uint64_t ownBackpack=0,placeReadAt=0,placeSent=0;
    static std::vector<mod::stack::Item> backpackItems(const std::optional<mod::carry::Summary>& s){
        std::vector<mod::stack::Item> out;if(s)for(auto& e:*s)if(e.slot==1)for(unsigned i=0;i<e.count;++i)out.push_back({e.hash,e.variant});   // 1: BackPack
        return out;}
    void tickStack(uint64_t body,const mod::puppet::Anchor& anchor) {
        bool on=(config.carryVisual&&stackTest!=-2)||stackTest>=0;
        bool forOthers=config.othersCarry&&others&&otherBodies&&gears;
        // Off: the partner's stack keeps itself (and its count of refusals) with no cases, as the single stack did;
        // any other player's goes.
        if(!on&&!forOthers){if(stacks)stacks->tick({mod::stack::Load{}});return;}
#ifndef MOD_FAKE_NETWORK
        if(!stacks&&!stackTried){
            auto* sp=&*sampler;if(!stackJob.ready([sp]{return mod::stack::nativeApi(*sp);}))return;   // checked beside the game
            stackTried=true;
            try{auto api=stackJob.job.get();
                // Slow native calls are logged (a case's first mesh can stall a tick while the game sets it up).
                auto slow=[this](const char* what,uint64_t us){if(us>=10000)note((std::string("STACK slow ")+what+" "+std::to_string(us/1000)+" ms\n").c_str());};
                api.caseFor=[f=api.caseFor,slow](uint32_t item,bool variant){auto t=mod::nowUs();auto r=f(item,variant);slow("case lookup",mod::nowUs()-t);return r;};
                api.create=[f=api.create,slow](const mod::stack::Case& c,const mod::puppet::Transform& at){auto t=mod::nowUs();auto r=f(c,at);slow("mesh create",mod::nowUs()-t);return r;};
                stacks=std::make_unique<mod::stack::Stacks>(std::move(api),[this](const std::string& row){note(row.c_str());});
                if(stackLayout)stacks->layout=*stackLayout;}
            catch(const std::exception& e){note((std::string("STACK unavailable: ")+e.what()+"\n").c_str());}}
#else
        // The fake world's case meshes: a table of what the mod made and where it last put each.
        if(!stacks){mod::stack::Api api;
            api.caseFor=[](uint32_t item,bool)->std::optional<mod::stack::Case>{mod::stack::Case c;c.model=0x1000+item;return c;};
            api.create=[this](const mod::stack::Case& c,const mod::puppet::Transform& at)->uint64_t{fakeCases[fakeCaseNext+=0x40]={uint32_t(c.model-0x1000),at.position};return fakeCaseNext;};
            api.place=[this](uint64_t m,const mod::puppet::Transform& at){auto it=fakeCases.find(m);if(it==fakeCases.end())return false;it->second.at=at.position;return true;};
            api.release=[this](uint64_t m){fakeCases.erase(m);};
            stacks=std::make_unique<mod::stack::Stacks>(std::move(api),[this](const std::string& row){note(row.c_str());});}
#endif
        if(!stacks)return;
        if(anchor.entity!=stackPorter){if(stackPorter)stacks->forget("world changed");stackPorter=anchor.entity;}
        std::vector<mod::stack::Load> loads;
        // The partner's (key 0), listed first: not permitted means his cases go.
        {mod::stack::Load partner;if(on)partnerLoad(body,partner);loads.push_back(std::move(partner));}
        // The other players': the nearest few carriers, each on the copy of the backpack that player's body wears.
        if(forOthers){
            auto viewer=others->here();
            for(auto& c:mod::others::carriers(othersShown,*otherBodies,[this](uint64_t id){return gears->copyAt(id,"HLP_Backpack");},!inEvent,viewer?&*viewer:nullptr,config.othersCarry,othersNearest)){
                // As the partner's: a hidden body or an unreadable backpack copy has no cases this tick (its stack stays).
                mod::stack::Load l;l.key=c.key;
                uint64_t flags=0;bool shown=sampler->read(c.body+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
                auto pose=packPose(c.pack,c.body);
                if(shown&&pose.available&&pose.coherent){l.permitted=true;l.anchor=mod::puppet::transform(pose);l.items=std::move(c.items);l.placed=std::move(c.placed);}
                loads.push_back(std::move(l));
            }
        }
        stacks->tick(loads);
    }
    // What the partner's body carries now (the partner path as it was; `load` stays not permitted when nothing is to show).
    void partnerLoad(uint64_t body,mod::stack::Load& load) {
        auto wanted=stackTest>=0?std::vector<mod::stack::Item>(size_t(stackTest)):backpackItems(carrySink.shown);
        if(!(core&&core->connected())){placeShown.reset();placeRx.reset();cartPlaceShown.reset();cartPlaceRx.reset();}
        uint64_t flags=0;bool shown=body&&sampler->read(body+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        bool permitted=shown&&!inEvent&&(stackTest>=0||(partnerMode()&&core&&core->connected()));
        // On the copy of the backpack the body wears when there is one (it moves with the body's back), else the body.
        uint64_t on_=gears?gears->copyAt(0,"HLP_Backpack"):0;
        auto pose=on_?packPose(on_,body):mod::puppet::readEntityPose(*sampler,body);
        if(!permitted||!(pose.available&&pose.coherent))return;
        auto t=mod::puppet::transform(pose);load.permitted=true;load.anchor=t;
        // With the partner's measured places and a backpack copy to hang them on: every case exactly there. The cases in
        // his hands follow (relative to his root, as his mirrored arms hold them), after the others so the stack stays.
        bool cartCases=config.partnerCart&&cartPlaceShown&&!cartPlaceShown->empty()&&cartDisplay.idAt(0);
        if(stackTest<0&&(placeShown||!partnerHands.empty()||cartCases)&&on_){
            std::vector<mod::stack::Item> items;std::vector<mod::puppet::Transform> places;
            if(placeShown)for(auto& c:*placeShown){items.push_back({c.item,c.variant});places.push_back(c.rel);}
            auto root=mod::puppet::readEntityPose(*sampler,body);
            if(root.available&&root.coherent)for(auto& h:partnerHands){
                auto world=mod::carryplace::compose(mod::puppet::transform(root),h.rel);
                items.push_back({h.item,h.variant});places.push_back(mod::carryplace::relative(t,world));}
            // The cases on his cart, on the cart drawn here for it (carts.h), as many as the stack still draws.
            if(cartCases)for(auto& c:mod::carts::read(*sampler,sampler->base))if(c.id==cartDisplay.idAt(0)){
                mod::puppet::Transform cart;cart.position=c.position;cart.rotation=c.rotation;
                for(auto& p:*cartPlaceShown){if(items.size()>=mod::stack::Stack::MaxPlaced)break;
                    items.push_back({p.item,p.variant});places.push_back(mod::carryplace::relative(t,mod::carryplace::compose(cart,p.rel)));}
                break;}
            load.items=std::move(items);load.placed=std::move(places);return;
        }
        load.items=std::move(wanted);
    }
    // --- Likes for the partner (likes.h): opt-in partner_likes. L sends one like (at most one per 1.5 s); a like that
    // arrives is given to this porter the game's way (at most one a second, ten a minute).
    std::vector<ds::CargoEvent> likeIn;mod::likes::Sender likeTx;mod::likes::Receiver likeRx;mod::marker::KeyEdge likeEdge;
    std::optional<mod::likes::Api> likesApi;bool likesTried=false;Background<mod::likes::Api> likesJob;uint64_t likesSent=0,likesGiven=0;
    void tickLikes(uint64_t now) {
        if(!config.partnerLikes||!core){likeIn.clear();return;}
#ifndef MOD_FAKE_NETWORK
        if(!likesTried){
            auto* sp=&*sampler;if(!likesJob.ready([sp]{return mod::likes::nativeApi(*sp);})){likeIn.clear();return;}
            likesTried=true;
            try{likesApi=likesJob.job.get();note("LIKE on (config), key L\n");}
            catch(const std::exception& e){note((std::string("LIKE unavailable: ")+e.what()+"\n").c_str());}}
#endif
        bool coop=partnerMode()&&core->connected()&&!inEvent;
        if(likeEdge.pressed(coop&&modKey(0x4c),now))
            if(auto e=likeTx.press(now,coop)){core->queueCargo(*e);++likesSent;note("LIKE sent to the partner\n");}
        for(auto& e:likeIn){
            auto n=mod::likes::decode(e);if(!n){note("LIKE refused: malformed\n");continue;}
            if(!coop||!likesApi){note("LIKE dropped: not in play\n");continue;}
            if(!likeRx.accept(now)){note("LIKE dropped: too many\n");continue;}
            if(likesApi->give(*n)){++likesGiven;note("LIKE received from the partner\n");if(notices)notices->event(mod::notice::Kind::Liked,partnerName,now);}
            else note("LIKE dropped: no likes state\n");
        }
        likeIn.clear();
    }
    // --- Pings and likes in group play (grouppings.h): opt-in group_pings, over the relay's broadcast mode (SIGNAL). B
    // pings where this porter stands, for the players near who see him and on his own HUD; L gives the nearest other
    // player within 25 m a like. The others' pings are HUD marks for 30 s (tickHudMarkers) and a line in the info log
    // naming the sender; a like for this porter is given the game's way, as the partner's (likes.h).
    mod::grouppings::Pings groupPings;mod::grouppings::LikeGate groupLikeGate;mod::grouppings::Serials groupSerials{1};
    mod::marker::KeyEdge groupPingEdge,groupLikeEdge;mod::likes::Sender groupLikeTx;bool groupPingsNoted=false;size_t pingMarksNoted=0;
    std::vector<std::u16string> pingLines;uint64_t groupPingsSent=0,groupPingsShown=0,groupLikesSent=0,groupLikesGiven=0;
    bool groupLikesReady(){
#ifndef MOD_FAKE_NETWORK
        if(!likesTried){
            auto* sp=&*sampler;if(!likesJob.ready([sp]{return mod::likes::nativeApi(*sp);}))return false;
            likesTried=true;
            try{likesApi=likesJob.job.get();note("LIKE on (group play), key L\n");}
            catch(const std::exception& e){note((std::string("LIKE unavailable: ")+e.what()+"\n").c_str());}}
#else
        if(!likesApi){mod::likes::Api a;a.give=[](uint32_t n){return n==1;};likesApi=a;}   // the fake world: a like is counted
#endif
        return likesApi.has_value();
    }
    void tickGroupPings(uint64_t now){
        if(!config.groupPings||!crowd)return;
        auto& client=crowd->client();
        if(!groupPingsNoted){groupPingsNoted=true;groupSerials=mod::grouppings::Serials(uint32_t(nonce()));
            note("PINGS on (group play): B pings where this porter stands, L likes the nearest player within 25 m\n");}
        const bool play=cache.available&&!cache.gated&&!inEvent;
        std::optional<std::array<double,3>> here;if(cache.available)here=cache.local.position;   // the core's State axes (y up)
        auto game=[](const std::array<double,3>& a){return std::array<double,3>{a[0],a[2],a[1]};};   // -> game axes (z up)
        auto named=[](const ds::broadcast::SignalNews& n){return n.name.empty()?"player "+std::to_string(n.source):n.name;};
        for(auto& n:client.takeSignals()){
            const std::string name=named(n);
            if(n.kind==ds::broadcast::SignalKind::Ping){
                if(!groupPings.take(n.source,n.serial,game(n.place),now))continue;
                ++groupPingsShown;double d=-1;
                if(here){d=0;for(int i=0;i<3;++i)d+=(n.place[i]-(*here)[i])*(n.place[i]-(*here)[i]);d=std::sqrt(d);}
                std::ostringstream o;o<<"PING from "<<name<<" at "<<n.place[0]<<","<<n.place[1]<<","<<n.place[2]<<" ("<<long(std::lround(d))<<" m)\n";note(o.str().c_str());
                pingLines.push_back(mod::grouppings::pingLine(name,d));
                continue;}
            if(!play||!groupLikesReady()){note(("LIKE from "+name+" dropped: not in play\n").c_str());continue;}
            if(!groupLikeGate.accept(n.source,n.serial,now)){note(("LIKE from "+name+" dropped: too many\n").c_str());continue;}
            if(likesApi->give(1)){++groupLikesGiven;note(("LIKE received from "+name+"\n").c_str());if(notices)notices->event(mod::notice::Kind::Liked,name,now);}
            else note(("LIKE from "+name+" dropped: no likes state\n").c_str());
        }
        const bool keys=play&&here.has_value();
#ifndef MOD_MUTANT_PINGS_GROUP
        if(groupPingEdge.pressed(keys&&keyFree(mod::grouppings::PingKey)&&modKey(mod::grouppings::PingKey),now)){
            const uint32_t serial=groupSerials.next();
            if(client.signal(ds::broadcast::SignalKind::Ping,serial,0,*here,now)){
                ++groupPingsSent;groupPings.take(config.playerId,serial,game(*here),now);   // on his own HUD too
                std::ostringstream o;o<<"PING sent at "<<(*here)[0]<<","<<(*here)[1]<<","<<(*here)[2]<<"\n";note(o.str().c_str());}
            else note(client.online()?"PING not sent: at most one every 500 ms and 20 a minute\n":"PING not sent: offline\n");
        }
#endif
        if(groupLikeEdge.pressed(keys&&keyFree(mod::grouppings::LikeKey)&&modKey(mod::grouppings::LikeKey),now)){
            std::vector<mod::grouppings::Candidate> candidates;
            for(auto& s:othersShown)candidates.push_back({s.id,s.render.state.position,s.visible});
            auto target=mod::grouppings::likeTarget(*here,candidates);
            if(!target){note("LIKE not sent: nobody within 25 m\n");pingLines.push_back(u"Nobody is near enough to like.");}
            else if(!groupLikeTx.press(now,true))note("LIKE not sent: at most one every 1.5 s\n");
            else{std::string name;for(auto& s:othersShown)if(s.id==*target)name=s.name;
                if(client.signal(ds::broadcast::SignalKind::Like,groupSerials.next(),*target,*here,now)){++groupLikesSent;note(("LIKE sent to "+(name.empty()?"player "+std::to_string(*target):name)+"\n").c_str());}
                else note(client.online()?"LIKE not sent: at most one signal every 500 ms and 20 a minute\n":"LIKE not sent: offline\n");}
        }
    }
    // --- Shout, scan and vitals for the partner (signals.h). Each key covers both directions: what this porter does
    // is sent, and what the partner does is shown. `puppet.ride` "voice N", "pulse" and "vitals" try one piece alone.
    std::vector<ds::CargoEvent> signalIn;mod::signals::EventSender signalTx;mod::signals::EventReceiver signalRx;
    mod::signals::VitalsSender vitalsTx;mod::signals::VitalsReceiver vitalsRx;mod::signals::LabelPacer tagPacer;
    std::optional<mod::signals::VoiceApi> voiceApi;std::optional<mod::signals::PulseApi> pulseApi;bool voiceTried=false,pulseTried=false;
    Background<mod::signals::VoiceApi> voiceJob;Background<mod::signals::PulseApi> pulseJob;
    uint64_t signalBody=0,signalReadAt=0,vitalsReadAt=0;int voiceTest=-1;bool pulseTest=false,vitalsTest=false,scanNoted=false;
    mod::puppet::FloatVariable unbalanceVar,overVar;mod::signals::Vitals vitalsNoted;
    bool playVoice(unsigned index,const char* why) {
        if(!voiceApi||!signalBody){note((std::string("SHOUT not played (")+why+"): "+(voiceApi?"no partner body":"voice unavailable")+"\n").c_str());return false;}
        bool ok=voiceApi->play(signalBody,index);
        note((std::string(ok?"SHOUT voice asked on the partner body, container ":"SHOUT refused: not the body class, container ")+std::to_string(index)+" ("+why+")\n").c_str());return ok;
    }
    bool showPulse(const char* why) {
        auto pose=signalBody?mod::puppet::readEntityPose(*sampler,signalBody):mod::Pose{};
        if(!pulseApi||!(pose.available&&pose.coherent)){note((std::string("SCAN pulse not shown (")+why+"): "+(pulseApi?"no partner body":"pulse unavailable")+"\n").c_str());return false;}
        // The slot this porter's own last scan did not use (the game writes slot count & 1 after counting); all the way round, 60 m (the game's own range is unread).
        auto count=mod::signals::readScanCount(*sampler,anchorNow().entity);unsigned slot=count?((*count+1)&1):0;
        bool ok=pulseApi->show(slot,pose.position,pose.rotation[3],pose.rotation[4],6.2831853f,60.f);
        note((std::string(ok?"SCAN pulse variables written, slot ":"SCAN pulse refused, slot ")+std::to_string(slot)+" ("+why+")\n").c_str());return ok;
    }
    void tickSignals(uint64_t now) {
        bool wantVoice=config.partnerShout||voiceTest>=0,wantPulse=config.partnerScan=="pulse"||pulseTest;
        if(!core||!sampler){signalIn.clear();return;}
#ifndef MOD_FAKE_NETWORK
        if(wantVoice&&!voiceTried){auto* sp=&*sampler;if(voiceJob.ready([sp]{return mod::signals::nativeVoiceApi(*sp);})){voiceTried=true;
            try{voiceApi=voiceJob.job.get();note("SHOUT voice on\n");}catch(const std::exception& e){note((std::string("SHOUT voice unavailable: ")+e.what()+"\n").c_str());}}}
        if(wantPulse&&!pulseTried){auto* sp=&*sampler;if(pulseJob.ready([sp]{return mod::signals::nativePulseApi(*sp);})){pulseTried=true;
            try{pulseApi=pulseJob.job.get();note("SCAN pulse on\n");}catch(const std::exception& e){note((std::string("SCAN pulse unavailable: ")+e.what()+"\n").c_str());}}}
#else
        voiceTried=pulseTried=true;
#endif
        bool coop=partnerMode()&&core->connected();auto anchor=anchorNow();
        if(voiceTest>=0&&(voiceTried||!wantVoice)){playVoice(unsigned(voiceTest),"test");voiceTest=-1;}
        if(pulseTest&&pulseTried){showPulse("test");pulseTest=false;}
        if(vitalsTest){vitalsTest=false;auto c=mod::signals::readScanCount(*sampler,anchor.entity);auto i=mod::signals::readInfo(*sampler,sampler->base,anchor.entity);
            auto u=mod::puppet::readFloatVariable(*sampler,anchor.entity,animatorVtable,"Unbalance_Rate",unbalanceVar);
            std::ostringstream o;o<<"SIGNAL test: scan count "<<(c?std::to_string(*c):"unknown")<<", first pair "<<(i.first?std::to_string(*i.first):"unknown")
                <<", second pair "<<(i.second?std::to_string(*i.second):"unknown")<<", Unbalance_Rate "<<(u?std::to_string(*u):"unknown")<<"\n";note(o.str().c_str());}
        // This porter's shout and scan, each once.
        if(config.partnerShout)if(auto e=signalTx.shout(now,coop,callSeen)){core->queueCargo(*e);note("SHOUT sent to the partner\n");}
        if(config.partnerScan!="off"&&now>=signalReadAt){signalReadAt=now+100;
            auto count=mod::signals::readScanCount(*sampler,anchor.entity);
            if(bool(count)!=scanNoted){scanNoted=bool(count);note(count?"SCAN counter read\n":"SCAN counter not readable\n");}
            if(auto e=signalTx.scan(now,coop,count)){core->queueCargo(*e);note("SCAN sent to the partner\n");}}
        // This porter's vitals, read twice a second, sent only when what the partner would see changes.
        if(config.partnerVitals&&now>=vitalsReadAt){vitalsReadAt=now+500;
            mod::signals::Raw raw;auto info=mod::signals::readInfo(*sampler,sampler->base,anchor.entity);raw.blood=info.first;raw.stamina=info.second;
            if(anchor.entity){
                auto u=mod::puppet::readFloatVariable(*sampler,anchor.entity,animatorVtable,"Unbalance_Rate",unbalanceVar);raw.unsteady=u&&*u>=mod::signals::UnsteadyFrom;
                // A bool control's byte read as a float is a denormal: the bits (as for Is_TakeABreak).
                auto v=mod::puppet::readFloatVariable(*sampler,anchor.entity,animatorVtable,"Is_Loading_Over",overVar);uint32_t bits=0;if(v)std::memcpy(&bits,&*v,4);raw.overloaded=(bits&0xff)!=0;}
            if(auto e=vitalsTx.tick(now,coop,raw)){core->queueCargo(*e);
                if(vitalsTx.shown()!=vitalsNoted){vitalsNoted=vitalsTx.shown();auto& w=vitalsNoted;
                    note(("SIGNAL vitals sent: blood "+std::to_string(w.blood)+" stamina "+std::to_string(w.stamina)+" overloaded "+std::to_string(w.overloaded)+" unsteady "+std::to_string(w.unsteady)+"\n").c_str());}}}
        // What the partner did.
        for(auto& e:signalIn){
            auto kind=mod::signals::decodeEvent(e);if(!kind){note("SIGNAL refused: malformed\n");continue;}
            bool shout=*kind==mod::signals::Kind::Shout;
            if(!coop||inEvent||(shout?!config.partnerShout:config.partnerScan=="off")){note("SIGNAL dropped: not in play\n");continue;}
            if(!signalRx.accept(*kind,now)){note("SIGNAL dropped: too many\n");continue;}
            if(shout)playVoice(config.partnerShoutVoice,"the partner shouted");
            else{note("SCAN the partner scanned\n");if(notices)notices->scan(now,true);if(config.partnerScan=="pulse")showPulse("the partner scanned");}
        }
        signalIn.clear();
    }
    // --- A private online server (online.h): opt-in `online_server`. The game's sign-in address is pointed at it as
    // soon as the image is checked, and the game is asked to sign in while its login task is idle.
    std::optional<mod::online::Api> onlineApi;bool onlineTried=false,onlinePointed=false;Background<mod::online::Api> onlineJob;
    mod::online::Requester onlineAsk;mod::online::State onlineSeen;
    mod::vitals::Reporter vitalsReport;mod::vitals::State vitalsSeen;uint64_t vitalsAt=0;uint16_t vitalsFlags=0;bool partnerLaid=false;
    mod::vitals::CallReporter callReport;mod::puppet::FloatVariable callTypeVar,callForceVar;bool callSeen=false;
    void tickOnline(uint64_t now) {
        if(config.onlineServer.empty())return;
#ifndef MOD_FAKE_NETWORK
        if(!onlineApi&&!onlineTried){
            auto* sp=&*sampler;if(!onlineJob.ready([sp]{return mod::online::nativeApi(*sp);}))return;
            onlineTried=true;
            try{onlineApi=onlineJob.job.get();}
            catch(const std::exception& e){note((std::string("ONLINE unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!onlineApi)return;
        if(!onlinePointed){
            onlinePointed=true;
            if(!onlineApi->plain()||!onlineApi->point(config.onlineServer)){note("ONLINE unavailable: address or body functions not set\n");onlineApi.reset();return;}
            note(("ONLINE server "+config.onlineServer+"\n").c_str());
            freeBuildPatch();receivedPatch();serverWorldPatch();}   // before the game can sign in or load a save: no answer has arrived yet
        auto st=onlineApi->state();
        if(!(st==onlineSeen)){
            onlineSeen=st;
            note(("ONLINE login task state "+std::to_string(st.taskState)+" step "+std::to_string(st.step)+" error "+std::to_string(st.error)+(st.signedIn()?" (signed in)":"")+"\n").c_str());}
        if(onlineAsk.tick(now,st))note(onlineApi->request()?"ONLINE sign-in asked\n":"ONLINE sign-in not asked: no login task\n");
    }
    // --- Received structures built (received.h): opt-in `build_received`. While the game is signed in to the server
    // the config names, every tick on the game thread looks at the construction records in state 0 and, for one the
    // game has just made from that server's answer, writes the one flag byte that skips its range and placement checks,
    // once per record. The walker then builds it in the game's own frame.
    mod::received::Builder receivedBuilder;mod::received::Builder::Write receivedWrite;bool receivedOn=false,receivedTimed=false;
    // And from the first frame: with the option, two bytes of the game's request builder for a server's answer are
    // changed once, right after the sign-in address is pointed at the server and before the game is asked to sign in,
    // so a record the game makes from an answer is made with the flags the scan would write and built in the frame it
    // is made (a rain shelter is refused in that same frame). The scan stays: it has nothing to do then, and does the
    // work alone when the code differs.
    mod::received::Early receivedEarly=mod::received::Early::Off;
    // Free build (freebuild.h): before any save is read, so what was built this way is not taken down as a save loads.
    mod::freebuild::State freeBuild=mod::freebuild::State::Off;
    void freeBuildPatch() {
        if(!sampler||!config.freeBuild)return;
        auto* sp=&*sampler;
        freeBuild=mod::freebuild::apply(sampler->base,true,[sp](uint64_t at,uint8_t* out,size_t n){return sp->read(at,out,n);},mod::received::nativeCodeWrite());
        note((std::string("FREEBUILD ")+mod::freebuild::label(freeBuild)+"\n").c_str());
    }
    // A world that is the server's (serverworld.h): set up before any save loads, so no record of it ever stands.
    mod::serverworld::Filter* serverWorld=nullptr;std::unique_ptr<mod::serverworld::Link> serverWorldLink;uint64_t serverWorldNotedAt=0,serverWorldRefused=0,serverWorldSaves=0;
    void serverWorldPatch() {
        if(config.serverWorld.empty()||!sampler||serverWorld)return;
#ifdef _WIN32
        static mod::serverworld::Filter filter(config.serverWorld);   // the game's code calls it for as long as it runs
        try{mod::serverworld::install(*sampler,filter);serverWorld=&filter;note("SERVERWORLD on: only this server's structures stand in the world\n");}
        catch(const std::exception& e){note((std::string("SERVERWORLD unavailable: ")+e.what()+"\n").c_str());config.serverWorld.clear();}
#else
        static mod::serverworld::Filter filter(config.serverWorld);serverWorld=&filter;note("SERVERWORLD on: only this server's structures stand in the world\n");
#endif
    }
    void tickServerWorld(uint64_t now) {
        if(!serverWorld)return;
        if(!serverWorldLink){
            auto at=serverAt();if(!at){note("SERVERWORLD: no server address, its older structures stay hidden\n");serverWorld=nullptr;return;}
            const std::string target=at->base+"/coop/world-ids",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            serverWorldLink=std::make_unique<mod::serverworld::Link>([=]{auto r=mod::strand::get(address,port,target,token,mod::strand::ConnectMs);return mod::serverworld::Result{r.status,r.body};},*serverWorld,config.serverWorld,[this](const std::string& s){note(s.c_str());});
        }
        const auto seen=mod::serverworld::stats();
        if(seen.saves!=serverWorldSaves){serverWorldSaves=seen.saves;note(("SERVERWORLD save: left out "+std::to_string(seen.leftOut)+" of the server's records\n").c_str());}
        if(now>=serverWorldNotedAt+60000&&seen.refused!=serverWorldRefused){serverWorldNotedAt=now;serverWorldRefused=seen.refused;std::ostringstream o;o<<"SERVERWORLD kept out: asked "<<seen.asked<<" refused "<<seen.refused<<" loads "<<seen.loads<<" saves "<<seen.saves<<" left out of the last "<<seen.leftOut<<" listed "<<serverWorld->listed()<<"\n";note(o.str().c_str());}
    }
    void receivedPatch() {
        if(!sampler||!mod::received::early(config.buildReceived,!config.onlineServer.empty()))return;
        receivedEarly=mod::received::patchAnswers(*sampler,sampler->base,mod::received::nativeCodeWrite());
        note(mod::received::line(receivedEarly).c_str());
    }
    void tickReceived() {
        bool on=sampler&&mod::received::active(config.buildReceived,!config.onlineServer.empty(),onlineSeen.signedIn());
        if(on!=receivedOn){receivedOn=on;if(config.buildReceived)note(on?"RECEIVED on: structures this server hands the game are built at once\n":"RECEIVED off: not signed in to the server\n");}
        if(!on)return;
        if(!receivedWrite)receivedWrite=mod::received::nativeWrite();
        if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
        auto t0=mod::nowUs();
        for(auto& c:receivedBuilder.step(*sampler,sampler->base,imageSize,receivedWrite))note(mod::received::Builder::line(c).c_str());
        if(!receivedTimed){receivedTimed=true;std::ostringstream o;o<<"RECEIVED first scan: "<<(mod::nowUs()-t0)/1000.<<" ms\n";note(o.str().c_str());}
    }
    // --- What other players build, at once (livebuild.h): with build_received, while signed in to the server, its
    // `…/coop/built` is read on a thread of its own and, once a second on the game thread, what is missing near the
    // porter is asked of the game's construction queue with the server's online id; what was removed is taken down.
    std::unique_ptr<mod::livebuild::Reader> liveReader;mod::livebuild::Plan livePlan;
    std::function<uint32_t(uint8_t,const mod::puppet::Transform&,const std::string&)> constructServed;bool liveTried=false;
    uint64_t liveStepAt=0,liveBuilt=0,liveRemoved=0;unsigned liveTwice=0;
    void tickLiveBuild(uint64_t now) {
        // The notice token is all the reads need: the game's own sign-in may come and go (one more game on the same
        // Steam account ends it), what others build still arrives.
        if(!config.buildReceived||config.strandToken.empty()||config.onlineServer.empty()||!sampler)return;
        if(!liveReader){
            auto at=serverAt();if(!at)return;
            const std::string target=at->base+"/coop/built",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            liveReader=std::make_unique<mod::livebuild::Reader>([=](const std::string& since){
                auto r=mod::strand::get(address,port,target+(since.empty()?std::string():"?since="+since),token,mod::strand::ConnectMs);
                return mod::livebuild::Result{r.status,r.body};},[this](const std::string& s){note(s.c_str());});
            note("LIVEBUILD on: what other players build here is built at once\n");
        }
        for(auto& a:liveReader->take()){
            livePlan.take(a);
            if(!a.pieces.empty()||!a.removed.empty()||a.skipped){std::ostringstream o;o<<"LIVEBUILD read "<<(a.full?"all":"changes")<<": "<<a.pieces.size()<<" structures, "<<a.removed.size()<<" removed"<<(a.skipped?", "+std::to_string(a.skipped)+" not understood":"")<<(a.more?" (more on the server)":"")<<"\n";note(o.str().c_str());}
        }
        if(now<liveStepAt+1000||livePlan.idle())return;
        liveStepAt=now;
        if(!cache.available||cache.gated||inEvent)return;
        if(!constructServed&&!liveTried){liveTried=true;
            try{constructServed=mod::puppet::nativeConstructServed(*sampler);}catch(const std::exception& e){note((std::string("LIVEBUILD unavailable: ")+e.what()+"\n").c_str());}}
        if(!constructServed)return;
        if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
        std::vector<mod::livebuild::Seen> seen;
        for(auto& s:mod::own_strand::readStructures(*sampler,sampler->base,imageSize)){
            mod::livebuild::Seen v;v.online=s.online;v.owner=s.owner;v.state=s.record.state;v.position=s.record.at.position;seen.push_back(std::move(v));}
        auto anchor=anchorNow();
        auto step=livePlan.step(seen,anchor.pose.position,anchor.entity&&anchor.pose.available,now);
        for(auto& p:step.build){
            mod::puppet::Transform t;t.position=p.at.position;t.rotation=p.at.rotation;
            uint32_t id=constructServed(p.type,t,p.id);++liveBuilt;
            std::ostringstream o;o<<"LIVEBUILD built "<<p.id<<" type "<<int(p.type)<<" record "<<id<<" at "<<p.at.position[0]<<","<<p.at.position[1]<<","<<p.at.position[2]<<"\n";note(o.str().c_str());
        }
        for(auto& r:step.remove){
            if(!unconstruct)break;
            mod::puppet::Transform t;t.position=r.position;t.rotation={1,0,0,0,1,0,0,0,1};
            unconstruct({-1.5f,-1.5f,-3,0},{1.5f,1.5f,3,0},t,false,false,true);++liveRemoved;   // Net only, a 3 m box
            std::ostringstream o;o<<"LIVEBUILD removed "<<r.online<<" at "<<r.position[0]<<","<<r.position[1]<<","<<r.position[2]<<"\n";note(o.str().c_str());
        }
        for(auto& id:step.givenUp)note(("LIVEBUILD given up: "+id+" did not stand after "+std::to_string(mod::livebuild::TriesPerPiece)+" asks\n").c_str());
        if(step.twice!=liveTwice){liveTwice=step.twice;note(("LIVEBUILD records with one online id twice: "+std::to_string(step.twice)+"\n").c_str());}
    }
    // --- Cargo shared in group play (groupcargo.h): with group_cargo, while signed in to the server. The same detection
    // as two-player cargo, on a Sync of its own; set-downs and pick-ups go to `…/coop/cargo`, and the others' crates near
    // the porter are made here (CreateBaggage) and removed when taken (DeleteBaggage).
    mod::cargo::Sync groupSync;std::unique_ptr<mod::groupcargo::Plan> groupPlan;std::unique_ptr<mod::groupcargo::Courier> courier;
    uint64_t groupReadAt=0,groupFullAt=0,groupMade=0,groupRemoved=0,groupPlaced=0,groupTook=0;unsigned groupLimit=mod::cargo::FirstRecords;
    void tickGroupCargo(uint64_t now) {
        if(!config.groupCargo||config.strandToken.empty()||config.onlineServer.empty()||!sampler||!cargoCreate||!cargoManagerSlot)return;   // the token is enough (tickLiveBuild)
        if(!courier){
            auto at=serverAt();if(!at)return;
            const std::string target=at->base+"/coop/cargo",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            const uint64_t base=(nonce()>>32)<<20;groupSync.setIdBase(base);groupPlan=std::make_unique<mod::groupcargo::Plan>(base+(uint64_t(1)<<19));
            courier=std::make_unique<mod::groupcargo::Courier>(
                [=](const std::string& since){auto r=mod::strand::get(address,port,target+(since.empty()?std::string():"?since="+since),token,mod::strand::ConnectMs);return mod::groupcargo::Result{r.status,r.body};},
                [=](const std::string& body){auto r=mod::strand::post(address,port,target,token,body,mod::strand::ConnectMs);return mod::groupcargo::Result{r.status,r.body};},
                [this](const std::string& s){note(s.c_str());});
            note("CARGOSRV on: cargo set down here is shared through the server\n");
        }
        for(auto& a:courier->answered()){
            courier->send(groupPlan->answered(a.local,a.id));
            note(("CARGOSRV set-down "+std::to_string(a.local)+(a.id.empty()?" refused by the server: it stays in this world only":" is "+a.id+" on the server")+"\n").c_str());}
        for(auto& r:courier->reads())groupPlan->take(r);
        if(now-groupReadAt<250)return;
        groupReadAt=now;uint64_t manager=0;if(!sampler->read(cargoManagerSlot,&manager,8)||!manager)return;
        auto anchor=anchorNow();if(!anchor.entity||!anchor.pose.available)return;
        bool full=!groupFullAt||now-groupFullAt>=4000;
        auto records=mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,full?mod::cargo::MaxRecords:groupLimit);if(records.empty())return;
        if(full){groupFullAt=now;groupLimit=mod::cargo::scanLimit(records);}
        if(!cache.gated&&!inEvent){
            auto step=groupPlan->step(anchor.pose.position,true);
            for(auto& p:step.make){
                mod::puppet::Transform t;t.position=p.position;t.rotation={1,0,0,0,1,0,0,0,1};
                groupSync.expect(p,now);cargoCreate(p.hash,t);++groupMade;
                std::ostringstream o;o<<"CARGOSRV made another player's item "<<p.hash<<" at "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<"\n";note(o.str().c_str());}
            for(auto id:step.gone){
                auto index=groupSync.partnerTook(id);
                if(index&&cargoDelete){cargoDelete(*index);++groupRemoved;note(("CARGOSRV another player took it: removed record "+std::to_string(*index)+"\n").c_str());}}
        }
        for(auto& p:groupSync.observe(records,anchor.pose.position,now)){
            if(p.mission)continue;   // an order's case stays this porter's
            courier->send(groupPlan->placed(p,gameArea));++groupPlaced;
            std::ostringstream o;o<<"CARGOSRV set down item "<<p.hash<<" at "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<" -> server\n";note(o.str().c_str());}
        for(auto index:groupSync.takeDoomed())if(cargoDelete){cargoDelete(index);++groupRemoved;}
        groupSync.takeKept();
        for(auto& tk:groupSync.takeTaken()){
            auto says=groupPlan->taken(tk);if(says.empty())continue;
            courier->send(std::move(says));++groupTook;note(("CARGOSRV took a shared item "+std::to_string(tk.hash)+" -> server\n").c_str());}
    }
    // --- Other players' vehicles in group play (othervehicles.h): with others_vehicles, each other player drawn here
    // who rides has his truck or bike under him; the passenger key gets on and off the nearest one being driven.
    std::unique_ptr<mod::othervehicles::Fleet> fleet;uint64_t fleetPorter=0,groupRideId=0,riderNotedAt=0;bool fleetNoted=false;
    mod::passenger::Ridden ridden;   // the vehicle ridden in group play, as last seen: where a rider let go is set down
    void tickOtherVehicles(const mod::puppet::Anchor& anchor,bool event,uint64_t now){
        if(!config.othersVehicles||!sampler)return;
        if(!fleet){
            fleet=std::make_unique<mod::othervehicles::Fleet>([this]()->std::optional<mod::ride::Api>{
#ifdef MOD_FAKE_NETWORK
                return std::nullopt;
#else
                try{return mod::ride::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("VEHICLES pool unavailable: ")+e.what()+"\n").c_str());return std::nullopt;}
#endif
            },[this](const std::string& row){note(row.c_str());});
            note("VEHICLES on: other players' trucks and bikes are shown here\n");}
        if(anchor.entity!=fleetPorter){if(fleetPorter)fleet->forget("world changed");fleetPorter=anchor.entity;if(rider.riding())rider.forget();groupRideId=0;}
        std::vector<mod::othervehicles::Seen> seen;
        for(auto& s:othersShown){
            mod::othervehicles::Seen v;v.id=s.id;v.visible=s.visible&&s.render.visible;const auto& st=s.render.state;v.kind=st.vehicle;
            if(v.kind){v.at=mod::ride::vehicleTransform(st.position,st.vehicleOffset,st.vehicleAngles);v.look={st.vehicleVariant,st.vehicleLook};}
            seen.push_back(v);}
        fleet->tick(now,!event&&anchor.entity,seen);
        tickGroupPassenger(anchor,event,now);
    }
    // The floating carriers other players tow (carts.h): one display per player, from his samples, at his body.
    std::map<uint64_t,mod::carts::Display> otherCarts;size_t groupCartsNoted=0,otherCartsNoted=0;uint64_t otherCartsSummaryAt=0;
    // Towing: the passenger key while this porter drives a truck or bike hooks his nearest floating carrier within
    // TowReach of the vehicle to it, with the game's one link routine (0x265a260(cart, puller, false), as Attach Carrier
    // calls it; live 2026-11-20: linked to the truck it reads mode 2 and the truck's id as puller, and follows it as
    // driven); pressed again by a towed one (mode 2), it is let go (puller 0). Never a cart drawn for another player.
    static constexpr double TowReach=8;
    mod::marker::KeyEdge towEdge;
    void tickTow(uint64_t now){
        if(!config.passengerKey||!localVehicle.riding)return;   // only while this porter drives: on foot the key is the passenger's
        if(towEdge.pressed(modKey(config.passengerKey),now))towWhileDriving();
    }
    bool towWhileDriving(){
        if(!localVehicle.riding||!localVehicle.entity||!sampler)return false;
#ifdef _WIN32
        static const uint8_t want[14]={0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xf1};uint8_t got[14]{};
        if(!sampler->read(sampler->base+0x265a260,got,14)||std::memcmp(got,want,14)){note("TOW unavailable: the link routine differs\n");return true;}
        auto shown=otherCartIds();for(auto id:cartDisplay.ids())shown.insert(id);
        const mod::carts::Cart* best=nullptr;double bestD=TowReach;auto all=mod::carts::read(*sampler,sampler->base);const auto& v=localVehicle.position;
        for(auto& c:all){if(!c.entity||shown.count(c.id))continue;double d=std::hypot(c.position[0]-v[0],c.position[1]-v[1],c.position[2]-v[2]);if(d<=bestD){bestD=d;best=&c;}}
        if(!best){note("TOW no floating carrier of yours within 8 m of the vehicle\n");return true;}
        uint32_t mode=0;sampler->read(best->entity+0x698,&mode,4);
        const bool letGo=mode==2;
        reinterpret_cast<void(*)(uint64_t,uint64_t,bool)>(sampler->base+0x265a260)(best->entity,letGo?0:localVehicle.entity,false);
        uint32_t after=0,puller=0;sampler->read(best->entity+0x698,&after,4);sampler->read(best->entity+0x77c,&puller,4);
        std::ostringstream o;o<<"TOW "<<(letGo?"let go":"hooked")<<" floating carrier "<<best->id<<" at "<<std::fixed<<std::setprecision(1)<<bestD<<" m: mode "<<mode<<" -> "<<after<<" puller 0x"<<std::hex<<puller<<"\n";note(o.str().c_str());
        return true;
#else
        return false;
#endif
    }
    std::set<uint32_t> otherCartIds() const {std::set<uint32_t> ids;for(auto& d:otherCarts)for(auto id:d.second.ids())ids.insert(id);return ids;}
    void tickOtherCarts(bool event,uint64_t now){
        if(!config.othersCarts||!sampler)return;
#ifdef _WIN32
        if(!cartTried){cartTried=true;try{cartApi=mod::carts::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("CART unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!cartApi)return;
        // A copy this porter attached is the other player's, not his: it goes (made again at its owner).
        {auto shown=otherCartIds();if(!shown.empty())for(auto& c:mod::carts::read(*sampler,sampler->base))if(c.attached&&shown.count(c.id)){cartApi->remove(c.id);note("CART this porter took another player's cart: removed, made again at him\n");}}
        std::set<uint64_t> here;size_t withAnimation=0,cartsSent=0;
        for(auto& s:othersShown){
            std::vector<mod::carts::Place> places;
            if(s.animation){++withAnimation;cartsSent+=s.animation->animation.cartCount;}
            if(!event&&s.visible&&s.render.visible&&s.animation){
                auto body=mod::puppet::fromState(s.render.state.position,s.render.state.yaw).position;
                for(size_t i=0;i<s.animation->animation.cartCount;++i)places.push_back(mod::carts::place(s.animation->animation.carts[i],body));}
            if(places.empty()&&!otherCarts.count(s.id))continue;
            here.insert(s.id);otherCarts[s.id].show(places,*cartApi,now);}
        for(auto it=otherCarts.begin();it!=otherCarts.end();){
            if(!here.count(it->first)||!it->second.count()){it->second.clear(*cartApi);it=otherCarts.erase(it);}else ++it;}
        if(now>=otherCartsSummaryAt+60000){otherCartsSummaryAt=now;std::ostringstream o;o<<"CART others: "<<othersShown.size()<<" drawn, "<<withAnimation<<" with animation, "<<cartsSent<<" carts in their samples\n";note(o.str().c_str());}
        size_t n=0;for(auto& d:otherCarts)n+=d.second.count();
        if(n!=otherCartsNoted){otherCartsNoted=n;note(("CART other players' floating carriers here: "+std::to_string(n)+"\n").c_str());}
    }
    // Riding along on another player's truck or bike (passenger.h), as on the partner's.
    void tickGroupPassenger(const mod::puppet::Anchor& anchor,bool event,uint64_t now){
        heldPorter=anchor.entity;
        if(!config.passengerKey||!fleet||!anchor.entity)return;
        if(!passengerTried){passengerTried=true;
#ifndef MOD_FAKE_NETWORK
            try{passengerApi=mod::passenger::nativeApi(*sampler);note("PASSENGER ready: the passenger key gets on and off another player's truck or bike\n");}catch(const std::exception& e){note((std::string("PASSENGER unavailable: ")+e.what()+"\n").c_str());}
#ifdef _WIN32
            try{rideTeleport=mod::join::nativeTeleport(*sampler);}catch(const std::exception&){}
#endif
#endif
        }
        if(!passengerApi.attach)return;
        auto here=anchor.pose.available?std::optional<mod::puppet::Transform>(mod::puppet::transform(anchor.pose)):std::nullopt;
        std::vector<mod::othervehicles::Candidate> cs;
        // Where each one is: where it was last put (the mover's playback target; the entity's own pose lags it by tens of
        // metres while it is driven: live), else its entity's pose.
        std::map<uint64_t,mod::puppet::Transform> placed;
        for(auto& v:fleet->vehicles()){
            mod::puppet::Transform t;t.position=v.at.position;t.rotation=v.at.rotation;
            if(t.position==std::array<double,3>{}){auto p=mod::puppet::readEntityPose(*sampler,v.entity);if(!p.available)continue;t=mod::puppet::transform(p);}
            placed[v.entity]=t;cs.push_back({v.id*16+v.kind,v.entity,v.kind,v.driven,t.position});}
        // The floating carriers other players tow, shown here: each one a place to stand on while he pulls it.
        if(!otherCarts.empty())for(auto& c:mod::carts::read(*sampler,sampler->base))for(auto& d:otherCarts)if(d.second.ids().count(c.id)&&c.entity)
            {auto p=d.second.placed(c.id);mod::puppet::Transform t;t.position=p?p->position:c.position;t.rotation=p?p->rotation:c.rotation;placed[c.entity]=t;
             cs.push_back({d.first*16+mod::passenger::CartKind,c.entity,mod::passenger::CartKind,true,t.position});}
        auto chosen=here?mod::othervehicles::choose(cs,here->position,rider.riding()?1e9:mod::passenger::Reach,rider.riding()?groupRideId:0):std::nullopt;
        mod::passenger::Look l;
        l.permitted=!event&&(rider.riding()||!cache.local.vehicle);
        auto seat=chosen?mod::passenger::seat(chosen->kind):std::nullopt;
        l.partnerInTruck=chosen&&seat;l.truck=l.partnerInTruck?chosen->entity:0;
        std::optional<mod::puppet::Transform> at;
        if(chosen){if(placed.count(chosen->entity))at=placed[chosen->entity];else{auto p=mod::puppet::readEntityPose(*sampler,chosen->entity);if(p.available)at=mod::puppet::transform(p);}}
        if(here&&at){double dx=here->position[0]-at->position[0],dy=here->position[1]-at->position[1];l.distance=std::sqrt(dx*dx+dy*dy);}   // across the ground, as the choice is; the height is board's (below)
        l.key=passengerEdge.pressed(modKey(config.passengerKey),now);
        if(l.key&&localVehicle.riding)l.key=false;   // driving: the key tows (tickTow)
        l.mover=passengerApi.state(anchor.entity);
        // From above or below the seat (a roof, a ledge): moved to the vehicle first, or refused (passenger.h board).
        auto boarding=mod::passenger::Board::Hold;std::optional<mod::puppet::Transform> boardTo;
        if(l.key&&!rider.riding()&&seat&&at&&here){
            const auto s=mod::passenger::seatPlace(*at,*seat);
            boarding=mod::passenger::board(here->position[2],s.position[2],bool(rideTeleport));
            if(boarding==mod::passenger::Board::Refuse&&l.permitted&&l.distance<=mod::passenger::Reach&&l.mover==mod::passenger::Free){l.key=false;
                std::ostringstream o;o<<"PASSENGER key: not now ("<<std::fixed<<std::setprecision(1)<<here->position[2]-s.position[2]<<" m above the seat: "
                    <<(rideTeleport?"further than "+std::to_string(int(mod::passenger::SeatClimb))+" m":std::string("the move to it is unavailable"))<<")\n";note(o.str().c_str());}
            if(boarding==mod::passenger::Board::MoveFirst)boardTo=mod::passenger::boardPlace(*at,chosen->kind,*seat,here);}
        if(l.key&&!rider.riding()&&!(l.permitted&&l.partnerInTruck&&l.distance<=mod::passenger::Reach&&l.mover==mod::passenger::Free)){
            std::ostringstream o;o<<"PASSENGER key: not now (permitted "<<l.permitted<<", a driven truck or bike near "<<l.partnerInTruck<<", distance "<<l.distance<<" m, mover "<<int(l.mover)<<"; "<<cs.size()<<" candidates";
            for(auto& c:cs)o<<" | "<<c.id<<" kind "<<int(c.kind)<<" driven "<<c.driven<<" at "<<int(c.at[0])<<","<<int(c.at[1])<<","<<int(c.at[2]);
            if(here)o<<"; porter at "<<int(here->position[0])<<","<<int(here->position[1])<<","<<int(here->position[2]);else o<<"; porter not read";
            o<<")\n";note(o.str().c_str());}
        switch(rider.tick(l)){
            case mod::passenger::Do::GetOn:{
                if(boardTo&&rideTeleport){   // the game's teleport with its ground placement (a dismount first), then the hold
                    auto porter=mod::join::readPorter(*sampler);
                    try{if(porter.readable){rideTeleport(porter.player,*boardTo);std::ostringstream o;o<<"PASSENGER moved to the vehicle first ("<<std::fixed<<std::setprecision(1)
                        <<here->position[2]-mod::passenger::seatPlace(*at,*seat).position[2]<<" m above the seat) to "<<boardTo->position[0]<<","<<boardTo->position[1]<<","<<boardTo->position[2]<<"\n";note(o.str().c_str());}}
                    catch(const std::exception& e){note((std::string("PASSENGER move to the vehicle failed: ")+e.what()+"\n").c_str());}}
                if(seat&&(seat->place?(at&&passengerApi.hold(anchor.entity,mod::passenger::seatPlace(*at,*seat))):passengerApi.attach(anchor.entity,rider.truck(),*seat))){if(seat->place)heldSeat=*seat;}
                if(seat&&passengerApi.state(anchor.entity)!=mod::passenger::Free){groupRideId=chosen->id;if(at)ridden.see(*at,chosen->kind);note(("PASSENGER on player "+std::to_string(chosen->id/16)+(chosen->kind==ds::Truck?"'s truck\n":chosen->kind==mod::passenger::CartKind?"'s floating carrier\n":"'s bike\n")).c_str());}
                else{rider.forget();ridden.clear();note("PASSENGER refused by the game\n");}
                break;}
            case mod::passenger::Do::GetOff:{
                heldSeat.reset();passengerApi.release(anchor.entity);groupRideId=0;
                // Set down beside the vehicle (where it is now, else where it last was: a game event or the fleet may
                // have taken it this tick) at its height, never where he hung: held up high he would fall from there.
                if(auto t=ridden.setDown(at,here);t&&rideTeleport){
                    auto porter=mod::join::readPorter(*sampler);
                    try{if(porter.readable){rideTeleport(porter.player,*t);std::ostringstream o;o<<"PASSENGER set down beside the vehicle"<<(at?"":"'s last place")<<" at "<<std::fixed<<std::setprecision(1)<<t->position[0]<<","<<t->position[1]<<","<<t->position[2];
                        if(here){o<<" (he was at "<<here->position[2]<<")";}
                        o<<"\n";note(o.str().c_str());}}
                    catch(const std::exception& e){note((std::string("PASSENGER set-down failed: ")+e.what()+"\n").c_str());}}
                else note(rideTeleport?"PASSENGER set-down: no place of the vehicle known\n":"PASSENGER set-down unavailable: no teleport\n");
                ridden.clear();
                note(("PASSENGER off: "+rider.why()+"\n").c_str());break;}
            default:break;
        }
        if(rider.riding()&&at)ridden.see(*at,chosen->kind);
        if(!rider.riding()){heldSeat.reset();ridden.clear();}
        if(rider.riding()&&here&&now>=riderNotedAt+2000){riderNotedAt=now;std::ostringstream o;o<<"PASSENGER riding at "<<here->position[0]<<","<<here->position[1]<<","<<here->position[2];
            if(heldSeat)o<<" seat at "<<seatTarget[0]<<","<<seatTarget[1]<<","<<seatTarget[2]<<(seatMoved?" (held there)":" (not held: the game's hold is off)");o<<"\n";note(o.str().c_str());}
        movePassenger();
    }
    // --- Offering the existing strand (offer_strand.h): opt-in `offer_strand`. While the game is signed in to the server
    // the config names and that server says this player's older strand may be offered (its notices answer, read with
    // the notice token below), the construction records he built that the game uploads are asked for, one at a time,
    // through the game's own upload call on the game thread: an empty online id with 0x2c9ed40, an id of the original
    // service with the uploader's re-create. The code and the uploader are checked before every call.
    mod::offer_strand::Offerer offerer;std::function<bool(uint64_t,bool)> offerAsk;bool offerTried=false;
    void tickOffer(uint64_t now) {
        if(!config.offerStrand)return;
        // Without strand_notices the token's reads are for the server's word only: their lines are not told.
        if(strandPoller&&!config.strandNotices){for(auto& row:strandPoller->logs())note(row.c_str());
#ifndef MOD_MUTANT_OFFER_LINES
            strandPoller->lines();
#endif
        }
        const std::string old=strandPoller?strandPoller->offerOld():std::string();
        bool on=sampler&&mod::offer_strand::active(config.offerStrand,!config.onlineServer.empty(),onlineSeen.signedIn(),old);
        if(on&&!offerTried){
            offerTried=true;
#ifdef _WIN32
            try{offerAsk=mod::offer_strand::nativeAsk(*sampler);note("OFFER the game's upload call and re-create queue are this build's\n");}
            catch(const std::exception& e){note((std::string("OFFER unavailable: ")+e.what()+"\n").c_str());}
#else
            note("OFFER unavailable: the game's upload call is Windows only\n");
#endif
        }
        if(!offerAsk)return;
        if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
        mod::offer_strand::Offerer::Calls calls;
        calls.scan=[this]{return mod::own_strand::readStructures(*sampler,sampler->base,imageSize,mod::offer_strand::select());};
        calls.ask=offerAsk;
        calls.queued=[this](uint64_t object,bool again){return mod::offer_strand::queued(*sampler,mod::offer_strand::uploader(*sampler),object,again);};
        for(auto& line:offerer.step(on,poseAvailable&&!inEvent,now,calls))note(line.c_str());
    }
    // --- What the game is told about this player's builds on the server (strand_notices.h): opt-in `strand_notices`.
    // The server's API is read on a thread of its own with the notice token the launcher took (`strand_token`); its
    // lines go into the notices' queue below. With `offer_strand` alone it is read for the server's word on his older
    // strand only (tickOffer), and nothing is told.
    std::unique_ptr<mod::strand::Poller> strandPoller;
    void strandStart() {
#ifndef MOD_MUTANT_STRAND_OFF
        if(!config.strandNotices&&!config.offerStrand)return;
#else
        if(config.onlineServer.empty())return;   // the mutant: every game with a server reads, whatever its config says
#endif
        const std::string& url=config.onlineServer;const size_t colon=url.find(':',7),slash=url.find('/',7);
        auto address=mod::peerAddress(url.substr(7,colon-7));if(!address||colon==std::string::npos||slash==std::string::npos)return;   // checked by the config
        const uint16_t port=uint16_t(std::stoul(url.substr(colon+1,slash-colon-1)));const std::string token=config.strandToken;
        const std::string base=mod::serverApiBase(url).value_or(std::string());
        strandPoller=std::make_unique<mod::strand::Poller>(base,[a=*address,port,token](const std::string& target){return mod::strand::get(a,port,target,token,mod::strand::ConnectMs);});
        note(config.strandNotices?"STRAND notices on: this player's builds on the server are read every 5 s\n":"STRAND read on: the server's word on this player's older strand is read every 5 s (offer_strand)\n");
    }
    // --- Route challenges (route.h, docs/ROUTES.md). The recording key (config route_record_key, K by default) starts and
    // stops recording the porter's path; the string goes into the log and a file in the co-op data folder's routes\.
    // A config `route` is a challenge this game runs: the checker is fed every tick with the porter's position; what it
    // says goes into the log (ROUTE ...) and, with `notices`, the info log; at the end a result file for the launcher.
    mod::route::Recorder routeRecorder;mod::marker::KeyEdge routeEdge;std::optional<mod::route::Checker> routeRun;
    std::string routeString;std::vector<std::u16string> routeLines;bool routeOver=false,routeTold=false;   // routeTold: a line was ever said
    uint64_t routeCasesAt=0;std::optional<unsigned> routeCasesSeen;
    static std::u16string routeWide(const std::string& s){std::u16string w;for(unsigned char c:s)w+=char16_t(c>=0x20&&c<0x7f?c:'?');return w;}
    static std::string routeStamp(){
        std::time_t t=std::time(nullptr);std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc,&t);
#else
        gmtime_r(&t,&utc);
#endif
        char b[32];std::strftime(b,sizeof b,"%Y%m%d-%H%M%S",&utc);return b;}
    // The co-op data folder's routes\ (beside logs\: %LOCALAPPDATA%\DeathStrandingCoop for a player, a test copy's own
    // folder in the lab, $XDG_STATE_HOME/DeathStrandingCoop in the fake world). Never a path from the config.
    // Beside the mod's own files (the mod config's folder, redirect-data\mod): the proxy lets the mod write there; a folder
    // beside the logs is outside it (live 2026-11-18: "routes folder unavailable" in every game).
    std::filesystem::path modFolder;
    std::filesystem::path routesDir() const {return modFolder.empty()?modFolder:modFolder/"routes";}
    bool routeWrite(const std::string& name,const std::string& text){
        auto dir=routesDir();if(dir.empty())return false;
        std::error_code ec;std::filesystem::create_directories(dir,ec);
        auto file=dir/name;
#ifdef _WIN32
        HANDLE h=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
        DWORD n=0;bool ok=WriteFile(h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size();CloseHandle(h);return ok;
#else
        int fd=::open(file.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)return false;
        bool ok=::write(fd,text.data(),text.size())==ssize_t(text.size());::close(fd);return ok;
#endif
    }
    // Route lines reach the info log whether or not `notices` is on (group play has no partner notices): the first one
    // brings the info log up, without a partner (Notifier::partnerless).
    void routeSay(const std::string& line){routeTold=true;routeLines.push_back(routeWide(line));if(routeLines.size()>8)routeLines.erase(routeLines.begin());}
    void routeStart(){
        if(config.route.empty())return;
        std::string why;auto r=mod::route::decode(config.route,&why);
        if(!r)throw std::runtime_error("route: "+why);
        routeString=config.route;routeRun.emplace(*r);
        char id[16];std::snprintf(id,sizeof id,"%08x",mod::route::id(config.route));
        note(("ROUTE challenge \""+r->name+"\" by \""+r->author+"\": "+mod::route::rulesText(r->rules)+", "+std::to_string(r->points.size())+" points, "
              +std::to_string(long(std::lround(mod::route::length(r->points))))+" m (id "+id+")\n").c_str());
    }
    // The porter's carried cases, read at most twice a second (carry_visual.h: cargo slots only); nullopt: unreadable.
    std::optional<unsigned> routeCases(uint64_t now){
        if(now-routeCasesAt<500)return routeCasesSeen;
        routeCasesAt=now;std::optional<mod::carry::Summary> read;auto anchor=anchorNow();uint64_t manager=0;
        if(sampler&&anchor.entity&&cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
            if(auto items=mod::carry::readCarried(*sampler,anchor.entity,sampler->base+mod::carry::ComponentVtable,manager))read=mod::carry::summarise(*items);
#ifdef MOD_FAKE_NETWORK
        if(anchor.entity&&fakeCarried)read=*fakeCarried;
#endif
        if(!read){routeCasesSeen.reset();return routeCasesSeen;}
        unsigned n=0;for(auto& e:*read)n+=e.count;routeCasesSeen=n;return routeCasesSeen;
    }
    void tickRoute(uint64_t now){
        const bool pose=cache.available&&!cache.gated;
#ifndef MOD_MUTANT_ROUTE_AXES
        const mod::route::Point at{cache.local.position[0],cache.local.position[1],cache.local.position[2]};   // presence axes: y up
#else
        const mod::route::Point at{cache.local.position[0],cache.local.position[2],cache.local.position[1]};   // the mutant: the game's axes (z up)
#endif
        if(config.routeRecordKey&&routeEdge.pressed(modKey(config.routeRecordKey),now)){
            if(!routeRecorder.on()){routeRecorder.start();if(pose)routeRecorder.sample(at,now);note("ROUTE recording started\n");routeSay("Recording a route. Press the key again to stop.");}
            else{
                if(pose)routeRecorder.sample(at,now);
                auto points=routeRecorder.stop();
                mod::route::Route r;r.name="Route "+routeStamp().substr(0,13);r.points=points;
                r.author=mod::route::nameText(config.nickname,mod::route::AuthorMax,true)?config.nickname:std::string();
                std::string text;
                try{if(points.size()>=2)text=mod::route::encode(r);}catch(const std::exception& e){note((std::string("ROUTE recording not kept: ")+e.what()+"\n").c_str());}
                if(text.empty()){note("ROUTE recording too short: nothing written\n");routeSay("Route too short: nothing recorded.");}
                else{
                    const std::string file="recorded-"+routeStamp()+".txt";
                    note(("ROUTE recorded "+std::to_string(points.size())+" points, "+std::to_string(long(std::lround(mod::route::length(points))))+" m: "+text+"\n").c_str());
                    note((routeWrite(file,text+"\n")?"ROUTE recording file routes/"+file+"\n":"ROUTE recording file not written (routes folder unavailable)\n").c_str());
                    routeSay("Route recorded: "+std::to_string(long(std::lround(mod::route::length(points))))+" m. Open the launcher's Challenges to share it.");}
            }
        }
        if(routeRecorder.on()&&pose)routeRecorder.sample(at,now);
        if(!routeRun||routeOver||!pose)return;
        mod::route::Sample sm;sm.ms=now;sm.at=at;sm.yaw=cache.local.yaw;
        sm.vehicle=(cache.local.flags&(ds::Vehicle|ds::Passenger))!=0;
        // The provider's own Teleport flag is set for any 5 m between two samples (providers.h), which a truck passes
        // in one long frame: the checker's own jump and speed rules decide instead.
        if(routeRun->route().rules.minCases)sm.cases=routeCases(now);
        auto events=routeRun->feed(sm);
        static const char* const kinds[]={"away","ready","started","leaving","back","failed","finished","wait"};
        for(auto& e:events){note(("ROUTE "+std::string(kinds[size_t(e.kind)])+": "+e.text+"\n").c_str());routeSay(e.text);}
        auto phase=routeRun->phase();
        if(phase!=mod::route::Phase::Failed&&phase!=mod::route::Phase::Finished)return;
        routeOver=true;const bool done=phase==mod::route::Phase::Finished;
        std::string track;
        try{auto r=routeRun->route();track=mod::route::encodeTrack({mod::route::id(routeString),routeRun->result(),routeRun->track()},r.points.front());}
        catch(const std::exception& e){note((std::string("ROUTE track not kept: ")+e.what()+"\n").c_str());}
        char id[16];std::snprintf(id,sizeof id,"%08x",mod::route::id(routeString));
        note(("ROUTE result "+std::string(done?"finished":"failed")+" "+std::to_string(routeRun->result())+" ms"+(done?"":" ("+routeRun->reason()+")")
              +(routeRun->casesVerified()?"":", cases unverified")+" track="+track+"\n").c_str());
        std::string json="{\"route\":\""+routeString+"\",\"outcome\":\""+(done?"finished":"failed")+"\",\"ms\":"+std::to_string(routeRun->result())
            +",\"reason\":\""+(done?"":routeRun->reason())+"\",\"cases_verified\":"+(routeRun->casesVerified()?"true":"false")+",\"track\":\""+track+"\"}\n";
        const std::string file="result-"+routeStamp()+"-"+id+".json";
        note((routeWrite(file,json)?"ROUTE result file routes/"+file+"\n":"ROUTE result file not written (routes folder unavailable)\n").c_str());
    }
    // --- Notices (notice.h): opt-in `notices`. The partner joined / left / nobody yet, in the game's own info log.
    // `puppet.ride` "notice" shows a test line whatever the session does. With `strand_notices` alone (group play has
    // no partner) the same queue tells the lines about this player's builds, and nothing about a partner.
    std::unique_ptr<mod::notice::Notifier> notices;std::optional<mod::notice::Api> noticeApi;bool noticeTried=false,noticeTest=false;Background<mod::notice::Api> noticeJob;
#ifdef MOD_FAKE_NETWORK
    std::vector<std::u16string> fakeNotices;   // the fake world's info log: every line shown, in order
#endif
    // F3: the test-mode switches on the game's common dialogue (debugmenu.h, tickDebugMenu).
    std::unique_ptr<mod::debugmenu::Menu> debugMenu;Background<mod::dialog::Api> dialogJob;mod::marker::KeyEdge debugEdge;
    // The game's common dialogue, once its code is checked; shared by the menus below. 0 unknown, 1 there, 2 not there.
    std::optional<mod::dialog::Api> dialogApi;int dialogState=0;
    // Y: text chat (chat.h, tickChat). puppet.chat (lab): "open", "type WORD" (_ for a space), "enter", "esc", each with the run's stamp.
    std::unique_ptr<mod::chat::Link> chatLink;std::unique_ptr<mod::chat::Box> chatBox;Background<mod::help::Api> chatHudJob;bool chatHudTried=false;std::optional<mod::help::Api> chatHud;
    mod::marker::KeyEdge chatEdge;std::vector<std::u16string> chatLines;std::filesystem::path chatCommand;std::vector<mod::chat::Press> chatScripted;bool chatOpenCommand=false;
    // P: the server page (serverinfo.h, tickServerInfo).
    struct MapWhere {std::atomic<float> x{0},y{0};std::atomic<uint32_t> area{0};std::atomic<bool> known{false};};std::shared_ptr<MapWhere> mapWhere=std::make_shared<MapWhere>();
    std::unique_ptr<mod::mapicons::Link> mapLink;const mod::mapicons::Board* mapBoard=nullptr;bool mapHooked=false,areaTried=false;uint32_t gameArea=0;uint64_t mapNotedUpdates=0,mapNotedAt=0;std::function<uint32_t()> areaOf;std::filesystem::path mapCommand;
    std::unique_ptr<mod::serverinfo::Screen> infoScreen;mod::marker::KeyEdge infoEdge;std::future<mod::strand::Fetched> infoRead;
    // Hold F1: the co-op keys on the game's common telop (help.h). puppet.help (lab): "at <pivot> <x> <y> <stamp>" moves
    // it, "show <stamp>" / "hide <stamp>" hold and release the key for an unattended test.
    std::unique_ptr<mod::help::Page> helpPage;bool helpTried=false,helpHeld=false;Background<mod::help::Api> helpJob;std::filesystem::path helpCommand;
    void tickHelp(uint64_t now){
        if(!config.helpPage)return;
#ifndef MOD_FAKE_NETWORK
        if(!helpPage&&!helpTried){
            auto* sp=&*sampler;if(!helpJob.ready([sp]{return mod::help::nativeApi(*sp);}))return;   // checked beside the game
            helpTried=true;
            try{helpPage=std::make_unique<mod::help::Page>(helpJob.job.get(),[this](const std::string& row){note(row.c_str());});note("HELP on: hold F1\n");}
            catch(const std::exception& e){note((std::string("HELP unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!helpPage)return;
        if(!helpCommand.empty()&&std::filesystem::exists(helpCommand)){
            auto text=readConfig(helpCommand);std::filesystem::remove(helpCommand);
            std::istringstream in(text);std::string verb,stamp;in>>verb;mod::help::Place p=helpPage->place;
            if(verb=="at")in>>p.pivot>>p.x>>p.y;
            in>>stamp;
            if(stamp!=mod::runStamp())note("HELP ignored command for another process\n");
            else if(verb=="at"){helpPage->close();helpPage->place=p;std::ostringstream o;o<<"HELP at "<<p.pivot<<" "<<p.x<<" "<<p.y<<"\n";note(o.str().c_str());}
            else if(verb=="show"||verb=="hide")helpHeld=verb=="show";
        }
        mod::help::Facts f;f.ping=pingKey?pingKey:(config.groupPings&&crowd?mod::grouppings::PingKey:0);f.like=partnerMode()||(config.groupPings&&crowd);f.likeNear=!partnerMode()&&config.groupPings&&crowd;f.ride=ride?config.passengerKey:0;f.route=config.routeRecordKey;
        f.materials=config.testMode.on?config.testMode.materialsKey:0;f.story=config.storyResync!="off"?config.storyKey:0;f.debug=config.testMode.on&&debugMenu?mod::debugmenu::Key:0;f.server=infoScreen&&infoKeyFree()?mod::serverinfo::Key:0;f.chat=chatBox&&chatLink&&chatLink->counters().on&&keyFree(mod::chat::Key)?mod::chat::Key:0;
        helpPage->tick(helpHeld||modKey(mod::help::Key),cache.available&&!cache.gated&&gameEvent&&!gameEvent(),f,now);
    }
    void tickNotices(uint64_t now) {
        if(!config.notices&&!noticeTest&&!config.strandNotices&&!routeTold&&!config.testMode.on&&!config.chat&&!config.groupPings&&!config.groupReplays)return;
#ifndef MOD_FAKE_NETWORK
        if(!noticeApi&&!noticeTried){
            auto* sp=&*sampler;if(!noticeJob.ready([sp]{return mod::notice::nativeApi(*sp);}))return;   // checked beside the game
            noticeTried=true;
            try{noticeApi=noticeJob.job.get();note("NOTICE on\n");}
            catch(const std::exception& e){note((std::string("NOTICE unavailable: ")+e.what()+"\n").c_str());}}
#else
        if(!noticeApi){mod::notice::Api a;a.show=[this](const std::u16string& text,uint8_t){fakeNotices.push_back(text);return true;};noticeApi=a;}
#endif
        if(!noticeApi)return;
        if(noticeTest){noticeTest=false;note(noticeApi->show(u"Co-op mod: test notice.",mod::notice::PlainIcon)?"NOTICE test shown\n":"NOTICE test refused: no info log now\n");}
#ifndef MOD_MUTANT_STRAND_PARTNER
        if(!config.notices&&!config.strandNotices&&!routeTold&&!config.testMode.on&&!config.chat&&!config.groupPings&&!config.groupOrders&&!config.groupReplays)return;
#else
        if(!config.notices&&!config.testMode.on)return;   // the mutant: strand lines only beside the partner's notices
#endif
        if(!notices){notices=std::make_unique<mod::notice::Notifier>(*noticeApi,[this](const std::string& row){note(row.c_str());});
            if(config.group||!config.notices)notices->partnerless();}   // nobody is expected: no "No partner yet"
        {for(auto& line:routeLines)notices->route(line,now);routeLines.clear();}   // the route challenge's lines (tickRoute)
        {for(auto& line:replayLines)notices->route(line,now);replayLines.clear();}   // boss replays together (replayStep), finished lines
        for(auto& line:testLines)notices->test(line,now);   // test mode's lines (tickTestMode), finished like the strand's
        testLines.clear();
        for(auto& line:chatLines)notices->chat(line,now);   // what the players said (tickChat)
        chatLines.clear();
        for(auto& line:pingLines)notices->ping(line,now);   // the others' pings (tickGroupPings)
        pingLines.clear();
        for(auto& line:orderLines)notices->strand(line,now);   // orders taken together (tickGroupOrders), finished like the strand's
        orderLines.clear();
        if(strandPoller&&config.strandNotices){for(auto& row:strandPoller->logs())note(row.c_str());for(auto& line:strandPoller->lines())notices->strand(line,now);}
#ifdef MOD_MUTANT_OFFER_LINES
        else if(strandPoller){for(auto& line:strandPoller->lines())notices->strand(line,now);}
#endif
        if(!config.notices){notices->tick(now,poseAvailable&&!inEvent,false,{});return;}
        bool connected=core&&core->connected();
        // Away: he reports an event or private room, or no pose of his has arrived (his game is at the title screen or
        // loading: without this a join was told at once, then "away" and "back" while he loaded).
        notices->call(now,connected,connected&&(cache.remote.state.flags&ds::Call));
        notices->link(now,connected,connected?core->presentationDelayMs():0);
        notices->status(now,connected,connected&&(cache.remote.state.flags&ds::Down),connected&&(cache.remote.state.flags&ds::Caught));
        notices->tick(now,poseAvailable&&!inEvent,connected,partnerName,connected&&((cache.remote.state.flags&ds::PrivateRoom)||!cache.remote.visible));
        notices->progress(now,connected,connected&&core->progressStatus()==ds::ProgressStatus::Mismatch);
    }
    // --- Test mode (testmode.h, config `test_mode`, README "Test mode"): for alpha testers, off by default. The material
    // key asks the game for a set of crates beside the porter (one set every 5 s, at most 20 of his on the ground); blood
    // and stamina are kept full. Every write goes through testmode::refusal: the game thread (this runs only from the
    // game-thread tick), the porter readable, no game event, not down and not caught. The info log tells the player once
    // that it is on, and each set dropped. The vehicle's battery is not kept full: its charge field is not known for
    // this build (game_offsets.md, "Test mode"); the porter's own battery is. F3 changes which are kept (testStamina...).
    mod::testmode::Dropper testDropper;mod::marker::KeyEdge testEdge;std::vector<std::u16string> testLines;
    bool testAnnounced=false,testNoted=false;uint64_t testReadAt=0,testFullAt=0,testGaugeAt=0;unsigned testLimit=mod::cargo::FirstRecords;std::string testGaugeWhy="start";
    unsigned testSets=0,testCrates=0,testRefused=0;uint64_t testBloodWrites=0,testStaminaWrites=0,testBatteryWrites=0;
    bool testStamina=false,testBlood=false,testBattery=false;   // the config's switches at the start, then the player's (F3)
    // The calls behind it, native or the fake world's: make a crate, the porter's status block, his state, the writer.
    std::function<void(uint32_t,const mod::puppet::Transform&)> testCreate;std::function<std::vector<mod::cargo::Record>(unsigned)> testRecords;
    std::function<uint64_t(uint64_t)> testBlock;std::function<mod::vitals::State(uint64_t)> porterVitals;
    mod::testmode::Writable testWritable;mod::testmode::Poke testPoke;
#ifdef MOD_FAKE_NETWORK
    std::vector<mod::testmode::Drop> fakeDrops;   // the fake world's crates: every one asked for, until the test takes it
    mod::vitals::State fakeVitals{true,false,false,false};
    bool fakeDialogUp=false;unsigned fakeDialogAnswer=mod::dialog::NoAnswer;std::u16string fakeDialogBody,fakeTelop;   // the fake world's common dialogue and telop
#endif
    static std::string keyName(unsigned vk){
        if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))return std::string(1,char(vk));
        if(vk>=0x70&&vk<=0x87)return "F"+std::to_string(vk-0x6f);
        std::ostringstream o;o<<"key 0x"<<std::hex<<vk;return o.str();}
    static std::u16string wide(const std::string& t){return std::u16string(t.begin(),t.end());}
    void tickTestMode(uint64_t now){
        const auto& t=config.testMode;if(!t.on||!sampler)return;
        auto anchor=anchorNow();const bool readable=anchor.entity&&anchor.pose.available;
        if(!testNoted){testNoted=true;testStamina=t.stamina;testBlood=t.blood;testBattery=t.battery;
            auto missing=mod::testmode::missingNames();
            note(("TESTMODE on: materials "+(t.materialsKey?"on "+keyName(t.materialsKey)+(missing.empty()?std::string():" (no crate known yet for "+missing+")"):std::string("off"))
                  +", stamina "+(t.stamina?"on":"off")+", blood "+(t.blood?"on":"off")
                  +(t.battery?", battery on (the porter's; a vehicle's charge field is not known for this build)":", battery off")+"\n").c_str());}
        if(!testAnnounced&&readable&&!inEvent){testAnnounced=true;
            std::string what=t.stamina&&t.blood?"unlimited stamina and blood":t.stamina?"unlimited stamina":t.blood?"unlimited blood":"";
            std::string keys=t.materialsKey&&testCreate?keyName(t.materialsKey)+" drops material crates":"";
            testLines.push_back(wide("Test mode is on"+(what.empty()&&keys.empty()?std::string("."):": "+what+(what.empty()||keys.empty()?"":"; ")+keys+".")));}
        mod::testmode::Gate gate;gate.gameThread=true;gate.porterReadable=readable;gate.inEvent=inEvent;
        if(readable&&porterVitals)gate.vitals=porterVitals(anchor.entity);
        // Materials: what of his crates still lies on the ground, twice a second while he has some out (quick scans up to
        // the mark of a full one, a full scan every 4 s, as the shared cargo reads the table); then the key.
        if(t.materialsKey&&testCreate){
            if(testRecords&&testDropper.onGround()&&now>=testReadAt){testReadAt=now+500;const bool full=now>=testFullAt;if(full)testFullAt=now+4000;
                auto records=testRecords(full?mod::cargo::MaxRecords:testLimit);if(full)testLimit=mod::cargo::scanLimit(records);
                testDropper.observe(records,now);}
            if(testEdge.pressed(modKey(t.materialsKey),now)){
                const char* why=mod::testmode::refusal(gate);mod::testmode::Dropper::Result r;
                if(*why)r.refused=why;
                else{auto tr=mod::puppet::transform(anchor.pose);r=testDropper.press(now,tr.position,tr.rotation[3],tr.rotation[4],mod::testmode::knownHashes());}
                if(r.drops.empty()){++testRefused;note(("TESTMODE materials refused: "+r.refused+"\n").c_str());
                    if(r.refused.find("on the ground")!=std::string::npos)testLines.push_back(wide("Test mode: "+std::to_string(mod::testmode::Dropper::MaxOnGround)+" of your crates are on the ground already."));}
                else{
                    for(auto& d:r.drops){mod::puppet::Transform at;at.position=d.at;at.rotation={1,0,0,0,1,0,0,0,1};testCreate(d.hash,at);}
                    ++testSets;testCrates+=unsigned(r.drops.size());
                    note(("TESTMODE materials dropped: "+std::to_string(r.drops.size())+" crates, "+std::to_string(testDropper.onGround())+" of yours on the ground\n").c_str());
                    testLines.push_back(u"Test mode: materials dropped.");}
            }
        }
        // Blood and stamina, ten times a second.
#ifndef MUTANT_TESTMODE_MENU_IGNORED
        const bool keepBlood=testBlood,keepStamina=testStamina,keepBattery=testBattery;
#else
        const bool keepBlood=t.blood,keepStamina=t.stamina,keepBattery=t.battery;
#endif
        if((keepBlood||keepStamina||keepBattery)&&now>=testGaugeAt){testGaugeAt=now+100;
            const uint64_t block=readable&&testBlock?testBlock(anchor.entity):0;
            auto k=mod::testmode::keepFull(gate,*sampler,block,keepBlood,keepStamina,testWritable,testPoke);
            testBloodWrites+=k.blood;testStaminaWrites+=k.stamina;
            if(keepBattery)testBatteryWrites+=mod::testmode::keepBattery(gate,*sampler,block,anchor.entity,testWritable,testPoke);
            if(testGaugeWhy!=k.refused){testGaugeWhy=k.refused;note(*k.refused?("TESTMODE gauges not kept: "+testGaugeWhy+"\n").c_str():"TESTMODE gauges kept full\n");}
        }
    }
    bool dialogReady(){
        if(dialogState==0){
#ifndef MOD_FAKE_NETWORK
            auto* sp=&*sampler;if(!dialogJob.ready([sp]{return mod::dialog::nativeApi(*sp);}))return false;   // checked beside the game
            try{dialogApi=dialogJob.job.get();dialogState=1;}catch(const std::exception& e){dialogState=2;note((std::string("DIALOGUE unavailable: ")+e.what()+"\n").c_str());}
#else
            mod::dialog::Api api;
            api.free=[this]{return !fakeDialogUp;};api.showing=[this]{return fakeDialogUp;};
            api.open=[this](const mod::dialog::Request& r){fakeDialogUp=true;fakeDialogAnswer=mod::dialog::NoAnswer;fakeDialogBody=r.body;return true;};
            api.take=[this]{auto v=fakeDialogAnswer;fakeDialogAnswer=mod::dialog::NoAnswer;if(v!=mod::dialog::NoAnswer)fakeDialogUp=false;return v;};
            dialogApi=api;dialogState=1;
#endif
        }
        return dialogState==1;
    }
    bool inWorldNow(){return cache.available&&!cache.gated&&gameEvent&&!gameEvent();}
    void tickDebugMenu(uint64_t now){
        if(!config.testMode.on||!testNoted||!sampler||!dialogReady())return;
        if(!debugMenu){
            debugMenu=std::make_unique<mod::debugmenu::Menu>(*dialogApi,std::vector<mod::debugmenu::Item>{{u"Unlimited stamina",&testStamina},{u"Unlimited blood",&testBlood},{u"Unlimited battery (not vehicles yet)",&testBattery}},
                [this](const std::string& row){note(row.c_str());});
            note("DEBUGMENU on: F3\n");
        }
        debugMenu->tick(debugEdge.pressed(modKey(mod::debugmenu::Key),now),inWorldNow(),now);
    }
    // The address the page reads: this game's server from online_server, whose form the config has checked.
    struct ServerAt {uint32_t address=0;uint16_t port=0;std::string base;};
    std::optional<ServerAt> serverAt() const {
        const std::string& url=config.onlineServer;if(url.empty())return std::nullopt;
        const size_t colon=url.find(':',7),slash=url.find('/',7);if(colon==std::string::npos||slash==std::string::npos)return std::nullopt;
        auto address=mod::peerAddress(url.substr(7,colon-7));if(!address)return std::nullopt;
        ServerAt at;at.address=*address;at.port=uint16_t(std::stoul(url.substr(colon+1,slash-colon-1)));at.base=mod::serverApiBase(url).value_or(std::string());return at;
    }
    bool keyFree(unsigned k) const {return config.passengerKey!=k&&config.storyKey!=k&&config.routeRecordKey!=k&&config.testMode.materialsKey!=k;}
    bool infoKeyFree() const {return keyFree(mod::serverinfo::Key);}
    void tickChat(uint64_t now){
        if(!config.chat||!sampler)return;
        if(!chatHud){
#ifndef MOD_FAKE_NETWORK
            if(chatHudTried)return;
            auto* sp=&*sampler;if(!chatHudJob.ready([sp]{return mod::help::nativeApi(*sp);}))return;
            chatHudTried=true;
            try{chatHud=chatHudJob.job.get();}catch(const std::exception& e){note((std::string("CHAT unavailable: ")+e.what()+"\n").c_str());return;}
#else
            mod::help::Api a;a.show=[this](const std::u16string&,const std::u16string& text,const mod::help::Place&){fakeTelop=text;return true;};a.hide=[this]{fakeTelop.clear();};chatHud=a;
#endif
        }
        if(!chatLink){
            auto at=serverAt();if(!at){note("CHAT unavailable: no server address\n");config.chat=false;return;}
            const std::string target=at->base+"/coop/chat",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            chatLink=std::make_unique<mod::chat::Link>(
                [=](const std::string& since){auto r=mod::strand::get(address,port,target+(since.empty()?std::string():"?since="+since),token,mod::strand::ConnectMs);return mod::chat::Result{r.status,r.body};},
                [=](const std::string& body){auto r=mod::strand::post(address,port,target,token,body,mod::strand::ConnectMs);return mod::chat::Result{r.status,r.body};});
            mod::chat::Hud hud;auto api=*chatHud;mod::help::Place place;place.y=700;
            hud.show=[api,place](const std::u16string& title,const std::u16string& text){return api.show(title,text,place);};hud.hide=api.hide;
            chatBox=std::make_unique<mod::chat::Box>(hud,mod::chat::nativeKeyboard(),[this](const std::string& line){chatLink->say(line);},[this](const std::string& row){note(row.c_str());});
            note(keyFree(mod::chat::Key)?"CHAT on: Y\n":"CHAT off: Y is another key of this game's config\n");
        }
        for(auto& line:chatLink->take())chatLines.push_back(line);
        std::vector<mod::chat::Press> scripted;
        if(!chatCommand.empty()&&std::filesystem::exists(chatCommand)){
            auto text=readConfig(chatCommand);std::filesystem::remove(chatCommand);std::istringstream in(text);std::vector<std::string> words;std::string w;while(in>>w)words.push_back(w);
            if(words.size()<2||words.back()!=mod::runStamp())note("CHAT ignored command for another process\n");
            else if(words[0]=="open")chatOpenCommand=true;
            else if(words[0]=="enter")scripted.push_back({mod::chat::Press::Enter,0});
            else if(words[0]=="esc")scripted.push_back({mod::chat::Press::Escape,0});
            else if(words[0]=="type"&&words.size()==3)for(char c:words[1])scripted.push_back({mod::chat::Press::Char,c=='_'?u' ':char16_t(static_cast<unsigned char>(c))});
        }
        bool pressed=chatEdge.pressed(keyFree(mod::chat::Key)&&modKey(mod::chat::Key),now)||chatOpenCommand;chatOpenCommand=false;
        const auto seen=chatLink->counters();
        if(pressed&&seen.reads&&!seen.on){chatLines.push_back(u"Text chat is off on this server.");pressed=false;}
        const bool active=pressed||chatBox->typing();
        const bool allowed=active&&inWorldNow()&&(chatBox->typing()||!dialogApi||!config.testMode.on&&!config.serverInfo||dialogApi->free());
        chatBox->tick(pressed,mod::marker::keyDown(mod::chat::Key),allowed,scripted);
        chatTyping.store(chatBox->typing(),std::memory_order_relaxed);
    }
    // Which of the game's maps this porter is in: the relay keeps players of different maps apart (they reuse coordinates), and
    // the map page shows each player on his own. The last known value stands while the game is between maps.
    void tickArea(){
#ifndef MOD_FAKE_NETWORK
        if(!sampler||config.provider!="game")return;
        if(!areaTried){areaTried=true;try{areaOf=mod::mapicons::nativeArea(*sampler);}catch(const std::exception& e){note((std::string("MAP area unknown: ")+e.what()+"\n").c_str());}}
        if(!areaOf)return;const uint32_t a=areaOf();if(a&&a!=gameArea){gameArea=a;note(("MAP area "+std::to_string(a)+"\n").c_str());}
#endif
    }
    // The other players on the game's map (mapicons.h). puppet.map (lab): "kind N", "ring METRES COUNT" (test icons around this
    // porter, at that moment), "clear", each with the run's stamp.
    void tickPlayerMap(uint64_t now){
        if(!config.playerMap||!sampler)return;
        static mod::mapicons::Board board;   // the map's update reads it for as long as the game runs
        mapBoard=&board;
        if(!mapHooked&&inWorldNow()){mapHooked=true;
#ifndef MOD_FAKE_NETWORK
            try{mod::mapicons::install(*sampler,board);note("MAP on\n");}catch(const std::exception& e){note((std::string("MAP unavailable: ")+e.what()+"\n").c_str());config.playerMap=false;return;}
#else
            note("MAP on\n");
#endif
        }
        if(!mapLink){
            auto at=serverAt();if(!at){note("MAP unavailable: no server address\n");config.playerMap=false;return;}
            const std::string target=at->base+"/coop/map",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            auto where=mapWhere;
            mapLink=std::make_unique<mod::mapicons::Link>([=]{std::string q;
                if(where->known.load(std::memory_order_relaxed)){std::ostringstream o;o<<"?x="<<where->x.load(std::memory_order_relaxed)<<"&y="<<where->y.load(std::memory_order_relaxed);if(auto a=where->area.load(std::memory_order_relaxed))o<<"&area="<<a;q=o.str();}
                auto r=mod::strand::get(address,port,target+q,token,mod::strand::ConnectMs);return mod::mapicons::Result{r.status,r.body};},board,std::to_string(config.playerId));
        }
        if(cache.available){mapWhere->x.store(float(cache.local.position[0]),std::memory_order_relaxed);mapWhere->y.store(float(cache.local.position[2]),std::memory_order_relaxed);mapWhere->area.store(gameArea,std::memory_order_relaxed);mapWhere->known.store(true,std::memory_order_relaxed);}
        const auto seen=mod::mapicons::stats();
        if(seen.updates!=mapNotedUpdates&&now>=mapNotedAt+5000){mapNotedUpdates=seen.updates;mapNotedAt=now;auto k=mapLink->counters();std::ostringstream o;
            o<<"MAP open: updates "<<seen.updates<<" taken "<<seen.taken<<" refused "<<seen.refused<<" map area "<<seen.shownArea<<" mode "<<seen.mode<<" porter area "<<gameArea<<" server on "<<k.on<<" players "<<k.shown<<"\n";note(o.str().c_str());}
        if(!mapCommand.empty()&&std::filesystem::exists(mapCommand)){
            auto text=readConfig(mapCommand);std::filesystem::remove(mapCommand);std::istringstream in(text);std::vector<std::string> w;std::string one;while(in>>one)w.push_back(one);
            if(w.size()<2||w.back()!=mod::runStamp())note("MAP ignored command for another process\n");
            else if(w[0]=="kind"&&w.size()==3){mod::mapicons::setKind(uint8_t(std::strtoul(w[1].c_str(),nullptr,0)));note(("MAP kind "+w[1]+"\n").c_str());}
            else if(w[0]=="hudkind"&&w.size()==3){const auto k=uint8_t(std::strtoul(w[1].c_str(),nullptr,0));mod::hudmarkers::setKind(k);note(("HUD markers kind "+w[1]+(mod::hudmarkers::safeKind(k)?"":" refused")+"\n").c_str());}
            // hudicon KIND|off: the far players' markers drawn with another kind's icon (hudmarkers.h); hudicons: the
            // game's icon items, once, read only; hudnames N: names over the nearest N far players (0: none).
            else if(w[0]=="hudicon"&&w.size()==3){const bool off=w[1]=="off";const unsigned long v=off?0:std::strtoul(w[1].c_str(),nullptr,0);
                const bool taken=(off||(v>0&&v<=0xff))&&mod::hudmarkers::setIconSource(uint8_t(v));
                note(("HUD markers icon "+w[1]+(taken?"":" refused")+"\n").c_str());}
            else if(w[0]=="hudicons"&&w.size()==2){for(auto& row:mod::hudmarkers::iconTableNow(*sampler))note(row.c_str());}
            else if(w[0]=="hudnames"&&w.size()==3){hudNames=std::min<size_t>(std::strtoul(w[1].c_str(),nullptr,10),mod::hudmarkers::MaxFarNames);note(("HUD names over "+std::to_string(hudNames)+" far players\n").c_str());}
            else if(w[0]=="clear")mapLink->setExtra({});
            else if(w[0]=="ring"&&w.size()==4&&cache.available){
                const double r=std::atof(w[1].c_str());const int n=std::min(std::atoi(w[2].c_str()),int(mod::mapicons::MaxIcons));std::vector<mod::mapicons::Icon> v;
                for(int i=0;i<n;++i){const double a=6.283185307*i/std::max(n,1);mod::mapicons::Icon k;k.x=float(cache.local.position[0]+r*std::cos(a));k.y=float(cache.local.position[2]+r*std::sin(a));k.z=float(cache.local.position[1]);k.area=gameArea;k.id=0x4d51000000000000ull+uint64_t(i);mod::mapicons::setName(k,"Test "+std::to_string(i+1));v.push_back(k);}
                mapLink->setExtra(std::move(v));note(("MAP test ring of "+w[2]+"\n").c_str());}
        }
    }
    // Far players on the HUD (hudmarkers.h): the map's players less those drawn here as bodies, published every tick for
    // the HUD's own job, which adds them to its marker list each frame. The pings of group play (grouppings.h) are a
    // second set on the same hook.
    // hudNames (lab `puppet.map hudnames N`): the nearest far players that get a name label (tickTag), 0 by default.
    bool hudHooked=false,hudFailed=false;uint64_t hudNotedAt=0,hudNotedFrames=0;size_t hudNames=0;std::vector<mod::hudmarkers::FarName> farNamesNow;
    static mod::hudmarkers::Board& pingBoard(){static mod::hudmarkers::Board b;return b;}   // read by the HUD's job for as long as the game runs
    void tickHudMarkers(uint64_t now){
        const bool distant=config.playerHud&&mapBoard,pings=config.groupPings&&crowd;
        if(!distant||!sampler||hudFailed)farNamesNow.clear();
        if((!distant&&!pings)||!sampler||hudFailed)return;
        static mod::hudmarkers::Board board;   // the HUD's job reads it for as long as the game runs
        if(!hudHooked&&inWorldNow()){hudHooked=true;
#ifndef MOD_FAKE_NETWORK
            try{mod::hudmarkers::attach(pingBoard());mod::hudmarkers::install(*sampler,board);note("HUD markers on\n");}
            catch(const std::exception& e){note((std::string("HUD markers unavailable: ")+e.what()+"\n").c_str());hudFailed=true;return;}
#else
            note("HUD markers on\n");
#endif
        }
        if(pings){auto marks=groupPings.marks(now);
            if(marks.size()!=pingMarksNoted){pingMarksNoted=marks.size();note(("HUD pings: "+std::to_string(marks.size())+"\n").c_str());}
            pingBoard().publish(marks);
            const auto seen=mod::hudmarkers::stats();   // without far players: what the HUD took, as their line says it
            if(!distant&&seen.frames!=hudNotedFrames&&now>=hudNotedAt+10000){hudNotedFrames=seen.frames;hudNotedAt=now;std::ostringstream o;
                o<<"HUD markers: "<<marks.size()<<" pings, HUD frames "<<seen.frames<<" records added "<<seen.added<<" kept "<<seen.kept<<" icon "<<unsigned(mod::hudmarkers::iconSource())<<" restyled "<<seen.restyled<<"\n";note(o.str().c_str());}}
        if(!distant)return;
        mod::mapicons::Board::Picture p;mapBoard->read(p);
        std::set<uint64_t> drawn;if(otherBodies)for(auto& s:othersShown)if(otherBodies->entity(s.id))drawn.insert(mod::hudmarkers::iconId(s.id));
        std::optional<std::array<double,3>> here;if(cache.available)here=std::array<double,3>{cache.local.position[0],cache.local.position[2],cache.local.position[1]};
        auto marks=mod::hudmarkers::marks(p,gameArea,drawn,here?&*here:nullptr,mod::hudmarkers::MaxMarks);board.publish(marks);
        if(hudNames)farNamesNow=mod::hudmarkers::farNames(p,marks,hudNames);else farNamesNow.clear();
        const auto seen=mod::hudmarkers::stats();
        if(seen.frames!=hudNotedFrames&&now>=hudNotedAt+10000){hudNotedFrames=seen.frames;hudNotedAt=now;std::ostringstream o;
            o<<"HUD markers: "<<marks.size()<<" far players ("<<drawn.size()<<" drawn as bodies), HUD frames "<<seen.frames<<" records added "<<seen.added<<" kept "<<seen.kept
             <<" icon "<<unsigned(mod::hudmarkers::iconSource())<<" restyled "<<seen.restyled<<" names "<<farNamesNow.size()<<"\n";note(o.str().c_str());}
    }
    void tickServerInfo(uint64_t now){
        if(!config.serverInfo||!sampler||!dialogReady())return;
        if(!infoScreen){
            mod::serverinfo::Fetch fetch;
            if(auto at=serverAt()){
#ifndef MUTANT_SERVERINFO_BASE
                const std::string target=at->base+"/coop/policy";
#else
                const std::string target="/coop/policy";
#endif
                fetch.start=[this,a=*at,target]{infoRead=std::async(std::launch::async,[a,target]{return mod::strand::get(a.address,a.port,target,std::string(),mod::strand::ConnectMs);});};
                fetch.poll=[this]()->std::optional<std::string>{
                    if(!infoRead.valid()||infoRead.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return std::nullopt;
                    auto got=infoRead.get();return got.status==200?got.body:std::string();};
            }
            infoScreen=std::make_unique<mod::serverinfo::Screen>(*dialogApi,fetch,[this](const std::string& row){note(row.c_str());});
            note(infoKeyFree()?"SERVERINFO on: P\n":"SERVERINFO off: P is another key of this game's config\n");
        }
        mod::serverinfo::Facts f;f.group=config.group;f.server=!config.onlineServer.empty();f.partner=partnerName;f.partnerHere=core&&core->connected();
        for(auto& s:othersShown)if(s.visible&&!s.name.empty())f.nearby.push_back(s.name);
        infoScreen->tick(infoEdge.pressed(infoKeyFree()&&modKey(mod::serverinfo::Key),now),inWorldNow(),f,now);
    }
    // --- Names over heads (nametag.h): one of the game's hologram-name records above the partner body and above every
    // other player's body (others.h). On with config partner_tag; `puppet.ride` "tag on" / "tag off" test it. The
    // names are the nicknames the players sent. One Tags set holds them all: key 0 is the partner, a player id another
    // player; all use the one native api, record by record, exactly as the partner's label always did.
    // At most LabelsAtOnce records are registered at once (the partner, then the others nearest this porter): the game
    // presents three names, and its record list has only been seen with one record of the mod's. `puppet.ride`
    // "tag all" lifts the limit (a label for every shown body; not run live), "tag near" puts it back.
    std::unique_ptr<mod::nametag::Tags> nameTags;bool tagTried=false,tagTest=false;Background<mod::nametag::Api> tagJob;uint64_t tagPorter=0;
    size_t tagLimit=mod::nametag::LabelsAtOnce;
    mod::nametag::Sender nameTx;mod::nametag::Receiver nameRx;std::string partnerName;
#ifdef MOD_FAKE_NETWORK
    struct FakeLabel {std::u16string name;std::array<double,3> at{};};std::map<uint64_t,FakeLabel> fakeLabels;uint64_t fakeLabelNext=0x100;
#endif
    // Where a body's label goes: 2.1 m above its feet (game axes, z up), while the body is visible and readable.
    std::optional<std::array<double,3>> labelPoint(uint64_t body) {
        uint64_t flags=0;if(!body||!sampler->read(body+mod::gear::Flags,&flags,8)||!(flags&mod::gear::FlagVisible))return std::nullopt;
        auto pose=mod::puppet::readEntityPose(*sampler,body);if(!(pose.available&&pose.coherent))return std::nullopt;
        std::array<double,3> at=pose.position;at[2]+=2.1;return at;
    }
    void makeTags(mod::nametag::Api api) {
        nameTags=std::make_unique<mod::nametag::Tags>(std::move(api),[this](uint64_t body){return labelPoint(body);},[this](const std::string& row){note(row.c_str());});
    }
    void tickTag(uint64_t body,const mod::puppet::Anchor& anchor) {
        // This player's name goes to the partner on every connection (both ends send; each shows the other's).
        if(core)for(auto& e:nameTx.tick(config.nickname,core->connected()&&partnerMode()))core->queueCargo(e);
        const bool bodyTags=tagTest||config.partnerTag;   // far players' names (farNamesNow) are a lab test of their own
        if(!bodyTags&&farNamesNow.empty()){if(nameTags)nameTags->tick(false,{});return;}
#ifndef MOD_FAKE_NETWORK
        if(!nameTags&&!tagTried){auto* sp=&*sampler;if(!tagJob.ready([sp]{return mod::nametag::nativeApi(*sp);}))return;
            tagTried=true;
            try{makeTags(tagJob.job.get());}
            catch(const std::exception& e){note((std::string("TAG unavailable: ")+e.what()+"\n").c_str());}}
#else
        // The fake world's HUD: the records the mod registered, by handle.
        if(!nameTags){mod::nametag::Api api;
            api.create=[this](const std::u16string& name,const std::array<double,3>& at)->uint64_t{fakeLabels[fakeLabelNext+=0x40]={name,at};return fakeLabelNext;};
            api.move=[this](uint64_t r,const std::array<double,3>& at){fakeLabels.at(r).at=at;};
            api.remove=[this](uint64_t r){fakeLabels.erase(r);};
            makeTags(std::move(api));}
#endif
        if(!nameTags)return;
        if(anchor.entity!=tagPorter){if(tagPorter)nameTags->forget("world changed");tagPorter=anchor.entity;}
        std::vector<mod::nametag::Named> named;
        // The partner (key 0), while his body is there and shown.
        if(bodyTags&&body&&!inEvent&&labelPoint(body)){
            auto name=mod::nametag::fromUtf8(partnerName);if(name.empty())name=u"Partner";
            // With partner_vitals: his blood, stamina and state after the name (signals.h), a new text at most every 2 s.
            if(config.partnerVitals)name=tagPacer.text(mod::nowMs(),name,vitalsRx.current(core&&core->connected()));
            named.push_back({0,body,std::move(name),std::nullopt});
        }
        // Every other player whose body exists (key: the player's id), with the name that player sent.
        if(bodyTags&&otherBodies)for(auto& n:mod::others::labels(othersShown,*otherBodies))named.push_back(std::move(n));
        // The nearest far players (not drawn here) at their place on the map, after the bodies in the limit's choice
        // since they are farther (hudmarkers::farNames; key: the mark's id).
        for(auto& f:farNamesNow)named.push_back({f.key,0,f.name,f.at});
        nameTags->limit(tagLimit);
        nameTags->tick(!inEvent,named,anchor.pose.available&&anchor.pose.coherent?&anchor.pose.position:nullptr);
    }
    std::filesystem::path markerCommand;std::unique_ptr<mod::marker::Markers> markers;std::vector<ds::CargoEvent> pingIn;
    mod::marker::KeyEdge pingEdge;unsigned pingKey=0;bool markerConfigDone=false;Background<mod::marker::Api> markerJob;
    void makeMarkers() {
#ifdef MOD_FAKE_NETWORK
        note("MARKER unavailable: the fake world has no map markers\n");
#else
        try{markers=std::make_unique<mod::marker::Markers>(mod::marker::nativeApi(*sampler),[this](const std::string& row){note(row.c_str());});
            markers->setRefBase((nonce()>>32)<<20);}
        catch(const std::exception& e){note((std::string("MARKER unavailable: ")+e.what()+"\n").c_str());}
#endif
    }
    void tickMarkers(uint64_t now) {
        auto stamp=mod::runStamp();bool ping=false;
        // partner_marker: the pin and the partner's pings from the start; no ping key (whether V is free is unverified).
        if(config.partnerMarker&&!markerConfigDone&&!markers){
#ifndef MOD_FAKE_NETWORK
            auto* sp=&*sampler;
            if(markerJob.ready([sp]{return mod::marker::nativeApi(*sp);})){markerConfigDone=true;   // checked beside the game
                try{markers=std::make_unique<mod::marker::Markers>(markerJob.job.get(),[this](const std::string& row){note(row.c_str());});
                    markers->setRefBase((nonce()>>32)<<20);}
                catch(const std::exception& e){note((std::string("MARKER unavailable: ")+e.what()+"\n").c_str());}
                if(markers){markers->enable(true);pingKey=0;note("MARKER on (config), no ping key\n");}}
#else
            markerConfigDone=true;makeMarkers();
#endif
        }
        else if(config.partnerMarker&&!markerConfigDone){markerConfigDone=true;markers->enable(true);pingKey=0;note("MARKER on (config), no ping key\n");}
        if(!markerCommand.empty()&&std::filesystem::exists(markerCommand)) {
            auto text=readConfig(markerCommand);
            if(!std::filesystem::remove(markerCommand))throw std::runtime_error("cannot consume marker command");
            if(text=="on "+stamp+"\n"||text=="off "+stamp+"\n") {
                bool on=text[1]=='n';
                if(on&&!markers)makeMarkers();
                if(markers){markers->enable(on);pingKey=on?0x42:0;}   // B: V is the game's Melee (game_offsets.md, "Keys")
            }
            else if(text=="ping "+stamp+"\n")ping=true;
            else if(auto vk=mod::puppet::parseCargoCommand(text,stamp,"key");vk&&*vk<=0xfe&&*vk!=mod::bugmark::Key){pingKey=*vk;note(("MARKER ping key "+std::to_string(*vk)+"\n").c_str());}
            else note("MARKER ignored command for another process\n");
        }
        auto incoming=std::move(pingIn);pingIn.clear();
        if(!markers||!markers->enabled()){if(!incoming.empty())note("MARKER partner ping ignored (off)\n");return;}
        auto anchor=anchorNow();
        bool hidden=inEvent||(anchor.entity&&twin&&doubleBusy(anchor.entity));   // event, private room, cargo menu
        std::optional<std::array<double,3>> partner;
        if(partnerMode()&&cache.remote.visible&&core&&core->connected()){auto& p=cache.remote.state.position;partner=std::array<double,3>{p[0],p[2],p[1]};}   // adapter -> game axes
        for(auto& e:incoming)markers->partnerPing({e.ref,{e.position[0],e.position[2],e.position[1]}},now);
        if(pingEdge.pressed(pingKey&&modKey(pingKey),now))ping=true;
        if(ping) {
            if(!anchor.entity||!anchor.pose.available)note("MARKER ping refused: no porter\n");
            else if(auto p=markers->localPing(anchor.pose.position,now)) {
                if(core&&core->connected()){ds::CargoEvent e;e.type=5;e.ref=p->ref;e.position={p->position[0],p->position[2],p->position[1]};core->queueCargo(e);   // game -> adapter axes
                    note(("MARKER ping "+std::to_string(p->ref)+" -> partner\n").c_str());}
                else note(("MARKER ping "+std::to_string(p->ref)+" shown here only (no partner)\n").c_str());
            }
        }
        std::optional<std::array<double,3>> local;if(anchor.entity&&anchor.pose.available)local=anchor.pose.position;
        markers->tick(now,partner,hidden,local);
    }
    // --- end map markers
    // --- ds3: what the partner carries (carry_visual.h); opt-in with carry_visual, read-only, the sink only logs ---
    struct CarryLog final:mod::carry::Sink {
        std::function<void(const std::string&)> log;explicit CarryLog(std::function<void(const std::string&)> f):log(std::move(f)){}
        std::optional<mod::carry::Summary> shown;   // drawn by tickStack
        void attach(const mod::carry::Summary& s) override {shown=s;log("CARRY partner shows "+mod::carry::describe(s)+"\n");}
        void detach() override {shown.reset();log("CARRY partner shows nothing\n");}
        void hold(uint32_t,uint8_t) override {log("CARRY refused: a cargo record on the partner body would be this porter's cargo\n");}
    } carrySink{[this](const std::string& s){note(s.c_str());}};
    mod::carry::Exchange carry;uint64_t carryReadAt=0,carrySent=0;std::string carryLast;
#ifdef MOD_FAKE_NETWORK
    std::optional<mod::carry::Summary> fakeCarried;
#endif
    void tickCarry(uint64_t now) {
        // The same read serves the partner (carry_visual) and the other players (others_carry): each has senders of its own.
        bool forOthers=config.othersCarry&&others;
        if((!config.carryVisual&&!forOthers)||!core||!sampler||now-carryReadAt<250)return;
#ifndef MOD_FAKE_NETWORK
        if(!cargoManagerSlot)return;
#endif
        carryReadAt=now;uint64_t manager=0;std::optional<mod::carry::Summary> read;
        auto anchor=anchorNow();
        if(anchor.entity&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
            if(auto items=mod::carry::readCarried(*sampler,anchor.entity,sampler->base+mod::carry::ComponentVtable,manager))read=mod::carry::summarise(*items);
#ifdef MOD_FAKE_NETWORK
        if(anchor.entity&&fakeCarried)read=*fakeCarried;   // the fake porter's load (mod_test_set_carry)
#endif
        std::string d=read?"porter carries "+mod::carry::describe(*read):anchor.entity?"porter's carrier unreadable (layout or vtable check failed)":"";
        if(!d.empty()&&d!=carryLast){carryLast=d;note(("CARRY "+d+"\n").c_str());}
        if(config.carryVisual)for(auto& e:carry.tick(read,core->connected()&&partnerMode(),now,carrySink)){core->queueCargo(e);++carrySent;}
        // Where the cases sit, measured from this porter's backpack (found by the gear read), twice a second.
        std::optional<std::vector<mod::carryplace::Placed>> placedNow;   // only on the passes that measured
        if(now-placeReadAt>=500&&anchor.entity&&manager&&ownBackpack){
            placeReadAt=now;auto pack=mod::puppet::readEntityPose(*sampler,ownBackpack);
            if(pack.available&&pack.coherent){
                auto placed=mod::carryplace::readPlaced(*sampler,anchor.entity,sampler->base+mod::carry::ComponentVtable,manager,mod::puppet::transform(pack));
                placedNow=placed;
                if(config.carryVisual){
                    auto events=placeTx.observe(placed,core->connected()&&partnerMode(),now);
                    for(auto& e:events){core->queueCargo(e);++placeSent;}
                    if(!events.empty())note(("CARRY this porter's "+std::to_string(placed?placed->size():0)+" cases with their places -> partner\n").c_str());
                }
            }
        }
        if(forOthers){auto sent=others->carries(read,placedNow,now);if(sent)note(("CARRY "+std::to_string(sent)+" events of what this porter carries -> other players\n").c_str());}
        if(!config.carryVisual)return;   // the cart's cases are the partner path's only
        // The cases on the cart he tows (the nearest attached one, as carts.h sends first), relative to that cart.
        if(config.partnerCart&&now-cartPlaceReadAt>=500&&anchor.entity&&anchor.pose.available&&manager){
            cartPlaceReadAt=now;auto here=mod::puppet::transform(anchor.pose).position;auto shown=cartDisplay.ids();
            const mod::carts::Cart* best=nullptr;double bestD=1e18;auto all=mod::carts::read(*sampler,sampler->base);
            for(auto& c:all){if(!c.attached||shown.count(c.id))continue;double d=0;for(int k=0;k<3;++k)d+=(c.position[k]-here[k])*(c.position[k]-here[k]);if(d<bestD){bestD=d;best=&c;}}
            std::optional<std::vector<mod::carryplace::Placed>> onCart=std::vector<mod::carryplace::Placed>{};
            if(best){mod::puppet::Transform t;t.position=best->position;t.rotation=best->rotation;onCart=mod::carryplace::readCarrier(*sampler,best->carrier,manager,t);}
            auto events=cartPlaceTx.observe(onCart,core->connected()&&partnerMode(),now);
            for(auto& e:events)core->queueCargo(e);
            if(!events.empty())note(("CART this porter's cart carries "+std::to_string(onCart?onCart->size():0)+" cases -> partner\n").c_str());
        }
    }
    // --- end ds3 ---
    // Shared structures (structures.h): what this porter builds goes to the partner; theirs appear here as Net structures.
    // Upgrades (structures.h, LevelSync): event type 15, item = level, ref = the structure's ref.
    static constexpr uint8_t LevelWire=15;
    mod::structures::LevelSync levelSync;mod::structures::MaterialSync materialSync;mod::roads::Levels levelApi;std::vector<ds::CargoEvent> levelIn;std::map<uint64_t,std::pair<uint32_t,uint64_t>> levelWants;bool levelTried=false,levelTell=false;
    mod::structures::Sync structSync{mod::structures::clockNow()};uint64_t structReadAt=0,structSent=0,structMade=0,structIdBase=0,imageSize=0,structPorter=0,structSettled=0;bool structTimed=false,structLinked=false,structTell=false;
    void tickStructures(uint64_t now) {
        if(!core||!sampler)return;   // like tickCargo: watched while the partner is away, sent when it connects
        bool coop=partnerMode()&&construct;
        // Incoming structures arrive with the cargo events (type 3); tickCargo hands them over in structIn.
        uint64_t manager=0;std::vector<mod::structures::Record> records;
        auto table=[&]()->const std::vector<mod::structures::Record>&{
            if(records.empty()&&sampler->read(sampler->base+0x7bf4960,&manager,8)&&manager){
                if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
                auto t0=mod::nowUs();records=mod::structures::readTable(*sampler,manager,sampler->base,imageSize);
                if(!structTimed){structTimed=true;std::ostringstream o;o<<"STRUCT table read: "<<records.size()<<" records, "<<(mod::nowUs()-t0)/1000.<<" ms\n";note(o.str().c_str());}}
            return records;};
        for(auto& e:structIn) {
            if(!coop){note("STRUCT partner structure ignored (not in peer mode)\n");continue;}
            std::array<double,3> game{e.position[0],e.position[2],e.position[1]};   // adapter -> game axes
            auto b=mod::structures::unpack(e.item,game,e.yaw);auto net=mod::structures::netVariant(b.type);
            if(!net){note(("STRUCT partner type "+std::to_string(b.type)+" has no Net variant; ignored\n").c_str());continue;}
            netCopies.built(e.ref,*net,b.position);
#ifndef MOD_MUTANT_STRUCTURE_TWICE
            // Already here: as the partner's copy from an earlier session, or as this porter's own (two players on
            // copies of one save both have it).
            if(mod::structures::standing(table(),*net,b.position)||mod::structures::standing(table(),b.type,b.position)){note(("STRUCT partner structure ref "+std::to_string(e.ref)+" already stands here\n").c_str());continue;}
#endif
            mod::puppet::Transform t;t.position=b.position;t.rotation=mod::structures::rotation(b.yaw,b.pitch,b.roll);
            // Through the request without the wait for a build effect, so heavy structures stand at once (double.cpp);
            // the export (which sets that wait) only where the request functions were not recognised.
            if(constructRequest)constructRequest(*net,t,{});else construct(*net,t,false);
            ++structMade;
            std::ostringstream o;o<<"STRUCT created partner type "<<int(b.type)<<" as "<<int(*net)<<" ref "<<e.ref<<" at "<<b.position[0]<<","<<b.position[1]<<","<<b.position[2]<<"\n";note(o.str().c_str());
        }
        structIn.clear();
        if(!coop)levelIn.clear();
        if(!coop||now-structReadAt<500)return;
        structReadAt=now;
        if(!structIdBase)structIdBase=(nonce()>>32)<<20;
        records.clear();if(table().empty())return;
        // On joining, and after a save was loaded: the structures the save already has go to the partner too
        // (share_saved_structures); the partner skips those that already stand there.
        { auto anchor=anchorNow();bool linked=core->connected();
          if(anchor.entity!=structPorter){structPorter=anchor.entity;structSettled=now+10000;structTell=true;}
          if(linked&&!structLinked)structTell=true;
          structLinked=linked;
          if(structTell&&linked&&structPorter&&now>=structSettled){
              structTell=false;levelTell=true;
              if(config.shareSavedStructures){
                  auto all=structSync.everything(records);
                  for(auto& b:all){ds::CargoEvent e;e.type=3;e.item=mod::structures::pack(b);e.ref=structIdBase+b.id;e.yaw=b.yaw;e.position={b.position[0],b.position[2],b.position[1]};core->queueCargo(e);++structSent;}
                  std::ostringstream o;o<<"STRUCT this world's "<<all.size()<<" structures -> partner\n";note(o.str().c_str());
              }
          } }
        // Upgrades: a structure that rose a level here (this porter's, or the partner's copy) goes to the partner;
        // what the partner reports is raised here once the structure stands, with the game's own routine for
        // materials from other players.
        if(!levelTried){levelTried=true;try{levelApi=mod::roads::nativeLevels(*sampler);}catch(const std::exception& e){note((std::string("STRUCT upgrades unavailable: ")+e.what()+"\n").c_str());}}
        if(levelApi.raise){
            std::vector<mod::structures::LevelSync::Level> seen;std::map<uint64_t,const mod::structures::Record*> byRef;
            for(auto& r:records){
                if(r.state!=1&&r.state!=2)continue;                       // built (in or out of range)
                std::optional<uint64_t> ref;
                if(r.id>=mod::structures::PlayerFirst&&r.id<mod::structures::NetFirst)ref=structIdBase+r.id;
                else if(r.id>=mod::structures::NetFirst)ref=netCopies.refAt(r.type,r.at.position);
                if(!ref||r.level<1||r.level>mod::structures::LevelSync::MaxLevel)continue;
                seen.push_back({*ref,r.level});byRef[*ref]=&r;
            }
            auto tell=[&](const mod::structures::LevelSync::Level& l){ds::CargoEvent e;e.type=LevelWire;e.item=l.level;e.ref=l.ref;core->queueCargo(e);++structSent;};
            // Materials delivered here (item 100 + kind, the amount in the position) go to the same structure there.
            { std::vector<mod::structures::MaterialSync::Have> have;for(auto& [ref,r]:byRef)have.push_back({ref,r->own});
              for(auto& d:materialSync.observe(have)){
                  ds::CargoEvent e;e.type=LevelWire;e.item=100u+d.kind;e.ref=d.ref;e.position={double(d.amount),0,0};core->queueCargo(e);++structSent;
                  std::ostringstream o;o<<"STRUCT "<<d.amount<<" of material "<<int(d.kind)<<" delivered to ref "<<d.ref<<" here -> partner\n";note(o.str().c_str());}
              for(auto it=levelIn.begin();it!=levelIn.end();){
                  if(it->item<100){++it;continue;}
                  mod::structures::MaterialSync::Delivered d{it->ref,uint8_t(it->item-100),int32_t(it->position[0])};auto f=byRef.find(d.ref);
                  if(mod::structures::MaterialSync::wanted(d)&&f!=byRef.end()&&levelApi.give){
                      mod::roads::Set s{};s[d.kind]=d.amount;levelApi.give(f->second->object,s);
                      std::ostringstream o;o<<"STRUCT partner delivered "<<d.amount<<" of material "<<int(d.kind)<<" to ref "<<d.ref<<": counted here\n";note(o.str().c_str());}
                  it=levelIn.erase(it);
              } }
            for(auto& l:levelSync.observe(seen)){tell(l);std::ostringstream o;o<<"STRUCT ref "<<l.ref<<" reached level "<<l.level<<" here -> partner\n";note(o.str().c_str());}
            if(levelTell){levelTell=false;for(auto& l:seen)if(l.level>=2)tell(l);}
            for(auto& e:levelIn)levelWants[e.ref]={e.item,now+60000};
            for(auto it=levelWants.begin();it!=levelWants.end();){
                auto f=byRef.find(it->first);
                if(f==byRef.end()){if(now>it->second.second)it=levelWants.erase(it);else ++it;continue;}   // the copy is still being built
                mod::structures::LevelSync::Level l{it->first,it->second.first};
                if(mod::structures::LevelSync::wanted(f->second->level,l)){
                    levelSync.applied(l);auto did=levelApi.raise(f->second->object,l.level);
                    std::ostringstream o;o<<"STRUCT partner's structure ref "<<l.ref<<" is level "<<l.level<<", here "<<f->second->level<<": "<<did<<"\n";note(o.str().c_str());
                }
                it=levelWants.erase(it);
            }
        }
        levelIn.clear();
        for(auto& b:structSync.observe(records)) {
            ds::CargoEvent e;e.type=3;e.item=mod::structures::pack(b);e.ref=structIdBase+b.id;e.yaw=b.yaw;
            e.position={b.position[0],b.position[2],b.position[1]};   // game -> adapter axes
            core->queueCargo(e);++structSent;
            std::ostringstream o;o<<"STRUCT built type "<<int(b.type)<<" id "<<b.id<<" at "<<b.position[0]<<","<<b.position[1]<<","<<b.position[2]<<" -> partner\n";note(o.str().c_str());
        }
        // The partner dismantled a structure: once our Net copy stands, remove it (Net only, a 3 m box around it).
        for(auto& at:netCopies.due(records,now)) {
            if(!unconstruct)break;
            mod::puppet::Transform t;t.position=at;t.rotation={1,0,0,0,1,0,0,0,1};
            unconstruct({-1.5f,-1.5f,-3,0},{1.5f,1.5f,3,0},t,false,false,true);++structRemoved;
            std::ostringstream o;o<<"STRUCT partner removed at "<<at[0]<<","<<at[1]<<","<<at[2]<<"\n";note(o.str().c_str());
        }
        for(auto& rm:structSync.takeRemoved()) {
            ds::CargoEvent e;e.type=4;e.ref=structIdBase+rm.id;e.item=rm.type;e.position={rm.position[0],rm.position[2],rm.position[1]};   // game -> adapter axes
            core->queueCargo(e);
            note(("STRUCT dismantled id "+std::to_string(rm.id)+" -> partner\n").c_str());
        }
    }
    mod::structures::NetCopies netCopies;uint64_t structRemoved=0;
    std::vector<ds::CargoEvent> structIn;
    // Shared orders (orders.h): opt-in share_orders. Standard orders only.
    mod::orders::UnitSync unitSync;
    mod::orders::Sync orderSync;std::vector<ds::CargoEvent> orderIn;std::function<bool(uint64_t,uint8_t)> orderRequest;std::map<uint64_t,uint8_t> orderDetails;
    uint64_t orderReadAt=0,orderSent=0,orderApplied=0;bool orderTried=false,orderLinked=false;
    // The story (opt-in experiments): share_story_orders widens the order scope one way, host to guest (orders.h);
    // story_resync moves the host's fresh checkpoint to the guest and loads it there (story.h).
    mod::orders::StoryWatch storyWatch;uint64_t storyWatchAt=0;
    std::unique_ptr<mod::story::Host> storyHost;std::unique_ptr<mod::story::Guest> storyGuest;mod::story::Serve storyServe;std::unique_ptr<mod::story::Fetch> storyFetch;
    std::vector<ds::CargoEvent> storyIn;std::function<void()> storySave;std::function<int(uint8_t,uint8_t)> storyLoad;
    std::optional<mod::story::Account> storyAccount;std::optional<mod::story::Stamp> storyNewest;std::filesystem::path storyFolder,storyCommand;
    bool storyTried=false,storyKeyCommand=false;uint64_t storyLookAt=0,storyPollAt=0,storyWatchUntil=0,storyWatchNext=0,storyChunks=0;int storyPercent=-1;mod::marker::KeyEdge storyEdge;
    uint32_t storyOfferGeneration=0;uint64_t storyOfferUntil=0;   // a written checkpoint still to be read and offered
    unsigned guardNoted=0,cullNoted=0,walkNoted=0;
    // The partner body's queue of deferred rep calls (game_offsets.md, "Deferred rep calls"): what the mod's own
    // calls put there in a tick is run at the end of that tick with the game's own flush, so no node waits for a
    // rep update the body may not get before the game resets the arena the nodes live in.
    struct RepWatch { uint64_t entity=0,entryEntity=0,entryHead=0,flushed=0,waiting=0; };
    RepWatch repBody,repSelf;uint64_t repWatch=0,repNoted=0,repNotedAt=0;bool repTried=false;std::function<void(uint64_t)> repFlush;
    uint64_t repQueue(uint64_t entity,uint64_t* repOut=nullptr){
        uint64_t rep=0,head=0;if(!entity||!sampler||!sampler->read(entity+0x60,&rep,8)||!rep||!sampler->read(rep+0x228,&head,8))return 0;
        if(repOut)*repOut=rep;
        return head;
    }
    // The game's culling pass (game_offsets.md, "Culling job and a drawable that left the tree") walks its tree with no
    // lock, and a drawable moved meanwhile can end up listed with no place. The mod moves and attaches entities in
    // its tick, so the tick first waits (at most 4 ms) for a pass that is running to end: manager
    // *(image+0x578c130), +0x8 jobs running, +0x3c pass active.
    uint64_t cullTicks=0,cullBusy=0,cullGaveUp=0,cullNotedAt=0;
    void cullQuiet(uint64_t now){
        uint64_t manager=0,vtable=0;
        if(!sampler||!sampler->read(sampler->base+0x578c130,&manager,8)||!manager||!sampler->read(manager,&vtable,8)||vtable!=sampler->base+0x3c0e780)return;
        auto busy=[&]{uint8_t pass=0;uint32_t jobs=0;return sampler->readOnce(manager+0x3c,&pass,1)&&sampler->readOnce(manager+8,&jobs,4)&&(pass||jobs);};
        ++cullTicks;
        if(busy()){++cullBusy;auto t0=mod::nowUs();while(busy()){if(mod::nowUs()-t0>4000){++cullGaveUp;break;}std::this_thread::yield();}}
        if(now-cullNotedAt>=60000&&cullBusy){cullNotedAt=now;std::ostringstream o;o<<"CULL a pass was running when the tick began "<<cullBusy<<" of "<<cullTicks<<" times; not over within 4 ms "<<cullGaveUp<<" times\n";note(o.str().c_str());}
    }
    void repTickStart(){
        if(!sampler)return;
        // The partner body and this porter: the mod calls the game on both from its tick.
        repBody.entity=repWatch;repSelf.entity=anchorNow().entity;
        for(auto* w:{&repBody,&repSelf}){w->entryEntity=w->entity;w->entryHead=repQueue(w->entity);if(w->entryHead)++w->waiting;}
    }
    // The body is made from the player's resource, so it carries the player's inventory component
    // (DSPlayerAKukanInventoryComponent, vtable 0x3d7eec8), whose update, while its "active" bit (+0x270 bit 0) is
    // set, sets shader variables on the entities a global names (this porter and his gear) from the body's own
    // update job: an unlocked write to another entity's queue of deferred calls from another worker, which is what
    // corrupted those queues (game_offsets.md, "Deferred rep calls"). The body has no inventory to show: the bit is
    // kept clear on the body.
    uint64_t kukanBody=0,kukanComponent=0,kukanCleared=0,kukanNoted=0,kukanNotedAt=0;
    void quietBodyInventory(uint64_t body,uint64_t now){quietInventoryOf(body,now,kukanBody,kukanComponent);}
    void quietInventoryOf(uint64_t body,uint64_t now,uint64_t& kukanBody,uint64_t& kukanComponent){
        if(!sampler||!body){kukanBody=0;kukanComponent=0;return;}
        uint64_t vtable=0;
        if(body!=kukanBody||!kukanComponent||!sampler->read(kukanComponent,&vtable,8)||vtable!=sampler->base+0x3d7eec8){
            kukanBody=body;kukanComponent=0;int32_t count=0;uint64_t list=0;
            if(!sampler->read(body+0x90,&count,4)||!sampler->read(body+0x98,&list,8)||!list||count<=0||count>512)return;
            std::vector<uint64_t> parts;parts.resize(size_t(count));if(!sampler->read(list,parts.data(),parts.size()*8))return;
            for(auto part:parts){uint64_t v=0;if(part&&sampler->read(part,&v,8)&&v==sampler->base+0x3d7eec8){kukanComponent=part;break;}}
            if(!kukanComponent)return;
        }
        uint8_t state=0;if(!sampler->read(kukanComponent+0x270,&state,1)||!(state&1))return;
#ifdef _WIN32
        *reinterpret_cast<volatile uint8_t*>(kukanComponent+0x270)=uint8_t(state&~1u);
#endif
        ++kukanCleared;
        if(kukanCleared!=kukanNoted&&(kukanNoted<3||now-kukanNotedAt>=30000)){kukanNoted=kukanCleared;kukanNotedAt=now;
            note(("BODY the body's inventory component had been switched on by the game: switched off ("+std::to_string(kukanCleared)+" times so far)\n").c_str());}
    }
    // The game updates entities joined by update links one after another (a parent and its children are joined this
    // way) and other entities side by side on its workers. The body is a second entity of the player's kind: code it
    // runs in its own update reaches this porter's entities through the game's globals, and the game's queue of
    // deferred calls on an entity takes no lock, so side by side the two corrupted this porter's queues (bad
    // function pointers, then the game thread waiting for good; about one run in six of an order taken at a
    // terminal; game_offsets.md, "Deferred rep calls"). Joined to this porter with the game's own link call
    // (0x23f4040(owner+0x208, other, owner, kind 0), what SetParent uses) the body is never updated beside him.
    // Since 2026-11-20 the writer is named and skipped for the mod's bodies (repguard.h), so by default bodies are no
    // longer joined (body_update_link=true joins them again; it is the default only with body_rep_guard=false).
    uint64_t linkedBody=0,linkedPlayer=0;
    void linkBodyUpdate(uint64_t body,uint64_t player){
        if(!config.bodyUpdateLink||!sampler||!body||!player||(body==linkedBody&&player==linkedPlayer))return;
        linkedBody=body;linkedPlayer=player;
        if(joinUpdateGroup(body,player))note("BODY joined to this porter's update group\n");
    }
    bool joinUpdateGroup(uint64_t body,uint64_t player){
#ifdef _WIN32
        static const uint8_t want[16]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};uint8_t got[16]{};
        if(!sampler->read(sampler->base+0x23f4040,got,16)||std::memcmp(got,want,16)){note("BODY update link unavailable: the link function differs\n");return false;}
        reinterpret_cast<void(*)(uint64_t,uint64_t,uint64_t,uint32_t)>(sampler->base+0x23f4040)(player+0x208,body,player,0);
        return true;
#else
        (void)body;(void)player;return false;
#endif
    }
    // One tick of the crowd experiment (game thread, after the partner's body was driven).
    void tickCrowd(const mod::puppet::Anchor& anchor,const mod::puppet::Transform* goal,const ds::Animation* sample,bool fresh,double speed,uint32_t actions,uint64_t now){
        if(!crowdBodies||(!crowdWanted&&!crowdBodies->size()))return;
        uint64_t began=mod::nowUs();
        if(crowdTickAt&&now-crowdTickAt>crowdGapWorstMs)crowdGapWorstMs=now-crowdTickAt;
        crowdTickAt=now;
        // Rings of 6, 12, 18 ... bodies, 2 m apart, at the centre's height (flat ground is assumed). With no centre
        // (no partner shown and no readable porter) they wait hidden.
        std::vector<mod::puppet::Transform> places(crowdWanted);std::vector<mod::others::Want> wants(crowdWanted);
        bool centred=goal||(anchor.entity&&anchor.pose.available);
        mod::puppet::Transform centre;if(goal)centre=*goal;else if(centred)centre=mod::puppet::transform(anchor.pose);
        for(size_t i=0;i<crowdWanted;++i){
            size_t ring=1,first=0;while(i>=first+6*ring){first+=6*ring;++ring;}
            double angle=6.283185307179586*double(i-first)/double(6*ring),radius=2.0*double(ring);
            places[i]=centre;places[i].position[0]+=radius*std::cos(angle);places[i].position[1]+=radius*std::sin(angle);
            wants[i].key=i;if(centred){wants[i].goal=&places[i];wants[i].sample=sample;wants[i].fresh=fresh;wants[i].speed=speed;wants[i].actions=actions;}
        }
        crowdBodies->drive(wants,anchor,now);
        uint64_t took=mod::nowUs()-began;crowdSumUs+=took;++crowdTicks;if(took>crowdWorstUs)crowdWorstUs=took;
        if(now-crowdNotedAt>=5000){crowdNotedAt=now;std::ostringstream o;
            o<<"CROWD wanted "<<crowdWanted<<" made "<<crowdBodies->made()<<" driving "<<crowdBodies->driving()<<" given up "<<crowdBodies->givenUp()<<" tick avg "<<(crowdTicks?crowdSumUs/crowdTicks:0)<<" us worst "<<crowdWorstUs
             <<" us, longest gap between ticks "<<crowdGapWorstMs<<" ms over "<<crowdTicks<<" ticks\n";note(o.str().c_str());
            crowdSumUs=crowdTicks=crowdWorstUs=crowdGapWorstMs=0;}
    }
    // The game thread's last tick, for the watchdog on the mod's own thread (guard_win.h, breakEndlessWalk).
    std::atomic<uint64_t> gameTickUs{0};uint64_t ringTriedUs=0;unsigned ringNoted=0;
    void watchRing(uint64_t clockUs){
#ifdef _WIN32
        uint64_t last=gameTickUs.load(std::memory_order_relaxed);
        if(!last||clockUs<last||clockUs-last<2000000||clockUs-ringTriedUs<500000)return;
        ringTriedUs=clockUs;mod::guard::breakEndlessWalk();
        unsigned n=mod::guard::ringsBroken.load();
        if(n!=ringNoted){ringNoted=n;try{note(("GUARD the game thread was waiting for a walk of a dangling queue that did not end: the walk was sent to its end ("+std::to_string(n)+" so far)\n").c_str());}catch(...){}}
#else
        (void)clockUs;
#endif
    }
    void repTickEnd(uint64_t now){
        gameTickUs.store(mod::nowUs(),std::memory_order_relaxed);
        if(!repTried&&sampler){repTried=true;
#ifdef _WIN32
            static const uint8_t want[17]={0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x99,0x28,0x02,0x00,0x00};uint8_t got[17]{};
            if(sampler->read(sampler->base+0x237e240,got,17)&&!std::memcmp(got,want,17)){auto f=reinterpret_cast<void(*)(uint64_t)>(sampler->base+0x237e240);repFlush=[f](uint64_t rep){f(rep);};}
#endif
        }
        // Only what was queued during this tick (the queue was empty when the tick began, or the entity is new to
        // this tick): those nodes are fresh.
        repBody.entity=repWatch;
        for(auto* w:{&repBody,&repSelf}){
            uint64_t rep=0;
            if(repFlush&&w->entity&&(w->entity!=w->entryEntity||!w->entryHead)&&repQueue(w->entity,&rep)&&rep){repFlush(rep);++w->flushed;}
        }
        uint64_t sum=repBody.flushed+repBody.waiting+repSelf.flushed+repSelf.waiting;
        if(sum!=repNoted&&now-repNotedAt>=30000){repNoted=sum;repNotedAt=now;
            std::ostringstream o;o<<"REP deferred calls queued in the mod's tick were run at its end: body "<<repBody.flushed<<", porter "<<repSelf.flushed<<" times; found waiting when a tick began: body "<<repBody.waiting<<", porter "<<repSelf.waiting<<" times\n";note(o.str().c_str());}
    }
    void tickGuard() {
#ifdef _WIN32
        unsigned n=mod::guard::skipped.load();
        if(n!=guardNoted){
            for(unsigned i=guardNoted;i<n&&i<8;++i){
                auto& e=mod::guard::seen[i];uint64_t base=sampler?sampler->base:0;std::ostringstream o;o<<std::hex<<"GUARD hit "<<i<<": target 0x"<<e.target<<" vtable ";
                if(base&&e.vtable>=base&&e.vtable<base+0x8000000)o<<"ds.exe+0x"<<e.vtable-base;else o<<"0x"<<e.vtable;
                o<<" callers ds.exe+0x"<<(e.caller>base?e.caller-base:0)<<" ds.exe+0x"<<(e.caller2>base?e.caller2-base:0)<<"\n";note(o.str().c_str());
            }
            guardNoted=n;note(("GUARD a deferred call with a bad function was skipped ("+std::to_string(n)+" so far; "+std::to_string(mod::guard::abandoned.load())+" times the rest of that entity's queue was dropped)\n").c_str());}
        unsigned w=mod::guard::walkLeft.load()+mod::guard::abandoned.load();
        if(w!=walkNoted){walkNoted=w;std::ostringstream o;o<<"GUARD a dangling queue of deferred calls was left "<<w<<" times ("<<mod::guard::unwound.load()<<" from deeper frames); entities"<<std::hex;
            for(auto& q:mod::guard::queueEntity)if(uint64_t e=q.load())o<<" 0x"<<e;
            o<<"\n";note(o.str().c_str());}
        unsigned c=mod::guard::cullSkipped.load();
        if(c!=cullNoted&&(c<16||c-cullNoted>=1000||!cullNoted)){
            cullNoted=c;std::ostringstream o;o<<"GUARD the culling job met a drawable that had left the tree: skipped ("<<c<<" so far, "<<mod::guard::cullRepaired.load()<<" mended)";
            uint64_t base=sampler?sampler->base:0;
            for(auto& s:mod::guard::cullSeen)if(uint64_t at=s.object.load())o<<std::hex<<"; object 0x"<<at<<" vtable ds.exe+0x"<<(s.vtable>base?s.vtable-base:0)<<" index 0x"<<s.index<<" flags 0x"<<s.flags<<std::dec<<" x"<<s.hits.load();
            o<<"\n";note(o.str().c_str());}
#endif
    }
    void storySend(const std::string& d) {
#ifndef MOD_FAKE_NETWORK
        if(config.group)return;   // no partner (group play): the direct socket does not exist
        sendto(socket,d.data(),int(d.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
#else
        (void)d;
#endif
    }
    // On the network thread, under the mod's lock: the checkpoint's datagrams (launcher/saves.h) from the partner.
    void storyDatagram(const std::string& d) {
        if(storyHost){auto a=storyServe.answer(d);if(!a.empty()){storySend(a);++storyChunks;}}
        else if(storyFetch)storyFetch->take(d);
    }
    void storyPoll(uint64_t now) {
        if(!storyFetch||now<storyPollAt||!core||!core->connected())return;
        storyPollAt=now+20;for(auto& d:storyFetch->poll(16))storySend(d);
    }
    void storyTell(const mod::story::News& n){core->queueCargo(mod::story::encode(n));}
    std::string storyState(const mod::story::SaveState& s){
        std::ostringstream o;o<<"readable="<<s.readable<<" in_game="<<s.inGame<<" requests="<<s.requests<<" pending_type="<<int(s.pendingType)<<" module_state="<<int(s.module);
        if(storyNewest)o<<" newest="<<storyNewest->folder<<" size="<<storyNewest->size<<" time="<<storyNewest->time;else o<<" newest=none";
        return o.str();
    }
    void tickStory(uint64_t now) {
        if(!core||!sampler){storyIn.clear();return;}
        bool on=config.storyResync!="off";
        if((on||config.shareStoryOrders)&&cache.available&&now-storyWatchAt>=1000){
            storyWatchAt=now;
            for(auto& m:storyWatch.observe(mod::orders::readOrders(*sampler,sampler->base,orderDetails))){
                std::ostringstream o;o<<"ORDER story "<<std::hex<<m.now.id<<std::dec<<" (detail "<<int(m.now.detail)<<") state "<<m.was.state<<"->"<<m.now.state<<" result "<<int(m.was.result)<<"->"<<int(m.now.result)<<"\n";note(o.str().c_str());
            }
        }
        // The lab commands below also run without the resync (group play has no partner to resync with) while
        // puppet_probe is on, as the other puppet command files do: only while a command waits or a watch runs.
        if(!on&&!(config.puppetProbe&&(storyWatchUntil||std::filesystem::exists(storyCommand)))){storyIn.clear();return;}
        bool host=config.role=="host";
        if(!storyTried){
            storyTried=true;
            try{storyAccount=mod::story::account(storyFolder);}catch(const std::exception& e){note((std::string("STORY saves unavailable: ")+e.what()+"\n").c_str());}
            try{storySave=mod::story::nativeSave(*sampler);}catch(const std::exception& e){note((std::string("STORY save request unavailable: ")+e.what()+"\n").c_str());}
            try{storyLoad=mod::story::nativeLoad(*sampler);}catch(const std::exception& e){note((std::string("STORY load request unavailable: ")+e.what()+"\n").c_str());}
        }
        if(on&&!storyHost&&!storyGuest){
            bool automatic=config.storyResync=="auto";
            if(host)storyHost=std::make_unique<mod::story::Host>(automatic);else storyGuest=std::make_unique<mod::story::Guest>(automatic);
            note((std::string("STORY resync on: ")+(host?"host":"guest")+", "+config.storyResync+", key "+std::to_string(config.storyKey)+", save "+(storySave?"armed":"off")+", load "+(storyLoad?"armed":"off")+", account "+(storyAccount?std::to_string(storyAccount->id):std::string("unknown"))+"\n").c_str());
        }
        auto state=mod::story::readSaveState(*sampler,sampler->base);
        bool connected=core->connected(),inWorld=cache.available&&!cache.gated&&!inEvent&&state.readable&&state.inGame;
        // Lab commands, each one call alone: "save 1", "load <type*100+slot>" (100: manualsave0), "state 0", "key 1",
        // "watch <seconds>" (the save state twice a second).
        if(std::filesystem::exists(storyCommand)){
            auto text=readConfig(storyCommand);
            if(!std::filesystem::remove(storyCommand))throw std::runtime_error("cannot consume story command");
            auto stamp=mod::runStamp();
            if(mod::puppet::parseCargoCommand(text,stamp,"state")){if(storyAccount)storyNewest=mod::story::newest(storyAccount->folder);note(("STORY test: state "+storyState(state)+"\n").c_str());}
            else if(auto w=mod::puppet::parseCargoCommand(text,stamp,"watch");w&&*w<=600){storyWatchUntil=now+uint64_t(*w)*1000;storyWatchNext=0;}
            else if(mod::puppet::parseCargoCommand(text,stamp,"key")){storyKeyCommand=true;note("STORY test: catch-up key\n");}
            else if(mod::puppet::parseCargoCommand(text,stamp,"save")){
                if(!storySave||!inWorld)note((std::string("STORY test: save not asked (")+(storySave?"not in the world":"unavailable")+")\n").c_str());
                else{note(("STORY test: asking for a checkpoint; before: "+storyState(state)+"\n").c_str());storySave();
                     note(("STORY test: asked; after: "+storyState(mod::story::readSaveState(*sampler,sampler->base))+"\n").c_str());if(!storyWatchUntil)storyWatchUntil=now+90000;}
            }
            else if(auto l=mod::puppet::parseCargoCommand(text,stamp,"load");l&&*l<10000){
                if(!storyLoad||!inWorld||state.requests||state.pending)note((std::string("STORY test: load not asked (")+(!storyLoad?"unavailable":!inWorld?"not in the world":"the game is saving")+")\n").c_str());
                else{note(("STORY test: asking the game to load type "+std::to_string(*l/100)+" slot "+std::to_string(*l%100)+"; before: "+storyState(state)+"\n").c_str());
                     int r=storyLoad(uint8_t(*l/100),uint8_t(*l%100));
                     note(("STORY test: load result "+std::to_string(r)+" (0 not asked, 1 the game does not list that save, 2 asked); after: "+storyState(mod::story::readSaveState(*sampler,sampler->base))+"\n").c_str());if(!storyWatchUntil)storyWatchUntil=now+90000;}
            }
            else note("STORY test: command refused\n");
        }
        if(storyWatchUntil&&now>=storyWatchNext){
            storyWatchNext=now+500;if(now>=storyWatchUntil)storyWatchUntil=0;
            if(storyAccount)storyNewest=mod::story::newest(storyAccount->folder);
            note(("STORY watch: porter="+std::to_string(cache.available)+" event="+std::to_string(inEvent)+" "+storyState(state)+"\n").c_str());
        }
        if(!on){storyIn.clear();return;}
        std::vector<mod::story::News> news;
        for(auto& e:storyIn){if(auto n=mod::story::decode(e))news.push_back(*n);else note("STORY refused: malformed news\n");}
        storyIn.clear();
        if(storyHost){
            mod::story::HostIn in;in.now=now;in.connected=connected;in.inWorld=inWorld;in.saveBusy=state.requests||state.pending;in.episode=cache.story.episode;in.completed=storyWatch.completed();
            for(auto& n:news){
                if(n.what==mod::story::Ask)in.asked=true;
                else if(n.what==mod::story::Loaded)note(("STORY the partner loaded generation "+std::to_string(n.generation)+" ("+std::to_string(storyChunks)+" chunks served so far)\n").c_str());
                else if(n.what==mod::story::Failed)note(("STORY the partner could not load generation "+std::to_string(n.generation)+"\n").c_str());
            }
            if(storyAccount&&connected&&now-storyLookAt>=(storyHost->waiting()?500u:5000u)){storyLookAt=now;storyNewest=mod::story::newest(storyAccount->folder);}
            in.newest=storyNewest;
            auto out=storyHost->tick(in);
            for(auto& l:out.log)note((l+"\n").c_str());
            if(out.save){if(storySave)storySave();else note("STORY save request unavailable: nothing asked\n");}
            if(out.offer){storyOfferGeneration=out.generation;storyOfferUntil=now+20000;}
            if(storyOfferGeneration){
                // The checkpoint the game just wrote, and no other: the newest readable one could be an older save.
                // The game can still be writing the file, so the read is tried again for twenty seconds.
                std::optional<launcher::saves::Checkpoint> c;std::string why="the save folder is not known";
                if(storyAccount&&storyNewest)c=launcher::saves::named(storyAccount->folder,storyNewest->folder,&why);
                if(c)storyServe.offer(config.session,storyOfferGeneration,*c);
                if(c&&storyServe.news().what){storyOfferGeneration=0;storyTell(storyServe.news());note(("STORY offering "+c->folder+" to the partner, "+std::to_string(c->bytes.size())+" bytes\n").c_str());}
                else if(now>=storyOfferUntil){
                    storyOfferGeneration=0;storyHost->unreadable();
                    // What was there, for the record: the size and the first bytes (three decimal sizes lead a checkpoint).
                    std::string seen="unknown";
                    if(storyAccount&&storyNewest){std::error_code ec;auto file=storyAccount->folder/storyNewest->folder/"checkpoint.dat";auto size=std::filesystem::file_size(file,ec);
                        std::ifstream f(file,std::ios::binary);char head[24]{};f.read(head,sizeof head);std::ostringstream o;o<<(ec?0:size)<<" bytes, head";
                        for(std::streamsize i=0;i<f.gcount();++i)o<<' '<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(static_cast<unsigned char>(head[i]));seen=o.str();}
                    note(("STORY the checkpoint could not be read ("+(c?std::string("too large or empty"):why)+"; file "+seen+"): nothing offered\n").c_str());}
            }
        }
        if(storyGuest){
            mod::story::GuestIn in;in.now=now;in.connected=connected;in.inWorld=inWorld;in.porter=cache.available;in.saveBusy=state.requests||state.pending;in.news=news;
            in.key=storyEdge.pressed(connected&&config.storyKey&&modKey(config.storyKey),now)||storyKeyCommand;storyKeyCommand=false;
            in.fetched=storyFetch&&storyFetch->done();
            if(storyFetch&&!in.fetched){int p=storyFetch->percent()/25*25;if(p!=storyPercent){storyPercent=p;note(("STORY fetch "+std::to_string(p)+"%\n").c_str());}}
            auto out=storyGuest->tick(in);
            for(auto& l:out.log)note((l+"\n").c_str());
            if(out.ask)storyTell({mod::story::Ask,1,0,0});
            if(out.fetch){storyFetch=std::make_unique<mod::story::Fetch>(config.session,*out.fetch);storyPercent=-1;storyPollAt=0;}
            if(out.notice&&notices)notices->event(mod::notice::Kind::StoryOffer,partnerName,now);
            if(out.install){
                bool ok=false;
                try{if(!storyAccount||!storyFetch)throw std::runtime_error("no account folder");mod::story::install(*storyAccount,storyFetch->bytes());ok=true;note("STORY the partner's checkpoint is in place as manualsave0 (re-signed for this account)\n");}
                catch(const std::exception& e){note((std::string("STORY install failed: ")+e.what()+"\n").c_str());}
                storyGuest->installed(ok);
                if(!ok){storyFetch.reset();if(notices)notices->event(mod::notice::Kind::StoryFailed,partnerName,now);}
            }
            if(out.load){
                int r=storyLoad?storyLoad(mod::story::LoadType,mod::story::LoadSlot):-1;
                note(("STORY load request result "+std::to_string(r)+" (2 asked, 1 the game does not list the save, 0 not asked, -1 unavailable); "+storyState(mod::story::readSaveState(*sampler,sampler->base))+"\n").c_str());
                if(r!=2){auto refused=storyGuest->refused(now);if(refused.tell)out.tell=refused.tell;note("STORY load refused\n");}
            }
            if(out.tell){
                if(connected)storyTell(*out.tell);
                storyFetch.reset();
                if(out.tell->what==mod::story::Loaded){
                    // A new world: what the order sync knew is the old one's. It learns again; nothing of the load is told as news.
                    orderSync.reset();unitSync=mod::orders::UnitSync();storyWatch.reset();
                    if(notices)notices->event(mod::notice::Kind::StoryLoaded,partnerName,now);
                }
                else if(notices)notices->event(mod::notice::Kind::StoryFailed,partnerName,now);
            }
        }
    }
    // A unit of an order marked delivered here as the game's own hand-in leaves its row (the packed word and the cargo's
    // two condition values, then the state; only while the row still says not delivered), and this game's copy of that
    // cargo lying in the world removed (never one on the porter). For the partner's hand-ins (tickOrders) and a party's
    // (tickGroupOrders). Returns how many cargo records were removed.
    unsigned deliverUnitHere(const mod::orders::Unit& u,uint32_t packed,float l0,float l1){
#ifdef _WIN32
        uint16_t seen=0;
        if(sampler->read(u.stateAt,&seen,2)&&seen==mod::orders::NotDelivered){
            std::memcpy(reinterpret_cast<void*>(u.stateAt+4),&packed,4);std::memcpy(reinterpret_cast<void*>(u.stateAt+0x10),&l0,4);std::memcpy(reinterpret_cast<void*>(u.stateAt+0x14),&l1,4);
            *reinterpret_cast<uint16_t*>(u.stateAt)=mod::orders::Delivered;
        }
#else
        (void)packed;(void)l0;(void)l1;
#endif
        unsigned removed=0;uint64_t manager=0;
        if(cargoDelete&&cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
            for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords))
                if(r.mission==u.mission&&r.unit==u.unit&&mod::cargo::location(r)==mod::cargo::InWorld){cargoDelete(r.index);++removed;}
        return removed;
    }
    void tickOrders(uint64_t now) {
        if(!core||!sampler||!config.shareOrders){orderIn.clear();return;}
        if(!orderTried){orderTried=true;try{orderRequest=mod::orders::nativeRequest(*sampler);}catch(const std::exception& e){note((std::string("ORDER unavailable: ")+e.what()+"\n").c_str());}}
        bool linked=core->connected()&&partnerMode();
        // Story orders travel one way: the host tells, the guest takes (share_story_orders).
        orderSync.scope.storyOut=config.shareStoryOrders&&config.role=="host";
        orderSync.scope.storyIn=config.shareStoryOrders&&(config.role=="guest"||config.role=="client");
        if(!linked&&orderLinked)orderSync.reset();   // the next partner is told what is under way
        orderLinked=linked;
        if(!orderRequest||!linked){orderIn.clear();return;}
        if(orderIn.empty()&&now-orderReadAt<1000)return;
        orderReadAt=now;
        auto orders=mod::orders::readOrders(*sampler,sampler->base,orderDetails);if(orders.empty())return;
        static const char* names[]={"","start","success","fail"};
        std::vector<mod::orders::Unit> units;if(config.shareOrderUnits)units=mod::orders::readUnits(*sampler,sampler->base);
        for(auto& e:orderIn){
            if((e.item&0xffff)>=mod::orders::UnitBase&&(e.item&0xffff)<mod::orders::UnitBase+64){
                // The partner handed a unit in: it is delivered here too, and this game's copy of that cargo goes.
                uint8_t n=uint8_t((e.item&0xffff)-mod::orders::UnitBase);
                if(!config.shareOrderUnits)continue;
                bool mine=false;for(auto& o:orders)if(o.id==e.ref)mine=mod::orders::takes(orderSync.scope,o);
                if(!mine){std::ostringstream o;o<<"ORDER partner handed in unit "<<int(n)<<" of "<<std::hex<<e.ref<<std::dec<<": not an order taken from the partner here\n";note(o.str().c_str());continue;}
                auto* u=unitSync.wanted(units,e.ref,n);
                std::ostringstream o;o<<"ORDER partner handed in unit "<<int(n)<<" of "<<std::hex<<e.ref<<std::dec;
                if(!u){o<<": not waiting here\n";note(o.str().c_str());continue;}
                unitSync.applied(e.ref,n);
                const uint32_t packed=(uint32_t(e.position[0])&0xffff)|((e.item>>16)<<16);
                const unsigned removed=deliverUnitHere(*u,packed,float(e.position[1]),float(e.position[2]));
                o<<": marked delivered, "<<removed<<" cargo record(s) removed\n";note(o.str().c_str());continue;
            }
            mod::orders::Change c{e.ref,uint8_t(e.item)};if(c.what<1||c.what>3)continue;
            if(!orderSync.wanted(orders,c)){std::ostringstream o;o<<"ORDER partner's "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<" does not apply here";
                for(auto& x:orders)if(x.id==c.id)o<<" (detail "<<int(x.detail)<<", state "<<x.state<<", result "<<int(x.result)<<")";   // a story order that does not fit: the two stories differ
                o<<"\n";note(o.str().c_str());continue;}
            bool ok=orderRequest(c.id,c.what);if(ok){orderSync.applied(c);++orderApplied;}
            if(ok&&config.shareOrderLoad){if(c.what==mod::orders::Start)orderLoads.started(c.id,true,now);else orderLoads.ended(c.id);}
            std::ostringstream o;o<<"ORDER partner: "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<(ok?" requested":" refused by the game")<<"\n";note(o.str().c_str());
        }
        if(!orderIn.empty()){orderIn.clear();return;}   // the game runs the requests on its next update: look again then
        std::vector<uint64_t> under;for(auto& o:orders)if(mod::orders::shareable(o)&&o.state==mod::orders::Progress)under.push_back(o.id);
        if(config.shareOrderLoad){cargoSync.shareMissions(under);orderLoads.underWay(under,now);}   // their cases are shared cargo that stays order cargo (tickCargo)
        if(config.shareOrderUnits){
            std::vector<uint64_t> underSent;for(auto& o:orders)if(mod::orders::sends(orderSync.scope,o)&&o.state==mod::orders::Progress)underSent.push_back(o.id);
            for(auto& u:unitSync.observe(units,underSent)){
                ds::CargoEvent e;e.type=mod::orders::WireType;e.item=uint32_t(mod::orders::UnitBase+u.unit);e.ref=u.mission;
                // The wire keeps positions within 100 km: the packed word travels as its low half there and its high
                // half in the item's upper bits; the two condition values are small numbers (a float is exact in a double).
                e.item|=(u.packed>>16)<<16;
                auto fits=[](float v){return std::isfinite(v)&&std::fabs(v)<=100000.f?double(v):0.0;};
                e.position={double(u.packed&0xffff),fits(u.life[0]),fits(u.life[1])};
                core->queueCargo(e);++orderSent;
                std::ostringstream o;o<<"ORDER unit "<<int(u.unit)<<" of "<<std::hex<<u.mission<<std::dec<<" handed in here -> partner\n";note(o.str().c_str());
            }
        }
        for(auto& c:orderSync.observe(orders)){
            ds::CargoEvent e;e.type=mod::orders::WireType;e.item=c.what;e.ref=c.id;core->queueCargo(e);++orderSent;
            if(config.shareOrderLoad&&c.what!=mod::orders::Start)orderLoads.ended(c.id);
            std::ostringstream o;o<<"ORDER "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<" here -> partner\n";note(o.str().c_str());
        }
    }
    // Shared rewards (rewards.h): opt-in share_rewards, with share_orders. A facility's connection points that rise
    // here are told to the partner as a total; his total is topped up here with the game's own routine.
    mod::rewards::Sync rewardSync;std::vector<ds::CargoEvent> rewardIn;mod::rewards::Api rewardApi;std::map<uint32_t,mod::rewards::News> rewardPending;
    uint64_t rewardReadAt=0,rewardSent=0,rewardApplied=0,rewardPorter=0,rewardSettled=0;bool rewardTried=false,rewardLinked=false;
    static std::string rewardLine(const mod::rewards::Facility& f){
        std::ostringstream o;o<<"facility "<<std::hex<<f.hash<<std::dec<<" points "<<f.points<<" level "<<f.level<<" (floor "<<f.floor<<", cap "<<f.cap<<")";return o.str();
    }
    // What holds a level below what the points have reached: the game's level cap for the facility (readHighest).
    static std::string rewardCap(const mod::rewards::Facility& f,int32_t points){
        std::ostringstream o;o<<"level cap ";if(f.highest==mod::rewards::Unknown)o<<"unknown";else o<<f.highest;
        o<<", points alone give level "<<mod::rewards::levelOf(f,points,mod::rewards::MaxLevel);return o.str();
    }
    std::string rewardCapNow(const mod::rewards::Facility& f,int32_t points) const {
        std::vector<mod::rewards::Facility> one{f};mod::rewards::readHighest(*sampler,sampler->base,one);return rewardCap(one[0],points);
    }
    void tickRewards(uint64_t now) {
        if(!core||!sampler||!config.shareRewards){rewardIn.clear();return;}
        if(!rewardTried){
            rewardTried=true;try{rewardApi=mod::rewards::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("REWARD add unavailable: ")+e.what()+"\n").c_str());}
            note((std::string("REWARD sharing on: add ")+(rewardApi.add?"armed":"off (read and log only)")+"\n").c_str());
        }
        // Another world (a save loaded): every total jumps; learn them again once it has settled.
        auto anchor=anchorNow();
        if(anchor.entity!=rewardPorter){rewardPorter=anchor.entity;rewardSync.forget();rewardPending.clear();rewardSettled=now+8000;}
        bool linked=core->connected()&&partnerMode();
        if(linked!=rewardLinked){rewardSync.unlinked();rewardPending.clear();}
        rewardLinked=linked;
        for(auto& e:rewardIn){
            auto n=mod::rewards::decode(e);
            if(!n){note("REWARD refused: malformed news\n");continue;}
            if(!linked)continue;
            auto& p=rewardPending[n->hash];if(n->points>p.points)p=*n;     // the highest total heard counts
        }
        rewardIn.clear();
        if(!rewardPorter||now<rewardSettled||now-rewardReadAt<1000)return;
        rewardReadAt=now;
        auto facilities=mod::rewards::readFacilities(*sampler,sampler->base);if(facilities.empty())return;
        // First what rose here (so a delivery here is never mistaken for a top-up), then the partner's totals.
        for(auto& n:rewardSync.observe(facilities)){
            if(!linked)continue;
            core->queueCargo(mod::rewards::encode(n));++rewardSent;
            std::ostringstream o;o<<"REWARD facility "<<std::hex<<n.hash<<std::dec<<" rose here to "<<n.points<<" points (level "<<n.level<<") -> partner\n";note(o.str().c_str());
        }
        if(rewardPending.empty()||inEvent||!cache.available)return;       // a top-up waits for the porter to be in the world
        for(auto& [hash,n]:rewardPending){
            rewardSync.heard(n);
            const mod::rewards::Facility* local=nullptr;for(auto& f:facilities)if(f.hash==hash)local=&f;
            std::ostringstream o;o<<"REWARD partner's facility "<<std::hex<<hash<<std::dec<<" is at "<<n.points<<" points (level "<<n.level<<"): ";
            if(!local){o<<"unknown here\n";note(o.str().c_str());continue;}
            int32_t owed=mod::rewards::Sync::owed(*local,n);
            if(owed<=0){o<<"nothing to add (here "<<local->points<<", level "<<local->level<<")\n";note(o.str().c_str());continue;}
            if(!rewardApi.add){o<<owed<<" short here, not added (add unavailable)\n";note(o.str().c_str());continue;}
            bool ok=rewardApi.add(local->resource,owed);
            int32_t after[2]{local->points,local->level};sampler->read(local->record+8,after,8);
            if(ok)++rewardApplied;
            o<<owed<<(ok?" added here":" refused by the game")<<", "<<local->points<<" -> "<<after[0]<<" points, level "<<local->level<<" -> "<<after[1];
            if(after[1]<n.level)o<<" ("<<rewardCapNow(*local,after[0])<<")";     // behind the partner's level: say what the game holds it to
            o<<"\n";note(o.str().c_str());
        }
        rewardPending.clear();
    }
    // Shared roads (roads.h): opt-in share_roads. What this player delivers to a paver goes to the partner's paver
    // as delivered by others; a road built here is completed there.
    mod::roads::Sync roadSync;std::vector<ds::CargoEvent> roadIn;mod::roads::Api roadApi;uint64_t roadReadAt=0,roadSent=0,roadApplied=0,roadPorter=0,roadSettled=0;bool roadTried=false,roadLinked=false,roadTell=false;
    void tickRoads(uint64_t now) {
        if(!core||!sampler||!config.shareRoads){roadIn.clear();return;}
        if(!roadTried){roadTried=true;try{roadApi=mod::roads::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("ROAD unavailable: ")+e.what()+"\n").c_str());}}
        // Another world (a save loaded): the pavers' numbers jump; learn them again once it has settled.
        auto anchor=anchorNow();
        if(anchor.entity!=roadPorter){roadPorter=anchor.entity;roadSync.forget();roadSettled=now+8000;roadTell=true;}
        bool linked=core->connected()&&partnerMode();if(linked&&!roadLinked)roadTell=true;
        roadLinked=linked;
        if(!roadApi.give||!roadPorter||now<roadSettled||now-roadReadAt<1000)return;
        roadReadAt=now;
        auto pavers=mod::roads::readPavers(*sampler,sampler->base);
        // On joining (and after a save was loaded) each side tells the other all its pavers hold: both worlds get
        // the roads of either.
        if(roadTell&&linked&&!pavers.empty()){
            roadTell=false;auto all=mod::roads::Sync::everything(pavers);
            for(auto& n:all){ds::CargoEvent e;e.type=mod::roads::WireType;e.item=n.item;e.ref=n.id;e.position={double(n.amount),0,0};core->queueCargo(e);++roadSent;}
            std::ostringstream o;o<<"ROAD this world's pavers -> partner ("<<all.size()<<" entries)\n";note(o.str().c_str());
        }
        // First what this game saw (so a delivery here is never mistaken for the partner's), then the partner's news.
        for(auto& n:roadSync.observe(pavers)){
            if(!core->connected()||!partnerMode())continue;
            ds::CargoEvent e;e.type=mod::roads::WireType;e.item=n.item;e.ref=n.id;e.position={double(n.amount),0,0};core->queueCargo(e);++roadSent;
            std::ostringstream o;if(n.item==mod::roads::Built)o<<"ROAD built at paver "<<n.id<<" here -> partner\n";else o<<"ROAD "<<n.amount<<" of material "<<n.item-mod::roads::Material<<" delivered to paver "<<n.id<<" here -> partner\n";
            note(o.str().c_str());
        }
        for(auto& e:roadIn){
            mod::roads::News n{uint32_t(e.ref),e.item,int32_t(e.position[0])};
            auto* p=mod::roads::Sync::wanted(pavers,n);if(!p)continue;
            std::ostringstream o;
            if(n.item==mod::roads::Built){roadSync.applied(n.id);roadApi.complete(p->address);o<<"ROAD partner's road at paver "<<n.id<<" is built: completed here\n";}
            else if(n.item>=mod::roads::Total){
                // Read again: an earlier entry of the same batch may have changed this paver.
                mod::roads::Paver fresh=*p;if(!sampler->read(p->address+0x210,fresh.own.data(),sizeof fresh.own)||!sampler->read(p->address+0x248,fresh.net.data(),sizeof fresh.net))continue;
                int32_t owed=mod::roads::Sync::owed(fresh,n);if(owed<=0)continue;
                mod::roads::Set s{};s[n.item-mod::roads::Total]=owed;roadSync.applied(n.id);roadApi.give(p->address,s);
                o<<"ROAD partner's paver "<<n.id<<" holds "<<n.amount<<" of material "<<n.item-mod::roads::Total<<": "<<owed<<" added here\n";
            }
            else{mod::roads::Set s{};s[n.item-mod::roads::Material]=n.amount;if(p->level<2)roadSync.applied(n.id);roadApi.give(p->address,s);
                o<<"ROAD partner delivered "<<n.amount<<" of material "<<n.item-mod::roads::Material<<" to paver "<<n.id<<": counted here\n";}
            ++roadApplied;note(o.str().c_str());
        }
        roadIn.clear();
    }
    // Shared voidouts (voidout.h), with share_enemies: a crater left in one world is added to the other.
    mod::voidout::Sync voidSync;std::vector<ds::CargoEvent> voidIn;std::function<uint32_t(const mod::voidout::Crater&)> voidAdd;
    uint64_t voidReadAt=0,voidPorter=0;bool voidTried=false;
    void tickVoidouts(uint64_t now){
        if(!core||!sampler||!config.shareEnemies){voidIn.clear();return;}
        if(!voidTried){voidTried=true;try{voidAdd=mod::voidout::nativeAdd(*sampler,[this](const std::string& row){note(row.c_str());});}catch(const std::exception& e){note((std::string("VOIDOUT unavailable: ")+e.what()+"\n").c_str());}}
        if(!voidAdd||(voidIn.empty()&&now-voidReadAt<1000))return;
        voidReadAt=now;
        auto anchor=anchorNow();if(anchor.entity!=voidPorter){voidPorter=anchor.entity;voidSync.forget();}
        auto table=mod::voidout::read(*sampler,sampler->base);
        for(auto& k:voidSync.observe(table)){
            if(!core->connected()||!partnerMode())continue;
            core->queueCargo(mod::voidout::encode(k));
            std::ostringstream o;o<<"VOIDOUT a crater appeared here (radius "<<k.radius<<" m) -> partner\n";note(o.str().c_str());
        }
        for(auto& e:voidIn){
            auto k=mod::voidout::decode(e);if(!k)continue;
            if(!mod::voidout::Sync::wanted(table,*k)){note("VOIDOUT the partner's crater is here already\n");continue;}
            if(auto id=voidAdd(*k)){voidSync.applied(id);std::ostringstream o;o<<"VOIDOUT the partner's crater added here (radius "<<k->radius<<" m)\n";note(o.str().c_str());}
            else note("VOIDOUT the partner's crater was refused (no crater resource yet)\n");
        }
        voidIn.clear();
    }
    // Shared BTs (bts.h), with share_enemies: gazers destroyed, a catcher fight started, its area cleared.
    mod::bts::GazerSync gazerSync;mod::bts::FightSync fightSync;bool btQuietTold=false;mod::bts::HealthSync healthSync;std::function<void(const mod::bts::Catcher&,float)> btHurt;std::vector<ds::CargoEvent> btIn;
    std::function<void(uint64_t)> btDestroy;std::function<void(uint8_t)> btCatcher;uint64_t btReadAt=0,btSent=0,btApplied=0,btPorter=0,btSettled=0;bool btTried=false,btLinked=false,btTell=false;
    void tickBts(uint64_t now) {
        if(!core||!sampler||!config.shareEnemies){btIn.clear();return;}
        if(!btTried){btTried=true;try{btDestroy=mod::bts::nativeDestroy(*sampler);btHurt=mod::bts::nativeHurt(*sampler);btCatcher=mod::bts::nativeCatcher(*sampler);}catch(const std::exception& e){note((std::string("BT unavailable: ")+e.what()+"\n").c_str());}}
        if(!btDestroy||!btCatcher||(btIn.empty()&&now-btReadAt<500))return;
        btReadAt=now;
        auto anchor=anchorNow();std::array<double,3> at{};bool here=anchor.entity&&anchor.pose.available;
        if(here){auto p=mod::puppet::transform(anchor.pose).position;at={double(p[0]),double(p[1]),double(p[2])};}
        auto gazers=mod::bts::readGazers(*sampler,sampler->base);auto fights=mod::bts::readFights(*sampler,sampler->base);
        bool send=core->connected()&&partnerMode();
        bool visiting=fightVisiting();   // the catcher fight here is a visited one (fights.h): not this world's to share
        // One health bar for the two games' catchers: first what this game's lost since the last look goes out, then
        // what the partner's lost is taken off here.
        auto catchers=fights.active>0?mod::bts::readCatchers(*sampler,sampler->base):std::vector<mod::bts::Catcher>{};
        for(auto& h:healthSync.observe(catchers)){
            if(!send||visiting)continue;
            ds::CargoEvent e;e.type=mod::bts::WireType;e.item=mod::bts::CatcherHurt;e.ref=h.ordinal;e.position={double(h.percent),0,0};core->queueCargo(e);++btSent;
            std::ostringstream o;o<<"BT catcher "<<h.ordinal<<" lost "<<h.percent<<" % here -> partner\n";note(o.str().c_str());
        }
        for(auto& e:btIn){
            if(visiting&&e.item!=mod::bts::GazerDead)continue;
            if(e.item==mod::bts::CatcherHurt){
                mod::bts::HealthSync::Hurt h{uint32_t(e.ref),float(e.position[0])};
                if(auto next=healthSync.applied(catchers,h)){btHurt(catchers[h.ordinal],*next);catchers[h.ordinal].percent=*next;++btApplied;
                    std::ostringstream o;o<<"BT partner took "<<h.percent<<" % off catcher "<<h.ordinal<<": now "<<*next<<" % here\n";note(o.str().c_str());}
                continue;
            }
            if(e.item==mod::bts::GazerDead){
                if(auto* g=mod::bts::GazerSync::wanted(gazers,e.ref)){gazerSync.applied(e.ref);btDestroy(g->address);++btApplied;std::ostringstream o;o<<"BT partner destroyed a gazer: gone here too (lod "<<g->lod<<")\n";note(o.str().c_str());}
                continue;
            }
            double d=1e9;if(here)d=std::sqrt((at[0]-e.position[0])*(at[0]-e.position[0])+(at[1]-e.position[1])*(at[1]-e.position[1]));
            if(!mod::bts::FightSync::wanted(fights,e.item,d))continue;
            // Not into a new fight while this porter is down or just lost one here (bts.h FightSync::resting).
            if(e.item==mod::bts::FightOn&&(vitalsSeen.dead||fightSync.resting(now))){
                if(!btQuietTold){btQuietTold=true;note("BT partner's fight not started here: this porter is down or just lost a fight\n");}continue;}
            btCatcher(e.item==mod::bts::FightOn?0:1);++btApplied;
            note(e.item==mod::bts::FightOn?"BT partner is in a catcher fight: it starts here too\n":"BT partner's catcher fight is over, area cleared: ended here too\n");
        }
        if(!btIn.empty()){btIn.clear();return;}                          // look again next time, after the game acted
        // On joining (and after a load, once it has settled): the gazers destroyed in this world go to the partner.
        if(anchor.entity!=btPorter){btPorter=anchor.entity;btSettled=now+10000;btTell=true;}
        if(send&&!btLinked)btTell=true;
        btLinked=send;
        if(btTell&&send&&btPorter&&now>=btSettled){
            btTell=false;auto all=mod::bts::GazerSync::everything(gazers);
            for(auto k:all){ds::CargoEvent e;e.type=mod::bts::WireType;e.item=mod::bts::GazerDead;e.ref=k;core->queueCargo(e);++btSent;}
            if(!all.empty()){std::ostringstream o;o<<"BT "<<all.size()<<" gazers are destroyed in this world -> partner\n";note(o.str().c_str());}
        }
        for(auto k:gazerSync.observe(gazers)){
            if(!send)continue;
            ds::CargoEvent e;e.type=mod::bts::WireType;e.item=mod::bts::GazerDead;e.ref=k;core->queueCargo(e);++btSent;note("BT a gazer was destroyed here -> partner\n");
        }
        if(!fightSync.resting(now))btQuietTold=false;
        for(auto w:fightSync.observe(fights,now)){
            if(!send||!here||visiting)continue;
            ds::CargoEvent e;e.type=mod::bts::WireType;e.item=w;e.position=at;core->queueCargo(e);++btSent;
            note(w==mod::bts::FightOn?"BT a catcher fight runs here -> partner\n":"BT a catcher fight ended here with the area cleared -> partner\n");
        }
    }
    // Fights with visitors (fights.h): config host_fights, join_fights, over the broadcast client's fight channel. Five
    // looks a second at this game's catcher fight (gazers only while owning or visiting one).
    std::unique_ptr<mod::fights::Fights> fights;uint64_t fightsReadAt=0;bool fightsTold=false;
#ifdef MOD_FAKE_NETWORK
    mod::fights::Sandbox fakeBts;   // the fake world's catcher manager, gazers and porter (mod_test_bts_*)
#endif
    // While this game visits a fight (or still runs the copy it showed), its catcher fight is not its own: none of it
    // goes to the partner, and the partner's fight news is not applied to it (tickBts).
    bool fightVisiting() const {return fights&&(fights->role()==mod::fights::Role::Visitor||fights->shown());}
    void tickFights(uint64_t now){
#ifndef MOD_MUTANT_FIGHT_SETTINGS
        if(!crowd||!sampler||!(config.hostFights||config.joinFights))return;
#else
        if(!crowd||!sampler)return;
#endif
        if(!fights){
            mod::fights::Api api;
#ifdef MOD_FAKE_NETWORK
            api=fakeBts.api();
#else
            try{api.command=mod::bts::nativeCatcher(*sampler);api.hurt=mod::bts::nativeHurt(*sampler);api.destroy=mod::bts::nativeDestroy(*sampler);}
            catch(const std::exception& e){if(!fightsTold){fightsTold=true;note((std::string("FIGHT unavailable: ")+e.what()+"\n").c_str());}return;}
#endif
            mod::fights::Settings s;s.host=config.hostFights;s.join=config.joinFights;
#ifdef MOD_MUTANT_FIGHT_SETTINGS
            s.host=s.join=true;
#endif
            fights=std::make_unique<mod::fights::Fights>(s,std::move(api),[this](const std::string& row){note(row.c_str());});
            note((std::string("FIGHT ready:")+(s.host?" hosts fights":"")+(s.join?" joins fights":"")+"\n").c_str());}
        if(now<fightsReadAt+200)return;
        fightsReadAt=now;
        std::array<double,3> at{cache.local.position[0],cache.local.position[1],cache.local.position[2]};bool available=cache.available&&!cache.gated;
#ifdef MOD_FAKE_NETWORK
        auto l=fakeBts.local(at,available);
#else
        mod::fights::Local l;l.fights=mod::bts::readFights(*sampler,sampler->base);l.at=at;l.available=available;l.dead=vitalsSeen.dead;l.caught=vitalsSeen.caught;
        {auto u=mod::ui::read(*sampler,sampler->base,cache.available);l.boss=(u.flagsKnown&&(u.flags&(uint64_t(1)<<15)))||replayRunning;}   // BossBattle (ui.h), or a boss replay (fact 12)
        if(l.fights.active>0)l.catchers=mod::bts::readCatchers(*sampler,sampler->base);
        if(fights->role()!=mod::fights::Role::None)l.gazers=mod::bts::readGazers(*sampler,sampler->base);
#endif
        fights->tick(l,crowd->client(),now);
    }
    // Shared MULEs (enemies.h): opt-in share_enemies. Each look sends the MULEs this player's game saw go down and
    // puts down the ones the partner reports, with the game's own defeat function.
    mod::enemies::Sync enemySync;std::vector<ds::CargoEvent> enemyIn;std::function<void(uint64_t,uint8_t)> enemyDefeat;
    mod::enemies::CampSync campSync;std::function<void(const std::array<uint8_t,16>&)> campAlert;
    uint64_t enemyReadAt=0,enemySent=0,enemyApplied=0,enemyPorter=0,enemySettled=0;bool enemyTried=false,enemyTimed=false,enemyLinked=false,enemyTell=false;std::map<uint64_t,uint64_t> enemyRetry;
    void tickEnemies(uint64_t now) {
        if(!core||!sampler||!config.shareEnemies){enemyIn.clear();return;}
        if(!enemyTried){try{campAlert=mod::enemies::nativeAlert(*sampler);}catch(const std::exception&){}}
        if(!enemyTried){enemyTried=true;try{enemyDefeat=mod::enemies::nativeDefeat(*sampler);}catch(const std::exception& e){note((std::string("ENEMY unavailable: ")+e.what()+"\n").c_str());}}
        if(!enemyDefeat||(enemyIn.empty()&&now-enemyReadAt<500))return;
        enemyReadAt=now;
        auto t0=mod::nowUs();auto mules=mod::enemies::readMules(*sampler,sampler->base);
        if(!enemyTimed&&!mules.empty()){enemyTimed=true;std::ostringstream o;o<<"ENEMY "<<mules.size()<<" MULEs read in "<<(mod::nowUs()-t0)/1000.<<" ms\n";note(o.str().c_str());}
        if(mules.empty())return;
        auto camps=campAlert?mod::enemies::readCamps(*sampler,sampler->base):std::vector<mod::enemies::Camp>{};
        for(auto& e:enemyIn){
            if(e.item==mod::enemies::CampAlert){
                if(auto* c=campSync.wanted(camps,e.ref)){campSync.applied(e.ref);campAlert(c->uuid);++enemyApplied;note("ENEMY partner's camp alarm: this game's camp is put on alert\n");}
                continue;
            }
            mod::enemies::Down d{e.ref,uint8_t(e.item)};
            if(auto* m=enemySync.wanted(mules,d)){
                enemySync.applied(d.key);enemyDefeat(m->address,d.kind);++enemyApplied;
                if(d.kind!=mod::enemies::Fainted&&d.kind!=mod::enemies::Dead)enemyRetry[d.key]=now;   // see below
                std::ostringstream o;o<<"ENEMY partner put a MULE down: id "<<std::hex<<m->id<<std::dec<<" kind "<<int(d.kind)<<" lod "<<m->lod<<"\n";note(o.str().c_str());
            }
        }
        if(!enemyIn.empty()){enemyIn.clear();mules=mod::enemies::readMules(*sampler,sampler->base);}
        // The game does not always take a knock-out or a sleep from outside (live: an alerted MULE ignored "asleep");
        // a MULE the partner reported down that still stands a second later is made to faint, which the game takes.
        for(auto it=enemyRetry.begin();it!=enemyRetry.end();){
            if(now-it->second<1000){++it;continue;}
            for(auto& m:mules)if(mod::enemies::key(m.uuid)==it->first&&mod::enemies::condition(m.state,m.flags)==mod::enemies::Up&&m.state!=5){
                enemyDefeat(m.address,mod::enemies::Fainted);note("ENEMY the partner's MULE still stood here: made to faint instead\n");}
            it=enemyRetry.erase(it);
        }
        // On joining (and after a load, once it has settled): the MULEs that are down here go to the partner.
        { auto anchor=anchorNow();bool linked=core->connected()&&partnerMode();
          if(anchor.entity!=enemyPorter){enemyPorter=anchor.entity;enemySettled=now+10000;enemyTell=true;}
          if(linked&&!enemyLinked)enemyTell=true;
          enemyLinked=linked;
          if(enemyTell&&linked&&enemyPorter&&now>=enemySettled){
              enemyTell=false;auto all=mod::enemies::Sync::everything(mules);
              for(auto& d:all){ds::CargoEvent e;e.type=mod::enemies::WireType;e.item=d.kind;e.ref=d.key;core->queueCargo(e);++enemySent;}
              if(!all.empty()){std::ostringstream o;o<<"ENEMY "<<all.size()<<" MULEs are down in this world -> partner\n";note(o.str().c_str());}
          } }
        for(auto k:campSync.observe(camps)){
            if(!core->connected()||!partnerMode())continue;
            ds::CargoEvent e;e.type=mod::enemies::WireType;e.item=mod::enemies::CampAlert;e.ref=k;core->queueCargo(e);++enemySent;
            note("ENEMY a camp went on alert here -> partner\n");
        }
        for(auto& d:enemySync.observe(mules)){
            if(!core->connected()||!partnerMode())continue;
            ds::CargoEvent e;e.type=mod::enemies::WireType;e.item=d.kind;e.ref=d.key;core->queueCargo(e);++enemySent;
            std::ostringstream o;o<<"ENEMY a MULE went down here: kind "<<int(d.kind)<<" -> partner\n";note(o.str().c_str());
        }
    }
    // --- Roads paved together in group play (roadshare.h): every 10 s, what the server told the game the others put into
    // its pavers, less what the game took and what the mod gave before, given into the pavers still without their road.
    mod::roadshare::Ledger roadLedger;bool roadLedgerRead=false;uint64_t groupRoadAt=0;size_t roadAskedNoted=size_t(-1);
    std::filesystem::path roadLedgerPath() const {
        uint64_t h=1469598103934665603ull;for(unsigned char c:config.onlineServer){h^=c;h*=1099511628211ull;}
        std::ostringstream n;n<<"roads-given-"<<std::hex<<h<<".txt";return modFolder.empty()?std::filesystem::path():modFolder/n.str();}
    void tickGroupRoads(uint64_t now){
        if(!config.groupRoads||!sampler||now<groupRoadAt+10000)return;
        groupRoadAt=now;
        if(!cache.available||cache.gated||inEvent)return;
        if(!roadTried){roadTried=true;try{roadApi=mod::roads::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("ROAD unavailable: ")+e.what()+"\n").c_str());}}
        if(!roadApi.give||!roadApi.missing)return;
        if(!roadLedgerRead){roadLedgerRead=true;auto p=roadLedgerPath();if(!p.empty()){std::ifstream f(p);std::stringstream b;b<<f.rdbuf();roadLedger=mod::roadshare::Ledger::load(b.str());}}
        auto asked=mod::roadshare::readAsked(*sampler,sampler->base);
        if(asked.size()!=roadAskedNoted){roadAskedNoted=asked.size();note(("ROADSRV the game holds "+std::to_string(asked.size())+" totals from the server\n").c_str());}
        if(asked.empty())return;
        auto gives=mod::roadshare::plan(asked,mod::roads::readPavers(*sampler,sampler->base),roadApi.missing,roadLedger);
        for(auto& g:gives){
            roadApi.give(g.paver,g.set);
            std::ostringstream o;o<<"ROADSRV gave paver "<<g.id<<" the others' materials:";for(auto n:g.set)o<<" "<<n;o<<"\n";note(o.str().c_str());}
        if(!gives.empty()){auto p=roadLedgerPath();if(!p.empty()){std::ofstream f(p,std::ios::trunc);f<<roadLedger.dump();}}
    }
    // --- MULEs down and camps alarmed in group play (groupevents.h): with group_enemies, the same detection as the
    // partner's (enemies.h Sync, CampSync), told to `…/coop/events` and read from it; the others' applied with the
    // game's own defeat and alert. After a load, once it has settled, every MULE down here is told once.
    // Structure levels (group play): a rise in this game is said under the hash of the structure's online id; another
    // player's rise raises the same structure here with the game's own routine (roads.h raise). A structure first seen
    // at some level is not said (it may come from the server so); what was raised for another player is not said back.
    std::map<int64_t,uint32_t> levelSeen,levelApplied;bool levelPrimed=false;uint64_t levelAt=0;   // levelTried, levelApi: the partner's (shared)
    void levelsStep(const std::vector<mod::groupevents::Answer>& reads,uint64_t now,std::vector<mod::groupevents::Event>& out){
        bool any=false;for(auto& a:reads)for(auto& e:a.events)if(e.kind=="level")any=true;
        if(!any&&now<levelAt+2000)return;
        levelAt=now;
        if(!cache.available||cache.gated||inEvent)return;
        if(!levelTried){levelTried=true;try{levelApi=mod::roads::nativeLevels(*sampler);}catch(const std::exception& e){note((std::string("LEVEL unavailable: ")+e.what()+"\n").c_str());}}
        if(!levelApi.raise)return;
        if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
        auto all=mod::own_strand::readStructures(*sampler,sampler->base,imageSize);
        std::map<int64_t,const mod::own_strand::Structure*> byKey;for(auto& s:all)if(!s.online.empty())byKey[mod::groupevents::levelKey(s.online)]=&s;
        for(auto& a:reads)for(auto& e:a.events){
            if(e.kind!="level")continue;
            auto f=byKey.find(e.key);if(f==byKey.end()||int64_t(f->second->record.level)>=e.value)continue;
            auto done=levelApi.raise(f->second->record.object,uint32_t(e.value));levelApplied[e.key]=uint32_t(e.value);levelSeen[e.key]=uint32_t(e.value);
            note(("LEVEL another player raised "+f->second->online+" to level "+std::to_string(e.value)+": "+done+"\n").c_str());}
        for(auto& [k,s]:byKey){
            const uint32_t lv=s->record.level;auto it=levelSeen.find(k);
            if(it==levelSeen.end()){levelSeen[k]=lv;continue;}
            if(lv>it->second){it->second=lv;
#ifndef MUTANT_LEVEL_ECHO
                if(levelApplied.count(k)&&levelApplied[k]>=lv)continue;
#endif
                if(levelPrimed){out.push_back({"level",k,int64_t(lv)});note(("LEVEL "+s->online+" rose to level "+std::to_string(lv)+" here -> server\n").c_str());}}}
        levelPrimed=true;
    }
    std::unique_ptr<mod::groupevents::Courier> enemyCourier;uint64_t groupEnemyAt=0,groupEnemyPorter=0,groupEnemySettled=0;bool groupEnemyTell=false;
    // Timefall shared in group play (groupweather.h): where this porter's own map says rain, it is said for his cell;
    // where others said rain, the game's own rain influence is placed here (never clear; never during an event).
    mod::groupweather::Rain groupRain;std::optional<mod::weather::Api> rainApi;bool rainTried=false;uint64_t rainCheckAt=0,rainNotedAt=0;unsigned rainSaid=0,rainPlaced=0;
    void rainStep(const std::vector<mod::groupevents::Answer>& reads,uint64_t now,std::vector<mod::groupevents::Event>& out){
        for(auto& a:reads)for(auto& e:a.events)if(e.kind=="rain")groupRain.heard(e.key,e.value,now);
        if(now<rainCheckAt)return;rainCheckAt=now+2000;
        if(!cache.available||cache.gated||inEvent||!environmentReader)return;
        auto anchor=anchorNow();if(!anchor.entity||!anchor.pose.available)return;
        const auto here=mod::puppet::transform(anchor.pose).position;
        auto o=environmentReader->read(here);std::optional<float> c;if(o.weatherAvailable&&std::isfinite(o.rainFromMap))c=o.rainFromMap;
        if(auto s=groupRain.report(now,here,c)){out.push_back({"rain",s->first,s->second});++rainSaid;}
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
        if(!rainTried){rainTried=true;try{rainApi=mod::weather::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("RAIN unavailable: ")+e.what()+"\n").c_str());}}
        if(rainApi)for(auto& p:groupRain.apply(now,here))if(rainApi->influence(mod::weather::Mode::Rain,p.at,mod::groupweather::Radius,mod::groupweather::Lifetime,1+unsigned(p.slot)))++rainPlaced;
#endif
        if(now>=rainNotedAt+30000&&(rainSaid||rainPlaced)){rainNotedAt=now;std::ostringstream x;
            x<<"RAIN said for this porter's ground "<<rainSaid<<" times, others' rain placed here "<<rainPlaced<<" times; the map here "<<(c?std::to_string(*c):std::string("unread"))<<"\n";note(x.str().c_str());}
    }
    // Lab (research for boss replays played together): `replay ID` asks the game's mission start for that mission
    // (0x33b80a0(S = *(image+0x7c0aff8), id, 0, ranking 0); the 17 boss-replay ids, 0x33cb020), and the state is logged
    // when it changes: fact 12 "a boss replay runs" (0x33acd40(S, 12)), the game-state bits 13 GameOver, 15 BossBattle,
    // 16 AfterBeatBoss (*(image+0x7bf6e88)+8), and the private room (the game-flow manager's +0x58 bit 25).
    bool replayWatch=false;uint64_t replayCheckAt=0;int replayNoted=-1;int replayUsable=-1;bool replayRunning=false;
    bool replayCallsUsable(){
#ifdef _WIN32
        static const uint8_t start[19]={0x40,0x53,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x48,0x45,0x0f,0xb6,0xf8,0x45,0x0f,0xb6,0xf1};
        static const uint8_t fact[12]={0x48,0x83,0xec,0x28,0x0f,0xb6,0xc2,0xff,0xc8,0x83,0xf8,0x0c};
        uint8_t a[19]{},b[12]{};
        return sampler&&sampler->read(sampler->base+0x33b80a0,a,19)&&!std::memcmp(a,start,19)&&sampler->read(sampler->base+0x33acd40,b,12)&&!std::memcmp(b,fact,12);
#else
        return false;
#endif
    }
    // Fact 12 now (game thread): 1 a boss replay runs, 0 not, -1 unknown.
    int replayFact(){
#ifdef _WIN32
        if(!sampler)return -1;
        if(replayUsable<0)replayUsable=replayCallsUsable()?1:0;
        uint64_t s=0;if(!replayUsable||!sampler->read(sampler->base+0x7c0aff8,&s,8)||!s)return -1;
        return reinterpret_cast<bool(*)(uint64_t,uint8_t)>(sampler->base+0x33acd40)(s,12)?1:0;
#else
        return -1;
#endif
    }
    // The game-flow manager GF = *(image+0x7c0d6c0): +0x58 bit 25 while the porter is in his private room; +0x80 the
    // room's blackboard code and +0x8c its flag, which the room's script reads (GetPrivateRoomBlackBoardFromCpp 0x33c9fe0,
    // only in the room) to start the Boss Battle replay; the private-room logic writes both after the menu (0x27677f4).
    uint64_t gameFlow(){uint64_t g=0;if(!sampler||!sampler->read(sampler->base+0x7c0d6c0,&g,8))return 0;return g;}
    int inPrivateRoom(){const uint64_t g=gameFlow();uint32_t f=0;if(!g||!sampler->read(g+0x58,&f,4))return -1;return int((f>>25)&1);}
    // The code to the blackboard, only in the private room. With `flag`, first the call the game makes there:
    // 0x273fe10(object, 0x1a, 1, 0); the object is taken to be the porter's entity (inferred: the game passes the
    // logic's +0x30; the replay log's Blackboard record names it).
    bool writeReplayCode(int32_t code,bool flag){
#ifdef _WIN32
        const uint64_t g=gameFlow();if(!g||inPrivateRoom()!=1)return false;
        if(flag){static const uint8_t head[8]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x20};uint8_t got[8]{};auto anchor=anchorNow();
            if(!anchor.entity||!sampler->read(sampler->base+0x273fe10,got,8)||std::memcmp(got,head,8))return false;
            reinterpret_cast<void(*)(uint64_t,int32_t,bool,bool)>(sampler->base+0x273fe10)(anchor.entity,0x1a,true,false);}
        *reinterpret_cast<volatile int32_t*>(g+0x80)=code;*reinterpret_cast<volatile uint8_t*>(g+0x8c)=1;
        return true;
#else
        (void)code;(void)flag;return false;
#endif
    }
    // Lab: into the private room (enter) or out, with the game's own request 0x3411950(GF, hash, -1, facility): hash
    // 0x3a1bb216 enters (the room mode is not active), at the facility 0x2c57cf0(*(image+0x7bf4960), handle, 0) with the
    // handle *(*(image+0x7be7980)+0xfeb4) (0: none), as the game's direct path does (0x2839640); 0x303767f9 leaves.
    bool requestPrivateRoom(bool enter){
#ifdef _WIN32
        static const uint8_t req[16]={0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x81,0xfa,0xf9,0x67,0x37,0x30,0x74};
        static const uint8_t look[16]={0x40,0x53,0x48,0x83,0xec,0x20,0x81,0xfa,0xff,0xff,0x1f,0x00,0x74,0x3a,0x8b,0xc2};
        uint8_t a[16]{},b[16]{};const uint64_t g=gameFlow();const int room=inPrivateRoom();
        if(!g||room<0||(room==1)==enter){note(room<0?"REPLAY private room: the game flow is not read\n":enter?"REPLAY private room: already in it\n":"REPLAY private room: not in it\n");return false;}
        if(!sampler->read(sampler->base+0x3411950,a,16)||std::memcmp(a,req,16)||!sampler->read(sampler->base+0x2c57cf0,b,16)||std::memcmp(b,look,16)){note("REPLAY private room: the functions differ\n");return false;}
        uint64_t facility=0,player=0,table=0;uint32_t handle=0x1fffff;
        if(enter&&sampler->read(sampler->base+0x7be7980,&player,8)&&player&&sampler->read(player+0xfeb4,&handle,4)&&sampler->read(sampler->base+0x7bf4960,&table,8)&&table)
            facility=reinterpret_cast<uint64_t(*)(uint64_t,uint32_t,bool)>(sampler->base+0x2c57cf0)(table,handle,false);
        reinterpret_cast<void(*)(uint64_t,uint32_t,int32_t,uint64_t)>(sampler->base+0x3411950)(g,enter?0x3a1bb216u:0x303767f9u,-1,facility);
        std::ostringstream o;o<<"REPLAY private room "<<(enter?"entry":"exit")<<" asked (facility handle 0x"<<std::hex<<handle<<", facility "<<(facility?"found":"none")<<")\n";note(o.str().c_str());
        return true;
#else
        (void)enter;return false;
#endif
    }
    // Lab (`replaylog 1`): every record of the replay hooks (replayhooks.h) in the log.
    bool replayLog=false;
    void drainReplayHooks(){
        if(!mod::replayhooks::installed())return;
        for(auto& r:mod::replayhooks::drain()){
            if(!replayLog)continue;
            std::ostringstream o;o<<"REPLAYLOG ";
            switch(r.kind){
            case mod::replayhooks::Kind::Accept:o<<"Boss Battle accepted: code 0x"<<std::hex<<r.a<<std::dec<<", row "<<r.b;break;
            case mod::replayhooks::Kind::Blackboard:o<<"private room hands on code 0x"<<std::hex<<r.a<<" (object 0x"<<r.b<<", index 0x"<<r.c<<")"<<std::dec;
                {auto anchor=anchorNow();o<<(anchor.entity&&r.b==anchor.entity?"; the object is the porter's entity":"; the object is not the porter's entity");}break;
            case mod::replayhooks::Kind::Script:o<<"room script asks a mission start: resource 0x"<<std::hex<<r.a<<", record 0x"<<r.b<<std::dec<<", mission "<<r.c;break;
            case mod::replayhooks::Kind::Start:o<<"mission start "<<r.a<<(r.b?" passed the check":" refused by the check")<<(mod::groupreplay::replayMission(int64_t(r.a))?" (a boss replay)":"");break;}
            o<<"\n";note(o.str().c_str());}
    }
    // Lab (`bosslife 1`): read-only, every 2 s while a boss replay runs: the porter's destructibility (entity+0xc0: the
    // floats +0x5c, +0x68 (damage, as Entity_ExportedSetHealth 0x2361d10 -> 0x23ec3f0 writes it), +0x6c, and the
    // resource's +0x24 (full life)), the catchers' lives, and the actor manager's units (their vtables and actor counts)
    // when they change: where a human boss's life is, before anything is written to it.
    bool bossLifeSurvey=false;uint64_t bossLifeAt=0;std::string bossUnitsNoted;
    void tickBossLifeSurvey(uint64_t now){
        if(!bossLifeSurvey||!sampler||now<bossLifeAt)return;bossLifeAt=now+2000;
        auto anchor=anchorNow();std::ostringstream o;o<<"BOSSLIFE replay "<<replayFact()<<", BossBattle "<<(replayRunning?1:0);
        uint64_t d=0,res=0;float f5c=0,f68=0,f6c=0,full=0;
        if(anchor.entity&&sampler->read(anchor.entity+0xc0,&d,8)&&d&&sampler->read(d+0x5c,&f5c,4)&&sampler->read(d+0x68,&f68,4)&&sampler->read(d+0x6c,&f6c,4)&&sampler->read(d+0x30,&res,8)&&res&&sampler->read(res+0x24,&full,4))
            o<<"; porter +5c "<<f5c<<" +68 "<<f68<<" +6c "<<f6c<<" full "<<full;
        else o<<"; porter's destructibility unread";
        auto fightsNow=mod::bts::readFights(*sampler,sampler->base);
        if(fightsNow.active>0){o<<"; catchers";for(auto& c:mod::bts::readCatchers(*sampler,sampler->base))o<<" "<<c.percent<<"% (type "<<int(c.type)<<")";}
        o<<"\n";note(o.str().c_str());
        uint64_t manager=0,units=0;int32_t count=0;std::ostringstream u;
        if(sampler->read(sampler->base+0x7bbb8c0,&manager,8)&&manager&&sampler->read(manager+0x440,&count,4)&&sampler->read(manager+0x448,&units,8)&&units&&count>0&&count<=256)
            for(int32_t i=0;i<count;++i){uint64_t unit=0,vt=0;uint32_t n=0;
                if(sampler->read(units+8*uint64_t(i),&unit,8)&&unit&&sampler->read(unit,&vt,8)&&sampler->read(unit+0x40,&n,4))u<<" "<<std::hex<<(vt-sampler->base)<<std::dec<<":"<<n;}
        if(u.str()!=bossUnitsNoted){bossUnitsNoted=u.str();note(("BOSSLIFE actor units (vtable:actors)"+bossUnitsNoted+"\n").c_str());}
    }
    void tickReplayWatch(uint64_t now){
        drainReplayHooks();
        tickBossLifeSurvey(now);
        if(!(replayWatch||config.groupReplays||bossLifeSurvey)||!sampler||now<replayCheckAt)return;replayCheckAt=now+1000;
#ifdef _WIN32
        uint64_t g=0,flags=0;
        if(sampler->read(sampler->base+0x7bf6e88,&g,8)&&g)sampler->read(g+8,&flags,8);
        const int fact=replayFact();const int room=inPrivateRoom();
        replayRunning=fact==1;
        if(!replayWatch)return;
        const int state=int(fact==1)|int((flags>>13)&1)<<1|int((flags>>15)&1)<<2|int((flags>>16)&1)<<3|int(room==1)<<4;
        if(state!=replayNoted){replayNoted=state;std::ostringstream o;o<<"REPLAY boss replay runs "<<fact<<", BossBattle "<<((flags>>15)&1)<<", AfterBeatBoss "<<((flags>>16)&1)<<", GameOver "<<((flags>>13)&1)<<", private room "<<room<<"\n";note(o.str().c_str());}
#else
        (void)now;
#endif
    }
    // Boss Battle replays together (groupreplay.h; `group_replays`, with group_enemies): its own copy of the events' reads
    // (groupCourier), taken in the private room too (group_enemies drops its reads there: the porter's double is busy).
    std::unique_ptr<mod::groupreplay::Group> groupReplay;bool replayHooksTried=false;uint64_t replayStepAt=0;std::vector<std::u16string> replayLines;
    void replayStep(const std::vector<mod::groupevents::Answer>& reads,uint64_t now,std::vector<mod::groupevents::Event>& out){
        if(!config.groupReplays||!sampler)return;
        if(!groupReplay){
            if(!replayHooksTried){replayHooksTried=true;try{mod::replayhooks::install(*sampler);note("REPLAY hooks on\n");}catch(const std::exception& e){note((std::string("REPLAY unavailable: ")+e.what()+"\n").c_str());}}
            if(!mod::replayhooks::installed())return;
            groupReplay=std::make_unique<mod::groupreplay::Group>(mod::groupreplay::Settings{true},[]{static std::mt19937 r(std::random_device{}());return uint16_t(1+r()%65535);},
                [this](const std::string& s){note(s.c_str());});
            note("REPLAY group replays on: a boss replay started here is told to the group; one another player started is joined from the private room\n");}
        for(auto& a:reads)for(auto& e:a.events)if(e.kind=="replay"||e.kind=="bosslife")groupReplay->heard({e.kind,e.key,e.value},now);
        if(now<replayStepAt)return;replayStepAt=now+250;
        if(!btTried){btTried=true;try{btDestroy=mod::bts::nativeDestroy(*sampler);btHurt=mod::bts::nativeHurt(*sampler);btCatcher=mod::bts::nativeCatcher(*sampler);}catch(const std::exception& e){note((std::string("BT unavailable: ")+e.what()+"\n").c_str());}}
        mod::groupreplay::Local l;
        auto u=mod::ui::read(*sampler,sampler->base,cache.available);
        l.available=u.flagsKnown&&!(u.flags&(uint64_t(1)<<22));   // the game runs and is not loading
        l.boss=u.flagsKnown&&(u.flags&(uint64_t(1)<<15));
        l.inRoom=inPrivateRoom()==1;l.running=replayFact()==1;replayRunning=l.running;
        l.mission=mod::replayhooks::lastMission();l.code=mod::replayhooks::lastCode();
        // The boss's life: the catchers of a BT boss (whale, titans). A human boss's life is not read yet (`bosslife`).
        std::vector<mod::bts::Catcher> catchers;
        if(l.running&&btHurt&&mod::bts::readFights(*sampler,sampler->base).active>0){catchers=mod::bts::readCatchers(*sampler,sampler->base);for(auto& c:catchers)l.lives.push_back(c.percent);}
        auto act=groupReplay->tick(l,now);
        for(auto& e:act.say)out.push_back({e.kind,e.key,e.value});
        if(act.start&&!writeReplayCode(*act.start,false))note("REPLAY the code could not be written (not in the private room)\n");
        for(auto& h:act.hurt)if(h.part<catchers.size())btHurt(catchers[h.part],h.percent);
        for(auto& n:act.notices){replayLines.push_back(routeWide(n));if(replayLines.size()>8)replayLines.erase(replayLines.begin());}
    }
    void tickGroupReplays(uint64_t now){
        if(!config.groupReplays||config.strandToken.empty()||!sampler)return;
        auto* courier=groupCourier();if(!courier)return;
        std::vector<mod::groupevents::Event> out;replayStep(std::exchange(replayReads,{}),now,out);
        if(!out.empty())courier->send(std::move(out));
    }
    // The server's world events (`…/coop/events`): one courier for every group feature that says or reads them (the
    // enemies, levels and timefall with group_enemies; orders with group_orders; boss replays with group_replays); each
    // takes its own copy of the reads.
    std::vector<mod::groupevents::Answer> enemyReads,orderReads,replayReads;
    mod::groupevents::Courier* groupCourier(){
        if(!enemyCourier){
            auto at=serverAt();if(!at)return nullptr;
            const std::string target=at->base+"/coop/events",token=config.strandToken;const uint32_t address=at->address;const uint16_t port=at->port;
            enemyCourier=std::make_unique<mod::groupevents::Courier>(
                [=](const std::string& since){auto r=mod::strand::get(address,port,target+(since.empty()?std::string():"?since="+since),token,mod::strand::ConnectMs);return mod::groupevents::Result{r.status,r.body};},
                [=](const std::string& body){auto r=mod::strand::post(address,port,target,token,body,mod::strand::ConnectMs);return mod::groupevents::Result{r.status,r.body};},
                [this](const std::string& s){note(s.c_str());});
            if(config.groupEnemies)note("ENEMYSRV on: MULEs down and camps alarmed are shared through the server\n");
            if(config.groupOrders)note("GROUPORDER on: standard orders are taken together by the players at one terminal, through the server\n");}
        for(auto& a:enemyCourier->reads()){
            if(config.groupOrders)orderReads.push_back(a);
            if(config.groupReplays)replayReads.push_back(a);
            if(config.groupEnemies)enemyReads.push_back(std::move(a));}
        auto cap=[](std::vector<mod::groupevents::Answer>& v){if(v.size()>32)v.erase(v.begin(),v.end()-32);};   // a consumer that cannot take them yet
        cap(enemyReads);cap(orderReads);cap(replayReads);
        return enemyCourier.get();
    }
    // --- Orders taken together in group play (grouporders.h): with group_orders, a standard order taken at a terminal
    // is said with a party number and where the porter stands; a game whose porter stands there within two minutes takes
    // it on too with the game's own start request (orders.h); the party's hand-ins, success and the leader's give-up
    // reach its other games through the same requests and rows as the partner's (tickOrders). Never writes the save.
    std::optional<mod::grouporders::Together> together;uint64_t groupOrderAt=0,groupOrderPorter=0;bool groupOrderOffTold=false;
    std::vector<std::u16string> orderLines;
    void tickGroupOrders(uint64_t now){
        if(!config.groupOrders||config.strandToken.empty()||!sampler)return;
        if(!orderTried){orderTried=true;try{orderRequest=mod::orders::nativeRequest(*sampler);}catch(const std::exception& e){note((std::string("ORDER unavailable: ")+e.what()+"\n").c_str());}}
        if(!orderRequest)return;
        auto* courier=groupCourier();if(!courier)return;
        if(!together)together.emplace(config.playerId^(now*0x9e3779b97f4a7c15ull)^uint64_t(mod::nowUs()));
        std::vector<mod::groupevents::Event> heard;
        for(auto& a:std::exchange(orderReads,{})){
            if(!a.orders&&!groupOrderOffTold){groupOrderOffTold=true;note("GROUPORDER the server keeps orders each player's own (its rule cargo is off): nothing is shared\n");}
            for(auto& e:a.events)if(e.kind=="order"||e.kind=="unit")heard.push_back(e);}
        if(!heard.empty())together->hear(heard,now);
        if(now<groupOrderAt+1000)return;
        groupOrderAt=now;
        if(!cache.available||cache.gated||inEvent)return;
        auto anchor=anchorNow();
        if(anchor.entity!=groupOrderPorter){if(groupOrderPorter)together->reset();groupOrderPorter=anchor.entity;}   // another world: its parties are left
        if(!anchor.entity)return;
        std::optional<mod::grouporders::Place> porter;if(anchor.pose.available)porter=mod::puppet::transform(anchor.pose).position;
        auto orders=mod::orders::readOrders(*sampler,sampler->base,orderDetails);if(orders.empty())return;
        auto units=mod::orders::readUnits(*sampler,sampler->base);
        auto step=together->step(now,orders,units,porter);
        for(auto& line:step.log)note((line+"\n").c_str());
        for(auto& r:step.apply){
            bool ok=false;
            if(r.what==mod::grouporders::Request::Unit){
                for(auto& u:units)if(u.mission==r.mission&&u.unit==r.unit&&u.state==mod::orders::NotDelivered){
                    const unsigned removed=deliverUnitHere(u,r.packed,r.life[0],r.life[1]);ok=true;
                    std::ostringstream o;o<<"GROUPORDER unit "<<int(r.unit)<<" of "<<std::hex<<r.mission<<std::dec<<" marked delivered, "<<removed<<" cargo record(s) removed\n";note(o.str().c_str());}}
            else{ok=orderRequest(r.mission,uint8_t(r.what));
                if(!ok){std::ostringstream o;o<<"GROUPORDER the game refused the request "<<int(r.what)<<" for order "<<std::hex<<r.mission<<std::dec<<"\n";note(o.str().c_str());}}
            together->done(r,ok);}
        if(!step.say.empty())courier->send(std::move(step.say));
        for(auto l:step.lines)switch(l){
            case mod::grouporders::Line::Joined:orderLines.push_back(u"A porter here took this order: it is under way for you too. Give it up to go alone.");break;
            case mod::grouporders::Line::UnitByOther:orderLines.push_back(u"A porter on your order handed in part of it.");break;
            case mod::grouporders::Line::DoneByOther:orderLines.push_back(u"A porter on your order completed it.");break;
            case mod::grouporders::Line::GivenUpByLeader:orderLines.push_back(u"The porter who took your order gave it up.");break;}
        if(orderLines.size()>8)orderLines.erase(orderLines.begin(),orderLines.end()-8);
    }
    void tickGroupEnemies(uint64_t now){
        if(!config.groupEnemies||config.strandToken.empty()||!sampler)return;
        if(!enemyTried){try{campAlert=mod::enemies::nativeAlert(*sampler);}catch(const std::exception&){}}
        if(!enemyTried){enemyTried=true;try{enemyDefeat=mod::enemies::nativeDefeat(*sampler);}catch(const std::exception& e){note((std::string("ENEMY unavailable: ")+e.what()+"\n").c_str());}}
        if(!enemyDefeat)return;
        if(!groupCourier())return;
        auto reads=std::exchange(enemyReads,{});
        if(!cache.available||cache.gated||inEvent)reads.clear();
        {std::vector<mod::groupevents::Event> lv;levelsStep(reads,now,lv);rainStep(reads,now,lv);enemyCourier->send(std::move(lv));}
        if(reads.empty()&&now-groupEnemyAt<500)return;
        groupEnemyAt=now;
        if(!cache.available||cache.gated||inEvent)return;
        if(!btTried){btTried=true;try{btDestroy=mod::bts::nativeDestroy(*sampler);btHurt=mod::bts::nativeHurt(*sampler);btCatcher=mod::bts::nativeCatcher(*sampler);}catch(const std::exception& e){note((std::string("BT unavailable: ")+e.what()+"\n").c_str());}}
        auto gazers=btDestroy?mod::bts::readGazers(*sampler,sampler->base):std::vector<mod::bts::Gazer>{};
        for(auto& a:reads)for(auto& e:a.events)if(e.kind=="gazer"&&btDestroy)
            if(auto* g=mod::bts::GazerSync::wanted(gazers,uint64_t(e.key))){gazerSync.applied(uint64_t(e.key));btDestroy(g->address);++btApplied;note("BT another player destroyed a gazer: gone here too\n");}
        auto mules=mod::enemies::readMules(*sampler,sampler->base);
        auto camps=campAlert?mod::enemies::readCamps(*sampler,sampler->base):std::vector<mod::enemies::Camp>{};
        for(auto& a:reads)for(auto& e:a.events){
            if(e.kind=="camp"){if(auto* c=campSync.wanted(camps,uint64_t(e.key))){campSync.applied(uint64_t(e.key));campAlert(c->uuid);++enemyApplied;note("ENEMY another player's camp alarm: this game's camp is put on alert\n");}continue;}
            if(e.kind!="mule")continue;
            mod::enemies::Down d{uint64_t(e.key),uint8_t(e.value)};
            if(auto* m=enemySync.wanted(mules,d)){
                enemySync.applied(d.key);enemyDefeat(m->address,d.kind);++enemyApplied;
                if(d.kind!=mod::enemies::Fainted&&d.kind!=mod::enemies::Dead)enemyRetry[d.key]=now;
                std::ostringstream o;o<<"ENEMY another player put a MULE down: id "<<std::hex<<m->id<<std::dec<<" kind "<<int(d.kind)<<"\n";note(o.str().c_str());}}
        for(auto it=enemyRetry.begin();it!=enemyRetry.end();){
            if(now-it->second<1000){++it;continue;}
            for(auto& m:mules)if(mod::enemies::key(m.uuid)==it->first&&mod::enemies::condition(m.state,m.flags)==mod::enemies::Up&&m.state!=5){
                enemyDefeat(m.address,mod::enemies::Fainted);note("ENEMY another player's MULE still stood here: made to faint instead\n");}
            it=enemyRetry.erase(it);}
        std::vector<mod::groupevents::Event> out;
        auto anchor=anchorNow();
        if(anchor.entity!=groupEnemyPorter){groupEnemyPorter=anchor.entity;groupEnemySettled=now+10000;groupEnemyTell=true;}
        if(groupEnemyTell&&groupEnemyPorter&&now>=groupEnemySettled){groupEnemyTell=false;
            for(auto& d:mod::enemies::Sync::everything(mules))out.push_back({"mule",int64_t(d.key),d.kind});
            for(auto k:mod::bts::GazerSync::everything(gazers))out.push_back({"gazer",int64_t(k),1});
            if(!out.empty())note(("ENEMY "+std::to_string(out.size())+" MULEs are down in this world -> server\n").c_str());}
        for(auto k:campSync.observe(camps)){out.push_back({"camp",int64_t(k),mod::enemies::CampAlert});note("ENEMY a camp went on alert here -> server\n");}
        if(btDestroy)for(auto k:gazerSync.observe(gazers)){out.push_back({"gazer",int64_t(k),1});note("BT a gazer was destroyed here -> server\n");}
        for(auto& d:enemySync.observe(mules)){out.push_back({"mule",int64_t(d.key),d.kind});std::ostringstream o;o<<"ENEMY a MULE went down here: kind "<<int(d.kind)<<" -> server\n";note(o.str().c_str());}
        enemyCourier->send(std::move(out));
    }
    // MULE positions (enemy_poses.h): opt-in share_enemy_positions, with share_enemies. The host's game says where the
    // MULEs near either player stand (the core's unreliable ENEMIES sample, five a second); the guest moves its own
    // MULEs there with the MULE actor's own teleport, except the ones its porter is fighting hand to hand.
    mod::enemyposes::Sampler enemyPoseSampler;mod::enemyposes::Corrector enemyPoseCorrector;
    std::function<bool(uint64_t,const mod::enemyposes::Place&)> enemyMove;bool enemyMoveTried=false;
    uint64_t enemyPoseAt=0,enemyPoseSent=0,enemyPoseMoved=0,enemyPoseRefused=0,enemyPoseSeen=0;
    // enemy_position_goal: key -> when the game's checks last let a forced destination through for that MULE.
    std::map<uint64_t,uint64_t> enemySteered;std::map<std::string,uint64_t> enemySteerRefusals;
    uint64_t enemySteerWrites=0,enemySteerRefused=0,enemySteerLogAt=0;
    // Lab only (mule_goal.h): the repeated forced destination of "mulegoto", and the watch of the goal after it.
    struct MuleGoto { bool active=false,first=true; uint64_t key=0,until=0,watchUntil=0,logAt=0,writes=0,refusals=0; mod::mulegoal::Chain chain; mod::mulegoal::Vec3 destination{}; std::string stopped; } muleGoto;
    void tickMuleGoto(uint64_t now) {
        auto& g=muleGoto;if(!g.active||!sampler)return;
        if(!config.shareEnemyPositions||now>=g.watchUntil){g.active=false;std::ostringstream o;o<<"MULEGOAL test: forced end key "<<std::hex<<g.key<<std::dec<<" writes "<<g.writes<<" refusals "<<g.refusals<<" last "<<(g.stopped.empty()?"ok":g.stopped)<<"\n";note(o.str().c_str());return;}
        mod::mulegoal::Vec3 mule{};bool placed=sampler->read(g.chain.actor+0x28,mule.data(),24);
        if(now<g.until){
            auto anchor=anchorNow();std::string why="porter or MULE position unreadable";
            if(placed&&anchor.pose.available)why=mod::mulegoal::force(*sampler,sampler->base,g.chain,mule,mod::puppet::transform(anchor.pose).position,g.destination,g.first,mod::mulegoal::nativeWritable(),mod::mulegoal::nativePoke());
            if(why.empty()){++g.writes;g.first=false;}else ++g.refusals;
            g.stopped=why;
        }
        if(now<g.logAt)return;
        g.logAt=now+mod::mulegoal::LogMs;
        auto c=mod::mulegoal::read(*sampler,sampler->base,g.chain.actor);
        std::ostringstream o;o<<"MULEGOAL test: forced watch key "<<std::hex<<g.key<<std::dec<<" phase "<<(now<g.until?"writing":"after")<<" writes "<<g.writes<<" refusals "<<g.refusals<<" last "<<(g.stopped.empty()?"ok":g.stopped)<<" at ";
        if(placed)o<<mule[0]<<","<<mule[1]<<","<<mule[2];else o<<"unreadable";
        o<<mod::mulegoal::describe(c,sampler->base)<<"\n";note(o.str().c_str());
    }
    void tickEnemyPositions(uint64_t now) {
        if(!core||!sampler||!config.shareEnemyPositions||!config.shareEnemies)return;
        if(now-enemyPoseAt<200)return;
        enemyPoseAt=now;
        if(!core->connected()||!partnerMode()){enemyPoseCorrector.forget();enemySteered.clear();return;}
        auto anchor=anchorNow();if(!anchor.pose.available)return;
        auto here=mod::puppet::transform(anchor.pose).position;
        if(config.role=="host"){
            auto mules=mod::enemies::readMules(*sampler,sampler->base);if(mules.empty())return;
            std::optional<std::array<double,3>> partner;
            if(cache.remote.visible){auto& p=cache.remote.state.position;partner=std::array<double,3>{p[0],p[2],p[1]};}   // adapter -> game axes
            auto sample=enemyPoseSampler.select(mules,here,partner,now);
            if(core->sendEnemies(sample,now)&&(++enemyPoseSent==1||enemyPoseSent%300==0)){
                std::ostringstream o;o<<"ENEMYPOS host: "<<int(sample.count)<<" MULEs near the players -> partner (sample "<<enemyPoseSent<<")\n";note(o.str().c_str());}
            return;
        }
        auto shown=core->latestEnemies(now);
        if(!shown)return;                                   // nothing fresh from the host: the MULEs are this game's own
        if(!enemyMoveTried){enemyMoveTried=true;try{enemyMove=mod::enemyposes::nativeTeleport(*sampler);}catch(const std::exception& e){note((std::string("ENEMYPOS unavailable: ")+e.what()+"\n").c_str());}}
        if(!enemyMove)return;
        if(++enemyPoseSeen==1){std::ostringstream o;o<<"ENEMYPOS guest: the host's first sample, "<<int(shown->enemies.count)<<" MULEs, "<<shown->ageMs<<" ms old\n";note(o.str().c_str());}
        auto mules=mod::enemies::readMules(*sampler,sampler->base);if(mules.empty())return;
        std::vector<mod::enemyposes::Move> steer;bool goal=config.enemyPositionGoal;
        for(auto at=enemySteered.begin();at!=enemySteered.end();)if(now>at->second+mod::enemyposes::SteerHoldMs)at=enemySteered.erase(at);else ++at;
        auto moves=enemyPoseCorrector.decide(mules,&*shown,here,now,config.enemyPositionPull,goal?&enemySteered:nullptr,goal?&steer:nullptr);
        // The walk first: the MULE's own forced destination (mule_goal.h), a checked write and no call. A MULE that
        // does not walk, runs after someone or is held still refuses it and is corrected by the teleport as before.
        if(!steer.empty()){
            auto writable=mod::mulegoal::nativeWritable();auto poke=mod::mulegoal::nativePoke();
            for(auto& g:steer){
                auto chain=mod::mulegoal::read(*sampler,sampler->base,g.address);
                bool first=!enemySteered.count(g.key);
                auto why=mod::mulegoal::steer(*sampler,sampler->base,chain,g.from,here,g.position,first,writable,poke);
                if(why==mod::mulegoal::Pending)continue;              // not counted: the walk has not come round to the last one
                if(why.empty()){
                    enemySteered[g.key]=now;
                    if(++enemySteerWrites<=20||enemySteerWrites%200==0){std::ostringstream o;o<<"ENEMYGOAL guest: MULE "<<std::hex<<g.key<<std::dec<<" sent walking, it was "<<g.error<<" m from the host's, to "
                        <<g.position[0]<<","<<g.position[1]<<","<<g.position[2]<<" (writes "<<enemySteerWrites<<", refused "<<enemySteerRefused<<")\n";note(o.str().c_str());}
                } else {++enemySteerRefused;++enemySteerRefusals[why];}
            }
        }
        if(goal&&now>=enemySteerLogAt){
            enemySteerLogAt=now+10000;
            std::ostringstream o;o<<"ENEMYGOAL guest: writes "<<enemySteerWrites<<" refused "<<enemySteerRefused<<" walking now "<<enemySteered.size()<<" teleports "<<enemyPoseCorrector.snaps();
            for(auto& r:enemySteerRefusals)o<<"; "<<r.first<<" "<<r.second;
            o<<"\n";note(o.str().c_str());
        }
        for(auto& mv:moves){
            mod::enemyposes::Place p;p.position=mv.position;p.rotation=mod::enemyposes::upright(mv.heading);
            bool ok=enemyMove(mv.address,p);uint64_t n=ok?++enemyPoseMoved:++enemyPoseRefused;
            if(mv.stuck){std::ostringstream o;o<<"ENEMYPOS guest: MULE "<<std::hex<<mv.key<<std::dec<<" stayed where it was when it was put at the host's place ("<<mv.stuck<<" in a row; it may ride): the next try waits longer\n";note(o.str().c_str());}
            if(n<=20||n%50==0){std::ostringstream o;o<<"ENEMYPOS guest: MULE "<<std::hex<<mv.key<<std::dec<<(mv.snap?" teleported":" stepped")<<", it was "<<mv.error<<" m from the host's, to "
                <<mv.position[0]<<","<<mv.position[1]<<","<<mv.position[2]<<(ok?"":" REFUSED")<<" (moved "<<enemyPoseMoved<<", refused "<<enemyPoseRefused<<")\n";note(o.str().c_str());}
        }
    }
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
        if(padRunning) {bool done,failed;std::string why;{std::lock_guard<std::mutex> l(padShared.lock);done=padShared.player.done();failed=padShared.player.failed();why=padShared.player.failure();}
            if(done&&failed){padRunning=false;note(("PAD script FAILED: "+why+" (UI state: "+(uiWatch.text().empty()?"not read":uiWatch.text())+")\n").c_str());}
            else if(done){padRunning=false;note("PAD script finished\n");}}
    }
    // ui.h: which screen the game shows, read-only. A line per change while scripted input is on (virtual_pad) or after
    // `puppet.ui on`; `puppet.ui state` writes the line now, `puppet.ui dump` the candidate memory for the highlighted
    // entry. A running script's waitui step is answered here.
    std::filesystem::path uiCommand;mod::ui::Watch uiWatch;bool uiOn=false;
    void tickUi() {
        bool force=false,dump=false;auto now=mod::nowMs();
        if(!uiCommand.empty()&&std::filesystem::exists(uiCommand)) {
            auto text=readConfig(uiCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(uiCommand))throw std::runtime_error("cannot consume ui command");
            if(text=="on"+stamp)uiOn=true;else if(text=="off"+stamp)uiOn=false;
            else if(text=="state"+stamp)force=true;else if(text=="dump"+stamp)dump=true;
            else note("UI command refused (on, off, state, dump)\n");
        }
        std::string wanted;
        if(padRunning){std::lock_guard<std::mutex> l(padShared.lock);if(auto w=padShared.player.waiting(now))wanted=*w;}
        if(!sampler){if(force||dump)note("UI state: unknown (no reader in this mode)\n");return;}
        if(!force&&!dump&&wanted.empty()&&(!(uiOn||config.virtualPad)||!uiWatch.due(now)))return;
        auto state=mod::ui::read(*sampler,sampler->base,cache.available);
        if(uiWatch.changed(state,now)||force)note(("UI state: "+uiWatch.text()+"\n").c_str());
        if(dump)for(auto& row:mod::ui::dump(*sampler,sampler->base,cache.available))note((row+"\n").c_str());
        if(!wanted.empty()&&mod::ui::matches(state,wanted)){
            {std::lock_guard<std::mutex> l(padShared.lock);padShared.player.met(mod::nowMs());}
            note(("PAD waitui met: "+wanted+"\n").c_str());
        }
    }
    // One line per change, so an outside driver can wait for gameplay by reading the log.
    void notePose() {
        if(!game||(poseLogged&&posePublished==poseAvailable))return;
        poseLogged=true;posePublished=poseAvailable;
        note((std::string("POSE ")+(poseAvailable?"available":"unavailable")+" reason="+poseReason+"\n").c_str());
    }
    Cache cache;std::unique_ptr<ds::StateProvider> provider;std::unique_ptr<ds::Core> core;
    uint64_t sent=0,received=0,dropped=0,wrongAccepted=0,buildRefused=0,versionRefused=0,sessionRefused=0;
    uint64_t animationSent=0,animationShown=0;bool animationNoted=false;
    bool paired=false,failed=false;std::string failure;uint64_t startedAt=0;std::vector<double> errors;
#ifdef MOD_FAKE_NETWORK
    std::deque<ds::Bytes> outgoing;
#endif
#ifdef _WIN32
    HANDLE log=INVALID_HANDLE_VALUE,presenceLog=INVALID_HANDLE_VALUE;bool wsa=false;
#else
    int log=-1,presenceLog=-1;
#endif
    struct Pending {uint64_t receivedUs;uint64_t due;std::vector<uint8_t> b;size_t size;};std::vector<Pending> pending;mod::link::Profile linkProfile;uint64_t linkLost=0;uint32_t notedDelay=120;
    void note(const char* s) {
        diagnostic.Line(s);
        if(!std::strncmp(s,"LINK",4)||!std::strncmp(s,"STATE",5)||!std::strncmp(s,"POSE",4)||!std::strncmp(s,"HANDSHAKE",9))diagnostic.Flight(s);
        std::cerr<<"mod: "<<s<<'\n';
#ifdef _WIN32
        OutputDebugStringA(s);if(log!=INVALID_HANDLE_VALUE) {DWORD n=0;auto len=DWORD(strlen(s));if(!WriteFile(log,s,len,&n,nullptr)||n!=len||!FlushFileBuffers(log)) throw std::runtime_error("mod log write failure");}
#else
        if(log!=-1 && write(log,s,strlen(s))!=static_cast<ssize_t>(strlen(s))) throw std::runtime_error("mod log write failure");
#endif
    }
    // Group play (config without `peer`): there is no partner. The two-player core still runs, but nothing it asks to
    // send leaves this process, by the direct socket (never opened, see init) or the test queue.
    bool noPartner() const {
#ifdef MOD_MUTANT_GROUP_SENDS
        return false;
#else
        return config.group;
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
        if(noPartner())return;   // the bookkeeping above still runs (a jump flag is not kept for ever)
        #ifdef MOD_FAKE_NETWORK
        if(outgoing.size()<256) {outgoing.push_back(b);++sent;} else ++dropped;
#else
        auto n=sendto(socket,reinterpret_cast<const char*>(b.data()),b.size(),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
        if(n==static_cast<int>(b.size())) ++sent;else ++dropped;
#endif
    }
    // ANIMATION (core.h): the one longer packet, with the same identity rewrite.
    void sendLong(const std::vector<uint8_t>& input) override {
        if(input.size()<ds::WireSize||noPartner())return;
        auto b=input;std::copy(mod::WireBuild.begin(),mod::WireBuild.end(),b.begin()+16);checksum(b.data(),b.size());
#ifndef MOD_FAKE_NETWORK
        auto n=sendto(socket,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
        if(n==static_cast<int>(b.size())) {++sent;++animationSent;} else ++dropped;
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
        auto check=p.b;checksum(check.data(),check.size());
        bool checksumOK=std::equal(check.begin()+120,check.begin()+124,p.b.begin()+120);
        if(checksumOK) {std::copy(ds::ModBuild.begin(),ds::ModBuild.end(),p.b.begin()+16);checksum(p.b.data(),p.b.size());}
        auto before=core->accepted();
        auto e=core->receive(p.b.data(),p.size,now);
        if(e==ds::Error::Progress&&!progressRefusalNoted){progressRefusalNoted=true;note("HANDSHAKE refused: story progress mismatch (progress_mismatch=refuse on one peer)\n");}
        noteHandshake();
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
                {std::unique_lock<std::timed_mutex> guard(mutex,std::chrono::seconds(5));
                if(!guard.owns_lock()){
#ifndef MOD_MUTANT_NO_STALL_REPORT
                    if(!stallNoted){stallNoted=true;std::string m=std::string("STALL the game thread has held the mod for over 5 s in '")+stage.load()+"'; the mod has stopped\n";try{note(m.c_str());}catch(...){}}
#endif
                    continue;
                }
                auto clock=mod::nowUs();auto now=clock/1000;
                watchRing(clock);
                if(lastNetwork)networkTick.add(clock-lastNetwork);
                lastNetwork=clock;
                #ifndef MOD_FAKE_NETWORK
                for(unsigned budget=0;budget<64&&!config.group;++budget) {   // group play: no direct socket to read
                    sockaddr_in source{};
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
                    bool longKind=n>int(ds::WireSize)&&n<=int(ds::AnimationWireMax)&&(uint8_t(raw[6])==uint8_t(ds::Kind::Animation)||uint8_t(raw[6])==uint8_t(ds::Kind::Enemies));
                    bool fromPeer=source.sin_addr.s_addr==peer.sin_addr.s_addr&&source.sin_port==peer.sin_port;
                    // The checkpoint's datagrams (story.h) are not the core's: they carry their own secret and nonce.
                    if(fromPeer&&n>=13&&!std::memcmp(raw,"DSS1",4)&&(storyHost||storyFetch)){storyDatagram(std::string(raw,size_t(n)));continue;}
                    if((n!=int(ds::WireSize)&&!longKind)||!fromPeer) {++dropped;continue;}
                    std::vector<uint8_t> b(raw,raw+n);
                    auto fate=mod::link::fate(linkProfile,config.impairment,received,b[6]==uint8_t(ds::Kind::Presence));
                    if(fate.lost) {if(++linkLost%100==0){std::ostringstream o;o<<"LINK "<<config.link<<" lost "<<linkLost<<" of "<<received<<" received\n";note(o.str().c_str());}continue;}
                    uint64_t delay=fate.delayMs;
                    if(pending.size()>=256) {++dropped;continue;}
                    pending.push_back({mod::nowUs(),now+delay,b,size_t(n)});
                }
                #endif
                for(size_t i=0;i<pending.size();) if(pending[i].due<=now) {auto p=pending[i];pending.erase(pending.begin()+i);accept(p,now);} else ++i;
                storyPoll(now);
                core->tick(now);paired|=core->connected();
                // SEAM (others.h): the transport for other players is polled here, under this lock, and feeds
                // othersEntered / othersLeft / othersReceived; then their sessions tick like the partner's.
                relayPoll(now);
                if(others)others->tick(now);}
                cadence.wait();
            }
        } catch(const std::exception& e) {std::lock_guard<std::timed_mutex> guard(mutex);failed=true;failure=e.what();try{note(failure.c_str());}catch(...){}running=false;}
    }
    void init(const std::filesystem::path& path) {
        modFolder=path.parent_path();
        config=mod::parseConfig(readConfig(path));config.link=mod::link::chosen(config.link,std::getenv("COOP_LINK"));mod::link::profile(config.link,linkProfile);
        diagnostic.Bind(diagnosticHost);
        if(!diagnostic.Open(diag::DefaultLogsDir(),"game",config.maxLogMb))
            std::cerr<<"mod: player diagnostic log unavailable\n";
        cache.progressReady=config.provider!="game";
        if(config.logging) {
#ifdef _WIN32
            auto filename=path.parent_path()/("mod-"+mod::runStamp()+".log");safePath(filename);
            log=CreateFileW(filename.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(log==INVALID_HANDLE_VALUE) throw std::runtime_error("cannot exclusively create instance mod log");
#else
            auto filename=path.parent_path()/("mod-"+mod::runStamp()+".log");safePath(filename);log=open(filename.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(log==-1)throw std::runtime_error("cannot create mod log");
#endif
        }
        routeStart();   // a bad challenge string refuses the start with its reason
        if(config.discovery) {
            discovery=std::make_unique<mod::discovery::Worker>(path,config.discoveryOptions);
            note("read-only discovery worker started; no provider, network or game calls\n");
            return;
        }
        if(config.provider=="game") {
            // Real failures (cannot create the log, hash mismatch, signature scan failure) throw and refuse startup.
            // An unavailable pose (main menu, loading) is NORMAL: presence stays gated until a pose is available.
            openPresenceLog(path);
            sampler=openSampler();stages.sampler=sampler.get();
            vehicleCommand=path.parent_path()/"puppet.vehicle";storyFolder=path.parent_path();storyCommand=path.parent_path()/"puppet.story";
            vehicleMonitor=std::make_unique<mod::vehicle::Monitor>(*sampler);
            environmentCommand=path.parent_path()/"puppet.environment";
            environmentReader=std::make_unique<mod::environment::Reader>(*sampler);
            if(config.shareEnvironment!="off") {
                std::function<void(float,float)> setter;
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
                auto address=environmentReader->setterAddress();
                setter=[address](float hour,float seconds){reinterpret_cast<void(*)(float,float)>(address)(hour,seconds);};
#endif
                // "rain" shares the host's rain only: the guest never applies the host's hour (the time setter can
                // advance the day counter, and the game shows no change of light; STATUS.md).
                if(config.shareEnvironment=="host")environmentSync=std::make_unique<mod::environment::Sync>(*sampler,std::move(setter));
                environmentSend=true;
                note(config.shareEnvironment=="host"?"ENVIRONMENT host sync enabled: time, and the host's rain (weather.h)\n":"ENVIRONMENT rain sharing enabled (no time)\n");
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
                if(config.role=="guest"||config.role=="client") {
                    try{auto api=mod::weather::nativeApi(*sampler);auto* reader=environmentReader.get();
                        api.conditionAt=[reader](const std::array<double,3>& at)->std::optional<float>{
                            auto o=reader->read(at);if(!o.weatherAvailable||!std::isfinite(o.rainFromMap))return std::nullopt;return o.rainFromMap;};
                        weatherShare=std::make_unique<mod::weather::Share>(std::move(api),[this](const std::string& row){note(row.c_str());});}
                    catch(const std::exception& e){note((std::string("WEATHER unavailable: ")+e.what()+"\n").c_str());}
                }
#endif
            }
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
                api.local=[this]{return anchorNow();};
                api.read=[this](uint64_t e){return mod::puppet::readEntityPose(*sampler,e);};
                api.canCreate=[](uint64_t){return true;};
                api.resource=[this](uint64_t){return fake->base+0x9000;};
                api.create=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0x8000,t.position.data(),t.rotation.data());return fake->base+0x8000;};
                api.place=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0x8000,t.position.data(),t.rotation.data());};
                api.remove=[this](uint64_t){fake->mutate([&]{fake->blocked=fake->base+0x80c8;});};
#else
                auto api=mod::puppet::nativeApi(*sampler);
#ifdef MOD_PUPPET_DOUBLE_RESOURCE
                // Experiment builds only: create from the resident double's resource, which has no player controller.
                {auto getResource=api.resource;auto* sp=&*sampler;uint64_t camera=sampler->base+0x3d198e8,akukan=sampler->base+0x3d7de48;
                 api.resource=[getResource,sp,camera,akukan](uint64_t porter){auto d=mod::puppet::findDouble(*sp,porter,camera,akukan);return d?getResource(d):0;};
                 api.canCreate=[](uint64_t r){return r!=0;};}
#endif
#endif
                puppet=std::make_unique<mod::puppet::Probe>(std::move(api),[this](const std::string& row){note(row.c_str());});
                twinCommand=path.parent_path()/"puppet.double";cargoCommand=path.parent_path()/"puppet.cargo";bodyCommand=path.parent_path()/"puppet.body";gearCommand=path.parent_path()/"puppet.gear";
                markerCommand=path.parent_path()/"puppet.marker";rideCommand=path.parent_path()/"puppet.ride";helpCommand=path.parent_path()/"puppet.help";mapCommand=path.parent_path()/"puppet.map";chatCommand=path.parent_path()/"puppet.chat";
#ifdef MOD_FAKE_NETWORK
                // Fake tree: player entity 0x6000 -> CameraEntity 0xa000 -> parked double 0xa800.
                fake->mutate([&]{auto B=fake->base;fake->put<uint64_t>(0x6078,B+0xa000);
                    fake->put<uint64_t>(0xa000,B+0xff00);fake->put<uint64_t>(0xa070,B+0x6000);fake->put<uint64_t>(0xa078,B+0xa800);
                    fake->put<uint64_t>(0xa800,B+0xff10);fake->put<uint64_t>(0xa870,B+0xa000);fake->put<uint64_t>(0xa888,mod::puppet::FlagSleeping|0x551);
                    const double origin[3]{};const float identity[9]{1,0,0,0,1,0,0,0,1};
                    memcpy(fake->data.data()+0xa8c8,origin,24);memcpy(fake->data.data()+0xa8e0,identity,36);});
                mod::puppet::DoubleApi twinApi;
                twinApi.local=[this]{return anchorNow();};
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
#ifdef _WIN32
                {   // the creation-callback guard (guard_win.h), once the call site is known to be the one read
                    const uint8_t want[8]={0xff,0xd0,0xb0,0x01,0x48,0x83,0xc4,0x28};uint8_t got[8]{};   // call rax; mov al,1; add rsp,0x28
                    if(sampler->read(sampler->base+0x22bdf18,got,8)&&!std::memcmp(got,want,8)){{uint64_t size=0;for(auto& sec:sampler->sections)size=std::max(size,sec.address+sec.size-sampler->base);if(size)mod::guard::imageSize.store(size);}
                        mod::guard::install(sampler->base);note("GUARD creation callback guard installed\n");}
                    else note("GUARD not installed: the call site differs\n");
                }
#endif
                cargoCreate=mod::puppet::nativeCargoCreate(*sampler);cargoGive=mod::puppet::nativeCargoGive(*sampler);cargoDelete=mod::puppet::nativeCargoDelete(*sampler);construct=mod::puppet::nativeConstruct(*sampler);try{constructRequest=mod::puppet::nativeConstructRequest(*sampler);}catch(const std::exception& e){note((std::string("CONSTRUCT request unavailable: ")+e.what()+"\n").c_str());}partToggle=mod::puppet::nativePartToggle(*sampler);unconstruct=mod::puppet::nativeRemoveConstructions(*sampler);
                gameEvent=mod::puppet::nativeGameEvent(*sampler);
                if(config.autoContinue){continueLastSave=mod::puppet::nativeContinueLastSave(*sampler);autoContinue=mod::AutoContinue(true);}
                body=std::make_unique<mod::body::Probe>(mod::body::nativeApi(*sampler),sampler->base+0x3d7de48,[this](const std::string& row){note(row.c_str());});
#endif
#ifdef MOD_FAKE_NETWORK
                {
                    // Own body in the fake world: made at 0xc800 (the double's class), visible and awake, as the game makes it.
                    mod::puppet::OwnBodyApi oa;
                    oa.resource=[this](uint64_t porter){return mod::puppet::findDouble(*sampler,porter,fake->base+0xff00,fake->base+0xff10)?fake->base+0x9000:0;};
                    oa.create=[this](uint64_t,const mod::puppet::Transform& t){fake->mutate([&]{fake->put<uint64_t>(0xc800,fake->base+0xff10);
                        fake->put<uint64_t>(0xc888,mod::puppet::FlagVisible|0x453);});fake->setEntityPose(0xc800,t.position.data(),t.rotation.data());return fake->base+0xc800;};
                    oa.alive=[this](uint64_t b){uint64_t v=0;return sampler->read(b,&v,8)&&v==fake->base+0xff10;};
                    oa.park=[this](uint64_t){fake->mutate([&]{uint64_t f=0;memcpy(&f,fake->data.data()+0xc888,8);fake->put<uint64_t>(0xc888,(f|mod::puppet::FlagSleeping)&~uint64_t(mod::puppet::FlagVisible));});};
                    oa.dress=[](uint64_t){return true;};oa.remove=[this](uint64_t){fake->mutate([&]{fake->put<uint64_t>(0xc800,0);});};
                    own=std::make_unique<mod::puppet::OwnBody>(std::move(oa),[this](const std::string& row){note(row.c_str());});
                    mod::puppet::DoubleApi oapi=twinApi;oapi.find=[this](uint64_t){return ownEntity;};
                    auto flag=[this](uint64_t bit,bool on){fake->mutate([&]{uint64_t f=0;memcpy(&f,fake->data.data()+0xc888,8);fake->put<uint64_t>(0xc888,on?f|bit:f&~bit);});};
                    oapi.sleep=[flag](uint64_t,bool b){flag(mod::puppet::FlagSleeping,b);};oapi.visible=[flag](uint64_t,bool b){flag(mod::puppet::FlagVisible,b);};
                    oapi.place=[this](uint64_t,const mod::puppet::Transform& t){fake->setEntityPose(0xc800,t.position.data(),t.rotation.data());};
                    ownTwin=std::make_unique<mod::puppet::Double>(std::move(oapi),[this](const std::string& row){note(row.c_str());});
                }
                gameEvent=[this]{return fakeEvent.load();};
                // The other players' bodies in the fake world (others.h): up to eight entities of the double's class in
                // 0xd000..0xefff, each made visible and awake as the game makes one, driven through the same calls.
                bodyParts=[this,twinApi]{
                    constexpr size_t First=0xd000,Each=0x400,Count=8;
                    mod::others::BodyParts p;auto at=[this](uint64_t e){return size_t(e-fake->base);};
                    auto flag=[this,at](uint64_t e,uint64_t bit,bool on){fake->mutate([&]{uint64_t f=0;memcpy(&f,fake->data.data()+at(e)+0x88,8);fake->put<uint64_t>(at(e)+0x88,on?f|bit:f&~bit);});};
                    p.own.resource=[this](uint64_t porter){return mod::puppet::findDouble(*sampler,porter,fake->base+0xff00,fake->base+0xff10)?fake->base+0x9000:0;};
                    p.own.create=[this](uint64_t,const mod::puppet::Transform& t)->uint64_t{
                        size_t slot=0;bool found=false;
                        fake->mutate([&]{for(size_t i=0;i<Count&&!found;++i){uint64_t v=0;memcpy(&v,fake->data.data()+First+i*Each,8);if(!v){slot=First+i*Each;found=true;}}
                            if(found){fake->put<uint64_t>(slot,fake->base+0xff10);fake->put<uint64_t>(slot+0x88,mod::puppet::FlagVisible|0x453);}});
                        if(!found)return 0;
                        fake->setEntityPose(slot,t.position.data(),t.rotation.data());return fake->base+slot;};
                    p.own.alive=[this](uint64_t b){uint64_t v=0;return sampler->read(b,&v,8)&&v==fake->base+0xff10;};
                    p.own.park=[flag](uint64_t b){flag(b,mod::puppet::FlagVisible,false);flag(b,mod::puppet::FlagSleeping,true);};
                    p.own.dress=[](uint64_t){return true;};
                    p.own.remove=[this,at](uint64_t b){
                        // A body removed while gear copies still hang on it (others.h, undress: they come off first).
                        for(auto& [id,x]:fakePieces)if(x.parent==b)++fakeOrphaned;
                        fake->mutate([&]{fake->put<uint64_t>(at(b),0);});};
                    p.drive=twinApi;
                    p.drive.sleep=[flag](uint64_t e,bool b){flag(e,mod::puppet::FlagSleeping,b);};
                    p.drive.visible=[flag](uint64_t e,bool b){flag(e,mod::puppet::FlagVisible,b);};
                    p.drive.place=[this,at](uint64_t e,const mod::puppet::Transform& t){fake->setEntityPose(at(e),t.position.data(),t.rotation.data());};
                    p.drive.anchor=[this,at](uint64_t e,const mod::puppet::Transform& t){fake->mutate([&]{memcpy(fake->data.data()+at(e)+0x358,&t,60);});};
                    return p;};
                otherBodies=std::make_unique<mod::others::Bodies>([this](uint64_t){
                    return std::make_unique<mod::others::Body>(bodyParts(),[this](const std::string& row){note(("OTHERS "+row).c_str());});});
                // The fake engine's side of gear.h: a piece is a row in a table, with its parent, helper and visibility.
                {mod::gear::Api api;
                 api.resource=[](uint64_t e){return e;};
                 api.create=[this](uint64_t,const mod::puppet::Transform&)->uint64_t{fakePieces[fakePieceNext+=0x40]={};return fakePieceNext;};
                 api.attach=[this](uint64_t e,uint64_t parent,const std::string& helper){auto& x=fakePieces.at(e);x.parent=parent;x.helper=helper;};
                 api.visible=[this](uint64_t e,bool on){auto it=fakePieces.find(e);if(it!=fakePieces.end())it->second.visible=on;};
                 api.remove=[this](uint64_t e){fakePieces.erase(e);};
                 api.alive=[this](uint64_t e){return fakePieces.count(e)!=0;};
                 gearOn=config.partnerGear;makeGears(std::move(api));}
#endif
#if defined(MOD_PUPPET_DOUBLE_RESOURCE) && !defined(MOD_FAKE_NETWORK)
                // Experiment builds only: once the probe has created our own body, the double backend drives it
                // instead of the game's resident double.
                {auto origFind=twinApi.find;twinApi.find=[this,origFind](uint64_t player){return ownedBody?ownedBody:origFind(player);};
                 parkSleep=twinApi.sleep;parkVisible=twinApi.visible;}
#endif
                twin=std::make_unique<mod::puppet::Double>(std::move(twinApi),[this](const std::string& row){note(row.c_str());});
                twin->walk(true);   // borrow the porter's animation network so the partner walks (game_offsets.md)
                // Join at host, join_move native: the game's player teleport, once per session (join.h). Code checked here.
                if(config.joinAtHost&&config.joinMove=="native") {
#if defined(_WIN32) && !defined(MOD_FAKE_NETWORK)
                    mod::join::MoveApi move;auto* sp=&*sampler;auto vehicles=std::make_shared<mod::vehicle::Reader>(*sampler);
                    move.porter=[sp]{return mod::join::readPorter(*sp);};
                    move.gameplay=[this,sp]{(void)sp;auto a=anchorNow();
                        return provider&&!provider->isInCutsceneOrLoading()&&gameEvent&&!gameEvent()&&a.entity&&twin&&!doubleBusy(a.entity);};
                    move.riding=[vehicles]()->std::optional<bool>{auto o=vehicles->read(mod::nowUs());if(!o.available||!o.coherent)return std::nullopt;return o.riding;};
                    move.teleport=mod::join::nativeTeleport(*sampler);
                    joinMover=std::make_unique<mod::join::Mover>(std::move(move));note("JOIN native move armed: code identity verified\n");
#else
                    note("JOIN native move unavailable in this build; join_move native logs only\n");
#endif
                }
#ifndef MOD_FAKE_NETWORK
                {
                    auto made=mod::puppet::nativeApi(*sampler);auto driver=mod::puppet::nativeDoubleApi(*sampler,true);
                    auto* sp=&*sampler;uint64_t camera=sampler->base+0x3d198e8,akukan=sampler->base+0x3d7de48;
                    mod::puppet::OwnBodyApi oa;auto resourceOf=made.resource;
                    oa.resource=[resourceOf,sp,camera,akukan](uint64_t porter){auto d=mod::puppet::findDouble(*sp,porter,camera,akukan);return d?resourceOf(d):0;};
                    oa.create=made.create;oa.remove=made.remove;
                    oa.alive=[sp,akukan](uint64_t b){uint64_t v=0;return sp->read(b,&v,8)&&v==akukan;};
                    auto hide=driver.visible;auto rest=driver.sleep;oa.park=[hide,rest](uint64_t b){hide(b,false);rest(b,true);};
                    oa.dress=mod::puppet::nativeDress(*sampler,[this]{return dressMask;},[this](const std::string& row){note(row.c_str());});
                    oa.ready=[sp](uint64_t porter){return mod::puppet::wornParts(*sp,porter,sp->base+0x3cf2b18)>1;};   // dressed by the game
                    own=std::make_unique<mod::puppet::OwnBody>(std::move(oa),[this](const std::string& row){note(row.c_str());});
                    driver.find=[this](uint64_t){return ownEntity;};
                    ownTwin=std::make_unique<mod::puppet::Double>(std::move(driver),[this](const std::string& row){note(row.c_str());});
                    ownTwin->walk(true);
                    // One more Sam, made like the body above and driven by a backend of its own (others.h).
                    bodyParts=[this,sp,camera,akukan]{
                        mod::others::BodyParts p;auto made=mod::puppet::nativeApi(*sp);p.drive=mod::puppet::nativeDoubleApi(*sp,true);
                        auto resourceOf=made.resource;
                        p.own.resource=[resourceOf,sp,camera,akukan](uint64_t porter){auto d=mod::puppet::findDouble(*sp,porter,camera,akukan);return d?resourceOf(d):0;};
                        p.own.create=made.create;p.own.remove=made.remove;
                        p.own.alive=[sp,akukan](uint64_t b){uint64_t v=0;return sp->read(b,&v,8)&&v==akukan;};
                        auto hide=p.drive.visible;auto rest=p.drive.sleep;p.own.park=[hide,rest](uint64_t b){hide(b,false);rest(b,true);};
                        p.own.dress=mod::puppet::nativeDress(*sp,[this]{return dressMask;},[this](const std::string& row){note(row.c_str());});
                        p.own.ready=[sp](uint64_t porter){return mod::puppet::wornParts(*sp,porter,sp->base+0x3cf2b18)>1;};
                        // As for the partner's body: its inventory component kept quiet, and updated in this porter's group.
                        struct Kept{uint64_t quietBody=0,quietComponent=0,linkedBody=0,linkedTo=0;};auto kept=std::make_shared<Kept>();
                        p.settle=[this,kept](uint64_t body,uint64_t porter,uint64_t now){
                            quietInventoryOf(body,now,kept->quietBody,kept->quietComponent);
                            if(config.bodyUpdateLink&&porter&&(kept->linkedBody!=body||kept->linkedTo!=porter)){kept->linkedBody=body;kept->linkedTo=porter;joinUpdateGroup(body,porter);}};
                        return p;};
                    auto bodiesNamed=[this](const char* label){return std::make_unique<mod::others::Bodies>([this,label](uint64_t){
                        return std::make_unique<mod::others::Body>(bodyParts(),[this,label](const std::string& row){note((std::string(label)+row).c_str());});});};
                    crowdBodies=bodiesNamed("CROWD ");otherBodies=bodiesNamed("OTHERS ");
                    gearOn=config.partnerGear;
                    makeGears(mod::gear::nativeApi(*sampler));
                }
#endif
                // The partner shown from the start (config partner), as the puppet.double command would; the Double
                // backends wait for a porter and a partner before they borrow or drive anything.
                if(config.partner=="own"&&own&&ownTwin){twinMode=TwinMode::Own;ownTwin->enable();twin->enable();note("DOUBLE start mode own (config)\n");}
                else if(config.partner!="off"){twinMode=TwinMode::Peer;twin->enable();note("DOUBLE start mode peer (config)\n");}
            }
            presence=std::make_unique<mod::PresenceLog>([this](const std::string& line){return writePresence(line);},uint64_t(config.presenceLogMaxMiB)*1024*1024,[this](const char* s){note(s);});
            auto* raw=sampler.get();
            auto g=std::make_unique<mod::GameProvider>([raw]{return mod::PoseReader(*raw).read();},mod::nowUs,presence.get());
            // What the porter rides travels in every presence packet (read-only; vehicle.h). Kind changes are logged.
            {auto rides=std::make_shared<mod::vehicle::Reader>(*raw);auto lastKind=std::make_shared<int>(-1);
             auto steerVar=std::make_shared<mod::puppet::FloatVariable>();
             g->ride([this,rides,lastKind,steerVar,raw](const mod::Pose& p){
                auto o=rides->read(mod::nowUs());mod::Ride r;
                if(o.available&&o.riding){
                    uint8_t kind=o.kind==mod::vehicle::Kind::Motorbike?ds::Motorbike:o.kind==mod::vehicle::Kind::Truck?ds::Truck:o.kind==mod::vehicle::Kind::Car?ds::Car:0;
                    r=mod::rideFrom(kind,o.position,o.rotation,p.position);
                    // The rider's steering: the porter's own Turn_Heading control (read-only).
                    if(auto t=mod::puppet::readFloatVariable(*raw,o.porter,raw->base+0x3d09910,"Turn_Heading",*steerVar))r.steer=std::clamp(double(*t),-ds::SteerMax,ds::SteerMax);
                    if(o.variant<=ds::VehicleVariantMax){r.variant=uint8_t(o.variant);r.look=o.appearance;}
                }
                if(o.available)localVehicle={o.riding,r.kind,o.position,o.rotation,mod::nowMs(),o.riding?o.vehicle:0};   // an unavailable read keeps the last one
                if(o.available&&int(r.kind)!=*lastKind){*lastKind=r.kind;
                    std::ostringstream m;m<<"RIDE local kind="<<int(r.kind)<<" vehicle=0x"<<std::hex<<o.vehicle<<"\n";note(m.str().c_str());}
                return r;});}
            game=g.get();provider=std::move(g);
        } else provider=std::make_unique<mod::SyntheticProvider>(config.slot);
        padCommand=path.parent_path()/"pad.script";uiCommand=path.parent_path()/"puppet.ui";padShared.enabled=config.virtualPad;
        if(sampler)animatorVtable=sampler->base+0x3d09910;   // MorphemeAnimationManager (pinned build)
#ifndef MOD_FAKE_NETWORK
        if(sampler){cargoManagerSlot=sampler->base+0x7bf4968;cargoManagerVtable=sampler->base+0x3dd9338;}   // DSBaggage2Manager global, vtable
        if(sampler&&config.testMode.on){   // test mode (tickTestMode): the game's calls and memory
            testCreate=cargoCreate;
            testRecords=[this](unsigned limit){uint64_t manager=0;if(!cargoManagerSlot||!sampler->read(cargoManagerSlot,&manager,8))return std::vector<mod::cargo::Record>{};
                return mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,limit);};
            testBlock=[this](uint64_t entity){return mod::testmode::statusComponent(*sampler,sampler->base,entity);};
            porterVitals=[this](uint64_t entity){return mod::vitals::read(*sampler,entity);};
            testWritable=mod::mulegoal::nativeWritable();testPoke=mod::mulegoal::nativePoke();}
#else
        if(sampler&&config.testMode.on){   // the fake world: crates recorded, the status block at 0xf800, the state the test sets
            testCreate=[this](uint32_t hash,const mod::puppet::Transform& t){fakeDrops.push_back({hash,t.position});};
            testRecords=[this](unsigned){std::vector<mod::cargo::Record> out;uint32_t i=0;
                for(auto& d:fakeDrops){mod::cargo::Record r;r.hash=d.hash;r.position=d.at;r.state=uint32_t(mod::cargo::InWorld)<<8|mod::cargo::SharedKind;r.index=i++;out.push_back(r);}
                return out;};
            testBlock=[this](uint64_t entity){return entity?fake->base+0xf800:0;};
            porterVitals=[this](uint64_t){return fakeVitals;};
            testWritable=[this](uint64_t at,size_t n){return at>=fake->base+0xf800&&at+n<=fake->base+0xfa00;};
            testPoke=[this](uint64_t at,const void* from,size_t n){fake->mutate([&]{std::memcpy(fake->data.data()+(at-fake->base),from,n);});};}
#endif
        cache.sampledAt=mod::nowMs();
        if(!config.gameThread){cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();provider->applyRemote({});}
        if(game){poseAvailable=cache.available;poseReason=game->reason();}
        if(!cache.available && !game) throw std::runtime_error("provider unavailable: synthetic provider failed; mod startup refused");
#ifndef MOD_FAKE_NETWORK
#ifdef _WIN32
        WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup failed");wsa=true;
#endif
        // Group play (config without `peer`): no partner, so no direct socket at all: nothing is bound, nothing can be
        // sent to or taken from a partner's address. Other players come through the relay's own socket (relayStart).
        if(config.group)note("NET no partner (group play): the direct socket is not opened\n");
        else {
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
                              <<':'<<ds::portForSlot(config.slot)<<" peer "<<config.peer<<':'<<ds::portForSlot(config.peerSlot)<<"\n";
         if(config.link!="none")o<<"LINK stand-in "<<config.link<<": loss "<<linkProfile.lossPerMille/10.0<<" %, delay "<<linkProfile.delayMinMs<<".."<<linkProfile.delayMinMs+linkProfile.delaySpanMs-1<<" ms on every received packet\n";
         note(o.str().c_str());}
#ifdef _WIN32
        u_long nonblock=1;if(ioctlsocket(socket,FIONBIO,&nonblock)) throw std::runtime_error("nonblocking UDP failed");
#else
        if(fcntl(socket,F_SETFL,O_NONBLOCK)<0) throw std::runtime_error("nonblocking UDP failed");
#endif
        }
        #endif
        relayStart();
        strandStart();
        ds::Config c;c.role=(config.role=="host"||(config.role=="none"&&config.slot<config.peerSlot))?ds::Role::Host:ds::Role::Client;
        c.progressPolicy=config.progressMismatch=="refuse"?ds::ProgressPolicy::Refuse:ds::ProgressPolicy::Warn;c.session=config.session;c.nonce=nonce();
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
        if(line.find("\"animation\"")==std::string::npos)diagnostic.Flight("PRESENCE "+line);
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
                auto anchor=anchorNow();
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
    // The porter's visible gear on the own body only (never on the game's resident double). "on <stamp>" / "off
    // <stamp>" in puppet.gear; off removes the copies. The copies are hidden whenever the body is.
    void tickGear(uint64_t bodyEntity,const mod::puppet::Anchor& anchor,uint64_t now) {
        if(!gears)return;
        if(std::filesystem::exists(gearCommand)) {
            auto command=readConfig(gearCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(gearCommand))throw std::runtime_error("cannot consume gear command");
            // As the single mirror did: off removes the partner body's copies and stops; on lets it make them again.
            if(command=="on"+stamp){gearOn=true;if(auto* one=gears->of(0))one->start();note("GEAR on\n");}
            else if(command=="off"+stamp){gearOn=false;if(auto* one=gears->of(0))one->stop();note("GEAR off\n");}
            else note("GEAR ignored command for another process\n");
        }
        // gearOn: the partner's body (partner_gear, or the command). forOthers: the other players' bodies (others_gear).
        bool forOthers=config.othersGear&&others&&otherBodies;
        if(!anchor.entity)return;
        if(!gearOn&&!forOthers){if(gears->size())gears->tick(anchor.entity,{});return;}   // whatever is left comes off
        if(gearOn&&gearChurnLeft){
            auto* one=gears->of(0);
            if(gearChurnMode==1){if(one)one->flash(gearChurnLeft%2==0?false:true);--gearChurnLeft;}
            else if(now-gearChurnAt>=6000){gearChurnAt=now;if(one)one->clear();--gearChurnLeft;}
#ifdef _WIN32
            if(!gearChurnLeft)note(("GEAR test: churn done, culling guard at "+std::to_string(mod::guard::cullSkipped.load())+"\n").c_str());
#endif
        }
        // What this porter shows goes to the partner (read once a second; sent when it holds still, and on every connection).
        // Nothing is read until a load (a game event) has been over for SettleMs: right after one the porter shows gear
        // and parts he hides seconds later (live: 7 pieces sent, then 2). The same read goes to the other players, each
        // session with a sender of its own.
        constexpr uint64_t SettleMs=8000;
        bool settled=!inEvent&&now-eventOverAt>=SettleMs&&anchor.pose.available;
        if((core||forOthers)&&settled&&now-wornReadAt>=1000){wornReadAt=now;stages.enter("gear worn",mod::nowUs());
            auto nodes=gearNodes(anchor.entity);
            ownBackpack=0;for(auto& n:nodes)if(n.parent<0&&n.vtable==sampler->base+mod::gear::BackpackVtable){ownBackpack=n.entity;break;}
            auto mine=mod::gear::worn(nodes,sampler->base);
            if(core&&gearOn){auto events=wornTx.observe(mine,core->connected(),now);
                if(!events.empty()){note(("GEAR sent "+std::to_string(mine.size())+" pieces\n").c_str());for(auto& e:events)core->queueCargo(e);}}
            if(forOthers)if(auto sent=others->wears(mine,now))note(("GEAR "+std::to_string(sent)+" events of this porter's "+std::to_string(mine.size())+" pieces -> other players\n").c_str());}
        stages.enter("gear",mod::nowUs());
        uint64_t flags=0;bool shown=bodyEntity&&sampler->read(bodyEntity+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        // The outfit: this porter's art parts go to the partner; the body takes the partner's, a few parts a second.
        if(gearOn&&core&&now-outfitAt>=1000){outfitAt=now;stages.enter("gear outfit",mod::nowUs());
            if(settled)if(auto e=outfitTx.observe(mod::outfit::read(*sampler,anchor.entity),core->connected(),now)){note("OUTFIT sent\n");core->queueCargo(*e);}
            if(bodyEntity&&shown&&partnerParts&&partToggle)if(auto mine=mod::outfit::read(*sampler,bodyEntity)){
                auto todo=mod::outfit::changes(*mine,*partnerParts,4);unsigned done=0;
                for(auto& [index,on]:todo)done+=partToggle(bodyEntity,index,on);
                if(!todo.empty())note(("OUTFIT switched "+std::to_string(done)+" of "+std::to_string(todo.size())+" parts on the body\n").c_str());}}
        stages.enter("gear mirror",mod::nowUs());
        std::vector<mod::gear::Wearer> wearers;
        // The partner's body (key 0, first): the partner's own gear when they have sent it; else what this porter shows.
        if(gearOn){mod::gear::Wearer partner;partner.body=bodyEntity;partner.worn=partnerWorn;partner.local=true;wearers.push_back(std::move(partner));}
        // Every other player whose body exists (key: the player's id): what that player sent, nothing until then.
        // SEAM others_gear (off by default, not run live): per body these are the partner's calls, but the end-of-tick
        // flush of deferred rep calls (repTickEnd) watches only the partner's body. Whether attaching copies to other
        // bodies leaves calls waiting on their queues must be seen live (REP and GUARD lines, fault records) before
        // others_gear is on for anybody; generalising the watch to every body is the step after that.
        if(forOthers)for(auto& w:mod::others::wearers(othersShown,*otherBodies))wearers.push_back(std::move(w));
        gearPorter=anchor.entity;
        gears->tick(anchor.entity,wearers);
    }
    void tickTwin(uint64_t now) {
        signalBody=0;
        if(std::filesystem::exists(twinCommand)) {
            auto command=readConfig(twinCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(twinCommand))throw std::runtime_error("cannot consume double command");
            if(command=="echo"+stamp){twinMode=TwinMode::Echo;echoOwn=false;}
            else if(command=="echoown"+stamp){twinMode=TwinMode::Echo;echoOwn=true;if(ownTwin)ownTwin->enable();}   // echo on the own body (controls test)
            else if(command=="peer"+stamp)twinMode=TwinMode::Peer;
            else if(auto ownMask=mod::puppet::parseOwnCommand(command,stamp)){
                // own: the porter's art parts (mask 8: drawn, stable; stretched-polygon artifacts). ownN / ownNN (hex),
                // research only: 1 cover, 2 cover+animation, 4 facial rig (lazy, empty components: 4 hangs the game),
                // 9 = 8 and remove the body's own ArtPartsData (hangs the game); 10 the engine's model swap, 20 the swap
                // then the apply if needed, 40 a read-only report (double.h). game_offsets.md, "Own-body mode".
                dressMask=*ownMask;twinMode=TwinMode::Own;if(ownTwin)ownTwin->enable();
                note(("DOUBLE own dress mask 0x"+[](unsigned m){std::ostringstream o;o<<std::hex<<m;return o.str();}(dressMask)+"\n").c_str());}
            else if(command.rfind("crowd ",0)==0&&command.size()>stamp.size()&&command.compare(command.size()-stamp.size(),stamp.size(),stamp)==0){
                auto n=command.substr(6,command.size()-stamp.size()-6);
                if(n.empty()||n.size()>3||n.find_first_not_of("0123456789")!=std::string::npos){note("CROWD refused: crowd <0..200>\n");return;}
                crowdWanted=std::min<size_t>(200,size_t(std::stoul(n)));note(("CROWD wanted "+std::to_string(crowdWanted)+(crowdBodies?"\n":" (unavailable in this build)\n")).c_str());return;}
            else if(command=="lead"+stamp)twinMode=TwinMode::Lead;
            else if(command=="still"+stamp){twin->walk(false);if(ownTwin)ownTwin->walk(false);note("DOUBLE own (private-room) network for the next acquire\n");return;}
            else if(command=="off"+stamp)twinMode=TwinMode::None;else {note("DOUBLE ignored command for another process\n");return;}
            note(("DOUBLE command "+command.substr(0,command.find(' '))+"\n").c_str());twin->enable();echo.clear();
        }
        std::optional<mod::puppet::Transform> target;double speed=0;uint32_t actions=0;
        auto packed=[](const ds::State& s){return uint32_t(s.flags)|(uint32_t(s.action&31)<<16);};
        auto horizontal=[](const ds::State& s){return std::hypot(s.velocity[0],s.velocity[2]);};
        if(partnerMode()&&cache.remote.visible){target=mod::puppet::fromState(cache.remote.state.position,cache.remote.state.yaw);speed=horizontal(cache.remote.state);actions=packed(cache.remote.state);
            // Riding: the body sits on the seat with the vehicle's pitch and roll, in the game's riding pose.
            if(auto kind=cache.remote.state.vehicle){target->rotation=mod::rotationFromAngles(cache.remote.state.vehicleAngles);actions|=uint32_t(kind)<<mod::puppet::VehicleShift;}}
        // A partner riding along on this porter's truck (passenger.h): held at the rider's place on this game's own
        // truck, where it is now (his samples come a link delay late: at speed his body would trail behind).
        {bool aboard=target&&partnerMode()&&cache.remote.visible&&(cache.remote.state.flags&ds::Passenger)&&localVehicle.riding&&(localVehicle.kind==ds::Truck||localVehicle.kind==ds::Motorbike)&&localVehicle.at+1500>now;   // (read during this tick: at may be past now)
            // Seated in the cab beside this porter (the driver sits 0.43 m left of the truck's middle, 0.82 m ahead, 1.94 m
            // up: live), in the riding pose: held against the truck's side the body pushed the real truck (live).
            if(aboard&&localVehicle.kind==ds::Truck){auto& v=localVehicle;const double off[3]={0.43,0.82,1.94};
                for(int k=0;k<3;++k)target->position[k]=v.position[k]+off[0]*v.rotation[k]+off[1]*v.rotation[3+k]+off[2]*v.rotation[6+k];
                target->rotation=v.rotation;speed=0;actions=uint32_t(ds::Truck)<<mod::puppet::VehicleShift;}
            // On a bike: behind this porter, from where he sits now, leaning with the bike.
            else if(aboard){auto& v=localVehicle;auto me=mod::puppet::fromState(cache.local.position,cache.local.yaw);
                for(int k=0;k<3;++k)target->position[k]=me.position[k]-mod::passenger::BikeBehind*v.rotation[3+k]+mod::passenger::BikeUp*v.rotation[6+k];
                target->rotation=v.rotation;speed=0;actions=uint32_t(ds::Motorbike)<<mod::puppet::VehicleShift;}
            if(aboard!=partnerAboard){partnerAboard=aboard;std::ostringstream o;
                if(aboard)o<<"BODY the partner rides along on this "<<(localVehicle.kind==ds::Truck?"truck":"bike")<<"\n";
                else o<<"BODY the partner is off this truck (target "<<bool(target)<<" visible "<<cache.remote.visible<<" flag "<<bool(cache.remote.state.flags&ds::Passenger)<<" riding "<<localVehicle.riding<<" kind "<<int(localVehicle.kind)<<" read "<<int64_t(now)-int64_t(localVehicle.at)<<" ms ago)\n";
                note(o.str().c_str());}}
        // The partner's own animation controls (controls.h), taken every tick so that each pulse is given once. While he is
        // down, caught or riding the mod's own poses hold the body instead.
        std::optional<ds::AnimationShown> controlsShown;
        if(core&&core->connected())controlsShown=core->takeAnimation(now);
        bool mirrorOn=config.partnerAnimation&&controlsShown&&target&&partnerMode()&&cache.remote.visible&&!cache.remote.state.vehicle&&!partnerAboard
                      &&!(cache.remote.state.flags&(ds::Down|ds::Caught));
        // A partner who is down lies on his back where he fell (no locomotion underneath).
        if(config.partnerPoses&&target&&partnerMode()&&cache.remote.visible&&!cache.remote.state.vehicle){
            if(cache.remote.state.flags&ds::Down){speed=0;actions=mod::puppet::DeadPoseBit;target=mod::puppet::laidDown(*target);}   // the collapse, laid on his back
            else if(cache.remote.state.flags&ds::Caught){speed=0;actions=mod::puppet::DragPoseBit;}
            else if((cache.remote.state.flags&ds::Rest)&&!mirrorOn&&horizontal(cache.remote.state)<0.3){speed=0;actions=mod::puppet::RestPoseBit;}}
        {bool lay=target&&partnerMode()&&cache.remote.visible&&(cache.remote.state.flags&ds::Down)&&!cache.remote.state.vehicle;
            if(lay&&!config.partnerPoses){target=mod::puppet::laidDown(*target);speed=0;actions&=mod::puppet::FlagCrouchBit;}   // with poses: done above
            if(lay!=partnerLaid){partnerLaid=lay;note(lay?(config.partnerPoses?"BODY laid down: the partner is down (the game's collapse)\n":"BODY laid down: the partner is down\n"):"BODY upright again\n");}}
        if(twinMode==TwinMode::Echo&&cache.available) {
            echo.emplace_back(now,cache.local);
            while(echo.size()>1&&now-echo[1].first>=1000)echo.pop_front();
            if(now-echo.front().first>=1000) {
                auto s=echo.front().second;double y=s.yaw;
                // Game right vector (cos, -sin, 0) in adapter axes (x, up, z).
                s.position[0]+=2*std::cos(y);s.position[2]-=2*std::sin(y);
                target=mod::puppet::fromState(s.position,y);speed=horizontal(s);actions=packed(s);
                // The controls of the same moment; pulses of frames passed over go with the next one shown.
                while(echoControls.size()>1&&now-echoControls[1].first>=1000){auto r=echoControls.front().second.rose;echoControls.pop_front();
                    for(size_t k=0;k<r.size();++k)echoControls.front().second.rose[k]|=r[k];}
                if(config.partnerAnimation&&!echoControls.empty()&&now-echoControls.front().first>=1000){
                    controlsShown=ds::AnimationShown{echoControls.front().second,0,true};echoControls.front().second.rose={};mirrorOn=true;}
            }
        }
        if(twinMode==TwinMode::Lead&&cache.available) {
            // Single-copy view test: the double walks 3 m ahead and 1.5 m right of the porter, in front of the camera.
            auto s=cache.local;double y=s.yaw;
            s.position[0]+=3*std::sin(y)+1.5*std::cos(y);s.position[2]+=3*std::cos(y)-1.5*std::sin(y);
            target=mod::puppet::fromState(s.position,y);speed=horizontal(s);actions=packed(s);
        }
        if(twinMode==TwinMode::None)echo.clear();
        if(twinMode!=TwinMode::Echo){echoControls.clear();echoHave=false;}
        // During a local game event or cutscene the double goes back to the game, so it never appears in one.
        bool event=gameEvent&&gameEvent();
        if(event!=inEvent){inEvent=event;if(!event)eventOverAt=now;note(event?"DOUBLE game event: double handed back\n":"DOUBLE game event over\n");}
        // A partner who is only missing for now (late packets, private room) is hidden, not handed back; a game event
        // or the mode switched off hands the double back.
        const mod::puppet::Transform* goal=(target&&!event)?&*target:nullptr;bool hold=twinMode!=TwinMode::None&&!event;
        // Test toggle: hide the body (kept, not handed back) while the partner's vehicle is drawn.
        if(goal&&rideHideRider&&partnerMode()&&cache.remote.state.vehicle&&ride&&ride->entity()&&ride->kind()==cache.remote.state.vehicle)goal=nullptr;
        // Own body: made (once per world) and driven by its own backend; the resident double is released, never
        // dropped while borrowed. Until the body exists, or if the game refuses to make one, the resident double is used.
        bool useOwn=(twinMode==TwinMode::Own||(twinMode==TwinMode::Echo&&echoOwn))&&own&&ownTwin&&!own->exhausted();
        auto anchor=anchorNow();
        stages.enter("body make",mod::nowUs());
        if(useOwn)ownEntity=own->tick(anchor);
        stages.enter("body drive",mod::nowUs());
        {double steer=partnerMode()&&cache.remote.visible?cache.remote.state.steer:0;twin->steering(steer);if(ownTwin)ownTwin->steering(steer);}
        // Continuous controls blended between the two samples around the shown time (controls.h).
        std::optional<ds::Animation> blended;if(mirrorOn&&controlsShown)blended=mod::controls::blend(*controlsShown);
        const ds::Animation* mirrorSample=blended?&*blended:nullptr;bool mirrorFresh=mirrorSample&&controlsShown->fresh;
        partnerHands.clear();if(blended)for(size_t i=0;i<blended->handCount;++i)partnerHands.push_back(mod::carryplace::fromHeld(blended->hands[i]));
        // The partner's wheels (wheels.h), blended towards the next sample when it is for the same model.
        partnerJoints.clear();partnerSkeleton=0;
        if(controlsShown&&controlsShown->animation.jointCount){const auto& a=controlsShown->animation;partnerSkeleton=a.skeleton;
            for(size_t i=0;i<a.jointCount;++i)partnerJoints.push_back(mod::wheels::fromWire(a.joints[i]));
            if(controlsShown->between&&controlsShown->next.jointCount&&controlsShown->next.skeleton==a.skeleton){std::vector<mod::wheels::Joint> next;
                for(size_t i=0;i<controlsShown->next.jointCount;++i)next.push_back(mod::wheels::fromWire(controlsShown->next.joints[i]));
                partnerJoints=mod::wheels::blend(partnerJoints,next,controlsShown->fraction);}}
        // The partner's floating carriers (carts.h): made here by the game's own creator and placed at his body.
        if(config.partnerCart){
#ifdef _WIN32
            if(!cartTried){cartTried=true;try{cartApi=mod::carts::nativeApi(*sampler);}catch(const std::exception& e){note((std::string("CART unavailable: ")+e.what()+"\n").c_str());}}
#endif
            if(cartApi){std::vector<mod::carts::Place> places;
                if(goal&&controlsShown&&partnerMode()&&cache.remote.visible)
                    for(size_t i=0;i<controlsShown->animation.cartCount;++i)places.push_back(mod::carts::place(controlsShown->animation.carts[i],goal->position));
                // A copy this porter attached is the partner's, not his: it goes, and a new one is made at the partner.
                if(cartDisplay.count()){auto shown=cartDisplay.ids();
                    for(auto& c:mod::carts::read(*sampler,sampler->base))if(c.attached&&shown.count(c.id)){cartApi->remove(c.id);note("CART this porter took the partner's cart: removed, made again at the partner\n");}}
                stages.enter("carts show",mod::nowUs());cartDisplay.show(places,*cartApi,now);
                if(cartDisplay.count()!=partnerCartsNoted){partnerCartsNoted=cartDisplay.count();note(("CART the partner's floating carriers here: "+std::to_string(partnerCartsNoted)+"\n").c_str());}}}
        if(mirrorSample&&!animationNoted){animationNoted=true;note("BODY moves with the partner's own animation controls\n");}
        if(mirrorSample)++animationShown;
        if(useOwn&&ownEntity){
            twin->mirror(nullptr,false);ownTwin->mirror(mirrorSample,mirrorFresh);
            twin->tick(true,nullptr);ownTwin->tick(true,goal,speed,actions,hold);
        }
        else{twin->mirror(mirrorSample,mirrorFresh);if(ownTwin)ownTwin->mirror(nullptr,false);
            twin->tick(true,goal,speed,actions,hold);if(ownTwin)ownTwin->tick(true,nullptr);}
        stages.enter("crowd",mod::nowUs());
        tickCrowd(anchor,goal,mirrorSample,mirrorFresh,goal?speed:0,goal?actions:0,now);
        stages.enter("others",mod::nowUs());
        tickOthers(anchor,event,now);tickRepGuard(now);
        twin->mirror(nullptr,false);if(ownTwin)ownTwin->mirror(nullptr,false);   // the sample lives only for this tick
        stages.enter("gear",mod::nowUs());
        repWatch=useOwn?ownEntity:0;
        quietBodyInventory(useOwn?ownEntity:0,now);
        if(useOwn&&ownEntity)linkBodyUpdate(ownEntity,anchor.entity);
        tickGear(useOwn?ownEntity:0,anchor,now);
        stages.enter("stack",mod::nowUs());
        tickStack(useOwn?ownEntity:0,anchor);
        stages.enter("tag",mod::nowUs());
        signalBody=useOwn?ownEntity:0;
        tickTag(useOwn?ownEntity:0,anchor);
        stages.enter("ride",mod::nowUs());
        tickRide(anchor,now);
    }
    bool tickOnce(std::string& err,bool gameContext=false) {
        std::lock_guard<std::timed_mutex> guard(mutex);
        stages.enter("start",mod::nowUs());++tickId;
        if(failed) {err=failure;return false;}
        try {
            auto clock=mod::nowUs();auto now=clock/1000;
            if(core)for(auto& e:core->takeCargo(mod::bugmark::WireType))
                if(auto mark=mod::bugmark::Decode(e))diagnostic.Bugmark(*mark);
            // Group play: the marks of the players near, passed on by the server (the sender's name as its roster gave it).
            if(crowd)for(auto& m:crowd->client().takeMarks())
                diagnostic.Bugmark(m.mark,diag::UtcMs(),m.name.empty()?"player "+std::to_string(m.source):m.name);
            if(lastProvider)providerTick.add(clock-lastProvider);
            lastProvider=clock;++ticks;
            if(auto mark=config.group?bugButton.press(modKey(mod::bugmark::Key),mod::bugmark::GroupPrefix(config.playerId),mod::bugmark::GroupBits)
                                     :bugButton.press(modKey(mod::bugmark::Key),config.slot)) {
                diagnostic.Bugmark(*mark);
                if(core&&core->connected()&&!config.group)core->queueCargo(mod::bugmark::Encode(*mark));
                // Group play over the broadcast relay: to the server (its log) and the players near who see this porter.
#ifndef MOD_MUTANT_MARK_GROUP
                if(crowd&&!crowd->client().mark(*mark,now))
                    diagnostic.Line("BUGMARK "+std::to_string(*mark)+" not sent to the server: "+(crowd->client().online()?"at most one every 2 s and 30 an hour":"offline"));
#endif
            }
            if(now>=flightAt){flightAt=now+250;std::ostringstream row;
                row<<"STATE presence="<<cache.available<<" gated="<<cache.gated<<" link="<<(core&&core->connected())
                   <<" sent="<<sent<<" received="<<received<<" dropped="<<dropped
                   <<" local="<<cache.local.position[0]<<","<<cache.local.position[1]<<","<<cache.local.position[2]
                   <<" partner="<<cache.remote.state.position[0]<<","<<cache.remote.state.position[1]<<","<<cache.remote.state.position[2];
                diagnostic.Flight(row.str());}

            stages.enter("provider",mod::nowUs());
            cache.sampledAt=now;cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();
            // Riding along on the partner's truck (passenger.h): the porter moves with the truck, not on his feet. The
            // partner's game shows his body from these samples: with no speed it holds still at the truck's side
            // instead of running beside it.
            if(cache.available&&rider.riding()){cache.local.velocity={0,0,0};cache.local.flags|=ds::Passenger;}
            // Sitting down to rest: the porter's own Is_TakeABreak (his rest state writes it).
            if(cache.available&&sampler){auto a=anchorNow();if(a.entity){auto v=mod::puppet::readFloatVariable(*sampler,a.entity,sampler->base+0x3d09910,"Is_TakeABreak",restVar);
                // A bool control's byte read as a float is a denormal, which the game's floating-point mode takes for zero: the bits.
                uint32_t bits=0;if(v)std::memcpy(&bits,&*v,4);bool resting=(bits&0xff)!=0;if(resting)cache.local.flags|=ds::Rest;
                if(resting!=localResting){localResting=resting;note(resting?"REST this porter sits down to rest\n":"REST this porter is up again\n");}}}
            stages.enter("route",mod::nowUs());
            tickRoute(now);
            // The porter's own animation controls (controls.h), every frame: a one-frame pulse is seen only so.
            stages.enter("controls",mod::nowUs());
            bool othersUp=others&&others->connected();   // other players (others.h) get the controls too, at their own rate
            othersControls.store(othersUp,std::memory_order_relaxed);
            controlsWanted.store(config.partnerAnimation&&cache.available&&sampler&&core&&(core->connected()||othersUp),std::memory_order_release);
            if(config.partnerAnimation&&cache.available&&sampler&&core&&(core->connected()||othersUp||twinMode==TwinMode::Echo)){auto a=anchorNow();
                std::lock_guard<std::mutex> controlsGuard(controlsLock);
                if(!othersUp&&othersGather.ready())othersGather.reset();
                std::array<uint8_t,ds::AnimationBoolBytes> held{};std::array<float,ds::AnimationFloats> floats{};
                controlEntity=a.entity;
                if(a.entity&&mod::controls::readPorter(*sampler,a.entity,animatorVtable,controlBinding,held,floats)){
                    if(controlBinding.rebound){controlBinding.rebound=false;std::ostringstream o;
                        o<<"CONTROLS the porter's animation manager has "<<controlBinding.count<<" controls; not mirrored ("<<controlBinding.unknown.size()<<"):";
                        for(auto& n:controlBinding.unknown)o<<" "<<n;
                        o<<"\n";note(o.str().c_str());}
                    if(twinMode==TwinMode::Echo){ds::Animation e;e.held=held;e.floats=floats;
                        if(echoHave)for(size_t k=0;k<held.size();++k)e.rose[k]=uint8_t(held[k]&~echoLast[k]);
                        echoLast=held;echoHave=true;echoControls.emplace_back(now,e);}
                    controlGather.add(held,floats);
                    // Lab (record_animation): a sent sample into the presence log, on its clock, for a stand-in to replay.
                    auto record=[this](const ds::Animation& sent){if(!config.recordAnimation||!presence||presence->stopped())return;
                        auto pay=ds::encodeAnimationPayload(sent);
                        static const char hex[]="0123456789abcdef";std::string h;h.reserve(pay.size()*2);for(auto b:pay){h+=hex[b>>4];h+=hex[b&15];}
                        writePresence("{\"t_us\":"+std::to_string(mod::nowUs())+",\"animation\":\""+h+"\"}\n");};
                    if(othersUp){othersGather.add(held,floats);auto mine=othersGather.current();
                        // The carts he tows, as in the partner's sample; the copies shown here for others stay out.
                        if(config.othersCarts&&a.pose.available){
                            auto towed=mod::carts::towed(mod::carts::read(*sampler,sampler->base),mod::puppet::transform(a.pose).position,otherCartIds());
                            mine.cartCount=uint8_t(std::min(towed.size(),mine.carts.size()));for(size_t i=0;i<mine.cartCount;++i)mine.carts[i]=towed[i];
                            if(towed.size()!=groupCartsNoted){groupCartsNoted=towed.size();note(("CART this porter tows "+std::to_string(groupCartsNoted)+" floating carriers (to the others)\n").c_str());}}
                        if(others->animation(mine,now)&&!core->connected())record(mine);   // with no partner, what the others were sent
                        othersGather.sent();}
                    // What he holds in his hands, relative to his root: it moves with the mirrored arms (carry_visual).
                    stages.enter("hands",mod::nowUs());
                    {std::array<ds::HeldCase,ds::AnimationHands> hands{};uint8_t count=0;uint64_t manager=0;
                     if(config.carryVisual&&cargoManagerSlot&&a.pose.available&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
                         if(auto inHands=mod::carryplace::readPlaced(*sampler,a.entity,sampler->base+mod::carry::ComponentVtable,manager,mod::puppet::transform(a.pose),true,&handSlots,now))
                             for(auto& p:*inHands)if(count<hands.size())hands[count++]=mod::carryplace::toHeld(p);
                     controlGather.hands(hands,count);
                     if(count!=handsNoted){handsNoted=count;note(("CARRY this porter holds "+std::to_string(count)+" cases in his hands\n").c_str());}}
                    // Driving: the vehicle's wheel joints as the game posed them (wheels.h); the controls stay out of that
                    // sample (the partner does not mirror them on a vehicle, and the datagram must stay under the MTU).
                    auto sample=controlGather.current();
                    // The carts he tows, relative to his root (carts.h); the ones drawn here for the partner stay out.
                    if(config.partnerCart&&a.pose.available){stages.enter("carts",mod::nowUs());
                        auto towed=mod::carts::towed(mod::carts::read(*sampler,sampler->base),mod::puppet::transform(a.pose).position,cartDisplay.ids());
                        sample.cartCount=uint8_t(towed.size());for(size_t i=0;i<towed.size();++i)sample.carts[i]=towed[i];
                        if(towed.size()!=cartsNoted){cartsNoted=towed.size();note(("CART this porter tows "+std::to_string(cartsNoted)+" floating carriers\n").c_str());}}
                    if(config.partnerVehicle&&localVehicle.riding&&localVehicle.entity&&localVehicle.at+1500>now){
                        stages.enter("wheels",mod::nowUs());
#ifdef _WIN32
                        if(auto w=mod::wheels::read(*sampler,localVehicle.entity,localVehicle.kind)){
                            sample.held={};sample.rose={};sample.floats={};sample.skeleton=w->skeleton;sample.jointCount=uint8_t(std::min(w->joints.size(),sample.joints.size()));
                            for(size_t i=0;i<sample.jointCount;++i)sample.joints[i]=mod::wheels::toWire(w->joints[i]);
                            if(!wheelsReadNoted){wheelsReadNoted=true;note(("WHEELS this porter's vehicle: "+std::to_string(sample.jointCount)+" joints of "+std::to_string(w->skeleton)+" sent\n").c_str());}}
#endif
                    }
                    if(core->connected()&&core->sendAnimation(sample,now)){controlGather.sent();
                        record(sample);}}}
            stages.enter("start",mod::nowUs());
            // A bounded, read-only observation; never writes story or save state.
            if(game&&cache.available&&sampler&&now>=nextProgressRead){
                auto progress=mod::join::ProgressReader(*sampler).read();cache.story=progress.value;nextProgressRead=now+1000;
                // Hold the first handshake until the episode is known (the first reads, before the world has loaded,
                // return unknown), but never more than 20 s after the first read.
                if(!firstProgressRead)firstProgressRead=now;
#ifdef MOD_FAKE_NETWORK
                constexpr uint64_t progressWaitMs=0;      // the fake world carries no story data: nothing to wait for
#else
                constexpr uint64_t progressWaitMs=20000;
#endif
                cache.progressReady=progress.value.episode!=ds::UnknownProgress||now-firstProgressRead>=progressWaitMs;
                if(progress.reason!=progressReason){progressReason=progress.reason;   // why an episode is (not) known, once per change
                    note(("PROGRESS episode="+(progress.value.episode==ds::UnknownProgress?std::string("unknown"):std::to_string(progress.value.episode))+" reason="+progress.reason+"\n").c_str());}
            }
            if(game&&cache.available&&gameContext) {
                // What the porter is doing, from his own animation variables (read-only): sprint and crouch.
                auto anchor=anchorNow();
                stages.enter("actions",mod::nowUs());
                if(anchor.entity){auto a=mod::puppet::readActions(*sampler,anchor.entity,animatorVtable,actionCache);
                    cache.local.flags|=uint16_t(a&0xffff);cache.local.action=uint8_t((a>>16)&31);}
                // In the private room the game uses this porter's own double: tell the partner, whose core then hides us.
                // Same for a game event or cutscene: the partner hides us instead of showing a porter in a cutscene pose.
                if(anchor.entity&&twin&&(doubleBusy(anchor.entity)||inEvent))cache.local.flags|=ds::PrivateRoom;
                // Down or caught by BTs (vitals.h), read ten times a second; the partner is told in his info log.
                if(now>=vitalsAt){
                    vitalsAt=now+100;auto v=mod::vitals::read(*sampler,anchor.entity);vitalsFlags=vitalsReport.flags(now,v);
                    if(!(v==vitalsSeen)){vitalsSeen=v;note(("VITALS known="+std::to_string(v.known)+" dead="+std::to_string(v.dead)+" caught="+std::to_string(v.caught)+" crouch="+std::to_string(v.crouch)+"\n").c_str());}}
                cache.local.flags|=vitalsFlags;
                // A call-out (the shout key), from the upper-body layer's two values.
                {bool callingNow=mod::vitals::calling(mod::puppet::readFloatVariable(*sampler,anchor.entity,animatorVtable,"Basic_Upper_Type",callTypeVar),
                                                     mod::puppet::readFloatVariable(*sampler,anchor.entity,animatorVtable,"Basic_Upper_Force_Type",callForceVar));
                    if(callingNow!=callSeen){callSeen=callingNow;note(callingNow?"CALL the porter calls out\n":"CALL over\n");}
                    cache.local.flags|=callReport.flags(now,callingNow);}
                stages.enter("start",mod::nowUs());
            }
            bool jumped=cache.available&&(cache.local.flags&ds::Teleport);   // the provider's own flag, on the tick it rose
            if(cache.available) {
                // A jump flag must survive until a presence packet carrying it has been sent.
                if(cache.local.flags&ds::Teleport)cache.stickyTeleport=true;else if(cache.stickyTeleport)cache.local.flags|=ds::Teleport;
            }
            // The other players' sessions read this porter from here (each keeps the jump until it has sent it).
            if(others){mod::others::Local l;l.state=cache.local;l.available=cache.available;l.gated=cache.gated;l.jumped=jumped;l.sampledAt=cache.sampledAt;l.area=gameArea;others->local(l);}
            if(game){poseAvailable=cache.available;poseReason=game->reason();}
            // Read-only, explicit per-process command; also usable with the ordinary self-tick provider.
            if(vehicleMonitor) {
                if(std::filesystem::exists(vehicleCommand)) {
                    auto command=readConfig(vehicleCommand);
                    if(!std::filesystem::remove(vehicleCommand))throw std::runtime_error("cannot consume vehicle command");
                    note(vehicleMonitor->command(command,mod::runStamp(),clock)?"VEHICLE command accepted\n":"VEHICLE ignored command for another process or malformed command\n");
                }
                auto row=vehicleMonitor->tick(clock);if(!row.empty())note(row.c_str());
            }
            // Render at the actual apply time, not the preceding network iteration.
            core->frame(now);provider->applyRemote(cache.remote);
            // The delay follows the link (core): say so when it has moved, so a log shows what the link was like.
            if(core->connected()){uint32_t d=core->presentationDelayMs();if(d>notedDelay+30||d+30<notedDelay){
                std::ostringstream o;o<<"LINK partner shown "<<d<<" ms behind (was "<<notedDelay<<"), round trip "<<std::lround(core->rttMs())<<" ms\n";note(o.str().c_str());notedDelay=d;}}
            stage="cull wait";stages.enter("cull wait",mod::nowUs());
            if(twin&&gameContext)cullQuiet(now);
            repTickStart();
            stage="puppet";stages.enter("puppet",mod::nowUs());
            if(puppet&&gameContext) {
                if(!puppet->armed()&&std::filesystem::exists(puppetCommand)) {
                    auto command=readConfig(puppetCommand);
                    if(command=="spawn "+mod::runStamp()+"\n") {
                        if(!std::filesystem::remove(puppetCommand))throw std::runtime_error("cannot consume puppet command");
                        note("PUPPET command consumed for this process; one experiment only\n");puppet->arm();
                    } else if(!staleCommandNoted){note("PUPPET ignored command for another process\n");staleCommandNoted=true;}
                }
                puppet->tick(now,true);
#ifdef MOD_PUPPET_DOUBLE_RESOURCE
                if(puppet->actor()&&!ownedBody){
                    // Created visible and awake; the double backend takes only a parked body, so park it first.
                    ownedBody=puppet->actor();if(parkVisible)parkVisible(ownedBody,false);if(parkSleep)parkSleep(ownedBody,true);
                    note("BODY own body parked and ready for the double backend\n");}
#endif
            }
            stage="double";stages.enter("double",mod::nowUs());
            if(twin&&gameContext)tickTwin(now);
            // Join-at-host: runs only in the verified game callback. Moves the porter only with join_move native.
            if(config.joinAtHost&&gameContext){
                mod::join::Input input;input.enabled=true;input.gameThread=true;input.connected=core->connected();input.connection=core->connectionSerial();
                input.role=(config.role=="guest"||config.role=="client")?mod::join::Role::Guest:config.role=="host"?mod::join::Role::Host:mod::join::Role::None;
                input.localGameplay=cache.available&&!cache.gated&&twin&&gameEvent&&!inEvent;
                input.local=cache.local;input.host=core->latestPresence(now);
                if(auto plan=joinDecision.tick(input)){
                    if(!joinMover)note(mod::join::wouldMove(*plan).c_str());
                    else{note(mod::join::willMove(*plan).c_str());stage="join move";stages.enter("join move",mod::nowUs());note(joinMover->start(*plan,now).c_str());}
                }
                if(joinMover){stage="join settle";stages.enter("join settle",mod::nowUs());auto line=joinMover->tick(now);if(!line.empty())note(line.c_str());}
            }
            // End join dispatch.
            // Environment dispatch: reads are optional; writes require the independently attested callback.
            if(environmentReader&&(environmentSend||std::filesystem::exists(environmentCommand))) {
                bool requested=false;
                if(std::filesystem::exists(environmentCommand)) {
                    auto command=readConfig(environmentCommand);
                    if(!std::filesystem::remove(environmentCommand))throw std::runtime_error("cannot consume environment command");
                    requested=mod::environment::readCommand(command,mod::runStamp());
                    if(!requested)note("ENVIRONMENT ignored command for another process or malformed command\n");
                }
                auto anchor=environmentSend&&gameContext?anchorNow():mod::puppet::Anchor{};
                bool gameplay=cache.available&&!cache.gated&&gameContext&&gameEvent&&!gameEvent()&&!(cache.local.flags&ds::PrivateRoom)&&
                    anchor.entity&&twin&&!doubleBusy(anchor.entity);
                std::array<double,3> position{cache.local.position[0],cache.local.position[2],cache.local.position[1]};
                if(!cache.available)position.fill(std::nan("")); // no stale porter position for a weather query in menus
                if(requested||(environmentSend&&gameplay&&now>=environmentReadAt)) {
                    auto observation=environmentReader->read(position);
                    // A host reads every second: log a reading only when it changed (or a test asked for it), not 3600 lines an hour.
                    auto envRow=mod::environment::row(observation);
                    if(requested||envRow!=environmentLastRow){note(envRow.c_str());environmentLastRow=envRow;}
                    environmentReadAt=now+1000;
                    if(environmentSend&&gameplay&&observation.available&&config.role=="host"){
                        ds::Environment sample{uint32_t(std::min<double>(ds::EnvironmentDayMs-1,std::llround(double(observation.hour)*3600000)))};
                        if(observation.weatherAvailable&&std::isfinite(observation.rainFromMap)){sample.haveRain=true;sample.rain=std::clamp(observation.rainFromMap,-ds::EnvironmentRainMax,ds::EnvironmentRainMax);}
                        core->sendEnvironment(sample,now);}
                }
                if(environmentSync&&gameContext&&now>=environmentApplyAt) {
                    environmentApplyAt=now+1000;
                    mod::environment::Input input;input.enabled=true;input.guest=config.role=="guest"||config.role=="client";
                    input.gameThread=gameContext;input.networkEnforced=gameContext; // dispatch Gate requires network_enforced==1
                    input.gameplay=gameplay;input.connected=core->connected();input.nowMs=now;input.host=core->latestEnvironment(now);input.gamePosition=position;
                    auto line=environmentSync->tick(input);if(!line.empty())note(line.c_str());
                }
                // Shared rain (guest): the host's last rain sample (sent every 30 s) is used for 40 s, placed at the
                // partner's position in this world.
                if(weatherShare&&gameContext) {
                    if(auto e=core->latestEnvironment(now);e&&e->haveRain){hostRain=e->rain;hostRainAt=now;}
                    if(hostRain&&now-hostRainAt>40000)hostRain.reset();
                    std::optional<std::array<double,3>> partnerAt;
                    if(cache.remote.visible){auto& r=cache.remote.state.position;partnerAt=std::array<double,3>{r[0],r[2],r[1]};}
                    weatherShare->tick(now,gameplay&&core->connected(),hostRain,partnerAt);
                }
            }
            // End environment dispatch.
            stage="cargo";stages.enter("cargo",mod::nowUs());
            if(twin&&gameContext)tickCargo(now);
            stage="carry";stages.enter("carry",mod::nowUs());   // ds3
            if(twin&&gameContext)tickCarry(now);
            stage="structures";stages.enter("structures",mod::nowUs());
            if(twin&&gameContext)tickStructures(now);
            stage="enemies";stages.enter("enemies",mod::nowUs());
            if(twin&&gameContext)tickEnemies(now);
            if(gameContext){tickGroupEnemies(now);tickGroupOrders(now);tickGroupReplays(now);tickGroupRoads(now);}
            if(twin&&gameContext)tickEnemyPositions(now);
            if(gameContext)tickMuleGoto(now);
            stage="bts";stages.enter("bts",mod::nowUs());
            if(twin&&gameContext)tickBts(now);
            if(gameContext)tickFights(now);
            if(twin&&gameContext)tickVoidouts(now);
            stage="roads";stages.enter("roads",mod::nowUs());
            if(twin&&gameContext)tickRoads(now);
            stage="orders";stages.enter("orders",mod::nowUs());
            if(twin&&gameContext)tickOrders(now);
            if(twin&&gameContext)tickRewards(now);
            stage="story";stages.enter("story",mod::nowUs());
            if(gameContext)tickStory(now);
            tickGuard();
            stage="markers";stages.enter("markers",mod::nowUs());
            if(twin&&gameContext)tickMarkers(now);
            stage="likes";stages.enter("likes",mod::nowUs());
            if(twin&&gameContext)tickLikes(now);
            if(gameContext)tickGroupPings(now);
            stage="signals";stages.enter("signals",mod::nowUs());
            if(twin&&gameContext)tickSignals(now);
            stage="online";stages.enter("online",mod::nowUs());
            if(gameContext)tickOnline(now);
            stage="received";stages.enter("received",mod::nowUs());
            if(gameContext){tickReceived();tickLiveBuild(now);tickGroupCargo(now);}
            stage="offer";stages.enter("offer",mod::nowUs());
            if(gameContext)tickOffer(now);
            stage="test mode";stages.enter("test mode",mod::nowUs());
            if(gameContext)tickTestMode(now);
            stage="debug menu";stages.enter("debug menu",mod::nowUs());
            if(gameContext)tickDebugMenu(now);
            stage="server page";stages.enter("server page",mod::nowUs());
            if(gameContext)tickServerInfo(now);
            stage="map";stages.enter("map",mod::nowUs());
            if(gameContext){tickArea();tickPlayerMap(now);tickHudMarkers(now);tickServerWorld(now);tickTow(now);tickReplayWatch(now);}
            stage="chat";stages.enter("chat",mod::nowUs());
            if(gameContext)tickChat(now);
            stage="notices";stages.enter("notices",mod::nowUs());
            if(gameContext)tickNotices(now);
            stage="help";stages.enter("help",mod::nowUs());
            if(gameContext)tickHelp(now);
            stage="cargo command";stages.enter("cargo command",mod::nowUs());
            if(cargoCreate&&gameContext&&std::filesystem::exists(cargoCommand)) {
                auto text=readConfig(cargoCommand);
                if(!std::filesystem::remove(cargoCommand))throw std::runtime_error("cannot consume cargo command");
                auto id=mod::puppet::parseCargoCommand(text,mod::runStamp());auto anchor=anchorNow();
                auto give=mod::puppet::parseCargoCommand(text,mod::runStamp(),"give");
                auto erase=mod::puppet::parseCargoCommand(text,mod::runStamp(),"delete");
                auto list=mod::puppet::parseCargoCommand(text,mod::runStamp(),"list");
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perfrate"))perfRate=uint8_t(std::min<uint32_t>(*v,9));
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perfshadow"))perfShadow=uint8_t(std::min<uint32_t>(*v,9));
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perfhide"))perfHideMetres=double(*v);
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perffar"))perfFarMetres=double(*v);
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"reptrace"))reptraceWanted=*v!=0;
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"replay")){
                    if(!replayCallsUsable())note("REPLAY unavailable: the functions differ\n");
                    else{replayWatch=true;
#ifdef _WIN32
                        uint64_t s=0;
                        if(*v&&sampler->read(sampler->base+0x7c0aff8,&s,8)&&s){reinterpret_cast<void(*)(uint64_t,uint64_t,bool,bool)>(sampler->base+0x33b80a0)(s,uint64_t(*v),false,false);note(("REPLAY asked the game to start mission "+std::to_string(*v)+"\n").c_str());}
#endif
                        note("REPLAY watching\n");}}
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"replaylog")){
                    replayLog=*v!=0;replayWatch=replayWatch||replayLog;
                    if(replayLog&&!mod::replayhooks::installed()){try{mod::replayhooks::install(*sampler);note("REPLAYLOG hooks on\n");}catch(const std::exception& e){note((std::string("REPLAYLOG unavailable: ")+e.what()+"\n").c_str());}}}
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"privateroom")){replayWatch=true;requestPrivateRoom(*v!=0);}
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"replaycode")){
                    // CODE (0x1f + row; ranking 0x24..0x2b), plus 256 for the game's flag call first (writeReplayCode).
                    replayWatch=true;const int32_t code=int32_t(*v&0xff);
                    note(writeReplayCode(code,(*v&0x100)!=0)?("REPLAY code "+std::to_string(code)+" written to the private room's blackboard\n").c_str():"REPLAY code not written: not in the private room, or the flag call's function differs\n");}
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"bosslife")){bossLifeSurvey=*v!=0;note(bossLifeSurvey?"BOSSLIFE survey on\n":"BOSSLIFE survey off\n");}
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perfminlod"))perfMinLod=std::min<uint32_t>(*v,99);
                if(auto v=mod::puppet::parseCargoCommand(text,mod::runStamp(),"perfpbd"))perfPbd=std::min<uint32_t>(*v,999);
                if(give&&cargoGive){cargoGive(*give);note(("CARGO give id="+std::to_string(*give)+"\n").c_str());}
                else if(auto hand=mod::puppet::parseCargoCommand(text,mod::runStamp(),"hand");hand&&(*hand==6||*hand==7)&&anchor.entity) {
                    // Lab: the top case of the porter's back into a hand slot (6 left, 7 right) with the game's own calls,
                    // as its hand-to-carrier move does (27eb050): remove 2c50150(carrier, record, 0x4f, 1, 0), insert
                    // 2c7dbd0(carrier, slot, record, 2, 1). The case stays only while the player grips (both mouse buttons).
#ifdef _WIN32
                    static const uint8_t removeHead[8]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x89},insertHead[8]={0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57};
                    uint8_t got[8]{},got2[8]{};uint64_t comp=0,vt=0,carrier=0,slots=0,record=0;int32_t count=0;
                    bool ok=sampler->read(sampler->base+0x2c50150,got,8)&&!std::memcmp(got,removeHead,8)&&sampler->read(sampler->base+0x2c7dbd0,got2,8)&&!std::memcmp(got2,insertHead,8)
                        &&sampler->read(anchor.entity+0x54d0,&comp,8)&&comp&&sampler->read(comp,&vt,8)&&vt==sampler->base+0x3dd9750&&sampler->read(comp+0x50,&carrier,8)&&carrier
                        &&sampler->read(carrier+0x10,&count,4)&&count>0&&count<64&&sampler->read(carrier+0x18,&slots,8)&&slots;
                    for(int32_t i=0;ok&&i<count&&!record;++i){uint64_t slot=0;uint8_t type=0;int32_t stacks=0;uint64_t arr=0;
                        if(!sampler->read(slots+8ull*uint32_t(i),&slot,8)||!slot||!sampler->read(slot+1,&type,1)||type!=1)continue;
                        if(!sampler->read(slot+0x18,&stacks,4)||stacks<=0||stacks>16||!sampler->read(slot+0x20,&arr,8))break;
                        uint64_t stack=0;int32_t n=0;uint64_t recs=0;
                        if(sampler->read(arr+8ull*uint32_t(stacks-1),&stack,8)&&stack&&sampler->read(stack+0x10,&n,4)&&n>0&&n<=32&&sampler->read(stack+0x18,&recs,8))
                            sampler->read(recs+8ull*uint32_t(n-1),&record,8);}
                    if(!ok||!record)note("CARGO hand refused: no carrier, no back case or the calls differ\n");
                    else{reinterpret_cast<void(*)(uint64_t,uint64_t,uint8_t,uint8_t,uint64_t)>(sampler->base+0x2c50150)(carrier,record,0x4f,1,0);
                        reinterpret_cast<void(*)(uint64_t,uint32_t,uint64_t,int32_t,uint8_t)>(sampler->base+0x2c7dbd0)(carrier,uint32_t(*hand),record,2,1);
                        std::ostringstream o;o<<"CARGO hand "<<*hand<<": record 0x"<<std::hex<<record<<" moved\n";note(o.str().c_str());}
#endif
                }
                else if(auto load=mod::puppet::parseCargoCommand(text,mod::runStamp(),"tocart");load&&*load>0&&*load<=12&&anchor.entity&&anchor.pose.available) {
                    // Lab: the top N cases of the porter's back onto the nearest attached cart (its one slot, type 0) with
                    // the game's own calls (remove 2c50150 as above, insert 2c7dbd0(cart carrier, 0, record, 2, 1)).
#ifdef _WIN32
                    static const uint8_t removeHead[8]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x89},insertHead[8]={0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57};
                    uint8_t got[8]{},got2[8]{};uint64_t comp=0,vt=0,carrier=0,slots=0;int32_t count=0;
                    bool ok=sampler->read(sampler->base+0x2c50150,got,8)&&!std::memcmp(got,removeHead,8)&&sampler->read(sampler->base+0x2c7dbd0,got2,8)&&!std::memcmp(got2,insertHead,8)
                        &&sampler->read(anchor.entity+0x54d0,&comp,8)&&comp&&sampler->read(comp,&vt,8)&&vt==sampler->base+0x3dd9750&&sampler->read(comp+0x50,&carrier,8)&&carrier
                        &&sampler->read(carrier+0x10,&count,4)&&count>0&&count<64&&sampler->read(carrier+0x18,&slots,8)&&slots;
                    uint64_t cart=0;double best=1e18;auto here=mod::puppet::transform(anchor.pose).position;
                    for(auto& c:mod::carts::read(*sampler,sampler->base))if(c.attached){double d=0;for(int k=0;k<3;++k)d+=(c.position[k]-here[k])*(c.position[k]-here[k]);if(d<best){best=d;cart=c.carrier;}}
                    unsigned moved=0;
                    for(uint32_t n=0;ok&&cart&&n<*load;++n){uint64_t record=0;
                        for(int32_t i=0;i<count&&!record;++i){uint64_t slot=0;uint8_t type=0;int32_t stacks=0;uint64_t arr=0;
                            if(!sampler->read(slots+8ull*uint32_t(i),&slot,8)||!slot||!sampler->read(slot+1,&type,1)||type!=1)continue;
                            if(!sampler->read(slot+0x18,&stacks,4)||stacks<=0||stacks>16||!sampler->read(slot+0x20,&arr,8))break;
                            uint64_t stack=0;int32_t k=0;uint64_t recs=0;
                            if(sampler->read(arr+8ull*uint32_t(stacks-1),&stack,8)&&stack&&sampler->read(stack+0x10,&k,4)&&k>0&&k<=32&&sampler->read(stack+0x18,&recs,8))
                                sampler->read(recs+8ull*uint32_t(k-1),&record,8);}
                        if(!record)break;
                        reinterpret_cast<void(*)(uint64_t,uint64_t,uint8_t,uint8_t,uint64_t)>(sampler->base+0x2c50150)(carrier,record,0x4f,1,0);
                        reinterpret_cast<void(*)(uint64_t,uint32_t,uint64_t,int32_t,uint8_t)>(sampler->base+0x2c7dbd0)(cart,0,record,2,1);++moved;}
                    note(("CARGO tocart: "+std::to_string(moved)+" cases moved"+(cart?"\n":" (no attached cart)\n")).c_str());
#endif
                }
                else if(erase&&cargoDelete){cargoDelete(*erase);note(("CARGO delete id="+std::to_string(*erase)+"\n").c_str());}
                else if(auto code=mod::puppet::parseCargoCommand(text,mod::runStamp(),"part");code&&partToggle&&ownEntity) {
                    // Research: "part <index*2+on> <stamp>" switches one art part of the own body.
                    bool ok=partToggle(ownEntity,*code/2,*code&1);
                    note(("PART "+std::to_string(*code/2)+(*code&1?" on":" off")+(ok?"\n":" failed\n")).c_str());
                }
                else if(auto stream=mod::puppet::parseCargoCommand(text,mod::runStamp(),"stream");stream) {
#ifdef _WIN32
                    // Research: the game's own save sections over a memory buffer (game_offsets.md, "Save sections").
                    // "stream 1" writes the weather forecast section to stream-weather.bin beside the command file,
                    // "stream 2" reads that file into this game's forecast, "stream 3" writes it again as
                    // stream-weather-after.bin (to compare).
                    const uint64_t base=sampler->base;uint64_t forecast=0;auto file=rideCommand.parent_path()/(*stream==3?"stream-weather-after.bin":"stream-weather.bin");
                    if(!sampler->read(base+0x7bf83b8,&forecast,8)||!forecast)note("STREAM no forecast object\n");
                    else if(*stream==1||*stream==3){
                        static std::vector<uint8_t> buffer(1<<20);alignas(16) static uint8_t memory[0x30],writer[0x1000];
                        std::memset(memory,0,sizeof memory);std::memset(writer,0,sizeof writer);
                        reinterpret_cast<void(*)(void*,uint8_t,void*,int32_t)>(base+0x18fc750)(memory,1,buffer.data(),int32_t(buffer.size()));
                        reinterpret_cast<void(*)(void*,void*,uint32_t,uint32_t)>(base+0x1d3d420)(writer,memory,0x40,0x2000);
                        reinterpret_cast<void(*)(void*,void*)>(base+0x2e1e990)(reinterpret_cast<void*>(forecast),writer);
                        reinterpret_cast<void(*)(void*)>(base+0x1d3e8d0)(writer);
                        uint64_t begin=0,cursor=0;std::memcpy(&begin,memory+0x18,8);std::memcpy(&cursor,memory+0x20,8);size_t n=size_t(cursor-begin);
                        if(n>buffer.size())n=0;
                        std::ofstream(file,std::ios::binary).write(reinterpret_cast<const char*>(buffer.data()),std::streamsize(n));
                        uint32_t sum=0;for(size_t i=0;i<n;++i)sum=sum*31+buffer[i];
                        std::ostringstream o;o<<"STREAM wrote weather section: "<<n<<" bytes, sum "<<std::hex<<sum<<std::dec<<"\n";note(o.str().c_str());
                    }
                    else if(*stream==2){
                        std::ifstream in(file,std::ios::binary);static std::vector<uint8_t> buffer;buffer.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
                        if(buffer.empty())note("STREAM no file to read\n");
                        else{
                            alignas(16) static uint8_t memory[0x30],reader[0x200];std::memset(memory,0,sizeof memory);std::memset(reader,0,sizeof reader);
                            reinterpret_cast<void(*)(void*,uint8_t,void*,int32_t)>(base+0x18fc750)(memory,0,buffer.data(),int32_t(buffer.size()));
                            reinterpret_cast<void(*)(void*,uint16_t,void*,uint32_t)>(base+0x1d3d270)(reader,0,memory,uint32_t(buffer.size()));
                            reinterpret_cast<void(*)(void*,void*)>(base+0x2e12520)(reinterpret_cast<void*>(forecast),reader);
                            std::ostringstream o;o<<"STREAM read weather section: "<<buffer.size()<<" bytes\n";note(o.str().c_str());
                        }
                    }
#endif
                }
                else if(auto churn=mod::puppet::parseCargoCommand(text,mod::runStamp(),"gear");churn&&gears) {
                    // Test: "gear 1" hides and shows the partner's gear copies 40 times; "gear 2" removes them all
                    // (the mirror makes them again), 10 times, 6 s apart. To find what upsets the game's culling tree.
                    gearChurnMode=*churn;gearChurnLeft=*churn==1?80:10;gearChurnAt=0;
                    note(("GEAR test: churn mode "+std::to_string(*churn)+"\n").c_str());
                }
                else if(auto up=mod::puppet::parseCargoCommand(text,mod::runStamp(),"upgrade");up) {
                    // Test: "upgrade 0" lists this porter's and the partner's structures with their levels;
                    // "upgrade ID" takes structure ID one level up as its menu would (materials delivered, level-up queued).
                    try{
                        uint64_t manager=0;sampler->read(sampler->base+0x7bf4960,&manager,8);if(!imageSize)for(auto& sec:sampler->sections)imageSize=std::max(imageSize,sec.address+sec.size-sampler->base);
                        auto all=mod::structures::readTable(*sampler,manager,sampler->base,imageSize);auto api=mod::roads::nativeLevels(*sampler);std::ostringstream o;
                        for(auto& r:all){
                            if(r.id<mod::structures::PlayerFirst)continue;
                            if(!*up){o<<"STRUCT id "<<r.id<<" type "<<int(r.type)<<" state "<<int(r.state)<<" level "<<r.level<<" at "<<int(r.at.position[0])<<","<<int(r.at.position[1])<<" own";for(auto v:r.own)o<<" "<<v;o<<"\n";}
                            else if(r.id==*up){o<<"STRUCT test: upgrade of "<<r.id<<" from level "<<r.level<<": "<<api.upgrade(r.object,r.level+1)<<"\n";}
                            else if(*up>=1000000&&r.id==(*up-1000000)/10){mod::roads::Set s{};s[(*up-1000000)%10%6]=50;api.deliver(r.object,s);o<<"STRUCT test: 50 of material "<<(*up-1000000)%10<<" delivered to "<<r.id<<"\n";}
                        }
                        note(o.str().empty()?"STRUCT test: nothing\n":o.str().c_str());
                    }catch(const std::exception& e){note((std::string("STRUCT test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto vo=mod::puppet::parseCargoCommand(text,mod::runStamp(),"voidout");vo&&anchor.pose.available) {
                    // Test: "voidout 0" lists the craters; "voidout N" adds a crater of N metres 70 m ahead of this porter
                    // (area: the one an existing crater names, or 0), as the partner's crater would be added.
                    auto table=mod::voidout::read(*sampler,sampler->base);std::ostringstream o;
                    o<<"VOIDOUT test: "<<(table.readable?"":"(unreadable) ")<<table.craters.size()<<" craters, counter "<<table.counter<<"\n";
                    for(auto& k:table.craters)o<<"VOIDOUT crater "<<k.id<<" at "<<int(k.position[0])<<","<<int(k.position[1])<<","<<int(k.position[2])<<" r "<<k.radius<<" area "<<k.area<<(k.active?" active":"")<<(k.disabled?" disabled":"")<<"\n";
                    if(*vo){
                        try{auto add=mod::voidout::nativeAdd(*sampler,[this](const std::string& row){note(row.c_str());});auto t=mod::puppet::transform(anchor.pose);mod::voidout::Crater k;k.radius=float(*vo);
                            for(int i=0;i<3;++i)k.position[size_t(i)]=t.position[size_t(i)]+70*double(t.rotation[size_t(3+i)]);
                            k.area=mod::voidout::nativeArea(*sampler)();k.yaw=0.7f;o<<"VOIDOUT test: area "<<k.area<<"\n";
                            // N >= 1000: a crater of N-1000 metres that the mod treats as this game's own voidout (sent to the partner).
                            bool own=*vo>=1000;if(own)k.radius=float(*vo-1000);
                            auto id=add(k);if(id&&!own)voidSync.applied(id);o<<"VOIDOUT test: crater "<<id<<(id?" added":" refused")<<(own?" as this game's own":"")<<"\n";}
                        catch(const std::exception& e){o<<"VOIDOUT test unavailable: "<<e.what()<<"\n";}
                    }
                    note(o.str().c_str());
                }
                else if(auto pv=mod::puppet::parseCargoCommand(text,mod::runStamp(),"paver");pv) {
                    // Test: "paver 0" lists the auto-pavers; "paver ID*10+K" delivers 100 of material K to paver ID as
                    // this player would at its terminal; "paver 1000000+ID" delivers everything it still needs.
                    try{
                        auto api=mod::roads::nativeApi(*sampler);auto all=mod::roads::readPavers(*sampler,sampler->base);
                        uint32_t id=*pv>=1000000?*pv-1000000:*pv/10;std::ostringstream o;
                        if(!*pv){
                            o<<"ROAD test: "<<all.size()<<" pavers\n";
                            for(auto& p:all){auto need=api.missing(p.address);double at[3]{};sampler->read(p.address+0x40,at,24);
                                o<<"ROAD paver "<<p.id<<" level "<<p.level<<" at "<<int(at[0])<<","<<int(at[1])<<" own";for(auto v:p.own)o<<" "<<v;o<<" net";for(auto v:p.net)o<<" "<<v;o<<" needs";for(auto v:need)o<<" "<<v;int32_t pending[6]{};if(sampler->read(p.address+0x524,pending,sizeof pending)){o<<" online pending";for(auto v:pending)o<<" "<<v;}o<<"\n";}
                        }else{
                            bool found=false;
                            for(auto& p:all)if(p.id==id){found=true;mod::roads::Set s{};if(*pv>=1000000)s=api.missing(p.address);else s[*pv%10%mod::roads::Kinds]=100;
                                api.deliver(p.address,s);o<<"ROAD test: delivered to paver "<<id<<":";for(auto v:s)o<<" "<<v;o<<"\n";}
                            if(!found)o<<"ROAD test: no paver "<<id<<"\n";
                        }
                        note(o.str().c_str());
                    }catch(const std::exception& e){note((std::string("ROAD test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto gz=mod::puppet::parseCargoCommand(text,mod::runStamp(),"gazer");gz&&anchor.pose.available) {
                    // Test: "gazer 1" destroys the nearest living gazer the way a partner's kill does ("gazer 2": the nearest with a body).
                    auto at=mod::puppet::transform(anchor.pose).position;double best=1e18;uint64_t pick=0;uint32_t lod=0;
                    for(auto& g:mod::bts::readGazers(*sampler,sampler->base)){
                        double p[3]{};if(!(g.life>0)||mod::bts::dynamic(g.flags)||(*gz==2&&g.lod!=2)||!sampler->read(g.address+0x28,p,24))continue;
                        double d=(p[0]-at[0])*(p[0]-at[0])+(p[1]-at[1])*(p[1]-at[1]);if(d<best){best=d;pick=g.address;lod=g.lod;}
                    }
                    try{if(pick)mod::bts::nativeDestroy(*sampler)(pick);}catch(const std::exception&){pick=0;}
                    std::ostringstream o;o<<"BT test: "<<(pick?"destroyed the nearest gazer":"no living gazer")<<" at "<<std::sqrt(best)<<" m lod "<<lod<<"\n";note(o.str().c_str());
                }
                else if(auto catcher=mod::puppet::parseCargoCommand(text,mod::runStamp(),"catcher");catcher) {
                    // Research: the game's catcher (boss BT) control command: 0 bring at the player, 1 return, 4 force off.
                    uint8_t got[7]{};static const uint8_t want[7]={0x40,0x55,0x48,0x8d,0x6c,0x24,0xa9};
                    if(sampler->read(sampler->base+0x2a928c0,got,7)&&!std::memcmp(got,want,7)){
                        if(*catcher>=100){
                            // "catcher 100+N": takes N percent off this game's first catcher, as a hit here would.
                            auto all=mod::bts::readCatchers(*sampler,sampler->base);
                            if(!all.empty()){float next=std::max(all[0].percent-float(*catcher-100),mod::bts::HealthSync::Floor);mod::bts::nativeHurt(*sampler)(all[0],next);}
                            note(all.empty()?"BT test: no catcher with a body\n":("BT test: catcher at "+std::to_string(all[0].percent)+" % hurt by "+std::to_string(*catcher-100)+"\n").c_str());
                        }else{
                        reinterpret_cast<void(*)(uint8_t)>(sampler->base+0x2a928c0)(uint8_t(*catcher));
                        note(("BT test: catcher command "+std::to_string(*catcher)+"\n").c_str());
                        }
                    }else note("BT test: catcher command function differs\n");
                }
                else if(auto del=mod::puppet::parseCargoCommand(text,mod::runStamp(),"cartdel");del) {
                    // Research: remove a GameActor by id the way the cargo manager removes a cart whose record went away
                    // (0x2c94a1b): 0x22fbd10(GameActor manager *(image+0x7bbb8c0), &id) -> true when its unit removed it.
                    uint64_t actors=0;uint8_t got[8]{};static const uint8_t want[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c};
                    if(sampler->read(sampler->base+0x7bbb8c0,&actors,8)&&actors&&sampler->read(sampler->base+0x22fbd10,got,8)&&!std::memcmp(got,want,8)){
                        uint32_t id=*del;bool ok=reinterpret_cast<bool(*)(uint64_t,const uint32_t*)>(sampler->base+0x22fbd10)(actors,&id);
                        note(("CART test: remove actor "+std::to_string(id)+(ok?" done\n":" not found\n")).c_str());
                    }else note("CART test: removal function differs\n");
                }
                else if(auto move=mod::puppet::parseCargoCommand(text,mod::runStamp(),"cartmove");move&&anchor.pose.available) {
                    // Research: every floating carrier (a cargo-manager carrier of type 2, entity = carrier +0x20 - 0x20,
                    // vtable image+0x3d5fed0) put N m east of this porter with the game's SetWorldTransform (0x2361ea0).
                    uint64_t manager=0,list=0;int32_t count=0;unsigned moved=0;
                    if(sampler->read(sampler->base+0x7bf4968,&manager,8)&&manager&&sampler->read(manager+0x46d280,&count,4)&&sampler->read(manager+0x46d288,&list,8)&&list&&count>0&&count<4096){
                        for(int32_t i=0;i<count;++i){uint64_t c=0,inner=0,vt=0;uint32_t type=0;
                            if(!sampler->read(list+8ull*i,&c,8)||!c||!sampler->read(c,&type,4)||type!=2||!sampler->read(c+0x20,&inner,8)||!inner)continue;
                            uint64_t e=inner-0x20;if(!sampler->read(e,&vt,8)||vt!=sampler->base+0x3d5fed0)continue;
                            auto t=mod::puppet::transform(anchor.pose);t.position[0]+=double(*move);
                            // The game's own cart placement (0x2cb2e00) teleports the entity with a message (vtable
                            // image+0x3dd96b8, +8 byte 0, +0x10 position, +0x28 rotation) sent by 0x235c990(entity, &msg, 0).
                            alignas(16) uint8_t msg[0x60]{};uint64_t mv=sampler->base+0x3dd96b8;std::memcpy(msg,&mv,8);
                            std::memcpy(msg+0x10,t.position.data(),24);std::memcpy(msg+0x28,t.rotation.data(),36);
                            reinterpret_cast<void(*)(uint64_t,uint8_t*,uint32_t)>(sampler->base+0x235c990)(e,msg,0);++moved;}}
                    note(("CART test: moved "+std::to_string(moved)+" carts\n").c_str());
                }
                else if(auto link=mod::puppet::parseCargoCommand(text,mod::runStamp(),"cartlink");link&&anchor.entity&&anchor.pose.available) {
                    // Research (game_offsets.md, floating carriers): the game's one link routine 0x265a260(cart, entity or 0,
                    // bool), as Attach and Detach Carrier call it. "cartlink 0": the nearest cart let go; 1: linked to this
                    // porter; 2: linked to the vehicle he rides. The cart's mode (+0x698: 1 ridden, 2 pulled) and puller id
                    // (+0x77c) are logged before and after.
#ifdef _WIN32
                    uint8_t got[14]{};static const uint8_t want[14]={0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xf1};
                    const mod::carts::Cart* best=nullptr;double bestD=1e18;auto all=mod::carts::read(*sampler,sampler->base);auto here=mod::puppet::transform(anchor.pose).position;
                    for(auto& c:all){double d=std::hypot(c.position[0]-here[0],c.position[1]-here[1]);if(c.entity&&d<bestD){bestD=d;best=&c;}}
                    uint64_t target=*link==1?anchor.entity:*link==2?(localVehicle.riding?localVehicle.entity:0):0;
                    if(*link==3){std::ostringstream o;o<<"CART list "<<all.size()<<" carts";
                        for(auto& c:all){uint32_t m=0,id=0;if(c.entity){sampler->read(c.entity+0x698,&m,4);sampler->read(c.entity+0x77c,&id,4);}
                            o<<" | id "<<c.id<<" at "<<int(c.position[0])<<","<<int(c.position[1])<<","<<int(c.position[2])<<" attached "<<c.attached<<" mode "<<m<<" puller 0x"<<std::hex<<id<<std::dec;}
                        note((o.str()+"\n").c_str());}
                    else if(!sampler->read(sampler->base+0x265a260,got,14)||std::memcmp(got,want,14))note("CART link refused: the link routine differs\n");
                    else if(!best||bestD>30)note("CART link: no cart within 30 m\n");
                    else if(*link==2&&!target)note("CART link: this porter rides no vehicle\n");
                    else{
                        auto mode=[&]{uint32_t m=0,id=0;sampler->read(best->entity+0x698,&m,4);sampler->read(best->entity+0x77c,&id,4);std::ostringstream o;o<<"mode "<<m<<" puller 0x"<<std::hex<<id;return o.str();};
                        std::string before=mode();
                        reinterpret_cast<void(*)(uint64_t,uint64_t,bool)>(sampler->base+0x265a260)(best->entity,target,false);
                        std::ostringstream o;o<<"CART link "<<*link<<" cart "<<best->id<<" at "<<int(bestD)<<" m: "<<before<<" -> "<<mode()<<"\n";note(o.str().c_str());}
#else
                    note("CART link needs the game\n");
#endif
                }
                else if(auto cart=mod::puppet::parseCargoCommand(text,mod::runStamp(),"cart");cart&&anchor.pose.available) {
                    // Research: the game's floating-carrier creation (0x2669710(&id, request)): a 0x70-byte request that it
                    // hands to the GameActor manager as actor type 9. Request: +0 position (3 f64), +0x18 rotation
                    // (9 f32), +0x40 variant word (0xffff: default), +0x48 the carrier's cargo record (none here), +0x50
                    // id (the image's default at 0x4af1150), +0x54/+0x55 bytes, +0x58 level-2 flag. "cart N": N m east.
                    uint8_t got[8]{};static const uint8_t want[8]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48};
                    if(sampler->read(sampler->base+0x2669710,got,8)&&!std::memcmp(got,want,8)){
                        alignas(16) uint8_t request[0x70]{};auto t=mod::puppet::transform(anchor.pose);t.position[0]+=double(*cart);
                        std::memcpy(request,t.position.data(),24);std::memcpy(request+0x18,t.rotation.data(),36);
                        uint16_t variant=0xffff;std::memcpy(request+0x40,&variant,2);
                        uint32_t id=0;sampler->read(sampler->base+0x4af1150,&id,4);std::memcpy(request+0x50,&id,4);
                        uint32_t out=0;reinterpret_cast<uint32_t*(*)(uint32_t*,const uint8_t*)>(sampler->base+0x2669710)(&out,request);
                        std::ostringstream o;o<<"CART test: requested at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<" id "<<out<<"\n";note(o.str().c_str());
                    }else note("CART test: creation function differs\n");
                }
                else if(auto reason=mod::puppet::parseCargoCommand(text,mod::runStamp(),"catcherend");reason) {
                    // Research: the game's end-of-fight message to every active catcher (0x2a6cf20(manager, -1, &reason,
                    // 0, 0)); the reasons it is called with differ in what the fight leaves (a cleared sphere or not).
                    uint8_t got[8]{};static const uint8_t want[8]={0x4c,0x8b,0xdc,0x55,0x53,0x49,0x8d,0x6b};uint64_t manager=0;
                    if(sampler->read(sampler->base+0x2a6cf20,got,8)&&!std::memcmp(got,want,8)&&sampler->read(sampler->base+0x7bee060,&manager,8)&&manager){
                        uint32_t hash=*reason;reinterpret_cast<void(*)(uint64_t,int32_t,const uint32_t*,uint32_t,uint32_t)>(sampler->base+0x2a6cf20)(manager,-1,&hash,0,0);
                        note(("BT test: catcher end reason "+std::to_string(hash)+"\n").c_str());
                    }else note("BT test: catcher end function differs\n");
                }
                else if(auto rain=mod::puppet::parseCargoCommand(text,mod::runStamp(),"rain");rain&&anchor.pose.available) {
                    // Test: "rain <seconds>" places the game's own timed rain around this porter (the influence the shared
                    // rain uses), so a host can have timefall on demand.
                    try{
                        auto api=mod::weather::nativeApi(*sampler);auto at=mod::puppet::transform(anchor.pose).position;
                        bool ok=api.influence(mod::weather::Mode::Rain,at,250.f,float(std::min<uint32_t>(*rain,900)),0);
                        std::ostringstream o;o<<"WEATHER test: rain for "<<*rain<<" s at "<<at[0]<<","<<at[1]<<","<<at[2]<<(ok?" placed":" refused")<<"\n";note(o.str().c_str());
                    }catch(const std::exception& e){note((std::string("WEATHER test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto lose=mod::puppet::parseCargoCommand(text,mod::runStamp(),"unitlose");lose) {
                    // Test: "unitlose <mission id * 10 + n>" removes this game's cargo of unit n and leaves the unit undelivered
                    // (so the player can only make a partial hand-in).
                    uint64_t id=*lose/10;uint8_t n=uint8_t(*lose%10);unsigned removed=0;uint64_t manager=0;
                    if(cargoDelete&&cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
                        for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords))
                            if(r.mission==id&&r.unit==n){cargoDelete(r.index);++removed;}
                    std::ostringstream o;o<<"ORDER test: cargo of unit "<<int(n)<<" of "<<std::hex<<id<<std::dec<<" removed ("<<removed<<")\n";note(o.str().c_str());
                }
                else if(auto info=mod::puppet::parseCargoCommand(text,mod::runStamp(),"loadinfo");info) {
                    // Test: "loadinfo <mission id>" logs every cargo record of that order as the shared load reads it, whether
                    // the record is in the manager's list of order cargo (+0x46d330), and the order's own flag word (+0x30).
                    uint64_t manager=0;unsigned n=0;
                    if(cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager){
                        int32_t listed=0;uint64_t list=0;sampler->read(manager+0x46d330,&listed,4);sampler->read(manager+0x46d338,&list,8);
                        for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords)){
                            if(r.mission!=*info)continue;
                            uint64_t at=manager+0x20+0x240ull*r.index;bool in=false;
                            for(int32_t i=0;i<listed&&i<8192&&!in;++i){uint64_t p=0;in=sampler->read(list+8ull*i,&p,8)&&p==at;}
                            std::ostringstream o;o<<"ORDERLOAD info record "<<r.index<<std::hex<<" handle 0x"<<r.slot<<" unit "<<std::dec<<int(r.unit)<<" location "<<int(mod::cargo::location(r))<<" kind "<<int(mod::cargo::kind(r))
                                <<" type "<<int(r.type)<<std::hex<<" name 0x"<<r.hash<<" item 0x"<<r.item<<" flags 0x"<<r.flags<<" 0x"<<r.flags2<<std::dec<<" life "<<r.life[0]<<" "<<r.life[1]<<" listed "<<in
                                <<" at "<<r.position[0]<<","<<r.position[1]<<","<<r.position[2]<<"\n";note(o.str().c_str());++n;}
                    }
                    std::map<uint64_t,uint8_t> details;uint64_t system=0,array=0,data=0;int32_t count=0;uint32_t flags=0;bool found=false;
                    if(sampler->read(sampler->base+0x7c0aff8,&system,8)&&system&&sampler->read(system+0x108,&array,8)&&array&&sampler->read(array,&count,4)&&sampler->read(array+8,&data,8)&&data)
                        for(int32_t i=0;i<count&&i<8192&&!found;++i){uint64_t id=0;if(sampler->read(data+0xa0ull*i+0x38,&id,8)&&id==*info)found=sampler->read(data+0xa0ull*i+0x30,&flags,4);}
                    uint16_t state=0;for(auto& o:mod::orders::readOrders(*sampler,sampler->base,details))if(o.id==*info)state=o.state;
                    std::ostringstream o;o<<"ORDERLOAD info order "<<std::hex<<*info<<": "<<std::dec<<n<<" cargo record(s), state "<<state<<std::hex<<", order flags 0x"<<(found?flags:0xffffffffu)
                        <<std::dec<<", mirrored here "<<orderLoads.mirrored(*info)<<", waiting cases "<<orderLoads.waiting()<<"\n";note(o.str().c_str());
                }
                else if(auto save=mod::puppet::parseCargoCommand(text,mod::runStamp(),"loadsave");save) {
                    // Test: "loadsave <mission id * 100 + n>" remembers unit n's case of that order as a partner would be told
                    // of it; "loadmake 0" then makes it again 2 m behind the porter (after `unitlose` removed the original).
                    uint64_t id=*save/100;uint8_t n=uint8_t(*save%100);uint64_t manager=0;loadSaved.reset();
                    if(cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
                        for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords))if(r.mission==id&&r.unit==n&&!loadSaved){
                            mod::cargo::Placement p{r.hash,r.position,1};p.mission=r.mission;p.unit=r.unit;p.type=r.type;p.item=r.item;p.flags=r.flags;p.flags2=r.flags2;p.life[0]=r.life[0];p.life[1]=r.life[1];
                            if(auto tag=mod::orderload::tagOf(p)){mod::orderload::Case c;c.tag=*mod::orderload::decode(mod::orderload::encode(*tag));c.name=r.hash;c.id=1;loadSaved=c;}}
                    note(loadSaved?"ORDERLOAD test: case remembered\n":"ORDERLOAD test: no such case, or not a standard order's\n");
                }
                else if(auto make=mod::puppet::parseCargoCommand(text,mod::runStamp(),"loadmake");make&&anchor.pose.available) {
                    needOrderCaseCreate();
                    if(!loadSaved||!orderCaseCreate)note("ORDERLOAD test: nothing remembered, or the creator is unavailable\n");
                    else{
                        auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                        if(l>.5){t.position[0]-=2*fx/l;t.position[1]-=2*fy/l;}
                        t.position[2]+=.5;t.rotation={1,0,0,0,1,0,0,0,1};
                        auto c=*loadSaved;c.position=t.position;c.rotation=t.rotation;bool ok=orderCaseCreate(c);
                        std::ostringstream o;o<<"ORDERLOAD test: unit "<<int(c.tag.unit)<<" of "<<std::hex<<c.tag.mission<<std::dec<<(ok?" asked of the game's creator":" refused by the game (does the unit still exist here?)")<<"\n";note(o.str().c_str());
                    }
                }
                else if(auto unit=mod::puppet::parseCargoCommand(text,mod::runStamp(),"unit");unit) {
                    // Test: "unit <mission id * 10 + n>" marks unit n of that order delivered in this game (as a hand-in would).
                    uint64_t id=*unit/10;uint8_t n=uint8_t(*unit%10);bool done=false;
                    for(auto& u:mod::orders::readUnits(*sampler,sampler->base))if(u.mission==id&&u.unit==n&&u.state==mod::orders::NotDelivered){
#ifdef _WIN32
                        *reinterpret_cast<uint16_t*>(u.stateAt)=mod::orders::Delivered;done=true;
#endif
                    }
                    // ... and removes this game's cargo of that unit, wherever it is (as if the partner had handed it in).
                    unsigned removed=0;uint64_t manager=0;
                    if(done&&cargoDelete&&cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
                        for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords))
                            if(r.mission==id&&r.unit==n){cargoDelete(r.index);++removed;}
                    std::ostringstream o;o<<"ORDER test: unit "<<int(n)<<" of "<<std::hex<<id<<std::dec<<(done?" marked delivered, ":" not waiting, ")<<removed<<" cargo record(s) removed\n";note(o.str().c_str());
                }
                else if(auto what=mod::puppet::parseCargoCommand(text,mod::runStamp(),"rewards");what) {
                    // Test: "rewards 0" prints every facility's connection points and level, "rewards 1" the porter's
                    // likes and grade totals (read-only, whatever share_rewards says).
                    if(*what==0){
                        auto facilities=mod::rewards::readFacilities(*sampler,sampler->base);
                        mod::rewards::readHighest(*sampler,sampler->base,facilities);
                        note(("REWARD test: "+std::to_string(facilities.size())+" facilities\n").c_str());
                        for(size_t i=0;i<facilities.size();++i)note(("REWARD test: "+std::to_string(i)+" "+rewardLine(facilities[i])+" "+rewardCap(facilities[i],facilities[i].points)+"\n").c_str());
                    }
                    else{
                        auto p=mod::rewards::readPorter(*sampler,sampler->base);std::ostringstream o;
                        o<<"REWARD test: porter "<<(p.readable?"readable":"unreadable")<<" likes "<<p.likes<<" online "<<p.likesOnline<<" experience";
                        for(auto x:p.experience)o<<" "<<x;
                        o<<" level";for(auto x:p.level)o<<" "<<x;
                        o<<"\n";note(o.str().c_str());
                    }
                }
                else if(auto add=mod::puppet::parseCargoCommand(text,mod::runStamp(),"reward_add");add) {
                    // Test: "reward_add <row * 10000 + points>" adds 1..9999 connection points to the facility in that
                    // row of "rewards 0" with the game's own routine (needs share_rewards).
                    try{
                        if(!config.shareRewards)throw std::runtime_error("share_rewards is off");
                        auto api=mod::rewards::nativeApi(*sampler);auto facilities=mod::rewards::readFacilities(*sampler,sampler->base);
                        size_t row=*add/10000;int32_t points=int32_t(*add%10000);
                        if(row>=facilities.size()||points<1||inEvent||!cache.available)note("REWARD test: add refused (row or points out of range, or the porter is not in the world)\n");
                        else{
                            auto& f=facilities[row];int32_t owed=mod::rewards::Sync::owed(f,{f.hash,f.points+points,f.level});
                            bool ok=owed>0&&api.add(f.resource,owed);int32_t after[2]{f.points,f.level};sampler->read(f.record+8,after,8);
                            std::ostringstream o;o<<"REWARD test: "<<rewardLine(f)<<": "<<owed<<(ok?" added":" not added")<<", now points "<<after[0]<<" level "<<after[1]<<" ("<<rewardCapNow(f,after[0])<<")\n";note(o.str().c_str());
                        }
                    }catch(const std::exception& e){note((std::string("REWARD test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto order=mod::puppet::parseCargoCommand(text,mod::runStamp(),"order");order) {
                    // Test: "order <mission id>" asks the game to start that order; + 1000000000 to complete it,
                    // + 2000000000 to give it up (the game's own queued requests, orders.h).
                    try{
                        auto request=mod::orders::nativeRequest(*sampler);uint8_t what=uint8_t(1+*order/1000000000u);uint64_t id=*order%1000000000u;
                        bool ok=what<=3&&request(id,what);
                        std::ostringstream o;o<<"ORDER test: request "<<int(what)<<" id "<<std::hex<<id<<std::dec<<(ok?" accepted":" refused")<<"\n";note(o.str().c_str());
                    }catch(const std::exception& e){note((std::string("ORDER test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto kind=mod::puppet::parseCargoCommand(text,mod::runStamp(),"mule");kind&&anchor.pose.available) {
                    // Test: "mule <kind>" puts the standing MULE nearest to the porter down (0 knocked, 1 fainted, 2 asleep, 3 dead).
                    try{
                        auto defeat=mod::enemies::nativeDefeat(*sampler);auto mules=mod::enemies::readMules(*sampler,sampler->base);
                        auto here=mod::puppet::transform(anchor.pose).position;const mod::enemies::Mule* best=nullptr;double bestD=1e18;
                        for(auto& m:mules){if(mod::enemies::condition(m.state,m.flags)!=mod::enemies::Up||m.state==5)continue;
                            double dx=m.position[0]-here[0],dy=m.position[1]-here[1],d=dx*dx+dy*dy;if(d<bestD){bestD=d;best=&m;}}
                        if(*kind==9){
                            auto alert=mod::enemies::nativeAlert(*sampler);auto camps=mod::enemies::readCamps(*sampler,sampler->base);
                            std::ostringstream o;o<<"MULE test: "<<camps.size()<<" camps, phases";for(auto& c:camps)o<<" "<<c.phase;
                            // the camp whose members are nearest is not known here: alert every calm camp (test only)
                            for(auto& c:camps)if(!mod::enemies::alarmed(c.phase))alert(c.uuid);
                            o<<"; all put on alert\n";note(o.str().c_str());
                        }
                        else if(!best||*kind>3)note("MULE test: none standing, or kind out of range\n");
                        else{defeat(best->address,uint8_t(*kind));std::ostringstream o;o<<"MULE test: id "<<std::hex<<best->id<<std::dec<<" kind "<<*kind<<" lod "<<best->lod<<" at "<<best->position[0]<<","<<best->position[1]<<","<<best->position[2]<<" ("<<std::sqrt(bestD)<<" m)\n";note(o.str().c_str());}
                    }catch(const std::exception& e){note((std::string("MULE test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto count=mod::puppet::parseCargoCommand(text,mod::runStamp(),"muleat");count&&anchor.pose.available) {
                    // Test, read-only: "muleat <n>" logs the n MULEs nearest to the porter (at most 12) as the position sync sees them.
                    auto mules=mod::enemies::readMules(*sampler,sampler->base);auto here=mod::puppet::transform(anchor.pose).position;
                    auto metres=[&](const mod::enemies::Mule& m){double d=0;for(int k=0;k<3;++k)d+=(m.position[k]-here[k])*(m.position[k]-here[k]);return std::sqrt(d);};
                    std::sort(mules.begin(),mules.end(),[&](const mod::enemies::Mule& a,const mod::enemies::Mule& b){return metres(a)<metres(b);});
                    std::ostringstream o;o<<"MULEPOS test: "<<mules.size()<<" MULEs, porter at "<<here[0]<<","<<here[1]<<","<<here[2]<<"\n";
                    for(size_t i=0;i<mules.size()&&i<std::min<uint32_t>(*count,12);++i){auto& m=mules[i];
                        o<<"MULEPOS test: key "<<std::hex<<mod::enemies::key(m.uuid)<<" id "<<m.id<<std::dec<<" lod "<<m.lod<<" state "<<m.state<<" at "<<m.position[0]<<","<<m.position[1]<<","<<m.position[2]
                         <<" heading "<<mod::enemyposes::heading(m.rotation)<<" "<<metres(m)<<" m"<<(m.rides?" rides":"")<<"\n";}
                    note(o.str().c_str());
                }
                else if(auto cm=mod::puppet::parseCargoCommand(text,mod::runStamp(),"mulemove");cm&&anchor.pose.available) {
                    // Test: "mulemove <cm>" moves the standing MULE nearest to the porter that many centimetres east with the
                    // MULE actor's own teleport (50 is one pull step, 500 a snap), and reads it back.
                    try{
                        auto move=mod::enemyposes::nativeTeleport(*sampler);auto mules=mod::enemies::readMules(*sampler,sampler->base);
                        auto here=mod::puppet::transform(anchor.pose).position;const mod::enemies::Mule* best=nullptr;double bestD=1e18;
                        for(auto& m:mules){if(mod::enemies::condition(m.state,m.flags)!=mod::enemies::Up||m.state==5)continue;
                            double d=0;for(int k=0;k<3;++k)d+=(m.position[k]-here[k])*(m.position[k]-here[k]);if(d<bestD){bestD=d;best=&m;}}
                        if(!best||*cm>10000)note("MULEPOS test: none standing, or more than 100 m asked\n");
                        else{mod::enemyposes::Place p;p.position=best->position;p.position[0]+=double(*cm)/100.;p.rotation=mod::enemyposes::upright(mod::enemyposes::heading(best->rotation));
                            bool ok=move(best->address,p);std::array<double,3> after{};sampler->read(best->address+0x28,after.data(),24);
                            std::ostringstream o;o<<"MULEPOS test: key "<<std::hex<<mod::enemies::key(best->uuid)<<std::dec<<" lod "<<best->lod<<" "<<std::sqrt(bestD)<<" m away, from "<<best->position[0]<<","<<best->position[1]<<","<<best->position[2]
                                <<" asked "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]<<" now "<<after[0]<<","<<after[1]<<","<<after[2]<<(ok?" moved":" REFUSED")<<"\n";note(o.str().c_str());}
                    }catch(const std::exception& e){note((std::string("MULEPOS test unavailable: ")+e.what()+"\n").c_str());}
                }
                else if(auto count=mod::puppet::parseCargoCommand(text,mod::runStamp(),"mulegoal");count&&anchor.pose.available) {
                    // Test, read-only (mule_goal.h): "mulegoal <n>" logs the movement goal of the n MULEs with a body nearest to
                    // the porter (at most 12): every link by guarded reads, `unreadable` for one that fails.
                    if(!config.shareEnemyPositions)note("MULEGOAL test: off (share_enemy_positions is not set)\n");
                    else{
                        auto mules=mod::enemies::readMules(*sampler,sampler->base);auto here=mod::puppet::transform(anchor.pose).position;
                        auto metres=[&](const mod::enemies::Mule& m){double d=0;for(int k=0;k<3;++k)d+=(m.position[k]-here[k])*(m.position[k]-here[k]);return std::sqrt(d);};
                        mules.erase(std::remove_if(mules.begin(),mules.end(),[](const mod::enemies::Mule& m){return m.lod!=2;}),mules.end());
                        std::sort(mules.begin(),mules.end(),[&](const mod::enemies::Mule& a,const mod::enemies::Mule& b){return metres(a)<metres(b);});
                        size_t n=std::min<size_t>(mules.size(),std::min<uint32_t>(*count,12));
                        std::ostringstream o;o<<"MULEGOAL test: "<<mules.size()<<" MULEs with a body, "<<n<<" shown, porter at "<<here[0]<<","<<here[1]<<","<<here[2]<<"\n";
                        for(size_t i=0;i<n;++i){auto& m=mules[i];
                            o<<"MULEGOAL test: key "<<std::hex<<mod::enemies::key(m.uuid)<<" id "<<m.id<<std::dec<<" life "<<m.state<<" at "<<m.position[0]<<","<<m.position[1]<<","<<m.position[2]<<" dist "<<metres(m)
                             <<mod::mulegoal::describe(mod::mulegoal::read(*sampler,sampler->base,m.address),sampler->base)<<"\n";}
                        note(o.str().c_str());
                    }
                }
                else if(auto argument=mod::puppet::parseCargoCommand(text,mod::runStamp(),"mulegoto");argument&&anchor.pose.available) {
                    // Test, a checked write and no call (mule_goal.h): "mulegoto <cm>" gives the nearest walking MULE with a body,
                    // 6 m or more from the porter, a forced destination that many centimetres (at most 5000) east of its own, once;
                    // "mulegoto <cm+1000000>" writes it every tick for 5 s. The goal is logged twice a second, and for 3 s after.
                    // "mulegoto <cm+2000000>": once, for the nearest MULE that stands at its walk goal, watched for 20 s.
                    auto wanted=mod::mulegoal::ask(*argument);
                    if(!config.shareEnemyPositions)note("MULEGOAL test: off (share_enemy_positions is not set)\n");
                    else if(!wanted)note("MULEGOAL test: forced refused: offset over 5000 cm\n");
                    else if(muleGoto.active)note("MULEGOAL test: forced refused: the last mulegoto is still watched\n");
                    else{
                        auto mules=mod::enemies::readMules(*sampler,sampler->base);auto here=mod::puppet::transform(anchor.pose).position;
                        auto metres=[&](const mod::enemies::Mule& m){double d=0;for(int k=0;k<3;++k)d+=(m.position[k]-here[k])*(m.position[k]-here[k]);return std::sqrt(d);};
                        std::sort(mules.begin(),mules.end(),[&](const mod::enemies::Mule& a,const mod::enemies::Mule& b){return metres(a)<metres(b);});
                        auto writable=mod::mulegoal::nativeWritable();const mod::enemies::Mule* best=nullptr;mod::mulegoal::Chain chain;std::string last="no standing MULE with a body";unsigned looked=0;
                        for(auto& m:mules){if(m.lod!=2||mod::enemies::condition(m.state,m.flags)!=mod::enemies::Up||m.state==5)continue;
                            if(++looked>12)break;
                            chain=mod::mulegoal::read(*sampler,sampler->base,m.address);last=mod::mulegoal::refusal(*sampler,sampler->base,chain,m.position,here,writable);
                            // "<cm+2000000>": a MULE that stands at its walk goal (everything else as for a walking one).
                            if(wanted->standing){if(last=="not walking"&&mod::mulegoal::refusal(*sampler,sampler->base,chain,m.position,here,writable,false).empty()){best=&m;break;}
                                if(last.empty())last="it walks";continue;}
                            if(last.empty()){best=&m;break;}}
                        if(!best)note(("MULEGOAL test: forced refused: none of the "+std::to_string(std::min(looked,12u))+" nearest can be forced, the last: "+last+"\n").c_str());
                        else{
                            auto destination=mod::mulegoal::east(*chain.destination,wanted->cm);uint64_t key=mod::enemies::key(best->uuid);
                            { std::ostringstream o;o<<"MULEGOAL test: forced before key "<<std::hex<<key<<std::dec<<" cm "<<wanted->cm<<" repeat "<<(wanted->repeat?1:0)<<" to "<<destination[0]<<","<<destination[1]<<","<<destination[2]
                                <<" at "<<best->position[0]<<","<<best->position[1]<<","<<best->position[2]<<" dist "<<metres(*best)<<mod::mulegoal::describe(chain,sampler->base)<<"\n";note(o.str().c_str()); }
                            auto why=mod::mulegoal::force(*sampler,sampler->base,chain,best->position,here,destination,!wanted->standing,writable,mod::mulegoal::nativePoke());
                            { std::ostringstream o;o<<"MULEGOAL test: forced after key "<<std::hex<<key<<std::dec<<" result "<<(why.empty()?"written":"REFUSED: "+why)
                                <<mod::mulegoal::describe(mod::mulegoal::read(*sampler,sampler->base,best->address),sampler->base)<<"\n";note(o.str().c_str()); }
                            if(why.empty()){auto& g=muleGoto;g=MuleGoto{};g.active=true;g.first=false;g.key=key;g.chain=chain;g.destination=destination;g.writes=1;
                                g.until=wanted->repeat?now+mod::mulegoal::RepeatMs:now;g.watchUntil=g.until+(wanted->standing?mod::mulegoal::StandingWatchMs:mod::mulegoal::WatchMs);g.logAt=now+mod::mulegoal::LogMs;}
                        }
                    }
                }
                else if(auto mask=mod::puppet::parseCargoCommand(text,mod::runStamp(),"unbuild");mask&&unconstruct&&anchor.entity&&anchor.pose.available) {
                    // Removal experiment: "unbuild <mask> <stamp>", a 6 x 6 x 10 m box around the point 4 m in front of the
                    // porter; mask bit 1 = owner 1, bit 2 = owner 0, bit 4 = owner 2.
                    auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                    if(l>.5){t.position[0]+=4*fx/l;t.position[1]+=4*fy/l;}
                    unconstruct({-3,-3,-5,0},{3,3,5,0},t,*mask&1,*mask&2,*mask&4);
                    note(("UNBUILD mask "+std::to_string(*mask)+"\n").c_str());
                }
                else if(auto code=mod::puppet::parseCargoCommand(text,mod::runStamp(),"request");code&&constructRequest&&anchor.entity&&anchor.pose.available) {
                    // Research: "request <type + 1000*variant> <stamp>" queues a construction request built by the mod,
                    // 4 m in front of the porter, with a set of the request's flag bytes (game_offsets.md): 0 the wait for
                    // the build effect only (as the game's CreateConstruction export), 1 + the bytes both built and
                    // downloaded requests have, 2 + the built ones, 3 + the save's extra two, 4 = 3 + the upload flag,
                    // 5 no flag at all: built at once, 6 the upload flag only: built at once and uploaded.
                    uint32_t kind=*code%1000,variant=*code/1000;std::vector<uint16_t> flags;
                    if(variant<=4)flags.push_back(0x645);
                    if(variant>=1&&variant<=4)flags.insert(flags.end(),{0x625,0x627,0x631,0x632,0x634,0x63b});
                    if(variant>=2&&variant<=4)flags.insert(flags.end(),{0x641,0x644,0x647});
                    if(variant>=3&&variant<=4)flags.insert(flags.end(),{0x63d,0x63e});
                    if(variant==4||variant==6)flags.push_back(0x620);
                    if(kind>=46||variant>6)note("CONSTRUCT request refused: out of range\n");
                    else {
                        auto t=mod::puppet::transform(anchor.pose);double fx=t.rotation[3],fy=t.rotation[4],l=std::hypot(fx,fy);
                        if(l>.5){t.position[0]+=4*fx/l;t.position[1]+=4*fy/l;}
                        auto id=constructRequest(uint8_t(kind),t,flags);
                        std::ostringstream o;o<<"CONSTRUCT request type "<<kind<<" variant "<<variant<<" id "<<id<<" at "<<t.position[0]<<","<<t.position[1]<<","<<t.position[2]<<"\n";note(o.str().c_str());
                    }
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
            stage="continue";stages.enter("continue",mod::nowUs());
            if(continueLastSave&&gameContext) {
                if(continueAgain){continueAgain=false;autoContinue.again(now-startedAt);autoNoted=false;note("AUTO continue asked again (test command)\n");}
                if(autoContinue.tick(now-startedAt,cache.available)){continueLastSave();if(autoContinue.calls()==1)note("AUTO continue: asking the game to load the last save\n");}
                if(autoContinue.done()&&!autoNoted){autoNoted=true;note(("AUTO in game after "+std::to_string((now-startedAt)/1000)+" s, "+std::to_string(autoContinue.calls())+" requests\n").c_str());}
            }
            stage="body";stages.enter("body",mod::nowUs());
            if(body&&gameContext)tickBody();
            stage="pad";stages.enter("pad",mod::nowUs());
            tickUi();tickPad();notePose();
            repTickEnd(now);
            stage="idle";stages.enter("idle",mod::nowUs());
            if(unappliedReceive) {receiveApply.add(clock-unappliedReceive);unappliedReceive=0;}
            if(gameContext){auto line=stages.report(mod::nowUs());if(!line.empty()){note(line.c_str());
                note(("CONTROLS read between ticks: "+std::to_string(frameSamples.exchange(0))+" frames in the last minute\n").c_str());}
}
            if(gameContext&&now>=gameFramesAt+15000){const uint64_t frames=gameFrames.exchange(0);if(gameFramesAt)note(("FRAMES "+std::to_string(frames)+" game frames in the last 15 s ("+std::to_string(frames/15)+" a second)\n").c_str());gameFramesAt=now;}
            if(!cache.available && !game) {err="provider unavailable during tick";note(err.c_str());return false;}
            if(now-startedAt>1000 && config.provider=="synthetic" && cache.remote.visible && core->connected()) {
                uint64_t behind=core->presentationDelayMs();auto truth=mod::trajectory(now>behind?now-behind:0,config.peerSlot);double e=0;
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
                if(!tickOnce(err)) {std::lock_guard<std::timed_mutex> guard(mutex);if(!failed){failed=true;failure=err;}running=false;try{note(err.c_str());}catch(...){}break;}
            }
            auto wait=schedule.deadline>mod::nowUs()?schedule.deadline-mod::nowUs():0;
            std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(std::max<uint64_t>(wait,500),5000)));
        }
    }
    ~Runtime() {
        running=false;if(ticker.joinable())ticker.join();if(worker.joinable())worker.join();
        if(core && socket!=INVALID_SOCKET) {try{core->disconnect(mod::nowMs());}catch(...){}}
        if(others){try{others->clear(mod::nowMs());}catch(...){}}
        relayStop();
        if(chatBox)chatBox->close();chatTyping=false;chatLink.reset();   // its thread ends within a read's time-outs
        if(infoRead.valid())infoRead.wait();   // a read of the server page ends within its time-outs
        strandPoller.reset();   // its thread ends within a read's time-outs, before the sockets' library goes
        if(socket!=INVALID_SOCKET)closesocket(socket);
        diagnostic.Close();
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
// The proxy hosts file IO so save redirection never moves player diagnostics.
// diag::HostBind: the name follows the Host layout, so a proxy of another layout finds nothing to bind.
MOD_API void mod_diag_bind2(const diag::Host* host) {diagnosticHost=host;}
#ifdef MOD_FAKE_NETWORK
// Test-only datagram boundary. Production DLLs never export these functions.
// Move the fake player (game axes, z up; rotation row-major) and enter the world; menu=1 returns to the main menu.
MOD_API uint32_t mod_test_thread_id() {return currentThread();}
MOD_API uint64_t mod_test_frame_caller() {return mod::dispatch::FixtureSites.framePattern+16;}
MOD_API const char* mod_test_run_stamp() {static std::string stamp=mod::runStamp();return stamp.c_str();}
MOD_API void mod_test_set_event(int on) {std::lock_guard<std::mutex> lock(lifecycle);if(runtime)runtime->fakeEvent=on!=0;}
// Test mode in the fake world: the porter's state (dead, caught by BTs), his status block's two gauges (value, maximum)
// and the crates asked for; take_crate removes one, as a pick-up would.
MOD_API void mod_test_set_vitals(int dead,int caught) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    r.fakeVitals=mod::vitals::State{true,dead!=0,caught!=0,false};}
MOD_API void mod_test_set_gauges(float blood,float bloodMax,float stamina,float staminaMax) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime||!runtime->fake)return;auto* f=runtime->fake.get();
    f->mutate([&]{f->put<float>(0xf800+mod::testmode::BloodValue,blood);f->put<float>(0xf800+mod::testmode::BloodMax,bloodMax);
        f->put<float>(0xf800+mod::testmode::StaminaValue,stamina);f->put<float>(0xf800+mod::testmode::StaminaMax,staminaMax);});}
MOD_API void mod_test_gauges(float* four) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime||!runtime->fake||!four)return;auto* f=runtime->fake.get();
    f->mutate([&]{for(int i=0;i<4;++i){uint64_t at=0xf800+(i==0?mod::testmode::BloodValue:i==1?mod::testmode::BloodMax:i==2?mod::testmode::StaminaValue:mod::testmode::StaminaMax);std::memcpy(&four[i],f->data.data()+at,4);}});}
// The fake dialogue's body, as UTF-8 for ASCII text (other characters as '?'); returns its length.
MOD_API unsigned mod_test_dialog_body(char* out,unsigned cap){std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    unsigned n=0;for(char16_t c:r.fakeDialogBody){if(n+1<cap)out[n]=c<0x80?char(c):'?';++n;}if(cap)out[std::min(n,cap-1)]=0;return n;}
MOD_API unsigned mod_test_map(char* out,unsigned cap){std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    std::ostringstream o;if(r.mapBoard){mod::mapicons::Board::Picture p;r.mapBoard->read(p);for(uint32_t i=0;i<p.count;++i)o<<p.icons[i].x<<","<<p.icons[i].y<<","<<p.icons[i].z<<","<<p.icons[i].area<<";";}
    auto s=o.str();unsigned n=unsigned(s.size());if(cap){std::memcpy(out,s.data(),std::min(n,cap-1));out[std::min(n,cap-1)]=0;}return n;}
MOD_API unsigned mod_test_telop(char* out,unsigned cap){std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    unsigned n=0;for(char16_t c:r.fakeTelop){if(n+1<cap)out[n]=c<0x80?char(c):'?';++n;}if(cap)out[std::min(n,cap-1)]=0;return n;}
MOD_API void mod_test_dialog_answer(unsigned row) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeDialogAnswer=row;}
MOD_API int mod_test_take_crate(unsigned index) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    if(index>=r.fakeDrops.size())return 0;
    r.fakeDrops.erase(r.fakeDrops.begin()+index);return 1;}
// What the fake porter shows of his gear (bit 0 the backpack, bit 1 the wrist cuff) and what he carries (the
// coop-partner --carry form, "hash[:slot[:count]],..." or "none").
MOD_API void mod_test_set_gear(unsigned shown) {std::lock_guard<std::mutex> lock(lifecycle);if(runtime)runtime->fakeGear=shown;}
MOD_API int mod_test_set_carry(const char* spec) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime||!spec)return 0;
    auto summary=mod::carry::parseSpec(spec);if(!summary)return 0;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeCarried=*summary;return 1;
}
MOD_API void mod_test_set_pose(const double* position,const float* rotation,int menu) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime||!runtime->fake)return;
    auto* f=runtime->fake.get();
    f->mutate([&]{f->pose(position[0],position[1],position[2],rotation);if(menu)f->menu();else f->game();});
}
MOD_API int mod_test_drain(unsigned char* bytes) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    if(r.discovery) {if(r.discovery->failed()){error="read-only discovery failed; inspect discovery JSONL and debug output";return 0;}return 1;}
    if(r.outgoing.empty())return 0;
    memcpy(bytes,r.outgoing.front().data(),ds::WireSize);r.outgoing.pop_front();return 1;
}
MOD_API void mod_test_deliver(const unsigned char* bytes) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);++r.received;
    if(r.pending.size()>=256){++r.dropped;return;}
    ds::Bytes b;memcpy(b.data(),bytes,b.size());
    auto fate=mod::link::fate(r.linkProfile,r.config.impairment,r.received,b[6]==uint8_t(ds::Kind::Presence));
    if(fate.lost){++r.linkLost;return;}
    uint64_t delay=fate.delayMs;
    r.pending.push_back({mod::nowUs(),mod::nowMs()+delay,std::vector<uint8_t>(b.begin(),b.end()),b.size()});
}
// The relay's side of the seam for other players (others.h), under the runtime lock as the network thread calls it:
// who is near, one datagram from a player, and what the sessions sent (returns its size, 0 when there is nothing).
MOD_API void mod_test_others_roster(uint64_t id,int enter) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    if(enter)r.othersEntered(id,mod::nowMs());else r.othersLeft(id,mod::nowMs());
}
MOD_API void mod_test_others_deliver(uint64_t from,const unsigned char* bytes,unsigned size) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    r.othersReceived(from,bytes,size,mod::nowMs());
}
// Broadcast mode (crowd.h): the test is the relay socket. One datagram from the relay for the client, and the client's
// next datagram for the relay (returns its size, 0 when there is nothing).
MOD_API void mod_test_relay_deliver(const unsigned char* bytes,unsigned size) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    if(r.crowd&&bytes){++r.relayIn;r.crowd->receive(bytes,size,mod::nowMs());}
}
MOD_API unsigned mod_test_relay_drain(unsigned char* bytes,unsigned capacity) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    while(!r.relayOutgoing.empty()){
        auto d=std::move(r.relayOutgoing.front());r.relayOutgoing.pop_front();
        if(d.empty()||d.size()>capacity)continue;
        memcpy(bytes,d.data(),d.size());return unsigned(d.size());
    }
    return 0;
}
// The fake world's BTs (fights.h Sandbox): a catcher fight that starts or ends by itself, this porter's own hits, gazers.
MOD_API void mod_test_bts_start(unsigned catchers) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeBts.start(catchers);}
MOD_API void mod_test_bts_hit(unsigned ordinal,float percent) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeBts.hit(ordinal,percent);}
MOD_API void mod_test_bts_end(int cleared) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeBts.end(cleared!=0);}
MOD_API void mod_test_bts_gazer(uint64_t key,float life) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeBts.gazer(key,life);}
MOD_API void mod_test_bts_kill(uint64_t key) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);r.fakeBts.kill(key);}
MOD_API float mod_test_bts_gazer_life(uint64_t key) {std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return -1;auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);return r.fakeBts.life(key);}
MOD_API unsigned mod_test_others_drain(uint64_t* to,unsigned char* bytes,unsigned capacity) {
    std::lock_guard<std::mutex> lock(lifecycle);if(!runtime)return 0;
    auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);
    while(!r.othersOutgoing.empty()){
        auto d=std::move(r.othersOutgoing.front());r.othersOutgoing.pop_front();
        if(d.bytes.empty()||d.bytes.size()>capacity)continue;
        *to=d.to;memcpy(bytes,d.bytes.data(),d.bytes.size());return unsigned(d.bytes.size());
    }
    return 0;
}
#endif
int mod_pad(mod_pad_state* s) {
    if(!s||s->size!=sizeof(mod_pad_state))return -1;
    const uint32_t size=s->size;*s={};s->size=size;
    if(!padShared.enabled.load(std::memory_order_acquire))return 0;
    ++padShared.calls;
    // The game's input thread must never wait: skip this poll when a script is being swapped in.
    std::unique_lock<std::mutex> l(padShared.lock,std::try_to_lock);if(!l)return 0;
    mod::pad::Gamepad g;mod::pad::Keys k;if(!padShared.player.sample(mod::nowMs(),g,&k))return 0;
    s->buttons=g.buttons;s->left_trigger=g.leftTrigger;s->right_trigger=g.rightTrigger;s->lx=g.lx;s->ly=g.ly;s->rx=g.rx;s->ry=g.ry;
    static_assert(sizeof s->keys==mod::pad::MaxKeys,"key slots");
    memcpy(s->keys,k.vk,sizeof s->keys);s->key_count=k.count;s->mouse_buttons=k.mouseButtons;s->mouse_x=k.mouseX;s->mouse_y=k.mouseY;
    s->mouse_wheel=k.wheel;s->wheel_seq=k.wheel?k.wheelStep:0;
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
        std::lock_guard<std::timed_mutex> guard(r.mutex);
        if(r.failed) {error=r.failure;return 0;}
        return 1;
    }
    std::string err;
    if(!r.tickOnce(err)) {error=err;return 0;}
    return 1;
}
int mod_game_thread_frame(const mod_game_thread_context* context) {
    std::unique_lock<std::mutex> lock(lifecycle,std::try_to_lock);if(!lock)return 0;
    if(!runtime||!context)return 0;
    auto& r=*runtime;
    if(!r.config.gameThread||!r.dispatchVerified||!r.gameGate.accept(*context,currentThread()))return 0;
    if(r.failed)return 0;
    try{r.movePassenger();return r.sampleControls()?1:0;}catch(...){return 0;}
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
        std::lock_guard<std::timed_mutex> guard(r.mutex);r.failed=true;r.failure=err;r.running=false;error=err;
        try{r.note(err.c_str());}catch(...){}return -1;
    }
    return 1;
}
void mod_shutdown() {std::lock_guard<std::mutex> lock(lifecycle);auto* r=runtime;runtime=nullptr;delete r;}
const char* mod_last_error() {
    thread_local std::string copy;
    std::lock_guard<std::mutex> lock(lifecycle);
    if(runtime) {std::lock_guard<std::timed_mutex> guard(runtime->mutex);copy=runtime->discovery&&runtime->discovery->failed()?"read-only discovery failed; inspect discovery JSONL and debug output":runtime->failed?runtime->failure:error;} else copy=error;
    return copy.c_str();
}
unsigned mod_status(char* out,unsigned cap) {
    std::lock_guard<std::mutex> lock(lifecycle);std::string s="{}";
    if(runtime && runtime->discovery) {
        s=std::string("{\"discovery\":true,\"failed\":")+(runtime->discovery->failed()?"true":"false")+"}";
    } else if(runtime) {auto& r=*runtime;std::lock_guard<std::timed_mutex> guard(r.mutex);auto errors=r.errors;std::sort(errors.begin(),errors.end());
        auto p=[&](double q){return errors.empty()?-1.:errors[size_t((errors.size()-1)*q)];};
        std::ostringstream o;o<<"{\"handshake\":"<<(r.paired?"true":"false")<<",\"connected\":"<<(r.core->connected()?"true":"false")<<",\"sent\":"<<r.sent<<",\"received\":"<<r.received<<",\"dropped\":"<<r.dropped<<",\"link_lost\":"<<r.linkLost<<",\"accepted\":"<<r.core->accepted()<<",\"wrong_accepted\":"<<r.wrongAccepted<<",\"build_refused\":"<<r.buildRefused<<",\"version_refused\":"<<r.versionRefused<<",\"session_refused\":"<<r.sessionRefused<<",\"rtt_ms\":"<<r.measuredRtt<<",\"current_rtt_ms\":"<<r.core->rttMs()<<",\"samples\":"<<errors.size()<<",\"error_p50\":"<<p(.5)<<",\"error_p95\":"<<p(.95)<<",\"error_p99\":"<<p(.99)<<",\"error_max\":"<<p(1)<<",\"failed\":"<<(r.failed?"true":"false")<<",\"self_tick\":{\"enabled\":"<<(r.config.selfTick?"true":"false")<<",\"ticks\":"<<r.ticks<<"},\"pose\":{\"available\":"<<(r.poseAvailable?"true":"false")<<",\"reason\":\""<<r.poseReason<<"\"}";
        o<<",\"game_thread\":{\"enabled\":"<<(r.config.gameThread?"true":"false")<<",\"verified\":"<<(r.dispatchVerified?"true":"false")<<",\"thread_id\":"<<r.gameGate.thread()<<",\"rejected\":"<<r.gameRejected<<"},\"puppet\":{\"enabled\":"<<(r.puppet?"true":"false")<<",\"stage\":\""<<(r.puppet?r.puppet->stage():"disabled")<<"\",\"actor\":"<<(r.puppet?r.puppet->actor():0)<<"}";
        o<<",\"pad\":{\"enabled\":"<<(r.config.virtualPad?"true":"false")<<",\"running\":"<<(r.padRunning?"true":"false")<<",\"calls\":"<<padShared.calls.load()<<",\"active_calls\":"<<padShared.active.load()<<"}";
        o<<",\"event\":"<<(r.inEvent?"true":"false")<<",\"cargo\":{\"sent\":"<<r.cargoSent<<",\"created\":"<<r.cargoMade<<",\"taken\":"<<r.cargoTaken<<",\"removed\":"<<r.cargoRemoved<<",\"shared\":"<<r.cargoSync.shared()<<",\"mirrored\":"<<r.cargoSync.mirrored()<<",\"structures_sent\":"<<r.structSent<<",\"structures_created\":"<<r.structMade<<",\"pending\":"<<(r.core?r.core->cargoPending():0)<<"}";
        {mod::others::Counters k;if(r.others)k=r.others->counters();
         o<<",\"others\":{\"on\":"<<(r.others?"true":"false")<<",\"mode\":\""<<(r.crowd?"broadcast":"pairs")<<"\",\"near\":"<<(r.others?r.others->size():0)<<",\"connected\":"<<(r.others?r.others->connected():0)
          <<",\"bodies\":"<<(r.otherBodies?r.otherBodies->made():0)<<",\"shown\":"<<(r.otherBodies?r.otherBodies->driving():0)
          <<",\"sent\":"<<k.sent<<",\"received\":"<<k.received<<",\"accepted\":"<<k.accepted<<",\"refused\":"<<k.refused<<",\"unknown\":"<<k.unknown<<",\"malformed\":"<<k.malformed<<"}";
         o<<",\"labels_shown\":"<<(r.nameTags?r.nameTags->shown():0);
         // What is shown on the bodies: gear copies and case meshes that exist now, in all and per key (0: the partner).
         o<<",\"gear_shown\":{\"pieces\":"<<(r.gears?r.gears->made():0)<<",\"by\":{";
         if(r.gears){bool first=true;for(auto key:r.gears->keys()){o<<(first?"":",")<<"\""<<key<<"\":"<<r.gears->made(key);first=false;}}
         o<<"},\"sets\":"<<k.gear<<"},\"cases_shown\":{\"cases\":"<<(r.stacks?r.stacks->shown():0)<<",\"by\":{";
         if(r.stacks){bool first=true;for(auto key:r.stacks->keys()){o<<(first?"":",")<<"\""<<key<<"\":"<<r.stacks->shown(key);first=false;}}
         o<<"},\"sets\":"<<k.carried<<",\"dropped_events\":"<<k.events<<"}";}
        {mod::fights::Counters k;if(r.fights)k=r.fights->counters();auto role=r.fights?r.fights->role():mod::fights::Role::None;
         o<<",\"fights\":{\"on\":"<<(r.fights?"true":"false")<<",\"role\":\""<<(role==mod::fights::Role::Owner?"owner":role==mod::fights::Role::Visitor?"visitor":"none")
          <<"\",\"owner\":"<<(r.fights?r.fights->owner():0)<<",\"fight\":"<<(r.fights?r.fights->fight():0)<<",\"joined\":"<<(r.fights&&r.fights->joined()?"true":"false")
          <<",\"shown\":"<<(r.fights&&r.fights->shown()?"true":"false")<<",\"visitors\":"<<(r.crowd?unsigned(r.crowd->client().visitors()):0u)
          <<",\"hosted\":"<<k.hosted<<",\"visits\":"<<k.joined<<",\"released\":"<<k.released<<",\"states\":"<<k.states<<",\"reports\":"<<k.reports
          <<",\"states_taken\":"<<k.statesTaken<<",\"reports_taken\":"<<k.reportsTaken<<",\"damage_applied\":"<<k.damageApplied<<",\"gazers_applied\":"<<k.gazersApplied
          <<",\"kills_taken\":"<<k.killsTaken<<",\"kills_refused\":"<<k.killsRefused<<",\"own_first\":"<<k.ownFirst<<",\"commands\":"<<k.commands<<",\"writes\":"<<k.writes<<"}";}
        o<<",\"route\":{\"on\":"<<(r.routeRun?"true":"false")<<",\"state\":\""<<(r.routeRun?mod::route::phaseName(r.routeRun->phase()):"none")<<"\",\"elapsed_ms\":"<<(r.routeRun?r.routeRun->elapsed():0)
         <<",\"progress_m\":"<<(r.routeRun?r.routeRun->progress():0)<<",\"outside_m\":"<<(r.routeRun?r.routeRun->outside():0)<<",\"reason\":\""<<(r.routeRun?r.routeRun->reason():std::string())
         <<"\",\"recording\":"<<(r.routeRecorder.on()?"true":"false")<<",\"recorded_samples\":"<<r.routeRecorder.samples()<<"}";
        o<<",\"test_mode\":{\"on\":"<<(r.config.testMode.on?"true":"false")<<",\"sets\":"<<r.testSets<<",\"crates\":"<<r.testCrates<<",\"refused\":"<<r.testRefused
         <<",\"on_ground\":"<<r.testDropper.onGround()<<",\"blood_writes\":"<<r.testBloodWrites<<",\"stamina_writes\":"<<r.testStaminaWrites<<",\"battery_writes\":"<<r.testBatteryWrites<<",\"gauges\":\""<<r.testGaugeWhy<<"\""
         <<",\"keep\":{\"stamina\":"<<(r.testStamina?"true":"false")<<",\"blood\":"<<(r.testBlood?"true":"false")<<",\"battery\":"<<(r.testBattery?"true":"false")<<"}"
         <<",\"menu\":{\"ready\":"<<(r.debugMenu?"true":"false")<<",\"open\":"<<(r.debugMenu&&r.debugMenu->open()?"true":"false")<<",\"opens\":"<<(r.debugMenu?r.debugMenu->opens():0)<<",\"switches\":"<<(r.debugMenu?r.debugMenu->switches():0)<<",\"selected\":"<<(r.debugMenu?r.debugMenu->selected():0)<<"}}";
        {mod::chat::Link::Counters k;if(r.chatLink)k=r.chatLink->counters();
         o<<",\"chat\":{\"ready\":"<<(r.chatBox?"true":"false")<<",\"on\":"<<(k.on?"true":"false")<<",\"typing\":"<<(r.chatBox&&r.chatBox->typing()?"true":"false")<<",\"reads\":"<<k.reads<<",\"sent\":"<<k.sent<<",\"refused\":"<<k.refused<<",\"received\":"<<k.received<<"}";}
        {mod::mapicons::Link::Counters k;if(r.mapLink)k=r.mapLink->counters();auto s=mod::mapicons::stats();
         o<<",\"player_map\":{\"ready\":"<<(r.mapLink?"true":"false")<<",\"on\":"<<(k.on?"true":"false")<<",\"reads\":"<<k.reads<<",\"shown\":"<<k.shown<<",\"area\":"<<r.gameArea<<",\"updates\":"<<s.updates<<",\"taken\":"<<s.taken<<",\"refused\":"<<s.refused<<",\"shown_area\":"<<s.shownArea<<",\"mode\":"<<s.mode<<"}";}
        {auto s=mod::serverworld::stats();o<<",\"server_world\":{\"on\":"<<(r.serverWorld?"true":"false")<<",\"listed\":"<<(r.serverWorld?r.serverWorld->listed():0)<<",\"reads\":"<<(r.serverWorldLink?r.serverWorldLink->reads():0)<<",\"asked\":"<<s.asked<<",\"refused\":"<<s.refused<<",\"loads\":"<<s.loads<<",\"saves\":"<<s.saves<<",\"left_out\":"<<s.leftOut<<"}";}
        o<<",\"server_info\":{\"ready\":"<<(r.infoScreen?"true":"false")<<",\"open\":"<<(r.infoScreen&&r.infoScreen->open()?"true":"false")<<",\"opens\":"<<(r.infoScreen?r.infoScreen->opens():0)
         <<",\"page\":"<<(r.infoScreen?r.infoScreen->page():0)<<",\"pages\":"<<(r.infoScreen?r.infoScreen->pageCount():0)<<",\"answered\":"<<(r.infoScreen&&r.infoScreen->answered()?"true":"false")<<"}";
        o<<",\"free_build\":\""<<mod::freebuild::label(r.freeBuild)<<"\"";
        o<<",\"received_structures\":{\"on\":"<<(r.receivedOn?"true":"false")<<",\"answers\":\""<<mod::received::label(r.receivedEarly)<<"\",\"built\":"<<r.receivedBuilder.changed()<<",\"skipped\":"<<r.receivedBuilder.skipped()<<"}";
        {mod::strand::Poller::Counters k;if(r.strandPoller)k=r.strandPoller->counters();
         o<<",\"strand\":{\"on\":"<<(r.strandPoller?"true":"false")<<",\"reads\":"<<k.reads<<",\"answers\":"<<k.answers<<",\"failures\":"<<k.failures<<",\"told\":"<<k.told
          <<",\"known\":"<<k.known<<",\"refused\":"<<(k.refused?"true":"false")<<",\"pending\":"<<(r.notices?r.notices->pending():0)
          <<",\"offer_old\":\""<<(r.strandPoller?r.strandPoller->offerOld():std::string())<<"\"}";}
        {mod::hudmarkers::Board::Picture hud;r.pingBoard().read(hud);
         o<<",\"group_pings\":{\"on\":"<<(r.config.groupPings&&r.crowd?"true":"false")<<",\"pings_sent\":"<<r.groupPingsSent<<",\"pings_shown\":"<<r.groupPingsShown
          <<",\"likes_sent\":"<<r.groupLikesSent<<",\"likes_given\":"<<r.groupLikesGiven<<",\"hud\":[";
         for(uint32_t i=0;i<hud.count;++i)o<<(i?",":"")<<"{\"id\":"<<hud.marks[i].id<<",\"at\":["<<hud.marks[i].x<<","<<hud.marks[i].y<<","<<hud.marks[i].z<<"]}";
         o<<"]}";}
        {auto& k=r.offerer.counters();
         o<<",\"offer_strand\":{\"on\":"<<(k.on?"true":"false")<<",\"asked\":"<<k.asked<<",\"uploaded\":"<<k.uploaded<<",\"sent_again\":"<<k.recreated<<",\"refused\":"<<k.refused
          <<",\"no_answer\":"<<k.timedOut<<",\"not_queued\":"<<k.unqueued<<",\"gone\":"<<k.gone<<",\"left_out\":"<<k.left<<",\"stopped\":"<<(k.broken?"true":"false")<<"}";}
#ifdef MOD_FAKE_NETWORK
        {auto ascii=[](const std::u16string& n){std::string t;for(auto ch:n)t+=(ch>=0x20&&ch<0x7f&&ch!='"'&&ch!='\\')?char(ch):'?';return t;};
         o<<",\"label_names\":[";bool first=true;for(auto& [id,l]:r.fakeLabels){o<<(first?"":",")<<"\""<<ascii(l.name)<<"\"";first=false;}
         o<<"],\"notices_shown\":[";first=true;for(auto& n:r.fakeNotices){o<<(first?"":",")<<"\""<<ascii(n)<<"\"";first=false;}
         o<<"],\"fake_drops\":[";first=true;for(auto& d:r.fakeDrops){o<<(first?"":",")<<"{\"item\":"<<d.hash<<",\"at\":["<<d.at[0]<<","<<d.at[1]<<","<<d.at[2]<<"]}";first=false;}
         o<<"],\"labels\":[";first=true;for(auto& [id,l]:r.fakeLabels){o<<(first?"":",")<<"{\"name\":\""<<ascii(l.name)<<"\",\"at\":["<<l.at[0]<<","<<l.at[1]<<","<<l.at[2]<<"]}";first=false;}
         o<<"]";
         // The fake engine's tables: every gear piece (the body it hangs on, where that body is) and every case mesh.
         o<<",\"pieces\":[";first=true;
         for(auto& [id,x]:r.fakePieces){auto pose=mod::puppet::readEntityPose(*r.sampler,x.parent);
             o<<(first?"":",")<<"{\"helper\":\""<<x.helper<<"\",\"visible\":"<<(x.visible?"true":"false")<<",\"at\":["<<pose.position[0]<<","<<pose.position[1]<<","<<pose.position[2]<<"]}";first=false;}
         o<<"],\"pieces_orphaned\":"<<r.fakeOrphaned<<",\"cases\":[";first=true;
         for(auto& [id,x]:r.fakeCases){o<<(first?"":",")<<"{\"item\":"<<x.item<<",\"at\":["<<x.at[0]<<","<<x.at[1]<<","<<x.at[2]<<"]}";first=false;}
         o<<"]";
         // The fake world's catcher manager and gazers, and everything the mod did to them.
         auto& b=r.fakeBts;o<<",\"fake_bts\":{\"active\":"<<(b.active()?"true":"false")<<",\"cleared\":"<<b.cleared()<<",\"lives\":[";first=true;
         for(float l:b.lives()){o<<(first?"":",")<<l;first=false;}
         o<<"],\"commands\":[";first=true;for(auto c:b.commands){o<<(first?"":",")<<int(c);first=false;}
         o<<"],\"writes\":"<<b.writes<<",\"unkillable\":"<<b.unkillable<<",\"destroyed\":[";first=true;for(auto k:b.destroyed){o<<(first?"":",")<<k;first=false;}
         o<<"]}";}
#endif
        o<<",\"carry\":{\"enabled\":"<<(r.config.carryVisual?"true":"false")<<",\"events_sent\":"<<r.carrySent<<",\"shown_entries\":"<<r.carry.view().shown().size()<<"}";   // ds3
        o<<",\"double\":{\"stage\":\""<<(r.twin?r.twin->name():"disabled")<<"\",\"mode\":\""<<(r.twinMode==Runtime::TwinMode::Echo?"echo":r.twinMode==Runtime::TwinMode::Lead?"lead":r.twinMode==Runtime::TwinMode::Peer?"peer":r.twinMode==Runtime::TwinMode::Own?"own":"none")<<"\",\"entity\":"<<(r.twin?r.twin->entity():0)<<"}";
        o<<",\"timing\":{\"clock\":\""
#ifdef _WIN32
        <<"QueryPerformanceCounter"
#else
        <<"steady_clock"
#endif
        <<"\",\"frequency_hz\":"<<mod::clockFrequency()<<",\"clock_quantum_us\":"<<1000000./mod::clockFrequency()<<",\"observed_step_us\":"<<r.observedClockStep<<",\"wire_quantum_us\":1000,\"interpolation_ms\":"<<r.core->presentationDelayMs()<<",\"extrapolation_cap_ms\":150,\"bucket_upper_us\":[";
        for(size_t i=0;i<mod::Histogram::limits.size();++i)o<<(i?",":"")<<mod::Histogram::limits[i];
        o<<"],\"network_tick\":"<<r.networkTick.json()<<",\"provider_tick\":"<<r.providerTick.json()<<",\"presence_send\":"<<r.sendInterval.json()<<",\"sample_age\":"<<r.sampleAge.json()<<",\"receive_to_apply\":"<<r.receiveApply.json()<<",\"delivery_lateness\":"<<r.deliveryLateness.json()
        <<",\"ping_sent\":"<<r.pingSent<<",\"pong_received\":"<<r.pongReceived<<",\"first_ping_ms\":"<<(r.pingSent?int64_t(r.firstPing):-1)<<",\"last_ping_ms\":"<<(r.pingSent?int64_t(r.lastPing):-1)<<",\"first_pong_ms\":"<<(r.pongReceived?int64_t(r.firstPong):-1)<<",\"last_pong_ms\":"<<(r.pongReceived?int64_t(r.lastPong):-1)<<"}}";s=o.str();}
    if(out&&cap>=s.size()+1)memcpy(out,s.c_str(),s.size()+1);
    return unsigned(s.size()+1);
}
}
