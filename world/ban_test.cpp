// Kick and ban (README, "Kick and ban"): the owner's kick ends a player's game session, sign-in ticket, relay token and
// notice token at once, and he may take new ones; a ban does the same and refuses him every new one, every sign-in
// and every launcher call but the rules, until it is lifted; only the owner's key does either; the world file keeps a
// ban; under identity none the ban refuses the account's sign-in by name.
#include "front.h"
#include "api.h"
#include <cctype>
#include <cstdio>
using namespace world;
namespace id=world::identity;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"ban oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="public"):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){w.setPolicy(presetPolicy(preset));}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    Reply api(const id::Keys* k,const char* method,const std::string& call,const std::string& body=""){
        std::string target="/s3cret/coop/"+call;std::map<std::string,std::string> h;
        if(k)for(auto& x:id::signRequest(*k,method,target,w.serverId(),body,now,id::newNonce())){
            std::string name=x.first;for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));h[name]=x.second;}
        return raw(method,target,body,h);}
    Reply post(const id::Keys* k,const std::string& call,const std::string& body){return api(k,"POST",call,body);}
    std::string pass(const id::Keys& k,const char* call){auto r=post(&k,call,"{\"player\":\""+id::account(k.pub)+"\"}");
        return r.status!=200?std::string():json::parse(r.body).get(std::string(call)=="ticket"?"ticket":"token").string();}
    std::string session(const std::string& ticket){auto r=raw("GET","/s3cret/t/"+ticket+"/auth/ds?provider=steam&display_name=&code=00");
        return r.status==200?json::parse(r.body).get("session").get("token").string():"";}
    int gate(const std::string& token){return raw("POST","/s3cret/gate/e/getVersion","{}",{{"authorization","Bearer "+token}}).status;}
};
static std::string me(const id::Keys& k){return "{\"player\":\""+id::account(k.pub)+"\"}";}
static std::string who(const id::Keys& k){return "{\"player\":\""+playerId(id::account(k.pub))+"\"}";}
int main(){
    auto o=id::makeKeys(),a=id::makeKeys(),b=id::makeKeys();
    const uint64_t ida=id::relayId(a.pub),idb=id::relayId(b.pub);
    Server s;s.w.setOwner(id::account(o.pub));
    // Ann plays: a session, a relay token, a notice token. Bea too.
    std::string session=s.session(s.pass(a,"ticket")),relay=s.pass(a,"relay-token"),notice=s.pass(a,"notice-token");
    std::string sessionB=s.session(s.pass(b,"ticket")),relayB=s.pass(b,"relay-token");
    CHECK(!session.empty()&&!relay.empty()&&!notice.empty()&&!sessionB.empty());
    CHECK(s.gate(session)==200&&s.w.relayAdmit(ida,relay,"",Now)&&s.w.notices(notice,"",Now).status==200);
    // Only the owner kicks or bans: unsigned 401, another key 403, nothing changes.
    CHECK(s.post(nullptr,"owner/kick",who(a)).status==401&&s.post(&b,"owner/kick",who(a)).status==403);
    CHECK(s.post(nullptr,"owner/ban",who(a).substr(0,who(a).size()-1)+",\"ban\":\"yes\"}").status==401);
    CHECK(s.post(&b,"owner/ban",who(a).substr(0,who(a).size()-1)+",\"ban\":\"yes\"}").status==403);
    CHECK(s.gate(session)==200&&s.w.relayAdmit(ida,relay,"",Now));
    CHECK(s.post(&o,"owner/kick","{\"player\":\"zygo_nobody\"}").status==403&&s.post(&o,"owner/ban","{\"player\":\"x\",\"ban\":\"maybe\"}").status==400);
    // A kick ends every session and pass of hers at once, and nobody else's.
    auto k=s.post(&o,"owner/kick",who(a));
    CHECK(k.status==200&&json::parse(k.body).get("ended").integer()==4);   // session, ticket, relay token, notice token
    CHECK(s.gate(session)==401);
    CHECK(!s.w.relayAdmit(ida,relay,"",Now));
    CHECK(s.w.notices(notice,"",Now).status==403);
    CHECK(s.gate(sessionB)==200&&s.w.relayAdmit(idb,relayB,"",Now));
    {auto gone=relayEvictions(s.w);CHECK(gone.size()==1&&gone[0]==ida);CHECK(relayEvictions(s.w).empty());}   // the relay takes her off at once
    // After a kick she may come back from her launcher.
    session=s.session(s.pass(a,"ticket"));relay=s.pass(a,"relay-token");
    CHECK(!session.empty()&&s.gate(session)==200&&s.w.relayAdmit(ida,relay,"",Now));
    // A ban: the same at once, and nothing new while it holds.
    auto r=s.post(&o,"owner/ban",who(a).substr(0,who(a).size()-1)+",\"ban\":\"yes\"}");
    CHECK(r.status==200);
    bool listed=false;const auto answer=json::parse(r.body);for(auto& p:answer.get("players").items())if(p.get("id").string()==playerId(id::account(a.pub)))listed=p.get("banned").boolean();
    CHECK(listed);
    CHECK(s.gate(session)==401&&!s.w.relayAdmit(ida,relay,"",Now));
    CHECK(s.post(&a,"ticket",me(a)).status==403&&s.post(&a,"relay-token",me(a)).status==403&&s.post(&a,"notice-token",me(a)).status==403);
    CHECK(s.api(&a,"GET","me?player="+id::account(a.pub)).status==403);
    CHECK(s.api(&a,"GET","friends?player="+id::account(a.pub)).status==403);
    CHECK(s.post(&a,"choices",me(a).substr(0,me(a).size()-1)+",\"offer_builds\":\"no\"}").status==403);
    CHECK(s.api(nullptr,"GET","policy").status==200);   // the rules are everyone's to read
    // The world's own refusal, under the launcher's check: no pass of any kind for a banned account.
    bool refused=false;try{s.w.apiTicket(id::account(a.pub),Now);}catch(const std::exception&){refused=true;}CHECK(refused);
    refused=false;try{s.w.apiRelayToken(id::account(a.pub),ida,Now);}catch(const std::exception&){refused=true;}CHECK(refused);
    refused=false;try{s.w.apiNoticeToken(id::account(a.pub),Now);}catch(const std::exception&){refused=true;}CHECK(refused);
    // Bea plays on.
    CHECK(s.gate(sessionB)==200&&s.w.relayAdmit(idb,relayB,"",Now)&&!s.pass(b,"ticket").empty());
    // The world file keeps the ban; a ban that is no true or false is refused.
    {Server t;t.w.restore(s.w.snapshot());bool kept=false;try{t.w.apiTicket(id::account(a.pub),Now);}catch(const std::exception&){kept=true;}CHECK(kept);
     auto bad=s.w.snapshot();bool found=false;
     auto text=bad.dump();auto at=text.find("\"banned\":true");found=at!=std::string::npos;CHECK(found);
     if(found){text.replace(at,13,"\"banned\":\"yes\"");bool thrown=false;World u;try{u.restore(json::parse(text));}catch(const std::exception&){thrown=true;}CHECK(thrown);}}
    // Lifted: she comes back.
    CHECK(s.post(&o,"owner/ban",who(a).substr(0,who(a).size()-1)+",\"ban\":\"no\"}").status==200);
    CHECK(!s.session(s.pass(a,"ticket")).empty()&&s.api(&a,"GET","me?player="+id::account(a.pub)).status==200);
    // Identity none: a ban refuses the account's sign-in by name and ends its session (offline: player-set banned=yes).
    {Server f("friends");
     auto in=f.raw("GET","/s3cret/k/ann/auth/ds?provider=steam&display_name=Ann&code=00");CHECK(in.status==200);
     std::string t=in.status==200?json::parse(in.body).get("session").get("token").string():"";
     CHECK(f.gate(t)==200);
     f.w.setPlayer(playerId("ann"),"banned=yes");
     CHECK(f.gate(t)==401);
     CHECK(f.raw("GET","/s3cret/k/ann/auth/ds?provider=steam&display_name=Ann&code=00").status==403);
     CHECK(f.raw("GET","/s3cret/k/bea/auth/ds?provider=steam&display_name=Bea&code=00").status==200);
     CHECK(f.api(nullptr,"GET","me?player=ann").status==403);
     f.w.setPlayer(playerId("ann"),"banned=no");
     CHECK(f.raw("GET","/s3cret/k/ann/auth/ds?provider=steam&display_name=Ann&code=00").status==200);}
    if(failures){std::fprintf(stderr,"%u ban checks failed\n",failures);return 1;}
    std::puts("ban: ok");return 0;
}
