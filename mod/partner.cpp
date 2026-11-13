// coop-partner: a stand-in partner for one game copy (partner.h). It binds the partner slot's loopback port and runs the
// real protocol core, so the game cannot tell it from a second copy.
//   coop-partner --slot 1 --peer-slot 0 --role client --session N [--follow | --replay FILE [--where-recorded] [--with-animation]]
//   --with-animation: also replays the animation samples recorded in FILE (mod option record_animation)
//                [--drop SECONDS ITEM] [--take-after SECONDS] [--take-own-after SECONDS] [--build SECONDS TYPE] [--unbuild-after SECONDS]
//                [--carry SPEC] [--carry-drop-after SECONDS] [--seconds N] [--stop FILE]
// --offset X Y Z shifts the follow target (adapter metres, Y up); --episode N|unknown and --progress-id N|unknown
// set explicit handshake fixtures, --progress-mismatch warn|refuse chooses policy.
// --environment-time HOURS requires --role host and sends a fixed [0,24) time sample every 30 s; --weather VALUE adds a
// rain condition to it (the game's map condition, lower is wetter: -1.8 heavy rain, 1.5 clear).
// --where-recorded plays the walk where it was recorded (same save, same place) instead of beside the porter.
// Prints one status line a second and every cargo event the game sends. --drop places one cargo item (the game's
// item hash) where the partner stands, SECONDS after connecting. --take-after picks up (sends "taken" for) every item the
// game places, SECONDS after it arrives; --take-own-after picks up the partner's own dropped item SECONDS after dropping.
// --build builds one structure (a Player type of EDSConstructionPointType, e.g. 25 ladder) where the partner stands;
// --unbuild-after dismantles it SECONDS later.
// --vehicle KIND ON OFF: from ON to OFF seconds after connecting the partner rides vehicle KIND (1 motorbike, 2 truck): its
// presence carries the vehicle block (origin at the stand-in's feet, level, facing its way). --seat F R U (metres,
// vehicle frame: forward, right, up) reports the stand-in at that seat above the vehicle origin, as a real rider is
// (a truck driver measured live: 0.8 0.44 left, 1.95 up -> --seat 0.8 -0.44 1.95).
// --flag BITS ON OFF: the partner also sets these presence flag bits (ds::Flag, e.g. 2 = crouch) from ON to OFF seconds
// after connecting: for looking at how the body shows a state.
// --down ON OFF, --caught ON OFF: from ON to OFF seconds after connecting the partner reports that he is down / caught
// by BTs (presence flags Down, Caught).
// --away ON OFF: from ON to OFF seconds after connecting the partner reports a game event or private room (presence
// flag PrivateRoom), as a real copy does in a cutscene or its private room.
// --carry tells the game what the partner carries (carry_visual.h; the game needs carry_visual true): SPEC is
// "hash[:slot[:count]],..." (slot 1 BackPack, 6 LeftHand, 7 RightHand; default 1 and 1), sent once connected and again
// after every reconnect; --carry-drop-after puts it all down SECONDS after connecting. What the game's porter carries
// (with carry_visual on there) is printed as CARRY lines.
#include "partner.h"
#include "structures.h"
#include "carry_visual.h"
#include "environment.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket=SOCKET;
static bool wouldBlock(){auto e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAECONNRESET;}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int;
static bool wouldBlock(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==ECONNREFUSED;}
#endif
using namespace mod::partner;
namespace {
uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Shell final:ds::StateProvider,ds::Transport {
    Socket socket{};sockaddr_in peer{};
    ds::Progress progress{};std::array<double,3> offset{};
    ds::Progress readProgress() const override {return progress;}
    Follower follower;std::optional<Replay> replay;std::optional<ds::State> porter,mine;
    uint64_t started=0,last=0;double replayLoop=0;bool stickyTeleport=false;std::array<double,3> replayOrigin{};bool replayPlaced=false,whereRecorded=false;
    uint8_t rideKind=0;bool riding=false,away=false,down=false,caught=false,extra=false;uint16_t extraFlags=0;std::array<double,3> seat{};
    std::vector<std::pair<double,ds::Animation>> anim;long animAt=-1;double animLoop=-1;   // --with-animation
    bool readLocal(ds::State& s) override {
        if(!mine)return false;
        s=*mine;if(stickyTeleport)s.flags|=ds::Teleport;
        if(away)s.flags|=ds::PrivateRoom;
        if(down)s.flags|=ds::Down;
        if(caught)s.flags|=ds::Caught;
        if(extra)s.flags|=extraFlags;
        if(riding&&rideKind){
            // Game forward (sin yaw, cos yaw), right (cos yaw, -sin yaw), z up; adapter axes are (x, up, game y).
            double sy=std::sin(s.yaw),cy=std::cos(s.yaw),gx=seat[0]*sy+seat[1]*cy,gy=seat[0]*cy-seat[1]*sy;
            s.position[0]+=gx;s.position[1]+=seat[2];s.position[2]+=gy;
            s.vehicle=rideKind;s.flags|=ds::Vehicle;s.vehicleOffset={-gx,-seat[2],-gy};s.vehicleAngles={s.yaw,0,0};}
        return true;
    }
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render& r) override {if(r.visible)porter=r.state;}
    void send(const ds::Bytes& b) override {
        if(b[60]&ds::Teleport)stickyTeleport=false;   // the jump has been sent
        auto w=b;toWire(w);
        sendto(socket,reinterpret_cast<const char*>(w.data()),int(w.size()),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
    }
    void sendLong(const std::vector<uint8_t>& b) override {
        auto w=b;toWireLong(w);
        sendto(socket,reinterpret_cast<const char*>(w.data()),int(w.size()),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
    }
    // The recorded animation sample of this moment of the replay, each sent once (a resent one would repeat its pulses).
    void animate(ds::Core& core,uint64_t now){
        if(anim.empty()||!replay||!replayPlaced||!core.connected())return;
        double span=replay->span(),ms=double(now-started),loop=std::floor(ms/span),t=ms-loop*span;
        if(loop!=animLoop){animLoop=loop;animAt=-1;}
        long i=animAt;while(i+1<long(anim.size())&&anim[size_t(i+1)].first<=t)++i;
        if(i>animAt&&core.sendAnimation(anim[size_t(i)].second,now))animAt=i;
    }
    void step(uint64_t now){
        double dt=last?(now-last)/1000.:0;last=now;
        if(replay){
            if(!replayPlaced&&(porter||whereRecorded)){replayOrigin=whereRecorded?replay->first():beside(*porter);
                replayPlaced=true;started=now;stickyTeleport=true;}
            if(!replayPlaced)return;
            double ms=double(now-started),loop=std::floor(ms/replay->span());
            if(loop!=replayLoop){replayLoop=loop;stickyTeleport=true;}
            mine=replay->at(ms,replayOrigin);
        } else {
            auto s=follower.step(offsetTarget(porter,offset),dt);
            if(s){if(s->flags&ds::Teleport)stickyTeleport=true;s->flags&=uint16_t(~ds::Teleport);mine=s;}
        }
    }
};
struct CarryPrint final:mod::carry::Sink {
    void attach(const mod::carry::Summary& s) override {std::printf("CARRY the porter carries %s\n",mod::carry::describe(s).c_str());std::fflush(stdout);}
    void detach() override {std::printf("CARRY the porter carries nothing\n");std::fflush(stdout);}
    void hold(uint32_t,uint8_t) override {}
};
[[noreturn]] void usage(const char* why){std::fprintf(stderr,"coop-partner: %s\n(see the comment at the top of partner.cpp)\n",why);std::exit(2);}
}
int main(int argc,char** argv){
    unsigned slot=1,peerSlot=0;ds::Role role=ds::Role::Client;uint64_t session=0;double seconds=3600,dropAfter=-1,takeAfter=-1,takeOwnAfter=-1,buildAfter=-1,unbuildAfter=-1;uint32_t dropItem=0,buildType=0;
    std::string replayFile,stopFile;bool withAnimation=false;Shell shell;ds::ProgressPolicy progressPolicy=ds::ProgressPolicy::Warn;std::optional<mod::carry::Summary> carrySpec;double carryDropAfter=-1;
    std::optional<ds::Environment> fixedEnvironment;double rideOn=-1,rideOff=-1,awayOn=-1,awayOff=-1,downOn=-1,downOff=-1,caughtOn=-1,caughtOff=-1,flagOn=-1,flagOff=-1;std::optional<float> fixedRain;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc)usage(("missing value after "+a).c_str());return std::string(argv[++i]);};
        if(a=="--slot")slot=unsigned(std::stoul(next()));else if(a=="--peer-slot")peerSlot=unsigned(std::stoul(next()));
        else if(a=="--role"){auto r=next();if(r=="host")role=ds::Role::Host;else if(r=="client"||r=="guest")role=ds::Role::Client;else usage("role is host, guest or client");}
        else if(a=="--session")session=std::stoull(next());else if(a=="--seconds")seconds=std::stod(next());
        else if(a=="--follow")replayFile.clear();else if(a=="--where-recorded")shell.whereRecorded=true;else if(a=="--replay")replayFile=next();else if(a=="--with-animation")withAnimation=true;else if(a=="--stop")stopFile=next();
        else if(a=="--offset"){for(auto& x:shell.offset)x=std::stod(next());offsetTarget({},shell.offset);}
        else if(a=="--episode")shell.progress.episode=progressValue(next());
        else if(a=="--progress-id")shell.progress.id=progressValue(next());
        else if(a=="--environment-time") {
            auto value=next();
            try {fixedEnvironment=ds::Environment{mod::environment::fixedTime(value)};}
            catch(const std::exception&){usage("--environment-time requires finite decimal hours [0,24)");}
        }
        else if(a=="--progress-mismatch"){auto p=next();if(p=="warn")progressPolicy=ds::ProgressPolicy::Warn;else if(p=="refuse")progressPolicy=ds::ProgressPolicy::Refuse;else usage("progress-mismatch is warn or refuse");}
        else if(a=="--drop"){dropAfter=std::stod(next());dropItem=uint32_t(std::stoul(next()));}
        else if(a=="--build"){buildAfter=std::stod(next());buildType=uint32_t(std::stoul(next()));if(buildType>255)usage("structure type is 0..255");}
        else if(a=="--unbuild-after")unbuildAfter=std::stod(next());
        else if(a=="--carry"){carrySpec=mod::carry::parseSpec(next());if(!carrySpec)usage("--carry takes hash[:slot[:count]],... (cargo slots 1..9) or none");}
        else if(a=="--carry-drop-after")carryDropAfter=std::stod(next());
        else if(a=="--weather"){auto v=std::stod(next());if(!std::isfinite(v)||std::abs(v)>ds::EnvironmentRainMax)usage("--weather is the map condition -2.5..2.5 (lower is wetter)");fixedRain=float(v);}
        else if(a=="--seat"){for(auto& x:shell.seat)x=std::stod(next());}
        else if(a=="--away"){awayOn=std::stod(next());awayOff=std::stod(next());if(!(awayOn>=0&&awayOff>awayOn))usage("away needs ON < OFF seconds");}
        else if(a=="--down"){downOn=std::stod(next());downOff=std::stod(next());if(!(downOn>=0&&downOff>downOn))usage("down needs ON < OFF seconds");}
        else if(a=="--flag"){auto bits=std::stoul(next());if(!bits||(bits&~unsigned(ds::FlagMask))||(bits&(ds::Vehicle|ds::Teleport)))usage("flag takes presence flag bits (not vehicle or teleport)");
            shell.extraFlags=uint16_t(bits);flagOn=std::stod(next());flagOff=std::stod(next());if(!(flagOn>=0&&flagOff>flagOn))usage("flag needs ON < OFF seconds");}
        else if(a=="--caught"){caughtOn=std::stod(next());caughtOff=std::stod(next());if(!(caughtOn>=0&&caughtOff>caughtOn))usage("caught needs ON < OFF seconds");}
        else if(a=="--vehicle"){auto k=std::stoul(next());if(k<1||k>ds::VehicleKindMax)usage("vehicle kind is 1..3");shell.rideKind=uint8_t(k);rideOn=std::stod(next());rideOff=std::stod(next());}
        else if(a=="--take-after")takeAfter=std::stod(next());else if(a=="--take-own-after")takeOwnAfter=std::stod(next());
        else usage(("unknown option "+a).c_str());
    }
    if(!session)usage("--session is required (the game copy's mod.json session)");
    if(fixedEnvironment&&role!=ds::Role::Host)usage("--environment-time requires --role host");
    if(fixedRain&&!fixedEnvironment)usage("--weather requires --environment-time");
    if(fixedRain){fixedEnvironment->haveRain=true;fixedEnvironment->rain=*fixedRain;}
    if(!replayFile.empty()){
        std::ifstream f(replayFile,std::ios::binary);if(!f)usage("cannot read the replay file");
        std::stringstream ss;ss<<f.rdbuf();Replay r(parseRecording(ss.str()));
        if(!r.usable()||r.span()<1000)usage("the replay file has no usable walk (need presence rows over at least a second)");
        shell.replay=std::move(r);
        if(withAnimation){shell.anim=parseAnimations(ss.str());if(shell.anim.empty())usage("--with-animation: the replay file has no animation rows (record_animation)");
            std::printf("PARTNER replays %zu animation samples\n",shell.anim.size());}
    }
    else if(withAnimation)usage("--with-animation needs --replay");
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))usage("winsock unavailable");
#endif
    shell.socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(ds::portForSlot(slot));local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(shell.socket,reinterpret_cast<sockaddr*>(&local),sizeof(local)))usage("the partner slot's port is busy (is that game copy still running?)");
    shell.peer.sin_family=AF_INET;shell.peer.sin_port=htons(ds::portForSlot(peerSlot));shell.peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
