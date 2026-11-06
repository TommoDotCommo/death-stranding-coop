// coop-partner: a stand-in partner for one game copy (partner.h). It binds the partner slot's loopback port and runs the
// real protocol core, so the game cannot tell it from a second copy.
//   coop-partner --slot 1 --peer-slot 0 --role client --session N [--follow | --replay FILE [--where-recorded]]
//                [--drop SECONDS ITEM] [--take-after SECONDS] [--take-own-after SECONDS] [--build SECONDS TYPE]
//                [--seconds N] [--stop FILE]
// --where-recorded plays the walk where it was recorded (same save, same place) instead of beside the porter.
// Prints one status line a second and every cargo event the game sends. --drop places one cargo item (the game's
// item hash) where the partner stands, SECONDS after connecting. --take-after picks up (sends "taken" for) every item the
// game places, SECONDS after it arrives; --take-own-after picks up the partner's own dropped item SECONDS after dropping.
// --build builds one structure (a Player type of EDSConstructionPointType, e.g. 25 ladder) where the partner stands.
#include "partner.h"
#include "structures.h"
#include <chrono>
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
    Follower follower;std::optional<Replay> replay;std::optional<ds::State> porter,mine;
    uint64_t started=0,last=0;double replayLoop=0;bool stickyTeleport=false;std::array<double,3> replayOrigin{};bool replayPlaced=false,whereRecorded=false;
    bool readLocal(ds::State& s) override {
        if(!mine)return false;
        s=*mine;if(stickyTeleport)s.flags|=ds::Teleport;
        return true;
    }
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render& r) override {if(r.visible)porter=r.state;}
    void send(const ds::Bytes& b) override {
        if(b[60]&ds::Teleport)stickyTeleport=false;   // the jump has been sent
        auto w=b;toWire(w);
        sendto(socket,reinterpret_cast<const char*>(w.data()),int(w.size()),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
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
            auto s=follower.step(porter,dt);
            if(s){if(s->flags&ds::Teleport)stickyTeleport=true;s->flags&=uint16_t(~ds::Teleport);mine=s;}
        }
    }
};
[[noreturn]] void usage(const char* why){std::fprintf(stderr,"coop-partner: %s\n(see the comment at the top of partner.cpp)\n",why);std::exit(2);}
}
int main(int argc,char** argv){
    unsigned slot=1,peerSlot=0;ds::Role role=ds::Role::Client;uint64_t session=0;double seconds=3600,dropAfter=-1,takeAfter=-1,takeOwnAfter=-1,buildAfter=-1;uint32_t dropItem=0,buildType=0;
    std::string replayFile,stopFile;Shell shell;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc)usage(("missing value after "+a).c_str());return std::string(argv[++i]);};
        if(a=="--slot")slot=unsigned(std::stoul(next()));else if(a=="--peer-slot")peerSlot=unsigned(std::stoul(next()));
        else if(a=="--role"){auto r=next();if(r=="host")role=ds::Role::Host;else if(r=="client")role=ds::Role::Client;else usage("role is host or client");}
        else if(a=="--session")session=std::stoull(next());else if(a=="--seconds")seconds=std::stod(next());
        else if(a=="--follow")replayFile.clear();else if(a=="--where-recorded")shell.whereRecorded=true;else if(a=="--replay")replayFile=next();else if(a=="--stop")stopFile=next();
        else if(a=="--drop"){dropAfter=std::stod(next());dropItem=uint32_t(std::stoul(next()));}
        else if(a=="--build"){buildAfter=std::stod(next());buildType=uint32_t(std::stoul(next()));if(buildType>255)usage("structure type is 0..255");}
        else if(a=="--take-after")takeAfter=std::stod(next());else if(a=="--take-own-after")takeOwnAfter=std::stod(next());
        else usage(("unknown option "+a).c_str());
    }
    if(!session)usage("--session is required (the game copy's mod.json session)");
    if(!replayFile.empty()){
        std::ifstream f(replayFile,std::ios::binary);if(!f)usage("cannot read the replay file");
        std::stringstream ss;ss<<f.rdbuf();Replay r(parseRecording(ss.str()));
        if(!r.usable()||r.span()<1000)usage("the replay file has no usable walk (need presence rows over at least a second)");
        shell.replay=std::move(r);
    }
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
    ds::Config config;config.role=role;config.session=session;config.nonce=(nowMs()<<8)|slot|1;
    ds::Core core(config,shell,shell);
    auto start=nowMs();uint64_t nextStatus=start+1000,connectedAt=0,refused=0,ownRef=0,droppedAt=0;bool dropped=false,built=false;
    std::vector<std::pair<uint64_t,uint64_t>> toTake;   // (when, ref)
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
            core.receive(raw,size_t(n),now);
        }
        core.frame(now);shell.step(now);core.tick(now);
        if(core.connected()&&!connectedAt)connectedAt=now;
        if(!core.connected())connectedAt=0;
        for(auto& e:core.takeCargo()){
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
            e.id=(nowMs()<<4)|slot|2;e.ref=(nowMs()<<4)|slot|6;core.queueCargo(e);built=true;
            std::printf("STRUCT built type %u ref %llu at %.2f,%.2f,%.2f\n",buildType,(unsigned long long)e.ref,e.position[0],e.position[1],e.position[2]);
        }
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
