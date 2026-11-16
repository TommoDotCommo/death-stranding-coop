#include "world.h"
#include <algorithm>
#include <cstring>
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
std::string randomHex(size_t n){
    static std::mutex m;static std::mt19937_64 rng{std::random_device{}()};std::lock_guard<std::mutex> g(m);
    const char* hex="0123456789abcdef";std::string t;while(t.size()<n){uint64_t v=rng();for(int k=0;k<16&&t.size()<n;++k){t+=hex[v&15];v>>=4;}}return t;}
const char* Digits62="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const char* DocumentKind="coop-world-objects";
[[noreturn]] void bad(const std::string& why){throw std::runtime_error(why);}
bool lettersAndDigits(const std::string& s,size_t least,size_t most,const char* also=""){
    if(s.size()<least||s.size()>most)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||(c&&std::strchr(also,c))))return false;
    return true;}
std::string trim(const std::string& s){size_t b=0,e=s.size();while(b<e&&(s[b]==' '||s[b]=='\t'))++b;while(e>b&&(s[e-1]==' '||s[e-1]=='\t'))--e;return s.substr(b,e-b);}
std::vector<std::string> split(const std::string& s,char by){std::vector<std::string> out;size_t at=0;while(true){size_t end=s.find(by,at);if(end==std::string::npos){out.push_back(trim(s.substr(at)));return out;}out.push_back(trim(s.substr(at,end-at)));at=end+1;}}
// The kinds whose letter is known (mod/game_offsets.md, "Online protocol"); any other kind is named by its letter.
const std::pair<const char*,char> TypeNames[]={{"post",'p'},{"charger",'c'},{"safehouse",'s'},{"watchtower",'w'},{"bridge",'b'},{"shelter",'n'},{"rope",'r'},
    {"ladder",'l'},{"bike",'v'},{"cairn",'e'},{"road",'o'},{"mushroom",'x'},{"chiralbridge",'g'},{"jumpstand",'j'}};
