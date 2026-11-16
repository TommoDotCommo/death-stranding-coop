// coop-world: the players' own server for the game's shared world (README.md). One process, one world file.
//   coop-world --port 8089 [--bind 127.0.0.1] [--data world.json] [--prefix SECRET] [--log events.log] [--trace calls.log]
//              [--idle-exit SECONDS]
//              [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS]
//               [--relay-players N] [--relay-rate N] [--broadcast-full METRES] [--broadcast-interval MS]
//               [--broadcast-animation METRES] [--broadcast-budget BYTES] [--broadcast-flush MS] [--broadcast-rate N]
//               [--broadcast-build GAME:HEX]]
//              [--link NAME --link-key KEY [--link-url http://ADDRESS:PORT[/PREFIX]] [--link-export FILTER] [--link-import FILTER]
//               [--link-every SECONDS]]...
//   coop-world export  --data world.json --out FILE [--filter FILTER]
//   coop-world import  --data world.json --in FILE [--filter FILTER] [--prune]
//   coop-world forget  --data world.json --server ID
//   coop-world origins --data world.json
// Plain HTTP/1.1 with keep-alive, one thread per connection, bounded sizes. It only answers, except for the linked
// servers it was given an address for (--link-url): those it asks for objects, and nothing else is ever connected to.
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
#include <sys/select.h>
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
        // A linked server asking is not a game asking: two servers that ask each other would never be idle.
        if(r.target.find("/link/pull")==std::string::npos)lastRequest=nowSeconds();
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
void setTimeouts(sock_t s,int seconds){
#ifdef _WIN32
    DWORD t=DWORD(seconds)*1000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&t),sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&t),sizeof t);
#else
    timeval t{seconds,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);
#endif
}
// One request to a linked server's /link/pull, on a connection of its own. Only a 200 with exactly the body its
// Content-Length names, within the size a request body may have here, is an answer.
bool linkPost(const world::LinkEndpoint& e,const std::string& key,const std::string& body,std::string& out){
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(e.port));
    if(inet_pton(AF_INET,e.host.c_str(),&a.sin_addr)!=1)return false;
    sock_t s=socket(AF_INET,SOCK_STREAM,0);
#ifdef _WIN32
    if(s==INVALID_SOCKET)return false;
#else
    if(s<0)return false;
#endif
    setTimeouts(s,10);
    std::string in;
    if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof a)==0&&
       sendAll(s,"POST "+(e.prefix.empty()?std::string():"/"+e.prefix)+"/link/pull HTTP/1.1\r\nHost: "+e.host+":"+std::to_string(e.port)+"\r\nAuthorization: Link "+key+
                 "\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body)){
        char chunk[8192];
        while(in.size()<=MaxHeader+MaxBody){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)break;in.append(chunk,size_t(n));}}
    closeSocket(s);
    size_t headEnd=in.find("\r\n\r\n");
    if(headEnd==std::string::npos||headEnd>MaxHeader||in.compare(0,13,"HTTP/1.1 200 ")!=0)return false;
    std::istringstream head(in.substr(0,headEnd));std::string row;size_t length=0;bool have=false;
    std::getline(head,row);
    while(std::getline(head,row)){
        if(!row.empty()&&row.back()=='\r')row.pop_back();
        auto colon=row.find(':');if(colon==std::string::npos)continue;
        std::string name=row.substr(0,colon),value=row.substr(colon+1);
        for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        while(!value.empty()&&value.front()==' ')value.erase(0,1);
        if(name=="content-length"){
            if(have||value.empty()||value.size()>9)return false;
            for(char c:value)if(c<'0'||c>'9')return false;
            length=std::stoul(value);have=true;}}
    if(!have||length>MaxBody||in.size()-headEnd-4!=length)return false;
    out=in.substr(headEnd+4);return true;
}
// One linked server this one asks: a round every few seconds, a log line when the state of the link changes or
// objects came, went or changed.
void serveLink(world::World& w,world::LinkConfig c,world::LinkEndpoint e){
    using Result=world::Puller::Result;
    world::Puller puller(w,c,[e,key=c.key](const std::string& ask,std::string& answer){return linkPost(e,key,ask,answer);});
    bool first=true,wasGood=false;Result last=Result::Same;
    while(true){
        Result r=puller.poll(nowSeconds());auto& n=puller.last();
        bool good=r==Result::Same||r==Result::Synced,moved=r==Result::Synced&&(n.added||n.updated||n.removed);
        if(first||good!=wasGood||(!good&&r!=last)||moved){
            std::string text="link "+c.name+": "+world::Puller::text(r);
            if(good)text+=" with server "+puller.peer();
            if(r==Result::Synced)text+=" (added "+std::to_string(n.added)+", changed "+std::to_string(n.updated)+", removed "+std::to_string(n.removed)+", "+std::to_string(n.unchanged)+" as before)";
            if(r==Result::Refused)text+=" ("+puller.why()+")";
            line(text);}
        first=false;wasGood=good;last=r;
        save(w);
        std::this_thread::sleep_for(std::chrono::seconds(c.everySeconds));}
}
// 0: loaded (or there is no file yet and that is allowed), 1: refused.
int loadWorld(world::World& w,bool mustExist){
    std::ifstream f(dataPath,std::ios::binary);
    if(!f){
        if(mustExist){std::cerr<<"cannot read "<<dataPath<<"\n";return 1;}
        w.setServerId(w.serverId());return 0;}   // a new world: its id is saved with the first write
    std::stringstream text;text<<f.rdbuf();
    try{w.restore(json::parseFile(text.str(),world::MaxWorldFileBytes));}
    catch(const std::exception& e){std::cerr<<"cannot read "<<dataPath<<": "<<e.what()<<" (left untouched; move it away to start a new world)\n";return 1;}
    return 0;
}
const char* OfflineUsage="usage: coop-world export  --data FILE --out FILE [--filter FILTER]\n"
                         "       coop-world import  --data FILE --in FILE [--filter FILTER] [--prune]\n"
                         "       coop-world forget  --data FILE --server ID\n"
                         "       coop-world origins --data FILE\n"
                         "FILTER: all, none, or terms separated by ';': types=l,rope,e  owners=NAME,ID  areas=226  max-age=30d  likes=no  imported=yes\n";
