// The launcher's API (api.h) through the HTTP surface: the policy in words, one player's choices, counts and objects,
// his offer and withdrawal while the server runs; under the prefix only, changes by POST only, a body that names
// nothing changes nothing, nobody acts on another's objects or on an import, member and the policy stay the owner's.
#include "front.h"
#include "api.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"api oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
struct Server {
    World w;Front f;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        // Identity none (the public preset's keys are keys_test.cpp's): unsigned calls act for any player.
        Policy p=presetPolicy(preset);p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    Reply raw(const char* method,const std::string& target,const std::string& body=""){Request r;r.method=method;r.target=target;r.body=body;r.headers["host"]="10.0.0.2:8089";return f.handle(r,1);}
    Reply get(const std::string& call,const std::string& key=""){return raw("GET","/s3cret/coop/"+call+(key.empty()?"":"?player="+key));}
    Reply post(const std::string& call,const std::string& body){return raw("POST","/s3cret/coop/"+call,body);}
    json::Value doc(const Reply& r){CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& token,const char* type="l"){auto r=w.call(token,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3}",100);CHECK(r.status==200);return json::parse(r.body).get("id").string();}
    size_t found(const std::string& token){return json::parse(w.call(token,"findQpidObjects","{\"q\":226}",1).body).get("n").get("oa").items().size();}
    std::vector<json::Value> mine(const std::string& key){auto d=doc(get("mine",key));auto list=d.get("objects").items();return list;}
    json::Value of(const std::string& key,const std::string& id){for(auto& o:mine(key))if(o.get("id").string()==id)return o;return {};}
    size_t players(){return w.snapshot().get("players").items().size();}
    json::Value object(const std::string& id){auto all=w.snapshot();for(auto& o:all.get("objects").items())if(o.get("id").string()==id)return o;return {};}
};
static void policy(){
    Server s;
    // Under the prefix only, never under /k/NAME, and only the known calls with their method.
    CHECK(s.raw("GET","/coop/policy").status==404&&s.raw("GET","/other/coop/policy").status==404&&s.raw("GET","/s3cret/k/ann/coop/policy").status==404);
    CHECK(s.raw("POST","/s3cret/coop/policy","{}").status==404&&s.get("nothing").status==404&&s.raw("GET","/s3cret/coop/").status==404);
    auto p=s.doc(s.get("policy"));
    CHECK(p.get("format").integer()==1&&p.get("server").string()==s.w.serverId()&&p.get("preset").string()=="friends");
    CHECK(p.get("players").get("count").integer()==0&&p.get("players").get("limit").integer()==16);
    auto& settings=p.get("settings").items();CHECK(settings.size()==12);
    for(auto& item:settings)CHECK(item.get("text").string().size()>20&&item.get("value").string()==item.get("preset").string());
    CHECK(settings.size()==12&&settings[1].get("key").string()=="build"&&settings[1].get("text").string()=="What any player builds here may be shared with the others.");
    Server pub("public",{"limit=50"});
    auto q=pub.doc(pub.get("policy"));auto& qs=q.get("settings").items();
    CHECK(q.get("preset").string()=="public"&&qs.size()==12&&qs[1].get("value").string()=="members"&&qs[3].get("value").string()=="50"&&qs[3].get("preset").string()=="200");
    CHECK(qs.size()==12&&qs[3].get("text").string().find("At most 50 of your objects")==0&&qs[2].get("text").string().find("owner's review")!=std::string::npos&&qs[4].get("text").string().find("remnant")!=std::string::npos);
    Server solo("solo");solo.signIn("a","A");auto r=solo.doc(solo.get("policy"));
    CHECK(r.get("players").get("count").integer()==1&&r.get("players").get("limit").integer()==1&&r.get("settings").items()[5].get("text").string()=="One player only: a personal archive. It has 1 now.");
    for(const char* k:{"join","build","accept_offers","limit","withdrawal","players","offer_builds","offer_old","order","per_builder","spread","identity"})CHECK(settingText(k,"x",0).size()>20);
    CHECK(qs[8].get("text").string().find("most liked")!=std::string::npos&&qs[9].get("text").string().find("At most 3 ")==0&&settings[10].get("text").string().find("20 m")!=std::string::npos);
}
static void choices(){
    Server s("friends");
    // A key nobody has signed in with: he is not made by asking.
    auto me=s.doc(s.get("me","ann"));
    CHECK(!me.get("known").boolean()&&me.get("player").string()==playerId("ann")&&me.get("applies").get("offer_builds").string()=="yes"&&s.players()==0);
    CHECK(s.mine("ann").empty()&&s.players()==0);
    // His choices before his game first signs in: he is made then, and the game's sign-in is the same player.
    me=s.doc(s.post("choices","{\"player\":\"ann\",\"offer_builds\":\"ask\",\"offer_old\":\"yes\"}"));
    CHECK(me.get("known").boolean()&&me.get("choices").get("offer_builds").string()=="ask"&&me.get("applies").get("offer_old").string()=="yes"&&s.players()==1);
    auto ann=s.signIn("ann","Ann");CHECK(s.players()==1&&s.doc(s.get("me","ann")).get("name").string()=="Ann");
    auto id=s.make(ann);CHECK(s.object(id).get("share").string()=="ask");
    me=s.doc(s.post("choices","{\"player\":\"ann\",\"offer_builds\":\"default\"}"));
    CHECK(me.get("choices").get("offer_builds").string()==""&&me.get("choices").get("offer_old").string()=="yes"&&me.get("applies").get("offer_builds").string()=="yes");
    // Refused, and nothing changed: bad values, nothing chosen, a field that is not his to set, keys that are no keys.
    auto before=s.w.snapshot().dump();
    for(const char* body:{"{\"player\":\"ann\",\"offer_builds\":\"maybe\"}","{\"player\":\"ann\",\"offer_old\":\"ask\"}","{\"player\":\"ann\"}","{\"player\":\"ann\",\"member\":\"yes\"}",
                          "{\"player\":\"ann\",\"offer_builds\":\"yes\",\"member\":\"yes\"}","{\"player\":\"ann\",\"offer_builds\":\"yes\",\"build\":\"everyone\"}","{\"player\":\"ann\",\"offer_builds\":true}",
                          "{\"player\":\"\",\"offer_builds\":\"yes\"}","{\"player\":\"a/b\",\"offer_builds\":\"yes\"}","{\"player\":\"a b\",\"offer_builds\":\"yes\"}","{\"offer_builds\":\"yes\"}",
                          "[\"ann\"]","not json",""}){
        CHECK(s.post("choices",body).status!=200);CHECK(s.w.snapshot().dump()==before);}
    CHECK(s.post("choices","{\"player\":\""+std::string(97,'k')+"\",\"offer_builds\":\"yes\"}").status==400&&s.players()==1);
    for(const char* key:{"","a%2Fb","a+b"})CHECK(s.get("me",key).status==400&&s.get("mine",key).status==400);
    CHECK(s.raw("GET","/s3cret/coop/me").status==400);
    CHECK(!s.doc(s.get("me","ann")).get("member").boolean());
    // A change is never a GET, whatever it carries.
    CHECK(s.raw("GET","/s3cret/coop/choices?player=ann","{\"player\":\"ann\",\"offer_builds\":\"no\"}").status==404&&s.w.snapshot().dump()==before);
    // A world with all its players makes nobody new by choices either.
    Server solo("solo");solo.signIn("a","A");
    CHECK(solo.post("choices","{\"player\":\"b\",\"offer_builds\":\"no\"}").status==403&&solo.players()==1);
    CHECK(solo.post("choices","{\"player\":\"a\",\"offer_builds\":\"no\"}").status==200);
}
static void objects(){
    Server s("public",{"build=everyone"});   // offers wait for the owner's review
    s.post("choices","{\"player\":\"ann\",\"offer_builds\":\"ask\"}");
    auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");
    auto a1=s.make(ann),a2=s.make(ann,"e"),b1=s.make(bob);
    auto o=s.of("ann",a1);CHECK(o.get("state").string()=="waiting"&&o.get("waiting_for").string()=="you"&&o.get("kind").string()=="ladder"&&o.get("area").integer()==226&&o.get("type").string()=="l");
    CHECK(s.of("ann",a2).get("kind").string()=="cairn"&&s.mine("ann").size()==2&&s.mine("bob").size()==1&&s.of("bob",a1).isNull()&&s.of("ann",b1).isNull());
    CHECK(s.of("bob",b1).get("waiting_for").string()=="owner");
    auto me=s.doc(s.get("me","ann"));CHECK(me.get("waiting_you").integer()==2&&me.get("shared").integer()==0&&me.get("known").boolean());
    // A body that names nothing changes nothing; nor does naming both.
    auto before=s.w.snapshot().dump();
    for(const char* call:{"offer","withdraw"})for(const char* body:{"{\"player\":\"ann\"}","{\"player\":\"ann\",\"all\":false}","{\"player\":\"ann\",\"object\":\"\"}","{\"player\":\"ann\",\"all\":true,\"object\":\"x\"}","{\"player\":\"ann\",\"all\":\"yes\"}"}){
        CHECK(s.post(call,body).status==400);CHECK(s.w.snapshot().dump()==before);}
    CHECK(s.post("offer","{\"player\":\"ann\",\"object\":\""+a1+"\",\"answer\":\"maybe\"}").status==400&&s.w.snapshot().dump()==before);
    CHECK(s.post("withdraw","{\"player\":\"ann\",\"object\":\""+a1+"\",\"answer\":\"no\"}").status==400&&s.w.snapshot().dump()==before);
    // Nobody acts on another player's objects, and a GET changes nothing.
    CHECK(s.post("withdraw","{\"player\":\"bob\",\"object\":\""+a1+"\"}").status==403&&s.post("offer","{\"player\":\"bob\",\"object\":\""+a1+"\"}").status==403);
    CHECK(s.raw("GET","/s3cret/coop/withdraw?player=ann","{\"player\":\"ann\",\"all\":true}").status==404&&s.raw("PUT","/s3cret/coop/offer","{\"player\":\"ann\",\"all\":true}").status==404);
    CHECK(s.post("withdraw","{\"player\":\"nobody\",\"all\":true}").status==403);
    CHECK(s.w.snapshot().dump()==before);
    // His answer: yes goes on to the owner (review), no keeps it his.
    auto r=s.doc(s.post("offer","{\"player\":\"ann\",\"object\":\""+a1+"\"}"));
    CHECK(r.get("offered").integer()==1&&r.get("me").get("waiting_owner").integer()==1&&s.of("ann",a1).get("waiting_for").string()=="owner"&&s.found(bob)==0);
    r=s.doc(s.post("offer","{\"player\":\"ann\",\"object\":\""+a2+"\",\"answer\":\"no\"}"));
    CHECK(r.get("kept").integer()==1&&s.of("ann",a2).get("state").string()=="kept"&&!s.of("ann",a2).has("waiting_for"));
    CHECK(s.post("offer","{\"player\":\"ann\",\"object\":\""+a1+"\"}").status==403);   // it waits for the owner now, not for him
    CHECK(s.w.review(a1,true)==1&&s.of("ann",a1).get("state").string()=="shared"&&s.found(bob)==1);
    s.w.call(bob,"sendLike","{\"oid\":\""+a1+"\",\"lp\":3,\"lp2\":1}",5);
    CHECK(s.of("ann",a1).get("likes").integer()==4&&s.doc(s.get("me","ann")).get("likes").integer()==4);
    // Held by the policy: the reason in words.
    {Policy p=s.w.policy();setPolicy(p,"build=members");s.w.setPolicy(p);}
    auto a3=s.make(ann);o=s.of("ann",a3);
    CHECK(o.get("state").string()=="held"&&o.get("held").string()=="member"&&o.get("held_text").string()=="only members build on this server"&&o.get("waiting_for").string()=="you");
    CHECK(s.doc(s.get("me","ann")).get("held").get("member").integer()==1);
    CHECK(s.post("offer","{\"player\":\"ann\",\"object\":\""+a3+"\"}").status==403);   // the policy does not allow it yet
    CHECK(s.doc(s.post("offer","{\"player\":\"ann\",\"all\":true}")).get("offered").integer()==0&&s.of("ann",a3).get("state").string()=="held");
    // Withdrawn with stay: the shared one stays as a remnant nobody owns, the rest go; its likes are nobody's.
    r=s.doc(s.post("withdraw","{\"player\":\"ann\",\"all\":true}"));
    CHECK(r.get("remnants").integer()==1&&r.get("removed").integer()==2&&s.mine("ann").size()==1&&s.of("ann",a1).get("state").string()=="remnant");
    me=r.get("me");CHECK(me.get("remnants").integer()==1&&me.get("likes").integer()==0&&me.get("shared").integer()==0&&s.found(bob)==1);
    CHECK(s.post("withdraw","{\"player\":\"ann\",\"object\":\""+a1+"\"}").status==403&&s.post("withdraw","{\"player\":\"ann\",\"all\":true}").status==200&&s.of("ann",a1).get("state").string()=="remnant");
    // An import whose builder id is his is not his to see or act on here: the other server says who built it.
    Shared in;in.server="otherServer01";in.id="lABCDEFGHIJKL";in.creator=playerId("ann");in.name="Ann";in.updated=50;in.post=json::parse("{\"t\":\"l\",\"q\":226}");
    s.w.importShared("otherServer01",{in},parseFilter("all"),false,60);
    std::string imported;auto all=s.w.snapshot();for(auto& x:all.get("objects").items())if(x.has("origin"))imported=x.get("id").string();
    CHECK(!imported.empty()&&s.of("ann",imported).isNull()&&s.mine("ann").size()==1&&s.doc(s.get("me","ann")).get("remnants").integer()==1);
    CHECK(s.post("withdraw","{\"player\":\"ann\",\"object\":\""+imported+"\"}").status==403&&s.post("offer","{\"player\":\"ann\",\"object\":\""+imported+"\"}").status==403);
    // Go: removed for everyone.
    Server g("friends");auto c=g.signIn("cat","Cat"),d=g.signIn("dan","Dan");auto c1=g.make(c);
    CHECK(g.found(d)==1&&g.doc(g.post("withdraw","{\"player\":\"cat\",\"object\":\""+c1+"\"}")).get("removed").integer()==1&&g.found(d)==0&&g.mine("cat").empty());
}
int main(){
    try{policy();choices();objects();}
    catch(const std::exception& e){std::fprintf(stderr,"api oracle: refused: %s\n",e.what());return 1;}
    if(failures)return 1;
    std::puts("launcher API tests passed");return 0;
}
