#include "groupevents.h"
#include "../world/json.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace mod::groupevents {
Answer parse(const std::string& text){
    Answer a;
    try{
        auto d=json::parse(text);
        if(d.get("format").integer()!=1||!d.get("version").isString())return a;
        a.version=d.get("version").string();a.on=d.get("on").boolean(true);a.orders=d.get("orders").boolean(true);a.full=d.get("full").boolean();
        a.waitMs=uint64_t(std::clamp<int64_t>(d.get("wait_ms").integer(),int64_t(WaitMinMs),int64_t(WaitMaxMs)));
        for(auto& e:d.get("events").items()){
            Event v;v.kind=e.get("kind").string();
            if((v.kind!="mule"&&v.kind!="camp"&&v.kind!="gazer"&&v.kind!="level"&&v.kind!="rain"&&v.kind!="order"&&v.kind!="unit")||!e.get("key").isInt()||!e.get("value").isInt())continue;
            v.key=e.get("key").integer();v.value=e.get("value").integer();
            if(v.kind=="order"||v.kind=="unit"){
                if(!e.get("party").isInt()||e.get("party").integer()<=0)continue;
                v.party=e.get("party").integer();v.age=std::max<int64_t>(0,e.get("age").integer());
                bool fine=true;
                auto numbers=[&](const char* k,std::vector<double>& out){for(auto& x:e.get(k).items()){const double n=x.real(std::nan(""));if(!std::isfinite(n))fine=false;else out.push_back(n);}};
                numbers("place",v.place);numbers("data",v.data);
                if(!fine||(!v.place.empty()&&v.place.size()!=3)||v.data.size()>3)continue;}
            a.events.push_back(v);}
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
int64_t levelKey(const std::string& online){uint64_t h=1469598103934665603ull;for(unsigned char c:online){h^=c;h*=1099511628211ull;}return int64_t(h&((uint64_t(1)<<52)-1));}
std::string body(const Event& e){json::Value v=json::Value::object();v.set("kind",e.kind);v.set("key",e.key);v.set("value",e.value);
    if(e.party){v.set("party",e.party);
        auto numbers=[&](const char* k,const std::vector<double>& in){if(in.empty())return;json::Value a=json::Value::array();for(double x:in)a.push(std::floor(x)==x&&std::fabs(x)<9e15?json::Value(int64_t(x)):json::Value(x));v.set(k,a);};
        numbers("place",e.place);numbers("data",e.data);}
    return v.dump();}
uint64_t delayAfter(const Answer& a,unsigned& failures){
    if(a.read){failures=0;return a.waitMs;}
    failures=std::min(failures+1,6u);return std::min<uint64_t>(FailWaitMs<<(failures-1),WaitMaxMs);
}
Courier::Courier(Get get,Post post,std::function<void(const std::string&)> log):get_(std::move(get)),post_(std::move(post)),log_(std::move(log)){thread_=std::thread([this]{run();});}
Courier::~Courier(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
void Courier::send(std::vector<Event> events){
    if(events.empty())return;
    {std::lock_guard<std::mutex> g(lock_);for(auto& e:events)out_.push_back(std::move(e));}
    wake_.notify_all();
}
std::vector<Answer> Courier::reads(){std::lock_guard<std::mutex> g(lock_);std::vector<Answer> out;out.swap(reads_);return out;}
void Courier::run(){
    unsigned failures=0;std::string since;bool told=false;auto next=std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        while(!out_.empty()&&!stop_){
            Event e=out_.front();out_.pop_front();g.unlock();
            Result r;try{r=post_(body(e));}catch(const std::exception&){r=Result{};}
            sent_.fetch_add(1,std::memory_order_relaxed);
            if(r.status!=200)log_("ENEMYSRV "+e.kind+" refused: status "+std::to_string(r.status)+"\n");
            g.lock();}
        if(stop_)break;
        if(std::chrono::steady_clock::now()>=next){
            g.unlock();
            Result r;try{r=get_(since);}catch(const std::exception&){r=Result{};}
            Answer a;if(r.status==200)a=parse(r.body);
            if(a.read&&!told){told=true;log_("ENEMYSRV the server lists "+std::to_string(a.events.size())+" events of other players"+(a.on?"":" (enemies are each player's own there)")+"\n");}
            const uint64_t wait=delayAfter(a,failures);
            g.lock();
            if(a.read){if(reads_.size()>=16){reads_.clear();since.clear();}else{since=a.version;reads_.push_back(std::move(a));}}
            next=std::chrono::steady_clock::now()+std::chrono::milliseconds(wait);}
        wake_.wait_until(g,next,[&]{return stop_||!out_.empty();});
    }
}
}
