#include "world.h"
#include <algorithm>
#include <random>
namespace world {
namespace {
using json::Value;
Reply ok(const Value& v){return Reply{200,v.dump()};}
Reply refuse(int status,const char* code,const std::string& message){
    Value e=Value::object();e.set("status",std::to_string(status));e.set("code",status);e.set("message",message+" ("+code+")");return Reply{status,e.dump()};}
uint64_t fnv(const std::string& s,uint64_t seed){uint64_t h=1469598103934665603ull^seed;for(unsigned char c:s){h^=c;h*=1099511628211ull;}return h;}
std::string defaultToken(){
    static std::mutex m;static std::mt19937_64 rng{std::random_device{}()};std::lock_guard<std::mutex> g(m);
    const char* hex="0123456789abcdef";std::string t;for(int i=0;i<4;++i){uint64_t v=rng();for(int k=0;k<16;++k){t+=hex[v&15];v>>=4;}}return t;}
Value triple(const Value& post,const char* a,const char* b,const char* c){Value v=Value::array();v.push(post.get(a).integer());v.push(post.get(b).integer());v.push(post.get(c).integer());return v;}
}
std::string unwrap(const std::string& body){
    const std::string head="{\"data\":\"",tail="\"}";
    if(body.size()>=head.size()+tail.size()&&body.compare(0,head.size(),head)==0&&body.compare(body.size()-tail.size(),tail.size(),tail)==0)
        return body.substr(head.size(),body.size()-head.size()-tail.size());
    return body;
}
std::string playerId(const std::string& key){
    const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id="zygo_";
    for(uint64_t part=0;id.size()<32;++part){uint64_t h=fnv(key,part*0x9e3779b97f4a7c15ull);for(int i=0;i<9&&id.size()<32;++i){id+=digits[h%62];h/=62;}}
    return id;
}
World::World(std::function<std::string()> randomToken):random_(randomToken?std::move(randomToken):defaultToken){}
const World::Player* World::playerById(const std::string& id) const {for(auto& p:players_)if(p.second.id==id)return &p.second;return nullptr;}
std::string World::playerOf(const std::string& token) const {std::lock_guard<std::mutex> g(lock_);auto t=tokens_.find(token);return t==tokens_.end()?std::string():t->second;}
Reply World::signIn(const std::string& key,const std::string& displayName,const std::string& gateway){
    std::lock_guard<std::mutex> g(lock_);
    if(key.empty()||key.size()>MaxKeyBytes||displayName.size()>MaxNameBytes)return refuse(400,"bad-account","account key or name refused");
    auto found=players_.find(key);
    if(found==players_.end()){
#ifndef MUTANT_WORLD_PLAYER_CAP
        if(players_.size()>=MaxPlayers){note("sign-in refused: world is full");return refuse(403,"full","this world has all its players");}
#endif
        Player p;p.key=key;p.id=playerId(key);p.name=displayName.empty()?"Porter":displayName;
        found=players_.emplace(key,p).first;note("new player "+p.id+" "+p.name);
    }else if(!displayName.empty())found->second.name=displayName;
    auto& p=found->second;
    // One session per player: a new sign-in ends the old one.
    for(auto it=tokens_.begin();it!=tokens_.end();)if(it->second==p.id)it=tokens_.erase(it);else ++it;
    std::string token=random_();tokens_[token]=p.id;dirty_=true;
    Value user=Value::object();user.set("provider","steam");user.set("providerId",key);user.set("id",p.id);user.set("entity",Value::object());
    user.set("displayName",p.name);user.set("display_name",p.name);
    Value session=Value::object();session.set("zone","ds");session.set("token",token);session.set("gateway",gateway);
    session.set("properties",Value::object());session.set("roles",Value::array());
    Value out=Value::object();out.set("user",user);out.set("session",session);return ok(out);
}
json::Value World::objectResponse(const Object& o) const {
    const Value& p=o.post;Value r=Value::object();
    r.set("id",o.id);r.set("c",o.creator);r.set("t",p.get("t"));
    r.set("m",triple(p,"x","y","m"));r.set("l",triple(p,"px","py","pz"));r.set("r",triple(p,"rx","ry","rz"));
    r.set("exp",p.get("exp").integer());r.set("lp",o.likes);r.set("ut",o.updated);r.set("q",p.get("q").integer());
    r.set("mt",p.has("mt")?p.get("mt"):Value::array());r.set("rmt",p.has("rmt")?p.get("rmt"):Value::array());r.set("bgs",Value::array());
    r.set("st",p.get("st").isString()?p.get("st"):Value(""));
    Value comments=Value::array();if(p.get("c").isObject())comments.push(p.get("c"));r.set("cm",comments);
    for(const char* k:{"ri","si","bi","pi","vi","ei","ci"})if(p.get(k).isObject())r.set(k,p.get(k));
    r.set("tags",p.has("tags")?p.get("tags"):Value::array());
    return r;
}
Reply World::call(const std::string& token,const std::string& name,const std::string& body,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    const Player* player=nullptr;
    auto t=tokens_.find(token);if(t!=tokens_.end())player=playerById(t->second);
#ifndef MUTANT_WORLD_NO_TOKEN
    if(!player){note("refused "+name+": no session");return refuse(401,"no-session","sign in first");}
#else
    if(!player&&!players_.empty())player=&players_.begin()->second;
    if(!player)return refuse(401,"no-session","sign in first");
#endif
    Value in;
    try{auto text=unwrap(body);in=text.empty()?Value::object():json::parse(text);}
    catch(const std::exception& e){note("refused "+name+": "+e.what());return refuse(400,"bad-body","request body is not JSON");}
    return gateway(*player,name,in,now);
}
Reply World::gateway(const Player& me,const std::string& name,const Value& in,int64_t now){
    if(name=="getVersion"){
        Value v=Value::object();v.set("version",std::to_string(VersionMajor)+"."+std::to_string(VersionMinor));v.set("major",VersionMajor);v.set("minor",VersionMinor);return ok(v);}
    if(name=="createObject"){
        if(!in.get("t").isString())return refuse(400,"bad-object","object type missing");
        size_t mine=0;for(auto& o:objects_)if(o.second.creator==me.id)++mine;
#ifndef MUTANT_WORLD_OBJECT_CAP
        if(mine>=MaxObjectsPerPlayer){note("refused createObject: "+me.id+" has too many");return refuse(403,"too-many","too many objects");}
#endif
        // The game reads an object's kind from the first letter of its id (its own ids are the type letter and 12
        // characters): an id of any other shape makes the builder's game delete its upload at once.
        const std::string type=in.get("t").string();
#ifndef MUTANT_WORLD_ID_LETTER
        if(type.size()!=1||type[0]<'a'||type[0]>'z')return refuse(400,"bad-object","object type must be one letter");
#endif
        Object o;
        do{
            const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
#ifndef MUTANT_WORLD_ID_LETTER
            o.id=type;
#else
            o.id="o";
#endif
            uint64_t h=fnv(me.id,nextObject_++*0x9e3779b97f4a7c15ull);
            for(int i=0;i<12;++i){o.id+=digits[h%62];h/=62;if(i==5)h^=fnv(o.id,nextObject_);}
        }while(objects_.count(o.id));
        o.creator=me.id;o.post=in;o.updated=now;o.likes=in.get("lp").integer();
        auto& kept=objects_[o.id]=o;dirty_=true;note("object "+kept.id+" type "+in.get("t").string()+" in area "+std::to_string(in.get("q").integer())+" by "+me.name);
        return ok(objectResponse(kept));}
    if(name=="deleteObject"||name=="updateObject"){
        auto found=objects_.find(in.get("id").string());
        if(found==objects_.end())return refuse(404,"no-object","no such object");
#ifndef MUTANT_WORLD_OWNER
        if(found->second.creator!=me.id){note("refused "+name+": "+found->first+" is not "+me.id+"'s");return refuse(403,"not-yours","only its builder can change an object");}
#endif
        dirty_=true;
        if(name=="deleteObject"){note("object "+found->first+" removed by "+me.name);objects_.erase(found);return ok(Value::object());}
        for(const char* k:{"si","pi","vi","ci","ei"})if(in.get(k).isObject())found->second.post.set(k,in.get(k));
        found->second.updated=now;return ok(objectResponse(found->second));}
    if(name=="findQpidObjects"){
        // The other players' objects in one area, at most the asked number per type. Never the asker's own: the game
        // has those in its save.
        int64_t area=in.get("q").integer();const Value& counts=in.get("o").get("ct");
        std::map<std::string,int64_t> left;for(auto& m:counts.members())left[m.first]=m.second.integer();
        Value list=Value::array();
        for(auto& o:objects_){
#ifndef MUTANT_WORLD_OWN
            if(o.second.creator==me.id)continue;
#endif
            if(o.second.post.get("q").integer()!=area)continue;
            auto type=o.second.post.get("t").string();
            if(counts.isObject()){auto l=left.find(type);if(l==left.end()||l->second<=0)continue;--l->second;}
            list.push(objectResponse(o.second));}
        Value set=Value::object();set.set("ro",Value::array());set.set("m",Value::array());set.set("oa",list);
        for(const char* k:{"ob","od","oe","op"})set.set(k,Value::array());
        Value out=Value::object();out.set("n",set);return ok(out);}
    if(name=="getQpidObjects"){
        Value list=Value::array();
        for(auto& id:in.get("ids").items()){auto found=objects_.find(id.string());if(found!=objects_.end())list.push(objectResponse(found->second));}
        Value out=Value::object();out.set("objects",list);return ok(out);}
    if(name=="sendLike"){
        auto found=objects_.find(in.get("oid").string());
        if(found!=objects_.end()){
            int64_t n=in.get("lp").integer()+in.get("lp2").integer();
#ifndef MUTANT_WORLD_LIKE_RANGE
            if(n<0||n>100000)return refuse(400,"bad-like","like count refused");
#endif
            found->second.likes+=n;dirty_=true;}
        return ok(Value::object());}
    if(name=="lookup"||name=="reverseLookup"){
        // Who is behind these ids: only the players of this world are known here.
        Value users=Value::array();
        for(auto& id:in.get(name=="lookup"?"ids":"bs").items())for(auto& p:players_){
            if((name=="lookup"?p.second.id:p.second.key)!=id.string())continue;
            Value u=Value::object();u.set("account",p.second.id);u.set("boundAccount",p.second.key);u.set("displayName",p.second.name);u.set("provider","steam");users.push(u);}
        Value out=Value::object();out.set("users",users);return ok(out);}
    if(name=="getRankingSchedules"){Value out=Value::object();out.set("rs",Value::array());out.set("ut",now);return ok(out);}
    if(name=="getWastedBaggages"){Value out=Value::object();out.set("ws",Value::array());out.set("ud",now);return ok(out);}
    if(name=="findMissions"||name=="getOrderedMissions"||name=="getMissionsInProgress"){Value out=Value::object();out.set("missions",Value::array());return ok(out);}
    if(name=="getLikeHistory"){Value out=Value::object();out.set("like_histories",Value::array());return ok(out);}
    if(name=="getRelationships"){Value out=Value::object();out.set("strand",Value::array());out.set("history",Value::array());return ok(out);}
    if(name=="getPlayerProfile"){Value out=Value::object();out.set("profiles",Value::array());return ok(out);}
    note("unhandled "+name);
    return ok(Value::object());
}
json::Value World::snapshot() const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("next_object",int64_t(nextObject_));
    Value players=Value::array();
    for(auto& p:players_){Value v=Value::object();v.set("key",p.second.key);v.set("id",p.second.id);v.set("name",p.second.name);players.push(v);}
    out.set("players",players);
    Value tokens=Value::object();for(auto& t:tokens_)tokens.set(t.first,t.second);out.set("sessions",tokens);
    Value objects=Value::array();
    for(auto& o:objects_){Value v=Value::object();v.set("id",o.second.id);v.set("creator",o.second.creator);v.set("updated",o.second.updated);v.set("likes",o.second.likes);v.set("post",o.second.post);objects.push(v);}
    out.set("objects",objects);return out;
}
void World::restore(const json::Value& v){
    std::lock_guard<std::mutex> g(lock_);
    if(v.get("format").integer()!=1)throw std::runtime_error("world file: unknown format");
    players_.clear();tokens_.clear();objects_.clear();
    nextObject_=uint64_t(std::max<int64_t>(1,v.get("next_object").integer()));
    for(auto& p:v.get("players").items()){Player pl;pl.key=p.get("key").string();pl.id=p.get("id").string();pl.name=p.get("name").string();
        if(pl.key.empty()||pl.id!=playerId(pl.key))throw std::runtime_error("world file: bad player");
        players_[pl.key]=pl;}
    for(auto& t:v.get("sessions").members())if(playerById(t.second.string()))tokens_[t.first]=t.second.string();
    for(auto& o:v.get("objects").items()){Object ob;ob.id=o.get("id").string();ob.creator=o.get("creator").string();ob.updated=o.get("updated").integer();ob.likes=o.get("likes").integer();ob.post=o.get("post");
        if(ob.id.empty()||!ob.post.isObject())throw std::runtime_error("world file: bad object");
        objects_[ob.id]=ob;}
    dirty_=false;
}
}
