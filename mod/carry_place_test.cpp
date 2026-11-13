#include "carry_place.h"
#include "structures.h"
#include <cmath>
#include <cstdio>
using namespace mod::carryplace;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"place oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Transform at(double x,double y,double z,double yaw,double pitch=0,double roll=0){Transform t;t.position={x,y,z};t.rotation=mod::structures::rotation(yaw,pitch,roll);return t;}
static double gap(const Transform& a,const Transform& b){double d=0;for(int k=0;k<3;++k)d=std::fmax(d,std::fabs(a.position[size_t(k)]-b.position[size_t(k)]));for(int k=0;k<9;++k)d=std::fmax(d,std::fabs(double(a.rotation[size_t(k)]-b.rotation[size_t(k)])));return d;}
int main(){
    // A case in a hand as the animation sample carries it: millimetres, the packed rotation, slot and variant in the tag.
    { Placed p;p.item=0xfeed;p.variant=true;p.slot=7;p.rel=at(0.4567,-0.1234,1.0005,0.7,0.3,-1.1);
      auto h=toHeld(p);CHECK(h.item==0xfeed&&h.tag==(7|0x80)&&h.position[0]==457&&h.position[1]==-123&&h.position[2]==1001);
      auto q=fromHeld(h);CHECK(q.item==p.item&&q.variant&&q.slot==7&&gap(q.rel,p.rel)<0.002);
      p.rel.position={9,-9,0};h=toHeld(p);CHECK(h.position[0]==3000&&h.position[1]==-3000);   // clamped to the wire's reach
      p.variant=false;p.slot=6;CHECK(fromHeld(toHeld(p)).slot==6&&!fromHeld(toHeld(p)).variant); }
    // Relative and back: exact for any pair; a case keeps its place on a backpack that stands and faces elsewhere.
    { Transform pack=at(1052.3,411.2,144.9,2.9,0.15,-0.05),box=at(1052.5,411.4,145.0,-1.3,1.2,0.4);
      auto rel=relative(pack,box);CHECK(gap(compose(pack,rel),box)<1e-5);
      Transform other=at(-300,90,12,0.4,-0.3,0.2);auto moved=compose(other,rel);
      CHECK(gap(relative(other,moved),rel)<1e-5);
      // 0.3 m to the pack's right is 0.3 m along its right axis wherever it stands
      Transform right;right.position={0.3,0,0};right.rotation={1,0,0,0,1,0,0,0,1};auto w=compose(pack,right);
      CHECK(std::fabs(w.position[0]-(pack.position[0]+0.3*pack.rotation[0]))<1e-9&&std::fabs(w.position[2]-(pack.position[2]+0.3*pack.rotation[2]))<1e-9); }
    // The packed rotation comes back within a fraction of a degree, for upright, sideways and upside-down cases.
    for(auto a:{at(0,0,0,0),at(0,0,0,3.1),at(0,0,0,1.0,1.5,0),at(0,0,0,-2.0,-1.2,2.9),at(0,0,0,0.3,0,3.14159),at(0,0,0,1.5708,1.5708,1.5708)}){
        auto back=unpackRotation(packRotation(a.rotation));double worst=0;for(int k=0;k<9;++k)worst=std::fmax(worst,std::fabs(double(back[size_t(k)]-a.rotation[size_t(k)])));
        CHECK(worst<0.001);CHECK(packRotation(a.rotation)<(uint64_t(1)<<47)); }
    // The wire: a list comes back as sent (0.1 mm, the variant bit); an empty list is a list; a broken run is dropped.
    { std::vector<Placed> list;for(int i=0;i<5;++i){Placed p;p.item=1000u+unsigned(i);p.variant=i&1;p.rel=at(0.1*i,-0.2,0.3+0.01*i,0.5*i,0.2*i,-0.1*i);list.push_back(p);}
      auto events=encode(list,7);CHECK(events.size()==5);Receiver r;std::optional<std::vector<Placed>> got;
      for(auto& e:events){CHECK(e.ref<(uint64_t(1)<<53)&&std::fabs(e.yaw)<3.15);
          // what the wire does to it: centimetres and 16-bit yaw steps
          auto w=e;for(auto& c:w.position)c=std::lround(c*100)/100.;w.yaw=std::lround(w.yaw*32767/3.14159265358979323846)*3.14159265358979323846/32767;got=r.feed(w);}
      CHECK(got&&got->size()==5);
      if(got&&got->size()==5)for(size_t i=0;i<5;++i){CHECK((*got)[i].item==list[i].item&&(*got)[i].variant==list[i].variant);CHECK(gap((*got)[i].rel,list[i].rel)<0.001);}
      Receiver e2;auto none=e2.feed(encode({},3)[0]);CHECK(none&&none->empty());
      Receiver b;CHECK(!b.feed(events[0]));CHECK(!b.feed(events[2]));CHECK(!b.feed(events[3]));CHECK(!b.feed(events[4]));   // one lost: nothing comes out
      Receiver d;bool out=false;for(int i:{0,1,1,2,3})out=out||bool(d.feed(events[size_t(i)]));CHECK(!out);   // one twice: not a list
      ds::CargoEvent other;other.type=6;CHECK(!b.feed(other)); }
    // The sender: nothing while the cases move, once when they have come to rest somewhere new, again for a new partner.
    { auto list=[](double x){std::vector<Placed> v;Placed p;p.item=5;p.rel=at(x,0,0.3,0);v.push_back(p);return v;};
      Sender s;CHECK(s.observe(list(0),true,0).empty());                       // first look: not yet seen at rest
      CHECK(s.observe(list(0.001),true,300).size()==1);                        // at rest: sent
      CHECK(s.observe(list(0.002),true,5000).empty());                         // the same place: nothing
      CHECK(s.observe(list(0.2),true,6000).empty());CHECK(s.observe(list(0.4),true,6300).empty());   // moving
      CHECK(s.observe(list(0.401),true,6600).size()==1);                       // at rest somewhere else
      CHECK(s.observe(list(0.6),true,6700).empty());CHECK(s.observe(list(0.6),true,6900).empty());   // too soon after the last
      CHECK(s.observe(list(0.6),true,9700).size()==1);
      CHECK(s.observe(list(0.6),false,9500).empty());CHECK(s.observe(list(0.6),true,12000).size()==1);   // a new connection
      CHECK(s.observe(std::nullopt,true,13000).empty());
      auto none=s.observe({std::vector<Placed>{}},true,20000);CHECK(none.empty());CHECK(s.observe({std::vector<Placed>{}},true,20300).size()==1); }   // put down: an empty list
    // A cart's cases travel as their own type: the back's receiver leaves them alone, the cart's takes them.
    { std::vector<Placed> list(2);list[0].item=7;list[1].item=8;list[1].rel=at(0.5,1.2,0.4,0.3);
      auto ev=encode(list,4,CartWireType);CHECK(ev.size()==2&&ev[0].type==CartWireType&&ev[1].type==CartWireType);
      Receiver back,cart(CartWireType);CHECK(!back.feed(ev[0])&&!back.feed(ev[1]));
      CHECK(!cart.feed(ev[0]));auto got=cart.feed(ev[1]);CHECK(got&&got->size()==2&&(*got)[1].item==8&&gap((*got)[1].rel,list[1].rel)<0.002);
      Sender s(CartWireType);s.observe(list,true,0);auto out=s.observe(list,true,300);CHECK(out.size()==2&&out[0].type==CartWireType); }
    if(failures){std::fprintf(stderr,"place oracle: %d failures\n",failures);return 1;}
    std::puts("carry placement ok");return 0;
}
