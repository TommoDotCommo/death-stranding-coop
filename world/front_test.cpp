// The world server's HTTP surface (front.h): the secret prefix, the account in the address or in the ticket, the
// gateway address handed back, and the bearer token.
#include "front.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"front oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Request req(const char* method,const std::string& target,const std::string& body="",const std::string& auth=""){
    Request r;r.method=method;r.target=target;r.body=body;r.headers["host"]="10.0.0.2:8089";if(!auth.empty())r.headers["authorization"]=auth;return r;}
int main(){
    const std::string ticket="14000000aabbccddeeff00117856341201001001"+std::string(40,'0');
    CHECK(steamIdOf(ticket)=="76561198265685624");CHECK(steamIdOf("1400")=="");CHECK(steamIdOf(std::string(60,'z'))=="");
    CHECK(steamIdOf("14000000aabbccddeeff00117856341201001002"+std::string(40,'0'))=="");   // not a player account
    unsigned n=0;World w([&n]{return "tok"+std::to_string(++n);});Front f(w,"s3cret");
    // Without the prefix nothing answers and nothing is created.
    CHECK(f.handle(req("GET","/auth/ds?provider=steam&display_name=&code="+ticket),1).status==404);
    CHECK(f.handle(req("GET","/s3cretX/auth/ds?code="+ticket),1).status==404);
    CHECK(f.handle(req("GET","/other/k/ann/auth/ds?code="+ticket),1).status==404);
    CHECK(w.snapshot().get("players").items().empty());
    auto a=f.handle(req("GET","/s3cret/auth/ds?provider=steam&display_name=Ann%20B&code="+ticket),1);CHECK(a.status==200);
    auto ja=json::parse(a.body);
    CHECK(ja.get("user").get("id").string()==playerId("76561198265685624"));CHECK(ja.get("user").get("display_name").string()=="Ann B");
    CHECK(ja.get("session").get("gateway").string()=="http://10.0.0.2:8089/s3cret/gate");
    auto ta=ja.get("session").get("token").string();
    // The same Steam account in a second copy: the address names the player.
    auto b=f.handle(req("GET","/s3cret/k/bob/auth/ds?provider=steam&display_name=&code="+ticket),1);CHECK(b.status==200);
    auto jb=json::parse(b.body);CHECK(jb.get("user").get("id").string()==playerId("bob"));auto tb=jb.get("session").get("token").string();
    CHECK(f.handle(req("GET","/s3cret/k/b%20b/auth/ds?code="+ticket),1).status==404);
    CHECK(f.handle(req("GET","/s3cret/auth/ds?code=1234"),1).status==400);
    CHECK(f.handle(req("POST","/s3cret/auth/ds?code="+ticket),1).status==404);
    // Gateway calls: the bearer token is the session.
    CHECK(f.handle(req("GET","/s3cret/gate/e/getVersion","","Bearer "+ta),1).status==200);
    CHECK(f.handle(req("GET","/s3cret/gate/getVersion","","Bearer "+ta),1).status==200);
    CHECK(f.handle(req("GET","/s3cret/gate/e/getVersion"),1).status==401);
    CHECK(f.handle(req("GET","/s3cret/gate/e/getVersion","","Basic "+ta),1).status==401);
    CHECK(f.handle(req("GET","/s3cret/gate/e/getVersion?token="+ta),1).status==401);
    CHECK(f.handle(req("GET","/gate/e/getVersion","","Bearer "+ta),1).status==404);
    CHECK(f.handle(req("GET","/s3cret/gate/e/a/b","","Bearer "+ta),1).status==404);
    CHECK(f.handle(req("POST","/s3cret/gate/e/createObject","{\"data\":\"{\"t\":\"l\",\"q\":226}\"}","Bearer "+ta),5).status==200);
    auto found=json::parse(f.handle(req("POST","/s3cret/gate/e/findQpidObjects","{\"data\":\"{\"q\":226}\"}","Bearer "+tb),6).body);
    CHECK(found.get("n").get("oa").items().size()==1);
    // No prefix configured: plain paths.
    World w2([&n]{return "t"+std::to_string(++n);});Front open(w2,"");
    CHECK(open.handle(req("GET","/auth/ds?code="+ticket),1).status==200);
    CHECK(json::parse(open.handle(req("GET","/k/x/auth/ds?code="+ticket),1).body).get("session").get("gateway").string()=="http://10.0.0.2:8089/gate");
    // Linked servers: /link/pull exists only for a server that has links, only with one of their keys, and the key
    // picks the filter. A game's session token is no key, and a key is no session.
    const std::string ask="{\"format\":1,\"server\":\"otherServer\",\"have\":\"\",\"after\":\"\"}";
    CHECK(f.handle(req("POST","/s3cret/link/pull",ask,"Link key-for-north"),7).status==404);
    LinkConfig north,south;north.name="north";north.key="key-for-north";north.exportFilter=parseFilter("all");
    south.name="south";south.key="key-for-south";south.exportFilter=parseFilter("types=r");
    std::vector<std::string> lines;w.log=[&lines](const std::string& text){lines.push_back(text);};
    Front linked(w,"s3cret",{north,south});
    auto pulled=linked.handle(req("POST","/s3cret/link/pull",ask,"Link key-for-north"),7);CHECK(pulled.status==200);
    CHECK(json::parse(pulled.body).get("objects").items().size()==1&&json::parse(pulled.body).get("server").string()==w.serverId());
    pulled=linked.handle(req("POST","/s3cret/link/pull",ask,"Link key-for-south"),7);CHECK(pulled.status==200&&json::parse(pulled.body).get("objects").items().empty());
    CHECK(lines.empty());
    for(const char* auth:{"Link key-for-nobody","Link key-for-nort","Link key-for-northh","Link ","","Bearer key-for-north","key-for-north","link key-for-north"})
        CHECK(linked.handle(req("POST","/s3cret/link/pull",ask,auth),7).status==404);
    CHECK(lines.size()==8&&lines[0].find("link request refused")==0);
    CHECK(linked.handle(req("POST","/s3cret/link/pull",ask,"Link "+ta),7).status==404);
    CHECK(linked.handle(req("GET","/s3cret/link/pull",ask,"Link key-for-north"),7).status==404);
    CHECK(linked.handle(req("POST","/link/pull",ask,"Link key-for-north"),7).status==404);
    CHECK(linked.handle(req("POST","/s3cret/link/pull","garbage","Link key-for-north"),7).status==400);
    CHECK(linked.handle(req("POST","/s3cret/gate/e/getVersion","","Bearer key-for-north"),7).status==401);
    CHECK(linked.handle(req("POST","/s3cret/gate/e/getVersion","","Link key-for-north"),7).status==401);
    w.log=nullptr;
    if(failures)return 1;
    std::puts("front tests passed");return 0;
}
