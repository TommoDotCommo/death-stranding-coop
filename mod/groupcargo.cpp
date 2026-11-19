#include "groupcargo.h"
#include "../world/json.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace mod::groupcargo {
Answer parse(const std::string& text){
    Answer a;
    try{
        auto d=json::parse(text);
        if(d.get("format").integer()!=1||!d.get("version").isString())return a;
        a.version=d.get("version").string();a.full=d.get("full").boolean();a.on=d.get("on").boolean(true);a.more=d.get("more").boolean();
        a.waitMs=uint64_t(std::clamp<int64_t>(d.get("wait_ms").integer(),int64_t(WaitMinMs),int64_t(WaitMaxMs)));
        for(auto& c:d.get("crates").items()){
            Crate k;k.id=c.get("id").string();k.hash=uint32_t(uint64_t(c.get("hash").integer()));
            k.at={c.get("x").real(),c.get("y").real(),c.get("z").real()};k.yaw=c.get("yaw").real();k.area=c.get("area").integer();
            if(k.id.empty()||k.id.size()>64||!k.hash||!std::isfinite(k.at[0])||!std::isfinite(k.at[1])||!std::isfinite(k.at[2]))continue;
            a.crates.push_back(k);}
        for(auto& v:d.get("removed").items())if(v.isString()&&!v.string().empty()&&v.string().size()<=64)a.removed.push_back(v.string());
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
uint64_t delayAfter(const Answer& a,unsigned& failures){
    if(a.read){failures=0;return a.waitMs;}
    failures=std::min(failures+1,6u);return std::min<uint64_t>(FailWaitMs<<(failures-1),WaitMaxMs);
}
std::string body(const Say& s){
    json::Value v=json::Value::object();
    if(s.take){v.set("op","take");v.set("id",s.id);}
    else{v.set("op","place");v.set("hash",int64_t(s.hash));v.set("x",s.at[0]);v.set("y",s.at[1]);v.set("z",s.at[2]);v.set("area",s.area);}
    return v.dump();
}
std::vector<Say> Plan::placed(const cargo::Placement& p,int64_t area){
    if(!p.id||byLocal_.count(p.id)||inFlight_.count(p.id))return {};
    inFlight_.insert(p.id);
    Say s;s.local=p.id;s.hash=p.hash;s.at=p.position;s.area=area;return {s};
}
std::vector<Say> Plan::taken(const cargo::Taken& t){
    if(!t.id)return {};
    if(auto f=byLocal_.find(t.id);f!=byLocal_.end()){
        Say s;s.take=true;s.id=f->second;byServer_.erase(f->second);aliases_.erase(t.id);byLocal_.erase(f);return {s};}
#ifndef MUTANT_GROUPCARGO_LOSE_EARLY_TAKE
    if(inFlight_.count(t.id))takeWhenAnswered_.insert(t.id);   // picked up again before the server answered the set-down
#endif
    return {};
}
std::vector<Say> Plan::answered(uint64_t local,const std::string& id){
    inFlight_.erase(local);
    if(id.empty()){takeWhenAnswered_.erase(local);return {};}
    if(takeWhenAnswered_.erase(local)){Say s;s.take=true;s.id=id;return {s};}
    byServer_[id]=local;byLocal_[local]=id;return {};
}
void Plan::take(const Answer& a){
    if(!a.read)return;
    if(a.full){
        std::set<std::string> listed;for(auto& c:a.crates)listed.insert(c.id);
        // Others' crates made here that the server no longer lists: gone (own ones are never listed back to us).
        for(auto it=byServer_.begin();it!=byServer_.end();){
            if(!listed.count(it->first)&&aliases_.count(it->second)){gone_.push_back(it->second);aliases_.erase(it->second);byLocal_.erase(it->second);it=byServer_.erase(it);}
            else ++it;}
        waiting_.clear();}
    for(auto& c:a.crates)if(!byServer_.count(c.id))waiting_[c.id]=c;
    for(auto& id:a.removed){
        waiting_.erase(id);
        if(auto f=byServer_.find(id);f!=byServer_.end()){
#ifndef MUTANT_GROUPCARGO_KEEP_TAKEN
            gone_.push_back(f->second);
#endif
            aliases_.erase(f->second);byLocal_.erase(f->second);byServer_.erase(f);}}
    if(!a.on)waiting_.clear();
}
Plan::Step Plan::step(const std::array<double,3>& porter,bool porterKnown){
    Step s;s.gone.swap(gone_);
    std::vector<std::pair<double,std::string>> order;
    for(auto& w:waiting_){
        double d=0;
        if(porterKnown){d=std::hypot(w.second.at[0]-porter[0],w.second.at[1]-porter[1]);
#ifndef MUTANT_GROUPCARGO_FAR
            if(d>NearMetres)continue;
#endif
        }
        order.push_back({d,w.first});}
    std::sort(order.begin(),order.end());
    for(auto& o:order){
        if(s.make.size()>=MakesPerStep)break;
        auto& c=waiting_.at(o.second);
        cargo::Placement p;p.hash=c.hash;p.position=c.at;p.id=nextAlias_++;
        byServer_[c.id]=p.id;byLocal_[p.id]=c.id;aliases_.insert(p.id);s.make.push_back(p);waiting_.erase(o.second);}
    return s;
}
Courier::Courier(Get get,Post post,std::function<void(const std::string&)> log):get_(std::move(get)),post_(std::move(post)),log_(std::move(log)){thread_=std::thread([this]{run();});}
Courier::~Courier(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
void Courier::send(std::vector<Say> says){
    if(says.empty())return;
    {std::lock_guard<std::mutex> g(lock_);for(auto& s:says)out_.push_back(std::move(s));}
    wake_.notify_all();
}
std::vector<Courier::Answered> Courier::answered(){std::lock_guard<std::mutex> g(lock_);std::vector<Answered> out;out.swap(answered_);return out;}
std::vector<Answer> Courier::reads(){std::lock_guard<std::mutex> g(lock_);std::vector<Answer> out;out.swap(reads_);return out;}
void Courier::run(){
    unsigned failures=0;std::string since;bool told=false;
    auto next=std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        // Messages first, in order; a set-down's answer carries the server's id.
        while(!out_.empty()&&!stop_){
            Say s=out_.front();out_.pop_front();g.unlock();
            Result r;try{r=post_(body(s));}catch(const std::exception&){r=Result{};}
            sent_.fetch_add(1,std::memory_order_relaxed);
            std::string id;
            if(!s.take&&r.status==200){try{id=json::parse(r.body).get("id").string();}catch(const std::exception&){}}
            if(r.status!=200&&!(s.take&&r.status==409))log_("CARGOSRV "+std::string(s.take?"take":"place")+" refused: status "+std::to_string(r.status)+"\n");
            g.lock();
            if(!s.take)answered_.push_back({s.local,id});}
        if(stop_)break;
        if(std::chrono::steady_clock::now()>=next){
            g.unlock();
            Result r;try{r=get_(since);}catch(const std::exception&){r=Result{};}
            Answer a;if(r.status==200)a=parse(r.body);
            if(a.read&&!told){told=true;log_("CARGOSRV the server lists "+std::to_string(a.crates.size())+" crates of other players"+(a.on?"":" (cargo is off there)")+"\n");}
            const uint64_t wait=delayAfter(a,failures);
            g.lock();
            if(a.read){if(reads_.size()>=16){reads_.clear();since.clear();}else{since=a.version;reads_.push_back(std::move(a));}}
            next=std::chrono::steady_clock::now()+std::chrono::milliseconds(wait);}
        wake_.wait_until(g,next,[&]{return stop_||!out_.empty();});
    }
}
}
