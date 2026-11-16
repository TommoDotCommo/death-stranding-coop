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
    if(key=="order")return value=="liked"?"When an area has more objects than a game asks for, a friend's come first, then the most liked.":
                           "When an area has more objects than a game asks for, the newest come first.";
    if(key=="per_builder")return value=="0"?"No cap on how many of one builder's objects a game is handed at once.":
                                 "At most "+value+" objects of one builder of each kind in what a game is handed at once.";
    if(key=="spread")return value=="0"?"Objects are handed however close together they stand.":
                            "An object closer than "+value+" m to one of its kind already handed is left out of that answer.";
    if(key=="identity")return value=="keys"?"Players are known by the key their launcher made: nobody signs in, builds or withdraws in another player's name (the traffic is not encrypted).":
                              "No player keys: whoever has the group's key can sign in or act as any player. For friends only.";
    return key+" "+value;
}
json::Value World::apiPolicy(int64_t now) const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("server",serverId_);out.set("preset",policy_.preset);
    out.set("time",now);out.set("owner",!owner_.empty());   // the server's clock, for a launcher whose clock is off
    out.set("name",name_);                                    // this world's display name ("" none; README "Builder names")
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
json::Value World::apiPlayers() const {
    auto list=playerList();Value players=Value::array();
    for(auto& i:list){Value v=Value::object();v.set("id",i.id);v.set("name",i.name);v.set("known",i.known);v.set("member",i.member);
        identity::PublicKey k{};v.set("key",identity::keyOf(i.key,k)?identity::shortText(k):std::string());   // a player's key, short; no other account is shown
        v.set("likes",i.likes);
        v.set("offer_builds",i.offerBuilds);v.set("offer_old",i.offerOld);v.set("shared",int64_t(i.shared));v.set("waiting",int64_t(i.pending));
        v.set("kept",int64_t(i.kept));v.set("held",int64_t(i.held));v.set("remnants",int64_t(i.remnants));v.set("imported",int64_t(i.imported));players.push(v);}
    Value out=Value::object();out.set("format",1);out.set("players",players);return out;
}
json::Value World::apiWaiting() const {
    auto list=waiting();Value items=Value::array();
    for(auto& o:list){Value v=Value::object();v.set("id",o.id);v.set("creator",o.creator);v.set("name",o.name);v.set("type",o.type);v.set("kind",typeName(o.type));
        v.set("area",o.area);v.set("updated",o.updated);v.set("held",o.held);v.set("waiting_for",o.forBuilder?"builder":"owner");items.push(v);}
    Value out=Value::object();out.set("format",1);out.set("waiting",items);return out;
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
namespace {
// One JSON object with exactly these fields, each text (or `flags` true, `lists` an array of text): checked before
// anything changes. Empty `why`: the body is good.
Value body(const std::string& text,const std::vector<std::string>& fields,std::string& why,const std::vector<std::string>& flags={},const std::vector<std::string>& lists={}){
    Value in;
    try{if(text.size()>MaxApiBody)bad("too long");in=json::parse(text);if(!in.isObject())bad("not an object");}
    catch(const std::exception&){why="the body is a JSON object";return {};}
    auto has=[](const std::vector<std::string>& v,const std::string& s){return std::find(v.begin(),v.end(),s)!=v.end();};
    for(auto& m:in.members()){
        if(!has(fields,m.first)&&!has(flags,m.first)&&!has(lists,m.first)){why="unknown field "+m.first;return {};}
        if(has(flags,m.first)?m.second.kind()!=Value::Bool||!m.second.boolean():has(lists,m.first)?!m.second.isArray():!m.second.isString()){why=m.first+" is in the wrong form";return {};}
        if(has(lists,m.first))for(auto& i:m.second.items())if(!i.isString()){why=m.first+" is a list of text";return {};}}
    return in;
}
// The owner's calls (api.h): only with the owner's key.
Reply ownerApi(World& w,const std::string& method,const std::string& call,const std::string& text,const std::string& signer){
    const bool read=method=="GET"&&(call=="players"||call=="review"),change=method=="POST"&&(call=="review"||call=="remove-player"||call=="member"||call=="policy");
    if(!read&&!change)return answer(404,"not found");
    if(signer.empty())return answer(401,"the owner's calls are signed with the owner's key");
#ifndef MUTANT_API_OWNER
    if(w.owner().empty()||signer!=w.owner())return answer(403,"only the owner's key does this");
#endif
    if(read)return ok(call=="players"?w.apiPlayers():w.apiWaiting());
    std::string why;
    try{
        Value out=Value::object();
        if(call=="review"){
            Value in=body(text,{"accept","reject","accept_player"},why);if(!why.empty())return answer(400,why);
            if(in.members().size()!=1)return answer(400,"name one: accept, reject or accept_player");
            const auto& m=in.members().front();
            if(m.first=="accept_player")out.set("accepted",int64_t(w.reviewPlayer(m.second.string())));
            else out.set(m.first=="accept"?"accepted":"rejected",int64_t(w.review(m.second.string(),m.first=="accept")));
            out.set("waiting",w.apiWaiting().get("waiting"));return ok(out);}
        if(call=="remove-player"){
            Value in=body(text,{"player"},why);if(!why.empty())return answer(400,why);
            if(in.get("player").string().empty())return answer(400,"player: a player id");
            out.set("removed",int64_t(w.removePlayer(in.get("player").string())));return ok(out);}
        if(call=="member"){
            Value in=body(text,{"player","member"},why);if(!why.empty())return answer(400,why);
            const std::string member=in.get("member").string();
            if(in.get("player").string().empty()||(member!="yes"&&member!="no"))return answer(400,"player and member (yes or no)");
            w.setPlayer(in.get("player").string(),"member="+member);out.set("players",w.apiPlayers().get("players"));return ok(out);}
        Value in=body(text,{"preset"},why,{},{"set"});if(!why.empty())return answer(400,why);
        if(!in.has("preset")&&!in.has("set"))return answer(400,"name a preset, settings to set, or both");
        Policy p;
        try{p=in.has("preset")?presetPolicy(in.get("preset").string()):w.policy();for(auto& s:in.get("set").items())setPolicy(p,s.string());}
        catch(const std::exception& e){return answer(400,e.what());}
        w.setPolicy(p);return ok(w.apiPolicy());
    }catch(const std::exception& e){return answer(403,e.what());}
}
}
Reply launcherApi(World& w,const std::string& method,const std::string& call,const std::string& player,const std::string& text,const std::string& signer,int64_t now){
    if(call.compare(0,6,"owner/")==0)return ownerApi(w,method,call.substr(6),text,signer);
    const bool read=call=="policy"||call=="me"||call=="mine",change=call=="choices"||call=="offer"||call=="withdraw",pass=call=="ticket"||call=="relay-token";
#ifndef MUTANT_API_METHOD
    if((read&&method!="GET")||((change||pass)&&method!="POST")||(!read&&!change&&!pass))return answer(404,"not found");   // a change is never a GET
#else
    if(!read&&!change&&!pass)return answer(404,"not found");
#endif
    if(call=="policy")return ok(w.apiPolicy(now));
    const bool keys=w.policy().identity=="keys";
    // Who may act for `key`: under identity keys, or for a ticket or relay token, only a call signed with that key; a
    // signed call only for its signer; an account of a key never by an unsigned call.
    auto actor=[&](const std::string& key)->int{
#ifndef MUTANT_API_UNSIGNED
        if((keys||pass)&&signer.empty())return 401;
#else
        if(pass&&signer.empty())return 401;
#endif
#ifndef MUTANT_API_SIGNER_PLAYER
        if(!signer.empty()&&key!=signer)return 403;
#endif
#ifndef MUTANT_IDENTITY_KEY_NAME
        if(signer.empty()&&identity::accountIsKey(key))return 403;
#endif
        return 0;};
    auto refusal=[&](int status){return answer(status,status==401?"this server takes signed calls from the launcher (identity keys)":"a call acts only for the player whose key signed it");};
    if(read){
#ifndef MUTANT_API_KEY
        if(!accountKeyText(player))return answer(400,"player: the account key his game signs in with");
#endif
        if(int r=actor(player))return refusal(r);
        return ok(call=="me"?w.apiMe(player):w.apiMine(player));}
    // A change: one JSON object with exactly the fields of this call, all of them checked before anything changes.
    Value in;
    try{if(text.size()>MaxApiBody)bad("too long");in=json::parse(text);if(!in.isObject())bad("not an object");}
    catch(const std::exception&){return answer(400,"the body is a JSON object");}
    const std::vector<std::string> fields=call=="choices"?std::vector<std::string>{"player","offer_builds","offer_old"}:
                                          call=="offer"?std::vector<std::string>{"player","object","all","answer"}:pass?std::vector<std::string>{"player"}:std::vector<std::string>{"player","object","all"};
    for(auto& m:in.members()){
#ifdef MUTANT_API_CHOICES_MEMBER
        if(m.first=="member")continue;
#endif
        if(std::find(fields.begin(),fields.end(),m.first)==fields.end())return answer(400,"unknown field "+m.first+" (member and the policy are the owner's: coop-world player-set, policy, or the owner's calls)");
        if(m.first=="all"?m.second.kind()!=Value::Bool||!m.second.boolean():!m.second.isString())return answer(400,m.first+(m.first=="all"?" is true":" is text"));}
    const std::string key=in.get("player").string();
#ifndef MUTANT_API_KEY
    if(!accountKeyText(key))return answer(400,"player: the account key his game signs in with");
#endif
    if(int r=actor(key))return refusal(r);
    try{
        if(pass){
            if(call=="ticket")return ok(w.apiTicket(key,now));
            identity::PublicKey k{};if(!identity::keyOf(key,k))return answer(403,"a relay token is for a player's key");
            return ok(w.apiRelayToken(key,identity::relayId(k),now));}
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
