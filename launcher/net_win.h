#pragma once
#include "vendor/coop_discover_win.h"
#include "core.h"
#include "../mod/identity.h"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <ctime>
#include <bcrypt.h>
namespace launcher {
inline std::string wireVersion() {std::string v;for(auto b:mod::WireBuild){v+="0123456789abcdef"[b>>4];v+="0123456789abcdef"[b&15];}return v;}
inline uint32_t random32() {uint32_t n=0;if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&n),sizeof(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)||!n)throw std::runtime_error("Windows random generator failed. Try again.");return n;}
inline std::string ipText(uint32_t ip) {char b[32];snprintf(b,sizeof(b),"%u.%u.%u.%u",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255);return b;}
inline sockaddr_in endpoint(uint32_t ip,unsigned port) {sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);a.sin_port=htons(uint16_t(port));return a;}
inline bool sameEndpoint(const sockaddr_in& a,const sockaddr_in& b) {return a.sin_addr.s_addr==b.sin_addr.s_addr&&a.sin_port==b.sin_port;}
struct ScanResult {cd_list hosts{};std::string status;};
class Network {
    SOCKET sock=INVALID_SOCKET;
    sockaddr_in partner{};
    cd_rate rate{};
    uint32_t lastPeer=0,lastSend=0,startAt=0,startSent=0;
    bool occupied=false,starting=false;
    HANDLE scanner=nullptr;
    volatile LONG stopScan=0;
    std::atomic<bool> scanDone{false};
    ScanResult scanned;
    void closeSocket(){if(sock!=INVALID_SOCKET){closesocket(sock);sock=INVALID_SOCKET;}}
    void send(Kind kind,bool value=false) {
        if(sock==INVALID_SOCKET)return;
        auto b=packet({kind,session.secret,nonce,session.port,value});
        sendto(sock,b.data(),int(b.size()),0,reinterpret_cast<sockaddr*>(&partner),sizeof(partner));
    }
public:
    bool hosting=false,lobby=false,joined=false,ready=false,peerReady=false,launch=false,inviteOnly=false,scanning=false;
    Session session;
    uint32_t nonce=0;
    std::string status="Choose Host, Join or Play solo.";
    cd_list hosts{};
    cd_recents recents{};
    std::string joinedName;
    ~Network(){InterlockedExchange(&stopScan,1);if(scanner){WaitForSingleObject(scanner,INFINITE);CloseHandle(scanner);}closeSocket();}
    void leave() {
        if(lobby&&occupied)send(Kind::leave);
        closeSocket();lobby=hosting=joined=ready=peerReady=launch=occupied=starting=false;startAt=0;
        status="Left the lobby.";
    }
    void openHost(Session s,bool privacy) {
        leave();s.host=true;s.secret=random32();config(s);session=s;nonce=random32();inviteOnly=privacy;
        sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);auto a=endpoint(0,s.port);
        if(sock==INVALID_SOCKET||bind(sock,reinterpret_cast<sockaddr*>(&a),sizeof(a))) {closeSocket();throw std::runtime_error("Cannot open the lobby port. Close another lobby or choose another port.");}
        u_long nb=1;ioctlsocket(sock,FIONBIO,&nb);hosting=lobby=true;rate={};status="Lobby open. Copy the invite or ask your friend to choose Join.";
    }
    std::string invite() const {
        ci_invite v{};v.secret=session.secret;v.port=uint16_t(session.port);v.flags=inviteOnly?CI_FLAG_INVITE_ONLY:0;
        cdw_adapter ad[128];int n=cdw_adapters(ad,128);
        bool vpn=false;for(int i=0;i<n;++i)vpn|=cd_tailscale_ip(ad[i].a.ip)!=0;
        // Reserve a route for Tailscale when virtual adapters would otherwise fill all four slots.
        for(int pass=0;pass<2;++pass)for(int i=0;i<n&&v.n<(pass==0&&vpn?3:4);++i) {
            auto ip=ad[i].a.ip;
            if(!mod::peerAddress(ipText(ip))||((ip>>24)==127)||bool(cd_tailscale_ip(ip))!=(pass==1))continue;
            bool duplicate=false;for(int k=0;k<v.n;++k)duplicate|=cd_ip(v.route[k].ip)==ip;
            if(duplicate)continue;
            auto& r=v.route[v.n++];r.kind=cd_tailscale_ip(ip)?CI_VPN:CI_LAN;
            for(int k=0;k<4;++k)r.ip[k]=uint8_t(ip>>(24-8*k));
        }
        if(!v.n)throw std::runtime_error("No LAN or Tailscale IPv4 address is available. Connect to a network and try again.");
        char code[CI_CODE_MAX];if(!ci_encode(&v,Prefix,code,sizeof(code)))throw std::runtime_error("Cannot create the invite.");return code;
    }
    void scan(bool lan,bool tailscale,unsigned port,const std::string& paste="") {
        if(scanning)return;
        if(scanner){WaitForSingleObject(scanner,INFINITE);CloseHandle(scanner);scanner=nullptr;}
        scanning=true;scanDone=false;InterlockedExchange(&stopScan,0);auto recent=recents;
        auto task=std::make_unique<std::function<void()>>([this,lan,tailscale,port,paste,recent]{
            ScanResult result;
            try {
                cd_candidates candidates{};
                if(lan){cdw_adapter w[128];cd_adapter a[128];int n=cdw_adapters(w,128);cdw_adapter_list(w,n,a);cd_add_adapters(&candidates,a,n,port);}
                result.status=lan?"LAN checked. ":"LAN off. ";
                if(tailscale){std::vector<char> json(4*1024*1024+1);cd_ts ts{};int n=cdw_tailscale_status(json.data(),json.size(),2000);
                    if(n>0&&cd_ts_parse(json.data(),n,&ts)){cd_add_tailscale(&candidates,&ts,port);result.status+="Tailscale checked. ";}
                    else result.status+="Tailscale unavailable (install or sign in). ";}
                else result.status+="Tailscale off. ";
                for(int i=0;i<recent.n;++i){auto ip=mod::peerAddress(recent.r[i].host);if(ip)cd_add_recent(&candidates,&recent.r[i],*ip);}
                if(!paste.empty()) {
                    cr_result r{};int kind=coop_recognise(paste.data(),paste.size(),Prefix,&r);
                    if(kind==CR_INVITE||kind==CR_LINK)cd_add_invite(&candidates,&r.invite,1);
                    else if(kind==CR_ADDRESS)cd_add(&candidates,cd_ip(r.ip),r.port?r.port:port,CD_RECENT,0,0,1);
                    else if(kind==CR_HOST||kind==CR_TAILSCALE) {
                        addrinfo hint{},*answer=nullptr;hint.ai_family=AF_INET;hint.ai_socktype=SOCK_DGRAM;
                        if(!getaddrinfo(r.host,nullptr,&hint,&answer)){for(auto p=answer;p;p=p->ai_next){auto ip=ntohl(reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr.s_addr);if(mod::peerAddress(ipText(ip)))cd_add(&candidates,ip,r.port?r.port:port,CD_RECENT,0,0,1);}freeaddrinfo(answer);}
                    }
                }
                struct Context {ScanResult* result;cd_candidates* candidates;std::string version;cd_he he{};int map[CD_MAX_CANDIDATES],picked=-1;};
                Context ctx{&result,&candidates,wireVersion(),{},{},-1};
                uint8_t routes[CD_HE_MAX];int attempts=0;
                for(int i=0;i<candidates.n;++i){ctx.map[i]=-1;if(candidates.c[i].group==1&&attempts<CD_HE_MAX){ctx.map[i]=attempts;routes[attempts++]=candidates.c[i].route;}}
                cd_he_start(&ctx.he,routes,attempts);
                auto callback=[](void* raw,int at,uint32_t ip,unsigned p,const cd_reply* r,uint32_t rtt){auto& c=*static_cast<Context*>(raw);auto& candidate=c.candidates->c[at];auto now=GetTickCount();
                    if(candidate.group==1&&c.map[at]>=0){
                        if(c.picked<0)c.picked=cd_he_choice(&c.he,now);
                        if(c.picked<0){cd_he_reply(&c.he,c.map[at],now);c.picked=cd_he_choice(&c.he,now);}
                        if(c.picked>=0&&c.map[at]!=c.picked)return;
                    }
                    cd_seen(&c.result->hosts,r,candidate.route,ip,p,candidate.group,int(rtt),now,c.version.c_str());};
                if(cdw_scan(&candidates,random32(),GameId,CD_WINDOW_MS,0,callback,&ctx,&stopScan)<0)result.status="Discovery socket unavailable. Check Windows Firewall and try again.";
                result.status+="Recent hosts checked. Steam friend discovery is not available in this build.";
            }catch(const std::exception& e){result.status=e.what();}
            scanned=result;scanDone=true;
        });
        scanner=CreateThread(nullptr,0,[](void* p)->DWORD{std::unique_ptr<std::function<void()>> work(static_cast<std::function<void()>*>(p));(*work)();return 0;},task.get(),0,nullptr);
        if(!scanner){scanning=false;throw std::runtime_error("Cannot start network discovery. Try again.");}task.release();
    }
    bool collectScan() {
        if(!scanning||!scanDone)return false;
        WaitForSingleObject(scanner,INFINITE);CloseHandle(scanner);scanner=nullptr;hosts=scanned.hosts;status=scanned.status;scanning=false;return true;
    }
    void joinHost(const cd_row& row,Session s,uint32_t secret) {
        if(row.mismatch||row.players>=row.limit||row.state!=CD_IN_LOBBY)throw std::runtime_error("This host cannot be joined. Choose an open lobby with the same version.");
        leave();s.host=false;s.peer=ipText(row.ip);s.port=row.port;s.secret=secret?secret:1;config(s);session=s;session.secret=secret;nonce=random32();partner=endpoint(row.ip,row.port);
        sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);auto a=endpoint(0,0);
        if(sock==INVALID_SOCKET||bind(sock,reinterpret_cast<sockaddr*>(&a),sizeof(a))){closeSocket();throw std::runtime_error("Cannot open the join socket. Try again.");}
        u_long nb=1;ioctlsocket(sock,FIONBIO,&nb);lobby=true;occupied=true;hosting=false;lastPeer=GetTickCount();lastSend=0;
        joinedName=row.name[0]?row.name:"friend";status="Joining "+joinedName+"...";
    }
    void setReady(){if(!joined||hosting)return;ready=!ready;send(Kind::ready,ready);}
    void start(){if(!hosting||!occupied||!peerReady)throw std::runtime_error("Wait for your friend to join and choose Ready.");starting=true;startSent=GetTickCount();send(Kind::start);lastSend=GetTickCount();status="Starting with your friend...";}
    void tick() {
        if(sock==INVALID_SOCKET)return;
        auto now=GetTickCount();
        for(int count=0;count<32;++count) {
            char bytes[256];sockaddr_in from{};int size=sizeof(from);int n=recvfrom(sock,bytes,sizeof(bytes),0,reinterpret_cast<sockaddr*>(&from),&size);
            if(n<0)break;
            auto ip=ntohl(from.sin_addr.s_addr);
            if(!mod::peerAddress(ipText(ip)))continue;
            if(hosting&&!cd_rate_allow(&rate,ip,now))continue;
            if(hosting) {
                cd_host h{GameId,nullptr,session.name.c_str(),CD_IN_LOBBY,inviteOnly,0,occupied?2u:1u,2};auto version=wireVersion();h.version=version.c_str();uint8_t reply[CD_REPLY_MAX];
                int len=cd_answer(reinterpret_cast<uint8_t*>(bytes),size_t(n),&h,reply);
                if(len){sendto(sock,reinterpret_cast<char*>(reply),len,0,reinterpret_cast<sockaddr*>(&from),size);continue;}
            }
            Packet p;if(!parsePacket(std::string(bytes,n),p)||p.port!=session.port)continue;
            if(hosting) {
                if(p.kind==Kind::join&&allowJoin(p,session.secret,inviteOnly,occupied)) {
                    partner=from;nonce=p.nonce;session.peer=ipText(ip);occupied=joined=true;lastPeer=now;
                    status="Your friend joined. Wait for Ready, then Start game.";send(Kind::accepted);continue;
                }
                if(!occupied||!sameEndpoint(from,partner)||p.nonce!=nonce)continue;
                if(p.kind==Kind::join&&(p.secret==session.secret||(!inviteOnly&&!p.secret))){send(Kind::accepted);lastPeer=now;continue;}
                if(p.secret!=session.secret)continue;
                if(p.kind==Kind::ready){peerReady=p.ready;lastPeer=now;send(Kind::accepted);status=peerReady?"Your friend is ready. Start game.":"Waiting for your friend to choose Ready.";}
                if(p.kind==Kind::started&&starting){launch=true;closeSocket();return;}
                if(p.kind==Kind::leave){occupied=joined=peerReady=false;status="Your friend left. Lobby is open.";}
            } else {
                if(!sameEndpoint(from,partner)||p.nonce!=nonce||!p.secret||(session.secret&&p.secret!=session.secret))continue;
                if(p.kind==Kind::accepted){session.secret=p.secret;joined=true;lastPeer=now;status="Joined "+joinedName+". Choose Ready when you are ready.";}
                if(p.kind==Kind::start&&joined&&ready){send(Kind::started);if(!startAt)startAt=now;lastPeer=now;status="Host is starting the game...";}
                if(p.kind==Kind::leave){leave();status="The host closed the lobby.";return;}
            }
        }
        if(!hosting&&startAt&&now-startAt>=1000){launch=true;closeSocket();return;}
        if(occupied&&now-lastPeer>15000){if(hosting){occupied=joined=peerReady=false;starting=false;status="Lost your friend. They can join again.";}else{leave();status="Lost the host. Choose Join and try again.";}return;}
        if(starting&&now-startSent>10000){starting=false;status="Your friend did not acknowledge Start. Try again.";}
        if(now-lastSend>=500) {
            if(hosting&&starting)send(Kind::start);
            if(!hosting){if(startAt)send(Kind::started);else send(joined?Kind::ready:Kind::join,ready);}
            lastSend=now;
        }
    }
    void launched(){launch=false;status="Launch requested. Keep this window open; return to Play to open a new lobby after the game exits.";}
};
}
