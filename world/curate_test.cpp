// Which objects a game is handed (world.h `curate`, README "Which objects a game is handed"): the asked number per kind
// exactly, never the asker's own nor what is kept, held or waits, newest first or friends, likes and then newest, a cap
// per builder, objects too close to one already chosen of their kind left out, and the same objects as last time while
// they are there; then the same through the server's calls, kept in the saved world and bounded per player.
#include "world.h"
#include <cmath>
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"curate oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static bool throws(const std::function<void()>& f){try{f();}catch(const std::exception&){return true;}return false;}
static const std::string Me="zygo_me",Ann="zygo_ann",Bob="zygo_bob";
// One object: kind, builder, time, likes, and a place in metres (x along a line) unless `placed` is false.
static Candidate object(const std::string& id,const char* type,const std::string& creator,int64_t updated,int64_t likes=0,double x=0,double z=0,bool placed=true){
    Candidate c;c.id=id;c.type=type;c.creator=creator;c.area=226;c.updated=updated;c.likes=likes;c.placed=placed;c.x=x;c.y=10;c.z=z;return c;}
static Policy policy(const char* preset,std::vector<const char*> settings={}){Policy p=presetPolicy(preset);for(auto s:settings)setPolicy(p,s);return p;}
static std::vector<std::string> pick(const std::vector<Candidate>& all,std::map<std::string,int64_t> counts,const Policy& p,const std::vector<std::string>& before={},const std::set<std::string>& friends={}){
    return curate(Me,226,&counts,all,before,p,friends).handed;}
static bool has(const std::vector<std::string>& list,const std::string& id){for(auto& x:list)if(x==id)return true;return false;}
static std::string id(char kind,int n){char b[16];std::snprintf(b,sizeof b,"%c%012d",kind,n);return b;}