bool wholeNumber(const std::string& s,int64_t& out){
    if(s.empty()||s.size()>18)return false;
    size_t i=s[0]=='-'?1:0;
    if(i==s.size())return false;
    for(size_t k=i;k<s.size();++k)if(s[k]<'0'||s[k]>'9')return false;
    out=std::stoll(s);return true;}
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
World::World(std::function<std::string()> randomToken):random_(randomToken?std::move(randomToken):defaultToken),serverId_(randomHex(16)),epoch_(randomHex(8)){}
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
    }else if(!displayName.empty()&&found->second.name!=displayName){found->second.name=displayName;++changes_;}
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
        size_t mine=0;for(auto& o:objects_)if(o.second.creator==me.id&&o.second.originServer.empty())++mine;
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
        auto& kept=objects_[o.id]=o;dirty_=true;++changes_;note("object "+kept.id+" type "+in.get("t").string()+" in area "+std::to_string(in.get("q").integer())+" by "+me.name);
        return ok(objectResponse(kept));}
    if(name=="deleteObject"||name=="updateObject"){
        auto found=objects_.find(in.get("id").string());
        if(found==objects_.end())return refuse(404,"no-object","no such object");
#ifndef MUTANT_WORLD_OWNER
        if(found->second.creator!=me.id){note("refused "+name+": "+found->first+" is not "+me.id+"'s");return refuse(403,"not-yours","only its builder can change an object");}
#endif
#ifndef MUTANT_LINK_IMPORT_OWNER
        // An imported object belongs to its own server: nobody here changes it, whatever his player id is.
        if(!found->second.originServer.empty()){note("refused "+name+": "+found->first+" is imported");return refuse(403,"not-yours","an imported object is changed on its own server");}
#endif
        dirty_=true;++changes_;
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
            found->second.likes+=n;dirty_=true;++changes_;
            // The builder hears of it in his like history (never of his own likes).
            if(found->second.creator!=me.id&&n>0&&playerById(found->second.creator)){
                Like l;l.to=found->second.creator;l.from=me.id;l.object=found->first;l.type=in.get("lt").string();l.time=now;
                l.points=in.get("lp").integer();l.autoPoints=in.get("lp2").integer();
                if(likes_.size()>=MaxLikes)likes_.erase(likes_.begin());
                likes_.push_back(l);}}
        return ok(Value::object());}
    if(name=="lookup"||name=="reverseLookup"){
        // Who is behind these ids: only the players of this world are known here.
        Value users=Value::array();
        for(auto& id:in.get(name=="lookup"?"ids":"bs").items())for(auto& p:players_){
            if((name=="lookup"?p.second.id:p.second.key)!=id.string())continue;
            Value u=Value::object();u.set("account",p.second.id);u.set("boundAccount",p.second.key);u.set("displayName",p.second.name);u.set("provider","steam");users.push(u);}
        // The builders of imported objects: their name as their own server gave it, and no account.
        if(name=="lookup"&&!byOrigin_.empty())for(auto& id:in.get("ids").items()){
            if(playerById(id.string()))continue;
            for(auto& o:objects_)if(!o.second.originServer.empty()&&o.second.creator==id.string()){
                Value u=Value::object();u.set("account",o.second.creator);u.set("boundAccount","");u.set("displayName",o.second.originName.empty()?"Porter":o.second.originName);u.set("provider","steam");users.push(u);break;}}
        Value out=Value::object();out.set("users",users);return ok(out);}
    if(name=="getRankingSchedules"){Value out=Value::object();out.set("rs",Value::array());out.set("ut",now);return ok(out);}
    if(name=="getWastedBaggages"){Value out=Value::object();out.set("ws",Value::array());out.set("ud",now);return ok(out);}
    if(name=="findMissions"||name=="getOrderedMissions"||name=="getMissionsInProgress"){Value out=Value::object();out.set("missions",Value::array());return ok(out);}
    if(name=="getLikeHistory"){
        // What others gave this player's objects after `since`; nobody reads another player's history.
        int64_t since=in.get("since").integer();Value list=Value::array();
        for(auto& l:likes_){
#ifndef MUTANT_WORLD_LIKES_MINE
            if(l.to!=me.id)continue;
#endif
#ifndef MUTANT_WORLD_LIKES_SINCE
            if(l.time<=since)continue;
#endif
            Value v=Value::object();v.set("t",l.time);v.set("oid",l.object);v.set("lp",l.points);v.set("lp2",l.autoPoints);v.set("uid",l.from);v.set("sc",1);v.set("lt",l.type);list.push(v);}
        Value out=Value::object();out.set("like_histories",list);return ok(out);}
    if(name=="getRelationships"){Value out=Value::object();out.set("strand",Value::array());out.set("history",Value::array());return ok(out);}
    if(name=="getPlayerProfile"){Value out=Value::object();out.set("profiles",Value::array());return ok(out);}
    note("unhandled "+name);
    return ok(Value::object());
}
json::Value World::snapshot() const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("server_id",serverId_);out.set("next_object",int64_t(nextObject_));
    Value players=Value::array();
    for(auto& p:players_){Value v=Value::object();v.set("key",p.second.key);v.set("id",p.second.id);v.set("name",p.second.name);players.push(v);}
    out.set("players",players);
    Value tokens=Value::object();for(auto& t:tokens_)tokens.set(t.first,t.second);out.set("sessions",tokens);
    Value objects=Value::array();
    for(auto& o:objects_){Value v=Value::object();v.set("id",o.second.id);v.set("creator",o.second.creator);v.set("updated",o.second.updated);v.set("likes",o.second.likes);v.set("post",o.second.post);
        if(!o.second.originServer.empty()){Value from=Value::object();from.set("server",o.second.originServer);from.set("id",o.second.originId);from.set("via",o.second.via);
            from.set("name",o.second.originName);from.set("likes",o.second.originLikes);v.set("origin",from);}
        objects.push(v);}
    out.set("objects",objects);
    Value likes=Value::array();
    for(auto& l:likes_){Value v=Value::object();v.set("to",l.to);v.set("from",l.from);v.set("object",l.object);v.set("type",l.type);v.set("time",l.time);v.set("points",l.points);v.set("auto",l.autoPoints);likes.push(v);}
    out.set("likes",likes);return out;
}
void World::restore(const json::Value& v){
    std::lock_guard<std::mutex> g(lock_);
    if(v.get("format").integer()!=1)throw std::runtime_error("world file: unknown format");
    players_.clear();tokens_.clear();objects_.clear();byOrigin_.clear();++changes_;
    // A world file from before linked servers has no id: it keeps the one made for this process, and is saved with it.
    bool hadId=v.has("server_id");
    if(hadId){if(!serverIdText(v.get("server_id").string()))throw std::runtime_error("world file: bad server id");serverId_=v.get("server_id").string();}
    nextObject_=uint64_t(std::max<int64_t>(1,v.get("next_object").integer()));
    for(auto& p:v.get("players").items()){Player pl;pl.key=p.get("key").string();pl.id=p.get("id").string();pl.name=p.get("name").string();
        if(pl.key.empty()||pl.id!=playerId(pl.key))throw std::runtime_error("world file: bad player");
        players_[pl.key]=pl;}
    for(auto& t:v.get("sessions").members())if(playerById(t.second.string()))tokens_[t.first]=t.second.string();
    for(auto& o:v.get("objects").items()){Object ob;ob.id=o.get("id").string();ob.creator=o.get("creator").string();ob.updated=o.get("updated").integer();ob.likes=o.get("likes").integer();ob.post=o.get("post");
        if(ob.id.empty()||!ob.post.isObject())throw std::runtime_error("world file: bad object");
        if(o.has("origin")){const Value& from=o.get("origin");ob.originServer=from.get("server").string();ob.originId=from.get("id").string();ob.via=from.get("via").string();
            ob.originName=from.get("name").string();ob.originLikes=from.get("likes").integer();
            if(!serverIdText(ob.originServer)||!serverIdText(ob.via)||!objectIdText(ob.originId)||ob.originServer==serverId_||!byOrigin_.emplace(std::make_pair(ob.originServer,ob.originId),ob.id).second)
                throw std::runtime_error("world file: bad imported object");}
        objects_[ob.id]=ob;}
    likes_.clear();
    for(auto& l:v.get("likes").items()){Like k;k.to=l.get("to").string();k.from=l.get("from").string();k.object=l.get("object").string();k.type=l.get("type").string();
        k.time=l.get("time").integer();k.points=l.get("points").integer();k.autoPoints=l.get("auto").integer();
        if(likes_.size()<MaxLikes)likes_.push_back(k);}
    dirty_=!hadId;
}
// ---- Linked servers: filters, the travelling form of an object, export, import, forget.
bool serverIdText(const std::string& s){return lettersAndDigits(s,4,32);}
bool objectIdText(const std::string& s){return lettersAndDigits(s,2,64);}
Filter parseFilter(const std::string& text){
    Filter f;std::string all=trim(text);
    if(all=="all")return f;
    if(all=="none"){f.none=true;return f;}
    if(all.empty())bad("filter: empty (write all, none or terms such as types=l,r;areas=226)");
    std::set<std::string> seen;
    for(auto& term:split(all,';')){
        auto eq=term.find('=');if(eq==std::string::npos)bad("filter: '"+term+"' is not key=value");
        std::string key=trim(term.substr(0,eq)),value=trim(term.substr(eq+1));
        if(!seen.insert(key).second)bad("filter: "+key+" is given twice");
        if(value.empty())bad("filter: "+key+" has no value");
        if(key=="types"){
            for(auto& t:split(value,',')){
                std::string letter;
                if(t.size()==1&&t[0]>='a'&&t[0]<='z')letter=t;
                else for(auto& n:TypeNames)if(t==n.first)letter=std::string(1,n.second);
                if(letter.empty())bad("filter: unknown object type '"+t+"'");
                f.types.insert(letter);}}
        else if(key=="owners"){
            for(auto& o:split(value,',')){if(o.empty()||o.size()>MaxNameBytes)bad("filter: bad owner");f.owners.insert(o);}}
        else if(key=="areas"){
            for(auto& a:split(value,',')){int64_t n=0;if(!wholeNumber(a,n))bad("filter: area '"+a+"' is not a number");f.areas.insert(n);}}
        else if(key=="max-age"){
            int64_t n=0;char unit=value.back();int64_t scale=unit=='s'?1:unit=='m'?60:unit=='h'?3600:unit=='d'?86400:0;
            if(!scale||!wholeNumber(value.substr(0,value.size()-1),n)||n<0||n>1000000)bad("filter: max-age is a number and s, m, h or d (30d)");
            f.maxAgeSeconds=n*scale;}
        else if(key=="likes"||key=="imported"){
            if(value!="yes"&&value!="no")bad("filter: "+key+" is yes or no");
            (key=="likes"?f.likes:f.imported)=value=="yes";}
        else bad("filter: unknown term '"+key+"'");}
    return f;
}
bool passes(const Filter& f,const Shared& s,bool imported,int64_t now){
    if(f.none)return false;
#ifndef MUTANT_LINK_PASS_IMPORTED
    if(imported&&!f.imported)return false;
#else
    (void)imported;
#endif
#ifndef MUTANT_LINK_FILTER_TYPE
    if(!f.types.empty()&&!f.types.count(s.type()))return false;
#endif
#ifndef MUTANT_LINK_FILTER_OWNER
    if(!f.owners.empty()&&!f.owners.count(s.creator)&&(s.name.empty()||!f.owners.count(s.name)))return false;
#endif
#ifndef MUTANT_LINK_FILTER_AREA
    if(!f.areas.empty()&&!f.areas.count(s.area()))return false;
#endif
#ifndef MUTANT_LINK_FILTER_AGE
    if(f.maxAgeSeconds>=0&&now-s.updated>f.maxAgeSeconds)return false;
#else
    (void)now;
#endif
    return true;
}
json::Value writeShared(const Shared& s){
    Value v=Value::object();v.set("server",s.server);v.set("id",s.id);v.set("creator",s.creator);v.set("name",s.name);v.set("updated",s.updated);v.set("likes",s.likes);v.set("post",s.post);return v;}
