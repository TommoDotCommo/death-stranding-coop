#include "offer_strand.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
using namespace mod::offer_strand;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"offer strand oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
// Sparse fake process memory: blocks at fixed addresses, a read must fall inside one block.
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> blocks;
    unsigned char* at(uint64_t a,size_t n){
        for(auto& b:blocks)if(a>=b.first&&a+n<=b.first+b.second.size())return b.second.data()+(a-b.first);
        return nullptr;}
    void block(uint64_t a,size_t n){blocks[a].assign(n,0);}
    template<class T> void put(uint64_t a,T v){std::memcpy(at(a,sizeof v),&v,sizeof v);}
    bool read(uint64_t a,void* p,size_t n){auto q=at(a,n);if(!q)return false;std::memcpy(p,q,n);return true;}
    uint64_t next=0x30000000;
    uint64_t string(const std::string& s,uint32_t length=0){   // an engine string: length 8 bytes before the characters
        uint64_t a=next;next+=0x100;block(a,0x10+std::max<size_t>(s.size(),length)+1);put<uint32_t>(a+8,length?length:uint32_t(s.size()));
        std::memcpy(at(a+0x10,s.size()),s.data(),s.size());return a+0x10;}
    // A construction object (record - 0x18) with what the game's body is made from.
    uint64_t object(uint8_t type,uint8_t owner,uint8_t state,uint8_t flags,uint32_t level,std::array<double,3> p,std::array<float,9> r,uint64_t id,float angle=0){
        uint64_t o=next;next+=0x1000;block(o,0x710);
        put(o+0x34,type);put(o+0x36,state);put(o+0xb2,owner);put(o+0xab,flags);put(o+0x84,level);
        std::memcpy(at(o+0x40,24),p.data(),24);std::memcpy(at(o+0x58,36),r.data(),36);put(o+0xb8,id);
        put(o+0x528,angle);put(o+0x580,angle);put(o+0x590,angle);return o;}
};
static const std::array<float,9> Level={1,0,0, 0,1,0, 0,0,1},Yaw90={0,1,0, -1,0,0, 0,0,1},Pole={0,0,-1, -1,0,0, 0,1,0};
static bool near(int32_t a,int32_t b){return std::abs(int64_t(a)-b)<=2;}   // the game's atan2 may differ by an ulp
int main(){
    Memory m;mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){return m.read(a,p,n);},0x140000000};
    const uint64_t none=m.string(""),post=m.string("pK3v9QmZ0aB1c"),capital=m.string("NQ7fT2nP4xY8w"),wrong=m.string("lA1b2C3d4E5f6");
    const uint64_t bridge=m.string("bK3v9QmZ0aB1c"),huge=m.string("pHUGE",100),odd=m.string("p-1b2C3d4E5f6");
    const std::array<double,3> knot={1054.34,-3512.77,143.63};

    // Reading: the fields, the id, a bridge's own angle; an id 100 characters long is no string.
    uint64_t ladder=m.object(25,Player,2,0,0,knot,Yaw90,none);
    auto l=read(s,ladder);CHECK(l.has_value());
    if(l){CHECK(l->type==25&&l->owner==Player&&l->state==2&&!l->held&&l->level==0&&l->online.empty());
        CHECK(l->position[1]==-3512.77&&l->rotation[1]==1.0f);}
    auto b30=read(s,m.object(31,Player,1,0,1,knot,Level,bridge,0.5f));CHECK(b30&&b30->angle==0.5f&&b30->online=="bK3v9QmZ0aB1c");
    CHECK(!read(s,m.object(25,Player,2,0,0,knot,Level,huge)).has_value());
    CHECK(!read(s,0x10).has_value());

    // Which records are offered, and how.
    auto decide=[&](uint8_t type,uint8_t owner,uint8_t state,uint8_t flags,uint64_t id){
        auto x=read(s,m.object(type,owner,state,flags,1,knot,Level,id));return x?plan(*x).plan:Plan::Skip;};
    CHECK(decide(25,Player,2,0,none)==Plan::Upload);          // his ladder, on no server
    CHECK(decide(6,Player,1,0,post)==Plan::Recreate);         // his post with an id of the original service
    CHECK(decide(18,Player,2,0,capital)==Plan::Recreate);     // the original service's own form: the letter in capitals
    CHECK(decide(6,Player,1,0,wrong)==Plan::Skip);            // an id whose letter is another kind's
    CHECK(decide(6,Player,1,0,odd)==Plan::Skip);              // an id with a character no server id has
    CHECK(decide(26,2,2,0,none)==Plan::Skip);                 // another player's (Net) ladder
    CHECK(decide(6,0,1,0,none)==Plan::Skip);                  // the game's own
    CHECK(decide(18,Player,0,0,none)==Plan::Skip);            // a request the game has not built
    CHECK(decide(18,Player,2,0x80,none)==Plan::Skip);         // a record the game refuses to upload
    CHECK(decide(4,Player,2,0,none)==Plan::Skip);             // a type the game sends as "unknown"
    CHECK(letter(3)=='s'&&letter(7)=='p'&&letter(22)=='z'&&letter(34)=='b'&&letter(41)=='g'&&letter(44)=='j'&&letter(26)==0);

    // The map cell: tiles of the game's size counted from the middle, two tiles a cell, the edges held.
    auto c=cell(1054.34,411.51,512,36,30);CHECK(c[0]==10&&c[1]==7);
    c=cell(1e6,-1e6,512,36,30);CHECK(c[0]==17&&c[1]==0);
    c=cell(-20000,-5000,512,36,30);CHECK(c[0]==0&&c[1]==2);
    c=cell(5,5,0,36,30);CHECK(c[0]==0&&c[1]==0);

    // Angles: degrees x 10^5; a yaw-only structure has only rz; standing on end, rz comes from the forward row.
    auto a=angles(Level);CHECK(a[0]==0&&a[1]==0&&a[2]==0);
    a=angles(Yaw90);CHECK(a[0]==0&&a[1]==0&&near(a[2],9000000));
    a=angles(Pole);CHECK(a[0]==0&&near(a[1],9000000)&&near(a[2],9000000));

    // The body as the game sends it: positions from the f32 of each coordinate, "st" by kind, the extra blocks.
    Placement here{10,7,22123,226,0,0,3};
    if(l){
        auto y=angles(Yaw90);
        std::string want="{\"x\":10,\"y\":7,\"m\":22123,\"t\":\"l\",\"px\":105434000,\"py\":-351276992,\"pz\":14363000,\"rx\":"+std::to_string(y[0])+
            ",\"ry\":"+std::to_string(y[1])+",\"rz\":"+std::to_string(y[2])+",\"exp\":5,\"q\":226,\"st\":\"0\",\"p\":0,\"lp\":3}";
        CHECK(body(*l,here)==want);
        CHECK(body(*l,here,true).empty());                    // no id to re-create with
    }
    if(b30){auto t=body(*b30,here);
        CHECK(t.find("\"t\":\"b\"")!=std::string::npos&&t.find("\"st\":\"30\"")!=std::string::npos&&t.find(",\"bi\":{\"a\":2864788},\"lp\":3}")!=std::string::npos);
        auto r=body(*b30,here,true);const std::string tail=",\"id\":\"bK3v9QmZ0aB1c\"}";
        CHECK(r.size()>tail.size()&&r.compare(r.size()-tail.size(),tail.size(),tail)==0&&r.find(",\"lp\":3,\"id\"")!=std::string::npos);}
    auto b45=read(s,m.object(34,Player,2,0,1,knot,Level,none,0.5f));
    if(b45){auto t=body(*b45,here);CHECK(t.find("\"st\":\"\"")!=std::string::npos&&t.find("\"bi\":{\"a\":2864788}")!=std::string::npos);}
    auto rope=read(s,m.object(28,Player,2,0,0,knot,Level,none));CHECK(rope&&body(*rope,here).empty());
    auto shelter=read(s,m.object(18,Player,2,0,1,knot,Level,none));
    if(shelter){Placement aside=here;aside.alternative=227;aside.priority=1;auto t=body(*shelter,aside);
        CHECK(t.find("\"st\":\"\",\"p\":1,\"ei\":{\"aq\":227},\"lp\":3}")!=std::string::npos&&t.find("bi")==std::string::npos);}

    if(failures){std::fprintf(stderr,"offer strand oracle: %d failures\n",failures);return 1;}
    std::puts("offer strand ok");return 0;
}
