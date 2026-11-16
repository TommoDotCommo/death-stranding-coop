// Linked servers (world.h, link.h): the filters, objects exported from one world and imported into another under ids
// of their own, taking the same export twice, forgetting a server, objects that never go back to where they came
// from, documents and answers that are refused whole, and one server asking another page by page.
#include "link.h"
#include <algorithm>
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"link oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
struct Server {
    World w;std::string id;
    explicit Server(const std::string& name):w([]{return "tok"+std::to_string(++counter);}),id(name){w.setServerId(name);}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& token,const char* type,int area,int64_t now,const std::string& more=""){
        auto r=w.call(token,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":"+std::to_string(area)+",\"px\":1,\"py\":2,\"pz\":3"+more+"}",now);
        return json::parse(r.body).get("id").string();}
    json::Value out(const char* filter,int64_t now,const std::string& forServer=""){return w.exportObjects(parseFilter(filter),forServer,now);}
    size_t found(const std::string& token,int area){
        return json::parse(w.call(token,"findQpidObjects","{\"q\":"+std::to_string(area)+"}",1).body).get("n").get("oa").items().size();}
    json::Value get(const std::string& token,const std::string& id){
        auto list=json::parse(w.call(token,"getQpidObjects","{\"ids\":[\""+id+"\"]}",1).body).get("objects");
        return list.items().empty()?json::Value():list.items()[0];}
    std::vector<std::string> ids(){std::vector<std::string> v;auto all=w.snapshot();for(auto& o:all.get("objects").items())v.push_back(o.get("id").string());return v;}
};
static size_t count(const json::Value& document){return document.get("objects").items().size();}
static bool refusedWhole(World& w,const json::Value& document,const char* filter="all",bool prune=false){
    auto before=w.snapshot().dump();bool threw=false;
    try{w.importObjects(document,parseFilter(filter),prune,5000);}catch(const std::exception&){threw=true;}
    return threw&&w.snapshot().dump()==before;}
static json::Value entry(const std::string& server,const std::string& id,const char* type="l",int64_t updated=10){
    Shared s;s.server=server;s.id=id;s.creator="zygo_someone";s.name="Far";s.updated=updated;s.post=json::parse(std::string("{\"t\":\"")+type+"\",\"q\":226}");return writeShared(s);}
static json::Value documentOf(const std::string& server,std::vector<json::Value> objects){
    json::Value d=json::Value::object();d.set("format",1);d.set("kind","coop-world-objects");d.set("server",server);
    json::Value list=json::Value::array();for(auto& o:objects)list.push(o);d.set("objects",list);return d;}

