// coop-swarm: many stand-in players for one relay (world/relay.h), to put a crowd around a game copy or to load a
// server without any game. Each player has a socket of its own, says HELLO to the relay, walks a circle and runs the
// real protocol core (one session for every player the relay says is near), with the mod's wire identity like
// coop-partner, so a game on the same relay cannot tell them from real copies.
//
//   coop-swarm --relay HOST:PORT --count K --centre X Y Z --radius R
//              [--key TEXT] [--session N] [--speed M/S] [--area N] [--first-id N] [--only ID] [--role auto|host|client]
//              [--seconds N] [--stop FILE] [--require-connected]
//
//   --relay    the server's relay address (numeric IPv4) and UDP port (coop-world --relay-port)
//   --count    how many players (1..1000)
//   --centre   the middle of the circle, in the core's axes: metres, x, y up, z
//   --radius   the circle's radius in metres (0: everyone stands at the centre); the players start evenly spread
//   --key      the relay's key (coop-world --relay-key, or its --prefix); default none
//   --session  the core session number every pair shares (default 1); a real game copy must use the same
//   --speed    walking speed in metres a second (default 1.6; above 3.7 they sprint)
//   --area     the area number reported to the relay (default 0)
//   --first-id the players' ids are N, N+1, ... (default 1001); nicknames are "Sam N"
//   --only     talk only to this player id (a game copy): the stand-ins start no sessions with each other, which keeps
//              a large crowd cheap. Without it every pair that is near starts a session: K*(K-1) of them in one place.
//   --role     the core role taken towards each player: auto (default: the lower id is the host), or always host / client
//   --seconds  stop after this long (default one hour); --stop FILE: stop when the file exists
//   --require-connected  exit 1 unless, at the end, every player is known to the relay and every session is connected
//
// Prints one SWARM line a second: players the relay answered, players near, sessions, sessions connected, presence
// samples accepted, and the core's last refusal if there was one.
#include "partner.h"
#include "../core/relay.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket=SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int;
#endif
using namespace mod::partner;
namespace {
uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Player;
// One core session with one player who is near: the core reads the stand-in's own state and sends through the relay.
struct Session final:ds::StateProvider,ds::Transport {
    Player& owner;uint64_t peer;std::unique_ptr<ds::Core> core;bool seen=false;
    Session(Player& o,uint64_t p):owner(o),peer(p){}
    bool readLocal(ds::State& s) override;
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render& r) override {seen=r.visible;}
    void send(const ds::Bytes& b) override;
    void sendLong(const std::vector<uint8_t>& b) override;
};
struct Player {
    uint64_t id=0;Socket socket{};std::unique_ptr<ds::relay::Client> relay;ds::State state{};
    std::map<uint64_t,std::unique_ptr<Session>> sessions;
};
bool Session::readLocal(ds::State& s){s=owner.state;return true;}
void Session::send(const ds::Bytes& b){auto w=b;toWire(w);owner.relay->sendTo(peer,w.data(),w.size());}
void Session::sendLong(const std::vector<uint8_t>& b){auto w=b;toWireLong(w);owner.relay->sendTo(peer,w.data(),w.size());}
[[noreturn]] void usage(const char* why){std::fprintf(stderr,"coop-swarm: %s\n(see the comment at the top of swarm.cpp)\n",why);std::exit(2);}
}
int main(int argc,char** argv){
    std::string relayText,key,stopFile,role="auto";unsigned count=0;std::array<double,3> centre{};bool haveCentre=false,requireConnected=false;
    double radius=-1,speed=Walk,seconds=3600;uint64_t session=1,firstId=1001,only=0;uint32_t area=0;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc)usage(("missing value after "+a).c_str());return std::string(argv[++i]);};
        try{
            if(a=="--relay")relayText=next();else if(a=="--count")count=unsigned(std::stoul(next()));
            else if(a=="--centre"||a=="--center"){for(auto& x:centre)x=std::stod(next());haveCentre=true;}
            else if(a=="--radius")radius=std::stod(next());else if(a=="--key")key=next();else if(a=="--session")session=std::stoull(next());
            else if(a=="--speed")speed=std::stod(next());else if(a=="--area")area=uint32_t(std::stoul(next()));
            else if(a=="--first-id")firstId=std::stoull(next());else if(a=="--only")only=std::stoull(next());
            else if(a=="--role")role=next();else if(a=="--seconds")seconds=std::stod(next());else if(a=="--stop")stopFile=next();
            else if(a=="--require-connected")requireConnected=true;
            else usage(("unknown option "+a).c_str());
        }catch(const std::exception&){usage(("not a number after "+a).c_str());}
    }
    auto colon=relayText.rfind(':');
    if(colon==std::string::npos)usage("--relay HOST:PORT is required");
    sockaddr_in relayAddress{};relayAddress.sin_family=AF_INET;
    int port=std::atoi(relayText.c_str()+colon+1);
    if(port<1||port>65535||inet_pton(AF_INET,relayText.substr(0,colon).c_str(),&relayAddress.sin_addr)!=1)usage("--relay needs a numeric IPv4 address and a port");
    relayAddress.sin_port=htons(uint16_t(port));
    if(count<1||count>1000)usage("--count 1..1000 is required");
    if(!haveCentre||!ds::relay::positionValid(centre))usage("--centre X Y Z is required (metres, within 100 km)");
    if(!(radius>=0&&radius<=10000))usage("--radius 0..10000 metres is required");
    if(!(speed>=0&&speed<=30))usage("--speed 0..30 metres a second");
    if(!session||!firstId||firstId+count<firstId)usage("--session and --first-id must not be 0");
    if(!ds::relay::tokenText(key))usage("--key: at most 32 letters, digits, - and _");
    if(role!="auto"&&role!="host"&&role!="client")usage("--role is auto, host or client");
    if(!ds::relay::positionValid({centre[0]+radius,centre[1],centre[2]+radius})||!ds::relay::positionValid({centre[0]-radius,centre[1],centre[2]-radius}))usage("the circle leaves the map (100 km)");
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))usage("winsock unavailable");
#endif
    std::vector<std::unique_ptr<Player>> players;
    for(unsigned i=0;i<count;++i){
        auto p=std::make_unique<Player>();p->id=firstId+i;
        // A connected socket: the system picks the local address of the route to the relay and hands over only what
        // the relay itself sends.
        p->socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(connect(p->socket,reinterpret_cast<const sockaddr*>(&relayAddress),sizeof relayAddress)!=0)usage("cannot open a socket to the relay");
#ifdef _WIN32
        u_long nb=1;ioctlsocket(p->socket,FIONBIO,&nb);
#else
        fcntl(p->socket,F_SETFL,fcntl(p->socket,F_GETFL)|O_NONBLOCK);
#endif
        ds::relay::ClientConfig c;c.id=p->id;c.token=key;c.nickname="Sam "+std::to_string(p->id);
        auto s=p->socket;
        p->relay=std::make_unique<ds::relay::Client>(c,[s](const uint8_t* d,size_t n){send(s,reinterpret_cast<const char*>(d),int(n),0);});
        players.push_back(std::move(p));
    }
    if(!only&&count>30)std::printf("SWARM note: %u players near each other without --only start %u sessions\n",count,count*(count-1));
    std::printf("SWARM %u players (ids %llu..%llu) -> relay %s, session %llu, circle of %.1f m around %.1f,%.1f,%.1f\n",count,
        (unsigned long long)firstId,(unsigned long long)(firstId+count-1),relayText.c_str(),(unsigned long long)session,radius,centre[0],centre[1],centre[2]);
    std::fflush(stdout);
    auto start=nowMs();uint64_t nextStatus=start+1000,nonces=0,accepted=0,strangers=0,otherIdentity=0;ds::Error lastError=ds::Error::None;
    double turn=radius>0?speed/radius:0;   // radians a second
    unsigned online=0,nearby=0,sessions=0,connected=0;
    auto census=[&]{
        online=nearby=sessions=connected=0;accepted=0;
        for(auto& p:players){online+=p->relay->online();nearby+=unsigned(p->relay->nearby().size());
            for(auto& s:p->sessions){++sessions;connected+=s.second->core->connected();accepted+=s.second->core->accepted();}}};
    while(nowMs()-start<seconds*1000){
        if(!stopFile.empty()&&std::ifstream(stopFile))break;
        auto now=nowMs();double t=double(now-start)/1000;
        for(unsigned i=0;i<count;++i){
            auto& p=*players[i];
            // Around the circle: angle 0 is +z, the walk goes towards +x first, and the player faces the way he walks.
            double angle=2*Pi*i/count+turn*t,sn=std::sin(angle),cs=std::cos(angle);
            ds::State s;s.position={centre[0]+radius*sn,centre[1],centre[2]+radius*cs};
            if(turn>0){s.velocity={speed*cs,0,-speed*sn};s.yaw=std::atan2(cs,-sn);if(speed>Run+.1)s.flags|=ds::Sprint;}
            else s.yaw=std::remainder(angle+Pi,2*Pi);   // standing: facing the centre
            p.state=s;p.relay->setPosition(s.position,area);
            for(;;){
                uint8_t raw[2048];auto n=recv(p.socket,reinterpret_cast<char*>(raw),sizeof raw,0);
                if(n<=0)break;
                uint64_t from=0;const uint8_t* payload=nullptr;size_t size=0;
                if(!p.relay->receive(raw,size_t(n),now,from,payload,size))continue;
                auto it=p.sessions.find(from);if(it==p.sessions.end()){++strangers;continue;}
                if(size!=ds::WireSize)continue;   // like coop-partner: the longer frames (animation, enemies) are not read
                uint8_t frame[ds::WireSize];std::memcpy(frame,payload,size);
                if(!fromWire(frame,size)){++otherIdentity;continue;}
                auto error=it->second->core->receive(frame,size,now);if(error!=ds::Error::None)lastError=error;
            }
            p.relay->tick(now);
            for(auto& e:p.relay->takeEvents()){
                if(!e.enter){
                    if(auto it=p.sessions.find(e.id);it!=p.sessions.end()){p.sessions.erase(it);}
                    continue;}
                if(only&&e.id!=only)continue;
                auto s2=std::make_unique<Session>(p,e.id);
                ds::Config c;c.session=session;c.nonce=((now&0xffffffffffull)<<20)|(++nonces&0xfffff)|1;
                c.role=role=="host"?ds::Role::Host:role=="client"?ds::Role::Client:p.id<e.id?ds::Role::Host:ds::Role::Client;
                s2->core=std::make_unique<ds::Core>(c,*s2,*s2);
                p.sessions[e.id]=std::move(s2);
            }
            for(auto& s2:p.sessions){s2.second->core->frame(now);s2.second->core->tick(now);}
        }
        if(now>=nextStatus){
            nextStatus+=1000;census();
            std::printf("SWARM t=%.0f online=%u/%u near=%u sessions=%u connected=%u accepted=%llu",t,online,count,nearby,sessions,connected,(unsigned long long)accepted);
            if(strangers||otherIdentity)std::printf(" not_in_roster=%llu other_identity=%llu",(unsigned long long)strangers,(unsigned long long)otherIdentity);
            if(lastError!=ds::Error::None)std::printf(" last_refusal=\"%s\"",ds::reason(lastError));
            for(auto& p:players)if(p->relay->refusal()!=ds::relay::Refusal::None){std::printf(" relay_refused=%s",p->relay->refusal()==ds::relay::Refusal::Full?"full":"id-in-use");break;}
            std::printf("\n");std::fflush(stdout);lastError=ds::Error::None;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    census();
    for(auto& p:players){auto now=nowMs();for(auto& s:p->sessions)s.second->core->disconnect(now);p->relay->bye();}
    std::printf("SWARM done online=%u/%u near=%u sessions=%u connected=%u accepted=%llu\n",online,count,nearby,sessions,connected,(unsigned long long)accepted);
    if(requireConnected&&(online!=count||connected!=sessions))return 1;
    return 0;
}
