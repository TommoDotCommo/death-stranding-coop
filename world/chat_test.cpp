// Text chat on the server (world.h chatRead, chatSay) through the HTTP surface: only with a player's notice token, under
// that player's name; no history for a game that joins; nothing while the policy says off; the limits on a line, on how
// fast a player talks and on what is kept; a kicked or banned player is out at once.
#include "front.h"
#include "api.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"chat oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
struct Server {
    World w;Front f;int64_t now=1800000000;std::vector<std::string> log;
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);w.log=[this](const std::string& l){log.push_back(l);};}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    std::string token(const std::string& key,const std::string& name){json::parse(w.signIn(key,name,"g").body);auto r=raw("POST","/s3cret/coop/notice-token","{\"player\":\""+key+"\"}");CHECK(r.status==200);return json::parse(r.body).get("token").string();}
    Reply say(const std::string& t,const std::string& text){json::Value v=json::Value::object();v.set("text",text);return raw("POST","/s3cret/coop/chat",v.dump(),{{"x-coop-token",t}});}
    json::Value read(const std::string& t,const std::string& since=""){auto r=raw("GET","/s3cret/coop/chat"+(since.empty()?std::string():"?since="+since),"",{{"x-coop-token",t}});CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
};
static std::string row(const json::Value& l){return l.get("name").string()+": "+l.get("text").string();}
int main(){
    {   Server s;auto ann=s.token("ann","Ann"),bob=s.token("bob","Bob");
        // A game that joins is told where to go on from, and no history.
        CHECK(s.say(ann,"early").status==200);auto first=s.read(bob);CHECK(first.get("format").integer()==1&&first.get("on").boolean()&&first.get("lines").items().empty()&&first.get("next").integer()==2);
        CHECK(s.say(ann,"  hello there  ").status==200);CHECK(s.say(bob,"h\xc3\xa9llo").status==200);
        auto d=s.read(bob,"2");auto& l=d.get("lines").items();CHECK(l.size()==2&&row(l[0])=="Ann: hello there"&&row(l[1])=="Bob: h\xc3\xa9llo"&&d.get("next").integer()==4);
        CHECK(s.read(ann,"4").get("lines").items().empty());
        // A number from before a restart of the server (past what it has): nothing, and where to go on.
        auto later=s.read(ann,"99");CHECK(later.get("lines").items().empty()&&later.get("next").integer()==4);
        for(auto bad:{"abc","-1","","12345678901234567890"}){auto v=s.read(ann,bad);CHECK(v.get("lines").items().empty());}
        // The owner can read it in the log.
        bool logged=false;for(auto& e:s.log)logged=logged||e.find("hello there")!=std::string::npos;CHECK(logged);}
    {   // Only a good token, and only this route.
        Server s;auto ann=s.token("ann","Ann");
        CHECK(s.say("","x").status==403&&s.say("not-a-token","x").status==403);
        CHECK(s.raw("GET","/s3cret/coop/chat","",{{"x-coop-token","nope"}}).status==403&&s.raw("GET","/s3cret/coop/chat").status==403);
        CHECK(s.raw("GET","/coop/chat","",{{"x-coop-token",ann}}).status==404&&s.raw("DELETE","/s3cret/coop/chat","",{{"x-coop-token",ann}}).status==404);
        s.now+=NoticeTokenUnusedSeconds+1;CHECK(s.say(ann,"late").status==403);}
    {   // The text: not empty, not too long, no control characters, valid UTF-8; a body that is no JSON.
        Server s;auto ann=s.token("ann","Ann");
        CHECK(s.say(ann,"   ").status==400&&s.say(ann,"a\nb").status==400&&s.say(ann,std::string(161,'x')).status==400&&s.say(ann,"\xff").status==400);
        CHECK(s.raw("POST","/s3cret/coop/chat","nonsense",{{"x-coop-token",ann}}).status==400);CHECK(s.say(ann,std::string(160,'x')).status==200);
        std::string wide;for(int i=0;i<160;++i)wide+="\xc3\xa9";CHECK(s.say(ann,wide).status==200);}
    {   // Five lines in ten seconds; each player has his own count; time frees it.
        Server s;auto ann=s.token("ann","Ann"),bob=s.token("bob","Bob");
        for(int i=0;i<5;++i)CHECK(s.say(ann,"m").status==200);
        CHECK(s.say(ann,"m").status==429&&s.say(bob,"m").status==200);s.now+=ChatWindowSeconds;CHECK(s.say(ann,"m").status==200);}
    {   // What is kept and handed at once.
        Server s;auto ann=s.token("ann","Ann");auto start=s.read(ann).get("next").integer();
        for(size_t i=0;i<ChatKept+20;++i){s.now+=3;auto r=s.say(ann,"m"+std::to_string(i));CHECK(r.status==200);}
        auto d=s.read(ann,std::to_string(start));CHECK(d.get("lines").items().size()==ChatBatch&&d.get("lines").items()[0].get("text").string()=="m20");
        auto rest=s.read(ann,std::to_string(d.get("next").integer()));CHECK(rest.get("lines").items()[0].get("text").string()=="m"+std::to_string(20+ChatBatch));}
    {   // The policy: off in the solo and public presets; off refuses lines and says so; a player kicked or banned is out.
        CHECK(presetPolicy("friends").chat=="on"&&presetPolicy("solo").chat=="off"&&presetPolicy("public").chat=="off");
        Server off("friends",{"chat=off"});auto t=off.token("ann","Ann");auto r=off.say(t,"x");CHECK(r.status==403&&r.body.find("chat-off")!=std::string::npos&&!off.read(t).get("on").boolean());
        Server s;auto ann=s.token("ann","Ann");CHECK(s.say(ann,"x").status==200);s.w.setPlayer(playerId("ann"),"banned=yes");CHECK(s.say(ann,"y").status==403&&s.raw("GET","/s3cret/coop/chat","",{{"x-coop-token",ann}}).status==403);}
    if(failures){std::fprintf(stderr,"%u chat failures\n",failures);return 1;}
    std::puts("chat tests passed");
}
