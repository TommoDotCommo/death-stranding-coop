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
int main(){
    reads();held();
    if(failures){std::fprintf(stderr,"%u built oracle failures\n",failures);return 1;}
    std::puts("built oracles passed");return 0;
}
