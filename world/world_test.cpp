// The world server's core (world.h): sign-in, sessions, objects shared between players but never echoed to their
// builder, owner-only changes, caps, and the saved document.
#include "world.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"world oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static std::string tokenOf(const Reply& r){return json::parse(r.body).get("session").get("token").string();}
int main(){
    // JSON: 64-bit integers survive, order is kept, bad input throws.
    auto v=json::parse("{\"lgd\":63808112823144242,\"a\":[1,-2,3.5,true,null],\"s\":\"x\\\"y\\u0041\"}");
    CHECK(v.get("lgd").integer()==63808112823144242ll);CHECK(v.get("a").items().size()==5);CHECK(v.get("s").string()=="x\"yA");
    CHECK(v.dump()=="{\"lgd\":63808112823144242,\"a\":[1,-2,3.5,true,null],\"s\":\"x\\\"yA\"}");
    for(const char* bad:{"{","[1,]","{\"a\":1}x","\"\\q\"","{\"a\" 1}","nul",""}){bool threw=false;try{json::parse(bad);}catch(const std::exception&){threw=true;}CHECK(threw);}
    {std::string deep(100,'[');bool threw=false;try{json::parse(deep);}catch(const std::exception&){threw=true;}CHECK(threw);}
    // The game's request envelope.
    CHECK(unwrap("{\"data\":\"{\"rn\":4, \"ct\":17}\"}")=="{\"rn\":4, \"ct\":17}");CHECK(unwrap("{\"q\":1}")=="{\"q\":1}");CHECK(unwrap("")=="");
    unsigned n=0;World w([&n]{return "token"+std::to_string(++n);});
    CHECK(playerId("a").size()==32&&playerId("a").compare(0,5,"zygo_")==0&&playerId("a")==playerId("a")&&playerId("a")!=playerId("b"));
    auto a=w.signIn("steam-a","Ann","http://127.0.0.1:1/g");CHECK(a.status==200);
    auto ja=json::parse(a.body);CHECK(ja.get("user").get("id").string()==playerId("steam-a"));CHECK(ja.get("session").get("gateway").string()=="http://127.0.0.1:1/g");
    auto ta=tokenOf(a),tb=tokenOf(w.signIn("steam-b","Bob","g"));CHECK(ta!=tb);
    CHECK(w.signIn("","x","g").status==400);CHECK(w.signIn(std::string(MaxKeyBytes+1,'k'),"x","g").status==400);
    // A call needs a session.
    CHECK(w.call("nobody","getVersion","",1).status==401);
    CHECK(w.call("nobody","createObject","{\"t\":\"l\",\"q\":5}",1).status==401);CHECK(w.objects()==0);
    auto ver=json::parse(w.call(ta,"getVersion","",1).body);CHECK(ver.get("major").integer()==0&&ver.get("minor").integer()==12);
    // Ann builds; Bob finds it in that area, Ann does not get her own back, another area is empty.
    auto made=w.call(ta,"createObject","{\"data\":\"{\"x\":1,\"y\":2,\"m\":3,\"t\":\"l\",\"px\":10,\"py\":20,\"pz\":30,\"rx\":0,\"ry\":90,\"rz\":0,\"q\":226,\"lp\":0}\"}",100);
    CHECK(made.status==200);auto obj=json::parse(made.body);auto id=obj.get("id").string();
    CHECK(id.size()==13&&id[0]=='l');   // the game reads the kind from the id's first letter
    CHECK(w.call(ta,"createObject","{\"t\":\"ladder\",\"q\":226}",100).status==400);CHECK(w.call(ta,"createObject","{\"t\":\"L\",\"q\":226}",100).status==400);
    {auto other=json::parse(w.call(ta,"createObject","{\"t\":\"e\",\"q\":7}",100).body).get("id").string();CHECK(other.size()==13&&other[0]=='e'&&other.substr(1)!=id.substr(1));
     CHECK(w.call(ta,"deleteObject","{\"id\":\""+other+"\"}",100).status==200);}
    CHECK(!id.empty()&&obj.get("c").string()==playerId("steam-a")&&obj.get("l").items().size()==3&&obj.get("l").items()[1].integer()==20&&obj.get("ut").integer()==100);
    auto find=[&](const std::string& token,const char* body){return json::parse(w.call(token,"findQpidObjects",body,200).body).get("n").get("oa").items().size();};
    CHECK(find(tb,"{\"q\":226,\"o\":{\"ct\":{\"l\":5}}}")==1);
    CHECK(find(ta,"{\"q\":226,\"o\":{\"ct\":{\"l\":5}}}")==0);
    CHECK(find(tb,"{\"q\":227,\"o\":{\"ct\":{\"l\":5}}}")==0);
    CHECK(find(tb,"{\"q\":226,\"o\":{\"ct\":{\"l\":0}}}")==0);    // no more than asked
    CHECK(find(tb,"{\"q\":226,\"o\":{\"ct\":{\"b\":5}}}")==0);    // not a type that was asked for
    // Only the builder changes or removes it.
    CHECK(w.call(tb,"deleteObject","{\"id\":\""+id+"\"}",300).status==403);CHECK(w.objects()==1);
    CHECK(w.call(tb,"updateObject","{\"id\":\""+id+"\",\"ci\":{\"cp\":1,\"col\":2}}",300).status==403);
    CHECK(w.call(tb,"sendLike","{\"oid\":\""+id+"\",\"lp\":3,\"lp2\":0}",300).status==200);
    CHECK(w.call(tb,"sendLike","{\"oid\":\""+id+"\",\"lp\":-5}",300).status==400);
    {auto got=json::parse(w.call(tb,"getQpidObjects","{\"ids\":[\""+id+"\"]}",300).body).get("objects");CHECK(got.items().size()==1&&got.items()[0].get("lp").integer()==3);}
    // Likes: Bob's like reaches Ann's history once (not Bob's, not again after `since`, not for a like of her own).
    auto history=[&](const std::string& token,const char* body){return json::parse(w.call(token,"getLikeHistory",body,900).body).get("like_histories");};
    {auto h=history(ta,"{\"uid\":\"\",\"since\":0}");CHECK(h.items().size()==1);
     if(h.items().size()==1){auto& e=h.items()[0];CHECK(e.get("oid").string()==id&&e.get("uid").string()==playerId("steam-b")&&e.get("lp").integer()==3&&e.get("t").integer()==300&&e.get("sc").integer()==1);}}
    CHECK(history(tb,"{\"uid\":\"\",\"since\":0}").items().empty());
    CHECK(history(ta,"{\"uid\":\"\",\"since\":300}").items().empty());
    CHECK(w.call(ta,"sendLike","{\"oid\":\""+id+"\",\"lp\":1}",350).status==200);CHECK(history(ta,"{\"since\":0}").items().size()==1);
    // A new sign-in ends the old session of that player; the player stays the same.
    auto ta2=tokenOf(w.signIn("steam-a","Ann","g"));CHECK(w.call(ta,"getVersion","",1).status==401);CHECK(w.playerOf(ta2)==playerId("steam-a"));
    // The saved document restores everything, sessions included.
    auto saved=w.snapshot();World r([]{return std::string("fresh");});r.restore(json::parse(saved.dump()));
    CHECK(r.objects()==1&&r.playerOf(ta2)==playerId("steam-a")&&r.snapshot().dump()==saved.dump());
    {bool threw=false;try{r.restore(json::parse("{\"format\":2}"));}catch(const std::exception&){threw=true;}CHECK(threw);}
    CHECK(w.call(ta2,"deleteObject","{\"id\":\""+id+"\"}",400).status==200);CHECK(w.objects()==0);
    CHECK(w.call(ta2,"createObject","not json",1).status==400);
    // Caps: players and objects per player.
    World full([&n]{return "t"+std::to_string(++n);});
    for(size_t i=0;i<MaxPlayers;++i)CHECK(full.signIn("k"+std::to_string(i),"p","g").status==200);
    CHECK(full.signIn("one-more","p","g").status==403);CHECK(full.signIn("k0","p","g").status==200);
    World big([&n]{return "u"+std::to_string(++n);});auto tk=tokenOf(big.signIn("k","p","g"));
    for(size_t i=0;i<MaxObjectsPerPlayer;++i)big.call(tk,"createObject","{\"t\":\"l\",\"q\":1}",1);
    CHECK(big.objects()==MaxObjectsPerPlayer);CHECK(big.call(tk,"createObject","{\"t\":\"l\",\"q\":1}",1).status==403);
    if(failures)return 1;
    std::puts("world tests passed");return 0;
}
