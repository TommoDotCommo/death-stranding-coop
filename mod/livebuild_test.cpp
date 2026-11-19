// What other players build, at once (livebuild.h), without a game: the kinds, the rotation against the game's own
// decomposition, the server's answer, and the plan (nearest first, a few at a time, asked again only after a while,
// given up after three asks, removed ones taken down as Net copies only, a full read dropping what is gone).
#include "livebuild.h"
#include <cmath>
#include <cstdio>
using namespace mod::livebuild;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"livebuild oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static void kinds(){
    CHECK(netType('l',"")==26&&netType('c',"")==16&&netType('n',"")==19&&netType('p',"")==7&&netType('z',"")==23);
    CHECK(netType('b',"30")==32&&netType('b',"")==35&&netType('j',"")==45&&netType('g',"")==42);
    CHECK(netType('e',"")==0&&netType('t',"")==0&&netType('v',"")==0&&netType('h',"")==0&&netType('o',"")==0);
}
static void rotation(){
    // The game's decomposition (0x19c1270): rx, ry, rz back from the rows, for pitched and rolled structures too.
    const double k=180/3.14159265358979323846;
    for(auto a:std::vector<std::array<double,3>>{{0,0,90},{0,0,-135},{10,-20,30},{-45,5,170},{30,60,-80}}){
        auto m=rotationOf(a[0],a[1],a[2]);
        double ry=std::asin(std::fmax(-1.,std::fmin(1.,-double(m[2])))),rx=std::atan2(double(m[5]),double(m[8])),rz=std::atan2(double(m[1]),double(m[0]));
        CHECK(std::fabs(rx*k-a[0])<1e-3&&std::fabs(ry*k-a[1])<1e-3&&std::fabs(rz*k-a[2])<1e-3);
        // rows are unit length and at right angles
        for(int i=0;i<3;++i){double n=0;for(int j=0;j<3;++j)n+=double(m[i*3+j])*m[i*3+j];CHECK(std::fabs(n-1)<1e-5);}
        double dot=0;for(int j=0;j<3;++j)dot+=double(m[j])*m[3+j];CHECK(std::fabs(dot)<1e-5);}
    auto yaw=rotationOf(0,0,90);CHECK(std::fabs(yaw[0])<1e-6&&std::fabs(yaw[1]-1)<1e-6&&std::fabs(yaw[8]-1)<1e-6);
}
static const char* Body=R"({"format":1,"version":"ab-7","full":true,"wait_ms":3000,"more":false,"objects":[
 {"id":"lAAAAAAAAAAAA","c":"zygo_x","t":"l","l":[100000,-200000,30000],"r":[0,0,9000000],"exp":5,"q":226,"st":""},
 {"id":"bBBBBBBBBBBBB","c":"zygo_x","t":"b","l":[103000,49000,15600],"exp":2,"q":226,"st":"30"},
 {"id":"tCCCCCCCCCCCC","c":"zygo_x","t":"t","l":[1,2,3],"exp":2,"q":226},
 {"id":"nDDDDDDDDDDDD","c":"zygo_x","t":"n","l":[1,2],"exp":2,"q":226}],"removed":["lZZZZZZZZZZZZ",7]})";
