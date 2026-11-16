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
#include "strand_notices.h"
#include "nametag.h"
#include "../world/json.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
namespace mod::strand {
namespace {
bool letters(const std::string& s,size_t least,size_t most,bool digits){
    if(s.size()<least||s.size()>most)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(digits&&((c>='A'&&c<='Z')||(c>='0'&&c<='9')))))return false;
    return true;}
bool oneOf(const std::string& s,std::initializer_list<const char*> allowed){for(auto a:allowed)if(s==a)return true;return false;}
std::u16string ascii(const std::string& s){return std::u16string(s.begin(),s.end());}
}
std::optional<Answer> parse(const std::string& body){
    try{
        auto v=json::parseFile(body,MaxAnswerBytes);
        if(!v.isObject()||v.get("format").integer()!=1||!v.get("name").isString()||!v.get("version").isString()||v.get("version").string().size()>64
           ||v.get("full").kind()!=json::Value::Bool||!v.get("objects").isArray()||v.get("objects").items().size()>MaxItems)return std::nullopt;
        Answer a;a.name=v.get("name").string();a.version=v.get("version").string();a.full=v.get("full").boolean();
        if(v.has("offer_old")){
            a.offerOld=v.get("offer_old").string();
#ifndef MUTANT_STRAND_OLD_PARSE
            if(!oneOf(a.offerOld,{"yes","no"}))return std::nullopt;
#endif
        }
        for(auto& o:v.get("objects").items()){
            Item i{o.get("id").string(),o.get("kind").string(),o.get("state").string(),o.get("why").string()};
            if(!o.isObject()||!letters(i.id,2,64,true)||i.kind.size()>32||i.why.size()>16)return std::nullopt;
#ifndef MUTANT_STRAND_PARSE
            if(!oneOf(i.state,{"shared","held","kept","waiting","remnant"}))return std::nullopt;
#endif
            a.items.push_back(std::move(i));}
        return a;
    }catch(const std::exception&){return std::nullopt;}
}
std::u16string sentence(const std::string& state,const std::string& why,const std::string& kind,size_t count,const std::string& server){
    auto where=mod::nametag::fromUtf8(server);
    while(!where.empty()&&where.back()==u' ')where.pop_back();
    if(where.empty())where=u"this server";
    // "Your ladder is" for one object, "3 of your builds are" for more; a kind the mod has no word for is a build.
    const bool one=count==1;
    const std::u16string subject=one?u"Your "+ascii(letters(kind,2,16,false)?kind:"build"):ascii(std::to_string(count))+u" of your builds",
                         be=one?u" is":u" are",it=one?u"it":u"them";
    if(state=="shared")return subject+be+u" now shared on "+where+u".";
    if(state=="held"){
        const std::u16string head=subject+be+u" not shared on "+where;
        if(why=="member")return head+u": only members' builds are shared here.";
        if(why=="build")return head+u": no builds are shared here.";
        if(why=="limit")return head+u": you have reached the limit of shared builds here.";
        return head+u".";}
    if(state=="kept"){
        const std::u16string head=subject+be+u" kept for you on "+where;
        if(why=="choice")return head+u" (your choice).";
        if(why=="owner")return head+u": the owner did not accept "+it+u".";
        if(why=="server")return head+u": builds are not shared here.";
        return head+u".";}
    if(state=="waiting"){
        if(why=="you")return subject+be+u" waiting for your answer on "+where+u": offer "+it+u" in the launcher.";
        return subject+be+u" waiting for the owner's review on "+where+u".";}
    return {};
}
std::vector<std::u16string> Tracker::take(const Answer& a){
#ifndef MUTANT_STRAND_SINCE
    since_=a.version;
#endif
    std::vector<std::u16string> out;
    if(first_){
        first_=false;
#ifndef MUTANT_STRAND_FIRST
        for(auto& i:a.items)seen_[i.id]=i.state+"/"+i.why;
        return out;
#endif
    }
    // Each kind of news once per answer, in the order it first appears: its objects, and the kind of the first one.
    struct News { std::string state,why,kind;size_t count=0; };std::vector<News> news;
    auto tell=[&](const Item& i,const std::string& state){
        for(auto& n:news)if(n.state==state&&n.why==(state=="shared"?"":i.why)){
#ifndef MUTANT_STRAND_GROUP
            ++n.count;if(n.kind!=i.kind)n.kind.clear();return;
#endif
        }
        news.push_back({state,state=="shared"?"":i.why,i.kind,1});};
    for(auto& i:a.items){
        const std::string now=i.state+"/"+i.why;
        auto seen=seen_.find(i.id);
        if(seen==seen_.end()){
            seen_[i.id]=now;
#ifndef MUTANT_STRAND_SHARED
            if(i.state=="shared")continue;
#endif
            if(i.state=="remnant")continue;
            tell(i,i.state);continue;}
        const std::string before=seen->second;
#ifndef MUTANT_STRAND_ONCE
        if(before==now)continue;
#endif
        seen->second=now;
        if(i.state=="shared"){if(before.compare(0,7,"shared/")!=0&&before.compare(0,8,"remnant/")!=0)tell(i,"shared");continue;}
        if(i.state=="remnant")continue;
#ifndef MUTANT_STRAND_CHOICE
        if(i.state=="kept"&&i.why=="choice")continue;   // his own no, in the launcher
#endif
        tell(i,i.state);}
    for(auto& n:news){auto line=sentence(n.state,n.why,n.count==1?n.kind:"",n.count,a.name);if(!line.empty())out.push_back(line);}
    return out;
}
uint64_t delayAfter(int status,bool good,unsigned& failures){
#ifndef MUTANT_STRAND_STOP
    if(status==401||status==403)return 0;   // the token is refused: a new one comes only with a new start from the launcher
#endif
    if(status==200&&good){failures=0;return PollMs;}
    if(failures<16)++failures;
#ifndef MUTANT_STRAND_BACKOFF
    return std::min<uint64_t>(BackoffMs,PollMs<<std::min(failures,3u));
#else
    return PollMs;
#endif
}
#ifdef _WIN32
namespace {using Sock=SOCKET;const Sock NoSock=INVALID_SOCKET;void closeSock(Sock s){closesocket(s);}
void blocking(Sock s,bool yes){u_long v=yes?0:1;ioctlsocket(s,FIONBIO,&v);}bool connecting(){return WSAGetLastError()==WSAEWOULDBLOCK;}
void timeouts(Sock s,unsigned ms){DWORD t=ms;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&t),sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&t),sizeof t);}}
#else
namespace {using Sock=int;const Sock NoSock=-1;void closeSock(Sock s){close(s);}
void blocking(Sock s,bool yes){int f=fcntl(s,F_GETFL,0);fcntl(s,F_SETFL,yes?(f&~O_NONBLOCK):(f|O_NONBLOCK));}bool connecting(){return errno==EINPROGRESS;}
void timeouts(Sock s,unsigned ms){timeval t{long(ms/1000),long(ms%1000)*1000};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);}}
#endif
Fetched get(uint32_t address,uint16_t port,const std::string& target,const std::string& token,unsigned ms){
    Fetched out;
    Sock s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==NoSock)return out;
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(address);
    blocking(s,false);
    if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0){
        if(!connecting()){closeSock(s);return out;}
        fd_set writable,failed;FD_ZERO(&writable);FD_ZERO(&failed);FD_SET(s,&writable);FD_SET(s,&failed);timeval wait{long(ms/1000),long(ms%1000)*1000};
        int error=0;socklen_t size=sizeof error;
        if(select(int(s)+1,nullptr,&writable,&failed,&wait)<=0||!FD_ISSET(s,&writable)||getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)!=0||error){closeSock(s);return out;}}
    blocking(s,true);timeouts(s,ms);
    char host[32];unsigned char b[4]={uint8_t(address>>24),uint8_t(address>>16),uint8_t(address>>8),uint8_t(address)};
    std::snprintf(host,sizeof host,"%u.%u.%u.%u:%u",b[0],b[1],b[2],b[3],unsigned(port));
    const std::string request="GET "+target+" HTTP/1.1\r\nHost: "+host+"\r\nX-Coop-Token: "+token+"\r\nConnection: close\r\n\r\n";
