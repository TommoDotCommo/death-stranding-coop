// Route challenges on the server (challenges.h, ../docs/ROUTES.md "On the server"), through the HTTP surface with the
// hook coop-world sets: publish, the list with its board, a run's track replayed before it counts, the best time kept,
// the policy's `challenges`, signatures and identity keys, the limits, removal by the author or the owner, a ban, and
// the world file. Every check has a MUTANT_CHALLENGE_* build that must fail here.
#include "challenges.h"
#include "front.h"
#include "api.h"
#include "../mod/route.h"
#include <cctype>
#include <cstdio>
using namespace world;
namespace id=world::identity;
namespace rt=mod::route;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"challenges oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Challenges ch;Front f;int64_t now=Now;std::vector<std::string> lines;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={},bool hook=true):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);for(auto s:settings)setPolicy(p,s);w.setPolicy(p);
        w.log=[this](const std::string& l){lines.push_back(l);};
        if(hook)f.challenges=[this](const std::string& m,const std::string& c,const std::string& b,const std::string& s,int64_t n){return challengesApi(w,ch,m,c,b,s,n);};}
    std::map<std::string,std::string> sign(const id::Keys& k,const char* method,const std::string& target,const std::string& body){
        std::map<std::string,std::string> h;
        for(auto& x:id::signRequest(k,method,target,w.serverId(),body,now,id::newNonce())){
            std::string name=x.first;for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));h[name]=x.second;}
        return h;}
    Reply api(const id::Keys* k,const char* method,const std::string& call,const std::string& body=""){
        Request r;r.method=method;r.target="/s3cret/coop/"+call;r.body=body;if(k)r.headers=sign(*k,method,r.target,body);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    json::Value list(){return json::parse(api(nullptr,"GET","challenges").body).get("challenges");}
};
static std::string q(const std::string& s){return "\""+s+"\"";}
static std::string publishBody(const std::string& player,const std::string& route,const std::string& name="Ann"){return "{\"player\":"+q(player)+",\"route\":"+q(route)+",\"name\":"+q(name)+"}";}
static std::string resultBody(const std::string& player,const std::string& challenge,const std::string& track,const std::string& name){
    return "{\"player\":"+q(player)+",\"challenge\":"+q(challenge)+",\"track\":"+q(track)+",\"name\":"+q(name)+"}";}
static rt::Route straight(double to=300,const char* name="Ridge"){rt::Route r;r.name=name;r.author="Ann";r.points={{0,0,0},{to/2,0,0},{to,0,0}};return r;}
// A run at `speed` m/s along the route, the start zone first; `jump`: one point moved 500 m on.
static std::string track(const rt::Route& r,double speed,bool jump=false,int64_t claimSkew=0){
    rt::Checker c(r);uint64_t ms=1000;
    auto at=[&](double x){rt::Sample s;s.ms=ms;s.at={x,0,0};c.feed(s);};
    for(int i=0;i<5;++i){ms+=100;at(0);}
    for(double x=0;x<=r.points.back().x;x+=speed/10){ms+=100;at(x);}
    auto t=c.track();if(jump&&t.size()>6)t[5].at.x+=500;
    return rt::encodeTrack({rt::id(rt::encode(r)),uint64_t(int64_t(c.result())+claimSkew),t},r.points.front());
}

