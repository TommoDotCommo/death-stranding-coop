#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include "strand.h"
#include "../world/json.h"
#include <cctype>
#include <chrono>
#include <ctime>
#include <sstream>
#ifndef _WIN32
#include <thread>
#endif
namespace launcher::strand {
namespace {
constexpr size_t MaxAnswer=16u*1024*1024;
bool oneOf(const std::string& s,std::initializer_list<const char*> allowed){for(auto a:allowed)if(s==a)return true;return false;}
[[noreturn]] void bad(const char* why){throw std::runtime_error(std::string("policy: ")+why);}
bool plain(const std::string& s){for(unsigned char c:s)if(c<0x20||c==0x7f)return false;return true;}
std::string section(const std::string& server){return "server-"+server;}
std::string trim(const std::string& s){size_t b=0,e=s.size();while(b<e&&(s[b]==' '||s[b]=='\t'||s[b]=='\r'))++b;while(e>b&&(s[e-1]==' '||s[e-1]=='\t'||s[e-1]=='\r'))--e;return s.substr(b,e-b);}
bool passText(const std::string& s){if(s.empty()||s.size()>32)return false;for(unsigned char c:s)if(!std::isalnum(c))return false;return true;}
}
std::string base(const Target& t){return t.prefix.empty()?std::string():"/"+t.prefix;}
bool call(const Target& t,const Transport& send,const std::string& method,const std::string& path,const std::string& body,Http& out){
    Headers h;
    if(t.sign&&t.keys)for(auto& x:world::identity::signRequest(*t.keys,method,path,t.server,body,int64_t(std::time(nullptr))+t.clockOffset,world::identity::newNonce()))h.push_back(x);
    return send(method,path,h,body,out);}
std::string message(const Http& h){try{auto v=json::parseFile(h.body,MaxAnswer);if(v.get("message").isString()&&plain(v.get("message").string()))return v.get("message").string();}catch(const std::exception&){}
    return "the server answered "+std::to_string(h.status);}
std::string account(const Session& s,uint32_t steamAccount){
    auto address=worldAddress(s);
#ifndef MUTANT_STRAND_ACCOUNT
    // Two games on one PC are one Steam account: the address names the player, and that name is his key.
    if(auto at=address.find("/k/");at!=std::string::npos){auto end=address.find('/',at+3);return address.substr(at+3,end-at-3);}
#endif
    return steamAccount?std::to_string(76561197960265728ull+steamAccount):std::string();
}
Target target(const Session& s,uint32_t steamAccount){
    Target t;t.address=s.group==Group::Host?"127.0.0.1":s.group==Group::Join?s.server:s.host?"127.0.0.1":s.peer;
    t.port=worldPort(s);t.prefix=worldPrefix(s);t.account=account(s,steamAccount);return t;
}
std::string Policy::value(const std::string& key) const {for(auto& s:settings)if(s.key==key)return s.value;return {};}
std::string Policy::text() const {
    std::string out="This server's rules ("+preset+" preset; "+std::to_string(players)+" of "+std::to_string(limit)+" players):\r\n";
    for(auto& s:settings)out+="- "+s.text+(s.value==s.preset?"":" (the owner changed this from the preset's "+s.preset+")")+"\r\n";
    out+=keys()?"This server knows players by their keys: your launcher's key is you here, and nobody else can sign in, build or withdraw in your name. The traffic is not encrypted: use a LAN or VPN."
               :"Anyone who has this group's invite can act for any player here: there are no player accounts yet. Join friends' servers only.";
    return out;
}
Policy parsePolicy(const std::string& body){
    json::Value v;
    try{v=json::parseFile(body,MaxAnswer);}catch(const std::exception&){bad("not JSON");}
    Policy p;
#ifndef MUTANT_STRAND_PARSE
    if(!v.isObject()||v.get("format").integer()!=1||!v.get("format").isInt())bad("not format 1");
    p.server=v.get("server").string();
    // The id names the server's section in launcher.ini: letters and digits only.
    if(p.server.size()<4||p.server.size()>32)bad("bad server id");
    for(char c:p.server)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))bad("bad server id");
    p.preset=v.get("preset").string();if(!oneOf(p.preset,{"friends","solo","public"}))bad("unknown preset");
    if(!v.get("players").get("count").isInt()||!v.get("players").get("limit").isInt())bad("no players");
    if(!v.get("settings").isArray()||v.get("settings").items().empty()||v.get("settings").items().size()>32)bad("no settings");
