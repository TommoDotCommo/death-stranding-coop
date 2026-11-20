// What other players built, read by each game while it plays (world.h built) through the HTTP surface: with the
// notice token only; every structure another player built here that this player may be handed on a first read, only
// what changed after a version of this process later, and the ids of what was removed or is no longer handed to him;
// never his own, never a kind the construction manager does not make (a sign), never what is held for its builder.
#include "front.h"
#include "api.h"
#include "idtag.h"
#include <algorithm>
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"built oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);w.setName("North");}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    std::string token(const std::string& key){auto r=raw("POST","/s3cret/coop/notice-token","{\"player\":\""+key+"\"}");CHECK(r.status==200);
        return r.status==200?json::parse(r.body).get("token").string():std::string();}
    Reply read(const std::string& token,const std::string& since=""){return raw("GET","/s3cret/coop/built"+(since.empty()?std::string():"?since="+since),"",{{"x-coop-token",token}});}
    json::Value doc(const std::string& token,const std::string& since=""){auto r=read(token,since);CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& session,const char* type="l",int x=1){
        auto r=w.call(session,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":226,\"px\":"+std::to_string(x)+",\"py\":2,\"pz\":3}",now);CHECK(r.status==200);
        return r.status==200?json::parse(r.body).get("id").string():std::string();}
    void remove(const std::string& session,const std::string& id){CHECK(w.call(session,"deleteObject","{\"id\":\""+id+"\"}",now).status==200);}
};
static std::vector<std::string> sorted(std::vector<std::string> v){std::sort(v.begin(),v.end());return v;}
static std::vector<std::string> ids(const json::Value& d,const char* field="objects"){
    std::vector<std::string> out;for(auto& o:d.get(field).items())out.push_back(o.isString()?o.string():o.get("id").string());return sorted(out);}