#ifdef _WIN32
    const int flags=0;
#else
    const int flags=MSG_NOSIGNAL;   // a server that went away is a failed read, never a signal to the process
#endif
    for(size_t at=0;at<request.size();){auto n=send(s,request.data()+at,int(request.size()-at),flags);if(n<=0){closeSock(s);return out;}at+=size_t(n);}
    std::string in;char chunk[8192];
    while(in.size()<=MaxAnswerBytes+16*1024){auto n=recv(s,chunk,sizeof chunk,0);if(n<=0)break;in.append(chunk,size_t(n));}
    closeSock(s);
    // The status line and Content-Length (the server's answers always have one); the body is exactly that long.
    auto end=in.find("\r\n\r\n");
    if(end==std::string::npos||end>16*1024||in.compare(0,9,"HTTP/1.1 ")!=0||in.size()<12)return out;
    int status=0;for(size_t i=9;i<12;++i){if(in[i]<'0'||in[i]>'9')return out;status=status*10+(in[i]-'0');}
    size_t length=0;bool have=false;
    for(size_t at=in.find("\r\n")+2;at<end;){
        size_t next=in.find("\r\n",at);if(next==std::string::npos||next>end)next=end;
        std::string row=in.substr(at,next-at);at=next+2;
        auto colon=row.find(':');if(colon==std::string::npos)continue;
        std::string name=row.substr(0,colon),value=row.substr(colon+1);
        for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        while(!value.empty()&&value.front()==' ')value.erase(0,1);
        if(name=="content-length"){if(have||value.empty()||value.size()>9||value.find_first_not_of("0123456789")!=std::string::npos)return out;length=std::stoul(value);have=true;}}
    if(!have||length>MaxAnswerBytes||in.size()-end-4!=length)return out;
    out.status=status;out.body=in.substr(end+4);return out;
}
Poller::Poller(std::string base,Fetch fetch,Schedule schedule):base_(std::move(base)),fetch_(std::move(fetch)),schedule_(schedule?std::move(schedule):Schedule(delayAfter)){thread_=std::thread([this]{run();});}
Poller::~Poller(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
std::vector<std::u16string> Poller::lines(){std::lock_guard<std::mutex> g(lock_);std::vector<std::u16string> out(lines_.begin(),lines_.end());lines_.clear();return out;}
std::vector<std::string> Poller::logs(){std::lock_guard<std::mutex> g(lock_);std::vector<std::string> out(logs_.begin(),logs_.end());logs_.clear();return out;}
Poller::Counters Poller::counters() const {std::lock_guard<std::mutex> g(lock_);return counters_;}
std::string Poller::offerOld() const {std::lock_guard<std::mutex> g(lock_);return offerOld_;}
void Poller::run(){
    unsigned failures=0,answers=0;bool wasDown=false;
    while(true){
        {std::lock_guard<std::mutex> g(lock_);if(stop_)return;}
        const std::string target=base_+"/coop/notices"+(tracker_.since().empty()?std::string():"?since="+tracker_.since());
        auto got=fetch_(target);
        std::optional<Answer> answer;if(got.status==200)answer=parse(got.body);
        std::vector<std::u16string> told;
        if(answer){const bool first=!answers;++answers;told=tracker_.take(*answer);
            if(first){std::lock_guard<std::mutex> g(lock_);logs_.push_back("STRAND first answer: "+std::to_string(answer->items.size())+" objects of this player on the server\n");}}
        const uint64_t wait=schedule_(got.status,answer.has_value(),failures);
        {
            std::lock_guard<std::mutex> g(lock_);
            ++counters_.reads;if(answer)++counters_.answers;else ++counters_.failures;counters_.known=tracker_.known();counters_.told+=unsigned(told.size());
            if(answer)offerOld_=answer->offerOld;
            for(auto& line:told){if(lines_.size()>=16)lines_.pop_front();lines_.push_back(line);}
            if(!told.empty())logs_.push_back("STRAND told "+std::to_string(told.size())+" line(s)\n");
            if(!answer&&!wasDown&&wait)logs_.push_back(got.status?"STRAND the server answered "+std::to_string(got.status)+" or an answer this mod cannot read; trying again\n":std::string("STRAND the server did not answer; trying again\n"));
            wasDown=!answer;
#ifndef MUTANT_STRAND_OLD_REFUSED
            if(!wait)offerOld_.clear();   // no word from the server any more: nothing is offered on an old yes
#endif
            if(!wait){counters_.refused=true;logs_.push_back("STRAND the server refused this game's notice token: no build notices until the game is started again from the launcher\n");return;}
            while(logs_.size()>64)logs_.pop_front();
        }
        std::unique_lock<std::mutex> l(lock_);
        if(wake_.wait_for(l,std::chrono::milliseconds(wait),[this]{return stop_;}))return;
    }
}
}