Shared readShared(const json::Value& v){
    if(!v.isObject()||v.members().size()!=7)bad("import: an object is not in the export form");
    for(const char* k:{"server","id","creator","name"})if(!v.get(k).isString())bad(std::string("import: an object has no ")+k);
    Shared s;s.server=v.get("server").string();s.id=v.get("id").string();s.creator=v.get("creator").string();s.name=v.get("name").string();
    if(!serverIdText(s.server))bad("import: bad origin server id");
    if(!objectIdText(s.id))bad("import: bad object id");
    if(!lettersAndDigits(s.creator,1,64,"_"))bad("import: bad builder id");
    if(s.name.size()>MaxNameBytes)bad("import: builder name too long");
    for(unsigned char c:s.name)if(c<0x20||c==0x7f)bad("import: control character in a builder name");
    if(!v.get("updated").isInt()||!v.get("likes").isInt())bad("import: an object's time or likes is not a whole number");
    s.updated=v.get("updated").integer();s.likes=v.get("likes").integer();
    if(s.updated<0||s.likes<0||s.likes>1000000000000ll)bad("import: an object's time or likes is out of range");
    s.post=v.get("post");
    if(!s.post.isObject())bad("import: an object has no data");
    const Value& t=s.post.get("t");
    if(!t.isString()||t.string().size()!=1||t.string()[0]<'a'||t.string()[0]>'z')bad("import: object type must be one letter");
    if(s.post.has("q")&&!s.post.get("q").isInt())bad("import: an object's area is not a number");
#ifndef MUTANT_LINK_SIZE
    if(s.post.dump().size()>MaxSharedPostBytes)bad("import: an object is too large");
#endif
    return s;
}
void World::setServerId(const std::string& id){
    std::lock_guard<std::mutex> g(lock_);if(!serverIdText(id))bad("server id: 4 to 32 letters and digits");
    serverId_=id;dirty_=true;}
