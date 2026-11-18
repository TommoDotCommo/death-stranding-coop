#include "challenges.h"
#include "../world/json.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace launcher::challenges {
namespace fs=std::filesystem;
namespace rt=mod::route;
namespace {
std::string readSmall(const fs::path& p,size_t most){
    std::error_code ec;auto size=fs::file_size(p,ec);if(ec||size>most)return {};
    std::ifstream f(p,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();}
std::string trim(const std::string& s){size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?std::string():s.substr(a,b-a+1);}
std::string metres(double m){
    char b[32];
    if(m>=1000)std::snprintf(b,sizeof b,"%.1f km",m/1000);else std::snprintf(b,sizeof b,"%ld m",long(std::lround(m)));
    return b;}
// Newest first by file name (the names carry the UTC time they were written).
std::vector<fs::path> files(const fs::path& dir,const std::string& prefix,const std::string& suffix){
    std::vector<fs::path> out;std::error_code ec;
    for(auto it=fs::directory_iterator(dir,ec);!ec&&it!=fs::directory_iterator();it.increment(ec)){
        auto name=it->path().filename().string();
        if(name.size()>prefix.size()+suffix.size()&&name.compare(0,prefix.size(),prefix)==0&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0&&it->is_regular_file(ec))out.push_back(it->path());}
    std::sort(out.begin(),out.end(),[](const fs::path& a,const fs::path& b){return a.filename().string()>b.filename().string();});
    return out;}
}
fs::path folder(const fs::path& data){return data/"redirect-data"/"mod"/"routes";}   // where the mod writes them (beside its config)
std::vector<Recorded> recordings(const fs::path& data){
    std::vector<Recorded> out;
    for(auto& f:files(folder(data),"recorded-",".txt")){
        auto text=trim(readSmall(f,8192));auto r=rt::decode(text);
        if(r)out.push_back({f,text,*r});}
    return out;
}
std::vector<Result> results(const fs::path& data){
    std::vector<Result> out;
    for(auto& f:files(folder(data),"result-",".json")){
        try{
            auto v=json::parse(readSmall(f,64*1024));if(!v.isObject())continue;
            Result r;r.file=f;r.route=v.get("route").string();r.outcome=v.get("outcome").string();r.reason=v.get("reason").string();
            r.track=v.get("track").string();r.ms=uint64_t(std::max<int64_t>(0,v.get("ms").integer()));r.casesVerified=v.get("cases_verified").boolean();
            auto route=rt::decode(r.route);if(!route||(r.outcome!="finished"&&r.outcome!="failed"))continue;
            r.name=route->name;out.push_back(r);
        }catch(const std::exception&){}}
    return out;
}
std::string describe(const std::string& text){
    std::string why;auto r=rt::decode(text,&why);
    if(!r)return why;
    return r->name+(r->author.empty()?std::string():" by "+r->author)+": "+rt::rulesText(r->rules)+"; "+metres(rt::length(r->points))+", "+std::to_string(r->points.size())+" points";
}
std::string describe(const Result& r){
    return r.name+": "+(r.outcome=="finished"?"finished in "+rt::clock(r.ms):"failed ("+r.reason+")")+(r.casesVerified?"":", cases unverified");
}
std::string canonical(const std::string& text){
    std::string why;auto r=rt::decode(text,&why);
    if(!r)throw std::invalid_argument(why);
    return rt::encode(*r);
}
Edit editOf(const std::string& text){
    std::string why;auto r=rt::decode(text,&why);if(!r)throw std::invalid_argument(why);
    return {r->name,r->author,r->rules};
}
std::string edited(const std::string& text,const Edit& e){
    std::string why;auto r=rt::decode(text,&why);if(!r)throw std::invalid_argument(why);
    r->name=e.name;r->author=e.author;
#ifndef MUTANT_CHALLENGES_EDIT_POINTS
    r->rules=e.rules;
#else
    r->rules=e.rules;r->points.pop_back();
#endif
    return rt::encode(*r);   // throws with the reason for a rule out of range or a bad name
}
std::string chosen(const std::string& ini){return strand::iniGet(ini,"launcher","challenge");}
std::string choose(const std::string& ini,const std::string& text){
    return strand::iniSet(ini,"launcher","challenge",trim(text).empty()?std::string():canonical(text));
}

std::string Listed::text() const {
    std::string t=name+" by "+author+(authorKey.empty()?std::string():" ("+authorKey+")")+": "+rulesText+"; "+metres(double(length))+", "+std::to_string(runs)+(runs==1?" run":" runs");
    if(!board.empty())t+="; best "+board[0].time+" by "+board[0].name;
    return t;
}
std::vector<Listed> parseList(const std::string& body){
    auto v=json::parse(body);
    if(!v.isObject()||v.get("format").integer()!=1||!v.get("challenges").isArray())throw std::runtime_error("not a challenge list");
    std::vector<Listed> out;
    for(auto& c:v.get("challenges").items()){
        Listed l;l.id=c.get("id").string();l.name=c.get("name").string();l.author=c.get("author").string();l.authorKey=c.get("author_key").string();
        l.route=c.get("route").string();l.rulesText=c.get("rules_text").string();l.length=c.get("length_m").integer();l.runs=c.get("runs").integer();
        if(l.id.empty()||!rt::decode(l.route))throw std::runtime_error("a challenge in the list is damaged");
        for(auto& b:c.get("board").items())l.board.push_back({b.get("name").string(),b.get("key").string(),b.get("time").string(),b.get("place").integer(),b.get("ms").integer()});
        out.push_back(l);}
    return out;
}
bool fetchList(const strand::Target& t,const strand::Transport& io,std::vector<Listed>& out,std::string& why){
    strand::Http h;
    if(!strand::call(t,io,"GET",strand::base(t)+"/coop/challenges","",h)){why="The server did not answer.";return false;}
    if(h.status==404){why="This server has no challenges (it is older than them).";return false;}
    if(h.status!=200){why=strand::message(h);return false;}
    try{out=parseList(h.body);}catch(const std::exception& e){why=e.what();return false;}
    return true;
}
bool publish(const strand::Target& t,const strand::Transport& io,const std::string& text,std::string& id,std::string& why){
    std::string route;
    try{route=canonical(text);}catch(const std::exception& e){why=e.what();return false;}
    json::Value b=json::Value::object();b.set("player",t.account);b.set("route",route);b.set("name",t.name);
    strand::Http h;
    if(!strand::call(t,io,"POST",strand::base(t)+"/coop/challenge-publish",b.dump(),h)){why="The server did not answer.";return false;}
    if(h.status!=200){why=strand::message(h);return false;}
    try{id=json::parse(h.body).get("id").string();}catch(const std::exception&){id.clear();}
    if(id.empty()){why="The server's answer has no id.";return false;}
    return true;
}
bool submit(const strand::Target& t,const strand::Transport& io,const std::string& id,const Result& r,int64_t& place,std::string& why){
#ifndef MUTANT_CHALLENGES_SEND_FAILED
    if(r.outcome!="finished"){why="Only a finished run goes to the server.";return false;}
#endif
    json::Value b=json::Value::object();b.set("player",t.account);b.set("challenge",id);b.set("track",r.track);b.set("name",t.name);
    strand::Http h;
    if(!strand::call(t,io,"POST",strand::base(t)+"/coop/challenge-result",b.dump(),h)){why="The server did not answer.";return false;}
    if(h.status!=200){why=strand::message(h);return false;}
    try{place=json::parse(h.body).get("place").integer();}catch(const std::exception&){place=0;}
    return place>0||(why="The server's answer has no place.",false);
}
std::string challengeOf(const std::vector<Listed>& list,const Result& r){
    uint32_t want=rt::id(r.route);if(!want)return {};
    for(auto& l:list)if(rt::id(l.route)==want)return l.id;
    return {};
}
}