// The commands that work on a world file while no server has it open (README, "Linked servers").
int offline(int argc,char** argv){
    std::string command=argv[1],in,out,filterText="all",server;bool prune=false;
    for(int i=2;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--data")dataPath=next();else if(a=="--in")in=next();else if(a=="--out")out=next();else if(a=="--filter")filterText=next();
        else if(a=="--server")server=next();else if(a=="--prune")prune=true;
        else{std::cerr<<OfflineUsage;return 2;}}
    bool known=command=="export"||command=="import"||command=="forget"||command=="origins";
    if(!known||dataPath.empty()||(command=="export")!=!out.empty()||(command=="import")!=!in.empty()||(command=="forget")!=!server.empty()||(prune&&command!="import")){std::cerr<<OfflineUsage;return 2;}
    world::Filter filter;
    try{filter=world::parseFilter(filterText);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
    if(command=="forget"&&!world::serverIdText(server)){std::cerr<<"--server: a server id (coop-world origins lists them)\n";return 2;}
    world::World w;
    if(loadWorld(w,command!="import"))return 1;
    save(w);   // a world file from before linked servers gets its id now, so every export of it names the same server
    if(command=="export"){
        auto document=w.exportObjects(filter,"",nowSeconds());
        std::ofstream f(out,std::ios::binary|std::ios::trunc);f<<document.dump();f.close();
        if(!f){std::cerr<<"cannot write "<<out<<"\n";return 1;}
        std::cout<<"exported "<<document.get("objects").items().size()<<" objects of server "<<w.serverId()<<"\n";return 0;}
    if(command=="import"){
        std::ifstream f(in,std::ios::binary);if(!f){std::cerr<<"cannot read "<<in<<"\n";return 1;}
        std::stringstream text;text<<f.rdbuf();
        try{
            auto document=json::parseFile(text.str(),world::MaxWorldFileBytes);
            auto r=w.importObjects(document,filter,prune,nowSeconds());save(w);
            std::cout<<"imported from server "<<document.get("server").string()<<": added "<<r.added<<", changed "<<r.updated<<", removed "<<r.removed<<", as before "<<r.unchanged
                     <<", not passing the filter "<<r.filtered<<", built here "<<r.own<<"\n";return 0;
        }catch(const std::exception& e){std::cerr<<"refused, nothing changed: "<<e.what()<<"\n";return 1;}}
    if(command=="forget"){auto n=w.forget(server);save(w);std::cout<<"forgot "<<n<<" objects of server "<<server<<"\n";return 0;}
    std::cout<<"this server "<<w.serverId()<<": "<<w.objects()-w.imported()<<" objects built here, "<<w.imported()<<" imported\n";
    for(auto& o:w.origins())std::cout<<"from server "<<o.server<<(o.via==o.server?std::string():" by way of "+o.via)<<": "<<o.objects<<" objects\n";
    return 0;
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
        // Wait for a datagram, at most until the next FRAME of the broadcast mode is due (and a quarter second).
        auto now=nowMs();uint64_t due=relay.flushDue(),waitMs=std::min<uint64_t>(250,due>now?due-now:0);
        fd_set readable;FD_ZERO(&readable);FD_SET(s,&readable);timeval wait{0,long(waitMs*1000)};
        int ready=select(int(s)+1,&readable,nullptr,nullptr,&wait);
        if(ready>0){
            sockaddr_in from{};socklen_t size=sizeof from;
            auto n=recvfrom(s,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),0,reinterpret_cast<sockaddr*>(&from),&size);
            now=nowMs();
            if(n>0&&from.sin_family==AF_INET)relay.receive({ntohl(from.sin_addr.s_addr),ntohs(from.sin_port)},buffer.data(),size_t(n),now);
            else if(n<0)std::this_thread::sleep_for(std::chrono::milliseconds(1));   // an error: never spin
        }else if(ready<0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        now=nowMs();
        if(now>=relay.flushDue())relay.flush(now);
        if(now>=nextTick){relay.tick(now);nextTick=now+250;if(relay.players())lastRequest=nowSeconds();}}
}
// --broadcast-build GAME:HEX32, the one build broadcast players may have.
bool parseBuild(const std::string& text,world::RelayConfig& c){
    auto colon=text.find(':');if(colon==std::string::npos||text.size()-colon-1!=32)return false;
    char* end=nullptr;unsigned long game=std::strtoul(text.c_str(),&end,10);if(end!=text.c_str()+colon||!game||game>0xffffffffUL)return false;
    for(size_t i=0;i<16;++i){
        auto digit=[](char ch)->int{return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:ch>='A'&&ch<='F'?ch-'A'+10:-1;};
        int hi=digit(text[colon+1+2*i]),lo=digit(text[colon+2+2*i]);if(hi<0||lo<0)return false;c.build[i]=uint8_t(hi*16+lo);}
    c.game=uint32_t(game);c.pinBuild=true;return true;}
}
int main(int argc,char** argv){
    if(argc>1&&argv[1][0]!='-')return offline(argc,argv);
    std::string bind="127.0.0.1",prefix,relayKey;int port=0,relayPort=0;long idleExit=0;bool haveRelayKey=false;world::RelayConfig relayConfig;
    std::vector<world::LinkConfig> links;
    auto link=[&](const std::string& option)->world::LinkConfig&{if(links.empty()){std::cerr<<option<<" comes after --link NAME\n";std::exit(2);}return links.back();};
    auto filter=[](const std::string& option,const std::string& text){
        try{return world::parseFilter(text);}catch(const std::exception& e){std::cerr<<option<<": "<<e.what()<<"\n";std::exit(2);}};
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--port")port=std::atoi(next().c_str());else if(a=="--bind")bind=next();else if(a=="--data")dataPath=next();
        else if(a=="--prefix")prefix=next();else if(a=="--log")logPath=next();else if(a=="--trace")tracePath=next();else if(a=="--idle-exit")idleExit=std::atol(next().c_str());
        else if(a=="--relay-port")relayPort=std::atoi(next().c_str());else if(a=="--relay-key"){relayKey=next();haveRelayKey=true;}
        else if(a=="--relay-enter")relayConfig.enterMetres=std::atof(next().c_str());else if(a=="--relay-leave")relayConfig.leaveMetres=std::atof(next().c_str());
        else if(a=="--relay-timeout")relayConfig.timeoutMs=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),100000L)*1000);
        else if(a=="--relay-players")relayConfig.maxPlayers=size_t(std::max(std::atol(next().c_str()),0L));
        else if(a=="--relay-rate")relayConfig.ratePerPeer=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-full")relayConfig.fullMetres=std::atof(next().c_str());else if(a=="--broadcast-animation")relayConfig.animationMetres=std::atof(next().c_str());
        else if(a=="--broadcast-interval")relayConfig.intervalMs=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-budget")relayConfig.budget=size_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-flush")relayConfig.flushMs=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-rate")relayConfig.broadcastRate=uint32_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-build"){if(!parseBuild(next(),relayConfig)){std::cerr<<"--broadcast-build GAME:HEX (the game build and the 32 hex digits of the mod's wire identity)\n";return 2;}}
        else if(a=="--link"){links.emplace_back();links.back().name=next();}
        else if(a=="--link-key")link(a).key=next();else if(a=="--link-url")link(a).url=next();
        else if(a=="--link-export")link(a).exportFilter=filter(a,next());else if(a=="--link-import")link(a).importFilter=filter(a,next());
        else if(a=="--link-every")link(a).everySeconds=std::atoi(next().c_str());
        else{std::cerr<<"usage: coop-world --port N [--bind ADDRESS] [--data FILE] [--prefix SECRET] [--log FILE] [--trace FILE] [--idle-exit SECONDS]\n"
                        "                  [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS] [--relay-players N] [--relay-rate N]]\n"
                        "                   [--broadcast-full METRES] [--broadcast-interval MS] [--broadcast-animation METRES] [--broadcast-budget BYTES]\n"
                        "                   [--broadcast-flush MS] [--broadcast-rate N] [--broadcast-build GAME:HEX]]\n"
                        "                  [--link NAME --link-key KEY [--link-url http://ADDRESS:PORT[/PREFIX]] [--link-export FILTER] [--link-import FILTER] [--link-every SECONDS]]...\n"
                      <<OfflineUsage;return 2;}}
    if(port<1||port>65535){std::cerr<<"--port 1..65535 required\n";return 2;}
    for(char c:prefix)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_'){std::cerr<<"--prefix: letters, digits, - and _ only\n";return 2;}
    // Linked servers: nothing is served to or asked of another server without a key, and every link has its own.
    std::vector<world::LinkEndpoint> linkEndpoints(links.size());
    if(links.size()>16){std::cerr<<"at most 16 links\n";return 2;}
    for(size_t i=0;i<links.size();++i){
        auto& l=links[i];
        if(l.name.empty()||l.name.size()>32){std::cerr<<"--link: a name of at most 32 letters, digits, - and _\n";return 2;}
        for(char c:l.name)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_'){std::cerr<<"--link: a name of at most 32 letters, digits, - and _\n";return 2;}
        if(!world::linkKeyText(l.key)){std::cerr<<"link "<<l.name<<": --link-key is required (8 to 64 letters, digits, - and _)\n";return 2;}
        for(size_t k=0;k<i;++k)if(links[k].key==l.key||links[k].name==l.name){std::cerr<<"link "<<l.name<<": every link needs a name and a key of its own\n";return 2;}
        if(!l.url.empty()&&!world::parseLinkUrl(l.url,linkEndpoints[i])){std::cerr<<"link "<<l.name<<": --link-url is http://ADDRESS:PORT or http://ADDRESS:PORT/PREFIX with a numeric IPv4 address\n";return 2;}
        if(l.url.empty()&&!l.importFilter.none){std::cerr<<"link "<<l.name<<": --link-import needs --link-url (the server to take from)\n";return 2;}
        if(l.everySeconds<1||l.everySeconds>3600){std::cerr<<"link "<<l.name<<": --link-every 1..3600 seconds\n";return 2;}}
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){std::cerr<<"winsock unavailable\n";return 1;}
#endif
    world::World w;w.log=[](const std::string& s){line(s);};
    if(!dataPath.empty()){
        bool existed=std::ifstream(dataPath,std::ios::binary).good();
        if(loadWorld(w,false))return 1;
        if(existed)line("world loaded: "+std::to_string(w.objects())+" objects");
        if(!links.empty())save(w);}   // the id other servers know this world by is on disk before any of them hears it
    world::Front front(w,prefix,links);
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
    for(size_t i=0;i<links.size();++i){
        line("link "+links[i].name+": "+(links[i].exportFilter.none?"gives nothing":"gives objects")+", "+(links[i].importFilter.none?"takes nothing":"takes objects from "+links[i].url)+" (this server is "+w.serverId()+")");
        if(!links[i].url.empty()&&!links[i].importFilter.none)std::thread([&w,l=links[i],e=linkEndpoints[i]]{serveLink(w,l,e);}).detach();}
    if(relayPort)line("coop-world relay on "+bind+":"+std::to_string(relayPort)+" (UDP; near from "+std::to_string(int(relayConfig.enterMetres))+" m to "+std::to_string(int(relayConfig.leaveMetres))+" m, "+std::to_string(relayConfig.maxPlayers)+" players)");
    if(relayPort)line("coop-world relay speaks pairs and broadcast (every update within "+std::to_string(int(relayConfig.fullMetres))+" m, beyond it one in "+std::to_string(relayConfig.intervalMs)+" ms, animation within "
                      +std::to_string(int(relayConfig.animationMetres))+" m, frames of "+std::to_string(relayConfig.budget)+" bytes within "+std::to_string(relayConfig.flushMs)+" ms"+(relayConfig.pinBuild?", one build":"")+")");
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
