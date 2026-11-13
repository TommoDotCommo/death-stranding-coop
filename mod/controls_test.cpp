#include "controls.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace mod::controls;
namespace {
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"controls oracle: %s\n",#x);++failures;}}while(0)
unsigned boolIndex(const char* name){for(unsigned i=0;i<BoolCount;++i)if(BoolNames[i]&&!std::strcmp(BoolNames[i],name))return i;std::fprintf(stderr,"controls oracle: no %s\n",name);++failures;return 0;}
unsigned floatIndex(const char* name){for(unsigned i=0;i<FloatCount;++i)if(FloatNames[i]&&!std::strcmp(FloatNames[i],name))return i;std::fprintf(stderr,"controls oracle: no %s\n",name);++failures;return 0;}
using Bits=std::array<uint8_t,ds::AnimationBoolBytes>;using Floats=std::array<float,ds::AnimationFloats>;
Bits bits(std::initializer_list<unsigned> on){Bits b{};for(auto i:on)setBit(b,i,true);return b;}
size_t count(const std::vector<Write>& w,bool isBool,unsigned index,bool trigger){size_t n=0;for(auto& x:w)if(x.isBool==isBool&&x.index==index&&x.trigger==trigger)++n;return n;}
const Write* find(const std::vector<Write>& w,bool isBool,unsigned index){for(auto& x:w)if(x.isBool==isBool&&x.index==index&&!x.trigger)return &x;return nullptr;}
void names(){
    // The game's own enum values (research-pose, the player registration).
    CHECK(boolIndex("Is_Dead")==0x0c&&boolIndex("Is_Drag")==0x12&&boolIndex("Is_TakeABreak")==0x36&&boolIndex("Action_Rewind")==0x29);
    CHECK(floatIndex("Temporary_Float0")==0x10&&floatIndex("TakeABreak_State")==0x2f&&floatIndex("Unbalance_Rate")==0x03);
    CHECK(pulseOnly(boolIndex("Action_Rewind"))&&pulseOnly(boolIndex("Action_Rewind_NoInter"))&&!pulseOnly(boolIndex("Is_TakeABreak"))&&!pulseOnly(boolIndex("Action_Rewind_Long")));
    CHECK(skipBool(boolIndex("Is_Ragdoll"))&&skipBool(boolIndex("Is_AKukan"))&&skipBool(boolIndex("Is_PrivateRoom"))&&!skipBool(boolIndex("Is_Crouch"))&&skipBool(0));
    for(unsigned i=0;i<BoolCount;++i)for(unsigned j=i+1;j<BoolCount;++j)CHECK(!BoolNames[i]||!BoolNames[j]||std::strcmp(BoolNames[i],BoolNames[j]));
    for(unsigned i=0;i<FloatCount;++i)for(unsigned j=0;j<BoolCount;++j)CHECK(!FloatNames[i]||!BoolNames[j]||std::strcmp(FloatNames[i],BoolNames[j]));
}
void gather(){
    Gather g;Floats f{};f[3]=.5f;unsigned rewind=boolIndex("Action_Rewind"),crouch=boolIndex("Is_Crouch");
    g.add(bits({crouch}),f);CHECK(g.ready()&&bit(g.current().held,crouch)&&!bit(g.current().rose,crouch)&&g.current().floats[3]==.5f);   // the first frame is no edge
    g.add(bits({crouch,rewind}),f);g.add(bits({crouch}),f);   // a one-frame pulse between two sends
    CHECK(bit(g.current().rose,rewind)&&!bit(g.current().held,rewind)&&!bit(g.current().rose,crouch));
    g.sent();CHECK(!bit(g.current().rose,rewind));
    g.add(bits({}),f);g.add(bits({crouch}),f);CHECK(bit(g.current().rose,crouch));
}
void planning(){
    unsigned rewind=boolIndex("Action_Rewind"),crouch=boolIndex("Is_Crouch"),rest=boolIndex("Is_TakeABreak"),ragdoll=boolIndex("Is_Ragdoll");
    unsigned unbalance=floatIndex("Unbalance_Rate"),state=floatIndex("TakeABreak_State");
    ds::Animation a;setBit(a.held,crouch,true);setBit(a.held,ragdoll,true);a.floats[unbalance]=.25f;
    auto w=plan(nullptr,a,true);   // first sample: everything, zeros included
    CHECK(find(w,true,crouch)&&find(w,true,crouch)->value==1&&find(w,true,rest)&&find(w,true,rest)->value==0);
    CHECK(find(w,false,unbalance)&&find(w,false,unbalance)->value==.25f&&find(w,false,state));
    CHECK(!find(w,true,ragdoll)&&!count(w,true,rewind,false));   // physics never, pulses never held
    auto b=a;b.floats[unbalance]=.5f;setBit(b.held,rest,true);b.floats[state]=1;setBit(b.rose,rewind,true);
    w=plan(&a,b,true);
    CHECK(w.size()==4&&find(w,false,unbalance)->value==.5f&&find(w,true,rest)->value==1&&find(w,false,state)->value==1&&count(w,true,rewind,true)==1);
    w=plan(&b,b,false);CHECK(w.empty());   // the same sample again: no repeat of its pulse
    // A held control that went on and off between samples is a pulse.
    auto c=b;setBit(c.rose,crouch,true);setBit(c.held,crouch,false);w=plan(&b,c,true);
    CHECK(find(w,true,crouch)&&find(w,true,crouch)->value==0&&count(w,true,crouch,true)==1);
    // A pulse-only control seen held at the sample is still only a pulse.
    auto d=b;setBit(d.held,rewind,true);setBit(d.rose,rewind,true);w=plan(&b,d,true);CHECK(count(w,true,rewind,true)==1&&!find(w,true,rewind));
    w=clear(b);CHECK(find(w,true,rest)&&find(w,true,crouch)&&find(w,false,unbalance)&&find(w,false,state)&&!find(w,true,ragdoll)&&!count(w,true,rewind,false));
    for(auto& x:w)CHECK(x.value==0&&!x.trigger);
}
}
void blending(){
    unsigned unbalance=floatIndex("Unbalance_Rate"),turn=floatIndex("Turn_Heading"),kind=floatIndex("Damage_Type"),temp=floatIndex("Temporary_Float0"),state=floatIndex("TakeABreak_State");
    CHECK(continuous(unbalance)&&continuous(turn)&&angular(turn)&&!angular(unbalance)&&!continuous(kind)&&!continuous(temp)&&!continuous(state));
    ds::AnimationShown s;s.animation.floats[unbalance]=.2f;s.animation.floats[turn]=170;s.animation.floats[kind]=2;s.animation.floats[temp]=90;
    auto same=blend(s);CHECK(same==s.animation);   // nothing to blend towards
    s.between=true;s.fraction=.25f;s.next.floats[unbalance]=.6f;s.next.floats[turn]=-170;s.next.floats[kind]=11;s.next.floats[temp]=-90;
    auto b=blend(s);
    CHECK(std::abs(b.floats[unbalance]-.3f)<1e-6f);
    CHECK(std::abs(b.floats[turn]-175.f)<1e-4f);   // the short way across 180, not through 0
    CHECK(b.floats[kind]==2&&b.floats[temp]==90);    // kinds and the shared Temporary_* step
    s.fraction=1;b=blend(s);CHECK(std::abs(std::remainder(double(b.floats[turn])+170.,360.))<1e-3);
    // A case in the same hand moves between its places; another hand's case does not borrow it.
    s.fraction=.5f;s.animation.handCount=1;s.animation.hands[0]={7,7,{100,0,-200},5};s.next.handCount=2;s.next.hands[0]={8,6,{900,900,900},5};s.next.hands[1]={7,7,{300,40,0},6};
    b=blend(s);CHECK(b.handCount==1&&b.hands[0].position==(std::array<int16_t,3>{200,20,-100})&&b.hands[0].rotation==5);
}
void aggregate(){
    unsigned rewind=boolIndex("Action_Rewind"),rest=boolIndex("Is_TakeABreak"),fall=boolIndex("Is_Fall"),crouch=boolIndex("Is_Crouch");
    ds::Animation a,b=a;setBit(b.held,rest,true);
    auto w=plan(&a,b,false);CHECK(count(w,true,rewind,true)==1);       // an action begins: the game's rewind, even with its pulse missed
    w=plan(&b,b,false);CHECK(count(w,true,rewind,true)==0);           // unchanged: none
    auto c=b;setBit(c.held,fall,true);w=plan(&b,c,false);CHECK(count(w,true,rewind,true)==1);   // another set
    w=plan(&c,a,false);CHECK(count(w,true,rewind,true)==0);           // back to none: the game sends none
    auto d=a;setBit(d.held,crouch,true);w=plan(&a,d,false);CHECK(count(w,true,rewind,true)==0);   // not an action
    auto e=b;setBit(e.rose,rewind,true);w=plan(&a,e,true);CHECK(count(w,true,rewind,true)==1);   // its own pulse: one, not two
}
int main(){
    names();gather();planning();blending();aggregate();
    if(!failures)std::puts("controls gather, plan and clear checks passed");
    return failures?1:0;
}