static void reads(){
    Server s;
    auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");auto ta=s.token("ann"),tb=s.token("bob");
    // The token is the only way in: none, a write, or outside the prefix is refused.
    CHECK(s.raw("GET","/s3cret/coop/built").status==403);
    CHECK(s.raw("GET","/s3cret/coop/built","",{{"x-coop-token","nope"}}).status==403);
    CHECK(s.raw("POST","/s3cret/coop/built","",{{"x-coop-token",tb}}).status==404);
    CHECK(s.raw("GET","/coop/built","",{{"x-coop-token",tb}}).status==404);
    // A first read: everything (nothing yet), the pace and a version to go on from.
    auto first=s.doc(tb);
    CHECK(first.get("format").integer()==1&&first.get("full").boolean()&&first.get("objects").items().empty()&&first.get("removed").items().empty());
    CHECK(first.get("wait_ms").integer()==BuiltWaitMs&&!first.get("more").boolean(true));
    const std::string v1=first.get("version").string();CHECK(!v1.empty());
    // Ann builds a ladder, a generator and a sign, Bob a ladder: Bob reads Ann's two structures, not the sign, not his own.
    auto l1=s.make(ann,"l",1),c1=s.make(ann,"c",2),t1=s.make(ann,"t",3),b1=s.make(bob,"l",4);
    auto d=s.doc(tb,v1);
    CHECK(!d.get("full").boolean(true)&&ids(d)==sorted({l1,c1})&&d.get("removed").items().empty());
    CHECK(ids(s.doc(ta))==std::vector<std::string>{b1});
    // Each as findQpidObjects answers it: this server's tagged id, the builder, the kind, the place.
    for(auto& o:d.get("objects").items()){
        CHECK(o.get("c").string()==playerId("ann"));
        CHECK(o.get("t").string()==(o.get("id").string()==l1?"l":"c"));
        CHECK(o.get("l").items().size()==3&&o.get("q").integer()==226);}
    CHECK(idtag::IdChars==l1.size());
    // Nothing new: nothing listed. A removed one: its id, once.
    const std::string v2=d.get("version").string();
    auto none=s.doc(tb,v2);CHECK(ids(none).empty()&&none.get("removed").items().empty()&&none.get("version").string()==v2);
    s.remove(ann,l1);
    auto gone=s.doc(tb,v2);CHECK(ids(gone).empty()&&ids(gone,"removed")==std::vector<std::string>{l1});
    CHECK(ids(s.doc(tb,gone.get("version").string()),"removed").empty());
    // A full read lists what stands now: the generator only.
    CHECK(ids(s.doc(tb))==std::vector<std::string>{c1});
    // A version of another process (a server started again) or a stamp from the future: everything again.
    CHECK(s.doc(tb,"0123456789abcdef-1").get("full").boolean());
    CHECK(s.doc(tb,v2+"999999").get("full").boolean());
    (void)t1;
}
static void held(){
    // Members only: Ann's builds are held for her, so Bob is never handed them; held after his version, it is listed
    // as removed (no longer handed), which his game takes as "not there".
    Server s("friends",{"build=members"});
    auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");s.w.setPlayer(playerId("bob"),"member=yes");
    auto ta=s.token("ann"),tb=s.token("bob");
    const std::string v=s.doc(tb).get("version").string();
    auto a1=s.make(ann,"n",1),b1=s.make(bob,"n",2);
    auto d=s.doc(tb,v);CHECK(ids(d).empty()&&ids(d,"removed")==std::vector<std::string>{a1});
    CHECK(ids(s.doc(tb)).empty());
    CHECK(ids(s.doc(ta))==std::vector<std::string>{b1});
}
// Cargo set down for the others (world.h cargoSay, cargoRead).
static void cargo(){
    Server s;
    auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");auto ta=s.token("ann"),tb=s.token("bob");(void)ann;(void)bob;
    auto say=[&](const std::string& t,const std::string& body){return s.raw("POST","/s3cret/coop/cargo",body,{{"x-coop-token",t}});};
    auto read=[&](const std::string& t,const std::string& since=""){auto r=s.raw("GET","/s3cret/coop/cargo"+(since.empty()?std::string():"?since="+since),"",{{"x-coop-token",t}});
        CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();};
    auto crates=[](const json::Value& d){return ids(d,"crates");};
    CHECK(s.raw("GET","/s3cret/coop/cargo").status==403&&say("nope","{}").status==403);
    CHECK(say(ta,"{").status==400&&say(ta,R"({"op":"throw"})").status==400&&say(ta,R"({"op":"place","x":1,"y":2,"z":3})").status==400);
    auto first=read(tb);CHECK(first.get("on").boolean()&&first.get("full").boolean()&&crates(first).empty()&&first.get("wait_ms").integer()==CargoWaitMs);
    const std::string v1=first.get("version").string();
    // Ann sets one down (a 64-bit hash, a place, a heading): an id of this server back; Bob reads it, Ann does not.
    auto put=say(ta,R"({"op":"place","hash":-6917529027641081856,"x":1030.5,"y":-490.25,"z":156,"yaw":1.5,"area":226})");CHECK(put.status==200);
    const std::string k1=put.status==200?json::parse(put.body).get("id").string():std::string();
    CHECK(k1.size()==idtag::IdChars&&k1[0]=='k');
    auto d=read(tb,v1);CHECK(!d.get("full").boolean(true)&&crates(d)==std::vector<std::string>{k1});
    if(d.get("crates").items().size()==1){auto& c=d.get("crates").items()[0];
        CHECK(c.get("hash").integer()==-6917529027641081856LL&&c.get("x").real()==1030.5&&c.get("y").real()==-490.25&&c.get("z").real()==156&&c.get("yaw").real()==1.5&&c.get("area").integer()==226);}
    CHECK(crates(read(ta)).empty());
    // Bob takes it first; Ann's take comes too late; Bob's next read lists it as removed, once.
    const std::string v2=d.get("version").string();
    CHECK(say(tb,"{\"op\":\"take\",\"id\":\""+k1+"\"}").status==200);
    CHECK(say(ta,"{\"op\":\"take\",\"id\":\""+k1+"\"}").status==409);
    auto gone=read(tb,v2);CHECK(crates(gone).empty()&&ids(gone,"removed")==std::vector<std::string>{k1});
    CHECK(ids(read(tb,gone.get("version").string()),"removed").empty()&&crates(read(tb)).empty());
    // The world file keeps what lies there.
    auto k2=json::parse(say(ta,R"({"op":"place","hash":77,"x":1,"y":2,"z":3})").body).get("id").string();
    World again([]{return std::string("t");});again.restore(s.w.snapshot());
    CHECK(again.snapshot().get("cargo").items().size()==1&&again.snapshot().get("cargo").items()[0].get("id").string()==k2);
    // A week later nobody took it: it goes, and readers are told.
    const std::string v3=read(tb).get("version").string();
    s.now+=CargoKeepSeconds+1;ta=s.token("ann");tb=s.token("bob");auto old=read(tb,v3);CHECK(crates(old).empty()&&ids(old,"removed")==std::vector<std::string>{k2});
    // At most 200 lying anywhere from one player.
    int refused=0;for(size_t i=0;i<=CargoPerPlayer;++i)if(say(ta,R"({"op":"place","hash":5,"x":1,"y":2,"z":3})").status==429)++refused;
    CHECK(refused==1);
    // The rule off: nothing set down is taken, nothing is listed.
    Server off("friends",{"cargo=off"});off.signIn("cid","Cid");auto tc=off.token("cid");
    CHECK(off.raw("POST","/s3cret/coop/cargo",R"({"op":"place","hash":5,"x":1,"y":2,"z":3})",{{"x-coop-token",tc}}).status==403);
    auto r=off.raw("GET","/s3cret/coop/cargo","",{{"x-coop-token",tc}});CHECK(r.status==200&&!json::parse(r.body).get("on").boolean(true));
    CHECK(presetPolicy("solo").cargo=="off"&&presetPolicy("friends").cargo=="on"&&presetPolicy("public").cargo=="on");
}
// What happens to the world's enemies (world.h eventSay, eventRead).
static void events(){
    Server s;s.signIn("ann","Ann");s.signIn("bob","Bob");auto ta=s.token("ann"),tb=s.token("bob");
    auto say=[&](const std::string& t,const std::string& body){return s.raw("POST","/s3cret/coop/events",body,{{"x-coop-token",t}}).status;};
    auto read=[&](const std::string& t,const std::string& since=""){auto r=s.raw("GET","/s3cret/coop/events"+(since.empty()?std::string():"?since="+since),"",{{"x-coop-token",t}});
        CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();};
    CHECK(s.raw("GET","/s3cret/coop/events").status==403&&say("nope","{}")==403);
    CHECK(say(ta,R"({"kind":"tank","key":1,"value":0})")==400&&say(ta,R"({"kind":"mule","key":1,"value":2000})")==400&&say(ta,"{")==400);
    auto first=read(tb);CHECK(first.get("on").boolean()&&first.get("events").items().empty()&&first.get("wait_ms").integer()==EventWaitMs);
    const std::string v1=first.get("version").string();
    CHECK(say(ta,R"({"kind":"mule","key":4503599627370495,"value":0})")==200&&say(ta,R"({"kind":"camp","key":77,"value":100})")==200);
    auto d=read(tb,v1);CHECK(d.get("events").items().size()==2&&!d.get("full").boolean(true));
    if(d.get("events").items().size()==2){auto& m=d.get("events").items()[1];CHECK(m.get("kind").string()=="mule"&&m.get("key").integer()==4503599627370495LL&&m.get("value").integer()==0);}
    CHECK(read(ta).get("events").items().empty());                          // never his own
    // The same MULE again (now asleep): one entry, the newest; listed again after the version.
    const std::string v2=d.get("version").string();
    CHECK(say(ta,R"({"kind":"mule","key":4503599627370495,"value":2})")==200);
    auto again=read(tb,v2);CHECK(again.get("events").items().size()==1&&again.get("events").items()[0].get("value").integer()==2);
    CHECK(read(tb).get("events").items().size()==2);
    // Timefall said for a cell lasts a minute (it is said again while it falls).
    {const std::string v3=read(tb).get("version").string();
     CHECK(say(ta,R"({"kind":"rain","key":123,"value":500})")==200);auto w=read(tb,v3).get("events").items();CHECK(w.size()==1&&w[0].get("kind").string()=="rain");
     s.now+=RainKeepSeconds+1;tb=s.token("bob");ta=s.token("ann");
     bool gone=true;for(auto& e:read(tb).get("events").items())if(e.get("kind").string()=="rain")gone=false;CHECK(gone);}
    // A structure's level (key: its online id's hash) stays a month; the rest go after an hour.
    CHECK(say(ta,R"({"kind":"level","key":123456789,"value":3})")==200);
    s.now+=EventKeepSeconds+1;tb=s.token("bob");ta=s.token("ann");
    {auto left=read(tb).get("events").items();CHECK(left.size()==1&&left[0].get("kind").string()=="level"&&left[0].get("value").integer()==3);}
    s.now+=LevelKeepSeconds;tb=s.token("bob");ta=s.token("ann");CHECK(read(tb).get("events").items().empty());
    // At most EventsMax at once: a new one past that is refused, an old one still changes.
    ta=s.token("ann");
    for(size_t i=0;i<EventsMax;++i)CHECK(say(ta,"{\"kind\":\"gazer\",\"key\":"+std::to_string(i)+",\"value\":1}")==200);
    CHECK(say(ta,"{\"kind\":\"gazer\",\"key\":-5,\"value\":1}")==429&&say(ta,"{\"kind\":\"gazer\",\"key\":7,\"value\":2}")==200);
    // The rule off: refused and nothing listed.
    Server off("friends",{"enemies=off"});off.signIn("cid","Cid");auto tc=off.token("cid");
    CHECK(off.raw("POST","/s3cret/coop/events",R"({"kind":"mule","key":1,"value":0})",{{"x-coop-token",tc}}).status==403);
    CHECK(off.raw("POST","/s3cret/coop/events",R"({"kind":"level","key":1,"value":2})",{{"x-coop-token",tc}}).status==200);   // levels follow the structures
    CHECK(presetPolicy("solo").enemies=="off"&&presetPolicy("friends").enemies=="on"&&presetPolicy("public").enemies=="on");
}
int main(){
    reads();held();cargo();events();
    if(failures){std::fprintf(stderr,"%u built oracle failures\n",failures);return 1;}
    std::puts("built oracles passed");return 0;
}
