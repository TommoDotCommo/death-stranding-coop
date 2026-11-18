// Players on the map (world.h mapRead) through the HTTP surface: only with a player's notice token; nobody while the policy
// says off, and off in the solo and public presets; only players placed in the last seconds, with name, place and facing;
// under identity keys never the player himself, and his choice of who he is shown holds; a bounded list.
#include "front.h"
#include "api.h"
#include "identity.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace world;namespace id=world::identity;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"map oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
struct Server {
    World w;Front f;int64_t now=1800000000;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    Reply signedPost(const id::Keys& k,const std::string& call,const std::string& body){
        const std::string target="/s3cret/coop/"+call;std::map<std::string,std::string> h;
        for(auto& x:id::signRequest(k,"POST",target,w.serverId(),body,now,id::newNonce())){std::string n=x.first;for(auto& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));h[n]=x.second;}
        return raw("POST",target,body,h);}
    std::string token(const std::string& key,const std::string& name){json::parse(w.signIn(key,name,"g").body);return json::parse(raw("POST","/s3cret/coop/notice-token","{\"player\":\""+key+"\"}").body).get("token").string();}
    std::string keyToken(const id::Keys& k){auto r=signedPost(k,"notice-token","{\"player\":\""+id::account(k.pub)+"\"}");CHECK(r.status==200);return r.status==200?json::parse(r.body).get("token").string():"";}
    void spot(uint64_t relay,const char* name,double x=1,bool yaw=false,uint32_t area=0){Spot s;s.x=x;s.y=2;s.z=3;s.haveYaw=yaw;s.yaw=.5;s.at=now;s.name=name;s.area=area;w.noteSpot(relay,s);}
    json::Value read(const std::string& t){auto r=raw("GET","/s3cret/coop/map","",{{"x-coop-token",t}});CHECK(r.status==200);return json::parse(r.status==200?r.body:"{\"format\":0,\"on\":false,\"players\":[]}");}
    std::string names(const std::string& t){std::string out;for(auto& p:read(t).get("players").items())out+=p.get("name").string()+",";return out;}
};
int main(){
    {   // Every player placed just now, with what the map needs; the id as text, since a key's relay id does not fit a double.
        Server s;auto ann=s.token("ann","Ann");s.spot(7,"Bob",12.5,true,200);s.spot(uint64_t(1)<<60,"Cy");
        auto d=s.read(ann);CHECK(d.get("format").integer()==1&&d.get("on").boolean());auto& l=d.get("players").items();CHECK(l.size()==2);
        if(l.size()==2){CHECK(l[0].get("id").string()=="7"&&l[0].get("name").string()=="Bob"&&l[0].get("x").real()==12.5&&l[0].get("y").real()==2&&l[0].get("z").real()==3&&l[0].get("yaw").real()==.5&&l[0].get("area").integer()==200);
            CHECK(l[1].get("id").string()=="1152921504606846976"&&!l[1].has("yaw")&&!l[1].has("area"));}
        // Gone quiet: off the map after MapFreshSeconds, not before; back when placed again.
        s.now+=MapFreshSeconds;CHECK(s.names(ann)=="Bob,Cy,");s.now+=1;CHECK(s.names(ann).empty());s.spot(7,"Bob");CHECK(s.names(ann)=="Bob,");}
    {   // Only a good token, only GET, only under the prefix.
        Server s;auto ann=s.token("ann","Ann");s.spot(7,"Bob");
        CHECK(s.raw("GET","/s3cret/coop/map").status==403&&s.raw("GET","/s3cret/coop/map","",{{"x-coop-token","nope"}}).status==403);
        CHECK(s.raw("GET","/coop/map","",{{"x-coop-token",ann}}).status==404&&s.raw("POST","/s3cret/coop/map","",{{"x-coop-token",ann}}).status==404);
        s.now+=NoticeTokenUnusedSeconds+1;s.spot(7,"Bob");CHECK(s.raw("GET","/s3cret/coop/map","",{{"x-coop-token",ann}}).status==403);}
    {   // Off: said so, and nobody listed. The presets; the owner can change it on a running server.
        Server s("friends",{"map=off"});auto ann=s.token("ann","Ann");s.spot(7,"Bob");auto d=s.read(ann);CHECK(!d.get("on").boolean()&&d.get("players").items().empty());
        Policy p=s.w.policy();setPolicy(p,"map=on");s.w.setPolicy(p);CHECK(s.read(ann).get("on").boolean()&&s.names(ann)=="Bob,");
        CHECK(presetPolicy("friends").map=="on"&&presetPolicy("solo").map=="off"&&presetPolicy("public").map=="off");
        bool refused=false;try{setPolicy(p,"map=maybe");}catch(const std::exception&){refused=true;}CHECK(refused);
        World back([]{return std::string("x");});Policy q=presetPolicy("public");setPolicy(q,"map=on");s.w.setPolicy(q);back.restore(s.w.snapshot());CHECK(back.policy().map=="on");}
    {   // A crowd: the nearest to the game that says where it is, of its area, within the bound; it is told to ask less often.
        Server s;auto ann=s.token("ann","Ann");for(uint64_t i=1;i<=MapPlayersMax+5;++i)s.spot(i,"P",double(i)*10,false,200);s.spot(5000,"Far",1,false,100);
        auto near=[&](const std::string& q){auto r=s.raw("GET","/s3cret/coop/map"+q,"",{{"x-coop-token",ann}});CHECK(r.status==200);return json::parse(r.body);};
        CHECK(s.read(ann).get("players").items().size()==MapPlayersMax);
        auto d=near("?x=1000&y=2&area=200");auto& l=d.get("players").items();CHECK(l.size()==MapPlayersMax&&l[0].get("x").real()==1000&&l[1].get("x").real()!=1000);
        double most=0;for(auto& p:l){most=std::max(most,std::fabs(p.get("x").real()-1000));CHECK(p.get("area").integer()==200);}CHECK(most==940);
        CHECK(near("?x=0&y=0&area=100").get("players").items().size()==1);
        CHECK(near("?x=nan&y=0").get("players").items().size()==MapPlayersMax&&near("?x=1e999&y=0").get("players").items().size()==MapPlayersMax);
        CHECK(d.get("wait_ms").integer()==std::min<int64_t>(MapWaitMaxMs,int64_t(MapPlayersMax+6)*20));
        Server few;auto bob=few.token("bob","Bob");few.spot(1,"A");CHECK(few.read(bob).get("wait_ms").integer()==MapWaitMinMs);
        Server many;auto cy=many.token("cy","Cy");for(uint64_t i=1;i<=1000;++i)many.spot(i,"P");CHECK(many.read(cy).get("wait_ms").integer()==20000);}
    {   // Under keys: never himself; his choice of who he is shown.
        Server s("public",{"map=on"});auto a=id::makeKeys(),b=id::makeKeys(),c=id::makeKeys();const std::string A=id::account(a.pub),B=id::account(b.pub);
        auto body=[&](const std::string& more){return "{\"player\":\""+A+"\""+more+"}";};auto t=s.keyToken(a);s.keyToken(b);
        s.spot(id::relayId(a.pub),"Ann");s.spot(id::relayId(b.pub),"Bob");s.spot(id::relayId(c.pub),"Cy");
        auto names=s.names(t);CHECK(names.find("Ann")==std::string::npos&&names.find("Bob")!=std::string::npos&&names.find("Cy")!=std::string::npos);
        CHECK(s.signedPost(a,"friends",body(",\"add\":[\""+B+"\"],\"show_others\":\"friends\"")).status==200);CHECK(s.names(t)=="Bob,");
        CHECK(s.signedPost(a,"friends",body(",\"show_others\":\"nobody\"")).status==200);CHECK(s.names(t).empty()&&s.read(t).get("on").boolean());}
    if(!failures)std::puts("map checks passed");
    return failures?1:0;
}
