// Map markers in a fake world that keeps the game's user-marker rules (static, game_offsets.md "Map markers"):
// 50 records, five groups of ten points; a set replaces that point and gives it a new id; a set is refused when another
// point of the group lies at the same x and y; the player's "add a pin" takes the first empty group, or else clears the
// oldest single-pin group. Then the same on raw memory through readRecords.
#include "marker.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod::marker;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"marker oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
struct World {
    std::vector<Record> r=std::vector<Record>(Records);uint64_t nextId=100;unsigned sets=0,removes=0;
    World(){for(unsigned i=0;i<Records;++i)r[i].index=i;}
    Record* at(uint8_t g,uint8_t p){for(auto& x:r)if(x.used&&x.group==g&&x.point==p)return &x;return nullptr;}
    void set(const std::array<float,3>& pos,uint8_t g,uint8_t p){
        ++sets;if(auto* old=at(g,p))old->used=false;
        for(auto& x:r)if(x.used&&x.group==g&&x.position[0]==pos[0]&&x.position[1]==pos[1])return;   // duplicate place: refused
        for(auto& x:r)if(!x.used){x.used=true;x.group=g;x.point=p;x.position=pos;x.id=nextId++;return;}
    }
    void remove(uint32_t i){++removes;r.at(i).used=false;}
    unsigned count(uint8_t g) const {unsigned n=0;for(auto& x:r)n+=x.used&&x.group==g;return n;}
    // The player adds a pin from the map at an unstamped place (the game's group choice).
    uint8_t playerPin(float x,float y){
        for(uint8_t g=0;g<Groups;++g)if(!count(g)){set({x,y,10},g,0);return g;}
        uint8_t oldest=0;uint64_t id=~0ull;
        for(uint8_t g=0;g<Groups;++g)if(count(g)==1)for(auto& e:r)if(e.used&&e.group==g&&e.id<id){id=e.id;oldest=g;}
        for(auto& e:r)if(e.used&&e.group==oldest)e.used=false;
        set({x,y,10},oldest,0);return oldest;
    }
    Api api(){Api a;a.read=[this]{return r;};a.set=[this](auto& p,uint8_t g,uint8_t q){set(p,g,q);};a.remove=[this](uint32_t i){remove(i);};return a;}
};
std::vector<std::string> lines;
auto logger=[](const std::string& s){lines.push_back(s);};
int main(){
    using P=std::array<double,3>;
    // Fingerprint: every mod position is stamped, within 2 cm of the goal; ordinary positions are not.
    { for(double x=-9000;x<9000;x+=137.37)for(double y:{-4321.123,-0.4,0.0,0.9,1234.5678}){auto s=stamp({x,y,5});
          CHECK(stamped(s));CHECK(std::abs(s[0]-x)<0.1&&std::abs(s[1]-y)<0.1&&std::abs(s[2]-5)<0.01);}
      unsigned hits=0;for(int i=0;i<200000;++i){float x=float(-5000+i*0.04999123),y=float(3000-i*0.03311);hits+=stamped({x,y,float(i%97)});}
      CHECK(hits<=5); }   // expected 200000 / 2^18, below one
    // Off by default: nothing is read or written.
    { World w;Markers m(w.api(),logger);m.tick(0,P{1,2,3},false);m.partnerPing({7,{1,1,1}},0);CHECK(!m.localPing({1,1,1},0));
      m.tick(10,P{1,2,3},false);CHECK(w.sets==0&&w.removes==0); }
    // The partner marker: placed at once in group 4, point 0; follows the partner at most once a second.
    { World w;Markers m(w.api(),logger);m.enable(true);
      m.tick(0,P{100,200,30},false);auto* r=w.at(4,0);CHECK(r&&std::abs(r->position[0]-100)<0.1&&stamped(r->position));
      unsigned before=w.sets;
      for(uint64_t t=10;t<=10000;t+=10)m.tick(t,P{100+double(t)/100,200,30},false);   // walks 10 m/s for 10 s
      CHECK(w.sets-before>=9&&w.sets-before<=10);                                     // at most 1 Hz
      r=w.at(4,0);CHECK(r&&std::abs(r->position[0]-200)<=10.5);CHECK(w.count(4)==1);
      before=w.sets;for(uint64_t t=10010;t<=20000;t+=10)m.tick(t,P{200.5,200,30},false);   // standing: no more moves
      CHECK(w.sets-before<=1);
      // Disconnect (nothing shown here): removed in the same tick.
      m.tick(20010,std::nullopt,false);CHECK(w.count(4)==0);
      m.tick(20020,P{200,200,30},false);CHECK(w.count(4)==1);
      // A local event or private room: removed.
      m.tick(20030,P{200,200,30},true);CHECK(w.count(4)==0);
      m.tick(21100,P{200,200,30},false);CHECK(w.count(4)==1);
      // Switched off: removed.
      m.enable(false);CHECK(w.count(4)==0&&w.count(3)==0);m.tick(30000,P{1,1,1},false);CHECK(w.count(4)==0); }
    // Pings: a local ping is shown here and returned for the partner; the partner's ping is shown once per ref; a
    // repeat (the same event delivered again) neither shows it again nor keeps it longer.
    { World w;Markers m(w.api(),logger);m.enable(true);m.setRefBase(1<<20);
      auto p=m.localPing({50,60,7},0);CHECK(p&&p->ref==(1<<20)+1);m.tick(0,std::nullopt,false);CHECK(w.at(3,0)&&w.count(3)==1);
      m.partnerPing(*p,10);CHECK(m.duplicates()==1);   // our own ping echoed back: ignored
      Ping q{9001,{70,80,9}};m.partnerPing(q,1000);m.tick(1000,std::nullopt,false);
      auto* r=w.at(3,0);CHECK(r&&std::abs(r->position[0]-70)<0.1);
      unsigned before=w.sets;
      m.partnerPing(q,50000);m.tick(50000,std::nullopt,false);CHECK(w.sets==before);CHECK(m.duplicates()==2);
      m.tick(60999,std::nullopt,false);CHECK(w.count(3)==1);
      m.tick(61000,std::nullopt,false);CHECK(w.count(3)==0);   // a minute after it arrived
      m.partnerPing({9002,{1,2,3}},62000);m.tick(62000,std::nullopt,true);CHECK(w.count(3)==0);   // hidden: waits
      m.tick(62100,std::nullopt,false);CHECK(w.count(3)==1); }
    // The player's own pins: a group the player uses is never written; when the game hands our group to the player's
    // new pin, the mod stops there and never removes the player's pin.
    { World w;w.playerPin(1.3f,2.7f);w.playerPin(5.1f,5.2f);w.playerPin(9.9f,9.1f);w.playerPin(11.1f,12.7f);   // groups 0..3
      w.set({20.3f,20.7f,0},4,0);   // a route point of the player's in group 4
      Markers m(w.api(),logger);m.enable(true);
      for(uint64_t t=0;t<5000;t+=100)m.tick(t,P{300,300,30},false);
      auto* r=w.at(4,0);CHECK(r&&r->position[0]==20.3f);CHECK(w.count(4)==1);
      auto q=m.localPing({1,1,1},5000);m.tick(5000,std::nullopt,false);CHECK(q);
      r=w.at(3,0);CHECK(r&&r->position[0]==11.1f); }
    { World w;Markers m(w.api(),logger);m.enable(true);
      for(int i=0;i<4;++i)w.playerPin(float(i)+0.3f,0.7f);   // groups 0..3 are the player's
      m.tick(0,P{300,300,30},false);CHECK(w.count(4)==1);
      auto g=w.playerPin(77.7f,66.6f);                         // all used: the game clears the oldest single pin
      for(uint64_t t=100;t<5000;t+=100)m.tick(t,P{310,300,30},false);
      auto* r=w.at(g,0);CHECK(r&&r->position[0]==77.7f);
      for(uint8_t k=0;k<Groups;++k)CHECK(w.count(k)==1); }
    // A mod marker left in a save comes back stamped after a reload: removed once the feature is armed; the player's
    // pins in the same group stay.
    { World w;w.set(stamp({500,500,5}),4,0);w.set(stamp({600,600,5}),3,0);w.set({1.1f,2.2f,3},2,0);
      Markers m(w.api(),logger);m.tick(0,std::nullopt,false);CHECK(w.count(4)==1);   // off: untouched
      m.enable(true);m.tick(10,std::nullopt,false);CHECK(w.count(4)==0&&w.count(3)==0&&w.count(2)==1); }
    // A reload while connected: the same stamped marker is back with a new id; the mod removes it and shows its own.
    { World w;Markers m(w.api(),logger);m.enable(true);m.tick(0,P{40,40,4},false);
      auto saved=*w.at(4,0);w.r[saved.index].used=false;w.set(saved.position,4,0);   // reloaded from the save
      for(uint64_t t=100;t<3000;t+=100)m.tick(t,P{40,40,4},false);
      CHECK(w.count(4)==1); }
    // Key: one ping per press, not per frame held, and not more than once a second.
    { KeyEdge k;unsigned n=0;for(uint64_t t=0;t<500;t+=16)n+=k.pressed(true,t);CHECK(n==1);
      n+=k.pressed(false,600);n+=k.pressed(true,700);CHECK(n==1);n+=k.pressed(false,800);n+=k.pressed(true,1100);CHECK(n==2); }
    // readRecords on raw memory: the manager pointer, the block at +0x5ef8 and 50 records of 0x50 bytes.
    { std::vector<uint8_t> mem(0x10000);uint64_t base=0x400000;
      auto put=[&](uint64_t a,const void* v,size_t n){std::memcpy(mem.data()+(a-base),v,n);};
      uint64_t slot=base+0x10,manager=base+0x100,block=base+0x7000;put(slot,&manager,8);put(manager+0x5ef8-0x0,&block,8);
      mod::discovery::Sampler s([&](uint64_t a,void* p,size_t n){if(a<base||a+n>base+mem.size())return false;std::memcpy(p,mem.data()+(a-base),n);return true;},base);
      CHECK(readRecords(s,slot).empty());   // marker types not 6: refused
      for(unsigned i=0;i<Records;++i)mem[(block-base)+i*RecordSize+0x2d]=UserMarkerType;
      uint8_t* r=mem.data()+(block-base)+7*RecordSize;r[0x2c]=1;float p[3]{1.5f,2.5f,3.5f};std::memcpy(r+0x10,p,12);uint64_t id=42;std::memcpy(r+0x20,&id,8);r[0x4c]=4;r[0x4d]=0;
      auto rs=readRecords(s,slot);CHECK(rs.size()==Records&&rs[7].used&&rs[7].id==42&&rs[7].group==4&&rs[7].position[1]==2.5f&&!rs[6].used);
      r[0x4c]=9;CHECK(readRecords(s,slot).empty());   // impossible group: refused
      manager=0;put(slot,&manager,8);CHECK(readRecords(s,slot).empty()); }
    if(failures)return 1;
    std::puts("marker tests passed");
}