#else
    p.server=v.get("server").string();p.preset=v.get("preset").string();
#endif
    p.players=v.get("players").get("count").integer();p.limit=v.get("players").get("limit").integer();p.time=v.get("time").integer();
    for(auto& s:v.get("settings").items()){
        Setting one{s.get("key").string(),s.get("value").string(),s.get("preset").string(),s.get("text").string()};
#ifndef MUTANT_STRAND_PARSE
        for(const char* k:{"key","value","preset","text"})if(!s.get(k).isString()||!plain(s.get(k).string())||s.get(k).string().size()>400)bad("a setting is not four lines of text");
        if(one.key.empty()||one.text.empty())bad("a setting without a name or words");
#endif
        p.settings.push_back(one);}
#ifndef MUTANT_STRAND_PARSE
    if(!oneOf(p.value("offer_builds"),{"yes","no","ask"})||!oneOf(p.value("offer_old"),{"yes","no"}))bad("no defaults for a player's choices");
#endif
    return p;
}
std::string request(const Target& t,const std::string& method,const std::string& path,const Headers& headers,const std::string& body){
    std::string extra;for(auto& h:headers)extra+=h.first+": "+h.second+"\r\n";
    return method+" "+path+" HTTP/1.1\r\nHost: "+t.address+":"+std::to_string(t.port)+"\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())
           +"\r\n"+extra+"Connection: close\r\n\r\n"+body;
}
bool parseResponse(const std::string& raw,Http& out){
    auto end=raw.find("\r\n\r\n");if(end==std::string::npos||end>16*1024||raw.compare(0,9,"HTTP/1.1 ")!=0||raw.size()<12)return false;
    int status=0;for(size_t i=9;i<12;++i){if(raw[i]<'0'||raw[i]>'9')return false;status=status*10+(raw[i]-'0');}
    std::istringstream head(raw.substr(0,end));std::string row;std::getline(head,row);size_t length=0;bool have=false;
    while(std::getline(head,row)){
        auto colon=row.find(':');if(colon==std::string::npos)continue;
        std::string name=row.substr(0,colon),value=trim(row.substr(colon+1));for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(name=="content-length"){if(have||value.empty()||value.size()>9||value.find_first_not_of("0123456789")!=std::string::npos)return false;length=std::stoul(value);have=true;}}
    if(!have||length>MaxAnswer||raw.size()-end-4!=length)return false;
    out.status=status;out.body=raw.substr(end+4);return true;
}
#ifdef _WIN32
using Sock=SOCKET;
static const Sock NoSock=INVALID_SOCKET;
static void closeSock(Sock s){closesocket(s);}
static void blocking(Sock s,bool yes){u_long v=yes?0:1;ioctlsocket(s,FIONBIO,&v);}
static bool connecting(){return WSAGetLastError()==WSAEWOULDBLOCK;}
static void timeouts(Sock s,unsigned ms){DWORD t=ms;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&t),sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&t),sizeof t);}
#else
using Sock=int;
static const Sock NoSock=-1;
static void closeSock(Sock s){close(s);}
static void blocking(Sock s,bool yes){int f=fcntl(s,F_GETFL,0);fcntl(s,F_SETFL,yes?f&~O_NONBLOCK:f|O_NONBLOCK);}
static bool connecting(){return errno==EINPROGRESS;}
static void timeouts(Sock s,unsigned ms){timeval t{long(ms/1000),long(ms%1000)*1000};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);}
#endif
Transport transport(const Target& t,unsigned ms){
    return [t,ms](const std::string& method,const std::string& path,const Headers& headers,const std::string& body,Http& out){
        auto ip=mod::peerAddress(t.address);if(!ip||!t.port||t.port>65535)return false;
        Sock s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==NoSock)return false;
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(uint16_t(t.port));a.sin_addr.s_addr=htonl(*ip);
        blocking(s,false);
        if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0){
            if(!connecting()){closeSock(s);return false;}
            fd_set writable,failed;FD_ZERO(&writable);FD_ZERO(&failed);FD_SET(s,&writable);FD_SET(s,&failed);timeval wait{long(ms/1000),long(ms%1000)*1000};
            int error=0;socklen_t size=sizeof error;
            if(select(int(s)+1,nullptr,&writable,&failed,&wait)<=0||!FD_ISSET(s,&writable)||getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)!=0||error){closeSock(s);return false;}}
        blocking(s,true);timeouts(s,ms);
        auto text=request(t,method,path,headers,body);size_t at=0;
        while(at<text.size()){auto n=send(s,text.data()+at,int(text.size()-at),0);if(n<=0){closeSock(s);return false;}at+=size_t(n);}
        std::string in;char chunk[8192];
        while(in.size()<=MaxAnswer+16*1024){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)break;in.append(chunk,size_t(n));}
        closeSock(s);return parseResponse(in,out);};
}
const char* fetchText(Fetch f){
    switch(f){
    case Fetch::Ok:return "The server showed its rules.";
    case Fetch::Old:return "This server does not show its rules (a coop-world from before the launcher could ask). It shares what every player builds with every player, and has no choices for you.";
    case Fetch::WrongKey:return "The server does not know this group's key. Ask the host for a new invite. Your game was not started.";
    case Fetch::Unreachable:return "The group's server did not answer. Ask the host to start it, check the invite and the host's Windows Firewall, then try again. Your game was not started.";
    default:return "The group's server answered in a form this launcher cannot read (another version?). Your game was not started.";
    }
}
Fetch fetchPolicy(const Target& t,const Transport& send,Policy& p,std::string* raw){
    Http h;if(!call(t,send,"GET",base(t)+"/coop/policy","",h))return Fetch::Unreachable;
    if(h.status==404){
        // No launcher API here. A gateway call without a session tells an older server (401: the key is right) from a
        // server that does not know the key (404).
        Http g;if(!send("GET",base(t)+"/gate/e/getVersion",{},"",g))return Fetch::Unreachable;
#ifndef MUTANT_STRAND_WRONG_KEY
        return g.status==401?Fetch::Old:Fetch::WrongKey;
#else
        return Fetch::Old;
#endif
    }
    if(h.status!=200)return Fetch::Bad;
    try{p=parsePolicy(h.body);}catch(const std::exception&){return Fetch::Bad;}
    if(raw)*raw=h.body;
    return Fetch::Ok;
}
bool identify(Target& t,const Policy& p,int64_t localNow,std::string& why){
    if(!p.keys())return true;
    if(!t.keys){why="This server knows players by their keys, and this launcher has none.";return false;}
    t.account=world::identity::account(t.keys->pub);t.server=p.server;t.clockOffset=p.time?p.time-localNow:0;
#ifndef MUTANT_STRAND_SIGN
    t.sign=true;
#endif
    return true;
}
bool takePasses(const Target& t,const Transport& send,Passes& out,std::string& why){
    if(!t.sign||!t.keys){why="this launcher has no key for the server.";return false;}
    json::Value body=json::Value::object();body.set("player",t.account);
    Http a,b;
    if(!call(t,send,"POST",base(t)+"/coop/ticket",body.dump(),a)||!call(t,send,"POST",base(t)+"/coop/relay-token",body.dump(),b)){why="the server did not answer.";return false;}
    if(a.status!=200){why=message(a)+".";return false;}
    if(b.status!=200){why=message(b)+".";return false;}
    try{
        auto ta=json::parseFile(a.body,MaxAnswer),tb=json::parseFile(b.body,MaxAnswer);
        Passes p{ta.get("ticket").string(),tb.get("token").string(),uint64_t(tb.get("relay_id").integer()),{}};
        // The relay id is made from the key: a server that names another one is not the server this key talks to.
        if(!passText(p.ticket)||!passText(p.relayToken)||p.relayId!=world::identity::relayId(t.keys->pub))throw std::runtime_error("bad passes");
        out=p;
    }catch(const std::exception&){why="its answer cannot be read.";return false;}
    return true;
}
void usePasses(Session& s,const Passes& p){s.signInTicket=p.ticket;s.relayToken=p.relayToken;s.playerId=p.relayId;}
void useNoticeToken(Session& s,const Passes& p){s.noticeToken=p.noticeToken;}
bool takeNoticeToken(const Target& t,const Transport& send,std::string& token,std::string& why){
    token.clear();
    if(t.account.empty()){why="your account on the server is not known.";return false;}
    json::Value body=json::Value::object();body.set("player",t.account);
    Http h;if(!call(t,send,"POST",base(t)+"/coop/notice-token",body.dump(),h)){why="the server did not answer.";return false;}
    if(h.status!=200){why=message(h)+".";return false;}
    try{auto v=json::parseFile(h.body,MaxAnswer);if(!passText(v.get("token").string()))throw std::runtime_error("bad token");token=v.get("token").string();}
    catch(const std::exception&){why="its answer cannot be read.";return false;}
    return true;
}
bool hostPasses(Target& t,const Transport& send,unsigned tries,Passes& out,bool& keyed,std::string& why){
    Policy p;Fetch f=Fetch::Unreachable;
    for(unsigned i=0;i<std::max(1u,tries);++i){
        if(i){
#ifdef _WIN32
            Sleep(500);   // the launcher's MinGW has no std::thread
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
        }
        f=fetchPolicy(t,send,p);if(f!=Fetch::Unreachable)break;}
    keyed=false;
    if(f==Fetch::Old)return true;
    if(f!=Fetch::Ok){why=fetchText(f);return false;}
    if(!identify(t,p,int64_t(std::time(nullptr)),why))return false;
    keyed=p.keys();
    if(keyed&&!takePasses(t,send,out,why)){why="The group server gave your game no sign-in ticket: "+why;return false;}
    std::string none;takeNoticeToken(t,send,out.noticeToken,none);   // none: the game starts without strand notices
    return true;
}
bool validChoices(const Choices& c){return oneOf(c.offerBuilds,{"yes","no","ask"})&&oneOf(c.offerOld,{"yes","no"});}
Choices defaults(const Policy& p){
#ifndef MUTANT_STRAND_DEFAULTS
    return {p.value("offer_builds"),p.value("offer_old")};
#else
    (void)p;return {"yes","no"};
#endif
}
std::string choicesText(const Choices& c){
    return std::string(c.offerBuilds=="yes"?"what you build here is shared":c.offerBuilds=="no"?"what you build here is kept for you":"what you build here waits for your answer")
           +"; "+(c.offerOld=="yes"?"your older strand may be taken over":"your older strand is not taken over");
}
std::string iniGet(const std::string& ini,const std::string& name,const std::string& key){
    std::istringstream in(ini);std::string row;bool inside=false;
    while(std::getline(in,row)){
        row=trim(row);
        if(!row.empty()&&row[0]=='['){inside=row=="["+name+"]";continue;}
#ifdef MUTANT_STRAND_PER_SERVER
        inside=true;
#endif
        auto eq=row.find('=');if(inside&&eq!=std::string::npos&&trim(row.substr(0,eq))==key)return trim(row.substr(eq+1));}
    return {};
}
std::string iniSet(const std::string& ini,const std::string& name,const std::string& key,const std::string& value){
    if(value.find_first_of("\r\n")!=std::string::npos||key.find_first_of("\r\n=[]")!=std::string::npos||name.find_first_of("\r\n[]")!=std::string::npos)throw std::runtime_error("A setting with a line break.");
    std::vector<std::string> rows;{std::istringstream in(ini);std::string row;while(std::getline(in,row)){if(!row.empty()&&row.back()=='\r')row.pop_back();rows.push_back(row);}}
    long header=-1,lastRow=-1;bool inside=false;
    for(size_t i=0;i<rows.size();++i){
        auto row=trim(rows[i]);
        if(!row.empty()&&row[0]=='['){inside=row=="["+name+"]";if(inside)header=lastRow=long(i);continue;}
        if(!inside)continue;
        if(!row.empty())lastRow=long(i);
        auto eq=row.find('=');
        if(eq!=std::string::npos&&trim(row.substr(0,eq))==key){rows[i]=key+"="+value;header=-2;break;}}
    if(header==-1){if(!rows.empty()&&!trim(rows.back()).empty())rows.push_back("");rows.push_back("["+name+"]");rows.push_back(key+"="+value);}
    else if(header>=0)rows.insert(rows.begin()+lastRow+1,key+"="+value);   // after the section's last setting
    std::string out;for(auto& r:rows)out+=r+"\n";return out;
}
Choices remembered(const std::string& ini,const Policy& p){
    Choices c=defaults(p),kept{iniGet(ini,section(p.server),"offer_builds"),iniGet(ini,section(p.server),"offer_old")};
    if(oneOf(kept.offerBuilds,{"yes","no","ask"}))c.offerBuilds=kept.offerBuilds;
    if(oneOf(kept.offerOld,{"yes","no"}))c.offerOld=kept.offerOld;
    return c;
}
std::string remember(const std::string& ini,const std::string& server,const Choices& c){
    return iniSet(iniSet(ini,section(server),"offer_builds",c.offerBuilds),section(server),"offer_old",c.offerOld);
}
bool sendChoices(const Target& t,const Transport& send,const Choices& c,std::string& why){
    if(t.account.empty()){why="your Steam account is not known yet. Start Steam, then try again.";return false;}
    if(!validChoices(c)){why="these are not choices this server takes.";return false;}
    json::Value body=json::Value::object();body.set("player",t.account);body.set("offer_builds",c.offerBuilds);body.set("offer_old",c.offerOld);
    Http h;if(!call(t,send,"POST",base(t)+"/coop/choices",body.dump(),h)){why="the server did not answer.";return false;}
    if(h.status!=200){why=message(h)+".";return false;}
    return true;
}
Join join(const Target& given0,const Transport& send,const std::string& ini,const Choices& given,bool game){
    Join j;j.ini=ini;j.target=given0;Target& t=j.target;j.fetched=fetchPolicy(t,send,j.policy,&j.raw);
    if(j.fetched==Fetch::Old){
#ifndef MUTANT_STRAND_OLD_SERVER
        j.start=true;
#endif
        j.message=fetchText(j.fetched);return j;}
    if(j.fetched!=Fetch::Ok){j.message=fetchText(j.fetched);return j;}
    {std::string why;if(!identify(t,j.policy,int64_t(std::time(nullptr)),why)){j.message=why+" Your game was not started.";return j;}}
    j.keyed=j.policy.keys();
    j.choices=remembered(ini,j.policy);
    if(!given.offerBuilds.empty())j.choices.offerBuilds=given.offerBuilds;
    if(!given.offerOld.empty())j.choices.offerOld=given.offerOld;
    if(!validChoices(j.choices)){j.message="Share what I build: yes, no or ask; older strand: yes or no. Your game was not started.";return j;}
    j.ini=remember(ini,j.policy.server,j.choices);
    std::string why;
    if(!sendChoices(t,send,j.choices,why)){
        j.message="The server did not take your choices: "+why+" Your game was not started.";
#ifdef MUTANT_STRAND_SEND_FAIL
        j.start=true;
#endif
        return j;}
    if(j.keyed&&game&&!takePasses(t,send,j.passes,why)){
        j.message="The server gave your game no sign-in ticket: "+why+" Your game was not started.";
#ifdef MUTANT_STRAND_NO_PASSES
        j.start=true;
#endif
        return j;}
    // What the game is told about his builds here: a notice token for its mod. Without one the game starts all the same.
    if(game&&!takeNoticeToken(t,send,j.passes.noticeToken,why)){
#ifdef MUTANT_STRAND_NOTICE_FATAL
        j.message="The server gave your game no notice token: "+why;return j;
#endif
    }
    j.start=true;j.message="Your choices for this server: "+choicesText(j.choices)+".";
    return j;
}
std::string Me::text() const {
    if(!known)return "You have not played on this server yet.";
    return "Here you have: "+std::to_string(shared)+" shared, "+std::to_string(waitingYou)+" waiting for your answer, "+std::to_string(waitingOwner)+" waiting for the owner, "
           +std::to_string(held)+" held by the server's rules, "+std::to_string(kept)+" kept for you, "+std::to_string(remnants)+" remnants; "+std::to_string(likes)+" likes."
           +(member?" You are a member.":"");
}
std::string Object::text() const {
    std::string s=kind+"  area "+std::to_string(area)+"  ";
    s+=state=="waiting"?(waitingFor=="owner"?"waits for the owner":"waits for your answer"):state=="held"?"held: "+held+(waitingFor=="you"?" (then your answer)":""):state=="kept"?"kept for you":state=="remnant"?"remnant (nobody's now)":state;
    return s+"  "+std::to_string(likes)+(likes==1?" like":" likes")+"  ("+id+")";
}
bool fetchMe(const Target& t,const Transport& send,Me& me,std::string& why){
    if(t.account.empty()){why="Your Steam account is not known yet. Start Steam, then try again.";return false;}
    Http h;if(!call(t,send,"GET",base(t)+"/coop/me?player="+t.account,"",h)){why=fetchText(Fetch::Unreachable);return false;}
    if(h.status!=200){why=message(h);return false;}
    try{
        auto v=json::parseFile(h.body,MaxAnswer);if(!v.isObject())throw std::runtime_error("not an object");
        me=Me();me.known=v.get("known").boolean();me.member=v.get("member").boolean();me.id=v.get("player").string();
        me.offerBuilds=v.get("choices").get("offer_builds").string();me.offerOld=v.get("choices").get("offer_old").string();
        me.shared=v.get("shared").integer();me.waitingYou=v.get("waiting_you").integer();me.waitingOwner=v.get("waiting_owner").integer();
        for(auto& m:v.get("held").members())me.held+=m.second.integer();
        me.kept=v.get("kept").integer();me.remnants=v.get("remnants").integer();me.likes=v.get("likes").integer();
    }catch(const std::exception&){why=fetchText(Fetch::Bad);return false;}
    return true;
}
bool fetchMine(const Target& t,const Transport& send,std::vector<Object>& list,std::string& why){
    if(t.account.empty()){why="Your Steam account is not known yet. Start Steam, then try again.";return false;}
    Http h;if(!call(t,send,"GET",base(t)+"/coop/mine?player="+t.account,"",h)){why=fetchText(Fetch::Unreachable);return false;}
    if(h.status==404){why="This server has no list of your objects (a coop-world from before the launcher could ask).";return false;}
    if(h.status!=200){why=message(h);return false;}
    try{
        auto v=json::parseFile(h.body,MaxAnswer);if(!v.get("objects").isArray())throw std::runtime_error("no list");
        list.clear();
        for(auto& o:v.get("objects").items()){
            Object x;x.id=o.get("id").string();x.kind=o.get("kind").string();x.state=o.get("state").string();x.waitingFor=o.get("waiting_for").string();
            x.held=o.get("held_text").string();x.area=o.get("area").integer();x.likes=o.get("likes").integer();
            if(x.id.empty()||!plain(x.id+x.kind+x.state+x.held))throw std::runtime_error("bad object");
            list.push_back(x);}
    }catch(const std::exception&){why=fetchText(Fetch::Bad);return false;}
    return true;
}
bool act(const Target& t,const Transport& send,const std::string& call,const std::string& object,std::string& result){
    if(call!="offer"&&call!="withdraw")throw std::runtime_error("offer or withdraw");
    if(t.account.empty()){result="Your Steam account is not known yet. Start Steam, then try again.";return false;}
    json::Value body=json::Value::object();body.set("player",t.account);
    if(object.empty())body.set("all",true);else body.set("object",object);
    Http h;if(!strand::call(t,send,"POST",base(t)+"/coop/"+call,body.dump(),h)){result=fetchText(Fetch::Unreachable);return false;}
    if(h.status!=200){result="Refused: "+message(h)+".";return false;}
    try{
        auto v=json::parseFile(h.body,MaxAnswer);
        result=call=="offer"?"Offered: "+std::to_string(v.get("offered").integer())+"."
                            :"Withdrawn: "+std::to_string(v.get("removed").integer())+" removed, "+std::to_string(v.get("remnants").integer())+" stay as remnants.";
    }catch(const std::exception&){result=fetchText(Fetch::Bad);return false;}
    return true;
}
}
