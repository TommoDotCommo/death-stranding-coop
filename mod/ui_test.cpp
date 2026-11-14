#include "ui.h"
#include <cstdio>
#include <cstring>
#include <map>
using namespace mod::ui;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"ui oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    // Sparse fake memory: only bytes that were written can be read.
    const uint64_t base=0x140000000,heap=0x9000000;std::map<uint64_t,uint8_t> mem;
    auto put=[&](uint64_t a,const void* v,size_t n){for(size_t i=0;i<n;++i)mem[a+i]=static_cast<const uint8_t*>(v)[i];};
    auto q=[&](uint64_t a,uint64_t v){put(a,&v,8);};auto d=[&](uint64_t a,uint32_t v){put(a,&v,4);};
    mod::discovery::Sampler s{[&](uint64_t a,void* o,size_t n){
        for(size_t i=0;i<n;++i){auto it=mem.find(a+i);if(it==mem.end())return false;static_cast<uint8_t*>(o)[i]=it->second;}return true;},base};
    const uint64_t state=heap+0x100,menu=heap+0x1000,pages=heap+0x2000,layers=heap+0x3000,resource=heap+0x4000,page=heap+0x6000,text=heap+0x7000,list=heap+0x8000;
    auto zero=[&](uint64_t a,size_t n){for(size_t i=0;i<n;++i)mem[a+i]=0;};
    // Nothing there yet: everything unknown, and unknown matches nothing, negated or not.
    auto none=read(s,base,true);
    CHECK(!none.flagsKnown&&!none.menuKnown&&none.kind==Kind::Unknown);
    CHECK(describe(none)=="unknown flags=unknown page=unknown world=1 item=unknown");
    CHECK(!matches(none,"gameplay")&&!matches(none,"!paused")&&!matches(none,"!PauseMenu")&&!matches(none,"page:none")&&!matches(none,"!page:any"));
    // The state object.
    q(base+GameState,state);q(state,base+GameStateVtable);q(state+FlagsNow,0);q(state+FlagsNext,0);
    auto play=read(s,base,true);
    CHECK(play.flagsKnown&&play.kind==Kind::Gameplay&&!play.menuKnown);
    CHECK(read(s,base,false).kind==Kind::Frontend);
    CHECK(matches(play,"gameplay")&&matches(play,"!paused")&&matches(play,"!PauseMenu+!Loading")&&!matches(play,"page:none"));
    // Kinds by priority; the flags' names.
    auto kind=[&](uint64_t f){q(state+FlagsNow,f);return read(s,base,true).kind;};
    CHECK(kind(bit(2))==Kind::Paused&&kind(bit(28))==Kind::Paused&&kind(bit(0))==Kind::Menu&&kind(bit(4))==Kind::Menu&&kind(bit(7))==Kind::Menu);
    CHECK(kind(bit(6))==Kind::Cutscene&&kind(bit(31+22))==Kind::Cutscene&&kind(bit(22))==Kind::Loading&&kind(bit(3))==Kind::Busy);
    CHECK(kind(bit(6)|bit(2))==Kind::Paused);                 // a paused cutscene is paused
    CHECK(kind(bit(0)|bit(2))==Kind::Paused&&kind(bit(2)|bit(22))==Kind::Loading);
    CHECK(kind(bit(31)|bit(31+4))==Kind::Gameplay);           // night, path recording: not a screen
    CHECK(flagNames(bit(2)|bit(6))=="PauseMenu+Cutscene"&&flagNames(0)=="-"&&flagNames(bit(31+15))=="SignboardMenu"&&flagNames(bit(63))=="bit63");
    q(state+FlagsNow,bit(0));q(state+FlagsNext,bit(0)|bit(2));
    auto term=read(s,base,true);
    CHECK(describe(term)=="menu flags=TerminalOperation next=TerminalOperation+PauseMenu page=unknown world=1 item=unknown");
    CHECK(matches(term,"TerminalOperation")&&matches(term,"menu+TerminalOperation")&&!matches(term,"menu+PauseMenu")&&matches(term,"paused|menu")&&!matches(term,"!menu"));
    // A state object of another class is not read.
    q(state,base+GameStateVtable+8);CHECK(!read(s,base,true).flagsKnown&&read(s,base,true).kind==Kind::Unknown);q(state,base+GameStateVtable);
    // The menu: no page open.
    zero(menu,0x130);zero(pages,0x60);zero(layers,0xa0);zero(page,0x240);
    q(base+Menu,menu);q(menu,base+MenuVtable);q(menu+MenuResource,resource);d(menu+MenuPhase,0);q(menu+MenuPages,pages);q(menu+MenuLayers,layers);
    q(pages,base+PagesVtable);q(pages+Owner,menu);q(pages+PagesCurrent,0);
    q(layers,base+LayersVtable);d(layers+LayerCountA,0);d(layers+LayerCountB,0);q(layers+LayerArrayA,0);q(layers+LayerArrayB,0);
    q(resource,base+ResourceVtable);for(unsigned i=0;i<PageCount;++i)q(resource+PageTable+8*i,heap+0x10000+0x100*i);
    q(state+FlagsNow,0);q(state+FlagsNext,0);
    auto closed=read(s,base,true);
    CHECK(closed.menuKnown&&!closed.page&&closed.pageId==-1);
    CHECK(describe(closed)=="gameplay flags=- page=none phase=0 layers=0/0 world=1 item=unknown");
    CHECK(matches(closed,"gameplay+page:none")&&!matches(closed,"page:any")&&matches(closed,"!page:3"));
    // The pause menu's page: slot 3, with its own name and identity.
    q(resource+PageTable+8*3,page);q(pages+PagesCurrent,page);q(state+FlagsNow,bit(2));q(state+FlagsNext,bit(2));d(layers+LayerCountA,1);
    uint8_t identity[16];for(int i=0;i<16;++i)identity[i]=uint8_t(0xa0+i);put(page+PageIdentity,identity,16);
    for(int i=0;i<256;++i)mem[text+uint64_t(i)]='A';   // more characters behind the name
    const char name[]="InGamePauseMenuPage";q(page+PageName,text);d(text-8,uint32_t(sizeof name-1));put(text,name,sizeof name-1);
    auto paused=read(s,base,true);
    CHECK(paused.pageId==3&&paused.pageName==name&&paused.pageIdentity[15]==0xaf&&paused.layerA==1);
    CHECK(describe(paused)=="paused flags=PauseMenu page=3:InGamePauseMenuPage phase=0 layers=1/0 world=1 item=unknown");
    CHECK(matches(paused,"page:3")&&matches(paused,"page:InGamePauseMenu")&&matches(paused,"page:InGamePauseMenuPage")&&matches(paused,"paused+page:any")&&!matches(paused,"page:DeviceMenu")&&!matches(paused,"page:none"));
    // A page that is in no slot keeps its name; an unreadable or absurd name is left out.
    q(pages+PagesCurrent,page+0x800);
    auto other=read(s,base,true);CHECK(other.menuKnown&&other.page&&other.pageId==-1&&other.pageName.empty());
    CHECK(describe(other).find("page=other:?")!=std::string::npos&&matches(other,"page:any")&&!matches(other,"page:3"));
    q(pages+PagesCurrent,page);
    d(text-8,200);CHECK(read(s,base,true).pageName.empty()&&read(s,base,true).pageId==3);d(text-8,uint32_t(sizeof name-1));
    mem[text+2]=' ';CHECK(read(s,base,true).pageName.empty());mem[text+2]='G';
    // Guards: each object's class, the page manager's owner, the resource's class, the layer counts.
    q(menu,base+MenuVtable+8);CHECK(!read(s,base,true).menuKnown&&read(s,base,true).flagsKnown);q(menu,base+MenuVtable);
    q(pages,base+PagesVtable+8);CHECK(!read(s,base,true).menuKnown);q(pages,base+PagesVtable);
    q(pages+Owner,menu+8);CHECK(!read(s,base,true).menuKnown);q(pages+Owner,menu);
    q(layers,base+LayersVtable+8);CHECK(!read(s,base,true).menuKnown);q(layers,base+LayersVtable);
    d(layers+LayerCountB,5000);CHECK(!read(s,base,true).menuKnown);d(layers+LayerCountB,0);
    q(resource,base+ResourceVtable+8);{auto r=read(s,base,true);CHECK(r.menuKnown&&r.page==page&&r.pageId==-1&&r.pageName==name);}q(resource,base+ResourceVtable);
    CHECK(read(s,base,true).pageId==3);
    // Expressions: the form is checked without a state.
    for(const char* good:{"gameplay","!paused","PauseMenu","menu+TerminalOperation|paused","page:none","page:any","page:3","page:DeviceMenu","!page:Some_Page1","gameplay+!page:any|loading"})
        CHECK(validExpression(good));
    for(const char* bad:{"","play","pausemenu","unknown","menu+","|menu","menu||paused","page:","page:a b","page:123","page:19","!!menu","menu,paused","Gameplay"})
        CHECK(!validExpression(bad));
    CHECK(!validExpression(std::string(121,'a')));
    // The change log: one line per change.
    Watch w;CHECK(w.due(0)&&w.changed(paused,1000)&&!w.due(1100)&&w.due(1250)&&!w.changed(paused,1250)&&w.changed(closed,1500)&&w.text()==describe(closed));
    // The dump never fails, with or without a menu.
    q(list,page+0x40);q(layers+LayerArrayA,list);
    auto rows=dump(s,base);CHECK(rows.size()>8&&rows[0].find("UI dump state: paused")==0);
    bool sawIdentity=false;for(auto& r:rows)sawIdentity=sawIdentity||r=="UI dump page identity a0a1a2a3a4a5a6a7a8a9aaabacadaeaf";CHECK(sawIdentity);
    q(base+Menu,0);CHECK(dump(s,base).size()==2);
    if(!failures)std::puts("ui state, page, guard, expression and log checks passed");
    return failures?1:0;
}