static void filters(){
    for(const char* good:{"all","none"," types=l,r ; areas=226,-1 ","types=ladder,rope,cairn","owners=Ann B,zygo_x","max-age=30d","max-age=0s","likes=no;imported=yes"}){
        bool threw=false;try{parseFilter(good);}catch(const std::exception&){threw=true;}CHECK(!threw);}
    for(const char* wrong:{"","types","types=","types=ladders","types=L","types=l;types=r","areas=x","areas=1.5","max-age=30","max-age=d","max-age=-1d","likes=maybe","colour=red","types=l;;areas=1","all;types=l"}){
        bool threw=false;try{parseFilter(wrong);}catch(const std::exception&){threw=true;}CHECK(threw);}
    auto f=parseFilter("types=ladder,r;owners=Ann;areas=226;max-age=2h;likes=no;imported=yes");
    CHECK(f.types==std::set<std::string>{"l","r"}&&f.owners.count("Ann")&&f.areas.count(226)&&f.maxAgeSeconds==7200&&!f.likes&&f.imported&&!f.none);
    CHECK(parseFilter("none").none&&!parseFilter("all").none&&parseFilter("all").likes&&!parseFilter("all").imported);
    // What a server gives: by kind, by builder (name or id), by area, by age, and all of them together.
    Server a("serverA1");auto ann=a.signIn("steam-a","Ann"),bob=a.signIn("steam-b","Bob");
    a.make(ann,"l",226,100);a.make(ann,"r",226,200);a.make(ann,"e",300,1000);a.make(bob,"l",226,1000);
    CHECK(count(a.out("all",1100))==4);CHECK(count(a.out("none",1100))==0);
    CHECK(count(a.out("types=l",1100))==2);CHECK(count(a.out("types=ladder,cairn",1100))==3);
    CHECK(count(a.out("owners=Ann",1100))==3);CHECK(count(a.out(("owners="+playerId("steam-b")).c_str(),1100))==1);CHECK(count(a.out("owners=Nobody",1100))==0);
    CHECK(count(a.out("areas=300",1100))==1);CHECK(count(a.out("areas=226,300",1100))==4);
    CHECK(count(a.out("max-age=500s",1100))==2);CHECK(count(a.out("max-age=1d",1100))==4);
    CHECK(count(a.out("types=l;owners=Ann;areas=226",1100))==1);
    auto all=a.out("all",1100);
    CHECK(all.get("server").string()=="serverA1"&&all.get("format").integer()==1&&all.get("kind").string()=="coop-world-objects");
    // What a server takes is narrowed the same way.
    for(auto& c:std::vector<std::pair<const char*,size_t>>{{"all",4},{"none",0},{"types=r",1},{"owners=Bob",1},{"areas=300",1},{"max-age=500s",2},{"types=l;max-age=500s",1}}){
        Server b("serverB1");auto r=b.w.importObjects(all,parseFilter(c.first),false,1100);CHECK(r.added==c.second&&r.filtered==4-c.second&&b.w.objects()==c.second);}
}
static void importAndIds(){
    Server a("serverA1"),b("serverB1");auto ann=a.signIn("steam-a","Ann"),bea=b.signIn("steam-x","Bea");
    auto ladder=a.make(ann,"l",226,100),rope=a.make(ann,"r",226,200);
    auto document=a.out("all",300);
    auto r=b.w.importObjects(document,parseFilter("all"),false,300);CHECK(r.added==2&&r.updated==0&&r.unchanged==0&&b.w.objects()==2&&b.w.imported()==2);
    // Ids of this server's own: the game's shape, the kind in the first letter, and not the id it had over there.
    auto ids=b.ids();CHECK(ids.size()==2);
    for(auto& id:ids){CHECK(id.size()==13&&(id[0]=='l'||id[0]=='r')&&id!=ladder&&id!=rope);for(char c:id)CHECK(std::isalnum(static_cast<unsigned char>(c)));}
    CHECK(ids.size()==2&&ids[0][0]!=ids[1][0]);
    // The players here are handed them like any other player's object, with the builder's id and name from over there
    // (his server's display name after it: that one has none, so the first characters of its id; names_test.cpp).
    CHECK(b.found(bea,226)==2);
    auto users=json::parse(b.w.call(bea,"lookup","{\"ids\":[\""+playerId("steam-a")+"\"]}",1).body).get("users");
    CHECK(users.items().size()==1&&users.items()[0].get("displayName").string()=="Ann \xC2\xB7 serverA1"&&users.items()[0].get("boundAccount").string().empty());
    // The same export again: nothing is added, nothing changes.
    auto before=b.w.snapshot().dump();
    r=b.w.importObjects(document,parseFilter("all"),false,400);CHECK(r.added==0&&r.updated==0&&r.unchanged==2&&b.w.objects()==2&&b.w.snapshot().dump()==before);
    r=b.w.importObjects(a.out("all",900),parseFilter("all"),true,900);CHECK(r.added==0&&r.removed==0&&r.unchanged==2&&b.w.objects()==2);
    // The same origin is the same id in any world, and again after it was forgotten.
    Server c("serverC1");c.w.importObjects(document,parseFilter("all"),false,300);CHECK(c.ids()==ids);
    CHECK(b.w.forget("serverA1")==2&&b.w.objects()==0);b.w.importObjects(document,parseFilter("all"),false,300);CHECK(b.ids()==ids);
    // An id from over there that is already an id here: both objects stay, and the one built here is untouched.
    auto mine=b.make(bea,"l",226,500);
    r=b.w.importObjects(documentOf("serverX1",{entry("serverX1",mine)}),parseFilter("all"),false,600);
    CHECK(r.added==1&&b.w.objects()==4&&b.get(bea,mine).get("c").string()==playerId("steam-x"));
    // The same id on two servers is two objects.
    r=b.w.importObjects(documentOf("serverY1",{entry("serverY1",mine)}),parseFilter("all"),false,600);CHECK(r.added==1&&b.w.objects()==5);
    // Nobody here changes or removes an imported object, not even the same account signed in here.
    auto annHere=b.signIn("steam-a","Ann");
    for(auto& id:ids){
        CHECK(b.w.call(annHere,"deleteObject","{\"id\":\""+id+"\"}",700).status==403);
        CHECK(b.w.call(annHere,"updateObject","{\"id\":\""+id+"\",\"ci\":{\"cp\":1}}",700).status==403);
        CHECK(b.w.call(bea,"deleteObject","{\"id\":\""+id+"\"}",700).status==403);}
    CHECK(b.w.objects()==5);
    CHECK(b.w.call(bea,"deleteObject","{\"id\":\""+mine+"\"}",700).status==200);   // his own he still removes
    // Likes given here stay when the object arrives again with more likes from over there.
    auto ladderHere=ids[0][0]=='l'?ids[0]:ids[1];
    CHECK(b.w.call(bea,"sendLike","{\"oid\":\""+ladderHere+"\",\"lp\":3,\"lp2\":0}",800).status==200);CHECK(b.get(bea,ladderHere).get("lp").integer()==3);
    auto cat=a.signIn("steam-c","Cat");CHECK(a.w.call(cat,"sendLike","{\"oid\":\""+ladder+"\",\"lp\":2,\"lp2\":0}",800).status==200);
    r=b.w.importObjects(a.out("all",900),parseFilter("all"),false,900);CHECK(r.updated==1&&r.unchanged==1);CHECK(b.get(bea,ladderHere).get("lp").integer()==5);
    {Server d("serverD1");d.w.importObjects(a.out("likes=no",900),parseFilter("all"),false,900);auto t=d.signIn("k","D");CHECK(d.get(t,ladderHere).get("lp").integer()==0);}
    // Changed and removed over there: with prune the import follows, without it nothing is removed.
    CHECK(a.w.call(ann,"updateObject","{\"id\":\""+ladder+"\",\"ci\":{\"cp\":7}}",1000).status==200);
    CHECK(a.w.call(ann,"deleteObject","{\"id\":\""+rope+"\"}",1000).status==200);
    auto later=a.out("all",1100);
    r=b.w.importObjects(later,parseFilter("all"),false,1100);CHECK(r.updated==1&&r.removed==0&&b.w.imported()==4);
    CHECK(b.get(bea,ladderHere).get("ci").get("cp").integer()==7&&b.get(bea,ladderHere).get("ut").integer()==1000&&b.get(bea,ladderHere).get("lp").integer()==5);
    r=b.w.importObjects(later,parseFilter("all"),true,1100);CHECK(r.removed==1&&r.unchanged==1&&b.w.imported()==3);
    // ... and prune takes what the filter no longer passes, but never another server's objects or this one's own.
    auto own=b.make(bea,"l",226,1100);
    r=b.w.importObjects(later,parseFilter("types=r"),true,1100);CHECK(r.removed==1&&r.filtered==1&&b.w.imported()==2&&!b.get(bea,own).isNull());
    // Forget one server: the others' and this one's stay.
    CHECK(b.w.forget("serverX1")==1&&b.w.imported()==1&&b.w.objects()==2);CHECK(b.w.forget("serverX1")==0);CHECK(b.w.forget("serverY1")==1&&b.w.objects()==1&&!b.get(bea,own).isNull());
}
static void noLoops(){
    Server a("serverA1"),b("serverB1"),c("serverC1");auto ann=a.signIn("steam-a","Ann"),bea=b.signIn("steam-x","Bea");
    a.make(ann,"l",226,100);a.make(ann,"l",226,101);b.make(bea,"r",226,100);
    b.w.importObjects(a.out("all",200),parseFilter("all"),true,200);CHECK(b.w.objects()==3);
    // What was imported is not handed on unless the filter says so, and never back to where it came from.
    CHECK(count(b.out("all",300))==1);CHECK(count(b.out("all",300,"serverA1"))==1);CHECK(count(b.out("all",300,"serverC1"))==1);
    CHECK(count(b.out("imported=yes",300,"serverA1"))==1);
    auto onward=b.out("imported=yes",300,"serverC1");CHECK(count(onward)==3);
    // The taking side has to say so too, and the objects keep the server they were first built on.
    {Server c2("serverC2");auto r=c2.w.importObjects(onward,parseFilter("all"),true,300);CHECK(r.added==1&&r.filtered==2);}
    auto r=c.w.importObjects(onward,parseFilter("imported=yes"),true,300);CHECK(r.added==3);
    {auto o=c.w.origins();CHECK(o.size()==2&&o[0].server=="serverA1"&&o[0].via=="serverB1"&&o[0].objects==2&&o[1].server=="serverB1"&&o[1].via=="serverB1"&&o[1].objects==1);}
    // Round the triangle: C gives everything on, and A's own two never come back to A, by either way.
    CHECK(count(c.out("imported=yes",400,"serverA1"))==1);CHECK(count(c.out("imported=yes",400,"serverB1"))==0);
    auto file=c.out("imported=yes",400);CHECK(count(file)==3);   // a file names no receiver: the receiver refuses its own
    r=a.w.importObjects(file,parseFilter("imported=yes"),true,400);CHECK(r.own==2&&r.added==1&&a.w.objects()==3&&a.w.imported()==1);
    // The same object by two ways is one object, and the copy that came first is kept unless the other is newer.
    Server d("serverD1");d.w.importObjects(a.out("all",500),parseFilter("all"),true,500);CHECK(d.w.imported()==2);
    r=d.w.importObjects(onward,parseFilter("imported=yes"),true,500);CHECK(r.added==1&&r.unchanged==2&&d.w.imported()==3);
    // Forgetting a server takes what it built and what it handed over.
    CHECK(c.w.forget("serverA1")==2&&c.w.imported()==1);CHECK(c.w.forget("serverB1")==1);
    c.w.importObjects(onward,parseFilter("imported=yes"),true,300);CHECK(c.w.forget("serverB1")==3&&c.w.objects()==0);
}
static void strict(){
    Server a("serverA1"),b("serverB1");auto ann=a.signIn("steam-a","Ann");a.make(ann,"l",226,100);
    auto good=a.out("all",200);CHECK(count(good)==1);
    auto with=[&](const char* key,json::Value v){auto d=good;d.set(key,std::move(v));return d;};
    CHECK(refusedWhole(b.w,json::parse("[]")));CHECK(refusedWhole(b.w,json::parse("{}")));
    CHECK(refusedWhole(b.w,with("format",2)));CHECK(refusedWhole(b.w,with("kind","something")));CHECK(refusedWhole(b.w,with("server","x")));
    CHECK(refusedWhole(b.w,with("server","bad id!")));CHECK(refusedWhole(b.w,with("objects",json::Value::object())));
    CHECK(refusedWhole(b.w,with("server","serverB1")));   // its own export
    CHECK(refusedWhole(a.w,good));
    auto object=[&](const char* key,json::Value v){auto e=entry("serverA1","lAAAAAAAAAAAA");e.set(key,std::move(v));return documentOf("serverA1",{entry("serverA1","lgood00000000"),e});};
    CHECK(!refusedWhole(b.w,documentOf("serverA1",{entry("serverA1","lAAAAAAAAAAAA")})));CHECK(b.w.forget("serverA1")==1);
    // One bad object refuses the whole document: the good one beside it is not taken either.
    CHECK(refusedWhole(b.w,object("extra",1)));CHECK(refusedWhole(b.w,object("id","l/../x")));CHECK(refusedWhole(b.w,object("id","")));CHECK(refusedWhole(b.w,object("id",7)));
    CHECK(refusedWhole(b.w,object("server","no")));CHECK(refusedWhole(b.w,object("creator","a b")));CHECK(refusedWhole(b.w,object("name",std::string(MaxNameBytes+1,'n'))));
    CHECK(refusedWhole(b.w,object("name","a\nb")));CHECK(refusedWhole(b.w,object("likes",-1)));CHECK(refusedWhole(b.w,object("likes",1.5)));CHECK(refusedWhole(b.w,object("updated","now")));
    CHECK(refusedWhole(b.w,object("post",json::parse("{\"t\":\"L\",\"q\":1}"))));CHECK(refusedWhole(b.w,object("post",json::parse("{\"t\":\"ladder\"}"))));
    CHECK(refusedWhole(b.w,object("post",json::parse("{\"t\":\"l\",\"q\":\"x\"}"))));CHECK(refusedWhole(b.w,object("post",json::parse("[]"))));
    {auto e=entry("serverA1","lAAAAAAAAAAAA");e.erase("name");CHECK(refusedWhole(b.w,documentOf("serverA1",{e})));}
    CHECK(refusedWhole(b.w,documentOf("serverA1",{entry("serverA1","lsame00000000"),entry("serverA1","lsame00000000")})));
    // Too large an object, and too many from one server.
    {auto post=json::parse("{\"t\":\"l\",\"q\":1}");post.set("st",std::string(MaxSharedPostBytes,'x'));CHECK(refusedWhole(b.w,object("post",post)));}
    std::vector<json::Value> many;for(size_t i=0;i<MaxImportedPerServer+1;++i)many.push_back(entry("serverA1","l"+std::to_string(100000+i)));
    CHECK(refusedWhole(b.w,documentOf("serverA1",many)));CHECK(refusedWhole(b.w,documentOf("serverA1",many),"all",true));
    many.pop_back();CHECK(!refusedWhole(b.w,documentOf("serverA1",many))&&b.w.imported()==MaxImportedPerServer);
    CHECK(refusedWhole(b.w,documentOf("serverA1",{entry("serverA1","lonemore00000")})));   // full: not one more without prune
    CHECK(!refusedWhole(b.w,documentOf("serverA1",{entry("serverA1","lonemore00000")}),"all",true)&&b.w.imported()==1);
    // A server never sends what the other side would refuse.
    Server big("serverE1");auto t=big.signIn("k","E");big.make(t,"l",1,1,",\"st\":\""+std::string(MaxSharedPostBytes,'x')+"\"");big.make(t,"l",1,1);CHECK(count(big.out("all",2))==1);
    // The saved world keeps every origin; what is taken afterwards still finds what it has.
    Server s("serverS1");auto bea=s.signIn("steam-x","Bea");s.make(bea,"l",226,10);s.w.importObjects(good,parseFilter("all"),false,300);
    auto saved=s.w.snapshot();World r([]{return std::string("fresh");});r.restore(json::parse(saved.dump()));
    CHECK(r.serverId()=="serverS1"&&r.objects()==2&&r.imported()==1&&r.snapshot().dump()==saved.dump()&&!r.dirty());
    auto again=r.importObjects(good,parseFilter("all"),true,400);CHECK(again.added==0&&again.unchanged==1&&r.objects()==2);
    CHECK(r.forget("serverA1")==1&&r.objects()==1);
    // A world file from before linked servers gets an id, and is saved with it.
    {auto old=saved;old.erase("server_id");World o([]{return std::string("fresh");});o.restore(old);CHECK(serverIdText(o.serverId())&&o.dirty());}
    {auto wrong=saved;wrong.set("server_id","serverA1");World o([]{return std::string("fresh");});bool threw=false;try{o.restore(wrong);}catch(const std::exception&){threw=true;}CHECK(threw);}
    // Keys and addresses.
    CHECK(linkKeyText("abcdefgh")&&linkKeyText("A-b_c-123")&&!linkKeyText("")&&!linkKeyText("short")&&!linkKeyText("with space")&&!linkKeyText(std::string(65,'k')));
    CHECK(sameKey("abcdefgh","abcdefgh")&&!sameKey("abcdefgh","abcdefgX")&&!sameKey("abcdefgh","abcdefghi")&&!sameKey("","abcdefgh")&&!sameKey("abcdefgh",""));
    LinkEndpoint e;CHECK(parseLinkUrl("http://10.0.0.5:8089",e)&&e.host=="10.0.0.5"&&e.port==8089&&e.prefix.empty());
    CHECK(parseLinkUrl("http://127.0.0.1:1/s3-cret_",e)&&e.prefix=="s3-cret_"&&e.port==1);
    for(const char* wrong:{"","10.0.0.5:8089","https://10.0.0.5:8089","http://example.org:80","http://10.0.0:80","http://10.0.0.256:80","http://10.0.0.5","http://10.0.0.5:","http://10.0.0.5:0",
                           "http://10.0.0.5:70000","http://10.0.0.5:80/","http://10.0.0.5:80/a/b","http://10.0.0.5:80/a?b","http://10.0.0.5:8x","http://1.2.3.4.5:80","http://1..2.3:80"})CHECK(!parseLinkUrl(wrong,e));
}
// Server B asks server A, through nothing but the two calls a socket would carry.
struct Wire {
    Server& a;Filter give;
    Wire(Server& server,Filter f):a(server),give(std::move(f)){}
    unsigned asked=0;bool down=false;std::string garbage;std::function<void(unsigned)> between;int64_t now=1000;
    Puller::Fetch fetch(){return [this](const std::string& request,std::string& answer){
        if(down)return false;
        ++asked;if(!garbage.empty()){answer=garbage;return true;}
        auto r=a.w.linkPull(give,request,now);if(r.status!=200)return false;
        answer=r.body;if(between)between(asked);return true;};}
};
static void live(){
    Server a("serverA1"),b("serverB1");auto ann=a.signIn("steam-a","Ann"),bea=b.signIn("steam-x","Bea");
    auto ladder=a.make(ann,"l",226,100);a.make(ann,"r",226,100);a.make(ann,"e",300,100);b.make(bea,"l",226,100);
    Wire wire{a,parseFilter("types=l,r")};LinkConfig config;config.importFilter=parseFilter("areas=226");
    Puller puller(b.w,config,wire.fetch());
    // Both filters apply: A gives ladders and ropes, B takes area 226.
    CHECK(puller.poll(1000)==Puller::Result::Synced&&puller.last().added==2&&b.w.imported()==2&&puller.peer()=="serverA1"&&wire.asked==1);
    CHECK(puller.poll(1005)==Puller::Result::Same&&wire.asked==2&&b.w.imported()==2);
    // New, changed and removed objects follow.
    auto second=a.make(ann,"l",226,1010);CHECK(puller.poll(1010)==Puller::Result::Synced&&puller.last().added==1&&puller.last().unchanged==2&&b.w.imported()==3);
    CHECK(a.w.call(ann,"updateObject","{\"id\":\""+second+"\",\"ci\":{\"cp\":4}}",1011).status==200);CHECK(puller.poll(1015)==Puller::Result::Synced&&puller.last().updated==1);
    CHECK(a.w.call(ann,"deleteObject","{\"id\":\""+second+"\"}",1016).status==200);CHECK(puller.poll(1020)==Puller::Result::Synced&&puller.last().removed==1&&b.w.imported()==2);
    a.make(ann,"l",300,1021);CHECK(puller.poll(1025)==Puller::Result::Synced&&puller.last().added==0&&b.w.imported()==2);   // not an area B takes
    CHECK(b.found(bea,226)==2&&b.w.objects()==3);
    // A is away: nothing changes at B. A is back as a new process with the same world, less one object: B catches up.
    wire.down=true;CHECK(puller.poll(1030)==Puller::Result::Unreachable&&b.w.imported()==2);wire.down=false;
    CHECK(a.w.call(ann,"deleteObject","{\"id\":\""+ladder+"\"}",1031).status==200);
    Server a2("unset000");a2.w.restore(json::parse(a.w.snapshot().dump()));CHECK(a2.w.serverId()=="serverA1"&&a2.w.version()!=a.w.version());
    Wire wire2{a2,parseFilter("types=l,r")};Puller after(b.w,config,wire2.fetch());
    CHECK(after.poll(1040)==Puller::Result::Synced&&after.last().removed==1&&after.last().unchanged==1&&after.last().added==0&&b.w.imported()==1);
    // B restarts: the first round takes everything again and finds it has it.
    World b2([]{return std::string("fresh");});b2.restore(json::parse(b.w.snapshot().dump()));Puller restarted(b2,config,wire2.fetch());
    CHECK(restarted.poll(1050)==Puller::Result::Synced&&restarted.last().added==0&&restarted.last().unchanged==1&&b2.imported()==1&&b2.objects()==2);
    CHECK(restarted.poll(1055)==Puller::Result::Same);
    // An object that grows too old for the filter goes at the next full round, with nothing having happened to it.
    {Server old("serverO1");auto t=old.signIn("k","O");old.make(t,"l",1,1000);Server taker("serverT1");
     Wire w{old,parseFilter("all")};LinkConfig c;c.importFilter=parseFilter("max-age=500s");Puller p(taker.w,c,w.fetch());
     CHECK(p.poll(1100)==Puller::Result::Synced&&taker.w.imported()==1);CHECK(p.poll(1600)==Puller::Result::Same&&taker.w.imported()==1);
     CHECK(p.poll(1100+LinkFullEverySeconds)==Puller::Result::Synced&&p.last().removed==1&&taker.w.imported()==0);}
    // Pages: 1200 objects come in three answers; an answer in the wrong form, or a world that moved between two pages,
    // leaves B as it was, and the next round is whole again.
    Server big("serverG1"),taker("serverT1");auto t=big.signIn("k","G");std::vector<std::string> made;
    for(int i=0;i<1200;++i)made.push_back(big.make(t,"l",1,1000));
    std::sort(made.begin(),made.end());
    Wire pages{big,parseFilter("all")};LinkConfig c;c.importFilter=parseFilter("all");Puller p(taker.w,c,pages.fetch());
    pages.between=[&](unsigned asked){if(asked==1)big.w.call(t,"deleteObject","{\"id\":\""+made[0]+"\"}",1001);};
    CHECK(p.poll(1000)==Puller::Result::Changed&&taker.w.imported()==0);
    pages.between=nullptr;pages.asked=0;
    CHECK(p.poll(1005)==Puller::Result::Synced&&taker.w.imported()==1199&&pages.asked==3);
    for(const char* garbage:{"not json","{}","[]","{\"format\":1,\"kind\":\"coop-world-objects\",\"server\":\"serverG1\",\"version\":\"v\",\"objects\":[{}],\"more\":false,\"next\":\"\"}",
                             "{\"format\":1,\"kind\":\"coop-world-objects\",\"server\":\"serverG1\",\"version\":\"v\",\"objects\":[],\"more\":true,\"next\":\"\"}",
                             "{\"format\":1,\"kind\":\"coop-world-objects\",\"server\":\"serverG1\",\"version\":\"v\",\"objects\":[]}",
                             "{\"format\":1,\"kind\":\"coop-world-objects\",\"server\":\"serverT1\",\"version\":\"v\",\"objects\":[],\"more\":false,\"next\":\"\"}",
                             "{\"format\":1,\"kind\":\"coop-world-objects\",\"server\":\"serverG1\",\"version\":\"v\",\"same\":true}",
                             "{\"format\":2,\"kind\":\"coop-world-objects\",\"server\":\"serverG1\",\"version\":\"v\",\"objects\":[],\"more\":false,\"next\":\"\"}"}){
        Puller fresh(taker.w,c,pages.fetch());pages.garbage=garbage;CHECK(fresh.poll(1010)==Puller::Result::Refused&&taker.w.imported()==1199);}
    {json::Value page=big.w.exportObjects(parseFilter("all"),"serverT1",1010,"",LinkPageObjects+1);Puller fresh(taker.w,c,pages.fetch());pages.garbage=page.dump();
     CHECK(count(page)==LinkPageObjects+1&&fresh.poll(1010)==Puller::Result::Refused&&taker.w.imported()==1199);}
    pages.garbage.clear();
    // What a server answers to a request in the wrong form: 400, whatever the filter.
    auto ask=[&](const std::string& body){return big.w.linkPull(parseFilter("all"),body,1010).status;};
    CHECK(ask("{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\"}")==200);
    for(const char* wrong:{"","not json","[]","{}","{\"format\":2,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\"}","{\"format\":1,\"server\":\"serverT1\",\"have\":\"\"}",
                           "{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\",\"more\":1}","{\"format\":1,\"server\":\"x\",\"have\":\"\",\"after\":\"\"}",
                           "{\"format\":1,\"server\":\"serverG1\",\"have\":\"\",\"after\":\"\"}","{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"../x\"}",
                           "{\"format\":1,\"server\":\"serverT1\",\"have\":7,\"after\":\"\"}"})CHECK(ask(wrong)==400);
    CHECK(ask("{\"format\":1,\"server\":\"serverT1\",\"have\":\""+std::string(MaxLinkRequestBytes,'v')+"\",\"after\":\"\"}")==400);
    // A page is bounded in objects and in bytes.
    auto page=json::parse(big.w.linkPull(parseFilter("all"),"{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\"}",1010).body);
    CHECK(count(page)==LinkPageObjects&&page.get("more").boolean()&&page.get("next").string()==made[LinkPageObjects]);
    Server fat("serverF1");auto ft=fat.signIn("k","F");for(int i=0;i<40;++i)fat.make(ft,"l",1,1,",\"st\":\""+std::string(60000,'x')+"\"");
    auto fatPage=fat.w.linkPull(parseFilter("all"),"{\"format\":1,\"server\":\"serverT1\",\"have\":\"\",\"after\":\"\"}",1);
    CHECK(fatPage.body.size()<=LinkPageBytes+4096&&count(json::parse(fatPage.body))<40&&json::parse(fatPage.body).get("more").boolean());
    {Server fatTaker("serverT2");Wire w{fat,parseFilter("all")};Puller fp(fatTaker.w,c,w.fetch());CHECK(fp.poll(5)==Puller::Result::Synced&&fatTaker.w.imported()==40&&w.asked>=3);}
    // Two servers that take everything from each other, imports included: each has the other's objects once, for good.
    Server x("serverX1"),y("serverY1");auto tx=x.signIn("k","X"),ty=y.signIn("k2","Y");x.make(tx,"l",1,1);y.make(ty,"r",1,1);y.make(ty,"e",1,1);
    Wire toY{x,parseFilter("imported=yes")},toX{y,parseFilter("imported=yes")};LinkConfig both;both.importFilter=parseFilter("imported=yes");
    Puller py(y.w,both,toY.fetch()),px(x.w,both,toX.fetch());
    for(int round=0;round<5;++round){px.poll(10+round);py.poll(10+round);}
    CHECK(x.w.objects()==3&&x.w.imported()==2&&y.w.objects()==3&&y.w.imported()==1);
    CHECK(px.poll(20)==Puller::Result::Same&&py.poll(20)==Puller::Result::Same);
}
int main(){
    // A broken world can throw where a whole one does not: that is a failure like any other, not a crash.
    for(auto part:{filters,importAndIds,noLoops,strict,live}){
        try{part();}catch(const std::exception& e){std::fprintf(stderr,"link oracle: threw %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("link tests passed");return 0;
}