#ifdef _WIN32
    u_long nb=1;ioctlsocket(shell.socket,FIONBIO,&nb);
#else
    fcntl(shell.socket,F_SETFL,fcntl(shell.socket,F_GETFL)|O_NONBLOCK);
#endif
    ds::Config config;config.role=role;config.session=session;config.nonce=(nowMs()<<8)|slot|1;config.progressPolicy=progressPolicy;
    ds::Core core(config,shell,shell);
    auto start=nowMs();uint64_t nextStatus=start+1000,connectedAt=0,refused=0,ownRef=0,droppedAt=0,builtRef=0,builtAt=0;bool dropped=false,built=false;std::array<double,3> builtAtPos{};
    uint64_t handshakeSerial=0;bool progressRefused=false;std::optional<uint32_t> observedEnvironment;
    std::vector<std::pair<uint64_t,uint64_t>> toTake;   // (when, ref)
    mod::carry::Exchange carry;CarryPrint carryPrint;uint64_t carryTickAt=0;bool carryDropped=false;
    std::printf("PARTNER slot %u -> %u session %llu mode %s\n",slot,peerSlot,(unsigned long long)session,shell.replay?"replay":"follow");std::fflush(stdout);
    while(nowMs()-start<seconds*1000){
        if(!stopFile.empty()&&std::ifstream(stopFile))break;
        auto now=nowMs();
        for(;;){
            uint8_t raw[2048];sockaddr_in from{};socklen_t size=sizeof(from);
            auto n=recvfrom(shell.socket,reinterpret_cast<char*>(raw),sizeof(raw),0,reinterpret_cast<sockaddr*>(&from),&size);
            if(n<0){if(wouldBlock())break;break;}
            if(n!=int(ds::WireSize)||from.sin_addr.s_addr!=shell.peer.sin_addr.s_addr||from.sin_port!=shell.peer.sin_port)continue;
            if(!fromWire(raw,size_t(n))){++refused;continue;}
            auto error=core.receive(raw,size_t(n),now);
            if(error==ds::Error::Progress&&!progressRefused){progressRefused=true;std::puts("HANDSHAKE refused: story progress mismatch");}
        }
        core.frame(now);shell.step(now);core.tick(now);shell.animate(core,now);
        if(fixedEnvironment&&core.sendEnvironment(*fixedEnvironment,now)) {
            if(fixedEnvironment->haveRain)std::printf("ENVIRONMENT sent fixed hour=%.6f rain=%.3f\n",fixedEnvironment->timeOfDayMs/3600000.,fixedEnvironment->rain);
            else std::printf("ENVIRONMENT sent fixed hour=%.6f weather=unknown\n",fixedEnvironment->timeOfDayMs/3600000.);
            std::fflush(stdout);
        }
        if(auto value=core.latestEnvironment(now)) {
            if(!observedEnvironment||*observedEnvironment!=value->timeOfDayMs) {
                observedEnvironment=value->timeOfDayMs;
                std::printf("ENVIRONMENT received hour=%.6f weather=unknown\n",value->timeOfDayMs/3600000.);std::fflush(stdout);
            }
        }
        if(core.connected()&&handshakeSerial!=core.connectionSerial()){
            handshakeSerial=core.connectionSerial();progressRefused=false;
            auto text=[](uint32_t n){return n==ds::UnknownProgress?std::string("unknown"):std::to_string(n);};
            std::printf("HANDSHAKE progress=%s local_episode=%s peer_episode=%s local_progress=%s peer_progress=%s\n",ds::progressReason(core.progressStatus()),
                text(core.localProgress().episode).c_str(),text(core.peerProgress().episode).c_str(),text(core.localProgress().id).c_str(),text(core.peerProgress().id).c_str());
        }
        if(core.connected()&&!connectedAt)connectedAt=now;
        if(!core.connected())connectedAt=0;
        if(awayOn>=0){bool on=connectedAt&&now-connectedAt>=awayOn*1000&&now-connectedAt<awayOff*1000;
            if(on!=shell.away){shell.away=on;std::printf("AWAY the partner %s\n",on?"is in a private room":"is back");std::fflush(stdout);}}
        if(downOn>=0){bool on=connectedAt&&now-connectedAt>=downOn*1000&&now-connectedAt<downOff*1000;
            if(on!=shell.down){shell.down=on;std::printf("DOWN the partner %s\n",on?"is down":"is up");std::fflush(stdout);}}
        if(flagOn>=0){bool on=connectedAt&&now-connectedAt>=flagOn*1000&&now-connectedAt<flagOff*1000;
            if(on!=shell.extra){shell.extra=on;std::printf("FLAG the partner %s presence flags %u\n",on?"sets":"clears",unsigned(shell.extraFlags));std::fflush(stdout);}}
        if(caughtOn>=0){bool on=connectedAt&&now-connectedAt>=caughtOn*1000&&now-connectedAt<caughtOff*1000;
            if(on!=shell.caught){shell.caught=on;std::printf("CAUGHT the partner %s\n",on?"is caught":"is free");std::fflush(stdout);}}
        if(shell.rideKind){bool on=connectedAt&&now-connectedAt>=rideOn*1000&&now-connectedAt<rideOff*1000;
            if(on!=shell.riding){shell.riding=on;std::printf("RIDE the partner %s vehicle kind %d\n",on?"rides":"gets off",int(shell.rideKind));std::fflush(stdout);}}
        for(auto& e:core.takeCargo()){
            if(carry.incoming(e,carryPrint))continue;   // what the game's porter carries (type 6)
            if(e.type==3){auto b=mod::structures::unpack(e.item,e.position,e.yaw);
                std::printf("STRUCT received type %u ref %llu at %.2f,%.2f,%.2f yaw %.2f pitch %.2f roll %.2f\n",unsigned(b.type),(unsigned long long)e.ref,
                            e.position[0],e.position[1],e.position[2],b.yaw,b.pitch,b.roll);continue;}
            std::printf("CARGO received type %u item %u ref %llu at %.2f,%.2f,%.2f id %llu\n",unsigned(e.type),e.item,(unsigned long long)e.ref,
                        e.position[0],e.position[1],e.position[2],(unsigned long long)e.id);
            if(e.type==1&&takeAfter>=0&&e.ref)toTake.push_back({now+uint64_t(takeAfter*1000),e.ref});
        }
        auto take=[&](uint64_t ref){
            ds::CargoEvent e;e.type=2;e.ref=ref;if(shell.mine){e.position=shell.mine->position;e.yaw=shell.mine->yaw;}e.id=(nowMs()<<4)|slot|8;
            core.queueCargo(e);std::printf("CARGO took ref %llu\n",(unsigned long long)ref);};
        for(auto it=toTake.begin();it!=toTake.end();)if(now>=it->first){take(it->second);it=toTake.erase(it);}else ++it;
        if(dropAfter>=0&&!dropped&&connectedAt&&shell.mine&&now-connectedAt>=dropAfter*1000){
            ds::CargoEvent e;e.type=1;e.item=dropItem;e.position=shell.mine->position;e.yaw=shell.mine->yaw;e.id=(nowMs()<<4)|slot;
            ownRef=(nowMs()<<4)|slot|4;e.ref=ownRef;droppedAt=now;
            core.queueCargo(e);dropped=true;
            std::printf("CARGO dropped item %u ref %llu at %.2f,%.2f,%.2f\n",dropItem,(unsigned long long)ownRef,e.position[0],e.position[1],e.position[2]);
        }
        if(dropped&&ownRef&&takeOwnAfter>=0&&now-droppedAt>=takeOwnAfter*1000){take(ownRef);ownRef=0;}
        if(buildAfter>=0&&!built&&connectedAt&&shell.mine&&now-connectedAt>=buildAfter*1000){
            mod::structures::Built b;b.type=uint8_t(buildType);
            ds::CargoEvent e;e.type=3;e.item=mod::structures::pack(b);e.position=shell.mine->position;e.yaw=shell.mine->yaw;
            e.id=(nowMs()<<4)|slot|2;e.ref=(nowMs()<<4)|slot|6;core.queueCargo(e);built=true;builtRef=e.ref;builtAt=now;builtAtPos=e.position;
            std::printf("STRUCT built type %u ref %llu at %.2f,%.2f,%.2f\n",buildType,(unsigned long long)e.ref,e.position[0],e.position[1],e.position[2]);
        }
        if(builtRef&&unbuildAfter>=0&&now-builtAt>=unbuildAfter*1000){
            ds::CargoEvent e;e.type=4;e.ref=builtRef;e.item=buildType;e.position=builtAtPos;e.id=(nowMs()<<4)|slot|10;core.queueCargo(e);
            std::printf("STRUCT dismantled ref %llu\n",(unsigned long long)builtRef);builtRef=0;
        }
        if(carrySpec&&now>=carryTickAt){
            carryTickAt=now+250;
            if(carryDropAfter>=0&&!carryDropped&&connectedAt&&now-connectedAt>=carryDropAfter*1000){
                carryDropped=true;carrySpec=mod::carry::Summary{};std::printf("CARRY put everything down\n");}
            for(auto& e:carry.tick(carrySpec,core.connected(),now,carryPrint)){
                if(e.ref%16==0)std::printf("CARRY sent %s\n",mod::carry::describe(*carrySpec).c_str());
                core.queueCargo(e);}   // ids from the core: unique per event (entries of one summary share a millisecond)
        } else if(!carrySpec)for(auto& e:carry.tick(std::nullopt,core.connected(),now,carryPrint))core.queueCargo(e);
        if(now>=nextStatus){
            nextStatus+=1000;
            if(shell.porter&&shell.mine){
                auto& p=shell.porter->position;auto& m=shell.mine->position;
                std::printf("STATUS connected=%d porter=%.1f,%.1f,%.1f partner=%.1f,%.1f,%.1f distance=%.1f speed=%.1f rtt=%.0f cargo_pending=%zu\n",
                    core.connected(),p[0],p[1],p[2],m[0],m[1],m[2],std::hypot(p[0]-m[0],p[2]-m[2]),
                    std::hypot(shell.mine->velocity[0],shell.mine->velocity[2]),core.rttMs(),core.cargoPending());
            } else std::printf("STATUS connected=%d waiting for the porter (refused %llu frames of another identity)\n",core.connected(),(unsigned long long)refused);
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    core.disconnect(nowMs());
    std::printf("PARTNER stopped\n");
    return 0;
}
