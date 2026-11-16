// The server policy (world.h, README "Server policy"): presets and settings, who builds and how much, the players'
// sharing choices, what waits for the builder or the owner and is handed to nobody else meanwhile, withdrawal that
// leaves remnants or removes, the owner removing one builder's objects, what linked servers are given, the saved world.
#include "world.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"policy oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
static bool throws(const std::function<void()>& f){try{f();}catch(const std::exception&){return true;}return false;}
struct Server {
    World w;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}){
        Policy p=presetPolicy(preset);for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    Reply build(const std::string& token,const char* type="l",int area=226,int64_t now=100){
        return w.call(token,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":"+std::to_string(area)+",\"px\":1,\"py\":2,\"pz\":3}",now);}
    std::string make(const std::string& token,const char* type="l",int area=226,int64_t now=100){
        auto r=build(token,type,area,now);CHECK(r.status==200);return json::parse(r.body).get("id").string();}
    size_t found(const std::string& token,int area=226,const char* counts=""){
        return json::parse(w.call(token,"findQpidObjects","{\"q\":"+std::to_string(area)+counts+"}",1).body).get("n").get("oa").items().size();}
    bool got(const std::string& token,const std::string& id){
        return json::parse(w.call(token,"getQpidObjects","{\"ids\":[\""+id+"\"]}",1).body).get("objects").items().size()==1;}
    int64_t likes(const std::string& token,const std::string& id){
        auto l=json::parse(w.call(token,"getQpidObjects","{\"ids\":[\""+id+"\"]}",1).body).get("objects");return l.items().empty()?-1:l.items()[0].get("lp").integer();}
    size_t history(const std::string& token){return json::parse(w.call(token,"getLikeHistory","{\"since\":0}",900).body).get("like_histories").items().size();}
    json::Value object(const std::string& id){auto all=w.snapshot();for(auto& o:all.get("objects").items())if(o.get("id").string()==id)return o;return {};}
    World::PlayerInfo info(const std::string& id){for(auto& i:w.playerList())if(i.id==id)return i;return {};}
};
static const std::string Ann=playerId("steam-a"),Bob=playerId("steam-b");

