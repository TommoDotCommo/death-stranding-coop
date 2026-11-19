// A world that is the server's (policy `world`, README "A world that is the server's"): the ids this server gives can be told
// as its own, a game can read which others to accept, and a player is handed back what he built here, told as another
// builder's, so a save that lacks it still shows it.
#include "front.h"
#include "api.h"
#include "idtag.h"
#include <algorithm>
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"server world oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
static bool throws(const std::function<void()>& f){try{f();}catch(const std::exception&){return true;}return false;}
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="public",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);p.identity="none";p.build="everyone";p.acceptOffers="auto";p.perBuilder=0;p.spread=0;for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    std::string session(const std::string& key){return json::parse(w.signIn(key,key,"g").body).get("session").get("token").string();}
    std::string token(const std::string& key){auto r=raw("POST","/s3cret/coop/notice-token","{\"player\":\""+key+"\"}");CHECK(r.status==200);return r.status==200?json::parse(r.body).get("token").string():"";}
    std::string make(const std::string& s,const std::string& extra=""){auto r=w.call(s,"createObject","{\"t\":\"l\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3"+extra+"}",now);CHECK(r.status==200);return r.status==200?json::parse(r.body).get("id").string():"";}
    std::vector<std::pair<std::string,std::string>> area(const std::string& s){std::vector<std::pair<std::string,std::string>> out;
        auto r=w.call(s,"findQpidObjects","{\"q\":226,\"o\":{\"ct\":{\"l\":50}}}",now);CHECK(r.status==200);
        if(r.status==200){auto d=json::parse(r.body);for(auto& o:d.get("n").get("oa").items())out.emplace_back(o.get("id").string(),o.get("c").string());}
        return out;}
    json::Value ids(const std::string& t){auto r=raw("GET","/s3cret/coop/world-ids","",{{"x-coop-token",t}});CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
    std::vector<std::string> untagged(const std::string& t){std::vector<std::string> out;auto d=ids(t);for(auto& v:d.get("untagged").items())out.push_back(v.string());std::sort(out.begin(),out.end());return out;}
};
static const std::string* has(const std::vector<std::pair<std::string,std::string>>& v,const std::string& id){for(auto& e:v)if(e.first==id)return &e.second;return nullptr;}
int main(){
    {   // The tag: the server's key and the id's first part decide it, a changed letter or another key fails, other lengths never carry one.
        std::string id="lABCDEFGH0000";idtag::stamp("key",id);CHECK(id.size()==13&&id.substr(0,9)=="lABCDEFGH"&&idtag::carries("key",id)&&!idtag::carries("other",id));
        auto changed=id;changed[3]='z';CHECK(!idtag::carries("key",changed));changed=id;changed[12]=changed[12]=='0'?'1':'0';CHECK(!idtag::carries("key",changed));
        CHECK(!idtag::carries("key",id.substr(0,12))&&!idtag::carries("key",id+"0")&&!idtag::carries("key",""));
        std::string shortId="l1";idtag::stamp("key",shortId);CHECK(shortId=="l1");
        for(char c:id)CHECK(std::isalnum(static_cast<unsigned char>(c)));
        // Ids of other keys rarely pass: a few of a few thousand would hide nothing that matters, many would.
        unsigned same=0;for(int i=0;i<5000;++i){std::string a="l"+std::to_string(100000000+i)+"0000";idtag::stamp("key",a);same+=idtag::carries("other",a);}CHECK(same<=1);}
    {   // The setting: the public preset is the server's world, friends and solo keep the save's; nothing else is taken.
        CHECK(presetPolicy("public").world=="server"&&presetPolicy("friends").world=="save"&&presetPolicy("solo").world=="save");
        Policy p=presetPolicy("friends");setPolicy(p,"world=server");CHECK(p.world=="server");setPolicy(p,"world=save");CHECK(p.world=="save");CHECK(throws([&]{setPolicy(p,"world=mine");}));
        Server s("friends",{"world=server"});World back;back.restore(s.w.snapshot());CHECK(back.policy().world=="server");}
    {   // Every id this server makes carries its tag, a built one and one of a collection's; one a game brings does not.
        Server s;auto ann=s.session("ann");auto made=s.make(ann);CHECK(idtag::carries(s.w.serverId(),made));
        const std::string old="lOLDOLDOLD123";CHECK(!idtag::carries(s.w.serverId(),old));CHECK(s.make(ann,",\"id\":\""+old+"\"")==old);
        auto t=s.token("ann");auto d=s.ids(t);CHECK(d.get("format").integer()==1&&d.get("server").string()==s.w.serverId()&&d.get("world").string()=="server"&&d.get("wait_ms").integer()==int64_t(WorldIdsWaitMs));
        CHECK(s.untagged(t)==std::vector<std::string>{old});
        // Only with the token, and only by GET.
        CHECK(s.raw("GET","/s3cret/coop/world-ids").status==403&&s.raw("GET","/s3cret/coop/world-ids","",{{"x-coop-token","nonsense"}}).status==403);
        CHECK(s.raw("POST","/s3cret/coop/world-ids","{}",{{"x-coop-token",t}}).status==404);
        // Another player's object he may not be handed is not named to him; his builder still has it.
        auto cid=s.session("cid");s.w.setPlayer(playerId("cid"),"offer_builds=no");const std::string kept="lKEPTKEPT1234";CHECK(s.make(cid,",\"id\":\""+kept+"\"")==kept);
        CHECK(s.untagged(t)==std::vector<std::string>{old});auto c=s.token("cid");auto mine=s.untagged(c);CHECK(std::count(mine.begin(),mine.end(),kept)==1&&std::count(mine.begin(),mine.end(),old)==1);}
    {   // Handed back: his own, under the collection builder's name; to everyone else under his. In a world of the saves, not at all.
        Server s;auto ann=s.session("ann"),bob=s.session("bob");auto a=s.make(ann),b=s.make(bob);
        auto mine=s.area(ann);CHECK(mine.size()==2);auto* back=has(mine,a);CHECK(back&&*back==collectionBuilderId(playerId("ann"))&&*back!=playerId("ann"));
        auto* other=has(mine,b);CHECK(other&&*other==playerId("bob"));
        auto theirs=s.area(bob);auto* seen=has(theirs,a);CHECK(seen&&*seen==playerId("ann"));
        auto again=s.area(ann);CHECK(again==mine);    // the same ones next time
        // Asked by id he is still told it is his: the game's own question about its own object.
        auto got=json::parse(s.w.call(ann,"getQpidObjects","{\"ids\":[\""+a+"\"]}",s.now).body).get("objects").items();CHECK(got.size()==1&&got[0].get("c").string()==playerId("ann"));
        Server f("friends");auto fa=f.session("ann"),fb=f.session("bob");auto x=f.make(fa),y=f.make(fb);auto v=f.area(fa);CHECK(v.size()==1&&!has(v,x)&&has(v,y));}
    {   // His own that nobody else may have (kept) comes back to him alone.
        Server s;auto cid=s.session("cid"),bob=s.session("bob");s.w.setPlayer(playerId("cid"),"offer_builds=no");auto k=s.make(cid);
        CHECK(has(s.area(cid),k)&&!has(s.area(bob),k));}
    if(!failures)std::puts("server world checks passed");
    return failures?1:0;
}
