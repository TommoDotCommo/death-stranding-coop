#include "api.h"
#include "idtag.h"
#include "collection.h"
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    if(key=="offer_old")return value=="yes"?"Unless you choose otherwise, your older strand may be offered here: a game whose co-op mod has offer_strand on uploads the structures its save already holds.":
                               "Unless you choose otherwise, your older strand is not offered here.";
    if(key=="order")return value=="liked"?"When an area has more objects than a game asks for, a friend's come first, then the most liked.":
                           "When an area has more objects than a game asks for, the newest come first.";
    if(key=="per_builder")return value=="0"?"No cap on how many of one builder's objects a game is handed at once.":
                                 "At most "+value+" objects of one builder of each kind in what a game is handed at once.";
    if(key=="spread")return value=="0"?"Objects are handed however close together they stand.":
                            "An object closer than "+value+" m to one of its kind already handed is left out of that answer.";
    if(key=="near_cap")return value=="0"?"Every player near you is shown to you, however many there are.":
                              "At most "+value+" of the players near you are shown to you at once: your friends first, then the nearest.";
    if(key=="challenges")return value=="members"?"Only members publish route challenges; everyone may run them.":
                                value=="owner"?"Only the server's owner publishes route challenges; everyone may run them.":
                                value=="nobody"?"No route challenges are published here.":"Any player may publish a route challenge for the others to run.";
    if(key=="chat")return value=="on"?"Players can talk here with text chat (Y in the game).":"Text chat is off on this server.";
    if(key=="map")return value=="on"?"Every player here is shown on the others' map, wherever they are.":"Other players are not shown on the map here.";
    if(key=="world")return value=="server"?"Only what is on this server stands in the world here. What your save holds is hidden while you play here, and stays in your save.":
                           "Your save's structures stay in the world here, with this server's added.";
    if(key=="test_mode")return value=="on"?"Test mode: unlimited materials (U), stamina and blood for everyone here. For testing: the game does not play as it should.":
                               "No test mode: the game plays as usual.";
    if(key=="identity")return value=="keys"?"Players are known by the key their launcher made: nobody signs in, builds or withdraws in another player's name (the traffic is not encrypted).":
                              "No player keys: whoever has the group's key can sign in or act as any player. For friends only.";
    if(key=="import")return value=="nobody"?"Nobody imports collections here.":value=="members"?"Members import collections (shared groups of structures) here; others do not.":
                            value=="owner"?"Only the server's owner imports collections here.":"Any player may import a collection (a shared group of structures) here.";
    if(key=="import_cost")return value=="materials"?"What an imported structure carried in materials is recorded with it (nothing is taken yet).":
                                 "Importing a collection costs nothing.";
    return key+" "+value;
}
json::Value World::apiPolicy(int64_t now) const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("server",serverId_);out.set("preset",policy_.preset);
    out.set("time",now);out.set("owner",!owner_.empty());   // the server's clock, for a launcher whose clock is off
    out.set("name",name_);                                    // this world's display name ("" none; README "Builder names")
    Value players=Value::object();players.set("count",int64_t(players_.size()));players.set("limit",int64_t(std::min<int64_t>(int64_t(MaxPlayers),policy_.players)));out.set("players",players);
