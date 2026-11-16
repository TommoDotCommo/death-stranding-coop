// coop-world: the players' own server for the game's shared world (README.md). One process, one world file.
//   coop-world --port 8089 [--bind 127.0.0.1] [--data world.json] [--prefix SECRET] [--log events.log] [--trace calls.log]
//              [--idle-exit SECONDS]
//              [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS]
//               [--relay-players N] [--relay-rate N]]
// Plain HTTP/1.1 with keep-alive, one thread per connection, bounded sizes. It only answers; it never connects out.
// With --relay-port it is also the relay of the real-time layer (relay.h): one UDP socket on the same address, one thread.
#include "front.h"
#include "relay.h"
#include <algorithm>
#include <random>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t=SOCKET;
static void closeSocket(sock_t s){closesocket(s);}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t=int;
static void closeSocket(sock_t s){close(s);}
#endif
namespace {
constexpr size_t MaxHeader=16*1024,MaxBody=4*1024*1024;constexpr int MaxConnections=32,IdleSeconds=60;
std::mutex outLock;std::atomic<int> connections{0};std::atomic<int64_t> lastRequest{0};
std::string dataPath,logPath,tracePath;
void line(const std::string& text){
    std::lock_guard<std::mutex> g(outLock);std::cout<<text<<std::endl;
    if(!logPath.empty()){std::ofstream f(logPath,std::ios::app);f<<text<<'\n';}}
int64_t nowSeconds(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void save(world::World& w){
    if(dataPath.empty()||!w.dirty())return;
    static std::mutex m;std::lock_guard<std::mutex> g(m);
    auto text=w.snapshot().dump();auto tmp=dataPath+".tmp";
    {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f<<text;if(!f){line("save failed: "+tmp);return;}}
    std::remove(dataPath.c_str());
    if(std::rename(tmp.c_str(),dataPath.c_str())!=0){line("save failed: "+dataPath);return;}
    w.saved();}
bool sendAll(sock_t s,const std::string& data){
    size_t at=0;while(at<data.size()){auto n=send(s,data.data()+at,int(data.size()-at),0);if(n<=0)return false;at+=size_t(n);}return true;}
const char* reason(int status){return status==200?"OK":status==400?"Bad Request":status==401?"Unauthorized":status==403?"Forbidden":status==404?"Not Found":"Error";}
void serve(sock_t s,world::Front& front,world::World& w){
    std::string buffer;char chunk[8192];
    while(true){
        size_t headEnd;
        while((headEnd=buffer.find("\r\n\r\n"))==std::string::npos){
            if(buffer.size()>MaxHeader)return;
            auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)return;buffer.append(chunk,size_t(n));}
        world::Request r;std::istringstream head(buffer.substr(0,headEnd));std::string version,row;
        head>>r.method>>r.target>>version;std::getline(head,row);
        size_t length=0;bool close=false;
        while(std::getline(head,row)){
            if(!row.empty()&&row.back()=='\r')row.pop_back();
            auto colon=row.find(':');if(colon==std::string::npos)continue;
            std::string name=row.substr(0,colon),value=row.substr(colon+1);
            for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            while(!value.empty()&&value.front()==' ')value.erase(0,1);
            r.headers[name]=value;
            if(name=="content-length"){try{length=std::stoul(value);}catch(const std::exception&){return;}}
            if(name=="connection"&&(value=="close"||value=="Close"))close=true;}
        if(r.method.empty()||r.target.empty()||length>MaxBody||r.headers.count("transfer-encoding"))return;
        buffer.erase(0,headEnd+4);
        while(buffer.size()<length){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)return;buffer.append(chunk,size_t(n));}
        r.body=buffer.substr(0,length);buffer.erase(0,length);
        world::Reply reply;
        lastRequest=nowSeconds();
        try{reply=front.handle(r,nowSeconds());}
        catch(const std::exception& e){reply.status=500;reply.body="{}";line(std::string("error: ")+e.what());}
        save(w);
        if(!tracePath.empty()){
            // Research aid: every call with its body. The sign-in ticket is left out; bodies can name other accounts.
            auto target=r.target;if(auto code=target.find("code=");code!=std::string::npos)target.resize(code+5);
            std::lock_guard<std::mutex> g(outLock);std::ofstream f(tracePath,std::ios::app);
            f<<nowSeconds()<<' '<<r.method<<' '<<target<<' '<<r.body.substr(0,4000)<<" -> "<<reply.status<<' '<<reply.body.substr(0,600)<<'\n';}
        std::string out="HTTP/1.1 "+std::to_string(reply.status)+" "+reason(reply.status)+"\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(reply.body.size())+"\r\n"+(close?"Connection: close\r\n":"")+"\r\n"+reply.body;
        if(!sendAll(s,out)||close)return;}
}
uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
// The relay's socket: datagrams in and out, the clock, and nothing else. All of the relay's state lives on this thread.
void serveRelay(sock_t s,world::RelayConfig config){
    world::Relay relay(std::move(config),[s](const world::RelayAddress& to,const uint8_t* data,size_t size){
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(to.port);a.sin_addr.s_addr=htonl(to.ip);
        sendto(s,reinterpret_cast<const char*>(data),int(size),0,reinterpret_cast<const sockaddr*>(&a),sizeof a);});
    relay.log=[](const std::string& text){line(text);};
    std::vector<uint8_t> buffer(ds::relay::MaxDatagram+1);uint64_t nextTick=0;   // one byte more: a longer datagram is seen as too long
    while(true){
        sockaddr_in from{};socklen_t size=sizeof from;
        auto n=recvfrom(s,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),0,reinterpret_cast<sockaddr*>(&from),&size);
        auto now=nowMs();
        if(n>0&&from.sin_family==AF_INET)relay.receive({ntohl(from.sin_addr.s_addr),ntohs(from.sin_port)},buffer.data(),size_t(n),now);
        else if(n<0)std::this_thread::sleep_for(std::chrono::milliseconds(1));   // the receive timeout, or an error: never spin
        if(now>=nextTick){relay.tick(now);nextTick=now+250;if(relay.players())lastRequest=nowSeconds();}}
}
}
int main(int argc,char** argv){
    std::string bind="127.0.0.1",prefix,relayKey;int port=0,relayPort=0;long idleExit=0;bool haveRelayKey=false;world::RelayConfig relayConfig;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--port")port=std::atoi(next().c_str());else if(a=="--bind")bind=next();else if(a=="--data")dataPath=next();
        else if(a=="--prefix")prefix=next();else if(a=="--log")logPath=next();else if(a=="--trace")tracePath=next();else if(a=="--idle-exit")idleExit=std::atol(next().c_str());
        else if(a=="--relay-port")relayPort=std::atoi(next().c_str());else if(a=="--relay-key"){relayKey=next();haveRelayKey=true;}
        else if(a=="--relay-enter")relayConfig.enterMetres=std::atof(next().c_str());else if(a=="--relay-leave")relayConfig.leaveMetres=std::atof(next().c_str());
        else if(a=="--relay-timeout")relayConfig.timeoutMs=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),100000L)*1000);
        else if(a=="--relay-players")relayConfig.maxPlayers=size_t(std::max(std::atol(next().c_str()),0L));
        else if(a=="--relay-rate")relayConfig.ratePerPeer=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else{std::cerr<<"usage: coop-world --port N [--bind ADDRESS] [--data FILE] [--prefix SECRET] [--log FILE] [--trace FILE] [--idle-exit SECONDS]\n"
                        "                  [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS] [--relay-players N] [--relay-rate N]]\n";return 2;}}
    if(port<1||port>65535){std::cerr<<"--port 1..65535 required\n";return 2;}
    for(char c:prefix)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_'){std::cerr<<"--prefix: letters, digits, - and _ only\n";return 2;}
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){std::cerr<<"winsock unavailable\n";return 1;}
#endif
    world::World w;w.log=[](const std::string& s){line(s);};
    if(!dataPath.empty()){
        std::ifstream f(dataPath,std::ios::binary);
        if(f){std::stringstream text;text<<f.rdbuf();
            try{w.restore(json::parse(text.str()));line("world loaded: "+std::to_string(w.objects())+" objects");}
            catch(const std::exception& e){std::cerr<<"cannot read "<<dataPath<<": "<<e.what()<<" (left untouched; move it away to start a new world)\n";return 1;}}}
    world::Front front(w,prefix);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));
    if(inet_pton(AF_INET,bind.c_str(),&address.sin_addr)!=1){std::cerr<<"--bind needs a numeric IPv4 address\n";return 2;}
    if(relayPort){
        // The relay listens on the same address as the rest, on its own UDP port. Its key is the group's secret: the
        // prefix unless another is given, and never empty beyond loopback.
        if(relayPort<1||relayPort>65535){std::cerr<<"--relay-port 1..65535\n";return 2;}
        relayConfig.token=haveRelayKey?relayKey:prefix;
        if(relayConfig.token.empty()&&(ntohl(address.sin_addr.s_addr)>>24)!=127){std::cerr<<"the relay needs --relay-key (or --prefix) when it listens beyond loopback\n";return 2;}
        relayConfig.seed=std::random_device{}();
        try{world::Relay check(relayConfig,[](const world::RelayAddress&,const uint8_t*,size_t){});}
        catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
        sockaddr_in relayAddress=address;relayAddress.sin_port=htons(uint16_t(relayPort));
        sock_t relaySocket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
#ifdef _WIN32
        DWORD wait=250;setsockopt(relaySocket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&wait),sizeof wait);