static void publishing(){
    Server s;const std::string route=rt::encode(straight());
    // Nothing yet; a published challenge is listed with its rules and an empty board.
    CHECK(s.api(nullptr,"GET","challenges").status==200&&s.list().items().empty());
    auto r=s.api(nullptr,"POST","challenge-publish",publishBody("ann",route));
    CHECK(r.status==200&&json::parse(r.body).get("id").string()=="c1");
    auto l=s.list();CHECK(l.items().size()==1);
    if(l.items().size()==1){auto& c=l.items()[0];
        CHECK(c.get("name").string()=="Ridge"&&c.get("author").string()=="Ann"&&c.get("author_key").string()==""&&c.get("route").string()==route);
        CHECK(c.get("rules").get("width").integer()==10&&c.get("length_m").integer()==300&&c.get("runs").integer()==0&&c.get("board").items().empty());
        CHECK(c.get("rules_text").string()=="10 m wide, 5 s grace");}
    CHECK(!s.lines.empty()&&s.lines.back().find("challenge c1 published by")==0);
    // The same route again (in lower case: the same challenge), a damaged string, wrong forms: refused, nothing changes.
    std::string lower;for(char c:route)lower+=char(std::tolower(static_cast<unsigned char>(c)));
    r=s.api(nullptr,"POST","challenge-publish",publishBody("bob",lower));CHECK(r.status==403&&r.body.find("here already (c1)")!=std::string::npos);
    r=s.api(nullptr,"POST","challenge-publish",publishBody("ann",route.substr(0,route.size()-4)));CHECK(r.status==403&&r.body.find("incomplete")!=std::string::npos);
    CHECK(s.api(nullptr,"POST","challenge-publish","{\"player\":\"ann\",\"route\":"+q(route)+",\"extra\":\"x\"}").status==400);
    CHECK(s.api(nullptr,"POST","challenge-publish","{\"player\":\"ann\",\"route\":7}").status==400);
    CHECK(s.api(nullptr,"POST","challenge-publish","[]").status==400);
    CHECK(s.api(nullptr,"GET","challenge-publish").status==404&&s.api(nullptr,"POST","challenges").status==404);
    CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("bad key!",rt::encode(straight(200)))).status==400);
    CHECK(s.list().items().size()==1);
    // Without the hook the calls do not exist.
    Server none("friends",{},false);CHECK(none.api(nullptr,"GET","challenges").status==404);
}
static void results(){
    Server s;const auto route=straight();const std::string text=rt::encode(route);
    CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("ann",text)).status==200);
    // A run replayed and counted; a slower one by the same player leaves his best; a faster one by another is first.
    auto r=s.api(nullptr,"POST","challenge-result",resultBody("ann","c1",track(route,5),"Ann"));
    CHECK(r.status==200&&json::parse(r.body).get("place").integer()==1);
    r=s.api(nullptr,"POST","challenge-result",resultBody("ann","c1",track(route,4),"Ann"));
    CHECK(r.status==200&&json::parse(r.body).get("place").integer()==1);
    auto board=s.list().items()[0].get("board").items();
    CHECK(board.size()==1&&board[0].get("ms").integer()>=55000&&board[0].get("ms").integer()<=56500&&board[0].get("time").string().compare(0,3,"0:5")==0);
    r=s.api(nullptr,"POST","challenge-result",resultBody("bob","c1",track(route,7),"Bob"));
    CHECK(r.status==200&&json::parse(r.body).get("place").integer()==1);
    board=s.list().items()[0].get("board").items();
    CHECK(board.size()==2&&board[0].get("name").string()=="Bob"&&board[1].get("name").string()=="Ann"&&board[1].get("place").integer()==2);
    CHECK(s.list().items()[0].get("runs").integer()==3);
    // A teleport, an edited time, another challenge's track, an unknown challenge: refused with the reason.
    r=s.api(nullptr,"POST","challenge-result",resultBody("cee","c1",track(route,6,true),"Cee"));CHECK(r.status==403&&r.body.find("teleported")!=std::string::npos);
    r=s.api(nullptr,"POST","challenge-result",resultBody("cee","c1",track(route,6,false,-20000),"Cee"));CHECK(r.status==403&&r.body.find("not the track's")!=std::string::npos);
    r=s.api(nullptr,"POST","challenge-result",resultBody("cee","c1",track(straight(280),6),"Cee"));CHECK(r.status==403&&r.body.find("another challenge")!=std::string::npos);
    r=s.api(nullptr,"POST","challenge-result",resultBody("cee","c9",track(route,6),"Cee"));CHECK(r.status==403&&r.body.find("no challenge c9")!=std::string::npos);
    r=s.api(nullptr,"POST","challenge-result",resultBody("cee","c1","DST1-0000","Cee"));CHECK(r.status==403);
    CHECK(s.list().items()[0].get("board").items().size()==2);
    // A result body may hold a long track; a body beyond 16 KB is refused.
    CHECK(s.api(nullptr,"POST","challenge-result",resultBody("cee","c1",std::string(17000,'A'),"Cee")).status==400);
    // Names on the board: printable and short.
    CHECK(displayName("Ann")=="Ann"&&displayName(std::string(40,'a')).size()==24&&displayName("a\x01").empty());
}
static void policy(){
    const std::string route=rt::encode(straight());
    {Server s("friends",{"challenges=nobody"});auto r=s.api(nullptr,"POST","challenge-publish",publishBody("ann",route));CHECK(r.status==403&&r.body.find("no route challenges")==std::string::npos&&r.body.find("takes no challenges")!=std::string::npos);}
    {   // owner: only the owner's signed call.
        Server s("friends",{"challenges=owner"});auto o=id::makeKeys(),a=id::makeKeys();s.w.setOwner(id::account(o.pub));
        CHECK(s.api(&a,"POST","challenge-publish",publishBody(id::account(a.pub),route)).status==403);
        CHECK(s.api(&o,"POST","challenge-publish",publishBody(id::account(o.pub),route)).status==200);}
    {   // members (the public preset): a player who is no member is refused until the owner makes him one.
        Server s("public");auto a=id::makeKeys();const std::string key=id::account(a.pub);
        CHECK(s.w.policy().challenges=="members");
        CHECK(s.api(&a,"POST","ticket","{\"player\":"+q(key)+"}").status==200);
        auto r=s.api(&a,"POST","challenge-publish",publishBody(key,route));CHECK(r.status==403&&r.body.find("only members")!=std::string::npos);
        s.w.setPlayer(playerId(key),"member=yes");
        CHECK(s.api(&a,"POST","challenge-publish",publishBody(key,route)).status==200);
        // Under identity keys: unsigned is refused, a call signed by one acts for him only; the board shows his key short.
        auto b=id::makeKeys();
        CHECK(s.api(nullptr,"POST","challenge-result",resultBody(key,"c1",track(straight(),5),"Ann")).status==401);
        CHECK(s.api(&b,"POST","challenge-result",resultBody(key,"c1",track(straight(),5),"Ann")).status==403);
        CHECK(s.api(&a,"POST","challenge-result",resultBody(key,"c1",track(straight(),5),"Ann")).status==200);
        auto c=s.list().items()[0];CHECK(c.get("author_key").string()==id::shortText(a.pub)&&c.get("board").items()[0].get("key").string()==id::shortText(a.pub));
        // A banned player: refused.
        s.w.ban(playerId(key),true);CHECK(s.api(&a,"POST","challenge-result",resultBody(key,"c1",track(straight(),6),"Ann")).status==403);}
    {   // identity none: an account of the key form is acted for only with its key.
        Server s;auto a=id::makeKeys();CHECK(s.api(nullptr,"POST","challenge-publish",publishBody(id::account(a.pub),route)).status==403);}
}
static void limits(){
    Server s;
    for(int i=0;i<int(MaxChallengesPerPlayer);++i)CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("ann",rt::encode(straight(100+i)))).status==200);
    auto r=s.api(nullptr,"POST","challenge-publish",publishBody("ann",rt::encode(straight(500))));CHECK(r.status==403&&r.body.find("remove one first")!=std::string::npos);
    CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("bob",rt::encode(straight(500)))).status==200);
    CHECK(s.list().items().size()==MaxChallengesPerPlayer+1);
    // Removal: by its author, not by another player; the owner's signed call removes any.
    CHECK(s.api(nullptr,"POST","challenge-remove","{\"player\":\"bob\",\"challenge\":\"c1\"}").status==403);
    CHECK(s.api(nullptr,"POST","challenge-remove","{\"player\":\"ann\",\"challenge\":\"c1\"}").status==200);
    CHECK(s.api(nullptr,"POST","challenge-remove","{\"player\":\"ann\",\"challenge\":\"c1\"}").status==404);
    auto o=id::makeKeys();s.w.setOwner(id::account(o.pub));
    CHECK(s.api(&o,"POST","challenge-remove","{\"challenge\":\"c2\"}").status==200);
    CHECK(s.api(nullptr,"POST","challenge-remove","{\"challenge\":\"c3\"}").status==400);
    CHECK(s.list().items().size()==MaxChallengesPerPlayer-1);
    // Now she has room for one more.
    CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("ann",rt::encode(straight(600)))).status==200);
}
static void saving(){
    Server s;const auto route=straight();
    CHECK(s.api(nullptr,"POST","challenge-publish",publishBody("ann",rt::encode(route))).status==200);
    CHECK(s.api(nullptr,"POST","challenge-result",resultBody("bob","c1",track(route,6),"Bob")).status==200);
    CHECK(s.ch.dirty());
    auto snap=s.ch.snapshot();s.ch.saved();CHECK(!s.ch.dirty());
    Challenges back;back.restore(json::parse(snap.dump()));
    CHECK(back.list().dump()==s.ch.list().dump());
    // A new challenge after a restore gets a new id.
    CHECK(back.publish("cee","Cee",rt::encode(straight(222)),Now).id=="c2");
    bool threw=false;try{Challenges x;x.restore(json::parse("{\"list\":[{\"id\":\"c1\",\"route\":\"DSR1-AAAAA\",\"account\":\"ann\"}]}"));}catch(const std::exception&){threw=true;}CHECK(threw);
    threw=false;try{Challenges x;x.restore(json::parse("{\"list\":[{\"id\":\"zz\",\"route\":"+q(rt::encode(route))+",\"account\":\"ann\"}]}"));}catch(const std::exception&){threw=true;}CHECK(threw);
}
int main(){
    publishing();results();policy();limits();saving();
    if(failures){std::fprintf(stderr,"challenges: %u checks failed\n",failures);return 1;}
    std::puts("challenges: publish, results replayed, the board, policy, keys, limits, removal and the world file hold");
    return 0;
}
