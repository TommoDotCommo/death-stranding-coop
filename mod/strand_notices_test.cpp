// Strand notices (strand_notices.h) without a game: the server's answer read strictly; the words for each state; what
// is told from a run of answers (nothing for what was there before, once per object and state, nothing for shared,
// remnant or his own no, grouped per answer, "now shared" when it changes); when to read again (stop on a refused token,
// back off while the server is away); the reads on their own thread against a scripted server; and one real HTTP read
// over loopback.
#include "strand_notices.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace mod::strand;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"strand oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static std::string utf8(const std::u16string& s){std::string o;for(auto c:s)o+=c<0x80?char(c):'?';return o;}
static std::vector<std::string> told(Tracker& t,const Answer& a){std::vector<std::string> out;for(auto& l:t.take(a))out.push_back(utf8(l));return out;}
static Answer answer(std::vector<Item> items,const std::string& version,bool full=false,const std::string& name="North"){Answer a;a.name=name;a.version=version;a.full=full;a.items=std::move(items);return a;}
static void reading(){
    auto a=parse(R"({"format":1,"name":"North","version":"ab12-7","full":false,"objects":[{"id":"lAbc123","kind":"ladder","state":"held","why":"member"},{"id":"e9","kind":"cairn","state":"shared","why":""}]})");
    CHECK(a&&a->name=="North"&&a->version=="ab12-7"&&!a->full&&a->items.size()==2&&a->items[0].kind=="ladder"&&a->items[0].why=="member"&&a->items[1].state=="shared");
    CHECK(parse(R"({"format":1,"name":"","version":"x-0","full":true,"objects":[]})").has_value());
    // What applies to his older strand: yes or no, nothing from a server that does not say.
    CHECK(a->offerOld.empty()&&parse(R"({"format":1,"name":"","version":"x-0","full":true,"objects":[],"offer_old":"yes"})")->offerOld=="yes");
    CHECK(parse(R"({"format":1,"name":"","version":"x-0","full":true,"objects":[],"offer_old":"no"})")->offerOld=="no");
    for(const char* bad:{"","[]","{}",R"({"format":2,"name":"","version":"x","full":true,"objects":[]})",R"({"format":1,"version":"x","full":true,"objects":[]})",
                         R"({"format":1,"name":"","version":"x","full":"yes","objects":[]})",R"({"format":1,"name":"","version":"x","full":true,"objects":{}})",
                         R"({"format":1,"name":"","version":"x","full":true,"objects":[{"id":"l-1","kind":"ladder","state":"held","why":""}]})",
                         R"({"format":1,"name":"","version":"x","full":true,"objects":[{"id":"l1","kind":"ladder","state":"lost","why":""}]})",
                         R"({"format":1,"name":"","version":"x","full":true,"objects":[7]})",R"({"format":1,"name":"","version":"x","full":true,"objects":[],"offer_old":"maybe"})",
                         R"({"format":1,"name":"","version":"x","full":true,"objects":[],"offer_old":true})"})
        CHECK(!parse(bad));
    std::string many=R"({"format":1,"name":"","version":"x","full":true,"objects":[)";
    for(size_t i=0;i<=MaxItems;++i)many+=std::string(i?",":"")+R"({"id":"l)"+std::to_string(i+10)+R"(","kind":"ladder","state":"held","why":"member"})";
    CHECK(!parse(many+"]}"));
}
static void words(){
    CHECK(utf8(sentence("held","member","ladder",1,"North"))=="Your ladder is not shared on North: only members' builds are shared here.");
    CHECK(utf8(sentence("held","build","cairn",1,"North"))=="Your cairn is not shared on North: no builds are shared here.");
    CHECK(utf8(sentence("held","limit","post",1,"North"))=="Your post is not shared on North: you have reached the limit of shared builds here.");
    CHECK(utf8(sentence("kept","choice","ladder",1,"North"))=="Your ladder is kept for you on North (your choice).");
    CHECK(utf8(sentence("kept","owner","ladder",2,"North"))=="2 of your builds are kept for you on North: the owner did not accept them.");
    CHECK(utf8(sentence("kept","server","ladder",1,"North"))=="Your ladder is kept for you on North: builds are not shared here.");
    CHECK(utf8(sentence("kept","","ladder",1,"North"))=="Your ladder is kept for you on North.");
    CHECK(utf8(sentence("waiting","owner","bridge",1,"North"))=="Your bridge is waiting for the owner's review on North.");
    CHECK(utf8(sentence("waiting","you","ladder",1,"North"))=="Your ladder is waiting for your answer on North: offer it in the launcher.");
    CHECK(utf8(sentence("shared","","ladder",1,"North"))=="Your ladder is now shared on North.");
    CHECK(utf8(sentence("held","member","ladder",3,"North"))=="3 of your builds are not shared on North: only members' builds are shared here.");
    // No name: this server. A kind the mod has no word for (the type letter, or something else): a build. Markup never.
    CHECK(utf8(sentence("held","member","ladder",1,""))=="Your ladder is not shared on this server: only members' builds are shared here.");
    CHECK(utf8(sentence("held","member","q",1,"North"))=="Your build is not shared on North: only members' builds are shared here.");
    CHECK(utf8(sentence("held","member","<b>",1,"<i>Nor{th}"))=="Your build is not shared on iNorth: only members' builds are shared here.");
    CHECK(sentence("remnant","","ladder",1,"North").empty()&&sentence("lost","","ladder",1,"North").empty());
    for(const char* state:{"held","kept","waiting","shared"})CHECK(sentence(state,"member","chiralbridge",999,"WWWWWWWWWWWW").size()<=160);   // the info log's limit
}
static void tracking(){
    Tracker t;
    // What was there before the game started: nothing told, whatever its state.
    CHECK(told(t,answer({{"l1","ladder","held","member"},{"l2","ladder","kept","choice"},{"e1","cairn","shared",""}},"v-3",true)).empty()&&t.since()=="v-3"&&t.known()==3);
    // A new held ladder: one line. The same again (a full answer after a restart): nothing.
    CHECK(told(t,answer({{"l3","ladder","held","member"}},"v-4"))==std::vector<std::string>({"Your ladder is not shared on North: only members' builds are shared here."})&&t.since()=="v-4");
    CHECK(told(t,answer({{"l1","ladder","held","member"},{"l2","ladder","kept","choice"},{"l3","ladder","held","member"},{"e1","cairn","shared",""}},"w-0",true)).empty());
    // Shared from the start, a remnant (his withdrawal), his own no: nothing.
    CHECK(told(t,answer({{"e2","cairn","shared",""}},"v-5")).empty());
    CHECK(told(t,answer({{"e1","cairn","remnant",""}},"v-6")).empty());
    CHECK(told(t,answer({{"w1","ladder","waiting","you"}},"v-7"))==std::vector<std::string>({"Your ladder is waiting for your answer on North: offer it in the launcher."}));
    CHECK(told(t,answer({{"w1","ladder","kept","choice"}},"v-8")).empty());
    // Changes: held to shared ("now shared"), waiting to kept by the owner, a reason that changed.
    CHECK(told(t,answer({{"l3","ladder","shared",""},{"l1","ladder","held","limit"}},"v-9"))==
          std::vector<std::string>({"Your ladder is now shared on North.","Your ladder is not shared on North: you have reached the limit of shared builds here."}));
    CHECK(told(t,answer({{"w2","bridge","waiting","owner"}},"v-10")).size()==1&&told(t,answer({{"w2","bridge","kept","owner"}},"v-11"))==
          std::vector<std::string>({"Your bridge is kept for you on North: the owner did not accept it."}));
    // Many in one answer: one line per kind of news, the kind named only when all are of one kind.
    CHECK(told(t,answer({{"m1","ladder","held","member"},{"m2","cairn","held","member"},{"m3","ladder","held","member"},{"m4","post","waiting","owner"},{"m5","post","waiting","owner"}},"v-12"))==
          std::vector<std::string>({"3 of your builds are not shared on North: only members' builds are shared here.","2 of your builds are waiting for the owner's review on North."}));
    CHECK(told(t,answer({{"m1","ladder","shared",""},{"m2","cairn","shared",""}},"v-13"))==std::vector<std::string>({"2 of your builds are now shared on North."}));
    // A shared one that becomes shared again (a remnant listed as shared never happens, but a known shared one) tells nothing.
    CHECK(told(t,answer({{"m1","ladder","shared",""}},"v-14")).empty());
}
static void schedule(){
    unsigned f=0;
    CHECK(delayAfter(200,true,f)==PollMs&&f==0);
    CHECK(delayAfter(0,false,f)==10000&&delayAfter(0,false,f)==20000&&delayAfter(500,false,f)==30000&&delayAfter(200,false,f)==30000&&f==4);
    CHECK(delayAfter(200,true,f)==PollMs&&f==0);
    CHECK(delayAfter(403,false,f)==0&&delayAfter(401,false,f)==0);
}
static void polling(){
    // A scripted server: a first answer, a held ladder, nothing new, a refusal; the reads go on from each version and stop.
    std::vector<std::string> targets;std::mutex m;
    std::vector<Fetched> script={{200,R"({"format":1,"name":"North","version":"e-1","full":true,"objects":[{"id":"l1","kind":"ladder","state":"held","why":"member"}]})"},
                                 {0,""},{200,"not json"},
                                 {200,R"({"format":1,"name":"North","version":"e-2","full":false,"objects":[{"id":"l2","kind":"ladder","state":"held","why":"member"}]})"},
                                 {200,R"({"format":1,"name":"North","version":"e-2","full":false,"objects":[]})"},{403,"{}"}};
    std::atomic<size_t> at{0};
    {
        Poller p("/w77",[&](const std::string& target){std::lock_guard<std::mutex> g(m);targets.push_back(target);size_t i=at++;return i<script.size()?script[i]:Fetched{200,script[4].body};},
                 [](int s,bool good,unsigned& n){return delayAfter(s,good,n)?uint64_t(1):0;});
        for(int i=0;i<200&&!p.counters().refused;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto k=p.counters();
        CHECK(k.refused&&k.reads==6&&k.answers==3&&k.failures==3&&k.told==1&&k.known==2);
        auto lines=p.lines();CHECK(lines.size()==1&&utf8(lines[0])=="Your ladder is not shared on North: only members' builds are shared here."&&p.lines().empty());
        auto logs=p.logs();bool refused=false;for(auto& l:logs)refused=refused||l.find("refused this game's notice token")!=std::string::npos;CHECK(refused);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(p.counters().reads==6);   // never again
    }
    CHECK(targets==std::vector<std::string>({"/w77/coop/notices","/w77/coop/notices?since=e-1","/w77/coop/notices?since=e-1","/w77/coop/notices?since=e-1","/w77/coop/notices?since=e-2","/w77/coop/notices?since=e-2"}));
}
static void older(){
    // The server's last word on his older strand: each answer's, kept while the server is away, gone with a refused token
    // and with an answer that does not say.
    auto body=[](const char* version,const char* old){return std::string(R"({"format":1,"name":"North","version":")")+version+R"(","full":true,"objects":[])"+
                                                            (old?std::string(R"(,"offer_old":")")+old+"\"":std::string())+"}";};
    for(bool refuse:{false,true}){
        std::vector<Fetched> script={{200,body("e-1","yes")},{0,""},{0,""}};
        if(refuse)script.push_back({403,"{}"});else script.push_back({200,body("e-2",nullptr)});
        std::atomic<size_t> at{0};std::atomic<bool> hold{true};
        Poller p("",[&](const std::string&){size_t i=at++;while(hold&&i==script.size()-1)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                     return i<script.size()?script[i]:Fetched{0,""};},[](int s,bool good,unsigned& n){return delayAfter(s,good,n)?uint64_t(1):0;});
        for(int i=0;i<200&&at<script.size();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CHECK(p.offerOld()=="yes");   // the first answer's, through two reads that failed
        hold=false;
        for(int i=0;i<200&&p.counters().reads<script.size();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(p.offerOld().empty()&&p.counters().refused==refuse);
    }
}
static void http(){
    // One real read over loopback: the header carries the token, the answer is taken only whole.
    int listener=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7f000001);
    bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof a);listen(listener,4);socklen_t size=sizeof a;getsockname(listener,reinterpret_cast<sockaddr*>(&a),&size);
    const uint16_t port=ntohs(a.sin_port);
    auto serve=[listener](std::string reply,std::string* request){
        return std::thread([listener,reply,request]{int c=accept(listener,nullptr,nullptr);char b[4096];std::string in;
            while(in.find("\r\n\r\n")==std::string::npos){auto n=recv(c,b,sizeof b,0);if(n<=0)break;in.append(b,size_t(n));}
            if(request){*request=in;}
            send(c,reply.data(),reply.size(),0);close(c);});};
    std::string request;
    auto th=serve("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}",&request);
    auto got=get(0x7f000001,port,"/w77/coop/notices?since=e-1","tok123",2000);th.join();
    CHECK(got.status==200&&got.body=="{}"&&request.find("GET /w77/coop/notices?since=e-1 HTTP/1.1\r\n")==0&&request.find("\r\nX-Coop-Token: tok123\r\n")!=std::string::npos);
    th=serve("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n{}",nullptr);got=get(0x7f000001,port,"/","t",2000);th.join();CHECK(got.status==0);       // cut short
    th=serve("HTTP/1.1 200 OK\r\n\r\n{}",nullptr);got=get(0x7f000001,port,"/","t",2000);th.join();CHECK(got.status==0);                           // no length
    th=serve("HTTP/1.1 403 Forbidden\r\nContent-Length: 2\r\n\r\n{}",nullptr);got=get(0x7f000001,port,"/","t",2000);th.join();CHECK(got.status==403);
    close(listener);
    CHECK(get(0x7f000001,port,"/","t",500).status==0);   // nobody there
}
int main(){
    reading();words();tracking();schedule();polling();older();http();
    if(failures){std::fprintf(stderr,"strand oracle: %u failures\n",failures);return 1;}
    std::puts("strand notices tests passed");return 0;
}