#else
        timeval wait{0,250000};setsockopt(relaySocket,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof wait);
#endif
        if(::bind(relaySocket,reinterpret_cast<sockaddr*>(&relayAddress),sizeof relayAddress)!=0){std::cerr<<"cannot listen on "<<bind<<":"<<relayPort<<" (UDP)\n";return 1;}
        std::thread([relaySocket,relayConfig]{serveRelay(relaySocket,relayConfig);}).detach();
    }
    sock_t listener=socket(AF_INET,SOCK_STREAM,0);int yes=1;
#ifndef _WIN32
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof yes);
#endif
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)!=0||listen(listener,16)!=0){std::cerr<<"cannot listen on "<<bind<<":"<<port<<"\n";return 1;}
    if(idleExit>0){
        // Started by a launcher for one session: leave when no game has asked anything for this long.
        lastRequest=nowSeconds();
        std::thread([idleExit,&w]{while(true){std::this_thread::sleep_for(std::chrono::seconds(5));if(nowSeconds()-lastRequest.load()>=idleExit){save(w);line("idle: leaving");std::_Exit(0);}}}).detach();}
    line("coop-world listening on "+bind+":"+std::to_string(port)+(prefix.empty()?"":" (secret prefix set)"));
    if(relayPort)line("coop-world relay on "+bind+":"+std::to_string(relayPort)+" (UDP; near from "+std::to_string(int(relayConfig.enterMetres))+" m to "+std::to_string(int(relayConfig.leaveMetres))+" m, "+std::to_string(relayConfig.maxPlayers)+" players)");
    while(true){
        sock_t s=accept(listener,nullptr,nullptr);
#ifdef _WIN32
        if(s==INVALID_SOCKET)continue;
        DWORD timeout=IdleSeconds*1000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof timeout);
#else
        if(s<0)continue;
        timeval timeout{IdleSeconds,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
#endif
        setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof yes);
        if(connections.load()>=MaxConnections){closeSocket(s);continue;}
        ++connections;
        std::thread([s,&front,&w]{serve(s,front,w);closeSocket(s);--connections;}).detach();}
}
