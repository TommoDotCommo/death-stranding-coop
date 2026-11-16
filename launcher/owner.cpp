#include "owner.h"
#include <ctime>
namespace launcher::owner {
namespace {
using json::Value;
constexpr size_t MaxAnswer=16u*1024*1024;
bool plain(const std::string& s){for(unsigned char c:s)if(c<0x20||c==0x7f)return false;return true;}
bool oneOf(const std::string& s,std::initializer_list<const char*> allowed){for(auto a:allowed)if(s==a)return true;return false;}
[[noreturn]] void bad(const char* why){throw std::runtime_error(std::string("owner: ")+why);}
std::string heldWords(const std::string& why){
    return why=="build"?"nobody builds on this server":why=="member"?"only members build on this server":why=="limit"?"the limit of objects per player":why;}
std::string count(int64_t n,const char* one,const char* many){return std::to_string(n)+" "+(n==1?one:many);}
// One answer of the owner's calls: the parsed body, or why there is none.
bool answer(const strand::Target& t,const strand::Transport& send,const std::string& method,const std::string& call,const std::string& body,Value& out,std::string& why){
    strand::Http h;
    if(!strand::call(t,send,method,strand::base(t)+"/coop/owner/"+call,body,h)){why="The group's server did not answer.";return false;}
#ifndef MUTANT_OWNER_REFUSAL
    if(h.status!=200){why="Refused: "+strand::message(h)+".";return false;}
#endif
    try{out=json::parseFile(h.body,MaxAnswer);}catch(const std::exception&){out=Value::object();}
    return true;
}
}
bool available(const Session& s,const world::identity::Keys& keys){
#ifndef MUTANT_OWNER_HOST_ONLY
    return s.group==Group::Host&&!s.ownerKey.empty()&&s.ownerKey==world::identity::hex(keys.pub);
#else
    (void)keys;return s.group!=Group::None;
#endif
}
std::string Player::text() const {
    if(!known)return name+"  from linked servers: "+count(imported,"object","objects")+"  ("+id+")";
    return name+"  ["+(key.empty()?std::string("no key"):key)+"]  "+(member?"member":"not a member")+"  shared "+std::to_string(shared)+", waiting "+std::to_string(waiting)
           +", held "+std::to_string(held)+", kept "+std::to_string(kept)+", remnants "+std::to_string(remnants)+(imported?", from linked servers "+std::to_string(imported):std::string())
           +"; "+count(likes,"like","likes")+"  ("+id+")";
}
std::string Waiting::text() const {
    std::string s=kind+"  area "+std::to_string(area)+"  by "+name+"  ";
    if(!held.empty())s+="held: "+heldWords(held)+(waitingFor=="builder"?" (then its builder's answer)":"");
    else s+=waitingFor=="builder"?"waits for its builder's answer":"waits for you";
    return s+"  ("+id+")";
}
std::vector<Player> parsePlayers(const std::string& body){
    Value v;try{v=json::parseFile(body,MaxAnswer);}catch(const std::exception&){bad("the player list is not JSON");}
#ifndef MUTANT_OWNER_PARSE
    if(!v.isObject()||!v.get("format").isInt()||v.get("format").integer()!=1||!v.get("players").isArray())bad("not a player list");
#endif
    std::vector<Player> out;
    for(auto& p:v.get("players").items()){
        Player x;x.id=p.get("id").string();x.name=p.get("name").string();x.key=p.get("key").string();x.offerBuilds=p.get("offer_builds").string();x.offerOld=p.get("offer_old").string();
        x.known=p.get("known").boolean();x.member=p.get("member").boolean();
        x.shared=p.get("shared").integer();x.waiting=p.get("waiting").integer();x.held=p.get("held").integer();x.kept=p.get("kept").integer();
        x.remnants=p.get("remnants").integer();x.imported=p.get("imported").integer();x.likes=p.get("likes").integer();
#ifndef MUTANT_OWNER_PARSE
        if(x.id.empty()||x.id.size()>64||!plain(x.id+x.name+x.key+x.offerBuilds+x.offerOld)||x.name.size()>128||x.key.size()>32)bad("a player that is none");
        for(const char* k:{"shared","waiting","held","kept","remnants","imported"})if(!p.get(k).isInt()||p.get(k).integer()<0)bad("a count that is none");
#endif
        out.push_back(x);}
    return out;
}
std::vector<Waiting> parseWaiting(const std::string& body){
    Value v;try{v=json::parseFile(body,MaxAnswer);}catch(const std::exception&){bad("the review queue is not JSON");}
#ifndef MUTANT_OWNER_PARSE
    if(!v.isObject()||!v.get("format").isInt()||v.get("format").integer()!=1||!v.get("waiting").isArray())bad("not a review queue");
#endif
    std::vector<Waiting> out;
    for(auto& o:v.get("waiting").items()){
        Waiting w;w.id=o.get("id").string();w.creator=o.get("creator").string();w.name=o.get("name").string();w.kind=o.get("kind").string();
        w.held=o.get("held").string();w.waitingFor=o.get("waiting_for").string();w.area=o.get("area").integer();w.updated=o.get("updated").integer();
#ifndef MUTANT_OWNER_PARSE
        if(w.id.empty()||w.id.size()>64||w.creator.empty()||!plain(w.id+w.creator+w.name+w.kind+w.held)||!oneOf(w.waitingFor,{"owner","builder"}))bad("an object that is none");
#endif
        out.push_back(w);}
    return out;
}
bool connect(strand::Target& t,const strand::Transport& send,strand::Policy& p,std::string& why){
    if(!t.keys){why="This launcher has no player key, so it cannot be the server's owner.";return false;}
    auto f=strand::fetchPolicy(t,send,p);
    if(f==strand::Fetch::Old){why="This server is a coop-world from before the owner's calls: use its commands while it is stopped (coop-world players, review, policy).";return false;}
    if(f==strand::Fetch::Unreachable){why="The group's server did not answer: start it first (Play, Host a game, Start group server and play).";return false;}
    if(f!=strand::Fetch::Ok){why=strand::fetchText(f);return false;}
    t.server=p.server;
#ifndef MUTANT_OWNER_CLOCK
    t.clockOffset=p.time?p.time-int64_t(std::time(nullptr)):0;   // the server's clock: a launcher whose clock is off is still in the window
#endif
#ifndef MUTANT_OWNER_UNSIGNED
    t.sign=true;   // the owner's calls are signed under every policy, also where players' calls are not
#endif
    return true;
}
bool fetch(const strand::Target& t,const strand::Transport& send,View& v,std::string& why){
    View out;std::string raw;
    auto f=strand::fetchPolicy(t,send,out.policy,&raw);
    if(f!=strand::Fetch::Ok){why=strand::fetchText(f);return false;}
    try{out.policyDoc=json::parseFile(raw,MaxAnswer);}catch(const std::exception&){why=strand::fetchText(strand::Fetch::Bad);return false;}
    out.name=out.policyDoc.get("name").string();if(!plain(out.name))out.name.clear();
    if(!answer(t,send,"GET","players","",out.playersDoc,why)||!answer(t,send,"GET","review","",out.waitingDoc,why))return false;
    try{out.players=parsePlayers(out.playersDoc.dump());out.waiting=parseWaiting(out.waitingDoc.dump());}
    catch(const std::exception&){why=strand::fetchText(strand::Fetch::Bad);return false;}
    v=std::move(out);return true;
}
bool validAction(const Action& a){
    if(a.kind=="list")return a.target.empty()&&a.sets.empty()&&a.preset.empty();
    if(a.kind=="member")return !a.target.empty()&&oneOf(a.value,{"yes","no"});
    if(oneOf(a.kind,{"remove","accept","reject","accept-player","reject-player"}))return !a.target.empty()&&a.value.empty();
    if(a.kind=="policy")return (!a.preset.empty()||!a.sets.empty())&&a.target.empty();
    return false;
}
Done act(const strand::Target& t,const strand::Transport& send,const Action& a){
    if(!validAction(a))return {false,"Not an action of the owner's."};
    Value body=Value::object(),out;std::string why;
    if(a.kind=="member"){
        body.set("player",a.target);body.set("member",a.value);
        if(!answer(t,send,"POST","member",body.dump(),out,why))return {false,why};
        return {true,a.value=="yes"?"Made a member: what he shares is shared as the rules say for members.":"No longer a member."};}
    if(a.kind=="remove"){
        body.set("player",a.target);
        if(!answer(t,send,"POST","remove-player",body.dump(),out,why))return {false,why};
        return {true,"Removed "+count(out.get("removed").integer(),"object","objects")+" of this player for every player. His account stays: he can sign in and build again."};}
    if(a.kind=="accept"||a.kind=="reject"||a.kind=="accept-player"){
        body.set(a.kind=="accept-player"?"accept_player":a.kind,a.target);
        if(!answer(t,send,"POST","review",body.dump(),out,why))return {false,why};
        if(a.kind=="reject")return {true,"Rejected: kept for its builder only, not removed."};
        return {true,"Accepted "+count(out.get("accepted").integer(),"object","objects")+": every player can be handed "+(out.get("accepted").integer()==1?"it.":"them.")};}
    if(a.kind=="reject-player"){
        // The server answers one object at a time here: everything of that player that waits for the owner.
        Value queue;if(!answer(t,send,"GET","review","",queue,why))return {false,why};
        std::vector<Waiting> list;try{list=parseWaiting(queue.dump());}catch(const std::exception&){return {false,strand::fetchText(strand::Fetch::Bad)};}
        int64_t n=0;
        for(auto& w:list){
#ifndef MUTANT_OWNER_REJECT_ALL
            if(w.creator!=a.target)continue;
#endif
            if(w.waitingFor!="owner")continue;   // what waits for its builder's answer is not the owner's to answer
            Value one=Value::object();one.set("reject",w.id);
            if(!answer(t,send,"POST","review",one.dump(),out,why))return {false,"Rejected "+std::to_string(n)+", then: "+why};
            ++n;}
        return {true,n?"Rejected "+count(n,"object","objects")+": kept for their builder only, not removed.":"Nothing of this player waits for you."};}
    if(!a.preset.empty())body.set("preset",a.preset);
    if(!a.sets.empty()){Value list=Value::array();for(auto& s:a.sets)list.push(s);body.set("set",list);}
    if(!answer(t,send,"POST","policy",body.dump(),out,why))return {false,why};
    return {true,"The rules are changed on the running server: they apply to uploads from now on."};
}
std::vector<std::string> rules(const strand::Policy& p){
    std::vector<std::string> out;
    for(auto& s:p.settings)out.push_back(s.key+" = "+s.value+(s.value==s.preset?std::string():" (preset "+p.preset+": "+s.preset+")")+"   "+s.text);
    return out;
}
std::string removalText(const strand::Policy& p,const Player& x){
    const int64_t all=x.shared+x.waiting+x.held+x.kept+x.remnants+x.imported;
    std::string s="Remove everything of "+x.name+" on this server?\r\n\r\n"
                  "This removes "+count(all,"object","objects")+" for every player at once: "+std::to_string(x.shared)+" shared, "+std::to_string(x.waiting)+" waiting, "
                  +std::to_string(x.held)+" held, "+std::to_string(x.kept)+" kept for him, "+std::to_string(x.remnants)+" remnants, and "+std::to_string(x.imported)
                  +" that came from him through linked servers (those come back with the next import from a server that still gives them).\r\n\r\n";
#ifndef MUTANT_OWNER_REMOVE_TEXT
    s+=p.value("withdrawal")=="stay"
        ?"This server's withdrawal rule is \"stay\": when a player takes back what he built, what he shared stays for the others as a remnant under his name. Removal by the owner is not that: nothing stays, his remnants go too.\r\n\r\n"
        :"This server's withdrawal rule is \"go\": when a player takes back what he built, it is removed for everyone, as this does; here the owner does it for him, his remnants included.\r\n\r\n";
    s+="This is not a ban: his account stays, and he can sign in and build again. Nobody's save is touched; whether a game that already shows his structures takes them away is not known.";
#else
    (void)p;
#endif
    return s;
}
json::Value run(strand::Target t,const strand::Transport& send,const Action& a){
    Value r=Value::object();r.set("action",a.kind);
    strand::Policy p;std::string why;
    if(!validAction(a)){r.set("ok",false);r.set("message","Not an action of the owner's.");return r;}
    if(!connect(t,send,p,why)){r.set("ok",false);r.set("message",why);return r;}
    r.set("server",p.server);
    Done d{true,"The server's players, what waits and its rules."};
    if(a.kind!="list")d=act(t,send,a);
    View v;bool seen=fetch(t,send,v,why);
    r.set("ok",d.ok&&seen);r.set("message",d.ok&&!seen?why:d.message);r.set("name",v.name);
    r.set("policy",seen?v.policyDoc:Value());r.set("players",seen?v.playersDoc.get("players"):Value::array());r.set("waiting",seen?v.waitingDoc.get("waiting"):Value::array());
    return r;
}
}
