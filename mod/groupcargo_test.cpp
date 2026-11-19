// Cargo shared in group play (groupcargo.h), without a game: the server's answer, the plan (a set-down told once and
// matched to its server id, a pick-up told only for a crate the server knows, a pick-up before the server answered told
// when it does, others' crates made near the porter a few at a time, taken ones removed, a full read dropping copies the
// server no longer lists), and the courier's order (messages first, reads since the last version).
#include "groupcargo.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace mod::groupcargo;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"groupcargo oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static mod::cargo::Placement put(uint64_t id,uint32_t hash,double x){mod::cargo::Placement p;p.id=id;p.hash=hash;p.position={x,0,0};return p;}
static mod::cargo::Taken took(uint64_t id){mod::cargo::Taken t;t.id=id;return t;}
static Answer read(std::vector<Crate> cs,std::vector<std::string> removed={},bool full=false){Answer a;a.read=true;a.full=full;a.version="e-1";a.crates=std::move(cs);a.removed=std::move(removed);return a;}
static Crate crate(const std::string& id,double x,uint32_t hash=9){Crate c;c.id=id;c.hash=hash;c.at={x,0,0};return c;}
static void answers(){
    auto a=parse(R"({"format":1,"version":"ab-3","full":true,"on":true,"wait_ms":3000,"crates":[
        {"id":"kAAAAAAAAAAAA","hash":-1,"x":1.5,"y":-2,"z":3,"yaw":0.5,"area":226},{"id":"kBBBBBBBBBBBB","hash":0,"x":1,"y":2,"z":3},
        {"id":"","hash":5,"x":1,"y":2,"z":3}],"removed":["kZZ",4]})");
    CHECK(a.read&&a.full&&a.on&&a.version=="ab-3"&&a.crates.size()==1&&a.removed==std::vector<std::string>{"kZZ"});
    if(a.crates.size()==1)CHECK(a.crates[0].hash==0xffffffffu&&a.crates[0].at[0]==1.5&&a.crates[0].at[1]==-2&&a.crates[0].area==226);
    CHECK(!parse("{").read&&!parse(R"({"format":2,"version":"x"})").read&&!parse(R"({"format":1,"version":"x","on":false})").on);
    Say p;p.hash=7;p.at={1,2,3};p.area=226;CHECK(body(p)==R"({"op":"place","hash":7,"x":1,"y":2,"z":3,"area":226})");
    Say t;t.take=true;t.id="kA";CHECK(body(t)==R"({"op":"take","id":"kA"})");
    unsigned f=0;Answer bad;CHECK(delayAfter(bad,f)==FailWaitMs&&delayAfter(read({}),f)==3000&&f==0);
}
static void plan(){
    Plan p(1000);
    // A set-down: told once, matched to the server's id when it answers; picking it up again tells the take.
    auto says=p.placed(put(5,77,1),226);CHECK(says.size()==1&&!says[0].take&&says[0].local==5&&says[0].hash==77&&says[0].area==226);
    CHECK(p.placed(put(5,77,1),226).empty());
    CHECK(p.answered(5,"kOWN").empty()&&p.known()==1);
    says=p.taken(took(5));CHECK(says.size()==1&&says[0].take&&says[0].id=="kOWN"&&p.known()==0);
    CHECK(p.taken(took(5)).empty()&&p.taken(took(0)).empty());
    // Picked up before the server answered: the take goes when it does. Refused: nothing more.
    p.placed(put(6,77,1),226);CHECK(p.taken(took(6)).empty());
    says=p.answered(6,"kLATE");CHECK(says.size()==1&&says[0].take&&says[0].id=="kLATE");
    p.placed(put(7,77,1),226);CHECK(p.answered(7,"").empty()&&p.taken(took(7)).empty());
    // Others' crates: the nearest made first, at most eight a step, the far ones wait; each with an alias of its own.
    std::vector<Crate> cs;for(int i=0;i<10;++i)cs.push_back(crate("k"+std::to_string(i),double(10*i)));cs.push_back(crate("kFAR",5000));
    p.take(read(cs));
    auto s=p.step({0,0,0},true);CHECK(s.make.size()==MakesPerStep&&s.make[0].position[0]==0&&s.make[0].id==1000&&s.make[7].id==1007&&s.gone.empty());
    s=p.step({0,0,0},true);CHECK(s.make.size()==2);
    CHECK(p.step({0,0,0},true).make.empty());
    CHECK(p.step({5000,0,0},true).make.size()==1);
    // Read again: what is made is not made twice.
    p.take(read(cs));CHECK(p.step({0,0,0},true).make.empty());
    // Taken by someone: its copy here goes. This porter picks one up: the take is told, by the server's id.
    p.take(read({},{"k0"}));s=p.step({0,0,0},true);CHECK(s.gone==std::vector<uint64_t>{1000});
    says=p.taken(took(1001));CHECK(says.size()==1&&says[0].take&&says[0].id=="k1");
    // A full read without some made ones: they go; one set down here is never dropped by that.
    p.placed(put(8,77,1),226);p.answered(8,"kMINE");
    p.take(read({crate("k2",20),crate("kFAR",5000)},{},true));
    s=p.step({0,0,0},true);CHECK(s.gone.size()==7);   // k3..k9: k0 was removed, k1 taken, k2 and kFAR still listed
    bool mineKept=true;for(auto g:s.gone)if(g==8)mineKept=false;CHECK(mineKept);
    CHECK(!p.taken(took(8)).empty());
    // Removed before it was made: never made.
    Plan q(2000);q.take(read({crate("kX",1)}));q.take(read({},{"kX"}));CHECK(q.step({0,0,0},true).make.empty());
    // The server says cargo is off: nothing waiting is made.
    Plan r(3000);r.take(read({crate("kY",1)}));Answer off=read({});off.on=false;r.take(off);CHECK(r.step({0,0,0},true).make.empty());
}
static void courier(){
    std::vector<std::string> log,posted,since;std::mutex m;
    Courier c([&](const std::string& s){std::lock_guard<std::mutex> g(m);since.push_back(s);return Result{200,R"({"format":1,"version":"e-)"+std::to_string(since.size())+R"(","wait_ms":1000,"crates":[]})"};},
              [&](const std::string& b){std::lock_guard<std::mutex> g(m);posted.push_back(b);return Result{200,R"({"format":1,"id":"kNEW"})"};},
              [&](const std::string& l){std::lock_guard<std::mutex> g(m);log.push_back(l);});
    Say a;a.local=3;a.hash=7;Say b;b.take=true;b.id="kOLD";c.send({a,b});
    std::vector<Courier::Answered> got;
    for(int i=0;i<100&&got.empty();++i){std::this_thread::sleep_for(std::chrono::milliseconds(20));auto x=c.answered();got.insert(got.end(),x.begin(),x.end());}
    CHECK(got.size()==1&&got[0].local==3&&got[0].id=="kNEW");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    auto reads=c.reads();
    std::lock_guard<std::mutex> g(m);
    CHECK(posted.size()==2&&posted[0].find("\"place\"")!=std::string::npos&&posted[1].find("kOLD")!=std::string::npos);
    CHECK(since.size()>=2&&since[0].empty()&&since[1]=="e-1"&&!reads.empty());
}
int main(){
    answers();plan();courier();
    if(failures){std::fprintf(stderr,"%u groupcargo oracle failures\n",failures);return 1;}
    std::puts("groupcargo oracles passed");return 0;
}
