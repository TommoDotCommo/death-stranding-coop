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
#include "online.h"
#include "vitals.h"
#include "bts.h"
#include "roads.h"
#include "enemies.h"
#include "orders.h"
#include "guard_win.h"
#include "link.h"
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
    uint32_t gearChurnMode=0,gearChurnLeft=0;uint64_t gearChurnAt=0;
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
    std::function<void(uint32_t)> cargoGive,cargoDelete;std::function<void(uint8_t,const mod::puppet::Transform&,bool)> construct;std::function<uint32_t(uint8_t,const mod::puppet::Transform&,const std::vector<uint16_t>&)> constructRequest;std::function<bool(uint64_t,unsigned,bool)> partToggle;
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
            if(e.type==mod::enemies::WireType){enemyIn.push_back(e);continue;}
            if(e.type==mod::bts::WireType){btIn.push_back(e);continue;}
            if(e.type==mod::roads::WireType){roadIn.push_back(e);continue;}
            if(e.type==LevelWire){levelIn.push_back(e);continue;}                      // a structure reached a level (tickStructures)             // road news (tickRoads)                // BT news (tickBts)   // a MULE went down (tickEnemies)
            if(e.type==mod::orders::WireType){orderIn.push_back(e);continue;}    // an order changed (tickOrders)
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
            note(("ONLINE server "+config.onlineServer+"\n").c_str());}
        auto st=onlineApi->state();
        if(!(st==onlineSeen)){
            onlineSeen=st;
            note(("ONLINE login task state "+std::to_string(st.taskState)+" step "+std::to_string(st.step)+" error "+std::to_string(st.error)+(st.signedIn()?" (signed in)":"")+"\n").c_str());}
        if(onlineAsk.tick(now,st))note(onlineApi->request()?"ONLINE sign-in asked\n":"ONLINE sign-in not asked: no login task\n");
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
        // Away: he reports an event or private room, or no pose of his has arrived (his game is at the title screen or
        // loading: without this a join was told at once, then "away" and "back" while he loaded).
        notices->call(now,connected,connected&&(cache.remote.state.flags&ds::Call));
        notices->link(now,connected,connected?core->presentationDelayMs():0);
        notices->status(now,connected,connected&&(cache.remote.state.flags&ds::Down),connected&&(cache.remote.state.flags&ds::Caught));
        notices->tick(now,poseAvailable&&!inEvent,connected,partnerName,connected&&((cache.remote.state.flags&ds::PrivateRoom)||!cache.remote.visible));
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
    // Upgrades (structures.h, LevelSync): event type 15, item = level, ref = the structure's ref.
    static constexpr uint8_t LevelWire=15;
    mod::structures::LevelSync levelSync;mod::roads::Levels levelApi;std::vector<ds::CargoEvent> levelIn;std::map<uint64_t,std::pair<uint32_t,uint64_t>> levelWants;bool levelTried=false,levelTell=false;
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
    void quietBodyInventory(uint64_t body,uint64_t now){
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
    // body_update_link=false leaves it out, to compare.
    uint64_t linkedBody=0,linkedPlayer=0;
    void linkBodyUpdate(uint64_t body,uint64_t player){
        if(!config.bodyUpdateLink||!sampler||!body||!player||(body==linkedBody&&player==linkedPlayer))return;
        linkedBody=body;linkedPlayer=player;
#ifdef _WIN32
        static const uint8_t want[16]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};uint8_t got[16]{};
        if(!sampler->read(sampler->base+0x23f4040,got,16)||std::memcmp(got,want,16)){note("BODY update link unavailable: the link function differs\n");return;}
        reinterpret_cast<void(*)(uint64_t,uint64_t,uint64_t,uint32_t)>(sampler->base+0x23f4040)(player+0x208,body,player,0);
        note("BODY joined to this porter's update group\n");
#endif
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
    void tickOrders(uint64_t now) {
        if(!core||!sampler||!config.shareOrders){orderIn.clear();return;}
        if(!orderTried){orderTried=true;try{orderRequest=mod::orders::nativeRequest(*sampler);}catch(const std::exception& e){note((std::string("ORDER unavailable: ")+e.what()+"\n").c_str());}}
        bool linked=core->connected()&&partnerMode();
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
                auto* u=unitSync.wanted(units,e.ref,n);
                std::ostringstream o;o<<"ORDER partner handed in unit "<<int(n)<<" of "<<std::hex<<e.ref<<std::dec;
                if(!u){o<<": not waiting here\n";note(o.str().c_str());continue;}
                unitSync.applied(e.ref,n);uint16_t seen=0;
#ifdef _WIN32
                if(sampler->read(u->stateAt,&seen,2)&&seen==mod::orders::NotDelivered){
                    // as the game's own hand-in leaves the row: the packed word and the cargo's two condition values, then the state
                    uint32_t packed=(uint32_t(e.position[0])&0xffff)|((e.item>>16)<<16);float l0=float(e.position[1]),l1=float(e.position[2]);
                    std::memcpy(reinterpret_cast<void*>(u->stateAt+4),&packed,4);std::memcpy(reinterpret_cast<void*>(u->stateAt+0x10),&l0,4);std::memcpy(reinterpret_cast<void*>(u->stateAt+0x14),&l1,4);
                    *reinterpret_cast<uint16_t*>(u->stateAt)=mod::orders::Delivered;
                }
#endif
                unsigned removed=0;uint64_t manager=0;
                if(cargoDelete&&cargoManagerSlot&&sampler->read(cargoManagerSlot,&manager,8)&&manager)
                    for(auto& r:mod::cargo::readRecords(*sampler,manager,cargoManagerVtable,mod::cargo::MaxRecords))
                        if(r.mission==e.ref&&r.unit==n&&mod::cargo::location(r)==mod::cargo::InWorld){cargoDelete(r.index);++removed;}
                o<<": marked delivered, "<<removed<<" cargo record(s) removed\n";note(o.str().c_str());continue;
            }
            mod::orders::Change c{e.ref,uint8_t(e.item)};if(c.what<1||c.what>3)continue;
            if(!orderSync.wanted(orders,c)){std::ostringstream o;o<<"ORDER partner's "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<" does not apply here\n";note(o.str().c_str());continue;}
            bool ok=orderRequest(c.id,c.what);if(ok){orderSync.applied(c);++orderApplied;}
            std::ostringstream o;o<<"ORDER partner: "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<(ok?" requested":" refused by the game")<<"\n";note(o.str().c_str());
        }
        if(!orderIn.empty()){orderIn.clear();return;}   // the game runs the requests on its next update: look again then
        if(config.shareOrderUnits){
            std::vector<uint64_t> under;for(auto& o:orders)if(mod::orders::shareable(o)&&o.state==mod::orders::Progress)under.push_back(o.id);
            for(auto& u:unitSync.observe(units,under)){
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
            std::ostringstream o;o<<"ORDER "<<names[c.what]<<" of "<<std::hex<<c.id<<std::dec<<" here -> partner\n";note(o.str().c_str());
        }
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
    // Shared BTs (bts.h), with share_enemies: gazers destroyed, a catcher fight started, its area cleared.
    mod::bts::GazerSync gazerSync;mod::bts::FightSync fightSync;mod::bts::HealthSync healthSync;std::function<void(const mod::bts::Catcher&,float)> btHurt;std::vector<ds::CargoEvent> btIn;
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
        // One health bar for the two games' catchers: first what this game's lost since the last look goes out, then
        // what the partner's lost is taken off here.
        auto catchers=fights.active>0?mod::bts::readCatchers(*sampler,sampler->base):std::vector<mod::bts::Catcher>{};
        for(auto& h:healthSync.observe(catchers)){
            if(!send)continue;
            ds::CargoEvent e;e.type=mod::bts::WireType;e.item=mod::bts::CatcherHurt;e.ref=h.ordinal;e.position={double(h.percent),0,0};core->queueCargo(e);++btSent;
            std::ostringstream o;o<<"BT catcher "<<h.ordinal<<" lost "<<h.percent<<" % here -> partner\n";note(o.str().c_str());
        }
        for(auto& e:btIn){
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
        for(auto w:fightSync.observe(fights)){
            if(!send||!here)continue;
            ds::CargoEvent e;e.type=mod::bts::WireType;e.item=w;e.position=at;core->queueCargo(e);++btSent;
            note(w==mod::bts::FightOn?"BT a catcher fight runs here -> partner\n":"BT a catcher fight ended here with the area cleared -> partner\n");
        }
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
    struct Pending {uint64_t receivedUs;uint64_t due;ds::Bytes b;size_t size;};std::vector<Pending> pending;mod::link::Profile linkProfile;uint64_t linkLost=0;uint32_t notedDelay=120;
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
                watchRing(clock);
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
                    auto fate=mod::link::fate(linkProfile,config.impairment,received,b[6]==uint8_t(ds::Kind::Presence));
                    if(fate.lost) {if(++linkLost%100==0){std::ostringstream o;o<<"LINK "<<config.link<<" lost "<<linkLost<<" of "<<received<<" received\n";note(o.str().c_str());}continue;}
                    uint64_t delay=fate.delayMs;
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
        config=mod::parseConfig(readConfig(path));config.link=mod::link::chosen(config.link,std::getenv("COOP_LINK"));mod::link::profile(config.link,linkProfile);
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
                              <<':'<<ds::portForSlot(config.slot)<<" peer "<<config.peer<<':'<<ds::portForSlot(config.peerSlot)<<"\n";
         if(config.link!="none")o<<"LINK stand-in "<<config.link<<": loss "<<linkProfile.lossPerMille/10.0<<" %, delay "<<linkProfile.delayMinMs<<".."<<linkProfile.delayMinMs+linkProfile.delaySpanMs-1<<" ms on every received packet\n";
         note(o.str().c_str());}
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
        if(gearChurnLeft&&gear){
            if(gearChurnMode==1){gear->flash(gearChurnLeft%2==0?false:true);--gearChurnLeft;}
            else if(now-gearChurnAt>=6000){gearChurnAt=now;gear->clear();--gearChurnLeft;}
            if(!gearChurnLeft)note(("GEAR test: churn done, culling guard at "+std::to_string(mod::guard::cullSkipped.load())+"\n").c_str());
        }
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
        // A partner who is down lies on his back where he fell (no locomotion underneath).
        {bool lay=target&&partnerMode()&&cache.remote.visible&&(cache.remote.state.flags&ds::Down)&&!cache.remote.state.vehicle;
            if(lay){target=mod::puppet::laidDown(*target);speed=0;actions&=mod::puppet::FlagCrouchBit;}
            if(lay!=partnerLaid){partnerLaid=lay;note(lay?"BODY laid down: the partner is down\n":"BODY upright again\n");}}
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
        repWatch=useOwn?ownEntity:0;
        quietBodyInventory(useOwn?ownEntity:0,now);
        if(useOwn&&ownEntity)linkBodyUpdate(ownEntity,anchor.entity);
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
            stage="enemies";stages.enter("enemies",mod::nowUs());
            if(twin&&gameContext)tickEnemies(now);
            stage="bts";stages.enter("bts",mod::nowUs());
            if(twin&&gameContext)tickBts(now);
            stage="roads";stages.enter("roads",mod::nowUs());
            if(twin&&gameContext)tickRoads(now);
            stage="orders";stages.enter("orders",mod::nowUs());
            if(twin&&gameContext)tickOrders(now);
            tickGuard();
            stage="markers";stages.enter("markers",mod::nowUs());
            if(twin&&gameContext)tickMarkers(now);
            stage="likes";stages.enter("likes",mod::nowUs());
            if(twin&&gameContext)tickLikes(now);
            stage="online";stages.enter("online",mod::nowUs());
            if(gameContext)tickOnline(now);
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
                else if(auto churn=mod::puppet::parseCargoCommand(text,mod::runStamp(),"gear");churn&&gear) {
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
                            if(!*up)o<<"STRUCT id "<<r.id<<" type "<<int(r.type)<<" state "<<int(r.state)<<" level "<<r.level<<" at "<<int(r.at.position[0])<<","<<int(r.at.position[1])<<"\n";
                            else if(r.id==*up){o<<"STRUCT test: upgrade of "<<r.id<<" from level "<<r.level<<": "<<api.upgrade(r.object,r.level+1)<<"\n";}
                        }
                        note(o.str().empty()?"STRUCT test: nothing\n":o.str().c_str());
                    }catch(const std::exception& e){note((std::string("STRUCT test unavailable: ")+e.what()+"\n").c_str());}
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
                                o<<"ROAD paver "<<p.id<<" level "<<p.level<<" at "<<int(at[0])<<","<<int(at[1])<<" own";for(auto v:p.own)o<<" "<<v;o<<" net";for(auto v:p.net)o<<" "<<v;o<<" needs";for(auto v:need)o<<" "<<v;o<<"\n";}
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
                else if(auto rain=mod::puppet::parseCargoCommand(text,mod::runStamp(),"rain");rain&&anchor.pose.available) {
                    // Test: "rain <seconds>" places the game's own timed rain around this porter (the influence the shared
                    // rain uses), so a host can have timefall on demand.
                    try{
                        auto api=mod::weather::nativeApi(*sampler);auto at=mod::puppet::transform(anchor.pose).position;
                        bool ok=api.influence(mod::weather::Mode::Rain,at,250.f,float(std::min<uint32_t>(*rain,900)));
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
            tickPad();notePose();
            repTickEnd(now);
            stage="idle";stages.enter("idle",mod::nowUs());
            if(unappliedReceive) {receiveApply.add(clock-unappliedReceive);unappliedReceive=0;}
            if(gameContext){auto line=stages.report(mod::nowUs());if(!line.empty())note(line.c_str());}
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
    auto fate=mod::link::fate(r.linkProfile,r.config.impairment,r.received,b[6]==uint8_t(ds::Kind::Presence));
    if(fate.lost){++r.linkLost;return;}
    uint64_t delay=fate.delayMs;
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
        std::ostringstream o;o<<"{\"handshake\":"<<(r.paired?"true":"false")<<",\"connected\":"<<(r.core->connected()?"true":"false")<<",\"sent\":"<<r.sent<<",\"received\":"<<r.received<<",\"dropped\":"<<r.dropped<<",\"link_lost\":"<<r.linkLost<<",\"accepted\":"<<r.core->accepted()<<",\"wrong_accepted\":"<<r.wrongAccepted<<",\"build_refused\":"<<r.buildRefused<<",\"version_refused\":"<<r.versionRefused<<",\"session_refused\":"<<r.sessionRefused<<",\"rtt_ms\":"<<r.measuredRtt<<",\"current_rtt_ms\":"<<r.core->rttMs()<<",\"samples\":"<<errors.size()<<",\"error_p50\":"<<p(.5)<<",\"error_p95\":"<<p(.95)<<",\"error_p99\":"<<p(.99)<<",\"error_max\":"<<p(1)<<",\"failed\":"<<(r.failed?"true":"false")<<",\"self_tick\":{\"enabled\":"<<(r.config.selfTick?"true":"false")<<",\"ticks\":"<<r.ticks<<"},\"pose\":{\"available\":"<<(r.poseAvailable?"true":"false")<<",\"reason\":\""<<r.poseReason<<"\"}";
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
        <<"\",\"frequency_hz\":"<<mod::clockFrequency()<<",\"clock_quantum_us\":"<<1000000./mod::clockFrequency()<<",\"observed_step_us\":"<<r.observedClockStep<<",\"wire_quantum_us\":1000,\"interpolation_ms\":"<<r.core->presentationDelayMs()<<",\"extrapolation_cap_ms\":150,\"bucket_upper_us\":[";
        for(size_t i=0;i<mod::Histogram::limits.size();++i)o<<(i?",":"")<<mod::Histogram::limits[i];
        o<<"],\"network_tick\":"<<r.networkTick.json()<<",\"provider_tick\":"<<r.providerTick.json()<<",\"presence_send\":"<<r.sendInterval.json()<<",\"sample_age\":"<<r.sampleAge.json()<<",\"receive_to_apply\":"<<r.receiveApply.json()<<",\"delivery_lateness\":"<<r.deliveryLateness.json()
        <<",\"ping_sent\":"<<r.pingSent<<",\"pong_received\":"<<r.pongReceived<<",\"first_ping_ms\":"<<(r.pingSent?int64_t(r.firstPing):-1)<<",\"last_ping_ms\":"<<(r.pingSent?int64_t(r.lastPing):-1)<<",\"first_pong_ms\":"<<(r.pongReceived?int64_t(r.firstPong):-1)<<",\"last_pong_ms\":"<<(r.pongReceived?int64_t(r.lastPong):-1)<<"}}";s=o.str();}
    if(out&&cap>=s.size()+1)memcpy(out,s.c_str(),s.size()+1);
    return unsigned(s.size()+1);
}
}
