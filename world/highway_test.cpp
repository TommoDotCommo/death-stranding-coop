// Roads paved together (README "Roads paved together"): what the players put into the game's auto-pavers
// (devoteHighwayResources) adds up per paver and resource, and every player is answered with what the others put in
// and his own (getHighwayResources), only for the pavers and resources he asks for; the policy decides whose
// contributions the others get, as it decides whose uploads they get; likes for contributions (sendHighwayLikes) reach
// only a player whose contributions were shared; a body in the wrong form changes nothing, a player's pairs of paver and
// resource are bounded, a withdrawal and the owner's removal take contributions back, and the world file keeps them.
#include "world.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"highway oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static bool throws(const std::function<void()>& f){try{f();}catch(const std::exception&){return true;}return false;}
static unsigned counter=0;
// The game's construction ids of two pavers, and the resources roads take (chiral crystals, metals, ceramics).
constexpr int64_t P1=2031,P2=2032;constexpr int Crystals=0,Metals=2,Ceramics=3;
struct Server {
    World w{[]{return "tok"+std::to_string(++counter);}};
    explicit Server(const char* preset="friends",std::vector<const char*> settings={}){Policy p=presetPolicy(preset);p.identity="none";for(auto s:settings)setPolicy(p,s);w.setPolicy(p);}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    Reply devote(const std::string& token,const std::string& entries){return w.call(token,"devoteHighwayResources","{\"ph\":["+entries+"]}",100);}
    static std::string put(int64_t paver,int resource,int64_t n){return "{\"ci\":"+std::to_string(paver)+",\"ri\":"+std::to_string(resource)+",\"n\":"+std::to_string(n)+"}";}
    json::Value ask(const std::string& token,std::vector<int64_t> pavers={P1,P2},const std::string& rids="[0,2,3]"){
        std::string cds;for(auto p:pavers)cds+=(cds.empty()?"":",")+std::string("{\"cid\":")+std::to_string(p)+",\"lgd\":1700000000}";
        auto r=w.call(token,"getHighwayResources","{\"cds\":["+cds+"],\"rids\":"+rids+"}",200);CHECK(r.status==200);return r.status==200?json::parse(r.body):json::Value();}
    // One answer's materials as "paver_resource n un" lines, and its contributors as "paver: id likes, ...".
    static std::vector<std::string> put(const json::Value& a){std::vector<std::string> out;
        for(auto& e:a.get("pr").items()){CHECK(e.get("id").string()==std::to_string(e.get("cid").integer())+"_"+std::to_string(e.get("rid").integer()));
            out.push_back(e.get("id").string()+" "+std::to_string(e.get("n").integer())+" "+std::to_string(e.get("un").integer()));}
        return out;}
    static std::vector<std::string> who(const json::Value& a){std::vector<std::string> out;
        for(auto& c:a.get("cs").items()){std::string line=std::to_string(c.get("cid").integer())+":";
            for(auto& u:c.get("cs").items()){line+=" "+u.get("u").string()+" "+std::to_string(u.get("l").integer());}
            out.push_back(line);}
        return out;}
};
using Lines=std::vector<std::string>;
static const std::string Ann=playerId("ann"),Bob=playerId("bob"),Carl=playerId("carl"),Dan=playerId("dan");
static void addsUp(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),carl=s.signIn("carl","Carl");
    // Nothing put in yet: an empty answer in the game's shape.
    {auto a=s.ask(carl);CHECK(a.get("pr").isArray()&&a.get("pr").items().empty()&&a.get("cs").isArray()&&a.get("cs").items().empty()&&a.get("ul").isInt()&&a.get("ul").integer()==0);}
    auto r=s.devote(ann,Server::put(P1,Metals,60)+","+Server::put(P1,Metals,40)+","+Server::put(P1,Ceramics,50));
    CHECK(r.status==200&&json::parse(r.body).get("e").isInt()&&json::parse(r.body).get("e").integer()==0);
    CHECK(s.devote(bob,Server::put(P1,Metals,30)).status==200);
    // Carl gets what Ann and Bob put in, per paver and resource, and who put it in, the most first.
    CHECK((Server::put(s.ask(carl))==Lines{"2031_2 130 0","2031_3 50 0"}));
    CHECK((Server::who(s.ask(carl))==Lines{"2031: "+Ann+" 0 "+Bob+" 0"}));
    // Ann gets Bob's and her own apart: hers never in `n` (her game counts her own deliveries already).
    CHECK((Server::put(s.ask(ann))==Lines{"2031_2 30 100","2031_3 0 50"}));
    CHECK((Server::who(s.ask(ann))==Lines{"2031: "+Bob+" 0"}));
    // The total only grows while they share: the game adds what grew since its last answer.
    CHECK(s.devote(bob,Server::put(P1,Metals,20)+","+Server::put(P2,Crystals,7)).status==200);
    CHECK((Server::put(s.ask(carl))==Lines{"2031_2 150 0","2031_3 50 0","2032_0 7 0"}));
    CHECK((Server::who(s.ask(carl))==Lines{"2031: "+Ann+" 0 "+Bob+" 0","2032: "+Bob+" 0"}));
    // Only what he asks for: another paver, or metals alone.
    CHECK((Server::put(s.ask(carl,{P2}))==Lines{"2032_0 7 0"})&&(Server::who(s.ask(carl,{P2}))==Lines{"2032: "+Bob+" 0"}));
    CHECK((Server::put(s.ask(carl,{P1,P2},"[2]"))==Lines{"2031_2 150 0"}));
    CHECK(Server::put(s.ask(carl,{999},"[0,2,3]")).empty()&&Server::put(s.ask(carl,{P1},"[]")).empty()&&Server::put(s.ask(carl,{},"[2]")).empty());
    // A body in the wrong form changes nothing.
    auto before=s.w.snapshot().dump();
    for(const char* bad:{"{}","{\"ph\":{}}","{\"ph\":[1]}","{\"ph\":[{\"ci\":2031,\"ri\":2}]}","{\"ph\":[{\"ci\":\"2031\",\"ri\":2,\"n\":5}]}",
                         "{\"ph\":[{\"ci\":0,\"ri\":2,\"n\":5}]}","{\"ph\":[{\"ci\":1000000,\"ri\":2,\"n\":5}]}","{\"ph\":[{\"ci\":2031,\"ri\":6,\"n\":5}]}",
                         "{\"ph\":[{\"ci\":2031,\"ri\":-1,\"n\":5}]}","{\"ph\":[{\"ci\":2031,\"ri\":2,\"n\":0}]}","{\"ph\":[{\"ci\":2031,\"ri\":2,\"n\":-500}]}",
                         "{\"ph\":[{\"ci\":2031,\"ri\":2,\"n\":100001}]}","{\"ph\":[{\"ci\":2031,\"ri\":2,\"n\":5},{\"ci\":2031,\"ri\":2,\"n\":-5}]}",
                         "{\"ph\":[{\"ci\":2032,\"ri\":3,\"n\":9},{\"ci\":2031,\"ri\":9,\"n\":5}]}"})
        CHECK(s.w.call(ann,"devoteHighwayResources",bad,300).status==400);
    {std::string many;for(int i=0;i<65;++i)many+=(i?",":"")+Server::put(P1,Metals,1);CHECK(s.devote(ann,many).status==400);}
    CHECK(s.w.snapshot().dump()==before&&(Server::put(s.ask(carl))==Lines{"2031_2 150 0","2031_3 50 0","2032_0 7 0"}));
    for(const char* bad:{"{}","{\"cds\":[],\"rids\":5}","{\"cds\":[{\"cid\":0}],\"rids\":[2]}","{\"cds\":[{\"cid\":2031}],\"rids\":[6]}","{\"cds\":[{\"cid\":2031}],\"rids\":[0,1,2,3,4,5,0]}",
                         "{\"cds\":[{\"lgd\":5}],\"rids\":[2]}"})
        CHECK(s.w.call(carl,"getHighwayResources",bad,300).status==400);
    {std::string cds;for(int i=0;i<65;++i)cds+=(i?",":"")+std::string("{\"cid\":")+std::to_string(P1+i)+"}";CHECK(s.w.call(carl,"getHighwayResources","{\"cds\":["+cds+"],\"rids\":[2]}",300).status==400);}
    // An empty list is answered and changes nothing; without a session nothing is answered.
    CHECK(s.devote(carl,"").status==200&&s.w.snapshot().dump()==before);
    CHECK(s.w.call("nobody","getHighwayResources","{\"cds\":[{\"cid\":2031}],\"rids\":[2]}",300).status==401);
}
static void bounded(){
    // A player keeps at most MaxHighwayKeys pairs of paver and resource; what he has may still grow, up to a total.
    Server s;auto ann=s.signIn("ann","Ann"),carl=s.signIn("carl","Carl");
    for(size_t call=0;call<MaxHighwayKeys/MaxHighwayEntries;++call){std::string list;
        for(size_t i=0;i<MaxHighwayEntries;++i){size_t k=call*MaxHighwayEntries+i;list+=(i?",":"")+Server::put(int64_t(1000+k/HighwayResources),int(k%HighwayResources),1);}
        CHECK(s.devote(ann,list).status==200);}
    CHECK(s.devote(ann,Server::put(P2+5000,Metals,1)).status==403);
    CHECK(s.devote(ann,Server::put(1000,0,5)).status==200&&(Server::put(s.ask(carl,{1000},"[0]"))==Lines{"1000_0 6 0"}));
    std::string big;for(size_t i=0;i<MaxHighwayEntries;++i)big+=(i?",":"")+Server::put(1000,1,MaxHighwayAmount);
    for(int i=0;i<160;++i)CHECK(s.devote(ann,big).status==200);
    CHECK((Server::put(s.ask(carl,{1000},"[1]"))==Lines{"1000_1 "+std::to_string(MaxHighwayTotal)+" 0"}));
}
static void policy(){
    // Whose contributions the others get: exactly while an upload of his would be shared at once.
    auto answer=[](const char* preset,std::vector<const char*> settings,const char* annChoice,bool annMember){
        Server s(preset,settings);auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),carl=s.signIn("carl","Carl");
        if(annChoice)s.w.setPlayer(Ann,annChoice);
        if(annMember)s.w.setPlayer(Ann,"member=yes");
        s.w.setPlayer(Bob,"member=yes");
        CHECK(s.devote(ann,Server::put(P1,Metals,100)).status==200&&s.devote(bob,Server::put(P1,Metals,1)).status==200);
        auto own=Server::put(s.ask(ann,{P1},"[2]"));CHECK(own.size()==1&&own[0].substr(own[0].rfind(' '))==" 100");   // his own, whatever the policy
        return Server::put(s.ask(carl,{P1},"[2]"));};
    const Lines both{"2031_2 101 0"},bobs{"2031_2 1 0"},none{};
    CHECK(answer("friends",{},nullptr,false)==both);
    CHECK(answer("friends",{},"offer_builds=no",false)==bobs);              // kept for himself
    CHECK(answer("friends",{},"offer_builds=ask",false)==bobs);             // nobody can ask him about a delivery
    CHECK((answer("friends",{"offer_builds=no"},"offer_builds=yes",false)==Lines{"2031_2 100 0"}));   // his yes over the server's no (Bob chose nothing)
    CHECK(answer("friends",{"accept_offers=review"},nullptr,false)==none);  // nothing is reviewed: nobody's
    CHECK(answer("friends",{"accept_offers=never"},nullptr,false)==none);
    CHECK(answer("friends",{"build=nobody"},nullptr,false)==none);
    CHECK(answer("friends",{"build=members"},nullptr,false)==bobs);         // held: he is no member
    CHECK(answer("friends",{"build=members"},nullptr,true)==both);
    // A change applies to what he put in before too: his no hides it, his yes brings it back.
    Server s;auto ann=s.signIn("ann","Ann"),carl=s.signIn("carl","Carl");
    CHECK(s.devote(ann,Server::put(P1,Metals,100)).status==200);
    s.w.setPlayer(Ann,"offer_builds=no");CHECK(Server::put(s.ask(carl)).empty()&&Server::who(s.ask(carl)).empty());
    s.w.setPlayer(Ann,"offer_builds=default");CHECK((Server::put(s.ask(carl))==Lines{"2031_2 100 0"}));
}
static void likes(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),carl=s.signIn("carl","Carl"),dan=s.signIn("dan","Dan");
    CHECK(s.devote(ann,Server::put(P1,Metals,100)).status==200&&s.devote(bob,Server::put(P1,Metals,10)).status==200&&s.devote(carl,Server::put(P2,Metals,10)).status==200);
    auto give=[&](const std::string& from,const std::string& list){return s.w.call(from,"sendHighwayLikes","{\"hls\":["+list+"]}",400).status;};
    auto like=[](const std::string& to,int64_t n){return "{\"u\":\""+to+"\",\"l\":"+std::to_string(n)+"}";};
    // To Ann and Bob, who put something in and share it: counted. To himself, to Dan (nothing put in), to nobody: dropped.
    CHECK(give(carl,like(Ann,3)+","+like(Bob,1)+","+like(Carl,5)+","+like(Dan,4)+","+like("zygo_nobody",2))==200);
    CHECK((Server::who(s.ask(dan))==Lines{"2031: "+Ann+" 3 "+Bob+" 1","2032: "+Carl+" 0"}));
    CHECK(s.ask(ann).get("ul").integer()==3&&s.ask(carl).get("ul").integer()==0&&s.ask(dan).get("ul").integer()==0);
    // Not to a player whose contributions are not shared.
    s.w.setPlayer(Bob,"offer_builds=no");CHECK(give(carl,like(Bob,4))==200);s.w.setPlayer(Bob,"offer_builds=default");
    CHECK(s.ask(bob).get("ul").integer()==1);
    // A body in the wrong form changes nothing.
    auto before=s.w.snapshot().dump();
    for(const char* bad:{"{}","{\"hls\":{}}","{\"hls\":[{\"u\":5,\"l\":1}]}","{\"hls\":[{\"u\":\"x\",\"l\":-1}]}","{\"hls\":[{\"u\":\"x\",\"l\":100001}]}","{\"hls\":[{\"u\":\"x\"}]}"})
        CHECK(s.w.call(dan,"sendHighwayLikes",bad,500).status==400);
    CHECK(give(dan,like(Ann,1)+",{\"u\":\""+Ann+"\",\"l\":-4}")==400&&s.w.snapshot().dump()==before);
}
static void takenBack(){
    for(const char* withdrawal:{"withdrawal=go","withdrawal=stay"}){
        Server s("friends",{withdrawal});auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),carl=s.signIn("carl","Carl");
        CHECK(s.devote(ann,Server::put(P1,Metals,100)).status==200&&s.devote(bob,Server::put(P1,Metals,10)).status==200);
        CHECK(s.w.call(carl,"sendHighwayLikes","{\"hls\":[{\"u\":\""+Ann+"\",\"l\":2}]}",300).status==200);
        // One object withdrawn leaves his paver contributions; everything withdrawn takes them back, likes too.
        auto ladder=json::parse(s.w.call(ann,"createObject","{\"t\":\"l\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3}",300).body).get("id").string();
        s.w.withdraw(Ann,ladder);CHECK((Server::put(s.ask(carl))==Lines{"2031_2 110 0"}));
        s.w.withdraw(Ann,"");
        CHECK((Server::put(s.ask(carl))==Lines{"2031_2 10 0"})&&(Server::who(s.ask(carl))==Lines{"2031: "+Bob+" 0"}));
        CHECK((Server::put(s.ask(ann))==Lines{"2031_2 10 0"})&&s.ask(ann).get("ul").integer()==0);
        // The owner's removal of Bob takes his.
        CHECK(s.w.removePlayer(Bob)==0&&Server::put(s.ask(carl)).empty());
        CHECK(!s.w.snapshot().has("highway")&&!s.w.snapshot().has("highway_likes"));}
}
static void saved(){
    Server s;auto ann=s.signIn("ann","Ann"),bob=s.signIn("bob","Bob"),carl=s.signIn("carl","Carl");
    CHECK(!s.w.snapshot().has("highway"));                       // a world without contributions is written as before
    CHECK(s.devote(ann,Server::put(P1,Metals,100)+","+Server::put(P2,Ceramics,8)).status==200&&s.devote(bob,Server::put(P1,Crystals,3)).status==200);
    CHECK(s.w.call(carl,"sendHighwayLikes","{\"hls\":[{\"u\":\""+Ann+"\",\"l\":2}]}",300).status==200);
    auto doc=s.w.snapshot();CHECK(doc.get("highway").items().size()==3&&doc.get("highway_likes").items().size()==1);
    World r([]{return std::string("fresh");});r.restore(json::parse(doc.dump()));CHECK(r.snapshot().dump()==doc.dump());
    auto here=[&](World& w){return Server::put(json::parse(w.call(carl,"getHighwayResources","{\"cds\":[{\"cid\":2031},{\"cid\":2032}],\"rids\":[0,2,3]}",1).body));};
    CHECK(here(r)==here(s.w)&&(here(r)==Lines{"2031_0 3 0","2031_2 100 0","2032_3 8 0"}));
    CHECK(json::parse(r.call(ann,"getHighwayResources","{\"cds\":[{\"cid\":2031}],\"rids\":[2]}",1).body).get("ul").integer()==2);
    // A world file from before has none and loads; files that are wrong are refused.
    {auto old=json::parse(doc.dump());old.erase("highway");old.erase("highway_likes");World o([]{return std::string("fresh");});CHECK(!throws([&]{o.restore(old);}));CHECK(here(o).empty());}
    auto broken=[&](const char* key,const std::string& value){auto d=json::parse(doc.dump());d.set(key,json::parse(value));World o([]{return std::string("fresh");});return throws([&]{o.restore(d);});};
    auto entry=[](const std::string& player,const char* cid,const char* rid,const char* n){return "{\"player\":\""+player+"\",\"cid\":"+cid+",\"rid\":"+rid+",\"n\":"+n+"}";};
    CHECK(!broken("highway","["+entry(Ann,"5","1","9")+"]")&&!broken("highway","[]")&&!broken("highway_likes","[]"));
    for(const std::string& bad:{std::string("{}"),"["+entry("zygo_nobody","5","1","9")+"]","["+entry("","5","1","9")+"]","["+entry(Ann,"0","1","9")+"]","["+entry(Ann,"5","6","9")+"]",
                                "["+entry(Ann,"5","1","0")+"]","["+entry(Ann,"5","1","1000000001")+"]","["+entry(Ann,"\"5\"","1","9")+"]","["+entry(Ann,"5","1","9")+","+entry(Ann,"5","1","4")+"]"})
        CHECK(broken("highway",bad));
    for(const std::string& bad:{std::string("{}"),std::string("[{\"player\":\"zygo_nobody\",\"likes\":2}]"),"[{\"player\":\""+Ann+"\",\"likes\":0}]",
                                "[{\"player\":\""+Ann+"\",\"likes\":2},{\"player\":\""+Ann+"\",\"likes\":3}]"})
        CHECK(broken("highway_likes",bad));
    {std::string many="[";for(size_t i=0;i<=MaxHighwayKeys;++i)many+=(i?",":"")+entry(Ann,std::to_string(1+i/6).c_str(),std::to_string(i%6).c_str(),"1");CHECK(broken("highway",many+"]"));}
}
static void kinds(){
    // The kinds read with the paver's calls (mod/game_offsets.md, "Roads, signs and lost cargo through the server"): a
    // sign is `t`, as zipline `z` and catapult `a`, by name in filters and in the launcher's lists.
    CHECK(typeName("t")=="sign"&&typeName("z")=="zipline"&&typeName("a")=="catapult"&&typeName("q")=="q");
    CHECK((parseFilter("types=sign,zipline,catapult").types==std::set<std::string>{"t","z","a"}));
}
int main(){
    for(auto part:{addsUp,bounded,policy,likes,takenBack,saved,kinds}){
        try{part();}catch(const std::exception& e){std::fprintf(stderr,"highway oracle: threw %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("highway tests passed");return 0;
}
