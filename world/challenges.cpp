#include "challenges.h"
#include "api.h"
#include "../mod/route.h"
#include <algorithm>
#include <stdexcept>
namespace world {
namespace {
using json::Value;
[[noreturn]] void bad(const std::string& why){throw std::runtime_error(why);}
Reply ok(const Value& v){return Reply{200,v.dump()};}
Reply answer(int status,const std::string& message){
    Value e=Value::object();e.set("status",std::to_string(status));e.set("code",status);e.set("message",message);return Reply{status,e.dump()};}
std::string shortKey(const std::string& account){identity::PublicKey k{};return identity::keyOf(account,k)?identity::shortText(k):std::string();}
}
std::string displayName(const std::string& s){
    std::string t=cutText(s,24,96);
    for(unsigned char c:t)if(c<0x20||c==0x7f)return {};
    return t;
}
bool challengeCall(const std::string& call){return call=="challenges"||call=="challenge-publish"||call=="challenge-result"||call=="challenge-remove";}

Challenges::Entry Challenges::publish(const std::string& account,const std::string& name,const std::string& text,int64_t now){
    std::string why;auto route=mod::route::decode(text,&why);
    if(!route)bad(why);
    const std::string canonical=mod::route::encode(*route);const uint32_t crc=mod::route::id(canonical);
    std::lock_guard<std::mutex> g(lock_);
#ifndef MUTANT_CHALLENGE_LIMITS
    if(entries_.size()>=MaxChallenges)bad("this server has all its challenges ("+std::to_string(MaxChallenges)+")");
    size_t his=0;for(auto& e:entries_)if(e.second.account==account)++his;
    if(his>=MaxChallengesPerPlayer)bad("you have published "+std::to_string(MaxChallengesPerPlayer)+" challenges here: remove one first");
#endif
#ifndef MUTANT_CHALLENGE_SAME
    for(auto& e:entries_)if(e.second.crc==crc)bad("that challenge is here already ("+e.second.id+")");
#endif
    Entry e;e.id="c"+std::to_string(next_++);e.route=canonical;e.account=account;e.crc=crc;e.published=now;
    e.name=displayName(name);if(e.name.empty())e.name=route->author.empty()?"Porter":route->author;
    entries_[e.id]=e;dirty_=true;return e;
}
size_t Challenges::result(const std::string& account,const std::string& name,const std::string& id,const std::string& text,int64_t now){
    std::string route;
    {std::lock_guard<std::mutex> g(lock_);auto it=entries_.find(id);if(it==entries_.end())bad("no challenge "+id+" on this server");route=it->second.route;}
    auto r=mod::route::decode(route);if(!r)bad("the challenge is damaged");
    std::string why;auto track=mod::route::decodeTrack(text,r->points.front(),&why);
    if(!track)bad("the run is refused: "+why);
#ifndef MUTANT_CHALLENGE_VERIFY
    if(auto v=mod::route::verify(*r,*track);!v.empty())bad("the run is refused: "+v);
#endif
    std::lock_guard<std::mutex> g(lock_);
    auto it=entries_.find(id);if(it==entries_.end())bad("no challenge "+id+" on this server");
    Entry& e=it->second;++e.runs;dirty_=true;
    const int64_t ms=int64_t(track->claimedMs);
    std::string shown=displayName(name);if(shown.empty())shown="Porter";
    auto mine=std::find_if(e.board.begin(),e.board.end(),[&](const Run& x){return x.account==account;});
#ifndef MUTANT_CHALLENGE_BEST
    if(mine!=e.board.end()&&mine->ms<=ms){mine->name=shown;return size_t(mine-e.board.begin())+1;}   // his best stands
#endif
    if(mine!=e.board.end())e.board.erase(mine);
    Run run{account,shown,ms,now};
    auto at=std::upper_bound(e.board.begin(),e.board.end(),run,[](const Run& a,const Run& b){return a.ms<b.ms;});
    size_t place=size_t(at-e.board.begin())+1;
    e.board.insert(at,run);
    if(e.board.size()>MaxBoard)e.board.resize(MaxBoard);
    return place;
}
void Challenges::remove(const std::string& id){
    std::lock_guard<std::mutex> g(lock_);
    if(!entries_.erase(id))bad("no challenge "+id+" on this server");
    dirty_=true;
}
bool Challenges::has(const std::string& id) const {std::lock_guard<std::mutex> g(lock_);return entries_.count(id)!=0;}
std::string Challenges::author(const std::string& id) const {std::lock_guard<std::mutex> g(lock_);auto it=entries_.find(id);return it==entries_.end()?std::string():it->second.account;}
json::Value Challenges::list() const {
    std::lock_guard<std::mutex> g(lock_);
    Value list=Value::array();
    for(auto& kv:entries_){
        const Entry& e=kv.second;auto r=mod::route::decode(e.route);if(!r)continue;
        Value v=Value::object();v.set("id",e.id);v.set("name",r->name);v.set("author",e.name);v.set("author_key",shortKey(e.account));
        v.set("route",e.route);v.set("rules_text",mod::route::rulesText(r->rules));
        Value rules=Value::object();rules.set("width",int64_t(r->rules.width));rules.set("start_radius",int64_t(r->rules.startRadius));
        rules.set("finish_radius",int64_t(r->rules.finishRadius));rules.set("grace",int64_t(r->rules.grace));rules.set("min_cases",int64_t(r->rules.minCases));
        rules.set("time_limit",int64_t(r->rules.timeLimit));rules.set("no_vehicles",r->rules.noVehicles);v.set("rules",rules);
        v.set("length_m",int64_t(mod::route::length(r->points)+0.5));v.set("points",int64_t(r->points.size()));
        v.set("published",e.published);v.set("runs",e.runs);
        Value board=Value::array();
        for(size_t i=0;i<e.board.size()&&i<BoardShown;++i){const Run& x=e.board[i];
            Value b=Value::object();b.set("place",int64_t(i+1));b.set("name",x.name);b.set("key",shortKey(x.account));b.set("ms",x.ms);
            b.set("time",mod::route::clock(uint64_t(x.ms)));b.set("when",x.when);board.push(b);}
        v.set("board",board);list.push(v);}
    Value out=Value::object();out.set("format",1);out.set("challenges",list);return out;
}
json::Value Challenges::snapshot() const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("next",int64_t(next_));Value list=Value::array();
    for(auto& kv:entries_){const Entry& e=kv.second;
        Value v=Value::object();v.set("id",e.id);v.set("route",e.route);v.set("account",e.account);v.set("name",e.name);v.set("published",e.published);v.set("runs",e.runs);
        Value board=Value::array();for(auto& x:e.board){Value b=Value::object();b.set("account",x.account);b.set("name",x.name);b.set("ms",x.ms);b.set("when",x.when);board.push(b);}
        v.set("board",board);list.push(v);}
    out.set("list",list);return out;
}
void Challenges::restore(const json::Value& v){
    std::map<std::string,Entry> entries;
    for(auto& item:v.get("list").items()){
        Entry e;e.id=item.get("id").string();e.route=item.get("route").string();e.account=item.get("account").string();e.name=item.get("name").string();
        e.published=item.get("published").integer();e.runs=item.get("runs").integer();
        if(e.id.size()<2||e.id[0]!='c'||e.id.find_first_not_of("0123456789",1)!=std::string::npos)bad("world file: bad challenge id");
        if(!mod::route::decode(e.route)||!accountKeyText(e.account))bad("world file: bad challenge "+e.id);
        e.crc=mod::route::id(e.route);
        for(auto& b:item.get("board").items()){Run x{b.get("account").string(),b.get("name").string(),b.get("ms").integer(),b.get("when").integer()};
            if(!accountKeyText(x.account)||x.ms<0)bad("world file: bad challenge board "+e.id);
            e.board.push_back(x);}
        std::sort(e.board.begin(),e.board.end(),[](const Run& a,const Run& b){return a.ms<b.ms;});
        if(e.board.size()>MaxBoard)e.board.resize(MaxBoard);
        entries[e.id]=e;}
    int64_t next=v.get("next").integer(1);
    for(auto& e:entries)next=std::max<int64_t>(next,std::stoll(e.first.substr(1))+1);
    std::lock_guard<std::mutex> g(lock_);entries_=std::move(entries);next_=uint64_t(next);dirty_=false;
}

