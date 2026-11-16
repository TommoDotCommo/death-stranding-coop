// Identity keys (README, "Identity") through the HTTP surface and the relay: a public server takes only a launcher's
// sign-in ticket and signed API calls; a signed call acts for its signer only, once, for this server; a player is made
// by his first signed call; tickets and relay tokens belong to one player, end when he takes a new one, and expire; the
// owner's calls need the owner's key; the world file keeps it all; identity none is as before, except that an account
// of the key form is acted for only with its key. A friend list and the choice of who a player sees are his own to
// set, by his key; the relay takes them for his relay id, and the friends by theirs.
#include "front.h"
#include "api.h"
#include "relay.h"
#include <cctype>
#include <cstdio>
#include <memory>
using namespace world;
namespace id=world::identity;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"keys oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
constexpr int64_t Now=1800000000;
struct Server {
    World w;Front f;int64_t now=Now;
    explicit Server(const char* preset="public",std::vector<const char*> settings={}):w([]{return "tok"+std::to_string(++counter);}),f(w,"s3cret"){
        Policy p=presetPolicy(preset);for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    Reply raw(const char* method,const std::string& target,const std::string& body="",std::map<std::string,std::string> headers={}){
        Request r;r.method=method;r.target=target;r.body=body;r.headers=std::move(headers);r.headers["host"]="10.0.0.2:8089";return f.handle(r,now);}
    // A launcher's call to /coop/CALL, signed with `k` (nullptr: unsigned). `server`, `time`: what the signer names.
    std::map<std::string,std::string> sign(const id::Keys& k,const char* method,const std::string& target,const std::string& body,std::string server="",int64_t time=-1){
        std::map<std::string,std::string> h;
        for(auto& x:id::signRequest(k,method,target,server.empty()?w.serverId():server,body,time<0?now:time,id::newNonce())){
            std::string name=x.first;for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));h[name]=x.second;}
        return h;}
    Reply api(const id::Keys* k,const char* method,const std::string& call,const std::string& body=""){
        std::string target="/s3cret/coop/"+call;return raw(method,target,body,k?sign(*k,method,target,body):std::map<std::string,std::string>{});}
    Reply post(const id::Keys* k,const std::string& call,const std::string& body){return api(k,"POST",call,body);}
    std::string ticket(const id::Keys& k){auto r=post(&k,"ticket","{\"player\":\""+id::account(k.pub)+"\"}");CHECK(r.status==200);return r.status==200?json::parse(r.body).get("ticket").string():"";}
    std::string token(const id::Keys& k){auto r=post(&k,"relay-token","{\"player\":\""+id::account(k.pub)+"\"}");CHECK(r.status==200);return r.status==200?json::parse(r.body).get("token").string():"";}
    Reply signIn(const std::string& ticket,const char* name="Ann"){return raw("GET","/s3cret/t/"+ticket+"/auth/ds?provider=steam&display_name="+name+"&code=00");}
    std::string session(const std::string& ticket,const char* name="Ann"){auto r=signIn(ticket,name);CHECK(r.status==200);return r.status==200?json::parse(r.body).get("session").get("token").string():"";}
    size_t players(){return w.snapshot().get("players").items().size();}
};
static std::string body(const id::Keys& k,const std::string& more=""){return "{\"player\":\""+id::account(k.pub)+"\""+more+"}";}
static void signIns(){
    Server s;auto p=json::parse(s.api(nullptr,"GET","policy").body);
    CHECK(p.get("settings").items().size()==13&&p.get("settings").items()[11].get("key").string()=="identity"&&p.get("settings").items()[11].get("value").string()=="keys");
    CHECK(p.get("time").integer()==Now&&!p.get("owner").boolean());
    // No sign-in but a launcher's ticket: not by a name in the address, not by a Steam ticket, not by a key's name.
    const std::string steam="?provider=steam&display_name=Ann&code=14000000aabbccddeeff00117856341201001001"+std::string(40,'0');
    auto a=id::makeKeys();
    CHECK(s.raw("GET","/s3cret/k/ann/auth/ds"+steam).status==403&&s.raw("GET","/s3cret/auth/ds"+steam).status==403);
    CHECK(s.raw("GET","/s3cret/k/"+id::account(a.pub)+"/auth/ds"+steam).status==403&&s.players()==0);
    CHECK(s.signIn("tok999").status==403&&s.raw("GET","/s3cret/t//auth/ds").status==404&&s.raw("GET","/s3cret/t/x.y/auth/ds").status==404);
    // A ticket: its player only, the game's whole session, for a short time.
    auto t=s.ticket(a);CHECK(!t.empty()&&s.players()==1);
    auto r=s.signIn(t);auto user=json::parse(r.body).get("user");
    CHECK(r.status==200&&user.get("providerId").string()==id::account(a.pub)&&user.get("id").string()==playerId(id::account(a.pub))&&user.get("displayName").string()=="Ann");
    auto token=json::parse(r.body).get("session").get("token").string();
    CHECK(s.w.call(token,"getVersion","",Now).status==200&&s.w.playerOf(token)==playerId(id::account(a.pub)));
    CHECK(s.raw("GET","/s3cret/t/"+t+"/gate/e/getVersion","",{{"authorization","Bearer "+token}}).status==404);   // a ticket is for the sign-in only
    // The game asking again soon after is the same sign-in; later the ticket is spent.
    s.now=Now+TicketGraceSeconds;CHECK(s.signIn(t).status==200);
    s.now=Now+TicketGraceSeconds+1;CHECK(s.signIn(t).status==403);
    // A ticket that is never used ends after TicketUnusedSeconds; a new one ends the older one.
    s.now=Now+1000;auto unused=s.ticket(a);s.now=Now+1000+TicketUnusedSeconds+1;CHECK(s.signIn(unused).status==403);
    auto one=s.ticket(a),two=s.ticket(a);CHECK(one!=two&&s.signIn(one).status==403&&s.signIn(two).status==200);
    // Another player's ticket is his: it never signs in as the first.
    auto b=id::makeKeys();auto tb=s.ticket(b);auto rb=json::parse(s.signIn(tb,"Bob").body);
    CHECK(rb.get("user").get("providerId").string()==id::account(b.pub)&&rb.get("user").get("id").string()!=user.get("id").string()&&s.players()==2);
    // The world's number of players applies to a new key.
    {Server one("public",{"players=1"});auto x=id::makeKeys(),y=id::makeKeys();
     CHECK(!one.ticket(x).empty()&&one.post(&y,"ticket",body(y)).status==403&&one.post(&y,"relay-token",body(y)).status==403&&one.players()==1);}
    // A session from before the world took keys is no session.
    {Server f("friends");auto old=json::parse(f.w.signIn("steam-a","Ann","g").body).get("session").get("token").string();
     CHECK(f.w.call(old,"getVersion","",Now).status==200);
     Policy p=f.w.policy();setPolicy(p,"identity=keys");f.w.setPolicy(p);
     CHECK(f.w.call(old,"getVersion","",Now).status==401);
     auto k=id::makeKeys();auto fresh=f.session(f.ticket(k));CHECK(f.w.call(fresh,"getVersion","",Now).status==200);}
}
static void calls(){
    Server s;auto a=id::makeKeys(),b=id::makeKeys();const std::string A=id::account(a.pub),B=id::account(b.pub);
    // Unsigned: refused, and nothing changed.
    auto before=s.w.snapshot().dump();
    CHECK(s.api(nullptr,"GET","me?player="+A).status==401&&s.api(nullptr,"GET","mine?player="+A).status==401);
    CHECK(s.post(nullptr,"choices",body(a,",\"offer_builds\":\"no\"")).status==401&&s.post(nullptr,"offer",body(a,",\"all\":true")).status==401);
    CHECK(s.post(nullptr,"withdraw",body(a,",\"all\":true")).status==401&&s.post(nullptr,"ticket",body(a)).status==401&&s.post(nullptr,"relay-token",body(a)).status==401);
    CHECK(s.post(nullptr,"choices","{\"player\":\"ann\",\"offer_builds\":\"no\"}").status==401&&s.w.snapshot().dump()==before);
    // Signed: his first call makes him, a call acts for its signer only.
    auto me=s.post(&a,"choices",body(a,",\"offer_builds\":\"ask\""));
    CHECK(me.status==200&&json::parse(me.body).get("known").boolean()&&json::parse(me.body).get("player").string()==playerId(A)&&s.players()==1);
    CHECK(s.api(&a,"GET","me?player="+A).status==200&&s.api(&a,"GET","mine?player="+A).status==200);
    before=s.w.snapshot().dump();
    CHECK(s.post(&b,"choices",body(a,",\"offer_builds\":\"no\"")).status==403&&s.post(&b,"withdraw",body(a,",\"all\":true")).status==403);
    CHECK(s.post(&b,"ticket",body(a)).status==403&&s.post(&b,"relay-token",body(a)).status==403&&s.api(&b,"GET","me?player="+A).status==403);
    CHECK(s.post(&a,"choices","{\"player\":\"ann\",\"offer_builds\":\"no\"}").status==403&&s.w.snapshot().dump()==before);
    // Taken once, for this server, as signed.
    std::string target="/s3cret/coop/choices",text=body(a,",\"offer_builds\":\"no\"");auto h=s.sign(a,"POST",target,text);
    CHECK(s.raw("POST",target,text,h).status==200&&s.raw("POST",target,text,h).status==401);
    h=s.sign(a,"POST",target,text);CHECK(s.raw("POST",target,body(a,",\"offer_builds\":\"yes\""),h).status==401);
    CHECK(s.raw("POST",target,text,s.sign(a,"POST",target,text,"OTHERSERVER00001")).status==401);
    CHECK(s.raw("POST",target,text,s.sign(a,"POST",target,text,"",Now-id::WindowSeconds-1)).status==401);
    CHECK(s.raw("POST","/s3cret/coop/withdraw",text,s.sign(a,"POST",target,text)).status==401);
    // A ticket and a relay token only under keys; the relay id is the one of his key.
    auto r=json::parse(s.post(&a,"relay-token",body(a)).body);CHECK(uint64_t(r.get("relay_id").integer())==id::relayId(a.pub)&&r.get("token").string().size()<=32);
    {Server f("friends");CHECK(f.post(&a,"ticket",body(a)).status==403&&f.post(&a,"relay-token",body(a)).status==403&&f.players()==0);}
}
static void none(){
    // Identity none is as before: unsigned calls act for any account, a sign-in by name or Steam ticket works. An
    // account of the key form is acted for only with its key; a signed call still acts for its signer only.
    Server s("friends");auto a=id::makeKeys(),b=id::makeKeys();const std::string A=id::account(a.pub);
    CHECK(s.post(nullptr,"choices","{\"player\":\"ann\",\"offer_builds\":\"no\"}").status==200&&s.raw("GET","/s3cret/k/ann/auth/ds?display_name=Ann").status==200);
    CHECK(s.post(nullptr,"choices",body(a,",\"offer_builds\":\"no\"")).status==403&&s.api(nullptr,"GET","me?player="+A).status==403);
    CHECK(s.raw("GET","/s3cret/k/"+A+"/auth/ds?display_name=Ann").status==403&&s.players()==1);
    CHECK(s.post(&b,"choices",body(a,",\"offer_builds\":\"no\"")).status==403&&s.post(&a,"choices",body(a,",\"offer_builds\":\"no\"")).status==200);
    CHECK(s.post(&a,"choices","{\"player\":\"ann\",\"offer_builds\":\"yes\"}").status==403);
}
// The relay: a token of the player's relay id, and nothing else, under keys.
struct Net {
    World& w;RelayConfig config;std::unique_ptr<Relay> relay;int64_t* now;std::vector<std::pair<RelayAddress,ds::relay::Buffer>> down;
    struct Peer { RelayAddress address;std::unique_ptr<ds::relay::Client> client; };
    std::vector<std::unique_ptr<Peer>> peers;uint64_t ms=1000;
    Net(World& world,int64_t& clock):w(world),now(&clock){
        config.token="group";config.admit=[this](uint64_t id,const std::string& t){return w.relayAdmit(id,t,"group",*now);};
        relay=std::make_unique<Relay>(config,[this](const RelayAddress& a,const uint8_t* d,size_t n){down.push_back({a,ds::relay::Buffer(d,d+n)});});}
    ds::relay::Client& add(uint64_t id,const std::string& token){
        auto p=std::make_unique<Peer>();p->address={0x0a000001u+uint32_t(peers.size()),uint16_t(40000+peers.size())};auto address=p->address;
        ds::relay::ClientConfig c;c.id=id;c.token=token;c.nickname="P";
        p->client=std::make_unique<ds::relay::Client>(c,[this,address](const uint8_t* d,size_t n){relay->receive(address,d,n,ms);});
        p->client->setPosition({0,0,0});peers.push_back(std::move(p));return *peers.back()->client;}
    void run(unsigned steps){
        for(unsigned i=0;i<steps;++i){ms+=100;for(auto& p:peers)p->client->tick(ms);
            auto got=std::move(down);down.clear();
            for(auto& d:got)for(auto& p:peers)if(p->address==d.first){uint64_t from;const uint8_t* payload;size_t size;p->client->receive(d.second.data(),d.second.size(),ms,from,payload,size);}
            relay->tick(ms);}}
};
static void relay(){
    Server s;auto a=id::makeKeys(),b=id::makeKeys();auto ta=s.token(a),tb=s.token(b);const uint64_t ida=id::relayId(a.pub),idb=id::relayId(b.pub);
    CHECK(s.w.relayAdmit(ida,ta,"group",Now)&&!s.w.relayAdmit(idb,ta,"group",Now)&&!s.w.relayAdmit(ida,tb,"group",Now)&&!s.w.relayAdmit(ida,"group","group",Now));
    CHECK(!s.w.relayAdmit(ida,"",       "group",Now)&&!s.w.relayAdmit(ida,ta+"x","group",Now));
    {   // Real players' sides: the token of his id is answered; the token of another id, or the group's key, is not.
        Net n(s.w,s.now);auto& good=n.add(ida,ta);auto& wrongId=n.add(ida+1,tb);auto& group=n.add(idb+7,"group");n.run(10);
        CHECK(good.online()&&!wrongId.online()&&!group.online()&&n.relay->players()==1);}
    // An unused token ends after RelayTokenUnusedSeconds; a used one while HELLOs keep coming, and RelayTokenIdleSeconds after.
    s.now=Now+RelayTokenUnusedSeconds+1;CHECK(!s.w.relayAdmit(idb,tb,"group",s.now));
    s.now=Now;auto t=s.token(a);CHECK(s.w.relayAdmit(ida,t,"group",Now+100)&&!s.w.relayAdmit(ida,ta,"group",Now+100));   // the new one ended the old one
    CHECK(s.w.relayAdmit(ida,t,"group",Now+100+RelayTokenIdleSeconds)&&s.w.relayAdmit(ida,t,"group",Now+100+2*RelayTokenIdleSeconds));
    CHECK(!s.w.relayAdmit(ida,t,"group",Now+100+3*RelayTokenIdleSeconds+1));
    // Identity none: the group's key, as before.
    Server f("friends");CHECK(f.w.relayAdmit(5,"group","group",Now)&&!f.w.relayAdmit(5,"grou","group",Now)&&!f.w.relayAdmit(5,"",  "group",Now));
    {Net n(f.w,f.now);auto& c=n.add(5,"group");n.run(10);CHECK(c.online());}
}
static void owner(){
    Server s;auto o=id::makeKeys(),a=id::makeKeys(),b=id::makeKeys();const std::string A=id::account(a.pub),B=id::account(b.pub);
    // No owner key yet: nobody is the owner.
    CHECK(s.api(&o,"GET","owner/players").status==403);
    s.w.setOwner(id::account(o.pub));CHECK(json::parse(s.api(nullptr,"GET","policy").body).get("owner").boolean());
    CHECK(s.api(&o,"GET","owner/players").status==200&&s.api(&a,"GET","owner/players").status==403&&s.api(nullptr,"GET","owner/players").status==401);
    CHECK(s.api(&o,"GET","owner/nothing").status==404&&s.api(&o,"POST","owner/players","{}").status==404&&s.api(&o,"GET","owner/policy").status==404);
    // A member's upload waits for the owner's review (public: members build, offers reviewed).
    auto sa=s.session(s.ticket(a)),sb=s.session(s.ticket(b),"Bob");
    auto made=s.w.call(sa,"createObject","{\"t\":\"l\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3}",Now);CHECK(made.status==200);auto obj=json::parse(made.body).get("id").string();
    auto found=[&]{return json::parse(s.w.call(sb,"findQpidObjects","{\"q\":226}",Now).body).get("n").get("oa").items().size();};
    CHECK(found()==0);
    const std::string memberA="{\"player\":\""+playerId(A)+"\",\"member\":\"yes\"}";
    CHECK(s.post(&a,"owner/member",memberA).status==403&&s.post(nullptr,"owner/member",memberA).status==401);
    CHECK(s.post(&o,"owner/member",memberA).status==200&&s.post(&o,"owner/member","{\"player\":\"x\",\"member\":\"maybe\"}").status==400);
    CHECK(s.post(&a,"offer",body(a,",\"all\":true")).status==200);
    auto waiting=json::parse(s.api(&o,"GET","owner/review").body).get("waiting").items();
    CHECK(waiting.size()==1&&waiting[0].get("id").string()==obj&&waiting[0].get("waiting_for").string()=="owner");
    CHECK(s.post(&a,"owner/review","{\"accept\":\""+obj+"\"}").status==403&&found()==0);
    CHECK(s.post(&o,"owner/review","{\"accept\":\""+obj+"\",\"reject\":\""+obj+"\"}").status==400&&s.post(&o,"owner/review","{}").status==400);
    auto r=s.post(&o,"owner/review","{\"accept\":\""+obj+"\"}");CHECK(r.status==200&&json::parse(r.body).get("accepted").integer()==1&&found()==1);
    // The owner's list: each player's key in its short form, his counts and the likes on what is his.
    s.w.call(sb,"sendLike","{\"oid\":\""+obj+"\",\"lp\":3,\"lp2\":0,\"lt\":\"\"}",Now);
    bool listed=false;auto players=json::parse(s.api(&o,"GET","owner/players").body);
    for(auto& p:players.get("players").items())
        if(p.get("id").string()==playerId(A))listed=p.get("key").string()==id::shortText(a.pub)&&p.get("likes").integer()==3&&p.get("shared").integer()==1&&p.get("member").boolean();
    CHECK(listed);
    // The policy, as the owner sets it; a value that is none changes nothing.
    auto before=s.w.policy();
    CHECK(s.post(&a,"owner/policy","{\"set\":[\"limit=5\"]}").status==403&&s.post(&o,"owner/policy","{\"set\":[\"limit=-1\"]}").status==400&&s.post(&o,"owner/policy","{}").status==400);
    CHECK(policyItems(s.w.policy())==policyItems(before));
    r=s.post(&o,"owner/policy","{\"set\":[\"limit=5\",\"identity=keys\"]}");CHECK(r.status==200&&s.w.policy().limit==5&&s.w.policy().preset=="public");
    r=s.post(&o,"owner/policy","{\"preset\":\"friends\"}");CHECK(r.status==200&&s.w.policy().preset=="friends"&&s.w.policy().identity=="none");
    r=s.post(&o,"owner/policy","{\"preset\":\"public\"}");CHECK(r.status==200&&s.w.policy().identity=="keys");
    // Removing a player: the owner's only.
    CHECK(s.post(&b,"owner/remove-player","{\"player\":\""+playerId(A)+"\"}").status==403&&found()==1);
    r=s.post(&o,"owner/remove-player","{\"player\":\""+playerId(A)+"\"}");CHECK(r.status==200&&json::parse(r.body).get("removed").integer()==1&&found()==0);
}
static void saved(){
    Server s;auto o=id::makeKeys(),a=id::makeKeys();s.w.setOwner(id::account(o.pub));
    auto t=s.ticket(a),r=s.token(a);
    auto snap=s.w.snapshot();CHECK(snap.get("owner").string()==id::account(o.pub)&&snap.get("tickets").items().size()==1&&snap.get("relay_tokens").items().size()==1);
    Server back("friends");back.w.restore(json::parse(snap.dump()));
    CHECK(back.w.owner()==id::account(o.pub)&&back.w.policy().identity=="keys"&&back.signIn(t).status==200&&back.w.relayAdmit(id::relayId(a.pub),r,"group",Now));
    // A world file from before identity keys has none, whatever its preset.
    auto old=json::parse(snap.dump());auto text=old.dump();
    auto at=text.find(",\"identity\":\"keys\"");CHECK(at!=std::string::npos);text.erase(at,std::string(",\"identity\":\"keys\"").size());
    Server older("friends");older.w.restore(json::parse(text));CHECK(older.w.policy().preset=="public"&&older.w.policy().identity=="none");
    // Refused: an owner that is no key, a ticket of nobody, one listed twice.
    for(auto change:{std::make_pair(std::string("\"owner\":\"")+id::account(o.pub)+"\"",std::string("\"owner\":\"ann\"")),
                     std::make_pair(std::string("\"account\":\"")+id::account(a.pub)+"\"",std::string("\"account\":\"")+id::account(o.pub)+"\"")}){
        auto bad=snap.dump();auto p=bad.find(change.first);CHECK(p!=std::string::npos);bad.replace(p,change.first.size(),change.second);
        Server x("friends");bool refused=false;try{x.w.restore(json::parse(bad));}catch(const std::exception&){refused=true;}CHECK(refused);}
    CHECK([&]{try{s.w.setOwner("ann");}catch(const std::exception&){return s.w.owner()==id::account(o.pub);}return false;}());
}
// The friend list and the choice of who he sees: only his own key sets or reads them; a short key names a player of this
// world; the relay takes them for his relay id (made from his key) and his friends by theirs, with the policy's cap.
static void friends(){
    Server s;auto a=id::makeKeys(),b=id::makeKeys(),c=id::makeKeys(),d=id::makeKeys();const std::string A=id::account(a.pub),B=id::account(b.pub);
    auto ta=s.token(a),tb=s.token(b),tc=s.token(c),td=s.token(d);
    const std::string add=body(a,",\"add\":[\""+id::shortText(b.pub)+"\"],\"show_others\":\"friends\"");
    auto before=s.w.snapshot().dump();
    CHECK(s.post(nullptr,"friends",add).status==401&&s.post(&b,"friends",add).status==403&&s.w.snapshot().dump()==before);
    CHECK(s.api(nullptr,"GET","friends?player="+A).status==401&&s.api(&b,"GET","friends?player="+A).status==403);
    auto r=s.post(&a,"friends",add);CHECK(r.status==200);
    auto f=json::parse(r.body);CHECK(f.get("friends").items().size()==1&&f.get("friends").items()[0].get("key").string()==B&&f.get("show_others").string()=="friends");
    CHECK(json::parse(s.api(&a,"GET","friends?player="+A).body).get("friends").items()[0].get("short").string()==id::shortText(b.pub));
    // The world's view for the relay: his choice and his friend's relay id, the policy's cap; nothing for an id nobody has.
    auto v=s.w.relayViewer(id::relayId(a.pub));CHECK(v.show=="friends"&&v.friends==std::set<uint64_t>{id::relayId(b.pub)}&&v.cap==16);
    CHECK(s.w.relayViewer(id::relayId(c.pub)).show.empty()&&s.w.relayViewer(12345).friends.empty());
    {Server f2("friends");CHECK(f2.post(nullptr,"friends","{\"player\":\"ann\",\"add\":[\"bea\"],\"show_others\":\"nobody\"}").status==200);
     auto none=f2.w.relayViewer(id::relayId(a.pub));CHECK(none.show.empty()&&none.friends.empty()&&none.cap==32);}
    // Through a relay as the server runs it: four broadcast players in one place, Ann sees Bea only.
    int64_t now=Now;RelayConfig config;config.token="group";config.admit=[&](uint64_t i,const std::string& t){return s.w.relayAdmit(i,t,"group",now);};
    config.viewer=[&](uint64_t i){return relayViewer(s.w,i,false);};
    Relay relay(config,[](const RelayAddress&,const uint8_t*,size_t){});
    uint32_t sequence=0;uint64_t ms=1000;
    auto hello=[&](const id::Keys& k,const std::string& token,uint32_t at){
        ds::broadcast::Hello h;h.base.id=id::relayId(k.pub);h.base.token=token;h.base.nickname="P";h.base.sequence=++sequence;h.base.stream=0;
        h.base.havePosition=true;h.base.position={double(at),0,0};h.t0=ms;auto b2=ds::broadcast::encode(h);relay.receive({0x0a000001u+at,uint16_t(40000+at)},b2.data(),b2.size(),ms);};
    auto round=[&]{ms+=500;hello(a,ta,0);hello(b,tb,1);hello(c,tc,2);hello(d,td,3);};
    round();round();
    const uint64_t ia=id::relayId(a.pub),ib=id::relayId(b.pub),ic=id::relayId(c.pub),idd=id::relayId(d.pub);
    CHECK(relay.areNear(ia,ic)&&relay.shows(ia,ib)&&!relay.shows(ia,ic)&&!relay.shows(ia,idd)&&relay.shows(ic,ia)&&relay.shown(ic)==3);
    // Everyone, on a server whose policy shows one at most: the nearest (Bea, 1 m).
    CHECK(s.post(&a,"friends",body(a,",\"show_others\":\"everyone\",\"remove\":[\""+B+"\"]")).status==200);
    {Policy p=s.w.policy();setPolicy(p,"near_cap=1");s.w.setPolicy(p);}
    round();CHECK(relay.shown(ia)==1&&relay.shows(ia,ib)&&relay.shown(ic)==1);
    // Kept with the world.
    World back([]{return std::string("x");});back.restore(s.w.snapshot());CHECK(back.apiFriends(A).get("show_others").string()=="everyone"&&back.apiFriends(A).get("friends").items().empty());
}
int main(){
    signIns();calls();none();relay();owner();saved();friends();
    if(failures)return 1;
    std::puts("identity keys tests passed");return 0;
}
