// coop-world: the players' own server for the game's shared world (README.md). One process, one world file.
//   coop-world --port 8089 [--bind 127.0.0.1] [--data world.json] [--prefix SECRET] [--log events.log] [--trace calls.log]
//              [--idle-exit SECONDS] [--owner KEY] [--name TEXT]
//              [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS]
//               [--relay-players N] [--relay-rate N] [--broadcast-full METRES] [--broadcast-interval MS]
//               [--broadcast-animation METRES] [--broadcast-budget BYTES] [--broadcast-flush MS] [--broadcast-rate N]
//               [--broadcast-build GAME:HEX] [--broadcast-near N] [--broadcast-near-margin METRES] [--fight-visitors N]]
//              [--link NAME --link-key KEY [--link-url http://ADDRESS:PORT[/PREFIX]] [--link-export FILTER] [--link-import FILTER]
//               [--link-every SECONDS] [--link-presence yes|no [--link-relay ADDRESS:PORT] [--link-presence-budget BYTES]
//               [--link-presence-players N]]]...
//   coop-world export  --data world.json --out FILE [--filter FILTER]
//   coop-world import  --data world.json --in FILE [--filter FILTER] [--prune]
//   coop-world forget  --data world.json --server ID
//   coop-world origins --data world.json
//   coop-world policy|players|player-set|offer|withdraw|remove-player|review --data world.json ...   (README, "Server policy")
//   coop-world policy --data world.json --name TEXT                    this world's display name (README, "Builder names")
//   coop-world keygen --out FILE                                       a key pair for signed calls (README, "Administration over SSH")
//   coop-world call --key FILE --port N [--prefix SECRET] METHOD CALL [BODY]   one signed call to this PC's server
// Limits per address (README, "Limits per address", limits.h): --max-connections N --ip-connections N --ip-rate N
// --ip-burst N --ip-strikes N --ip-block SECONDS --request-seconds N [--limit-loopback | --no-ip-limits].
//   coop-world pubkey --key FILE                                       the public half of a key file
// Plain HTTP/1.1 with keep-alive, one thread per connection, bounded sizes. It only answers, except for the linked
// servers it was given an address for (--link-url): those it asks for objects, and nothing else is ever connected to.
// With --relay-port it is also the relay of the real-time layer (relay.h): one UDP socket on the same address, one thread.
#include "api.h"
#include "front.h"
#include "challenges.h"
#include "limits.h"
#include "relay.h"
#include <algorithm>
#include <memory>
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
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t=int;
static void closeSocket(sock_t s){close(s);}
#endif
#include "../common/crash_handler.h"
namespace {
constexpr size_t MaxHeader=16*1024,MaxBody=4*1024*1024;constexpr int IdleSeconds=60;
int maxConnections=32,requestSeconds=30;
std::mutex outLock;std::atomic<int> connections{0};std::atomic<int64_t> lastRequest{0};
diag::Session* diagnostic=new diag::Session; // worker threads may still run at process exit
std::unique_ptr<world::Gate> gate;   // limits per address (limits.h)
std::string dataPath,logPath,tracePath;
void line(const std::string& text){
    std::lock_guard<std::mutex> g(outLock);diagnostic->Line(text);std::cout<<text<<std::endl;
    if(!logPath.empty()){std::ofstream f(logPath,std::ios::app);f<<text<<'\n';}}
int64_t nowSeconds(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
world::Challenges challenges;   // route challenges (challenges.h), saved in the world file beside the world
void save(world::World& w){
    if(dataPath.empty()||(!w.dirty()&&!challenges.dirty()))return;
    static std::mutex m;std::lock_guard<std::mutex> g(m);
    auto doc=w.snapshot();if(challenges.size()||challenges.dirty())doc.set("challenges",challenges.snapshot());
    auto text=doc.dump();auto tmp=dataPath+".tmp";
    {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f<<text;if(!f){line("save failed: "+tmp);return;}}
    std::remove(dataPath.c_str());
    if(std::rename(tmp.c_str(),dataPath.c_str())!=0){line("save failed: "+dataPath);return;}
    w.saved();challenges.saved();}
bool sendAll(sock_t s,const std::string& data){
    size_t at=0;while(at<data.size()){auto n=send(s,data.data()+at,int(data.size()-at),0);if(n<=0)return false;at+=size_t(n);}return true;}
const char* reason(int status){return status==200?"OK":status==400?"Bad Request":status==401?"Unauthorized":status==403?"Forbidden":status==404?"Not Found":status==429?"Too Many Requests":"Error";}
uint64_t steadyMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
std::string ipText(uint32_t ip){return std::to_string(ip>>24)+"."+std::to_string((ip>>16)&255)+"."+std::to_string((ip>>8)&255)+"."+std::to_string(ip&255);}
void serve(sock_t s,uint32_t ip,world::Front& front,world::World& w){
    std::string buffer;char chunk[8192];
    while(true){
        size_t headEnd;
        // A request has requestSeconds from its first byte to its last (a sender who drips bytes keeps no connection).
        uint64_t started=0;
        while((headEnd=buffer.find("\r\n\r\n"))==std::string::npos){
            if(buffer.size()>MaxHeader)return;
            if(!buffer.empty()&&!started)started=steadyMs();
            auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)return;buffer.append(chunk,size_t(n));
            if(!started)started=steadyMs();
#ifndef MUTANT_GATE_DRIP
            if(steadyMs()-started>uint64_t(requestSeconds)*1000)return;
#endif
        }
        if(!started)started=steadyMs();
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
        while(buffer.size()<length){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)return;buffer.append(chunk,size_t(n));
            if(steadyMs()-started>uint64_t(requestSeconds)*1000)return;}
        r.body=buffer.substr(0,length);buffer.erase(0,length);
        // Over its rate (or blocked meanwhile): 429 and the connection ends; nothing is handled.
        if(gate&&!gate->request(ip,steadyMs())){sendAll(s,"HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");return;}
        world::Reply reply;
        // A linked server asking is not a game asking: two servers that ask each other would never be idle.
        if(r.target.find("/link/pull")==std::string::npos)lastRequest=nowSeconds();
        try{reply=front.handle(r,nowSeconds());}
        catch(const std::exception& e){reply.status=500;reply.body="{}";line(std::string("error: ")+e.what());}
        save(w);
        if(gate&&gate->answered(ip,reply.status,steadyMs()))line("address "+ipText(ip)+" blocked: too many requests for paths that are no one's");
        if(!tracePath.empty()){
            // Research aid: every call with its body. The Steam ticket, a launcher's sign-in ticket and the answers that hand
            // out tickets and tokens are left out; bodies can name other accounts.
            auto target=r.target;if(auto code=target.find("code=");code!=std::string::npos)target.resize(code+5);
            if(auto t=target.find("/t/");t!=std::string::npos){auto end=target.find('/',t+3);target.replace(t+3,(end==std::string::npos?target.size():end)-t-3,"<ticket>");}
            // The answers that hand out a ticket or a token (a mod's notice token travels in a header, which is not written).
            bool pass=false;for(const char* call:{"/coop/ticket","/coop/relay-token","/coop/notice-token"})pass=pass||target.find(call)!=std::string::npos;
            std::lock_guard<std::mutex> g(outLock);std::ofstream f(tracePath,std::ios::app);
            f<<nowSeconds()<<' '<<r.method<<' '<<target<<' '<<r.body.substr(0,4000)<<" -> "<<reply.status<<' '<<(pass?std::string("<pass>"):reply.body.substr(0,600))<<'\n';}
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
        // The link's name is what its builders' home is called here while that server gives no name of its own.
        if(good)w.setLinkName(puller.peer(),c.name);
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
    try{auto doc=json::parseFile(text.str(),world::MaxWorldFileBytes);w.restore(doc);challenges.restore(doc.get("challenges"));}
    catch(const std::exception& e){std::cerr<<"cannot read "<<dataPath<<": "<<e.what()<<" (left untouched; move it away to start a new world)\n";return 1;}
    return 0;
}
const char* OfflineUsage="usage: coop-world export  --data FILE --out FILE [--filter FILTER]\n"
                         "       coop-world import  --data FILE --in FILE [--filter FILTER] [--prune]\n"
                         "       coop-world forget  --data FILE --server ID\n"
                         "       coop-world origins --data FILE\n"
                         "       coop-world policy        --data FILE [--preset friends|solo|public] [--set KEY=VALUE]... [--name TEXT]\n"
                         "       coop-world players       --data FILE\n"
                         "       coop-world player-set    --data FILE --player ID KEY=VALUE...   (offer_builds=yes|no|ask|default offer_old=yes|no|default member=yes|no banned=yes|no)\n"
                         "       coop-world offer         --data FILE --player ID (--object ID | --all) [--no]\n"
                         "       coop-world withdraw      --data FILE --player ID (--object ID | --all)\n"
                         "       coop-world remove-player --data FILE --player ID\n"
                         "       coop-world review        --data FILE [--accept ID | --reject ID | --accept-player ID]\n"
                         "FILTER: all, none, or terms separated by ';': types=l,rope,e  owners=NAME,ID  areas=226  max-age=30d  likes=no  imported=yes\n";
// The server policy (README, "Server policy"): one line for the log, or a table; the preset's value where it differs.
std::string policyText(const world::Policy& p,bool table){
    auto items=world::policyItems(p),preset=world::policyItems(world::presetPolicy(p.preset));
    std::string out=table?"server policy, preset "+p.preset+"\n":"policy "+p.preset+":";
    for(size_t i=0;i<items.size();++i){
        std::string differs=items[i].second==preset[i].second?"":" (preset: "+preset[i].second+")";
        if(table)out+="  "+items[i].first+std::string(15-items[i].first.size(),' ')+items[i].second+differs+"\n";
        else out+=std::string(i?",":"")+" "+items[i].first+" "+items[i].second+differs;}
    if(table)out+="join is recorded only; offer_old is acted on by a player's mod (offer_strand); offer_builds and offer_old are what a player who chose nothing has (coop-world players)\n";
    return out;
}
// The policy commands on a world file (README, "Server policy"): -1 when argv[1] is not one of them.
int policyCommand(int argc,char** argv){
    const std::string command=argv[1];
    if(command!="policy"&&command!="players"&&command!="player-set"&&command!="offer"&&command!="withdraw"&&command!="remove-player"&&command!="review")return -1;
    std::string preset,player,object,accept,reject,acceptPlayer,name;std::vector<std::string> sets;bool all=false,no=false,naming=false;
    for(int i=2;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--data")dataPath=next();else if(a=="--preset")preset=next();else if(a=="--set")sets.push_back(next());else if(a=="--player")player=next();
        else if(a=="--object")object=next();else if(a=="--all")all=true;else if(a=="--no")no=true;
        else if(a=="--accept")accept=next();else if(a=="--reject")reject=next();else if(a=="--accept-player")acceptPlayer=next();
        else if(a=="--name"&&command=="policy"){name=next();naming=true;}
        else if(command=="player-set"&&a.find('=')!=std::string::npos&&a.compare(0,2,"--")!=0)sets.push_back(a);
        else{std::cerr<<OfflineUsage;return 2;}}
    const bool needsPlayer=command=="player-set"||command=="offer"||command=="withdraw"||command=="remove-player";
    const bool choosesObject=command=="offer"||command=="withdraw";
    bool fits=!dataPath.empty()&&needsPlayer==!player.empty()&&(command=="policy"||preset.empty())&&(command=="policy"||command=="player-set"||sets.empty())
              &&(command=="player-set"?!sets.empty():true)&&(choosesObject?object.empty()==all:(object.empty()&&!all))&&(command=="offer"||!no)
              &&(command=="review"||(accept.empty()&&reject.empty()&&acceptPlayer.empty()))&&(!accept.empty())+(!reject.empty())+(!acceptPlayer.empty())<=1;
    if(!fits){std::cerr<<OfflineUsage;return 2;}
    world::World w;
    if(command=="policy"&&(!preset.empty()||naming)&&!std::ifstream(dataPath,std::ios::binary).good()){if(loadWorld(w,false))return 1;}   // a new world, made with its policy
    else if(loadWorld(w,true))return 1;
    try{
        if(command=="policy"){
            if(!preset.empty()||!sets.empty()){
                world::Policy p=preset.empty()?w.policy():world::presetPolicy(preset);
                for(auto& s:sets)world::setPolicy(p,s);
                w.setPolicy(p);save(w);}
            if(naming){w.setName(name);save(w);}
            std::cout<<policyText(w.policy(),true)<<"server name: "<<(w.name().empty()?"(none: other servers name it by their link, or by its id)":w.name())<<"\n";return 0;}
        if(command=="players"){
            auto p=w.policy();
            for(auto& i:w.playerList()){
                if(!i.known){std::cout<<i.id<<"  "<<i.name<<"  from linked servers: "<<i.imported<<" objects\n";continue;}
                std::cout<<i.id<<"  "<<i.name<<"  shared "<<i.shared<<", waiting "<<i.pending<<", kept "<<i.kept<<", held "<<i.held<<", remnants "<<i.remnants
                         <<(i.imported?", from linked servers "+std::to_string(i.imported):std::string())<<(i.member?"  member":"")<<(i.banned?"  BANNED":"")
                         <<"  offer_builds "<<(i.offerBuilds.empty()?p.offerBuilds+" (server)":i.offerBuilds)<<"  offer_old "<<(i.offerOld.empty()?p.offerOld+" (server)":i.offerOld)<<"\n";}
            return 0;}
        if(command=="player-set"){for(auto& s:sets)w.setPlayer(player,s);save(w);std::cout<<"set for "<<player<<"\n";return 0;}
        if(command=="offer"){auto n=w.offer(player,object,!no);save(w);std::cout<<(no?"kept for its builder: ":"offered: ")<<n<<" objects of "<<player<<"\n";return 0;}
        if(command=="withdraw"){auto r=w.withdraw(player,object);save(w);std::cout<<"withdrawn for "<<player<<": "<<r.remnants<<" stay as remnants, "<<r.removed<<" removed\n";return 0;}
        if(command=="remove-player"){auto n=w.removePlayer(player);save(w);std::cout<<"removed "<<n<<" objects of "<<player<<"\n";return 0;}
        if(!accept.empty()||!reject.empty()){w.review(accept.empty()?reject:accept,!accept.empty());save(w);std::cout<<(accept.empty()?"rejected (kept for its builder) ":"accepted ")<<(accept.empty()?reject:accept)<<"\n";return 0;}
        if(!acceptPlayer.empty()){auto n=w.reviewPlayer(acceptPlayer);save(w);std::cout<<"accepted "<<n<<" objects of "<<acceptPlayer<<"\n";return 0;}
        auto list=w.waiting();
        if(list.empty())std::cout<<"nothing waits\n";
        for(auto& o:list)std::cout<<o.id<<"  type "<<o.type<<", area "<<o.area<<", by "<<o.name<<" ("<<o.creator<<"): "<<(!o.held.empty()?"held for its builder only: "+world::heldText(o.held)+(o.forBuilder?" (and then its builder's answer)":""):o.forBuilder?"waits for its builder (coop-world offer)":"waits for the owner")<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"refused, nothing changed: "<<e.what()<<"\n";return 1;}
}
// --policy and --policy-set at the start: the preset only for a new world, the settings every time (saved).
int startPolicy(world::World& w,const std::string& preset,const std::vector<std::string>& sets,bool newWorld,std::string& note){
    world::Policy p=w.policy();
    try{
        if(!preset.empty()&&newWorld)p=world::presetPolicy(preset);
        else if(!preset.empty()&&preset!=p.preset)note=" (this world keeps its preset: --policy "+preset+" is for a new world, coop-world policy --preset changes it)";
        for(auto& s:sets)world::setPolicy(p,s);
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
    if(newWorld||world::policyItems(p)!=world::policyItems(w.policy())){w.setPolicy(p);save(w);}
    return 0;
}
// One request to the server on this PC (127.0.0.1 only): the status, or 0 when there was no answer.
int localRequest(int port,const std::string& method,const std::string& target,const std::vector<std::pair<std::string,std::string>>& headers,const std::string& body,std::string& out){
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(port));a.sin_addr.s_addr=htonl(0x7f000001);
    sock_t s=socket(AF_INET,SOCK_STREAM,0);
#ifdef _WIN32
    if(s==INVALID_SOCKET)return 0;
#else
    if(s<0)return 0;
#endif
    setTimeouts(s,10);
    std::string request=method+" "+target+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\n";
    for(auto& h:headers)request+=h.first+": "+h.second+"\r\n";
    request+="Content-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    std::string in;
    if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof a)==0&&sendAll(s,request)){
        char chunk[8192];
        while(in.size()<=MaxHeader+MaxBody){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)break;in.append(chunk,size_t(n));}}
    closeSocket(s);
    size_t headEnd=in.find("\r\n\r\n");
    if(headEnd==std::string::npos||in.compare(0,9,"HTTP/1.1 ")!=0)return 0;
    out=in.substr(headEnd+4);return std::atoi(in.c_str()+9);
}
// A key file: the secret half (the seed) in 64 hex digits, alone on its line. Never printed.
bool readKey(const std::string& path,world::identity::Keys& keys){
    std::ifstream f(path,std::ios::binary);std::string text;if(!f||!std::getline(f,text))return false;
    while(!text.empty()&&(text.back()=='\r'||text.back()==' '))text.pop_back();
    world::identity::Seed seed{};if(!world::identity::unhex(text,seed.data(),seed.size()))return false;
    keys=world::identity::keysFromSeed(seed);return true;}
