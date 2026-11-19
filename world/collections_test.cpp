// Collections on the server (../docs/COLLECTIONS.md) through the HTTP surface: a player makes one from his own objects
// (named, or near his spot, facing his way) and never from another's; another imports it at the original place or
// here (moved to his spot, turned by his heading, in the area of the nearest object), as his own uploads under the
// policy (who may import, the limit, the spread, the hard cap); the games, his own included, are told another builder
// (his collection builder, "Name · collection") and handed them as another player's structures, while they stay his
// (limit, withdrawal, likes, `mine`); bad bodies and strings change nothing; signed under identity keys; the world file keeps the
// source; the relay's report becomes a spot in the game's axes.
#include "front.h"
#include "api.h"
#include "collection.h"
#include "idtag.h"
#include <cctype>
#include <cmath>
#include <cstdio>
using namespace world;
namespace id=world::identity;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"collections oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={},bool keys=false):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);if(!keys)p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    Reply post(const std::string& call,const std::string& body,const id::Keys* k=nullptr){
        std::string target="/s3cret/coop/"+call;std::map<std::string,std::string> h;
        if(k)for(auto& x:id::signRequest(*k,"POST",target,w.serverId(),body,now,id::newNonce())){std::string n=x.first;for(auto& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));h[n]=x.second;}
        return raw("POST",target,body,h);}
    json::Value doc(const Reply& r){CHECK(r.status==200);if(r.status!=200)std::fprintf(stderr,"  answer %d %s\n",r.status,r.body.c_str());return r.status==200?json::parse(r.body):json::Value();}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    // An upload as the game sends it: positions in metres x 10^5 with exp 5, angles in degrees x 10^5.
    std::string build(const std::string& token,char type,double x,double y,double z,double rzDegrees=0,int64_t area=226,const std::string& extra=""){
        char b[512];std::snprintf(b,sizeof b,"{\"t\":\"%c\",\"x\":9,\"y\":7,\"m\":22123,\"px\":%lld,\"py\":%lld,\"pz\":%lld,\"rx\":0,\"ry\":0,\"rz\":%lld,\"exp\":5,\"q\":%lld,\"st\":\"0\",\"p\":0,\"lp\":0%s}",
            type,(long long)std::llround(x*1e5),(long long)std::llround(y*1e5),(long long)std::llround(z*1e5),(long long)std::llround(rzDegrees*1e5),(long long)area,extra.c_str());
        auto r=w.call(token,"createObject",b,now);CHECK(r.status==200);return json::parse(r.body).get("id").string();}
    std::vector<json::Value> handed(const std::string& token,int64_t area=226){return json::parse(w.call(token,"findQpidObjects","{\"q\":"+std::to_string(area)+"}",now).body).get("n").get("oa").items();}
    json::Value object(const std::string& id){auto all=w.snapshot();for(auto& o:all.get("objects").items())if(o.get("id").string()==id)return o;return {};}
    void spot(uint64_t relay,double x,double y,double z,bool haveYaw=false,double yaw=0){Spot s;s.x=x;s.y=y;s.z=z;s.haveYaw=haveYaw;s.yaw=yaw;s.at=now;w.noteSpot(relay,s);}
};
static double metres(const json::Value& post,const char* k){return double(post.get(k).integer())/std::pow(10.0,double(post.get("exp").integer()));}
static bool has(const std::vector<json::Value>& list,const std::string& id){for(auto& o:list)if(o.get("id").string()==id)return true;return false;}
// Made from named objects: his own only; imported at the original place by another, who is handed them, as is a third.
static void original(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),cy=s.signIn("cy","Cy");
    auto ladder=s.build(ann,'l',1000,2000,30,45),cairn=s.build(ann,'e',1040,2000,30,0,226,",\"si\":{\"r\":1}"),bobs=s.build(bob,'l',1100,2000,30);
    auto bridge=s.build(ann,'b',1080,2010,31,90,227,",\"bi\":{\"a\":-111525},\"mt\":[{\"c\":\"\",\"t\":0,\"mat\":[0,0,1000,0,0,0],\"rmat\":[0,0,0,0,0,0]}]");
    auto r=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+ladder+"\",\""+cairn+"\",\""+bridge+"\",\""+bobs+"\",\"lNOPE00000000\"],\"name\":\"Ann's camp\"}"));
    CHECK(r.get("count").integer()==3&&r.get("left_out").items().size()==2);
    const std::string text=r.get("collection").string();
    collection::Collection c;std::string why;CHECK(collection::parse(text,c,why)&&c.items.size()==3&&c.name=="Ann's camp"&&!c.headingKnown);
    CHECK(c.area==226&&std::llabs(c.x-104000)<=1&&c.items[2].ownArea&&c.items[2].area==227&&c.items[2].materials&&c.items[2].material[2]==1000&&c.items[2].blocks.size()==1);
    for(auto& l:r.get("left_out").items())CHECK(l.get("why").string()=="not yours"||l.get("why").string()=="no such object");
    // On the same server the originals stand there: the game would fail a copy on top of them, so nothing is made.
    auto same=s.doc(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}"));
    CHECK(same.get("imported").integer()==0&&same.get("left_out").items().size()==3);
    // On another server Bob imports it where it was: three objects of his, from a collection, at the same places.
    Server t;bob=t.signIn("bob","Bob");cy=t.signIn("cy","Cy");
    const std::string bobsOwn=t.build(bob,'l',3000,2000,30);
    auto before=t.w.objects();
    auto in=t.doc(t.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}"));
    CHECK(in.get("imported").integer()==3&&in.get("shared").integer()==3&&t.w.objects()==before+3&&in.get("source").string()=="Ann's camp"&&in.get("left_out").items().empty());
    std::vector<std::string> made;for(auto& i:in.get("ids").items())made.push_back(i.string());
    for(auto& i:made)CHECK(idtag::carries(t.w.serverId(),i));   // this server's, as a built object is
    CHECK(made.size()==3);
    if(made.size()==3){
        auto o=t.object(made[0]);auto p=o.get("post");
        CHECK(o.get("creator").string()==playerId("bob")&&o.get("collection").string()=="Ann's camp"&&p.get("t").string()=="l"&&p.get("exp").integer()==5);
        CHECK(std::abs(metres(p,"px")-1000)<0.02&&std::abs(metres(p,"py")-2000)<0.02&&std::abs(metres(p,"pz")-30)<0.02&&p.get("rz").integer()==4500000&&p.get("q").integer()==226&&p.get("m").integer()==22123);
        auto b=t.object(made[2]).get("post");CHECK(b.get("q").integer()==227&&b.get("bi").get("a").integer()==-111525&&b.get("mt").items().size()==1);
        CHECK(t.object(made[1]).get("post").get("si").get("r").integer()==1);
        // Every game, his own too, is told another builder: his collection builder, not his player id (his game sets an
        // object of its own player that its save lacks to Fail). So his game is handed them as another player's, as
        // Cy's is, and never his own build.
        auto toBob=t.handed(bob),toCy=t.handed(cy);
        CHECK(has(toBob,made[0])&&has(toBob,made[1])&&!has(toBob,bobsOwn)&&has(toCy,made[0])&&has(toCy,made[1])&&has(toCy,bobsOwn));
        CHECK(has(t.handed(bob,227),made[2]));
        const std::string builder=collectionBuilderId(playerId("bob"));
        CHECK(builder!=playerId("bob")&&builder.size()==playerId("bob").size()&&builder.rfind("zygo_",0)==0&&collectionBuilderId(playerId("cy"))!=builder);
        for(auto& o2:toBob)if(o2.get("id").string()!=bobsOwn)CHECK(o2.get("c").string()==builder);
        for(auto& o2:toCy){if(o2.get("id").string()==bobsOwn)CHECK(o2.get("c").string()==playerId("bob"));else CHECK(o2.get("c").string()==builder);}
        // lookup names the collection builder "Bob · collection", with no account; Bob himself by his name only.
        auto look=json::parse(t.w.call(cy,"lookup","{\"ids\":[\""+builder+"\",\""+playerId("bob")+"\",\"zygo_nobody\"]}",t.now).body).get("users").items();
        CHECK(look.size()==2);
        for(auto& u:look){
            if(u.get("account").string()==builder)CHECK(u.get("displayName").string()=="Bob \xC2\xB7 collection"&&u.get("boundAccount").string().empty());
            else CHECK(u.get("account").string()==playerId("bob")&&u.get("displayName").string()=="Bob");}
        // Likes on them are Bob's: his like history has Cy's like.
        CHECK(t.w.call(cy,"sendLike","{\"oid\":\""+made[1]+"\",\"lp\":3,\"lp2\":0,\"lt\":\"m\"}",t.now).status==200);
        auto likes=json::parse(t.w.call(bob,"getLikeHistory","{\"since\":0}",t.now).body).get("like_histories").items();
        CHECK(likes.size()==1&&likes.size()&&likes[0].get("oid").string()==made[1]&&likes[0].get("uid").string()==playerId("cy"));
        // His view lists them with their source; he withdraws them as his own.
        auto mine=t.doc(t.raw("GET","/s3cret/coop/mine?player=bob"));size_t sourced=0;
        for(auto& o2:mine.get("objects").items())sourced+=o2.get("collection").string()=="Ann's camp";
        CHECK(sourced==3);
        auto w=t.doc(t.post("withdraw","{\"player\":\"bob\",\"object\":\""+made[0]+"\"}"));CHECK(w.get("removed").integer()==1&&!has(t.handed(cy),made[0]));}
    // The world file keeps the source.
    World again;again.restore(t.w.snapshot());
    size_t sourced=0;auto kept=again.snapshot();for(auto& o:kept.get("objects").items())sourced+=o.get("collection").string()=="Ann's camp";
    CHECK(sourced==2);
}
// Near his spot, facing his way; imported here by another, moved to his spot and turned by his heading.
static void here(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");
    // Ann at (500, 600, 10), facing +y. A ladder 10 m ahead of her, a cairn 30 m to her right (+x), one 300 m away.
    auto ahead=s.build(ann,'l',500,610,10,0),right=s.build(ann,'e',530,600,10,0),far=s.build(ann,'e',800,600,10,0);(void)far;
    CHECK(s.post("collection","{\"player\":\"ann\",\"near\":50,\"relay_id\":\"11\"}").status==409);   // no spot yet
    s.spot(11,500,600,10,true,0);
    auto r=s.doc(s.post("collection","{\"player\":\"ann\",\"near\":50,\"relay_id\":\"11\"}"));
    CHECK(r.get("count").integer()==2&&r.get("anchor").has("heading"));
    const std::string text=r.get("collection").string();collection::Collection c;std::string why;CHECK(collection::parse(text,c,why)&&c.headingKnown&&c.heading==0);
    // A spot that is too old is no spot.
    s.now+=SpotFreshSeconds+1;CHECK(s.post("collection","{\"player\":\"ann\",\"near\":50,\"relay_id\":\"11\"}").status==409);s.now-=SpotFreshSeconds+1;
    // Bob far away, facing +x (90 degrees), near one of the server's objects in area 300 (the area the game asks there).
    auto marker=s.build(bob,'p',2000,3000,50,0,300);(void)marker;
    CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"here\",\"relay_id\":\"22\"}").status==409);   // no spot
    s.spot(22,2000,3100,50,true,3.14159265358979323846/2);
    auto in=s.doc(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"here\",\"relay_id\":\"22\"}"));
    CHECK(in.get("imported").integer()==2&&in.get("anchor").get("turned").integer()==90);
    std::vector<std::string> made;for(auto& i:in.get("ids").items())made.push_back(i.string());
    // What was 10 m ahead of Ann is 10 m ahead of Bob (+x); what was 30 m to her right is 30 m to his (-y). In area 300.
    bool sawLadder=false,sawCairn=false;
    for(auto& id:made){
        auto p=s.object(id).get("post");const double x=metres(p,"px"),y=metres(p,"py");
        CHECK(p.get("q").integer()==300);
        if(p.get("t").string()=="l"){sawLadder=true;CHECK(std::abs(x-2010)<0.05&&std::abs(y-3100)<0.05&&p.get("rz").integer()==-9000000);}
        if(p.get("t").string()=="e"){sawCairn=true;CHECK(std::abs(x-2000)<0.05&&std::abs(y-3070)<0.05);}}
    CHECK(sawLadder&&sawCairn);
    CHECK(has(s.handed(bob,300),made.size()?made[0]:"")&&has(s.handed(ann,300),made.size()?made[0]:""));
    // Nowhere near anything the server knows: the area there is unknown, and nothing is made.
    s.spot(22,9000,9000,0,true,0);auto count=s.w.objects();
    auto refusedHere=s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"here\",\"relay_id\":\"22\"}");
    CHECK(refusedHere.status==409&&refusedHere.body.find("original place")!=std::string::npos&&s.w.objects()==count);
    // The relay's report (x, up, y) becomes a spot in the game's axes (x, y on the ground, z up).
    Spot sp=relaySpot({12.5,3.0,-40.0},true,0.25,Now);CHECK(sp.x==12.5&&sp.y==-40.0&&sp.z==3.0&&sp.haveYaw&&sp.yaw==0.25&&sp.at==Now);
}
// The policy: who may import, the limit, the spread, the hard cap; refusals change nothing.
static void policy(){
    {Server s("friends",{"import=nobody","spread=0"});auto ann=s.signIn("ann","Ann");s.signIn("bob","Bob");auto l=s.build(ann,'l',10,10,0);
     auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+l+"\"]}")).get("collection").string();auto n=s.w.objects();
     auto r=s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}");CHECK(r.status==403&&s.w.objects()==n);
     Policy p=s.w.policy();setPolicy(p,"import=members");s.w.setPolicy(p);
     CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}").status==403&&s.w.objects()==n);
     s.w.setPlayer(playerId("bob"),"member=yes");
     CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}").status==200&&s.w.objects()==n+1);
     setPolicy(p,"import=owner");s.w.setPolicy(p);
     CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}").status==403);}
    // The presets: friends and solo import=everyone, public import=members; import_cost free everywhere.
    CHECK(presetPolicy("friends").importWho=="everyone"&&presetPolicy("solo").importWho=="everyone"&&presetPolicy("public").importWho=="members"&&presetPolicy("public").importCost=="free");
    {Policy p;bool threw=false;try{setPolicy(p,"import=maybe");}catch(const std::exception&){threw=true;}CHECK(threw&&p.importWho=="everyone");}
    // The limit holds what is past it; import_cost materials records what each carried.
    {Server s("friends",{"limit=1","import_cost=materials","spread=0"});auto ann=s.signIn("ann","Ann");s.signIn("bob","Bob");
     auto a=s.build(ann,'l',10,10,0,0,226,",\"mt\":[{\"c\":\"\",\"t\":0,\"mat\":[5,0,0,0,0,0],\"rmat\":[0,0,0,0,0,0]}]"),b=s.build(ann,'l',100,10,0);
     auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+a+"\",\""+b+"\"]}")).get("collection").string();
     auto in=s.doc(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}"));
     CHECK(in.get("imported").integer()==2&&in.get("shared").integer()==1&&in.get("held").integer()==1&&in.get("materials").items()[0].integer()==5&&in.get("cost").string()=="materials");
     size_t costed=0;for(auto& i:in.get("ids").items())costed+=s.object(i.string()).get("cost").items().size()==6;CHECK(costed==2);
     // The limit is his: an upload of his own is held now too. The held import is handed to his game (it is his, and
     // only the server has it), not to Ann's.
     auto bob=s.signIn("bob","Bob");auto own=s.build(bob,'r',500,10,0);CHECK(s.object(own).get("held").string()=="limit");
     std::string held,shared;for(auto& i:in.get("ids").items())(s.object(i.string()).get("held").string()=="limit"?held:shared)=i.string();
     CHECK(!held.empty()&&!shared.empty()&&has(s.handed(bob),held)&&has(s.handed(bob),shared)&&!has(s.handed(ann),held)&&has(s.handed(ann),shared)&&!has(s.handed(bob),own));}
    // Kept by his choice (offer_builds no): his game is handed them, nobody else's.
    {Server s("friends",{"offer_builds=no","spread=0"});auto ann=s.signIn("ann","Ann");
     auto a=s.build(ann,'l',10,10,0);
     auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+a+"\"]}")).get("collection").string();
     Server t("friends",{"offer_builds=no","spread=0"});auto bob=t.signIn("bob","Bob"),cy=t.signIn("cy","Cy");
     auto in=t.doc(t.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}"));
     CHECK(in.get("kept").integer()==1&&in.get("ids").items().size()==1);
     if(in.get("ids").items().size()==1){const std::string k=in.get("ids").items()[0].string();CHECK(has(t.handed(bob),k)&&!has(t.handed(cy),k));}}
    // The spread: of two cairns 8 m apart one is left out, and one 5 m from a cairn already handed to everyone.
    {Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");
     auto a=s.build(ann,'e',10,10,0),b=s.build(ann,'e',18,10,0),c=s.build(ann,'e',200,10,0);
     auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+a+"\",\""+b+"\",\""+c+"\"]}")).get("collection").string();
     Server t;bob=t.signIn("bob","Bob");t.build(t.signIn("cy","Cy"),'e',205,10,0);
     auto in=t.doc(t.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}"));
     CHECK(in.get("imported").integer()==1&&in.get("left_out").items().size()==2);
     for(auto& l:in.get("left_out").items()){CHECK(l.get("why").string()=="too-close");}
     (void)bob;}
    // The hard cap: the whole import, or nothing.
    {Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");
     auto a=s.build(ann,'l',10,10,0),b=s.build(ann,'l',100,10,0);
     auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+a+"\",\""+b+"\"]}")).get("collection").string();
     for(size_t k=0;k+1<MaxObjectsPerPlayer;++k){auto r=s.w.call(bob,"createObject","{\"t\":\"r\",\"q\":1}",s.now);if(r.status!=200){CHECK(false);break;}}
     auto n=s.w.objects();auto r=s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"original\"}");
     CHECK(r.status==403&&s.w.objects()==n);}
}
// Bodies and strings that change nothing; another's objects; signed calls under identity keys.
static void refusals(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob");auto l=s.build(ann,'l',10,10,0),theirs=s.build(bob,'l',300,10,0);
    auto text=s.doc(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+l+"\"]}")).get("collection").string();auto n=s.w.objects();
    CHECK(s.raw("GET","/s3cret/coop/collection?player=ann").status==404&&s.raw("GET","/s3cret/coop/import?player=ann").status==404);
    CHECK(s.post("collection","{\"player\":\"ann\"}").status==400&&s.post("collection","{\"player\":\"ann\",\"objects\":[]}").status==400);
    CHECK(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+l+"\"],\"near\":5}").status==400&&s.post("collection","{\"player\":\"ann\",\"near\":5000,\"relay_id\":\"1\"}").status==400);
    CHECK(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+l+"\"],\"color\":\"red\"}").status==400&&s.post("collection","{\"player\":\"ann\",\"near\":\"5\"}").status==400);
    CHECK(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+l+"\"],\"name\":\" x\"}").status==400);
    // Only another's objects: nothing to make.
    CHECK(s.post("collection","{\"player\":\"ann\",\"objects\":[\""+theirs+"\"]}").status==403);
    std::string cut=text.substr(0,text.size()-3),changed=text;changed[8]=changed[8]=='A'?'B':'A';
    for(auto& bad:{cut,changed,std::string("DSK2-AAAAA"),std::string("hello")}){auto r=s.post("import","{\"player\":\"bob\",\"collection\":\""+bad+"\",\"place\":\"original\"}");CHECK(r.status==400);}
    CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"there\"}").status==400&&s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\"}").status==400);
    CHECK(s.post("import","{\"player\":\"bob\",\"collection\":\""+text+"\",\"place\":\"here\",\"relay_id\":\"x1\"}").status==400&&s.w.objects()==n);
    // Identity keys: unsigned calls are refused, a signed call acts for its signer and takes his own relay id.
    Server k("public",{"import=everyone"},true);
    auto annKeys=id::keysFromSeed(id::Seed{{1}}),bobKeys=id::keysFromSeed(id::Seed{{2}});
    const std::string annAccount=id::account(annKeys.pub),bobAccount=id::account(bobKeys.pub);
    CHECK(k.post("collection","{\"player\":\""+annAccount+"\",\"near\":10}").status==401);
    CHECK(k.post("import","{\"player\":\""+bobAccount+"\",\"collection\":\""+text+"\",\"place\":\"original\"}").status==401);
    CHECK(k.post("import","{\"player\":\""+bobAccount+"\",\"collection\":\""+text+"\",\"place\":\"original\"}",&annKeys).status==403);
    auto made=k.post("import","{\"player\":\""+bobAccount+"\",\"collection\":\""+text+"\",\"place\":\"original\"}",&bobKeys);CHECK(made.status==200);
    k.spot(id::relayId(bobKeys.pub),10,10,0);
    // His spot is the one of his key's relay id: one he names is not taken.
    auto near=k.post("collection","{\"player\":\""+bobAccount+"\",\"near\":10,\"relay_id\":\"5\"}",&bobKeys);
    CHECK(near.status==200&&json::parse(near.body).get("count").integer()==1);
    k.w.noteSpot(id::relayId(bobKeys.pub),Spot{});
    auto none=k.post("collection","{\"player\":\""+bobAccount+"\",\"near\":10}",&bobKeys);CHECK(none.status==409);
}
// A long name is cut to fit the game's label (24 characters, 32 bytes); " · collection" is never cut.
static void longName(){
    Server s;auto ann=s.signIn("ann","Ann");s.signIn("bob","Bartholomew Longname-Smythe");
    auto look=json::parse(s.w.call(ann,"lookup","{\"ids\":[\""+collectionBuilderId(playerId("bob"))+"\"]}",s.now).body).get("users").items();
    CHECK(look.size()==1);
    if(look.size()==1){const std::string label=look[0].get("displayName").string(),tail=" \xC2\xB7 collection";
        CHECK(label=="Bartholomew"+tail);   // 24 characters: 11 of the name, " · ", 10 of "collection"
        CHECK(label.size()<=MaxLabelBytes&&label.size()>tail.size()&&label.compare(label.size()-tail.size(),tail.size(),tail)==0);}
}
int main(){
    original();here();policy();refusals();longName();
    if(failures){std::fprintf(stderr,"collections oracle: %u failures\n",failures);return 1;}
    std::puts("collections tests passed");return 0;
}
