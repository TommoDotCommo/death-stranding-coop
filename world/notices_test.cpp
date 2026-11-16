// What the game is told (README, "What the game is told") through the HTTP surface: a notice token for one player, taken
// as the launcher's other calls are; the mod's read with it, which lists that player's own objects built here with
// their state and why (held, kept, waiting, shared, remnant), everything on a first read and only what changed after a
// version of this process; never another player's object or an import, never a write, and the token is nothing else
// (no sign-in, no session, no relay); it expires unused and when the reads stop, a new one ends the older, and the
// world file keeps it and why each kept object is kept.
#include "front.h"
#include "api.h"
#include <algorithm>
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"notices oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);w.setName("North");}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    std::string token(const std::string& key){auto r=raw("POST","/s3cret/coop/notice-token","{\"player\":\""+key+"\"}");CHECK(r.status==200);
        auto d=r.status==200?json::parse(r.body):json::Value();CHECK(d.get("expires").integer()==now+NoticeTokenUnusedSeconds);return d.get("token").string();}
    Reply read(const std::string& token,const std::string& since=""){return raw("GET","/s3cret/coop/notices"+(since.empty()?std::string():"?since="+since),"",{{"x-coop-token",token}});}
    json::Value doc(const std::string& token,const std::string& since=""){auto r=read(token,since);CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& session,const char* type="l"){auto r=w.call(session,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3}",now);CHECK(r.status==200);return json::parse(r.body).get("id").string();}
};
// One object of an answer as "id state why".
// In order of id (the answer's order), so a list to compare with is sorted too.
static std::vector<std::string> rows(const json::Value& d){std::vector<std::string> out;for(auto& o:d.get("objects").items())out.push_back(o.get("id").string()+" "+o.get("state").string()+" "+o.get("why").string());return out;}
static std::vector<std::string> sorted(std::vector<std::string> v){std::sort(v.begin(),v.end());return v;}
static std::string row(const std::string& id,const char* state,const char* why){return id+" "+state+" "+why;}
static void reads(){
    // Members only: Ann is no member, so her ladder is held for her; Bob is one, and his is shared.
    Server s("friends",{"build=members"});
    auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");s.w.setPlayer(playerId("bob"),"member=yes");
    auto t=s.token("ann");CHECK(!t.empty());
    // A first read: every one of her objects (none yet), the server's name and a version to go on from.
    auto first=s.doc(t);
    CHECK(first.get("format").integer()==1&&first.get("name").string()=="North"&&first.get("full").boolean()&&first.get("objects").items().empty());
    const std::string v0=first.get("version").string();CHECK(v0.find('-')!=std::string::npos);
    auto ladder=s.make(ann),cairn=s.make(ann,"e"),theirs=s.make(bob);
    auto d=s.doc(t,v0);
    CHECK(!d.get("full").boolean()&&rows(d)==sorted({row(cairn,"held","member"),row(ladder,"held","member")}));
    CHECK(d.get("objects").items().size()==2&&d.get("objects").items()[1].get("kind").string()=="ladder"&&d.get("objects").items()[0].get("kind").string()=="cairn");
    const std::string v1=d.get("version").string();CHECK(v1!=v0);
    // Nothing changed: nothing listed. Bob's object never, by list or after a change of his.
    CHECK(s.doc(t,v1).get("objects").items().empty());
    s.w.withdraw(playerId("bob"),theirs);CHECK(s.doc(t,v1).get("objects").items().empty());
    for(auto& o:s.doc(t).get("objects").items())CHECK(o.get("id").string()!=theirs);
    // She is made a member and the owner accepts what the policy now allows: shared, and only that is listed.
    s.w.setPlayer(playerId("ann"),"member=yes");CHECK(s.w.reviewPlayer(playerId("ann"))==2);
    d=s.doc(t,v1);CHECK(rows(d)==sorted({row(cairn,"shared",""),row(ladder,"shared","")}));
    const std::string v2=d.get("version").string();
    // A withdrawal under "stay" (the public way) leaves a remnant: listed as one.
    s.w.setPolicy([&]{auto p=s.w.policy();p.withdrawal="stay";return p;}());
    s.w.withdraw(playerId("ann"),ladder);d=s.doc(t,v2);CHECK(rows(d)==sorted({row(ladder,"remnant","")}));
    // A version that is not one of this process (another server's start, a later stamp, garbage): every object of hers.
    for(std::string other:{std::string("0123abcd-1"),v2.substr(0,v2.find('-')+1)+"99999",std::string("x"),v2+"0x",v2.substr(0,v2.find('-')+1)})
        {auto e=s.doc(t,other);CHECK(e.get("full").boolean()&&rows(e).size()==2);}
}
static void states(){
    // Why each one is not shared, in the server's words for the mod to say.
    Server s("friends",{"limit=1"});auto ann=s.signIn("ann","Ann");auto t=s.token("ann");auto v=s.doc(t).get("version").string();
    auto a=s.make(ann),b=s.make(ann);
    CHECK(rows(s.doc(t,v))==sorted({row(a,"shared",""),row(b,"held","limit")}));
    Server nobody("friends",{"build=nobody"});auto bea=nobody.signIn("bea","Bea");auto tb=nobody.token("bea");auto c=nobody.make(bea);
    CHECK(rows(nobody.doc(tb))==sorted({row(c,"held","build")}));
    // Kept: his choice (offer_builds no, or his no to what waited), the owner's review, or a server that takes nothing.
    Server k("friends",{"accept_offers=review"});auto cid=k.signIn("cid","Cid");auto tc=k.token("cid");v=k.doc(tc).get("version").string();
    auto w1=k.make(cid),w2=k.make(cid);
    CHECK(rows(k.doc(tc,v))==sorted({row(w1,"waiting","owner"),row(w2,"waiting","owner")}));
    v=k.doc(tc,v).get("version").string();
    k.w.review(w1,false);k.w.review(w2,true);
    CHECK(rows(k.doc(tc,v))==sorted({row(w1,"kept","owner"),row(w2,"shared","")}));
    v=k.doc(tc,v).get("version").string();
    k.w.setPlayer(playerId("cid"),"offer_builds=ask");auto w3=k.make(cid),w4=k.make(cid);
    CHECK(rows(k.doc(tc,v))==sorted({row(w3,"waiting","you"),row(w4,"waiting","you")}));
    v=k.doc(tc,v).get("version").string();
    k.w.offer(playerId("cid"),w3,true);k.w.offer(playerId("cid"),w4,false);   // his yes goes to the owner; his no keeps it
    CHECK(rows(k.doc(tc,v))==sorted({row(w3,"waiting","owner"),row(w4,"kept","choice")}));
    v=k.doc(tc,v).get("version").string();
    k.w.setPlayer(playerId("cid"),"offer_builds=no");auto w5=k.make(cid);
    k.w.setPolicy([&]{auto p=k.w.policy();p.acceptOffers="never";return p;}());k.w.setPlayer(playerId("cid"),"offer_builds=default");auto w6=k.make(cid);
    CHECK(rows(k.doc(tc,v))==sorted({row(w5,"kept","choice"),row(w6,"kept","server")}));
    // The world file keeps why (and refuses a reason that is none, or one on an object that is not kept).
    World back;back.restore(k.w.snapshot());auto file=back.snapshot();
    for(auto& o:file.get("objects").items())if(o.get("id").string()==w1)CHECK(o.get("kept").string()=="owner");
    auto changed=[&](const std::string& id,const char* kept){auto file=k.w.snapshot();json::Value list=json::Value::array();
        for(auto o:file.get("objects").items()){if(o.get("id").string()==id)o.set("kept",kept);list.push(o);}file.set("objects",list);return file;};
    auto refused=[](const json::Value& file){try{World x;x.restore(file);}catch(const std::exception&){return true;}return false;};
    CHECK(refused(changed(w2,"owner"))&&refused(changed(w1,"whim"))&&!refused(changed(w1,"choice")));
}
static void scope(){
    Server s("friends",{"build=members"});auto ann=s.signIn("ann","Ann");auto t=s.token("ann");auto mine=s.make(ann);
    // Her builder id on another server is still not hers here: an import with it is never listed.
    World other([]{return "o"+std::to_string(++counter);});auto there=json::parse(other.signIn("ann","Ann","g").body).get("session").get("token").string();
    CHECK(other.call(there,"createObject","{\"t\":\"l\",\"q\":226,\"px\":9,\"py\":2,\"pz\":3}",Now).status==200);
    auto imported=s.w.importObjects(other.exportObjects(parseFilter("all"),"",Now),parseFilter("all"),false,Now);CHECK(imported.added==1&&s.w.objects()==2);
    CHECK(rows(s.doc(t))==sorted({row(mine,"held","member")}));
    // No token, a wrong one, the token in the address, another method: nothing.
    CHECK(s.read("").status==403&&s.read("nope").status==403&&s.read(t+"x").status==403&&s.read("a b").status==403);
    CHECK(s.raw("GET","/s3cret/coop/notices?token="+t).status==403&&s.raw("POST","/s3cret/coop/notices","",{{"x-coop-token",t}}).status==404);
    CHECK(s.raw("GET","/coop/notices","",{{"x-coop-token",t}}).status==404&&s.raw("GET","/s3cret/k/ann/coop/notices","",{{"x-coop-token",t}}).status==404);
    // The token reads and is nothing else: no sign-in ticket, no gateway session, no relay token; and no API call takes it.
    CHECK(s.raw("GET","/s3cret/t/"+t+"/auth/ds?provider=steam&display_name=Ann&code=00").status==403);
    CHECK(s.w.call(t,"getVersion","",Now).status==401&&!s.w.relayAdmit(1,t,"group",Now));
    CHECK(s.raw("GET","/s3cret/coop/mine","",{{"x-coop-token",t}}).status==400);
    // Taking one: a body of the call's form only, for an account key, as a change (POST); under identity none for anyone.
    CHECK(s.raw("GET","/s3cret/coop/notice-token?player=ann").status==404);
    for(auto bad:{"{}","{\"player\":\"\"}","{\"player\":\"a b\"}","{\"player\":\"ann\",\"all\":true}","[1]","{\"player\":7}"})
        CHECK(s.raw("POST","/s3cret/coop/notice-token",bad).status==400);
    // A new key becomes a player, within the world's players.
    Server solo("solo");solo.signIn("ann","Ann");CHECK(solo.raw("POST","/s3cret/coop/notice-token","{\"player\":\"bob\"}").status==403);
    // A new token ends the older one; each token reads its own player's objects.
    auto again=s.token("ann");CHECK(s.read(t).status==403&&s.read(again).status==200);
    auto bob=s.signIn("bob","Bob");auto tb=s.token("bob");auto his=s.make(bob);
    CHECK(rows(s.doc(tb))==sorted({row(his,"held","member")})&&rows(s.doc(again))==sorted({row(mine,"held","member")}));
}
static void lifetime(){
    Server s;s.signIn("ann","Ann");auto t=s.token("ann");
    // Unused: good for NoticeTokenUnusedSeconds.
    s.now+=NoticeTokenUnusedSeconds+1;CHECK(s.read(t).status==403);
    t=s.token("ann");s.now+=NoticeTokenUnusedSeconds-1;CHECK(s.read(t).status==200);
    // Then while the reads keep coming (an hour of reads 10 minutes apart), and not after NoticeTokenIdleSeconds without one.
    for(int i=0;i<6;++i){s.now+=600;CHECK(s.read(t).status==200);}
    s.now+=NoticeTokenIdleSeconds+1;CHECK(s.read(t).status==403);
    // The world file keeps a token, and the time of its last read now and then: a server started again within the idle
    // time still takes it (a first read there: every object).
    t=s.token("ann");CHECK(s.read(t).status==200);auto saved=s.w.snapshot();CHECK(saved.get("notice_tokens").items().size()==1);
    Server back;back.w.restore(saved);back.now=s.now+NoticeTokenIdleSeconds-60;CHECK(back.read(t).status==200&&back.doc(t).get("full").boolean());
    s.now+=NoticeTokenSaveSeconds;CHECK(s.read(t).status==200&&s.w.dirty());
    // A world without tokens writes none; a token for an account the world does not have is refused.
    Server none;CHECK(!none.w.snapshot().has("notice_tokens"));
    auto tokens=saved.get("notice_tokens");json::Value list=json::Value::array();
    for(auto x:tokens.items()){x.set("account","zed");list.push(x);}
    auto wrong=saved;wrong.set("notice_tokens",list);bool refused=false;try{World x;x.restore(wrong);}catch(const std::exception&){refused=true;}CHECK(refused);
}
int main(){
    reads();states();scope();lifetime();
    if(failures){std::fprintf(stderr,"notices oracle: %u failures\n",failures);return 1;}
    std::puts("notices tests passed");return 0;
}