static void answers(){
    auto a=parse(Body);
    CHECK(a.read&&a.full&&!a.more&&a.version=="ab-7"&&a.waitMs==3000&&a.pieces.size()==2&&a.skipped==2);
    if(a.pieces.size()==2){
        auto& l=a.pieces[0];CHECK(l.id=="lAAAAAAAAAAAA"&&l.type==26&&std::fabs(l.at.position[0]-1)<1e-9&&std::fabs(l.at.position[1]+2)<1e-9&&std::fabs(l.at.position[2]-0.3)<1e-9);
        CHECK(std::fabs(l.at.rotation[1]-1)<1e-5);   // rz 90 degrees
        auto& b=a.pieces[1];CHECK(b.type==32&&std::fabs(b.at.position[0]-1030)<1e-9&&std::fabs(b.at.rotation[0]-1)<1e-6);}
    CHECK(a.removed==std::vector<std::string>{"lZZZZZZZZZZZZ"});
    CHECK(!parse("{").read&&!parse(R"({"format":2,"version":"x"})").read&&!parse(R"({"format":1})").read);
    CHECK(parse(R"({"format":1,"version":"v","wait_ms":5})").waitMs==WaitMinMs&&parse(R"({"format":1,"version":"v","wait_ms":999999999})").waitMs==WaitMaxMs);
    unsigned f=0;CHECK(delayAfter(a,f)==3000&&f==0);
    Answer bad;CHECK(delayAfter(bad,f)==FailWaitMs&&delayAfter(bad,f)==2*FailWaitMs);
    for(int i=0;i<10;++i)delayAfter(bad,f);
    CHECK(delayAfter(bad,f)==WaitMaxMs);CHECK(delayAfter(a,f)==3000&&f==0);
}
static Piece piece(const std::string& id,double x){Piece p;p.id=id;p.type=26;p.at.position={x,0,0};return p;}
static Answer full(std::vector<Piece> ps,const std::string& v="e-1"){Answer a;a.read=true;a.full=true;a.version=v;a.pieces=std::move(ps);return a;}
static Seen seen(const std::string& online,uint8_t owner=2){Seen s;s.online=online;s.owner=owner;s.state=2;return s;}
static std::vector<std::string> ids(const std::vector<Piece>& ps){std::vector<std::string> out;for(auto& p:ps)out.push_back(p.id);return out;}
static void plan(){
    const std::array<double,3> here{0,0,0};
    Plan p;p.take(full({piece("a",10),piece("b",5),piece("c",1500),piece("d",2500),piece("e",3),piece("f",4)}));
    CHECK(p.wanted()==6&&p.since()=="e-1");
    // Nearest first, four at a time, nothing out of reach.
    auto s=p.step({},here,true,1000);CHECK(ids(s.build)==std::vector<std::string>({"e","f","b","a"}));
    // Asked a moment ago: not asked again; the fifth waits its turn.
    s=p.step({},here,true,2000);CHECK(ids(s.build)==std::vector<std::string>{"c"});
    // They stand now (any owner): nothing to build. After the wait, one that never appeared is asked again.
    s=p.step({seen("a"),seen("b"),seen("c"),seen("e",1)},here,true,12000);CHECK(ids(s.build)==std::vector<std::string>{"f"});
    s=p.step({seen("a"),seen("b"),seen("c"),seen("e")},here,true,23000);CHECK(ids(s.build)==std::vector<std::string>{"f"});
    s=p.step({seen("a"),seen("b"),seen("c"),seen("e")},here,true,34000);CHECK(s.build.empty()&&s.givenUp==std::vector<std::string>{"f"});
    s=p.step({seen("a"),seen("b"),seen("c"),seen("e")},here,true,45000);CHECK(s.build.empty()&&s.givenUp.empty());
    // Without the porter's place every one is in reach.
    Plan far;far.take(full({piece("d",2500)}));CHECK(ids(far.step({},here,false,0).build)==std::vector<std::string>{"d"});
    // Removed: its Net copy taken down, never the player's own record with that id; then not built again.
    Answer gone;gone.read=true;gone.version="e-2";gone.removed={"a","e"};p.take(gone);
    s=p.step({seen("a"),seen("b"),seen("c"),seen("e",1)},here,true,50000);
    CHECK(s.remove.size()==1&&s.remove[0].online=="a"&&s.remove[0].owner==2&&s.build.empty());
    s=p.step({seen("b"),seen("c")},here,true,51000);CHECK(s.remove.empty()&&s.build.empty());
    // A changed one comes back to life, even given up before.
    Answer again;again.read=true;again.version="e-3";again.pieces={piece("f",4)};p.take(again);
    CHECK(ids(p.step({seen("b"),seen("c")},here,true,52000).build)==std::vector<std::string>{"f"});
    // A full read without one that was made: it is taken down.
    p.take(full({piece("b",5),piece("f",4)},"e-9"));
    s=p.step({seen("b"),seen("c"),seen("f")},here,true,70000);CHECK(s.remove.size()==1&&s.remove[0].online=="c");
    // Removed while still on its way: taken down when it appears, not forgotten.
    Plan q;q.take(full({piece("x",1)}));CHECK(ids(q.step({},here,true,0).build)==std::vector<std::string>{"x"});
    Answer rx;rx.read=true;rx.version="e-2";rx.removed={"x"};q.take(rx);
    CHECK(q.step({},here,true,1000).remove.empty());
    s=q.step({seen("x")},here,true,2000);CHECK(s.remove.size()==1&&s.build.empty());
    // Two records with one id are counted.
    CHECK(q.step({seen("y"),seen("y",1)},here,true,3000).twice==1);
    // An answer that was not read changes nothing.
    Answer none;q.take(none);CHECK(q.since()=="e-2");
}
int main(){
    kinds();rotation();answers();plan();
    if(failures){std::fprintf(stderr,"%u livebuild oracle failures\n",failures);return 1;}
    std::puts("livebuild oracles passed");return 0;
}
