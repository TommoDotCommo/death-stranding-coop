#include "structures.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace mod::structures;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"structures oracle: %s\n",#x);++failures;}}while(0)
constexpr double Pi=3.14159265358979323846;
double wrap(double a){return std::remainder(a,2*Pi);}
Record rec(uint32_t id,uint8_t type,uint8_t state,double x=0){Record r;r.id=id;r.type=type;r.state=state;r.at.position={x,2,3};r.at.rotation=rotation(.5,0,0);return r;}
int main(){
    {   // The construction table, read in one copy, and record by record when the one copy is not possible:
        // the same structures either way. Records count back from TableEnd; a record is three image-pointer words.
        const uint64_t base=0x140000000,image=0x8000000,manager=0x20000000;
        for(bool short_:{false,true}){
            uint64_t lo=manager+TableEnd-uint64_t(MaxRecords)*Stride;std::vector<unsigned char> mem(size_t(MaxRecords)*Stride);
            auto rec=[&](unsigned i,uint32_t id,uint8_t type){uint64_t at=manager+TableEnd-i*Stride-lo;uint64_t q0=base+0x3000000,z=0,q2=q0+0x10;
                std::memcpy(&mem[at],&q0,8);std::memcpy(&mem[at+8],&z,8);std::memcpy(&mem[at+16],&q2,8);std::memcpy(&mem[at+0x18],&id,4);mem[at+0x1c]=type;mem[at+0x1e]=2;
                double p[3]={1,2,3};std::memcpy(&mem[at+0x28],p,24);};
            rec(1,700,25);rec(5,701,26);
            // short_: memory ends 100 records before the table start, so the one copy fails and the loop reads what it can.
            uint64_t from=short_?lo+100ull*Stride:lo;
            mod::discovery::Sampler sm{[&](uint64_t a,void* o,size_t n){if(a<from||a+n>lo+mem.size())return false;std::memcpy(o,&mem[a-lo],n);return true;},base};
            auto t=readTable(sm,manager,base,image);
            CHECK(t.size()==2&&t[0].id==700&&t[0].type==25&&t[1].id==701&&t[1].at.position[1]==2);
        }
    }
    // The porter's matrix (live: rows -0.819,0.574,0 / -0.574,-0.819,0 / 0,0,1) is yaw 145 degrees.
    { auto r=rotation(145*Pi/180,0,0);CHECK(std::fabs(r[0]+.81915)<1e-4&&std::fabs(r[1]-.57358)<1e-4&&std::fabs(r[3]+.57358)<1e-4&&std::fabs(r[4]+.81915)<1e-4&&std::fabs(r[8]-1)<1e-6);
      double y,p,q;angles(r,y,p,q);CHECK(std::fabs(wrap(y-145*Pi/180))<1e-5&&std::fabs(p)<1e-6&&std::fabs(q)<1e-6); }
    // Rotation <-> angles round trip, including tilted ladders and rolled ropes.
    for(double y:{-3.,-1.,0.,.7,2.9})for(double p:{-1.2,-.3,0.,.4,1.3})for(double q:{-2.,-.5,0.,.6,2.5}){
        auto r=rotation(y,p,q);double a,b,c;angles(r,a,b,c);auto s=rotation(a,b,c);
        double e=0;for(int i=0;i<9;++i)e=std::fmax(e,std::fabs(r[i]-s[i]));CHECK(e<1e-5);
        // rows are orthonormal
        double d01=r[0]*r[3]+r[1]*r[4]+r[2]*r[5],n2=r[6]*r[6]+r[7]*r[7]+r[8]*r[8];CHECK(std::fabs(d01)<1e-6&&std::fabs(n2-1)<1e-6);
    }
    // Wire packing keeps type, pitch and roll within one 12-bit step.
    { Built b;b.type=25;b.pitch=-.7;b.roll=1.9;auto u=unpack(pack(b),{1,2,3},.3);
      CHECK(u.type==25&&std::fabs(wrap(u.pitch+.7))<2*Pi/4096&&std::fabs(wrap(u.roll-1.9))<2*Pi/4096&&u.yaw==.3&&u.position[2]==3); }
    // Player types map to their Net variant; others have none.
    CHECK(netVariant(25)==uint8_t(26)&&netVariant(28)==uint8_t(29)&&netVariant(6)==uint8_t(7)&&netVariant(31)==uint8_t(32)&&netVariant(34)==uint8_t(35));
    CHECK(!netVariant(26)&&!netVariant(24)&&!netVariant(0)&&!netVariant(36));
    // Sync: existing structures are learnt; a structure this porter builds is announced once, when built; the
    // partner's (Net ids) and the game's own never; refused ones not until built.
    { Sync s;CHECK(s.observe({rec(225,0,1),rec(10001,25,2)}).empty());
      auto a=s.observe({rec(225,0,1),rec(10001,25,2),rec(10002,28,2,7),rec(100001,26,2),rec(300,25,2)});
      CHECK(a.size()==1&&a[0].id==10002&&a[0].type==28&&a[0].position[0]==7&&std::fabs(a[0].yaw-.5)<1e-6);
      CHECK(s.observe({rec(10002,28,2,7),rec(100001,26,2)}).empty());
      CHECK(s.observe({rec(10003,6,0)}).empty());                        // refused or pending
      auto b=s.observe({rec(10003,6,2)});CHECK(b.size()==1&&b[0].id==10003);
      CHECK(s.observe({rec(10004,24,2)}).empty()); }                     // no Net variant (a Stage type): not sent
    // A partner structure recreated here with a Player type still has a Net id: never sent back.
    { Sync s;s.observe({});CHECK(s.observe({rec(100002,25,2),rec(100003,28,2)}).empty()); }
    // Dismantled: an announced structure that disappears (or stops being built) is reported once; others never.
    { Sync s;s.observe({});s.observe({rec(10005,25,2),rec(10006,28,2)});
      CHECK(s.takeRemoved().empty());
      s.observe({rec(10006,28,2)});auto r=s.takeRemoved();CHECK(r.size()==1&&r[0].id==10005&&r[0].type==25);
      s.observe({rec(10006,28,0)});r=s.takeRemoved();CHECK(r.size()==1&&r[0].id==10006);
      s.observe({});CHECK(s.takeRemoved().empty()); }
    { Sync s;s.observe({rec(10007,25,2,4)});s.observe({});auto r=s.takeRemoved();CHECK(r.size()==1&&r[0].id==10007&&r[0].type==25&&r[0].position[0]==4); }   // built in an earlier session: still reported, with type and place
    { NetCopies n;n.dismantledAt(26,{10,2,3},1000);auto a=rec(100009,26,2,10);CHECK(n.due({a},1500).size()==1);CHECK(n.due({a},2000).empty()); }   // unknown ref: by type and place
    // A partner structure already standing here (saved from an earlier session) is not built twice.
    { auto a=rec(100009,26,2,10);CHECK(standing({a},26,{10.3,2,3})&&standing({a},26,{10.9,2,3})&&!standing({a},26,{11.5,2,3})&&!standing({a},29,{10,2,3}));   // the game may move what it builds by about 0.6 m
      auto b=rec(100010,26,0,10);CHECK(!standing({b},26,{10,2,3})); }
    // A dismantle that arrives before the Net copy is built waits for it, then clears it once; unknown refs are ignored.
    { NetCopies n;n.built(7,26,{10,2,3});n.dismantled(7,1000);n.dismantled(99,1000);
      CHECK(n.due({},1500).empty());                                   // not built yet: wait
      auto r=rec(100005,26,0,10);CHECK(n.due({r},2000).empty());       // pending, not built
      r.state=2;auto d=n.due({r},2500);CHECK(d.size()==1&&d[0][0]==10);CHECK(n.due({r},3000).empty()&&!n.known(7));
      NetCopies g;g.built(8,26,{10,2,3});g.dismantled(8,1000);CHECK(g.due({},1000+NetCopies::GiveUp+1).empty()&&!g.known(8)); }
    if(!failures)std::puts("structure rotation, packing, variants and sync checks passed");
    return failures?1:0;
}