static void presets(){
    auto f=presetPolicy("friends"),s=presetPolicy("solo"),p=presetPolicy("public");
    CHECK(f.build=="everyone"&&f.acceptOffers=="auto"&&f.limit==0&&f.withdrawal=="go"&&f.players==int64_t(MaxPlayers)&&f.offerBuilds=="yes"&&f.offerOld=="no");
    CHECK(s.build=="everyone"&&s.acceptOffers=="auto"&&s.limit==0&&s.withdrawal=="go"&&s.players==1);
    CHECK(p.build=="members"&&p.acceptOffers=="review"&&p.limit==200&&p.withdrawal=="stay"&&p.join=="open"&&p.offerOld=="no");
    CHECK(throws([]{presetPolicy("open");})&&throws([]{presetPolicy("");}));
    Policy q=f;
    for(const char* good:{"join=open","build=nobody","accept_offers=never","limit=0","limit=4000","withdrawal=stay","players=1","players=16","offer_builds=ask","offer_old=yes"," build = members "})
        CHECK(!throws([&]{setPolicy(q,good);}));
    CHECK(q.build=="members"&&q.join=="open"&&q.acceptOffers=="never"&&q.limit==4000&&q.players==16&&q.offerBuilds=="ask"&&q.offerOld=="yes");
    for(const char* wrong:{"","build","build=","build=all","limit=-1","limit=4001","limit=x","players=0","players=17","offer_builds=maybe","offer_old=ask","withdraw=go","colour=red","accept_offers=Auto"}){
        Policy before=q;CHECK(throws([&]{setPolicy(q,wrong);}));CHECK(policyItems(q)==policyItems(before));}
    auto items=policyItems(p);CHECK(items.size()==8&&items[0].first=="join"&&items[3]==std::make_pair(std::string("limit"),std::string("200")));
    CHECK(Server().w.policy().preset=="friends");
}
static void building(){
    // Nobody builds: refused, nothing stored. Members only: a member builds, another does not.
    {Server s("friends",{"build=nobody"});auto ann=s.signIn("steam-a","Ann");CHECK(s.build(ann).status==403&&s.w.objects()==0);}
    {Server s("public",{"accept_offers=auto"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
     CHECK(s.build(ann).status==403&&s.w.objects()==0);
     s.w.setPlayer(Ann,"member=yes");CHECK(s.build(ann).status==200&&s.build(bob).status==403&&s.w.objects()==1&&s.found(bob)==1);
     s.w.setPlayer(Ann,"member=no");CHECK(s.build(ann).status==403);}
    // The limit per player: the next one is refused; others still build; a remnant does not count.
    {Server s("friends",{"limit=2","withdrawal=stay"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
     auto first=s.make(ann);s.make(ann,"r");CHECK(s.build(ann,"e").status==403&&s.w.objects()==2&&s.build(bob).status==200);
     CHECK(s.w.withdraw(Ann,first).remnants==1);CHECK(s.build(ann,"e").status==200&&s.build(ann,"e").status==403);}
    // One player (solo): a second account is refused, the first signs in again.
    {Server s("solo");s.signIn("steam-a","Ann");CHECK(s.w.signIn("steam-b","Bob","g").status==403&&s.w.signIn("steam-a","Ann","g").status==200);}
    // Every object records its builder: his id and his name when he built it.
    {Server s;auto ann=s.signIn("steam-a","Ann");auto id=s.make(ann);auto o=s.object(id);CHECK(o.get("creator").string()==Ann&&o.get("name").string()=="Ann"&&!o.has("share"));}
}
static void choices(){
    // offer_builds=no: stored for its builder, handed to nobody else, not by list, not by id, not to like.
    Server s;auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    CHECK(throws([&]{s.w.setPlayer(Ann,"offer_builds=maybe");})&&throws([&]{s.w.setPlayer("zygo_nobody","offer_builds=no");})&&throws([&]{s.w.setPlayer(Ann,"colour=red");}));
    s.w.setPlayer(Ann,"offer_builds=no");
    auto kept=s.make(ann);CHECK(s.w.objects()==1&&s.object(kept).get("share").string()=="kept");
    CHECK(s.found(bob)==0&&!s.got(bob,kept)&&s.got(ann,kept));
    CHECK(s.w.call(bob,"sendLike","{\"oid\":\""+kept+"\",\"lp\":3}",300).status==200&&s.likes(ann,kept)==0&&s.history(ann)==0);
    // Kept ones do not use up what the asker may be handed of a kind.
    s.w.setPlayer(Ann,"offer_builds=yes");auto shared=s.make(ann);
    for(int i=0;i<3;++i){s.w.setPlayer(Ann,"offer_builds=no");s.make(ann);}
    CHECK(s.found(bob,226,",\"o\":{\"ct\":{\"l\":1}}")==1&&s.found(bob)==1&&s.got(bob,shared));
    // ask: waits for its builder's answer, handed to nobody else meanwhile; yes shares it, no keeps it his.
    s.w.setPlayer(Ann,"offer_builds=ask");auto asked=s.make(ann),second=s.make(ann,"r");
    CHECK(s.object(asked).get("share").string()=="ask"&&s.found(bob)==1&&!s.got(bob,asked)&&s.got(ann,asked));
    {auto w=s.w.waiting();CHECK(w.size()==2&&w[0].forBuilder&&w[0].name=="Ann");}
    CHECK(throws([&]{s.w.review(asked,true);}));                           // not the owner's to answer
    auto bobs=s.make(bob);CHECK(throws([&]{s.w.offer(Ann,bobs,true);}));    // nor another player's
    CHECK(s.w.offer(Ann,asked,true)==1&&s.found(bob)==2&&s.got(bob,asked));
    CHECK(throws([&]{s.w.offer(Ann,asked,true);}));                         // answered already
    CHECK(s.w.offer(Ann,"",false)==1&&s.object(second).get("share").string()=="kept"&&!s.got(bob,second));
    // A player who chose nothing has the server's choice, and "default" goes back to it.
    {Server t("friends",{"offer_builds=no"});auto a=t.signIn("steam-a","Ann"),b=t.signIn("steam-b","Bob");
     t.make(a);CHECK(t.found(b)==0);t.w.setPlayer(Ann,"offer_builds=yes");t.make(a);CHECK(t.found(b)==1);
     t.w.setPlayer(Ann,"offer_builds=default");t.make(a);CHECK(t.found(b)==1);
     t.w.setPlayer(Ann,"offer_old=yes");CHECK(t.info(Ann).offerOld=="yes"&&t.info(Ann).offerBuilds.empty());}
}
static void review(){
    // accept_offers=review: an offered upload waits for the owner and is handed to nobody else until he accepts it.
    Server s("public",{"build=everyone"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    auto one=s.make(ann),two=s.make(ann,"r"),three=s.make(ann,"e");
    CHECK(s.object(one).get("share").string()=="review"&&s.found(bob)==0&&!s.got(bob,one)&&s.got(ann,one));
    {auto w=s.w.waiting();CHECK(w.size()==3&&!w[0].forBuilder);}
    CHECK(s.w.review(one,true)==1&&s.found(bob)==1&&s.got(bob,one));
    CHECK(s.w.review(two,false)==1&&s.found(bob)==1&&!s.got(bob,two)&&s.object(two).get("share").string()=="kept");
    CHECK(throws([&]{s.w.review(two,true);})&&throws([&]{s.w.review("lnothing00000",true);}));
    auto four=s.make(ann);CHECK(s.w.reviewPlayer(Ann)==2&&s.found(bob)==3&&s.got(bob,three)&&s.got(bob,four)&&s.w.waiting().empty());
    CHECK(throws([&]{s.w.reviewPlayer("zygo_nobody");}));
    // The builder's yes on an "ask" goes on to the owner's review.
    s.w.setPlayer(Ann,"offer_builds=ask");auto five=s.make(ann);CHECK(s.w.offer(Ann,five,true)==1&&s.object(five).get("share").string()=="review"&&!s.got(bob,five));
    // never: an offered upload is kept for its builder.
    {Server t("friends",{"accept_offers=never"});auto a=t.signIn("steam-a","Ann"),b=t.signIn("steam-b","Bob");auto id=t.make(a);
     CHECK(t.object(id).get("share").string()=="kept"&&t.found(b)==0);}
}
static void withdrawal(){
    // stay: what was shared stays as a remnant under its builder's name; what was never shared goes.
    Server s("friends",{"withdrawal=stay"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    auto shared=s.make(ann),other=s.make(ann,"r");s.w.setPlayer(Ann,"offer_builds=no");auto kept=s.make(ann,"e");s.w.setPlayer(Ann,"offer_builds=ask");auto asked=s.make(ann,"e");
    auto bobs=s.make(bob);
    CHECK(throws([&]{s.w.withdraw(Ann,bobs);})&&s.found(ann)==1);                           // only his own
    CHECK(throws([&]{s.w.withdraw("zygo_nobody","");})&&throws([&]{s.w.withdraw(Ann,"lnothing00000");}));
    auto r=s.w.withdraw(Ann,shared);CHECK(r.remnants==1&&r.removed==0&&s.found(bob)==2&&s.object(shared).get("remnant").boolean());
    CHECK(throws([&]{s.w.withdraw(Ann,shared);}));                                         // withdrawn already
    r=s.w.withdraw(Ann,"");CHECK(r.remnants==1&&r.removed==2&&s.w.objects()==3&&s.found(bob)==2&&s.object(kept).isNull()&&s.object(asked).isNull()&&!s.object(other).isNull());
    CHECK(s.info(Ann).remnants==2&&s.info(Ann).shared==0&&s.info(Ann).kept==0&&s.info(Ann).pending==0);
    // Nobody changes a remnant, its builder included; its likes stay on it and reach nobody's history.
    CHECK(s.w.call(ann,"deleteObject","{\"id\":\""+shared+"\"}",500).status==403&&s.w.call(ann,"updateObject","{\"id\":\""+shared+"\",\"ci\":{\"cp\":1}}",500).status==403);
    CHECK(s.w.call(bob,"deleteObject","{\"id\":\""+shared+"\"}",500).status==403&&s.w.objects()==3);
    CHECK(s.w.call(bob,"sendLike","{\"oid\":\""+shared+"\",\"lp\":4}",600).status==200&&s.likes(bob,shared)==4&&s.history(ann)==0);
    // Ann's name changes; the remnant keeps the one it was built under, also for a linked server.
    s.signIn("steam-a","Annabel");
    {auto out=s.w.exportObjects(parseFilter("all"),"",700);bool seen=false;
     for(auto& o:out.get("objects").items())if(o.get("creator").string()==Ann){seen=true;CHECK(o.get("name").string()=="Ann");}
     CHECK(seen&&out.get("objects").items().size()==3);}
    // go: removed for everyone.
    Server g;auto a=g.signIn("steam-a","Ann"),b=g.signIn("steam-b","Bob");auto id=g.make(a);g.make(a,"r");g.make(b);
    r=g.w.withdraw(Ann,id);CHECK(r.removed==1&&r.remnants==0&&g.found(b)==1&&!g.got(b,id));
    r=g.w.withdraw(Ann,"");CHECK(r.removed==1&&g.w.objects()==1&&g.found(a)==1);
}
static void removal(){
    // The owner removes everything one builder has: built, waiting, kept and remnants, and his likes' history with it.
    Server s("friends",{"withdrawal=stay"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    auto remnant=s.make(ann);s.make(ann,"r");s.w.setPlayer(Ann,"offer_builds=ask");s.make(ann,"e");s.w.setPlayer(Ann,"offer_builds=no");s.make(ann,"e");
    CHECK(s.w.withdraw(Ann,remnant).remnants==1);
    auto bobs=s.make(bob);s.w.call(ann,"sendLike","{\"oid\":\""+bobs+"\",\"lp\":2}",300);CHECK(s.history(bob)==1);
    s.w.setPlayer(Ann,"offer_builds=yes");auto annNew=s.make(ann);s.w.call(bob,"sendLike","{\"oid\":\""+annNew+"\",\"lp\":2}",300);CHECK(s.history(ann)==1);
    // Objects of two builders from one linked server, both in the same area as the rest.
    auto far=[](const char* id,const char* creator){Shared x;x.server="serverF1";x.id=id;x.creator=creator;x.name=creator;x.updated=10;x.post=json::parse("{\"t\":\"l\",\"q\":226}");return writeShared(x);};
    json::Value doc=json::Value::object();doc.set("format",1);doc.set("kind","coop-world-objects");doc.set("server","serverF1");
    {json::Value list=json::Value::array();list.push(far("lfar100000000","zygo_farone"));list.push(far("lfar200000000","zygo_farone"));list.push(far("lfar300000000","zygo_fartwo"));doc.set("objects",list);}
    s.w.importObjects(doc,parseFilter("all"),false,100);CHECK(s.w.imported()==3&&s.w.objects()==9);
    {auto list=s.w.playerList();CHECK(list.size()==4&&!list[2].known&&!list[3].known&&s.info("zygo_farone").imported==2);}
    CHECK(s.w.removePlayer(Ann)==5&&s.w.objects()==4&&s.found(bob)==3&&s.history(ann)==0&&s.history(bob)==1);
    CHECK(s.info(Ann).known&&s.info(Ann).shared+s.info(Ann).remnants+s.info(Ann).pending+s.info(Ann).kept==0);   // the player stays
    // A builder from a linked server: only what came from him goes, not the others' from the same server.
    CHECK(s.w.removePlayer("zygo_farone")==2&&s.w.imported()==1&&s.w.objects()==2&&s.found(bob)==1&&s.found(ann)==2);
    CHECK(throws([&]{s.w.removePlayer("zygo_nobody");})&&s.w.removePlayer(Ann)==0);
    // ... and it comes back from an import that still has it (the import filter is what keeps it out).
    s.w.importObjects(doc,parseFilter("all"),false,200);CHECK(s.w.imported()==3);
}
static void linked(){
    // A linked server is given only what every player here is handed, remnants included, under their builder's name.
    Server s("public",{"build=everyone","withdrawal=stay"});auto ann=s.signIn("steam-a","Ann");
    auto waits=s.make(ann);auto accepted=s.make(ann,"r");s.w.review(accepted,true);auto rejected=s.make(ann,"e");s.w.review(rejected,false);
    auto remnant=s.make(ann,"b");s.w.review(remnant,true);s.w.withdraw(Ann,remnant);
    auto out=s.w.exportObjects(parseFilter("all"),"",500);CHECK(out.get("objects").items().size()==2);
    for(auto& o:out.get("objects").items())CHECK(o.get("post").get("t").string()=="r"||o.get("post").get("t").string()=="b");
    auto page=json::parse(s.w.linkPull(parseFilter("all"),"{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\"}",500).body);CHECK(page.get("objects").items().size()==2);
    // Imports stay read-only, also for the policy commands.
    Server t;t.w.importObjects(out,parseFilter("all"),false,600);auto bea=t.signIn("steam-x","Bea");CHECK(t.found(bea)==2);
    auto annHere=t.signIn("steam-a","Ann");auto all=t.w.snapshot();std::string imported=all.get("objects").items()[0].get("id").string();
    CHECK(throws([&]{t.w.withdraw(Ann,imported);})&&t.w.withdraw(Ann,"").removed==0&&t.w.objects()==2);
    CHECK(t.w.call(annHere,"deleteObject","{\"id\":\""+imported+"\"}",700).status==403);
    (void)waits;
}
static void saved(){
    // The saved world keeps the policy, the players' choices, what waits, what is kept and the remnants.
    Server s("public",{"build=everyone","limit=3"});auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    s.w.setPlayer(Bob,"member=yes");s.w.setPlayer(Ann,"offer_old=yes");
    auto waits=s.make(ann),remnant=s.make(ann,"r");s.w.review(remnant,true);s.w.withdraw(Ann,remnant);s.w.setPlayer(Ann,"offer_builds=no");auto kept=s.make(ann,"e");
    auto doc=s.w.snapshot();World r([]{return std::string("fresh");});r.restore(json::parse(doc.dump()));
    CHECK(r.snapshot().dump()==doc.dump()&&doc.get("format").integer()==2);
    CHECK(policyItems(r.policy())==policyItems(s.w.policy())&&r.policy().preset=="public"&&r.policy().limit==3);
    auto rb=json::parse(r.call(bob,"findQpidObjects","{\"q\":226}",1).body).get("n").get("oa").items();
    CHECK(rb.size()==1&&rb[0].get("id").string()==remnant);
    CHECK(json::parse(r.call(bob,"getQpidObjects","{\"ids\":[\""+waits+"\",\""+kept+"\"]}",1).body).get("objects").items().empty());
    CHECK(r.call(ann,"deleteObject","{\"id\":\""+remnant+"\"}",1).status==403);
    {bool member=false,old=false;for(auto& i:r.playerList()){if(i.id==Bob)member=i.member;if(i.id==Ann)old=i.offerOld=="yes"&&i.offerBuilds=="no";}CHECK(member&&old);}
    // A world file from before the policy: friends, every object shared.
    {auto old=doc;old.set("format",1);old.erase("policy");json::Value objects=json::Value::array();
     for(auto o:doc.get("objects").items()){o.erase("share");o.erase("remnant");o.erase("name");objects.push(o);}old.set("objects",objects);
     World o([]{return std::string("fresh");});o.restore(old);
     CHECK(policyItems(o.policy())==policyItems(presetPolicy("friends"))&&o.policy().preset=="friends");
     CHECK(json::parse(o.call(bob,"findQpidObjects","{\"q\":226}",1).body).get("n").get("oa").items().size()==3);}
    // Values that are not allowed refuse the file.
    auto broken=[&](const std::function<void(json::Value&)>& change){auto d=json::parse(doc.dump());change(d);World o([]{return std::string("fresh");});return throws([&]{o.restore(d);});};
    CHECK(broken([](json::Value& d){d.set("format",3);}));
    CHECK(broken([](json::Value& d){json::Value p=d.get("policy");p.set("build","anyone");d.set("policy",p);}));
    CHECK(broken([](json::Value& d){json::Value p=d.get("policy");p.set("preset","club");d.set("policy",p);}));
    CHECK(broken([](json::Value& d){json::Value p=d.get("policy");p.set("speed",1);d.set("policy",p);}));
    CHECK(broken([](json::Value& d){json::Value p=d.get("policy");p.set("limit",-4);d.set("policy",p);}));
    CHECK(broken([](json::Value& d){json::Value list=json::Value::array();for(auto o:d.get("objects").items()){o.set("share","later");list.push(o);}d.set("objects",list);}));
    CHECK(broken([](json::Value& d){json::Value list=json::Value::array();for(auto p:d.get("players").items()){p.set("offer_builds","maybe");list.push(p);}d.set("players",list);}));
    CHECK(broken([](json::Value& d){json::Value list=json::Value::array();for(auto o:d.get("objects").items()){json::Value from=json::Value::object();from.set("server","serverF1");from.set("id","lfar100000000");
        from.set("via","serverF1");o.set("origin",from);o.set("remnant",true);list.push(o);break;}d.set("objects",list);}));
    CHECK(!broken([](json::Value&){}));
}
int main(){
    for(auto part:{presets,building,choices,review,withdrawal,removal,linked,saved}){
        try{part();}catch(const std::exception& e){std::fprintf(stderr,"policy oracle: threw %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("policy tests passed");return 0;
}