Reply challengesApi(World& w,Challenges& ch,const std::string& method,const std::string& call,const std::string& text,const std::string& signer,int64_t now){
    if(call=="challenges"){if(method!="GET")return answer(404,"not found");return ok(ch.list());}
    if(method!="POST"||!challengeCall(call))return answer(404,"not found");
    Value in;
    try{if(text.size()>MaxChallengeBody)bad("too long");in=json::parse(text);if(!in.isObject())bad("not an object");}
    catch(const std::exception&){return answer(400,"the body is a JSON object");}
    const std::vector<std::string> fields=call=="challenge-publish"?std::vector<std::string>{"player","route","name"}:
                                          call=="challenge-result"?std::vector<std::string>{"player","challenge","track","name"}:std::vector<std::string>{"player","challenge"};
    for(auto& m:in.members()){
        if(std::find(fields.begin(),fields.end(),m.first)==fields.end())return answer(400,"unknown field "+m.first);
        if(!m.second.isString())return answer(400,m.first+" is text");}
    const std::string key=in.get("player").string(),id=in.get("challenge").string();
    const bool owner=!signer.empty()&&signer==w.owner();
    // The owner's signed call removes any challenge; everything else acts for KEY under the API's rules (api.h).
    if(call=="challenge-remove"&&owner&&key.empty()){
        try{ch.remove(id);}catch(const std::exception& e){return answer(404,e.what());}
        if(w.log)w.log("challenge "+id+" removed by the owner");
        return ok(ch.list());}
    if(!accountKeyText(key))return answer(400,"player: the account key his game signs in with");
    const bool keys=w.policy().identity=="keys";
#ifndef MUTANT_CHALLENGE_UNSIGNED
    if(keys&&signer.empty())return answer(401,"this server takes signed calls from the launcher (identity keys)");
    if(!signer.empty()&&key!=signer)return answer(403,"a call acts only for the player whose key signed it");
    if(signer.empty()&&identity::accountIsKey(key))return answer(403,"a call acts only for the player whose key signed it");
#endif
    if(w.banned(key))return answer(403,"this player is banned from this server");
    try{
        if(call=="challenge-publish"){
            const std::string who=w.policy().challenges;
#ifndef MUTANT_CHALLENGE_POLICY
            if(who=="nobody")return answer(403,"this server takes no challenges");
            if(who=="owner"&&!owner)return answer(403,"only the server's owner publishes challenges here");
            if(who=="members"&&!w.apiMe(key).get("member").boolean())return answer(403,"only members publish challenges here");
#endif
            auto e=ch.publish(key,in.get("name").string(),in.get("route").string(),now);
            if(w.log)w.log("challenge "+e.id+" published by "+playerId(key)+" ("+e.name+")");
            Value out=Value::object();out.set("id",e.id);out.set("challenges",ch.list().get("challenges"));return ok(out);}
        if(call=="challenge-result"){
            size_t place=ch.result(key,in.get("name").string(),id,in.get("track").string(),now);
            if(w.log)w.log("challenge "+id+": a run of "+playerId(key)+", place "+std::to_string(place));
            Value out=Value::object();out.set("place",int64_t(place));out.set("challenges",ch.list().get("challenges"));return ok(out);}
        // challenge-remove by its author
        if(!ch.has(id))return answer(404,"no challenge "+id+" on this server");
        if(ch.author(id)!=key)return answer(403,"only its author or the server's owner removes a challenge");
        ch.remove(id);if(w.log)w.log("challenge "+id+" removed by its author");
        return ok(ch.list());
    }catch(const std::exception& e){return answer(403,e.what());}
}
}
