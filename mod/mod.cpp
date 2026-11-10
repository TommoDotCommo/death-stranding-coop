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
#include "body.h"
#include "vehicle.h"
#include "ride.h"
#include "cargo_stack.h"
#include "likes.h"
#include "nametag.h"
#include "notice.h"
#include "weather.h"
#include "environment.h"
#include "auto_continue.h"
#include "pad.h"
#include "cargo.h"
#include "marker.h"
#include "carry_visual.h"
#include "gear.h"
#include "outfit.h"
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
#include <cctype>
#include <chrono>
#include <mutex>
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
// One of the mod's own keys (like, ping): held by the player (focused window only), or by a running pad script, so an
// unattended test presses it like any other scripted key.
bool modKey(unsigned vk) {
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
    // The porter's visible gear on the partner's own body (gear.h); config partner_gear or puppet.gear "on <stamp>".
    std::filesystem::path gearCommand;std::unique_ptr<mod::gear::Mirror> gear;bool gearOn=false;
    // The partner's own gear (gear.h, CARGO type 7): what they show, and what this porter shows sent to them.
    // The partner's outfit (outfit.h, CARGO type 8): their enabled art parts, and this porter's sent to them.
    std::optional<mod::outfit::Parts> partnerParts;mod::outfit::Sender outfitTx;uint64_t outfitAt=0;
    std::optional<mod::gear::WornSet> partnerWorn;mod::gear::WornReceiver wornRx;mod::gear::WornSender wornTx;uint64_t wornReadAt=0;
    // Read-only vehicle diagnostics. No changes to presence or actor control (vehicle.h).
    std::filesystem::path vehicleCommand;std::unique_ptr<mod::vehicle::Monitor> vehicleMonitor;
    // Optional environment sampling and host time setter. Only the verified callback can call the export.
    std::filesystem::path environmentCommand;std::unique_ptr<mod::environment::Reader> environmentReader;
    std::unique_ptr<mod::environment::Sync> environmentSync;uint64_t environmentReadAt=0,environmentApplyAt=0;bool environmentSend=false;
    std::unique_ptr<mod::weather::Share> weatherShare;std::optional<float> hostRain;uint64_t hostRainAt=0;   // guest: shared rain
    std::function<void(uint32_t)> cargoGive,cargoDelete;std::function<void(uint8_t,const mod::puppet::Transform&,bool)> construct;std::function<bool(uint64_t,unsigned,bool)> partToggle;
    std::function<void(const std::array<float,4>&,const std::array<float,4>&,const mod::puppet::Transform&,bool,bool,bool)> unconstruct;
    std::function<void()> continueLastSave;mod::AutoContinue autoContinue;bool autoNoted=false,continueAgain=false;
    std::function<bool()> gameEvent;bool inEvent=false;uint64_t eventOverAt=0;std::atomic<bool> fakeEvent{false};
    // Shared cargo (cargo.h): placements read from the game's cargo map go to the partner; theirs are created here.
    mod::cargo::Sync cargoSync;uint64_t cargoReadAt=0,cargoManagerSlot=0,cargoManagerVtable=0,cargoSent=0,cargoMade=0,cargoTaken=0,cargoRemoved=0;
    uint64_t cargoFullAt=0;unsigned cargoLimit=mod::cargo::FirstRecords;   // a full record scan every 4 s, quick scans up to its mark
    bool cargoIdsSet=false;
    void tickCargo(uint64_t now) {
        // Watch the world even while the partner is away: what is placed or taken meanwhile is queued by the core and
        // sent when the partner connects (the core keeps undelivered events across reconnects).
        if(!core)return;
        // Shared ids below 2^53 (a wire t-field): 32 random bits above a 20-bit counter.
        if(!cargoIdsSet){cargoSync.setIdBase((nonce()>>32)<<20);cargoIdsSet=true;}
        auto incoming=core->takeCargo();
        bool coop=partnerMode()&&cargoCreate&&cargoManagerSlot;
        for(auto& e:incoming) {
            if(e.type==mod::carry::WireType){if(config.carryVisual)carry.incoming(e,carrySink);continue;}   // ds3: tickCarry
            if(e.type==3){structIn.push_back(e);continue;}   // a structure (tickStructures)
            if(e.type==5){pingIn.push_back(e);continue;}     // a ping (tickMarkers, marker.h)
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
                auto index=cargoSync.partnerTook(e.ref,e.item,at);std::ostringstream o;
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
        auto anchor=anchorNow();if(!anchor.entity||!anchor.pose.available)return;
        bool full=!cargoFullAt||now-cargoFullAt>=4000;auto scanStart=mod::nowUs();
        auto records=mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,full?mod::cargo::MaxRecords:cargoLimit);if(records.empty())return;
        if(full){auto limit=mod::cargo::scanLimit(records);
            if(!cargoFullAt||limit!=cargoLimit){std::ostringstream o;o<<"CARGO full scan: "<<records.size()<<" records in use, quick scans to "<<limit
                <<", took "<<(mod::nowUs()-scanStart)/1000.<<" ms\n";note(o.str().c_str());}
            cargoFullAt=now;cargoLimit=limit;}
        for(auto& p:cargoSync.observe(records,anchor.pose.position,now)) {
            ds::CargoEvent e;e.type=1;e.item=p.hash;e.ref=p.id;e.position={p.position[0],p.position[2],p.position[1]};   // game -> adapter axes
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
    void tickRide(const mod::puppet::Anchor& anchor,uint64_t now) {
        auto stamp=" "+mod::runStamp()+"\n";
        if(config.partnerVehicle&&!rideConfigDone){rideConfigDone=true;if(!ride)makeRide();rideOn=ride!=nullptr;if(rideOn)note("RIDE partner vehicle on (config)\n");}
        if(!rideCommand.empty()&&std::filesystem::exists(rideCommand)) {
            auto text=readConfig(rideCommand);
            if(!std::filesystem::remove(rideCommand))throw std::runtime_error("cannot consume ride command");
            if(text=="on"+stamp){if(!ride)makeRide();rideOn=ride!=nullptr;rideLead=0;}
            else if(text=="off"+stamp){rideOn=false;rideLead=0;}
            else if(text=="park"+stamp){rideLead=0xff;}   // test: the lead vehicle stops where it is
            else if(text=="rider"+stamp){rideHideRider=!rideHideRider;}
            else if(text=="notice"+stamp){noticeTest=true;}           // test: one line in the game's info log
            else if(text=="continue"+stamp){continueAgain=true;}      // test: load the last save again (from the title screen)
            else if(text=="tag on"+stamp){tagTest=true;}              // test: a name over the partner body
            else if(text=="tag off"+stamp){tagTest=false;}
            else if(text=="stack off"+stamp){stackTest=-2;}            // test: no cases drawn
            else if(text=="stack auto"+stamp){stackTest=-1;}           // the partner's summary (the default)
            else if(text.rfind("stack ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test: `stack N` draws N plain cases on the body whatever the partner carries; `stack at ox oy oz ux uy uz`
                // moves them and sets the stacking direction (anchor axes right, forward, up; metres).
                mod::stack::Layout l;int n=0;
                if(std::sscanf(text.c_str()+6,"at %lf %lf %lf %lf %lf %lf",&l.offset[0],&l.offset[1],&l.offset[2],&l.up[0],&l.up[1],&l.up[2])==6){
                    auto now=stackLayout.value_or(mod::stack::Layout{});now.offset=l.offset;now.up=l.up;stackLayout=now;if(cargoStack)cargoStack->layout=now;}
                else if(unsigned cols=0;std::sscanf(text.c_str()+6,"across %lf %lf %lf %u",&l.across[0],&l.across[1],&l.across[2],&cols)==4&&cols>=1&&cols<=4){
                    auto now=stackLayout.value_or(mod::stack::Layout{});now.across=l.across;now.columns=cols;stackLayout=now;if(cargoStack)cargoStack->layout=now;}
                else if(std::sscanf(text.c_str()+6,"%d",&n)==1&&n>=0&&n<=int(mod::stack::Stack::MaxCases))stackTest=n;
                else {note("RIDE stack refused\n");return;}}
            else if(text.rfind("goto ",0)==0&&text.size()>stamp.size()&&text.compare(text.size()-stamp.size(),stamp.size(),stamp)==0){
                // Lab test: move this porter with the game's own player teleport (join.h), once, in normal gameplay.
                double x=0,y=0,z=0;auto porter=mod::join::readPorter(*sampler);
                if(rideTeleport&&std::sscanf(text.c_str()+5,"%lf %lf %lf",&x,&y,&z)==3&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)
                   &&porter.readable&&!inEvent){mod::puppet::Transform t;t.position={x,y,z};t.rotation=porter.rotation;rideTeleport(porter.player,t);
                    note("RIDE goto done\n");}
                else note("RIDE goto refused\n");
                return;}
            else if(auto k=mod::puppet::parseCargoCommand(text,mod::runStamp(),"lead");k&&*k<=3){if(!ride)makeRide();rideOn=ride!=nullptr;rideLead=uint8_t(*k);}
            else {note("RIDE ignored command for another process\n");return;}
            note(("RIDE command "+text.substr(0,text.find(' '))+(rideLead?" "+std::to_string(rideLead):std::string())+"\n").c_str());
        }
        if(!ride)return;
        if(anchor.entity!=ridePorter){if(ridePorter)ride->forget("world changed");ridePorter=anchor.entity;}
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
        if(!connected||inEvent){ride->tick(now,false,0,nullptr);return;}
        if(!r.visible||!r.state.vehicle){ride->tick(now,true,0,nullptr);return;}
        auto t=mod::ride::vehicleTransform(r.state.position,r.state.vehicleOffset,r.state.vehicleAngles);
        ride->tick(now,true,r.state.vehicle,&t,{r.state.vehicleVariant,r.state.vehicleLook});
    }
    // --- The partner's carried cargo drawn on the partner body (cargo_stack.h): with carry_visual, from the partner's
    // carry summary (backpack slot); `puppet.ride` "stack N" / "stack auto" / "stack off" / "stack at ..." test it.
    std::unique_ptr<mod::stack::Stack> cargoStack;bool stackTried=false;Background<mod::stack::Api> stackJob;int stackTest=-1;uint64_t stackPorter=0;
    std::optional<mod::stack::Layout> stackLayout;
    static std::vector<mod::stack::Item> backpackItems(const std::optional<mod::carry::Summary>& s){
        std::vector<mod::stack::Item> out;if(s)for(auto& e:*s)if(e.slot==1)for(unsigned i=0;i<e.count;++i)out.push_back({e.hash,e.variant});   // 1: BackPack
        return out;}
    void tickStack(uint64_t body,const mod::puppet::Anchor& anchor) {
        bool on=(config.carryVisual&&stackTest!=-2)||stackTest>=0;
        if(!on){if(cargoStack)cargoStack->tick(false,{},nullptr);return;}
#ifndef MOD_FAKE_NETWORK
        if(!cargoStack&&!stackTried){
            auto* sp=&*sampler;if(!stackJob.ready([sp]{return mod::stack::nativeApi(*sp);}))return;   // checked beside the game
            stackTried=true;
            try{auto api=stackJob.job.get();
                // Slow native calls are logged (a case's first mesh can stall a tick while the game sets it up).
                auto slow=[this](const char* what,uint64_t us){if(us>=10000)note((std::string("STACK slow ")+what+" "+std::to_string(us/1000)+" ms\n").c_str());};
                api.caseFor=[f=api.caseFor,slow](uint32_t item,bool variant){auto t=mod::nowUs();auto r=f(item,variant);slow("case lookup",mod::nowUs()-t);return r;};
                api.create=[f=api.create,slow](const mod::stack::Case& c,const mod::puppet::Transform& at){auto t=mod::nowUs();auto r=f(c,at);slow("mesh create",mod::nowUs()-t);return r;};
                cargoStack=std::make_unique<mod::stack::Stack>(std::move(api),[this](const std::string& row){note(row.c_str());});
                if(stackLayout)cargoStack->layout=*stackLayout;}
            catch(const std::exception& e){note((std::string("STACK unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!cargoStack)return;
        if(anchor.entity!=stackPorter){if(stackPorter)cargoStack->forget("world changed");stackPorter=anchor.entity;}
        auto wanted=stackTest>=0?std::vector<mod::stack::Item>(size_t(stackTest)):backpackItems(carrySink.shown);
        uint64_t flags=0;bool shown=body&&sampler->read(body+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        bool permitted=shown&&!inEvent&&(stackTest>=0||(partnerMode()&&core&&core->connected()));
        // On the copy of the backpack the body wears when there is one (it moves with the body's back), else the body.
        uint64_t on_=gear?gear->copyAt("HLP_Backpack"):0;
        auto pose=mod::puppet::readEntityPose(*sampler,on_?on_:body);
        if(!permitted||!(pose.available&&pose.coherent)){cargoStack->tick(false,{},nullptr);return;}
        auto t=mod::puppet::transform(pose);cargoStack->tick(true,wanted,&t);
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
    // --- Notices (notice.h): opt-in `notices`. The partner joined / left / nobody yet, in the game's own info log.
    // `puppet.ride` "notice" shows a test line whatever the session does.
    std::unique_ptr<mod::notice::Notifier> notices;std::optional<mod::notice::Api> noticeApi;bool noticeTried=false,noticeTest=false;Background<mod::notice::Api> noticeJob;
    void tickNotices(uint64_t now) {
        if(!config.notices&&!noticeTest)return;
#ifndef MOD_FAKE_NETWORK
        if(!noticeApi&&!noticeTried){
            auto* sp=&*sampler;if(!noticeJob.ready([sp]{return mod::notice::nativeApi(*sp);}))return;   // checked beside the game
            noticeTried=true;
            try{noticeApi=noticeJob.job.get();note("NOTICE on\n");}
            catch(const std::exception& e){note((std::string("NOTICE unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!noticeApi)return;
        if(noticeTest){noticeTest=false;note(noticeApi->show(u"Co-op mod: test notice.",mod::notice::PlainIcon)?"NOTICE test shown\n":"NOTICE test refused: no info log now\n");}
        if(!config.notices)return;
        if(!notices)notices=std::make_unique<mod::notice::Notifier>(*noticeApi,[this](const std::string& row){note(row.c_str());});
        bool connected=core&&core->connected();
        notices->tick(now,poseAvailable&&!inEvent,connected,partnerName,connected&&(cache.remote.state.flags&ds::PrivateRoom));
    }
    // --- A name over the partner (nametag.h): one of the game's hologram-name records above the partner body.
    // On with config partner_tag; `puppet.ride` "tag on" / "tag off" test it. The name is the partner's nickname.
    std::unique_ptr<mod::nametag::Tag> nameTag;bool tagTried=false,tagTest=false;Background<mod::nametag::Api> tagJob;uint64_t tagPorter=0;
    mod::nametag::Sender nameTx;mod::nametag::Receiver nameRx;std::string partnerName;
    void tickTag(uint64_t body,const mod::puppet::Anchor& anchor) {
        // This player's name goes to the partner on every connection (both ends send; each shows the other's).
        if(core)for(auto& e:nameTx.tick(config.nickname,core->connected()&&partnerMode()))core->queueCargo(e);
        if(!tagTest&&!config.partnerTag){if(nameTag)nameTag->tick(false,u"",nullptr);return;}
#ifndef MOD_FAKE_NETWORK
        if(!nameTag&&!tagTried){auto* sp=&*sampler;if(!tagJob.ready([sp]{return mod::nametag::nativeApi(*sp);}))return;
            tagTried=true;
            try{nameTag=std::make_unique<mod::nametag::Tag>(tagJob.job.get(),[this](const std::string& row){note(row.c_str());});}
            catch(const std::exception& e){note((std::string("TAG unavailable: ")+e.what()+"\n").c_str());}}
#endif
        if(!nameTag)return;
        if(anchor.entity!=tagPorter){if(tagPorter)nameTag->forget("world changed");tagPorter=anchor.entity;}
        uint64_t flags=0;bool shown=body&&sampler->read(body+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        auto pose=body?mod::puppet::readEntityPose(*sampler,body):mod::Pose{};
        if(!shown||inEvent||!(pose.available&&pose.coherent)){nameTag->tick(false,u"",nullptr);return;}
        std::array<double,3> at=pose.position;at[2]+=2.1;   // game axes, z up: above the head
        auto name=mod::nametag::fromUtf8(partnerName);if(name.empty())name=u"Partner";
        nameTag->tick(true,name,&at);
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
            else if(auto vk=mod::puppet::parseCargoCommand(text,stamp,"key");vk&&*vk<=0xfe){pingKey=*vk;note(("MARKER ping key "+std::to_string(*vk)+"\n").c_str());}
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
    void tickCarry(uint64_t now) {
        if(!config.carryVisual||!core||!sampler||!cargoManagerSlot||now-carryReadAt<250)return;
        carryReadAt=now;uint64_t manager=0;std::optional<mod::carry::Summary> read;
        auto anchor=anchorNow();
        if(anchor.entity&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
            if(auto items=mod::carry::readCarried(*sampler,anchor.entity,sampler->base+mod::carry::ComponentVtable,manager))read=mod::carry::summarise(*items);
        std::string d=read?"porter carries "+mod::carry::describe(*read):anchor.entity?"porter's carrier unreadable (layout or vtable check failed)":"";
        if(!d.empty()&&d!=carryLast){carryLast=d;note(("CARRY "+d+"\n").c_str());}
        for(auto& e:carry.tick(read,core->connected()&&partnerMode(),now,carrySink)){core->queueCargo(e);++carrySent;}
    }
    // --- end ds3 ---
    // Shared structures (structures.h): what this porter builds goes to the partner; theirs appear here as Net structures.
    mod::structures::Sync structSync;uint64_t structReadAt=0,structSent=0,structMade=0,structIdBase=0,imageSize=0;bool structTimed=false;
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
            if(mod::structures::standing(table(),*net,b.position)){note(("STRUCT partner structure ref "+std::to_string(e.ref)+" already stands here\n").c_str());continue;}
#endif
            mod::puppet::Transform t;t.position=b.position;t.rotation=mod::structures::rotation(b.yaw,b.pitch,b.roll);
            construct(*net,t,false);++structMade;
            std::ostringstream o;o<<"STRUCT created partner type "<<int(b.type)<<" as "<<int(*net)<<" ref "<<e.ref<<" at "<<b.position[0]<<","<<b.position[1]<<","<<b.position[2]<<"\n";note(o.str().c_str());
        }
        structIn.clear();
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
        } catch(const std::exception& e) {std::lock_guard<std::timed_mutex> guard(mutex);failed=true;failure=e.what();try{note(failure.c_str());}catch(...){}running=false;}
    }
    void init(const std::filesystem::path& path) {
        config=mod::parseConfig(readConfig(path));
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
            vehicleCommand=path.parent_path()/"puppet.vehicle";
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
                markerCommand=path.parent_path()/"puppet.marker";rideCommand=path.parent_path()/"puppet.ride";
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
                cargoCreate=mod::puppet::nativeCargoCreate(*sampler);cargoGive=mod::puppet::nativeCargoGive(*sampler);cargoDelete=mod::puppet::nativeCargoDelete(*sampler);construct=mod::puppet::nativeConstruct(*sampler);partToggle=mod::puppet::nativePartToggle(*sampler);unconstruct=mod::puppet::nativeRemoveConstructions(*sampler);
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
                    auto made=mod::puppet::nativeApi(*sampler);auto driver=mod::puppet::nativeDoubleApi(*sampler);
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
                    gearOn=config.partnerGear;
                    gear=std::make_unique<mod::gear::Mirror>(mod::gear::nativeApi(*sampler),[this](const std::string& row){note(row.c_str());});
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
                if(o.available&&int(r.kind)!=*lastKind){*lastKind=r.kind;
                    std::ostringstream m;m<<"RIDE local kind="<<int(r.kind)<<" vehicle=0x"<<std::hex<<o.vehicle<<"\n";note(m.str().c_str());}
                return r;});}
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
        if(!gear)return;
        if(std::filesystem::exists(gearCommand)) {
            auto command=readConfig(gearCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(gearCommand))throw std::runtime_error("cannot consume gear command");
            if(command=="on"+stamp){gearOn=true;gear->start();note("GEAR on\n");}
            else if(command=="off"+stamp){gearOn=false;gear->stop();note("GEAR off\n");}
            else note("GEAR ignored command for another process\n");
        }
        if(!gearOn||!anchor.entity)return;
        // What this porter shows goes to the partner (read once a second; sent when it holds still, and on every connection).
        // Nothing is read until a load (a game event) has been over for SettleMs: right after one the porter shows gear
        // and parts he hides seconds later (live: 7 pieces sent, then 2).
        constexpr uint64_t SettleMs=8000;
        bool settled=!inEvent&&now-eventOverAt>=SettleMs&&anchor.pose.available;
        if(core&&settled&&now-wornReadAt>=1000){wornReadAt=now;stages.enter("gear worn",mod::nowUs());
            auto mine=mod::gear::worn(mod::gear::snapshot(*sampler,anchor.entity),sampler->base);
            auto events=wornTx.observe(mine,core->connected(),now);
            if(!events.empty()){note(("GEAR sent "+std::to_string(mine.size())+" pieces\n").c_str());for(auto& e:events)core->queueCargo(e);}}
        stages.enter("gear",mod::nowUs());
        uint64_t flags=0;bool shown=bodyEntity&&sampler->read(bodyEntity+mod::gear::Flags,&flags,8)&&(flags&mod::gear::FlagVisible);
        // The outfit: this porter's art parts go to the partner; the body takes the partner's, a few parts a second.
        if(core&&now-outfitAt>=1000){outfitAt=now;stages.enter("gear outfit",mod::nowUs());
            if(settled)if(auto e=outfitTx.observe(mod::outfit::read(*sampler,anchor.entity),core->connected(),now)){note("OUTFIT sent\n");core->queueCargo(*e);}
            if(bodyEntity&&shown&&partnerParts&&partToggle)if(auto mine=mod::outfit::read(*sampler,bodyEntity)){
                auto todo=mod::outfit::changes(*mine,*partnerParts,4);unsigned done=0;
                for(auto& [index,on]:todo)done+=partToggle(bodyEntity,index,on);
                if(!todo.empty())note(("OUTFIT switched "+std::to_string(done)+" of "+std::to_string(todo.size())+" parts on the body\n").c_str());}}
        stages.enter("gear mirror",mod::nowUs());
        auto pose=bodyEntity?mod::puppet::readEntityPose(*sampler,bodyEntity):anchor.pose;
        if(bodyEntity&&!(pose.available&&pose.coherent))return;   // never make pieces at an unreadable place
        auto at=bodyEntity?mod::puppet::transform(pose):mod::puppet::Transform{};
        auto* sp=&*sampler;uint64_t porter=anchor.entity;
        // The partner's own gear when they have sent it; else what this porter shows.
        auto theirs=partnerWorn;
        gear->tick(bodyEntity,porter,[sp,porter,theirs]{auto nodes=mod::gear::snapshot(*sp,porter);
            return theirs?mod::gear::planFor(nodes,sp->base,*theirs):mod::gear::plan(nodes,sp->base);},at,shown);
    }
    void tickTwin(uint64_t now) {
        if(std::filesystem::exists(twinCommand)) {
            auto command=readConfig(twinCommand);auto stamp=" "+mod::runStamp()+"\n";
            if(!std::filesystem::remove(twinCommand))throw std::runtime_error("cannot consume double command");
            if(command=="echo"+stamp)twinMode=TwinMode::Echo;else if(command=="peer"+stamp)twinMode=TwinMode::Peer;
            else if(auto ownMask=mod::puppet::parseOwnCommand(command,stamp)){
                // own: the porter's art parts (mask 8: drawn, stable; stretched-polygon artifacts). ownN / ownNN (hex),
                // research only: 1 cover, 2 cover+animation, 4 facial rig (lazy, empty components: 4 hangs the game),
                // 9 = 8 and remove the body's own ArtPartsData (hangs the game); 10 the engine's model swap, 20 the swap
                // then the apply if needed, 40 a read-only report (double.h). game_offsets.md, "Own-body mode".
                dressMask=*ownMask;twinMode=TwinMode::Own;if(ownTwin)ownTwin->enable();
                note(("DOUBLE own dress mask 0x"+[](unsigned m){std::ostringstream o;o<<std::hex<<m;return o.str();}(dressMask)+"\n").c_str());}
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
        if(event!=inEvent){inEvent=event;if(!event)eventOverAt=now;note(event?"DOUBLE game event: double handed back\n":"DOUBLE game event over\n");}
        // A partner who is only missing for now (late packets, private room) is hidden, not handed back; a game event
        // or the mode switched off hands the double back.
        const mod::puppet::Transform* goal=(target&&!event)?&*target:nullptr;bool hold=twinMode!=TwinMode::None&&!event;
        // Test toggle: hide the body (kept, not handed back) while the partner's vehicle is drawn.
        if(goal&&rideHideRider&&partnerMode()&&cache.remote.state.vehicle&&ride&&ride->entity()&&ride->kind()==cache.remote.state.vehicle)goal=nullptr;
        // Own body: made (once per world) and driven by its own backend; the resident double is released, never
        // dropped while borrowed. Until the body exists, or if the game refuses to make one, the resident double is used.
        bool useOwn=twinMode==TwinMode::Own&&own&&ownTwin&&!own->exhausted();
        auto anchor=anchorNow();
        stages.enter("body make",mod::nowUs());
        if(useOwn)ownEntity=own->tick(anchor);
        stages.enter("body drive",mod::nowUs());
        {double steer=partnerMode()&&cache.remote.visible?cache.remote.state.steer:0;twin->steering(steer);if(ownTwin)ownTwin->steering(steer);}
        if(useOwn&&ownEntity){
            twin->tick(true,nullptr);ownTwin->tick(true,goal,speed,actions,hold);
        }
        else{twin->tick(true,goal,speed,actions,hold);if(ownTwin)ownTwin->tick(true,nullptr);}
        stages.enter("gear",mod::nowUs());
        tickGear(useOwn?ownEntity:0,anchor,now);
        stages.enter("stack",mod::nowUs());
        tickStack(useOwn?ownEntity:0,anchor);
        stages.enter("tag",mod::nowUs());
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
            if(lastProvider)providerTick.add(clock-lastProvider);
            lastProvider=clock;++ticks;
            stages.enter("provider",mod::nowUs());
            cache.sampledAt=now;cache.available=provider->readLocal(cache.local);cache.gated=provider->isInCutsceneOrLoading();
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
                stages.enter("start",mod::nowUs());
            }
            if(cache.available) {
                // A jump flag must survive until a presence packet carrying it has been sent.
                if(cache.local.flags&ds::Teleport)cache.stickyTeleport=true;else if(cache.stickyTeleport)cache.local.flags|=ds::Teleport;
            }
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
                    auto observation=environmentReader->read(position);note(mod::environment::row(observation).c_str());
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
            stage="markers";stages.enter("markers",mod::nowUs());
            if(twin&&gameContext)tickMarkers(now);
            stage="likes";stages.enter("likes",mod::nowUs());
            if(twin&&gameContext)tickLikes(now);
            stage="notices";stages.enter("notices",mod::nowUs());
            if(gameContext)tickNotices(now);
            stage="cargo command";stages.enter("cargo command",mod::nowUs());
            if(cargoCreate&&gameContext&&std::filesystem::exists(cargoCommand)) {
                auto text=readConfig(cargoCommand);
                if(!std::filesystem::remove(cargoCommand))throw std::runtime_error("cannot consume cargo command");
                auto id=mod::puppet::parseCargoCommand(text,mod::runStamp());auto anchor=anchorNow();
                auto give=mod::puppet::parseCargoCommand(text,mod::runStamp(),"give");
                auto erase=mod::puppet::parseCargoCommand(text,mod::runStamp(),"delete");
                auto list=mod::puppet::parseCargoCommand(text,mod::runStamp(),"list");
                if(give&&cargoGive){cargoGive(*give);note(("CARGO give id="+std::to_string(*give)+"\n").c_str());}
                else if(erase&&cargoDelete){cargoDelete(*erase);note(("CARGO delete id="+std::to_string(*erase)+"\n").c_str());}
                else if(auto code=mod::puppet::parseCargoCommand(text,mod::runStamp(),"part");code&&partToggle&&ownEntity) {
                    // Research: "part <index*2+on> <stamp>" switches one art part of the own body.
                    bool ok=partToggle(ownEntity,*code/2,*code&1);
                    note(("PART "+std::to_string(*code/2)+(*code&1?" on":" off")+(ok?"\n":" failed\n")).c_str());
                }
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
            stage="continue";stages.enter("continue",mod::nowUs());
            if(continueLastSave&&gameContext) {
                if(continueAgain){continueAgain=false;autoContinue.again(now-startedAt);autoNoted=false;note("AUTO continue asked again (test command)\n");}
                if(autoContinue.tick(now-startedAt,cache.available)){continueLastSave();if(autoContinue.calls()==1)note("AUTO continue: asking the game to load the last save\n");}
                if(autoContinue.done()&&!autoNoted){autoNoted=true;note(("AUTO in game after "+std::to_string((now-startedAt)/1000)+" s, "+std::to_string(autoContinue.calls())+" requests\n").c_str());}
            }
            stage="body";stages.enter("body",mod::nowUs());
            if(body&&gameContext)tickBody();
            stage="pad";stages.enter("pad",mod::nowUs());
            tickPad();notePose();
            stage="idle";stages.enter("idle",mod::nowUs());
            if(unappliedReceive) {receiveApply.add(clock-unappliedReceive);unappliedReceive=0;}
            if(gameContext){auto line=stages.report(mod::nowUs());if(!line.empty())note(line.c_str());}
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
                if(!tickOnce(err)) {std::lock_guard<std::timed_mutex> guard(mutex);if(!failed){failed=true;failure=err;}running=false;try{note(err.c_str());}catch(...){}break;}
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
    mod::pad::Gamepad g;mod::pad::Keys k;if(!padShared.player.sample(mod::nowMs(),g,&k))return 0;
    s->buttons=g.buttons;s->left_trigger=g.leftTrigger;s->right_trigger=g.rightTrigger;s->lx=g.lx;s->ly=g.ly;s->rx=g.rx;s->ry=g.ry;
    static_assert(sizeof s->keys==mod::pad::MaxKeys,"key slots");
    memcpy(s->keys,k.vk,sizeof s->keys);s->key_count=k.count;s->mouse_buttons=k.mouseButtons;s->mouse_x=k.mouseX;s->mouse_y=k.mouseY;
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
        std::ostringstream o;o<<"{\"handshake\":"<<(r.paired?"true":"false")<<",\"connected\":"<<(r.core->connected()?"true":"false")<<",\"sent\":"<<r.sent<<",\"received\":"<<r.received<<",\"dropped\":"<<r.dropped<<",\"accepted\":"<<r.core->accepted()<<",\"wrong_accepted\":"<<r.wrongAccepted<<",\"build_refused\":"<<r.buildRefused<<",\"version_refused\":"<<r.versionRefused<<",\"session_refused\":"<<r.sessionRefused<<",\"rtt_ms\":"<<r.measuredRtt<<",\"current_rtt_ms\":"<<r.core->rttMs()<<",\"samples\":"<<errors.size()<<",\"error_p50\":"<<p(.5)<<",\"error_p95\":"<<p(.95)<<",\"error_p99\":"<<p(.99)<<",\"error_max\":"<<p(1)<<",\"failed\":"<<(r.failed?"true":"false")<<",\"self_tick\":{\"enabled\":"<<(r.config.selfTick?"true":"false")<<",\"ticks\":"<<r.ticks<<"},\"pose\":{\"available\":"<<(r.poseAvailable?"true":"false")<<",\"reason\":\""<<r.poseReason<<"\"}";
        o<<",\"game_thread\":{\"enabled\":"<<(r.config.gameThread?"true":"false")<<",\"verified\":"<<(r.dispatchVerified?"true":"false")<<",\"thread_id\":"<<r.gameGate.thread()<<",\"rejected\":"<<r.gameRejected<<"},\"puppet\":{\"enabled\":"<<(r.puppet?"true":"false")<<",\"stage\":\""<<(r.puppet?r.puppet->stage():"disabled")<<"\",\"actor\":"<<(r.puppet?r.puppet->actor():0)<<"}";
        o<<",\"pad\":{\"enabled\":"<<(r.config.virtualPad?"true":"false")<<",\"running\":"<<(r.padRunning?"true":"false")<<",\"calls\":"<<padShared.calls.load()<<",\"active_calls\":"<<padShared.active.load()<<"}";
        o<<",\"event\":"<<(r.inEvent?"true":"false")<<",\"cargo\":{\"sent\":"<<r.cargoSent<<",\"created\":"<<r.cargoMade<<",\"taken\":"<<r.cargoTaken<<",\"removed\":"<<r.cargoRemoved<<",\"shared\":"<<r.cargoSync.shared()<<",\"mirrored\":"<<r.cargoSync.mirrored()<<",\"structures_sent\":"<<r.structSent<<",\"structures_created\":"<<r.structMade<<",\"pending\":"<<(r.core?r.core->cargoPending():0)<<"}";
        o<<",\"carry\":{\"enabled\":"<<(r.config.carryVisual?"true":"false")<<",\"events_sent\":"<<r.carrySent<<",\"shown_entries\":"<<r.carry.view().shown().size()<<"}";   // ds3
        o<<",\"double\":{\"stage\":\""<<(r.twin?r.twin->name():"disabled")<<"\",\"mode\":\""<<(r.twinMode==Runtime::TwinMode::Echo?"echo":r.twinMode==Runtime::TwinMode::Lead?"lead":r.twinMode==Runtime::TwinMode::Peer?"peer":r.twinMode==Runtime::TwinMode::Own?"own":"none")<<"\",\"entity\":"<<(r.twin?r.twin->entity():0)<<"}";
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