static void whoAndHowMany(){
    // Only other players' shared objects of the area; at most the asked number of each kind, and no kind not asked.
    std::vector<Candidate> all;
    for(int i=0;i<10;++i)all.push_back(object(id('e',i),"e",Ann,100+i,0,i*20.0));
    all.push_back(object(id('l',1),"l",Ann,50,0,0,40));
    all.push_back(object(id('e',90),"e",Me,500,0,0,80));                                   // the asker's own
    {Candidate kept=object(id('e',91),"e",Bob,600,0,0,120);kept.shared=false;all.push_back(kept);}   // kept, held or waiting
    {Candidate there=object(id('e',92),"e",Bob,700,0,0,160);there.area=227;all.push_back(there);}  // another area
    auto p=policy("friends");
    for(int64_t n:{0,1,3,10}){auto got=pick(all,{{"e",n}},p);CHECK(int64_t(got.size())==n);for(auto& g:got)CHECK(g[0]=='e');}
    CHECK(pick(all,{{"e",20}},p).size()==10&&pick(all,{{"b",5}},p).empty()&&pick(all,{{"e",-3},{"l",2}},p)==std::vector<std::string>{id('l',1)});
    CHECK(pick(all,{{"e",3},{"l",1}},p).size()==4);
    for(auto& got:{pick(all,{{"e",20},{"l",9}},p),curate(Me,226,nullptr,all,{},p).handed}){
        CHECK(got.size()==11&&!has(got,id('e',90))&&!has(got,id('e',91))&&!has(got,id('e',92)));}
    // Another player is handed the asker's object, and a shared one of the other area when he asks there.
    CHECK(has(curate(Bob,226,nullptr,all,{},p).handed,id('e',90))&&curate(Me,227,nullptr,all,{},p).handed==std::vector<std::string>{id('e',92)});
}
static void order(){
    // friends: newest first (the time an object was last changed; the same time: by id).
    std::vector<Candidate> all;
    for(int i=0;i<6;++i)all.push_back(object(id('e',i),"e",i%2?Ann:Bob,i==4?100:200+i,10-i,i*20.0));
    CHECK((pick(all,{{"e",3}},policy("friends"))==std::vector<std::string>{id('e',5),id('e',3),id('e',2)}));
    // liked (public): the most liked first, then the newest; a friend's object before everything else.
    auto liked=policy("public",{"per_builder=0"});
    CHECK((pick(all,{{"e",3}},liked)==std::vector<std::string>{id('e',0),id('e',1),id('e',2)}));
    all[5].likes=50;CHECK((pick(all,{{"e",2}},liked)==std::vector<std::string>{id('e',5),id('e',0)}));
    all[1].likes=all[0].likes;CHECK(pick(all,{{"e",3}},liked)[1]==id('e',1));                 // the same likes: the newer
    CHECK((pick(all,{{"e",2}},liked,{},{Ann})==std::vector<std::string>{id('e',5),id('e',1)}));   // Ann's first
    CHECK((pick(all,{{"e",2}},policy("friends"),{},{Ann})==std::vector<std::string>{id('e',5),id('e',3)}));   // newest: no friends first
}
static void builders(){
    // A cap per builder and kind: one builder does not fill an area while others have built there.
    std::vector<Candidate> all;
    for(int i=0;i<10;++i)all.push_back(object(id('e',i),"e",Ann,300+i,0,i*20.0));
    all.push_back(object(id('e',20),"e",Bob,100,0,0,50));all.push_back(object(id('e',21),"e",Bob,101,0,20,50));
    for(int i=0;i<5;++i)all.push_back(object(id('l',i),"l",Ann,300+i,0,i*20.0,90));
    auto pub=policy("public");CHECK(pub.perBuilder==3);
    auto got=pick(all,{{"e",8},{"l",8}},pub);size_t ann=0,bob=0,ladders=0;
    for(auto& g:got){if(g[0]=='l')++ladders;else if(g==id('e',20)||g==id('e',21))++bob;else ++ann;}
    CHECK(ann==3&&bob==2&&ladders==3&&got.size()==8);
    CHECK(pick(all,{{"e",8}},policy("public",{"per_builder=0"})).size()==8&&pick(all,{{"e",8}},policy("friends")).size()==8);
    CHECK(!has(pick(all,{{"e",8}},policy("friends")),id('e',20)));                         // newest first, no cap: Ann's
}
static void spread(){
    // What was measured live: the game asks for 5 cairns; of cairns on a 4 m grid it placed 1 in 5, 20 m apart 4 in 5.
    // 40 cairns on a 4 m grid (28 x 16 m): two are handed, 20 m apart at least; three on a 4 m grid: one.
    std::vector<Candidate> all;
    for(int i=0;i<40;++i)all.push_back(object(id('e',i),"e",Ann,1000-i,0,(i%8)*4.0,(i/8)*4.0));
    auto p=policy("friends");CHECK(p.spread==20);
    auto got=pick(all,{{"e",5}},p);CHECK(got.size()==2);
    std::map<std::string,const Candidate*> at;for(auto& c:all)at[c.id]=&c;
    double least=1e9;for(auto& a:got)for(auto& b:got)if(a!=b)least=std::min(least,std::hypot(at[a]->x-at[b]->x,at[a]->z-at[b]->z));
    CHECK(least>=20);
    CHECK(pick(all,{{"e",5}},policy("friends",{"spread=0"}))==(std::vector<std::string>{id('e',0),id('e',1),id('e',2),id('e',3),id('e',4)}));
    CHECK(pick(all,{{"e",40}},p).size()==2&&pick(all,{{"e",40}},policy("friends",{"spread=8"})).size()==12);   // 12 fit 8 m apart
    {std::vector<Candidate> three(all.begin(),all.begin()+3);CHECK(pick(three,{{"e",5}},p).size()==1);
     std::vector<Candidate> apart;for(int i=0;i<5;++i)apart.push_back(object(id('e',i),"e",Ann,100+i,0,i*20.0));CHECK(pick(apart,{{"e",5}},p).size()==5);}
    // Exactly the spread apart is far enough; another kind beside it does not count; a place unknown is never too close.
    std::vector<Candidate> few{object(id('e',1),"e",Ann,9,0,0),object(id('e',2),"e",Ann,8,0,20),object(id('e',3),"e",Ann,7,0,19.9),
                               object(id('l',1),"l",Ann,9,0,0.5),object(id('e',4),"e",Ann,6,0,0,0,false),object(id('e',5),"e",Ann,5,0,0,0,false)};
    CHECK((pick(few,{{"e",9},{"l",9}},p)==std::vector<std::string>{id('e',1),id('l',1),id('e',2),id('e',4),id('e',5)}));
}
static void stable(){
    // What was handed before comes back first while it is there and shared, even when newer or more liked ones came.
    std::vector<Candidate> all;
    for(int i=0;i<6;++i)all.push_back(object(id('e',i),"e",i<3?Ann:Bob,100+i,0,i*20.0));
    all.push_back(object(id('l',1),"l",Ann,100,0,0,60));
    auto p=policy("friends");std::map<std::string,int64_t> two{{"e",2},{"l",1}},one{{"e",1}};
    auto first=curate(Me,226,&two,all,{},p);
    CHECK((first.handed==std::vector<std::string>{id('e',5),id('e',4),id('l',1)})&&first.remember==first.handed);
    all.push_back(object(id('e',9),"e",Ann,900,0,0,200));all[4].likes=all[5].likes=0;all[0].likes=99;
    CHECK((pick(all,{{"e",2},{"l",1}},p,first.remember)==std::vector<std::string>{id('e',5),id('e',4),id('l',1)}));
    CHECK((pick(all,{{"e",2}},policy("public"),first.remember)==std::vector<std::string>{id('e',5),id('e',4)}));
    // One of them went (removed, or no longer shared): the other stays, the place is filled as the policy says.
    auto gone=all;gone.erase(gone.begin()+5);gone[3].shared=false;
    CHECK((pick(gone,{{"e",2}},p,first.remember)==std::vector<std::string>{id('e',4),id('e',9)}));
    // Fewer asked, or a kind not asked this time: the rest is still remembered and comes back when asked again.
    auto fewer=curate(Me,226,&one,all,first.remember,p);
    CHECK((fewer.handed==std::vector<std::string>{id('e',5)})&&(fewer.remember==std::vector<std::string>{id('e',5),id('e',4),id('l',1)}));
    CHECK((pick(all,{{"e",2},{"l",1}},p,fewer.remember)==std::vector<std::string>{id('e',5),id('e',4),id('l',1)}));
    // Not handed twice, never more than asked, an id that is not there is dropped, and what is remembered is bounded.
    CHECK((pick(all,{{"e",1}},p,{id('e',0),id('e',0),id('x',5)})==std::vector<std::string>{id('e',0)}));
    {std::vector<Candidate> many;std::vector<std::string> before;
     for(int i=0;i<int(MaxPicksPerArea)+50;++i){many.push_back(object(id('e',i),"e",Ann,i,0,i*20.0));before.push_back(id('e',i));}
     auto r=curate(Me,226,nullptr,many,before,p);CHECK(r.handed.size()==MaxPicksPerArea+50&&r.remember.size()==MaxPicksPerArea&&r.remember[0]==id('e',0));}
}
// The same through the server: the game's call, the saved world, a world file from before, the bound per player.
static unsigned counter=0;
struct Server {
    World w{[]{return "tok"+std::to_string(++counter);}};
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& token,const char* type,int64_t x,int64_t now,int area=226){
        auto r=w.call(token,"createObject",std::string("{\"t\":\"")+type+"\",\"q\":"+std::to_string(area)+",\"px\":"+std::to_string(x)+",\"py\":0,\"pz\":0,\"exp\":2}",now);
        CHECK(r.status==200);return json::parse(r.body).get("id").string();}
    std::vector<std::string> found(World& in,const std::string& token,const char* counts,int area=226,int64_t now=1000){
        std::vector<std::string> out;auto answer=json::parse(in.call(token,"findQpidObjects","{\"q\":"+std::to_string(area)+",\"o\":{\"ct\":"+counts+"}}",now).body);
        for(auto& o:answer.get("n").get("oa").items())out.push_back(o.get("id").string());
        return out;}
    std::vector<std::string> found(const std::string& token,const char* counts,int area=226,int64_t now=1000){return found(w,token,counts,area,now);}
};
static void server(){
    Server s;auto ann=s.signIn("steam-a","Ann"),bob=s.signIn("steam-b","Bob");
    auto a1=s.make(ann,"e",0,100),a2=s.make(ann,"e",5000,101),a3=s.make(ann,"e",10000,102);   // 50 m apart
    auto mine=s.make(bob,"e",15000,103);
    auto first=s.found(bob,"{\"e\":2}");CHECK((first==std::vector<std::string>{a3,a2}));
    CHECK((s.found(ann,"{\"e\":5}")==std::vector<std::string>{mine}));
    auto a4=s.make(ann,"e",20000,200),a5=s.make(ann,"e",5100,201);                          // newer: far away, and 1 m from a2
    CHECK(s.found(bob,"{\"e\":2}")==first);                                                  // the same two again
    CHECK((s.found(bob,"{\"e\":3}")==std::vector<std::string>{a3,a2,a4}));                   // the newest is too close to a2
    CHECK(s.w.call(ann,"deleteObject","{\"id\":\""+a2+"\"}",300).status==200);
    auto after=s.found(bob,"{\"e\":3}");CHECK((after==std::vector<std::string>{a3,a4,a5}));
    // Saved with the world, and the same answer after a restore; a world file from before has none and loads.
    auto doc=s.w.snapshot();CHECK(doc.get("picks").items().size()==2);
    {World r([]{return std::string("fresh");});r.restore(json::parse(doc.dump()));CHECK(r.snapshot().dump()==doc.dump());
     CHECK(r.call(ann,"createObject","{\"t\":\"e\",\"q\":226,\"px\":30000,\"py\":0,\"pz\":0}",400).status==200);   // the newest, far away
     CHECK(s.found(r,bob,"{\"e\":3}")==after);}
    {auto old=json::parse(doc.dump());old.erase("picks");World o([]{return std::string("fresh");});CHECK(!throws([&]{o.restore(old);}));CHECK(o.snapshot().get("picks").items().empty());}
    auto broken=[&](const char* picks){auto d=json::parse(doc.dump());d.set("picks",json::parse(picks));World o([]{return std::string("fresh");});return throws([&]{o.restore(d);});};
    const std::string bobId=playerId("steam-b");
    CHECK(!broken("[]"));
    CHECK(!broken(("[{\"player\":\""+bobId+"\",\"area\":5,\"asked\":1,\"ids\":[\"e000000000001\"]}]").c_str()));
    for(std::string bad:{std::string("[{\"player\":\"zygo_nobody\",\"area\":5,\"asked\":1,\"ids\":[\"e000000000001\"]}]"),"[{\"player\":\""+bobId+"\",\"area\":\"5\",\"asked\":1,\"ids\":[\"e1\"]}]",
                         "[{\"player\":\""+bobId+"\",\"area\":5,\"asked\":1,\"ids\":[]}]","[{\"player\":\""+bobId+"\",\"area\":5,\"asked\":1,\"ids\":[\"e/1\"]}]",
                         "[{\"player\":\""+bobId+"\",\"area\":5,\"asked\":1,\"ids\":[\"ea\"]},{\"player\":\""+bobId+"\",\"area\":5,\"asked\":2,\"ids\":[\"eb\"]}]"})CHECK(broken(bad.c_str()));
    // Bounded: a player's picks are kept for at most MaxPickAreas areas, the ones he asked for longest ago go first.
    Server b;auto x=b.signIn("steam-a","Ann"),y=b.signIn("steam-b","Bob");
    for(int area=1;area<=int(MaxPickAreas)+20;++area){b.make(x,"e",0,area,area);CHECK(b.found(y,"{\"e\":1}",area,area).size()==1);}
    auto saved=b.w.snapshot();auto kept=saved.get("picks").items();CHECK(kept.size()==MaxPickAreas);
    int64_t least=1<<30;for(auto& k:kept)least=std::min(least,k.get("area").integer());CHECK(least==21);
    // An answer with nothing in it remembers nothing.
    CHECK(b.found(x,"{\"e\":1}",1,5000).empty()&&b.w.snapshot().get("picks").items().size()==MaxPickAreas);
}
int main(){
    for(auto part:{whoAndHowMany,order,builders,spread,stable,server}){
        try{part();}catch(const std::exception& e){std::fprintf(stderr,"curate oracle: threw %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("curate tests passed");return 0;
}
