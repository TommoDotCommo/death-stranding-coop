// What happens to the world's enemies in group play (groupevents.h), without a game: the server's answer, the body,
// and the courier's order (messages first, then reads since the last version).
#include "groupevents.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace mod::groupevents;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"groupevents oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
int main(){
    auto a=parse(R"({"format":1,"on":true,"version":"e-4","full":false,"wait_ms":2000,"events":[
        {"kind":"mule","key":4503599627370495,"value":0},{"kind":"camp","key":7,"value":100},{"kind":"tank","key":1,"value":0},{"kind":"gazer","key":"x","value":1}]})");
    CHECK(a.read&&a.on&&!a.full&&a.version=="e-4"&&a.waitMs==2000&&a.events.size()==2);
    if(a.events.size()==2)CHECK(a.events[0]==(Event{"mule",4503599627370495LL,0})&&a.events[1]==(Event{"camp",7,100}));
    CHECK(!parse("{").read&&!parse(R"({"format":2,"version":"x"})").read&&!parse(R"({"format":1,"version":"x","on":false})").on);
    CHECK(body(Event{"mule",5,2})==R"({"kind":"mule","key":5,"value":2})");
    unsigned f=0;Answer bad;CHECK(delayAfter(bad,f)==FailWaitMs&&delayAfter(a,f)==2000&&f==0);
    std::vector<std::string> posted,since;std::mutex m;
    {
        Courier c([&](const std::string& s){std::lock_guard<std::mutex> g(m);since.push_back(s);return Result{200,R"({"format":1,"version":"e-)"+std::to_string(since.size())+R"(","wait_ms":1000,"events":[]})"};},
                  [&](const std::string& b){std::lock_guard<std::mutex> g(m);posted.push_back(b);return Result{200,R"({"format":1})"};},[](const std::string&){});
        c.send({Event{"mule",1,0},Event{"camp",2,100}});
        std::this_thread::sleep_for(std::chrono::milliseconds(1300));
        auto reads=c.reads();
        std::lock_guard<std::mutex> g(m);
        CHECK(posted.size()==2&&posted[0].find("\"mule\"")!=std::string::npos&&posted[1].find("\"camp\"")!=std::string::npos);
        CHECK(since.size()>=2&&since[0].empty()&&since[1]=="e-1"&&!reads.empty());
    }
    if(failures){std::fprintf(stderr,"%u groupevents oracle failures\n",failures);return 1;}
    std::puts("groupevents oracles passed");return 0;
}
