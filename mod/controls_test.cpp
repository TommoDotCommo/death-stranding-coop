#include "controls.h"
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
int main(){
    names();gather();planning();
    if(!failures)std::puts("controls gather, plan and clear checks passed");
    return failures?1:0;
}