// keygen and call (README, "Administration over SSH"): a key pair for signed calls, and one signed call to the server
// on this PC, as a launcher makes them. With the owner's key (--owner) its calls are the owner's.
int keyCommand(int argc,char** argv){
    const std::string command=argv[1];
    std::string out,keyPath,prefix;int port=0;std::vector<std::string> rest;
    for(int i=2;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--out"&&command=="keygen")out=next();else if(a=="--key"&&command!="keygen")keyPath=next();
        else if(a=="--port"&&command=="call")port=std::atoi(next().c_str());else if(a=="--prefix"&&command=="call")prefix=next();
        else if(command=="call"&&a.compare(0,2,"--")!=0)rest.push_back(a);
        else{std::cerr<<"usage: coop-world keygen --out FILE\n       coop-world pubkey --key FILE\n       coop-world call --key FILE --port N [--prefix SECRET] GET|POST CALL [BODY]   (CALL: policy, owner/players, owner/ban, ...)\n";return 2;}}
    namespace id=world::identity;
    if(command=="keygen"){
        if(out.empty()){std::cerr<<"usage: coop-world keygen --out FILE\n";return 2;}
        if(std::ifstream(out).good()){std::cerr<<out<<" exists: it is left as it is (a new key would be a new owner)\n";return 1;}
        id::Keys k;try{k=id::makeKeys();}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
#ifndef _WIN32
        int fd=::open(out.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){std::cerr<<"cannot write "<<out<<"\n";return 1;}
        std::string text=id::hex(k.seed)+"\n";bool good=::write(fd,text.data(),text.size())==ssize_t(text.size());good=::close(fd)==0&&good;
        if(!good){std::cerr<<"cannot write "<<out<<"\n";return 1;}
#else
        {std::ofstream f(out,std::ios::binary);f<<id::hex(k.seed)<<"\n";if(!f){std::cerr<<"cannot write "<<out<<"\n";return 1;}}
#endif
        std::cout<<id::hex(k.pub)<<"\n";return 0;}
    id::Keys k;
    if(keyPath.empty()||!readKey(keyPath,k)){std::cerr<<"--key FILE: a key file made by coop-world keygen\n";return 2;}
    if(command=="pubkey"){std::cout<<id::hex(k.pub)<<"\n";return 0;}
    if(port<1||port>65535||rest.size()<2||rest.size()>3||(rest[0]!="GET"&&rest[0]!="POST")){std::cerr<<"usage: coop-world call --key FILE --port N [--prefix SECRET] GET|POST CALL [BODY]\n";return 2;}
    for(char c:prefix)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_'){std::cerr<<"--prefix: letters, digits, - and _ only\n";return 2;}
    const std::string base=prefix.empty()?"/coop/":"/"+prefix+"/coop/",target=base+rest[1],body=rest.size()>2?rest[2]:"";
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){std::cerr<<"winsock unavailable\n";return 1;}
#endif
    // The server's id and clock come from its rules, as the launcher takes them, so this PC's clock may be off.
    std::string text;int status=localRequest(port,"GET",base+"policy",{},"",text);
    if(status!=200){std::cerr<<"the server on 127.0.0.1:"<<port<<" did not answer its rules ("<<(status?std::to_string(status):std::string("no answer"))<<"): is it running, with this prefix?\n";return 3;}
    std::string server;int64_t time=0;
    try{auto p=json::parse(text);server=p.get("server").string();time=p.get("time").integer();}catch(const std::exception&){}
    if(server.empty()||!time){std::cerr<<"the server's rules could not be read\n";return 3;}
    status=localRequest(port,rest[0],target,id::signRequest(k,rest[0],target,server,body,time,id::newNonce()),body,text);
    std::cout<<text<<"\n";
    if(status!=200){std::cerr<<"refused: "<<(status?std::to_string(status):std::string("no answer"))<<"\n";return 3;}
    return 0;
}
// The commands that work on a world file while no server has it open (README, "Linked servers").
int offline(int argc,char** argv){
    if(std::string c=argv[1];c=="keygen"||c=="pubkey"||c=="call")return keyCommand(argc,argv);
    if(int r=policyCommand(argc,argv);r>=0)return r;
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
    for(auto& o:w.origins())std::cout<<"from server "<<o.server<<(o.via==o.server?std::string():" by way of "+o.via)<<": "<<o.objects<<" objects (its builders' home: "<<w.homeName(o.server)<<")\n";
    return 0;
}
uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
// The relay's socket: datagrams in and out, the clock, and nothing else. All of the relay's state lives on this thread.
void serveRelay(sock_t s,world::RelayConfig config){
    world::Relay relay(std::move(config),[s](const world::RelayAddress& to,const uint8_t* data,size_t size){
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(to.port);a.sin_addr.s_addr=htonl(to.ip);
        sendto(s,reinterpret_cast<const char*>(data),int(size),0,reinterpret_cast<const sockaddr*>(&a),sizeof a);});
    relay.log=[](const std::string& text){line(text);};relay.utc=[]{return diag::UtcMs();};   // bug mark lines carry the UTC time
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
// --link-relay A.B.C.D:PORT, a linked server's relay.
bool parseRelayAddress(const std::string& text,world::RelayAddress& out){
    auto colon=text.find(':');if(colon==std::string::npos)return false;
    in_addr a{};if(inet_pton(AF_INET,text.substr(0,colon).c_str(),&a)!=1)return false;
    std::string port=text.substr(colon+1);if(port.empty()||port.size()>5)return false;
    long p=0;for(char c:port){if(c<'0'||c>'9')return false;p=p*10+(c-'0');}
    if(p<1||p>65535)return false;
    out.ip=ntohl(a.s_addr);out.port=uint16_t(p);return out.ip!=0;}
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
    unsigned maxLogMb=diag::DefaultMaxLogMb;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--max-log-mb") {
        if(i+1>=argc){std::cerr<<"--max-log-mb needs 5..2000\n";return 2;}
        std::string v=argv[++i];
        if(v.empty()||v.size()>4||v.find_first_not_of("0123456789")!=std::string::npos||!diag::ValidMaxLogMb(std::strtoul(v.c_str(),nullptr,10))){std::cerr<<"--max-log-mb needs 5..2000\n";return 2;}
        maxLogMb=unsigned(std::strtoul(v.c_str(),nullptr,10));
    }
    if(diagnostic->Open(diag::DefaultLogsDir(),"world",maxLogMb))diag::crash::Install(*diagnostic);
    std::atexit([]{diagnostic->Close(true);});
    if(argc>1&&argv[1][0]!='-')return offline(argc,argv);
    std::string bind="127.0.0.1",prefix,relayKey;int port=0,relayPort=0;long idleExit=0;bool haveRelayKey=false,nearGiven=false;world::RelayConfig relayConfig;
    std::vector<world::LinkConfig> links;
    struct Presence {std::string on,relay;long budget=250000,players=64;};std::vector<Presence> presence;   // per link: --link-presence and its options
    std::string policyPreset,owner,name;bool naming=false;std::vector<std::string> policySets;
    world::GateConfig gateConfig;bool limits=true;
    auto link=[&](const std::string& option)->world::LinkConfig&{if(links.empty()){std::cerr<<option<<" comes after --link NAME\n";std::exit(2);}return links.back();};
    auto filter=[](const std::string& option,const std::string& text){
        try{return world::parseFilter(text);}catch(const std::exception& e){std::cerr<<option<<": "<<e.what()<<"\n";std::exit(2);}};
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto next=[&]{if(i+1>=argc){std::cerr<<"missing value for "<<a<<"\n";std::exit(2);}return std::string(argv[++i]);};
        if(a=="--port")port=std::atoi(next().c_str());else if(a=="--bind")bind=next();else if(a=="--data")dataPath=next();
        else if(a=="--max-log-mb"){next();}
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
        else if(a=="--broadcast-near"){relayConfig.nearCap=size_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));nearGiven=true;}
        else if(a=="--broadcast-near-margin")relayConfig.nearMargin=std::atof(next().c_str());
        else if(a=="--fight-visitors")relayConfig.fightVisitors=size_t(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--broadcast-build"){if(!parseBuild(next(),relayConfig)){std::cerr<<"--broadcast-build GAME:HEX (the game build and the 32 hex digits of the mod's wire identity)\n";return 2;}}
        else if(a=="--link"){links.emplace_back();links.back().name=next();presence.emplace_back();}
        else if(a=="--link-key")link(a).key=next();else if(a=="--link-url")link(a).url=next();
        else if(a=="--link-export")link(a).exportFilter=filter(a,next());else if(a=="--link-import")link(a).importFilter=filter(a,next());
        else if(a=="--link-every")link(a).everySeconds=std::atoi(next().c_str());
        else if(a=="--link-presence"){link(a);presence.back().on=next();}else if(a=="--link-relay"){link(a);presence.back().relay=next();}
        else if(a=="--link-presence-budget"){link(a);presence.back().budget=std::min(std::max(std::atol(next().c_str()),0L),1000000000L);}
        else if(a=="--link-presence-players"){link(a);presence.back().players=std::min(std::max(std::atol(next().c_str()),0L),100000L);}
        else if(a=="--policy")policyPreset=next();else if(a=="--policy-set")policySets.push_back(next());
        else if(a=="--max-connections")maxConnections=int(std::min(std::max(std::atol(next().c_str()),1L),4096L));
        else if(a=="--ip-connections")gateConfig.connections=int(std::min(std::max(std::atol(next().c_str()),0L),4096L));
        else if(a=="--ip-rate")gateConfig.rate=std::min(std::max(std::atof(next().c_str()),0.0),100000.0);
        else if(a=="--ip-burst")gateConfig.burst=std::min(std::max(std::atof(next().c_str()),1.0),1000000.0);
        else if(a=="--ip-strikes")gateConfig.strikes=int(std::min(std::max(std::atol(next().c_str()),0L),1000000L));
        else if(a=="--ip-block")gateConfig.blockMs=uint64_t(std::min(std::max(std::atol(next().c_str()),1L),604800L))*1000;
        else if(a=="--request-seconds")requestSeconds=int(std::min(std::max(std::atol(next().c_str()),1L),300L));
        else if(a=="--limit-loopback")gateConfig.loopback=true;else if(a=="--no-ip-limits")limits=false;
        else if(a=="--owner"){owner=world::identity::AccountPrefix+next();if(!world::identity::accountIsKey(owner)){std::cerr<<"--owner: the owner's public key, 64 hex digits\n";return 2;}}
        else if(a=="--name"){name=next();naming=true;if(!world::serverNameText(name)){std::cerr<<"--name: 1 to "<<world::MaxHomeChars<<" characters (at most "<<world::MaxHomeBytes<<" bytes of UTF-8), no control characters, no space at either end\n";return 2;}}
        else{std::cerr<<"usage: coop-world --port N [--bind ADDRESS] [--data FILE] [--prefix SECRET] [--log FILE] [--trace FILE] [--idle-exit SECONDS] [--owner KEY] [--name TEXT]\n"
                        "                  [--policy friends|solo|public] [--policy-set KEY=VALUE]...\n"
                        "                  [--max-connections N] [--ip-connections N] [--ip-rate N] [--ip-burst N] [--ip-strikes N] [--ip-block SECONDS] [--request-seconds N] [--limit-loopback | --no-ip-limits]\n"
                        "                  [--relay-port N [--relay-key TEXT] [--relay-enter METRES] [--relay-leave METRES] [--relay-timeout SECONDS] [--relay-players N] [--relay-rate N]]\n"
                        "                   [--broadcast-full METRES] [--broadcast-interval MS] [--broadcast-animation METRES] [--broadcast-budget BYTES]\n"
                        "                   [--broadcast-flush MS] [--broadcast-rate N] [--broadcast-build GAME:HEX] [--broadcast-near N]\n"
                        "                   [--broadcast-near-margin METRES] [--fight-visitors N]]\n"
                        "                  [--link NAME --link-key KEY [--link-url http://ADDRESS:PORT[/PREFIX]] [--link-export FILTER] [--link-import FILTER] [--link-every SECONDS]\n"
                        "                   [--link-presence yes|no [--link-relay ADDRESS:PORT] [--link-presence-budget BYTES] [--link-presence-players N]]]...\n"
                      <<OfflineUsage;return 2;}}
    if(port<1||port>65535){std::cerr<<"--port 1..65535 required\n";return 2;}
    try{world::Policy p=world::presetPolicy(policyPreset.empty()?"friends":policyPreset);for(auto& s:policySets)world::setPolicy(p,s);}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
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
        if(l.everySeconds<1||l.everySeconds>3600){std::cerr<<"link "<<l.name<<": --link-every 1..3600 seconds\n";return 2;}
        // Presence across the link (README, "Presence across linked servers"): off unless asked for, and only with a relay here.
        auto& p=presence[i];
        if(!p.on.empty()&&p.on!="yes"&&p.on!="no"){std::cerr<<"link "<<l.name<<": --link-presence yes or no\n";return 2;}
        if(p.on!="yes"&&(!p.relay.empty()||p.budget!=250000||p.players!=64)){std::cerr<<"link "<<l.name<<": --link-relay and --link-presence-* need --link-presence yes\n";return 2;}
        if(p.on!="yes")continue;
        if(!relayPort){std::cerr<<"link "<<l.name<<": --link-presence needs --relay-port (this server's relay)\n";return 2;}
        world::RelayLink r;r.name=l.name;r.key=l.key;r.budget=uint32_t(p.budget);r.players=size_t(p.players);
        if(!p.relay.empty()&&!parseRelayAddress(p.relay,r.peer)){std::cerr<<"link "<<l.name<<": --link-relay is ADDRESS:PORT with a numeric IPv4 address (the other server's --relay-port)\n";return 2;}
        relayConfig.links.push_back(r);}
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){std::cerr<<"winsock unavailable\n";return 1;}
#endif
    world::World w;w.log=[](const std::string& s){line(s);};
    const bool newWorld=dataPath.empty()||!std::ifstream(dataPath,std::ios::binary).good();
    if(!dataPath.empty()){
        bool existed=std::ifstream(dataPath,std::ios::binary).good();
        if(loadWorld(w,false))return 1;
        if(existed)line("world loaded: "+std::to_string(w.objects())+" objects");
        if(!links.empty())save(w);}   // the id other servers know this world by is on disk before any of them hears it
    std::string policyNote;if(int r=startPolicy(w,policyPreset,policySets,newWorld,policyNote))return r;
    // The owner's key (the host's launcher gives its own): his signed calls are the owner's actions (README, "Identity").
    // Whoever starts the server with the world file is its owner, so a new key replaces the one the world had.
    if(!owner.empty()&&owner!=w.owner()){w.setOwner(owner);save(w);}
    // This world's display name, saved with it (README, "Builder names").
    if(naming&&name!=w.name()){w.setName(name);save(w);}
    world::Front front(w,prefix,links);
    front.challenges=[&w](const std::string& m,const std::string& c,const std::string& b,const std::string& signer,int64_t now){
        auto r=world::challengesApi(w,challenges,m,c,b,signer,now);if(challenges.dirty())save(w);return r;};
    if(limits)gate=std::make_unique<world::Gate>(gateConfig);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));
    if(inet_pton(AF_INET,bind.c_str(),&address.sin_addr)!=1){std::cerr<<"--bind needs a numeric IPv4 address\n";return 2;}
    if(relayPort){
        // The relay listens on the same address as the rest, on its own UDP port. Its key is the group's secret: the
        // prefix unless another is given, and never empty beyond loopback.
        if(relayPort<1||relayPort>65535){std::cerr<<"--relay-port 1..65535\n";return 2;}
        relayConfig.token=haveRelayKey?relayKey:prefix;
        if(relayConfig.token.empty()&&(ntohl(address.sin_addr.s_addr)>>24)!=127){std::cerr<<"the relay needs --relay-key (or --prefix) when it listens beyond loopback\n";return 2;}
        relayConfig.seed=std::random_device{}();relayConfig.serverId=w.serverId();
        // Who may say HELLO for a player id: the group's key, or under identity keys a relay token of that player.
        relayConfig.admit=[&w,key=relayConfig.token](uint64_t id,const std::string& token){return w.relayAdmit(id,token,key,nowSeconds());};
        relayConfig.evicted=[&w]{return world::relayEvictions(w);};   // the owner's kick and ban (README, "Kick and ban")
        // Who a broadcast player is shown (README, "How many players one player sees"): his choice and friends as the
        // world holds them (under identity keys), and the policy's near_cap unless --broadcast-near gives the cap.
        relayConfig.viewer=[&w,nearGiven](uint64_t id){return world::relayViewer(w,id,nearGiven);};
        // Where each player's game is, for collections made "near me" and imported "here" (../docs/COLLECTIONS.md).
        relayConfig.spotted=[&w](uint64_t id,uint32_t,const std::array<double,3>& at,bool haveYaw,double yaw,const std::string& name){auto s=world::relaySpot(at,haveYaw,yaw,nowSeconds());s.name=name;w.noteSpot(id,s);};
        relayConfig.online=[&w](std::vector<std::string> names){w.setOnline(std::move(names));};   // who is on, for the policy
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
        std::thread([idleExit,&w]{while(true){std::this_thread::sleep_for(std::chrono::seconds(5));if(nowSeconds()-lastRequest.load()>=idleExit){save(w);line("idle: leaving");diagnostic->Close();std::_Exit(0);}}}).detach();}
    line("coop-world listening on "+bind+":"+std::to_string(port)+(prefix.empty()?"":" (secret prefix set)"));
    for(size_t i=0;i<links.size();++i){
        line("link "+links[i].name+": "+(links[i].exportFilter.none?"gives nothing":"gives objects")+", "+(links[i].importFilter.none?"takes nothing":"takes objects from "+links[i].url)+" (this server is "+w.serverId()+")");
        if(!links[i].url.empty()&&!links[i].importFilter.none)std::thread([&w,l=links[i],e=linkEndpoints[i]]{serveLink(w,l,e);}).detach();}
    if(relayPort)line("coop-world relay on "+bind+":"+std::to_string(relayPort)+" (UDP; near from "+std::to_string(int(relayConfig.enterMetres))+" m to "+std::to_string(int(relayConfig.leaveMetres))+" m, "+std::to_string(relayConfig.maxPlayers)+" players)");
    for(auto& l:relayConfig.links)
        line("link "+l.name+": presence of broadcast players with the relay at "+(l.peer.ip?std::to_string(l.peer.ip>>24)+"."+std::to_string((l.peer.ip>>16)&255)+"."+std::to_string((l.peer.ip>>8)&255)+"."+std::to_string(l.peer.ip&255)+":"+std::to_string(l.peer.port):std::string("the address of its first HELLO"))
             +" (at most "+std::to_string(l.budget)+" bytes a second and "+std::to_string(l.players)+" players each way)");
    if(relayPort)line("coop-world relay speaks pairs and broadcast (every update within "+std::to_string(int(relayConfig.fullMetres))+" m, beyond it one in "+std::to_string(relayConfig.intervalMs)+" ms, animation within "
                      +std::to_string(int(relayConfig.animationMetres))+" m, frames of "+std::to_string(relayConfig.budget)+" bytes within "+std::to_string(relayConfig.flushMs)+" ms"+(relayConfig.pinBuild?", one build":"")
                      +(nearGiven?relayConfig.nearCap?", at most "+std::to_string(relayConfig.nearCap)+" players near shown to each":", every player near shown to each"
                                  :", at most the policy's near_cap players near shown to each ("+std::to_string(w.policy().nearCap)+" now)")
                      +(relayConfig.fightVisitors?", fights with at most "+std::to_string(relayConfig.fightVisitors)+" visitors":", no fights")+")");
    line(policyText(w.policy(),false)+policyNote);
    if(!w.owner().empty())line("owner key "+w.owner().substr(8,16)+"... (signed calls of this key are the owner's)");
    if(!w.name().empty())line("server name "+w.name()+" (the home of its builders on linked servers)");
    line("limits: "+std::to_string(maxConnections)+" connections"+(gate?", per address "+std::to_string(gateConfig.connections)+" at once, "+std::to_string(int(gateConfig.rate))+" requests a second (burst "
         +std::to_string(int(gateConfig.burst))+"), blocked "+std::to_string(gateConfig.blockMs/1000)+" s after "+std::to_string(gateConfig.strikes)+" unknown paths in a minute"+(gateConfig.loopback?", loopback too":", loopback exempt"):std::string(", none per address")));
    while(true){
        sockaddr_in from{};socklen_t fromSize=sizeof from;
        sock_t s=accept(listener,reinterpret_cast<sockaddr*>(&from),&fromSize);
#ifdef _WIN32
        if(s==INVALID_SOCKET)continue;
        DWORD timeout=IdleSeconds*1000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof timeout);
#else
        if(s<0)continue;
        timeval timeout{IdleSeconds,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
#endif
        setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof yes);
        if(connections.load()>=maxConnections){closeSocket(s);continue;}
        const uint32_t ip=from.sin_family==AF_INET?ntohl(from.sin_addr.s_addr):0;
        // Limits per address: one address cannot hold every connection, nor come back while it is blocked.
        if(gate&&gate->open(ip,steadyMs())!=world::Gate::Open::Ok){closeSocket(s);continue;}
        ++connections;
        std::thread([s,ip,&front,&w]{serve(s,ip,front,w);closeSocket(s);if(gate)gate->close(ip);--connections;}).detach();}
}