#ifndef MUTANT_API_ONLINE_ALWAYS
    if(onlineKnown_){
#else
    {
#endif
        // Who is connected now: every player counts, at most OnlineNames names are listed, a player without one is not.
        constexpr size_t OnlineNames=64;Value online=Value::object(),names=Value::array();size_t listed=0;
        for(auto& n:online_)if(!n.empty()&&listed<OnlineNames){names.push(n);++listed;}
        online.set("count",int64_t(online_.size()));online.set("names",names);out.set("online",online);}
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
        if(!ob.collection.empty())v.set("collection",ob.collection);
        list.push(v);}
    Value out=Value::object();out.set("format",1);out.set("player",id);out.set("objects",list);return out;
}
json::Value World::apiPlayers() const {
    auto list=playerList();Value players=Value::array();
    for(auto& i:list){Value v=Value::object();v.set("id",i.id);v.set("name",i.name);v.set("known",i.known);v.set("member",i.member);v.set("banned",i.banned);
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
// ---- Who a player sees (README, "How many players one player sees") ---------------------------------------------
// A list entry: a short key "a1b2 c3d4 e5f6 a7b8" is the one player of this world whose key begins so; anything else is
// an account key as the API takes it. The caller holds the lock.
std::string World::friendEntry(const std::string& key,const std::string& entry) const {
    std::string e=entry;for(auto& c:e)if(c>='A'&&c<='F')c=char(c-'A'+'a');
    if(e.size()==19&&e[4]==' '&&e[9]==' '&&e[14]==' '){
        std::string found;
        for(auto& p:players_){
            identity::PublicKey k{};if(!identity::keyOf(p.first,k)||identity::shortText(k)!=e)continue;
            if(!found.empty())bad("two players of this world have keys that begin "+e+": name the whole key");
            found=p.first;}
        if(found.empty())bad("no player of this world has the key "+e);
        e=found;}
    else if(!accountKeyText(e))bad("a friend is a player's account key (ed25519- and 64 hex digits) or the short form of his key (a1b2 c3d4 e5f6 a7b8)");
    if(e==key)bad("a player is not his own friend");
    return e;}
json::Value World::apiFriends(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock_);
    auto found=players_.find(key);const Player* p=found==players_.end()?nullptr:&found->second;
    Value out=Value::object();out.set("format",1);out.set("player",playerId(key));out.set("known",p!=nullptr);
    out.set("show_others",p?p->showOthers:std::string());
    Value list=Value::array();
    if(p)for(auto& f:p->friends){
        Value v=Value::object();v.set("key",f);v.set("id",playerId(f));identity::PublicKey k{};v.set("short",identity::keyOf(f,k)?identity::shortText(k):std::string());
        auto them=players_.find(f);v.set("known",them!=players_.end());v.set("name",them!=players_.end()?them->second.name:std::string());list.push(v);}
    out.set("friends",list);return out;
}
void World::apiSetFriends(const std::string& key,const std::vector<std::string>& add,const std::vector<std::string>& remove,const std::string& show){
    if(!oneOf(show,{"","everyone","friends","nobody","default"}))bad("show_others is everyone, friends, nobody or default");
    if(add.empty()&&remove.empty()&&show.empty())bad("nothing chosen (add, remove, show_others)");
    if(add.size()+remove.size()>MaxFriends)bad("at most "+std::to_string(MaxFriends)+" friends");
    std::lock_guard<std::mutex> g(lock_);
    std::set<std::string> adding;for(auto& a:add)adding.insert(friendEntry(key,a));
    auto found=players_.find(key);
    std::set<std::string> next=found==players_.end()?std::set<std::string>{}:found->second.friends;
    for(auto& r:remove){
        std::string e=r;for(auto& c:e)if(c>='A'&&c<='F')c=char(c-'A'+'a');
        if(e.size()==19&&e[4]==' '&&e[9]==' '&&e[14]==' '){
            for(auto it=next.begin();it!=next.end();){identity::PublicKey k{};if(identity::keyOf(*it,k)&&identity::shortText(k)==e)it=next.erase(it);else ++it;}}
        else if(accountKeyText(e))next.erase(e);
        else bad("remove: an account key or the short form of a key");}
    next.insert(adding.begin(),adding.end());
    if(next.size()>MaxFriends)bad("at most "+std::to_string(MaxFriends)+" friends");
    if(found==players_.end()){
        size_t room=std::min(MaxPlayers,size_t(policy_.players));
        if(players_.size()>=room){note("friends refused: world is full ("+std::to_string(room)+" players)");bad("this world has all its players");}
        Player p;p.key=key;p.id=playerId(key);p.name="Porter";found=players_.emplace(key,p).first;note("new player "+p.id+" (friends from the launcher)");}
    Player& p=found->second;p.friends=std::move(next);
    if(!show.empty())p.showOthers=show=="default"?"":show;
    dirty_=true;++changes_;note("friends of "+p.id+": "+std::to_string(p.friends.size())+", show_others "+(p.showOthers.empty()?"(his game's)":p.showOthers));
}
Reply World::worldIds(const std::string& token,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    auto* me=chatPlayer(token,now);
#ifndef MUTANT_WORLDIDS_TOKEN
    if(!me)return noticeTokenRefused();
#endif
    Value out=Value::object(),list=Value::array();out.set("format",1);out.set("server",serverId_);out.set("world",policy_.world);out.set("wait_ms",int64_t(WorldIdsWaitMs));
    for(auto& o:objects_){
#ifndef MUTANT_WORLDIDS_ALL
        if(idtag::carries(serverId_,o.first))continue;
#endif
#ifndef MUTANT_WORLDIDS_UNSERVED
        if(!me||!servedTo(o.second,me->id))continue;
#endif
        list.push(o.first);}
    out.set("untagged",list);return ok(out);
}
Reply World::mapRead(const std::string& token,int64_t now,const std::string& xText,const std::string& yText,const std::string& areaText){
    std::lock_guard<std::mutex> g(lock_);
#ifndef MUTANT_MAP_TOKEN
    if(!chatPlayer(token,now))return noticeTokenRefused();
#endif
    auto number=[](const std::string& s,double& out){if(s.empty()||s.size()>24)return false;char* end=nullptr;out=std::strtod(s.c_str(),&end);return end&&!*end&&std::isfinite(out);};
    double x=0,y=0,wanted=0;const bool placed=number(xText,x)&&number(yText,y);const uint32_t area=number(areaText,wanted)&&wanted>=1&&wanted<=65535?uint32_t(wanted):0;
    Value out=Value::object(),list=Value::array();out.set("format",1);out.set("on",policy_.map=="on");size_t seen=0;
#ifndef MUTANT_MAP_OFF
    if(policy_.map=="on"){
#else
    {
#endif
        uint64_t me=0;Viewer choice;
        if(policy_.identity=="keys"){auto t=noticeTokens_.find(token);if(t!=noticeTokens_.end())me=relayIdOf(t->second.account);if(me)choice=viewerOf(me);}
        std::vector<std::pair<double,std::map<uint64_t,Spot>::const_iterator>> found;
#ifndef MUTANT_MAP_CHOICE
        if(choice.show!="nobody")
#endif
        for(auto e=spots_.cbegin();e!=spots_.cend();++e){
            auto& s=e->second;
#ifndef MUTANT_MAP_STALE
            if(s.at+MapFreshSeconds<now)continue;
#endif
            ++seen;
#ifndef MUTANT_MAP_SELF
            if(e->first==me)continue;
#endif
#ifndef MUTANT_MAP_CHOICE
            if(choice.show=="friends"&&!choice.friends.count(e->first))continue;
#endif
#ifndef MUTANT_MAP_AREA
            if(area&&s.area&&s.area!=area)continue;
#endif
            found.emplace_back(placed?(s.x-x)*(s.x-x)+(s.y-y)*(s.y-y):0.0,e);}
#ifndef MUTANT_MAP_NEAREST
        if(placed)std::stable_sort(found.begin(),found.end(),[](auto& a,auto& b){return a.first<b.first;});
#endif
        for(size_t i=0;i<found.size()&&i<MapPlayersMax;++i){auto& e=*found[i].second;auto& s=e.second;
            Value v=Value::object();v.set("id",std::to_string(e.first));v.set("name",s.name);v.set("x",s.x);v.set("y",s.y);v.set("z",s.z);if(s.haveYaw)v.set("yaw",s.yaw);if(s.area)v.set("area",int64_t(s.area));
            list.push(v);}
    }
    out.set("players",list);out.set("wait_ms",std::min<int64_t>(MapWaitMaxMs,std::max<int64_t>(MapWaitMinMs,int64_t(seen)*20)));Reply r;r.body=out.dump();return r;
}
uint64_t World::relayIdOf(const std::string& account) const {
    auto it=relayIds_.find(account);if(it!=relayIds_.end())return it->second;
    identity::PublicKey k{};uint64_t id=identity::keyOf(account,k)?identity::relayId(k):0;relayIds_[account]=id;return id;
}
World::Viewer World::relayViewer(uint64_t relayId) const {
    std::lock_guard<std::mutex> g(lock_);
    return viewerOf(relayId);
}
World::Viewer World::viewerOf(uint64_t relayId) const {
    Viewer v;v.cap=policy_.nearCap;
    if(policy_.identity!="keys")return v;   // a relay id is whatever the game says: nobody's choice or friends
    for(auto& p:players_){
        if(relayIdOf(p.first)!=relayId)continue;
        v.show=p.second.showOthers;
        for(auto& f:p.second.friends)if(uint64_t id=relayIdOf(f))v.friends.insert(id);
        break;}
    return v;
}
Spot relaySpot(const std::array<double,3>& p,bool haveYaw,double yaw,int64_t now){
    Spot s;
#ifndef MUTANT_COLLECTION_SPOT_AXES
    s.x=p[0];s.y=p[2];s.z=p[1];
#else
    s.x=p[0];s.y=p[1];s.z=p[2];
#endif
    s.haveYaw=haveYaw;s.yaw=yaw;s.at=now;return s;}
std::vector<uint64_t> relayEvictions(World& w){
    std::vector<uint64_t> out;
    for(auto& account:w.takeEvictions()){identity::PublicKey k{};if(identity::keyOf(account,k))out.push_back(identity::relayId(k));}
    return out;}
RelayViewer relayViewer(const World& w,uint64_t relayId,bool capGiven){
    auto v=w.relayViewer(relayId);RelayViewer r;r.chosen=!v.show.empty();
    r.show=v.show=="nobody"?ds::broadcast::Show::Nobody:v.show=="friends"?ds::broadcast::Show::Friends:ds::broadcast::Show::Everyone;
    r.friends=std::move(v.friends);r.cap=capGiven?-1:long(v.cap);return r;}
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
    const bool read=method=="GET"&&(call=="players"||call=="review"),change=method=="POST"&&(call=="review"||call=="remove-player"||call=="member"||call=="policy"||call=="ban"||call=="kick");
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
        // Kick and ban (README, "Kick and ban"): {"player":ID} ends his sessions and passes; {"player":ID,"ban":"yes|no"}.
        if(call=="kick"){
            Value in=body(text,{"player"},why);if(!why.empty())return answer(400,why);
            if(in.get("player").string().empty())return answer(400,"player: a player id");
            out.set("ended",int64_t(w.kick(in.get("player").string())));return ok(out);}
        if(call=="ban"){
            Value in=body(text,{"player","ban"},why);if(!why.empty())return answer(400,why);
            const std::string ban=in.get("ban").string();
            if(in.get("player").string().empty()||(ban!="yes"&&ban!="no"))return answer(400,"player and ban (yes or no)");
            w.ban(in.get("player").string(),ban=="yes");out.set("players",w.apiPlayers().get("players"));return ok(out);}
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
    const bool friends=call=="friends";   // GET reads the list, POST changes it
    // notice-token is taken as a change is: for any player under identity none, signed by its player under keys.
    const bool read=call=="policy"||call=="me"||call=="mine"||(friends&&method=="GET"),change=call=="choices"||call=="offer"||call=="withdraw"||call=="notice-token"||call=="collection"||call=="import"||(friends&&method!="GET"),pass=call=="ticket"||call=="relay-token";
#ifndef MUTANT_API_METHOD
    if((read&&method!="GET")||((change||pass)&&method!="POST")||(!read&&!change&&!pass))return answer(404,"not found");   // a change is never a GET
#else
    if(!read&&!change&&!pass)return answer(404,"not found");
#endif
    if(call=="policy")return ok(w.apiPolicy(now));
    const bool keys=w.policy().identity=="keys";
    const bool collect=call=="collection"||call=="import";
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
    // Kick and ban (README): a banned player's launcher is refused every call but reading the rules.
    auto barred=[&](const std::string& key)->bool{
#ifndef MUTANT_BAN_API
        return w.banned(key);
#else
        (void)key;return false;
#endif
        };
    if(read){
#ifndef MUTANT_API_KEY
        if(!accountKeyText(player))return answer(400,"player: the account key his game signs in with");
#endif
#ifdef MUTANT_API_FRIENDS_ANY
        if(friends)return ok(w.apiFriends(player));
#endif
        if(int r=actor(player))return refusal(r);
        if(barred(player))return answer(403,"this player is banned from this server");
        return ok(call=="me"?w.apiMe(player):friends?w.apiFriends(player):w.apiMine(player));}
    if(collect){
        // Collections (../docs/COLLECTIONS.md). collection: {"player":KEY,"objects":[ID,...]} or {"player":KEY,"near":METRES},
        // optional "name"; import: {"player":KEY,"collection":TEXT,"place":"here|original"}. Both: optional "relay_id"
        // (decimal) under identity none, where a relay id is whatever the launcher names; under keys it is the key's.
        if(method!="POST")return answer(404,"not found");
        Value in;
        try{if(text.size()>collection::MaxTextChars+1024)bad("too long");in=json::parse(text);if(!in.isObject())bad("not an object");}
        catch(const std::exception&){return answer(400,"the body is a JSON object");}
        const std::vector<std::string> fields=call=="collection"?std::vector<std::string>{"player","objects","near","name","relay_id"}:std::vector<std::string>{"player","collection","place","relay_id"};
        for(auto& m:in.members()){
            if(std::find(fields.begin(),fields.end(),m.first)==fields.end())return answer(400,"unknown field "+m.first);
            const bool good=m.first=="objects"?m.second.isArray():m.first=="near"?m.second.isInt():m.second.isString();
            if(!good)return answer(400,m.first+" is in the wrong form");}
        const std::string key=in.get("player").string();
        if(!accountKeyText(key))return answer(400,"player: the account key his game signs in with");
        if(int r=actor(key))return refusal(r);
        if(barred(key))return answer(403,"this player is banned from this server");
        uint64_t relayId=0;
        if(keys){identity::PublicKey k{};if(identity::keyOf(key,k))relayId=identity::relayId(k);}
        else if(in.has("relay_id")){
            const std::string r=in.get("relay_id").string();
            if(r.empty()||r.size()>20||r.find_first_not_of("0123456789")!=std::string::npos)return answer(400,"relay_id: the player number in decimal");
            relayId=std::stoull(r);}
        try{
            if(call=="collection"){
                std::vector<std::string> ids;
                for(auto& i:in.get("objects").items()){if(!i.isString()||!objectIdText(i.string()))return answer(400,"objects: a list of object ids");ids.push_back(i.string());}
                if(in.has("objects")&&ids.empty())return answer(400,"objects: name at least one");
                return ok(w.apiCollection(key,ids,in.get("near").integer(),in.get("name").string(),relayId,now));}
            const bool owner=!signer.empty()&&signer==w.owner();
            return ok(w.apiImport(key,in.get("collection").string(),in.get("place").string(),relayId,owner,now));
        }catch(const Refused& e){return answer(e.status,e.what());}
        catch(const std::exception& e){return answer(403,e.what());}}
    if(friends){
        // {"player":KEY,"add":[...],"remove":[...],"show_others":"everyone|friends|nobody|default"}, one or more of the last three.
        std::string why;Value in=body(text,{"player","show_others"},why,{},{"add","remove"});if(!why.empty())return answer(400,why);
        const std::string key=in.get("player").string();
        if(!accountKeyText(key))return answer(400,"player: the account key his game signs in with");
#ifndef MUTANT_API_FRIENDS_ANY
        if(int r=actor(key))return refusal(r);
#endif
        if(barred(key))return answer(403,"this player is banned from this server");
        std::vector<std::string> add,remove;for(auto& i:in.get("add").items())add.push_back(i.string());for(auto& i:in.get("remove").items())remove.push_back(i.string());
        try{w.apiSetFriends(key,add,remove,in.get("show_others").string());}
        catch(const std::exception& e){std::string m=e.what();return answer(m.find("all its players")!=std::string::npos||m.find("no player of this world")!=std::string::npos||m.find("two players")!=std::string::npos?403:400,m);}
        return ok(w.apiFriends(key));}
    // A change: one JSON object with exactly the fields of this call, all of them checked before anything changes.
    Value in;
    try{if(text.size()>MaxApiBody)bad("too long");in=json::parse(text);if(!in.isObject())bad("not an object");}
    catch(const std::exception&){return answer(400,"the body is a JSON object");}
    const std::vector<std::string> fields=call=="choices"?std::vector<std::string>{"player","offer_builds","offer_old"}:
                                          call=="offer"?std::vector<std::string>{"player","object","all","answer"}:call=="ticket"?std::vector<std::string>{"player","name"}:pass||call=="notice-token"?std::vector<std::string>{"player"}:std::vector<std::string>{"player","object","all"};
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
    if(barred(key))return answer(403,"this player is banned from this server");
    try{
        if(pass){
            if(call=="ticket")return ok(w.apiTicket(key,now,in.get("name").string()));
            identity::PublicKey k{};if(!identity::keyOf(key,k))return answer(403,"a relay token is for a player's key");
            return ok(w.apiRelayToken(key,identity::relayId(k),now));}
        if(call=="notice-token")return ok(w.apiNoticeToken(key,now));
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
