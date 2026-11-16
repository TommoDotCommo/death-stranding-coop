#include "api.h"
#include <algorithm>
namespace world {
namespace {
using json::Value;
constexpr size_t MaxApiBody=4096;
Reply ok(const Value& v){return Reply{200,v.dump()};}
Reply answer(int status,const std::string& message){
    Value e=Value::object();e.set("status",std::to_string(status));e.set("code",status);e.set("message",message);return Reply{status,e.dump()};}
bool oneOf(const std::string& s,std::initializer_list<const char*> allowed){for(auto a:allowed)if(s==a)return true;return false;}
[[noreturn]] void bad(const std::string& why){throw std::runtime_error(why);}
}
bool accountKeyText(const std::string& k){
    if(k.empty()||k.size()>MaxKeyBytes)return false;
    for(char c:k)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;}
std::string settingText(const std::string& key,const std::string& value,size_t players){
    if(key=="join")return value=="open"?"Open to anyone (recorded only: the group's key is still the way in).":"By invitation: the group's key is the way in.";
    if(key=="build")return value=="members"?"Only members' builds are shared; what anyone else builds is held for its builder alone.":
                           value=="nobody"?"Nobody's builds are shared: what you build is held for you alone.":"What any player builds here may be shared with the others.";
    if(key=="accept_offers")return value=="review"?"What you share waits for the owner's review before anyone else is handed it.":
                                   value=="never"?"Nothing reaches the others: what you offer is kept for you.":"What you share reaches every player at once.";
    if(key=="limit")return value=="0"?"No limit on how many of your objects are shared.":"At most "+value+" of your objects are shared or waiting; past that they are held for you.";
    if(key=="withdrawal")return value=="stay"?"If you take back what you shared, it stays for the others as a remnant under your name that nobody owns.":
                                "If you take back what you built, it is removed for everyone.";
    if(key=="players")return (value=="1"?std::string("One player only: a personal archive."):"At most "+value+" players.")+" It has "+std::to_string(players)+" now.";
    if(key=="offer_builds")return value=="no"?"Unless you choose otherwise, what you build here is kept for you alone.":
                                  value=="ask"?"Unless you choose otherwise, what you build here waits for your answer.":"Unless you choose otherwise, what you build here is shared.";
    if(key=="offer_old")return value=="yes"?"Unless you choose otherwise, your older strand may be taken over (recorded only).":
                               "Unless you choose otherwise, your older strand is not taken over (recorded only).";
    if(key=="order")return value=="liked"?"Where there are more objects than a game takes, the most liked come first, then the newest.":
                           "Where there are more objects than a game takes, the newest come first.";
    if(key=="per_builder")return value=="0"?"No cap on how many of one builder's objects of a kind a game is handed at once.":
                                 "At most "+value+" of one builder's objects of a kind are handed to a game at once.";
    if(key=="spread")return value=="0"?"Objects of a kind may be handed however close together they stand.":
                            "Objects of a kind closer than "+value+" m to one already handed are left out of that answer.";
    return key+" "+value;
}
json::Value World::apiPolicy() const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("server",serverId_);out.set("preset",policy_.preset);
    Value players=Value::object();players.set("count",int64_t(players_.size()));players.set("limit",int64_t(std::min<int64_t>(int64_t(MaxPlayers),policy_.players)));out.set("players",players);
    auto items=policyItems(policy_),preset=policyItems(presetPolicy(policy_.preset));Value settings=Value::array();
    for(size_t i=0;i<items.size();++i){
        Value s=Value::object();s.set("key",items[i].first);s.set("value",items[i].second);s.set("preset",preset[i].second);
        s.set("text",settingText(items[i].first,items[i].second,players_.size()));settings.push(s);}
    out.set("settings",settings);return out;
}
json::Value World::apiMe(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock_);
    const std::string id=playerId(key);auto found=players_.find(key);const Player* p=found==players_.end()?nullptr:&found->second;
    Value out=Value::object();out.set("format",1);out.set("player",id);out.set("known",p!=nullptr);
    out.set("name",p?p->name:std::string());out.set("member",p&&p->member);
    Value chose=Value::object();chose.set("offer_builds",p?p->offerBuilds:std::string());chose.set("offer_old",p?p->offerOld:std::string());out.set("choices",chose);
    Value applies=Value::object();applies.set("offer_builds",p&&!p->offerBuilds.empty()?p->offerBuilds:policy_.offerBuilds);
    applies.set("offer_old",p&&!p->offerOld.empty()?p->offerOld:policy_.offerOld);out.set("applies",applies);
    int64_t shared=0,waitingYou=0,waitingOwner=0,kept=0,remnants=0,likes=0;std::map<std::string,int64_t> held{{"build",0},{"member",0},{"limit",0}};
    for(auto& o:objects_){
        const Object& ob=o.second;if(ob.creator!=id||!ob.originServer.empty())continue;
        if(ob.remnant){++remnants;continue;}   // a remnant is nobody's: its likes reach no one
        likes+=ob.likes;
        if(ob.share.empty())++shared;else if(ob.share=="kept")++kept;else if(ob.share=="ask")++waitingYou;else if(ob.share=="review")++waitingOwner;else if(ob.share=="held")++held[ob.held];}
    out.set("shared",shared);out.set("waiting_you",waitingYou);out.set("waiting_owner",waitingOwner);
    Value h=Value::object();for(auto& r:held)h.set(r.first,r.second);out.set("held",h);
    out.set("kept",kept);out.set("remnants",remnants);out.set("likes",likes);return out;
}
json::Value World::apiMine(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock_);
    const std::string id=playerId(key);Value list=Value::array();
    for(auto& o:objects_){
        const Object& ob=o.second;
#ifndef MUTANT_API_MINE_OTHERS
        if(ob.creator!=id)continue;
#endif
#ifndef MUTANT_API_MINE_IMPORTS
        if(!ob.originServer.empty())continue;   // its builder id is what the other server says: not his to act on here
#endif
        Value v=Value::object();const std::string type=ob.post.get("t").string();
        v.set("id",ob.id);v.set("type",type);v.set("kind",typeName(type));v.set("area",ob.post.get("q").integer());v.set("updated",ob.updated);v.set("likes",ob.likes);
        std::string state=ob.remnant?"remnant":ob.share.empty()?"shared":ob.share=="kept"?"kept":ob.share=="held"?"held":"waiting";
        v.set("state",state);
        if(ob.share=="ask"||ob.heldAsk)v.set("waiting_for","you");else if(ob.share=="review")v.set("waiting_for","owner");
        if(ob.share=="held"){v.set("held",ob.held);v.set("held_text",heldText(ob.held));}
        list.push(v);}
    Value out=Value::object();out.set("format",1);out.set("player",id);out.set("objects",list);return out;
}
void World::apiChoices(const std::string& key,const std::string& offerBuilds,const std::string& offerOld){
#ifndef MUTANT_API_STRICT
    if(!oneOf(offerBuilds,{"","yes","no","ask","default"}))bad("offer_builds is yes, no, ask or default");
    if(!oneOf(offerOld,{"","yes","no","default"}))bad("offer_old is yes, no or default");
#endif
    if(offerBuilds.empty()&&offerOld.empty())bad("nothing chosen (offer_builds, offer_old)");
    std::lock_guard<std::mutex> g(lock_);
    auto found=players_.find(key);
    if(found==players_.end()){
        // A new account, as at a sign-in: within the hard cap and the policy's number of players.
#ifndef MUTANT_API_CHOICES_ROOM
        size_t room=std::min(MaxPlayers,size_t(policy_.players));
        if(players_.size()>=room){note("choices refused: world is full ("+std::to_string(room)+" players)");bad("this world has all its players");}
#endif
        Player p;p.key=key;p.id=playerId(key);p.name="Porter";found=players_.emplace(key,p).first;note("new player "+p.id+" (choices from the launcher)");}
    Player& p=found->second;
    if(!offerBuilds.empty())p.offerBuilds=offerBuilds=="default"?"":offerBuilds;
    if(!offerOld.empty())p.offerOld=offerOld=="default"?"":offerOld;
    dirty_=true;++changes_;note("choices of "+p.id+": offer_builds "+(p.offerBuilds.empty()?"(server)":p.offerBuilds)+", offer_old "+(p.offerOld.empty()?"(server)":p.offerOld));
}
Reply launcherApi(World& w,const std::string& method,const std::string& call,const std::string& player,const std::string& body){
    const bool read=call=="policy"||call=="me"||call=="mine",change=call=="choices"||call=="offer"||call=="withdraw";
#ifndef MUTANT_API_METHOD
    if((read&&method!="GET")||(change&&method!="POST")||(!read&&!change))return answer(404,"not found");   // a change is never a GET
#else
    if(!read&&!change)return answer(404,"not found");
#endif
    if(call=="policy")return ok(w.apiPolicy());
    if(read){
#ifndef MUTANT_API_KEY
        if(!accountKeyText(player))return answer(400,"player: the account key his game signs in with");
#endif
        return ok(call=="me"?w.apiMe(player):w.apiMine(player));}
    // A change: one JSON object with exactly the fields of this call, all of them checked before anything changes.
    Value in;
    try{if(body.size()>MaxApiBody)bad("too long");in=json::parse(body);if(!in.isObject())bad("not an object");}
    catch(const std::exception&){return answer(400,"the body is a JSON object");}
    const std::vector<std::string> fields=call=="choices"?std::vector<std::string>{"player","offer_builds","offer_old"}:
                                          call=="offer"?std::vector<std::string>{"player","object","all","answer"}:std::vector<std::string>{"player","object","all"};
    for(auto& m:in.members()){
#ifdef MUTANT_API_CHOICES_MEMBER
        if(m.first=="member")continue;
#endif
        if(std::find(fields.begin(),fields.end(),m.first)==fields.end())return answer(400,"unknown field "+m.first+" (member and the policy are the owner's: coop-world player-set, policy)");
        if(m.first=="all"?m.second.kind()!=Value::Bool||!m.second.boolean():!m.second.isString())return answer(400,m.first+(m.first=="all"?" is true":" is text"));}
    const std::string key=in.get("player").string();
#ifndef MUTANT_API_KEY
    if(!accountKeyText(key))return answer(400,"player: the account key his game signs in with");
#endif
    try{
        if(call=="choices"){
            w.apiChoices(key,in.get("offer_builds").string(),in.get("offer_old").string());
#ifdef MUTANT_API_CHOICES_MEMBER
            if(in.has("member"))w.setPlayer(playerId(key),"member="+in.get("member").string());
#endif
            return ok(w.apiMe(key));}
        const std::string object=in.get("object").string();const bool all=in.get("all").boolean();
#ifndef MUTANT_API_ALL_IMPLIED
        if(all==!object.empty())return answer(400,"name one object, or all");   // a body that names nothing changes nothing
#endif
        Value out=Value::object();
        if(call=="offer"){
            const std::string a=in.get("answer").string();
            if(!oneOf(a,{"","yes","no"}))return answer(400,"answer is yes or no");
            out.set(a=="no"?"kept":"offered",int64_t(w.offer(playerId(key),object,a!="no")));}
        else{auto r=w.withdraw(playerId(key),object);out.set("remnants",int64_t(r.remnants));out.set("removed",int64_t(r.removed));}
        out.set("me",w.apiMe(key));return ok(out);
    }catch(const std::exception& e){return answer(403,e.what());}
}
}