Shared World::sharedOf(const Object& o) const {
    Shared s;bool imported=!o.originServer.empty();
    s.server=imported?o.originServer:serverId_;s.id=imported?o.originId:o.id;s.creator=o.creator;s.updated=o.updated;s.likes=o.likes;s.post=o.post;
    if(imported)s.name=o.originName;else if(auto* p=playerById(o.creator))s.name=p->name;
    return s;
}
json::Value World::exportObjects(const Filter& f,const std::string& forServer,int64_t now,const std::string& after,size_t limit,size_t bytes) const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("kind",DocumentKind);out.set("server",serverId_);out.set("time",now);out.set("version",versionLocked());
    Value list=Value::array();bool more=false;std::string next;size_t size=0;
    for(auto it=after.empty()?objects_.begin():objects_.upper_bound(after);it!=objects_.end();++it){
        const Object& o=it->second;bool imported=!o.originServer.empty();
#ifndef MUTANT_LINK_ECHO
        // Never back to where it came from, whatever the filter says.
        if(imported&&!forServer.empty()&&(o.originServer==forServer||o.via==forServer))continue;
#endif
        Shared s=sharedOf(o);
        if(!passes(f,s,imported,now))continue;
        if(!f.likes)s.likes=0;
        // What the other side would refuse never leaves: an object of a shape or size the travelling form does not allow.
        if(!objectIdText(s.id)||s.type().size()!=1||s.type()[0]<'a'||s.type()[0]>'z'||(s.post.has("q")&&!s.post.get("q").isInt()))continue;
        if(s.name.size()>MaxNameBytes)s.name.clear();
        Value v=writeShared(s);size_t n=v.dump().size();
        if(s.post.dump().size()>MaxSharedPostBytes)continue;
        if(list.items().size()>=limit||(size+n>bytes&&!list.items().empty())){more=true;break;}
        size+=n;list.push(std::move(v));next=it->first;}
    out.set("objects",list);out.set("more",more);out.set("next",next);return out;
}
std::string World::importedId(char type,const std::pair<std::string,std::string>& origin) const {
#ifdef MUTANT_LINK_KEEP_ID
    (void)type;return origin.second;
#else
    // The game's own shape (the type letter and 12 characters), made from where the object was first built: the same
    // origin gets the same id here every time, and an id that is taken (by anything) is stepped past.
    for(uint64_t n=0;;++n){
        std::string id(1,type);
        uint64_t a=fnv(origin.first+"/"+origin.second,n*0x9e3779b97f4a7c15ull),b=fnv(origin.second+"/"+origin.first,~n*0xbf58476d1ce4e5b9ull);
        for(int i=0;i<6;++i){id+=Digits62[a%62];a/=62;}
        for(int i=0;i<6;++i){id+=Digits62[b%62];b/=62;}
        if(!objects_.count(id))return id;}
#endif
}
ImportResult World::importObjects(const json::Value& document,const Filter& f,bool prune,int64_t now){
    if(!document.isObject()||document.get("format").integer()!=1||document.get("kind").string()!=DocumentKind)bad("import: not an export of coop-world objects (format 1)");
    if(!document.get("server").isString()||!document.get("objects").isArray())bad("import: no server id or no object list");
    std::vector<Shared> list;
    for(auto& v:document.get("objects").items()){
#ifndef MUTANT_LINK_STRICT
        list.push_back(readShared(v));
#else
        try{list.push_back(readShared(v));}catch(const std::exception&){}
#endif
    }
    return importShared(document.get("server").string(),list,f,prune,now);
}
ImportResult World::importShared(const std::string& via,const std::vector<Shared>& list,const Filter& f,bool prune,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    using Key=std::pair<std::string,std::string>;
    if(!serverIdText(via))bad("import: bad server id");
    if(via==serverId_)bad("import: this is this server's own export");
    ImportResult r;std::set<Key> listed,kept;std::vector<const Shared*> take;size_t adds=0;
    for(auto& s:list){
        Key k{s.server,s.id};
        if(!listed.insert(k).second)bad("import: an object is listed twice");
#ifndef MUTANT_LINK_IMPORT_OWN
        if(s.server==serverId_){++r.own;continue;}   // built here: it is already here, or was removed here
#endif
        if(!passes(f,s,s.server!=via,now)){++r.filtered;continue;}
        kept.insert(k);take.push_back(&s);if(!byOrigin_.count(k))++adds;}
    size_t have=0;for(auto& o:objects_)if(o.second.via==via)++have;
#ifndef MUTANT_LINK_CAP
    if(prune?take.size()>MaxImportedPerServer:have+adds>MaxImportedPerServer)bad("import: too many objects from one server");
#endif
    for(auto* s:take){
        Key k{s->server,s->id};int64_t likes=f.likes?s->likes:0;
        auto found=byOrigin_.find(k);
#ifdef MUTANT_LINK_DUPLICATE
        found=byOrigin_.end();
#endif
        if(found!=byOrigin_.end()&&!objects_.count(found->second)){byOrigin_.erase(found);found=byOrigin_.end();}
        if(found!=byOrigin_.end()){
            Object& o=objects_.find(found->second)->second;
            if(o.id[0]==s->type()[0]){
                // Held already. From another server than the one it was taken from, only a newer state replaces it.
                if(o.via!=via&&s->updated<=o.updated){++r.unchanged;continue;}
                if(o.updated==s->updated&&o.originLikes==likes&&o.creator==s->creator&&o.originName==s->name&&o.post.dump()==s->post.dump()){++r.unchanged;continue;}
#ifndef MUTANT_LINK_LIKES
                o.likes=std::max<int64_t>(0,o.likes+likes-o.originLikes);   // the likes given here stay
#else
                o.likes=likes;
#endif
                o.originLikes=likes;o.creator=s->creator;o.originName=s->name;o.updated=s->updated;o.post=s->post;++r.updated;continue;}
            objects_.erase(found->second);byOrigin_.erase(found);   // another kind now: another id (the id says the kind)
        }
        Object o;o.id=importedId(s->type()[0],k);o.creator=s->creator;o.post=s->post;o.updated=s->updated;o.likes=o.originLikes=likes;
        o.originServer=k.first;o.originId=k.second;o.via=via;o.originName=s->name;
        byOrigin_[k]=o.id;objects_[o.id]=std::move(o);++r.added;}
#ifndef MUTANT_LINK_NO_PRUNE
    if(prune)for(auto it=objects_.begin();it!=objects_.end();){
        if(it->second.via==via&&!kept.count({it->second.originServer,it->second.originId})){byOrigin_.erase({it->second.originServer,it->second.originId});it=objects_.erase(it);++r.removed;}
        else ++it;}
#endif
    if(r.added||r.updated||r.removed){dirty_=true;++changes_;}
    return r;
}
size_t World::forget(const std::string& server){
    std::lock_guard<std::mutex> g(lock_);size_t n=0;
    for(auto it=objects_.begin();it!=objects_.end();){
        const Object& o=it->second;
#ifndef MUTANT_LINK_FORGET_ANY
        bool gone=!o.originServer.empty()&&(o.originServer==server||o.via==server);
#else
        bool gone=!o.originServer.empty();
#endif
        if(gone){byOrigin_.erase({o.originServer,o.originId});it=objects_.erase(it);++n;}else ++it;}
    if(n){dirty_=true;++changes_;note("forgot "+std::to_string(n)+" objects of server "+server);}
    return n;
}
std::vector<World::Origin> World::origins() const {
    std::lock_guard<std::mutex> g(lock_);std::map<std::pair<std::string,std::string>,size_t> count;
    for(auto& o:objects_)if(!o.second.originServer.empty())++count[{o.second.originServer,o.second.via}];
    std::vector<Origin> out;for(auto& c:count)out.push_back({c.first.first,c.first.second,c.second});return out;
}
Reply World::linkPull(const Filter& f,const std::string& body,int64_t now) const {
    std::string server,have,after;
    try{
        if(body.size()>MaxLinkRequestBytes)bad("too long");
        Value in=json::parse(body);
        if(!in.isObject())bad("not an object");
        for(auto& m:in.members())if(m.first!="format"&&m.first!="server"&&m.first!="have"&&m.first!="after")bad("unknown field");
        if(!in.get("format").isInt()||in.get("format").integer()!=1)bad("format");
        for(const char* k:{"server","have","after"})if(!in.get(k).isString())bad(k);
        server=in.get("server").string();have=in.get("have").string();after=in.get("after").string();
        if(!serverIdText(server)||have.size()>64||(!after.empty()&&!objectIdText(after)))bad("value");
    }catch(const std::exception& e){note(std::string("link request refused: ")+e.what());return refuse(400,"bad-link","link request refused");}
    {std::lock_guard<std::mutex> g(lock_);
     if(server==serverId_){note("link request refused: the asking server has this server's id");return refuse(400,"bad-link","link request refused");}
     if(!have.empty()&&after.empty()&&have==versionLocked()){
         Value same=Value::object();same.set("format",1);same.set("kind",DocumentKind);same.set("server",serverId_);same.set("version",versionLocked());same.set("same",true);return ok(same);}}
    return ok(exportObjects(f,server,now,after,LinkPageObjects,LinkPageBytes));
}
}
