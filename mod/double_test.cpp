#include "double.h"
#include "fake_memory.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace mod::puppet;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"double oracle: %s\n",#x);++failures;}}while(0)
// The game's side: one parked double owned by one player entity.
struct Scene {
    Anchor anchor;uint64_t twin=0x7000,found=0x7000,flags=FlagSleeping|0x551;mod::Pose pose;
    bool ignorePlace=false,ignoreWake=false,alterLocal=false;Transform mover{{0,0,0},{1,0,0,0,1,0,0,0,1}};double speed=-1;unsigned animations=0;uint32_t actions=0xffffffff;uint64_t network=0x500,upgradedFor=0;unsigned downgrades=0;bool effectsShown=true;unsigned effectCalls=0;
    std::vector<std::string> calls,events;
    Scene(){anchor.entity=0x1000;anchor.pose.available=anchor.pose.coherent=true;anchor.pose.position={10,20,30};anchor.pose.rotation={1,0,0,0,1,0,0,0,1};
        flags&=~FlagVisible;pose.available=pose.coherent=true;pose.rotation={1,0,0,0,1,0,0,0,1};}
    void call(const char* name,uint64_t e){CHECK(e==twin);calls.push_back(name);if(alterLocal)anchor.entity=0x999;}
    DoubleApi api(){return {[this]{return anchor;},[this](uint64_t p){return p==anchor.entity?found:0;},[this](uint64_t){return flags;},[this](uint64_t){return pose;},
        [this](uint64_t e,bool b){call(b?"sleep":"wake",e);if(!ignoreWake)flags=b?flags|FlagSleeping:flags&~FlagSleeping;},
        [this](uint64_t e,bool b){call(b?"show":"hide",e);flags=b?flags|FlagVisible:flags&~FlagVisible;},
        [this](uint64_t e,const Transform& t){call("place",e);if(!ignorePlace)pose.position=t.position;},
        [this](uint64_t e,const Transform& t){CHECK(e==twin);mover=t;},
        [this](uint64_t e,double v,uint32_t f){CHECK(e==twin);speed=v;actions=f;++animations;},
        [this](uint64_t e,uint64_t player){CHECK(e==twin);upgradedFor=player;auto old=network;network=0x600;return old;},
        [this](uint64_t e,uint64_t original){CHECK(e==twin&&original==0x500);network=original;++downgrades;},
        [this](uint64_t e,bool show){CHECK(e==twin);effectsShown=show;++effectCalls;}};}
    // One engine frame: an awake double is put where its FixedPositionMover says.
    void frame(){if(!(flags&FlagSleeping))pose.position=mover.position;}
    Double make(){Double d(api(),[this](const std::string& row){events.push_back(row);});return d;}
    bool last(const char* event) const {return !events.empty()&&events.back().find(std::string("\"")+event+"\"")!=std::string::npos;}
};
Transform at(double x){return {{x,2,3},{1,0,0,0,1,0,0,0,1}};}
using Calls=std::vector<std::string>;
void lifecycle(){
    Scene s;auto d=s.make();auto t=at(5);
    d.tick(true,&t);CHECK(s.calls.empty());                       // off until enabled
    d.enable();d.tick(false,&t);CHECK(s.calls.empty());           // only on the permitted game thread
    d.tick(true,nullptr);CHECK(s.calls.empty());                  // nothing to show
    d.tick(true,&t);CHECK((s.calls==Calls{"place","show","wake"}));CHECK(d.stage()==Double::Stage::Driving&&s.last("acquired"));
    auto u=at(6);s.frame();d.tick(true,&u);CHECK(s.calls.size()==3&&s.mover.position[0]==6);   // no teleport while driving
    s.frame();CHECK(s.pose.position[0]==6);
    d.tick(true,nullptr);CHECK((Calls(s.calls.begin()+3,s.calls.end())==Calls{"place","hide","sleep"}));
    CHECK(d.stage()==Double::Stage::Parked&&s.pose.position[0]==0&&(s.flags&FlagSleeping)&&!(s.flags&FlagVisible)&&s.last("released"));
    d.tick(true,&t);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==9);   // can be taken again
}
void mover(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(s.mover.position[0]==5);
    for(double x:{6.,7.,8.}){s.frame();auto u=at(x);d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.mover.position[0]==x);}
    d.tick(true,nullptr);CHECK(s.mover.position[0]==0&&d.stage()==Double::Stage::Parked);s.frame();CHECK(s.pose.position[0]==0);
}
void animation(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t,2.5);CHECK(s.speed==2.5&&s.animations==1);
    auto near=[](double a,double b){return std::fabs(a-b)<1e-9;};
    auto u=at(6);d.tick(true,&u,0);CHECK(near(s.speed,1.5));s.frame();d.tick(true,&u,NAN);CHECK(near(s.speed,.9));   // smoothed, NaN counts as 0
    s.frame();d.tick(true,&u,1e9);CHECK(near(s.speed,.6*.9+.4*20));                                                 // capped at 20
    s.frame();d.tick(true,&u,3);CHECK(near(s.speed,.6*(.6*.9+8)+1.2));s.frame();d.tick(true,nullptr,3);CHECK(s.speed==0&&d.stage()==Double::Stage::Parked);   // stands again when released
}
void network(){
    {Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(s.network==0x500&&!s.upgradedFor);}   // off by default
    for(int ending=0;ending<3;++ending){
        Scene s;auto d=s.make();d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
        CHECK(s.upgradedFor==s.anchor.entity&&s.network==0x600&&d.stage()==Double::Stage::Driving);
        if(ending==0){d.tick(true,nullptr);CHECK(s.network==0x500&&s.downgrades==1);}             // release
        if(ending==1){s.pose.position[0]=40;auto u=at(6);d.tick(true,&u);CHECK(s.network==0x500&&d.stage()==Double::Stage::Yielded&&s.mover.position[0]==0);}   // game takes it
        if(ending==2){s.anchor.entity=0x2000;s.found=0x7000;d.tick(true,&t);CHECK(s.downgrades==0);}   // world gone: nothing to restore
    }
}
void refusedUpgrade(){
    Scene s;auto api=s.api();api.upgrade=[](uint64_t,uint64_t)->uint64_t{throw std::runtime_error("layout");};
    Double d(api,[&](const std::string& r){s.events.push_back(r);});d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
    CHECK(d.stage()==Double::Stage::Driving);d.tick(true,nullptr);CHECK(s.downgrades==0&&d.stage()==Double::Stage::Parked);
}
void actions(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t,3,FlagSprintBit|FlagCrouchBit|0x20|JumpBit|(1u<<21));
    CHECK(s.actions==(FlagSprintBit|FlagCrouchBit|JumpBit));               // only the replayed bits pass
    s.frame();auto u=at(6);d.tick(true,&u,3,FlagCrouchBit);CHECK(s.actions==FlagCrouchBit);
    d.tick(true,nullptr,3,FlagSprintBit);CHECK(s.actions==0&&s.speed==0);  // released standing
    // Reading the porter's own variables: entity 0x6000 -> components -> manager -> named slots.
    fakemem::Fake f;auto B=f.base;
    f.put<uint32_t>(0x6090,2);f.put<uint64_t>(0x6098,B+0x7000);f.put<uint64_t>(0x7000,B+0x7100);f.put<uint64_t>(0x7008,B+0x7200);
    f.put<uint64_t>(0x7100,B+0x1f00);f.put<uint64_t>(0x7200,B+0x1f40);f.put<uint64_t>(0x7248,B+0x6000);   // second is the animator
    f.put<uint32_t>(0x7250,3);f.put<uint64_t>(0x7258,B+0x8000);
    const char* names[]={"Speed","Sprint","Is_Crouch","Is_Jump"};
    f.put<uint32_t>(0x7250,4);
    for(unsigned i=0;i<4;++i){f.put<uint64_t>(0x8000+48*i,B+0x9000+32*i);memcpy(f.data.data()+0x9000+32*i,names[i],strlen(names[i])+1);}
    auto sm=f.sampler();ActionCache c;
    CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==0&&c.index[0]==1&&c.index[1]==2&&c.index[2]==3&&c.index[3]==-1);
    f.data[0x8000+48+0x20]=1;CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==FlagSprintBit);
    f.data[0x8000+96+0x20]=1;f.data[0x8000+144+0x20]=1;CHECK(readActions(sm,B+0x6000,B+0x1f40,c)==(FlagSprintBit|FlagCrouchBit|JumpBit));
    CHECK(readActions(sm,B+0x6000,B+0x1f00,c)==0);                         // wrong animator vtable: nothing
    f.put<uint64_t>(0x7248,B+0x6100);ActionCache d2;CHECK(readActions(sm,B+0x6000,B+0x1f40,d2)==0);   // not owned by this entity
}
void effects(){
    for(int ending=0;ending<3;++ending){
        Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);CHECK(!s.effectsShown&&s.effectCalls==1);
        s.frame();auto u=at(6);d.tick(true,&u);CHECK(!s.effectsShown&&s.effectCalls==2);   // re-hidden every driving tick
        if(ending==0){s.frame();d.tick(true,nullptr);}
        if(ending==1){s.pose.position[0]=40;d.tick(true,&u);}
        if(ending==2){s.anchor.entity=0x2000;s.found=0x7000;d.tick(true,&t);CHECK(!s.effectsShown&&s.effectCalls==2);continue;}   // gone with the world
        CHECK(s.effectsShown&&s.effectCalls==3);
    }
}
void cargoCommand(){
    CHECK(parseCargoCommand("create 519 S1\n","S1")==519u);
    CHECK(parseCargoCommand("give 7 S1\n","S1","give")==7u);CHECK(parseCargoCommand("give 125983117 S1\n","S1","give")==125983117u);CHECK(!parseCargoCommand("give 7 S1\n","S1"));CHECK(!parseCargoCommand("create 7 S1\n","S1","give"));
    for(const char* bad:{"create 519 S2\n","create 519\n","create -1 S1\n","create 4294967296 S1\n","create 5x S1\n","delete 519 S1\n","create 519 S1 x\n",""})
        CHECK(!parseCargoCommand(bad,"S1"));
    CHECK(parseCargoCommand("delete 519 S1\n","S1","delete")==519u);CHECK(!parseCargoCommand("create 519 S1\n","S1","delete"));
    CHECK(parseCargoCommand("construct 26 S1\n","S1","construct")==26u);
    CHECK(parseCargoCommand("list 0 S1\n","S1","list")==0u);CHECK(!parseCargoCommand("list 0 S2\n","S1","list"));
}
void ownBody(){
    struct World{uint64_t made=0;unsigned creates=0,parks=0,removes=0;bool refuse=false,dead=false;uint64_t resource=0x500;};
    auto w=std::make_shared<World>();
    OwnBodyApi api;api.resource=[w](uint64_t){return w->resource;};
    api.create=[w](uint64_t r,const Transform&){CHECK(r==0x500);++w->creates;if(w->refuse)return uint64_t(0);w->dead=false;return w->made=0x9000+w->creates;};
    api.alive=[w](uint64_t b){return b==w->made&&!w->dead;};api.park=[w](uint64_t){++w->parks;};api.remove=[w](uint64_t){++w->removes;};
    auto dressed=std::make_shared<unsigned>(0);api.dress=[w,dressed](uint64_t b){CHECK(b==w->made&&w->parks<w->creates);++*dressed;return true;};   // before parking
    std::vector<std::string> rows;OwnBody o(api,[&](const std::string& r){rows.push_back(r);});
    Anchor porter;porter.entity=0x100;porter.pose.available=true;porter.pose.coherent=true;porter.pose.rotation={1,0,0,0,1,0,0,0,1};
    Anchor none;CHECK(o.tick(none)==0&&w->creates==0);                                   // no world yet: nothing made
    auto b=o.tick(porter);CHECK(b==0x9001&&w->creates==1&&w->parks==1&&*dressed==1);     // made once, dressed, parked
    CHECK(o.tick(porter)==b&&w->creates==1&&w->parks==1&&*dressed==1);                   // kept
    w->dead=true;CHECK(o.tick(porter)==0x9002&&w->creates==2&&w->parks==2);              // lost: made again
    Anchor next=porter;next.entity=0x200;CHECK(o.tick(next)==0x9003&&w->creates==3);     // new world: made again
    o.remove();CHECK(w->removes==1&&o.body()==0);
    // Refusals: given up after MaxFailures; the caller falls back to the resident double.
    w->refuse=true;OwnBody r(api,[&](const std::string&){});Anchor p2=porter;p2.entity=0x300;
    for(unsigned i=0;i<10;++i)CHECK(r.tick(p2)==0);CHECK(r.exhausted()&&w->creates==3+OwnBody::MaxFailures);
    // No resident double resource (yet): not counted as a failure.
    auto x=std::make_shared<World>();x->resource=0;OwnBodyApi a2=api;a2.resource=[x](uint64_t){return x->resource;};
    OwnBody n(a2,[&](const std::string&){});for(unsigned i=0;i<5;++i)CHECK(n.tick(porter)==0);CHECK(!n.exhausted());
    // The porter not dressed yet (right after a load): nothing made until he is, logged once; bounded wait.
    {auto ready=std::make_shared<bool>(false);OwnBodyApi a3=api;a3.ready=[ready](uint64_t){return *ready;};
     std::vector<std::string> lines;OwnBody g(a3,[&](const std::string& r){lines.push_back(r);});auto before=w->creates;w->refuse=false;
     for(unsigned i=0;i<20;++i)CHECK(g.tick(porter)==0);CHECK(w->creates==before&&lines.size()==1&&lines[0].find("waiting")!=std::string::npos);
     *ready=true;CHECK(g.tick(porter)!=0&&w->creates==before+1);
     OwnBody h(a3,[&](const std::string&){});*ready=false;unsigned made=0;
     for(unsigned i=0;i<=OwnBody::ReadyWaitTicks+1;++i)made+=h.tick(porter)!=0;CHECK(made>0&&w->creates==before+2);}
}
void ownCommand(){
    CHECK(parseOwnCommand("own S1\n"," S1\n")==DressArtPartsInPlace);CHECK(parseOwnCommand("own9 S1\n"," S1\n")==9u);
    CHECK(parseOwnCommand("own0 S1\n"," S1\n")==0u);CHECK(parseOwnCommand("own10 S1\n"," S1\n")==0x10u);
    CHECK(parseOwnCommand("own4a S1\n"," S1\n")==0x4au);
    for(const char* bad:{"ownA S1\n","own1A S1\n","own100 S1\n","own10 S2\n","owng S1\n","own S1","peer S1\n","own-1 S1\n",""})
        CHECK(!parseOwnCommand(bad," S1\n"));
}
// A made body (0xb000) and the resident double (0xa000) in fake memory; the double wears the 52-part art-parts resource
// 0xa400, which its model resource 0xa200 builds; the made body wears its entity resource's one-part 0xb400.
struct Wardrobe {
    fakemem::Fake f;uint64_t B=f.base,art=B+0x1f40;unsigned setModels=0,applies=0;
    void component(size_t at,uint64_t vt,size_t owner,uint64_t resource){f.put<uint64_t>(at,vt);f.put<uint64_t>(at+0x48,B+owner);f.put<uint64_t>(at+0x30,resource);}
    void list(size_t entity,size_t at,std::vector<size_t> items){f.put<uint32_t>(entity+0x90,uint32_t(items.size()));f.put<uint64_t>(entity+0x98,B+at);
        for(size_t i=0;i<items.size();++i)f.put<uint64_t>(at+8*i,B+items[i]);}
    void artParts(size_t at,size_t owner,size_t resource,uint32_t parts,size_t array){component(at,art,owner,B+resource);f.put<uint32_t>(at+0x100,parts);f.put<uint64_t>(at+0x108,B+array);}
    Wardrobe(){
        component(0xa100,B+0x1f50,0xa000,B+0xa200);f.put<uint64_t>(0xa200+0x30,B+0xa400);            // double: model -> model resource -> 52 parts
        artParts(0xa300,0xa000,0xa400,52,0xa600);f.put<uint64_t>(0xa000+0xb8,B+0xa100);f.put<uint64_t>(0xa000+0x180,B+0xa300);
        list(0xa000,0xa900,{0xa100,0xa300});
        for(unsigned i=0;i<52;++i)f.put<uint64_t>(0xa600+8*i,B+0xa800+0x40*(i==20));
        f.data[0xa840+0x2c]=0x0d;
        f.put<uint64_t>(0xa400+0xe0,B+0xa500);f.put<int32_t>(0xa400+0xec,3);                        // name map {used, hash, index}
        f.put<uint32_t>(0xa500,0);f.put<uint32_t>(0xa50c,1);f.put<uint32_t>(0xa510,0x11111111);f.put<uint32_t>(0xa514,7);
        f.put<uint32_t>(0xa518,1);f.put<uint32_t>(0xa51c,0x2020abcd);f.put<uint32_t>(0xa520,20);
        f.put<uint32_t>(0xa500+8,20);                                                                // an unused slot naming 20: ignored
        component(0xb100,B+0x1f50,0xb000,B+0xb200);artParts(0xb300,0xb000,0xb400,1,0xb680);             // made body
        f.put<uint64_t>(0xb000+0xb8,B+0xb100);f.put<uint64_t>(0xb000+0x180,B+0xb300);list(0xb000,0xb800,{0xb100,0xb300});
    }
    // The engine's SetModel as read statically: old ArtPartsData and model removed, a new model and an ArtPartsData built
    // from ModelResource+30 attached and cached at +b8 / +180.
    ModelSwapCalls calls(){
        ModelSwapCalls c;
        c.setModel=[this](uint64_t body,uint64_t model){CHECK(body==B+0xb000);++setModels;uint64_t builds=0;std::memcpy(&builds,f.data.data()+(model-B)+0x30,8);
            component(0xb500,B+0x1f50,0xb000,model);f.put<uint64_t>(0xb000+0xb8,B+0xb500);
            artParts(0xb600,0xb000,builds-B,builds==B+0xa400?52:1,0xa600);f.put<uint64_t>(0xb000+0x180,B+0xb600);list(0xb000,0xb800,{0xb500,0xb600});};
        c.apply=[this](uint64_t body,uint64_t resource){CHECK(body==B+0xb000&&resource==B+0xa400);++applies;
            artParts(0xb700,0xb000,0xa400,52,0xa600);list(0xb000,0xb800,{0xb500,0xb600,0xb700});};
        return c;
    }
    bool dress(unsigned mask,std::vector<std::string>* rows=nullptr){auto s=f.sampler();
        return dressByModel(s,B+0xb000,B+0xa000,art,mask,calls(),[rows](const std::string& r){if(rows)rows->push_back(r);});}
    // The engine's in-place replacement 235d320 as read statically: the primary ArtPartsData is replaced by one built from
    // the resource and cached at +180; the model component stays. keepOld: a broken engine that adds instead.
    unsigned inPlaces=0;bool keepOld=false;
    bool inPlace(std::vector<std::string>* rows=nullptr,uint64_t body=0,uint64_t resident=0,uint64_t porter=0){auto s=f.sampler();
        return dressInPlace(s,body?body:B+0xb000,resident?resident:B+0xa000,art,[this](uint64_t b,uint64_t resource){
            CHECK(b==B+0xb000);++inPlaces;artParts(0xb600,0xb000,resource-B,resource==B+0xa400?52:1,0xa600);f.put<uint64_t>(0xb000+0x180,B+0xb600);
            if(keepOld)list(0xb000,0xb800,{0xb100,0xb300,0xb600});else list(0xb000,0xb800,{0xb100,0xb600});},
            [rows](const std::string& r){if(rows)rows->push_back(r);},porter);}
};
// Mask 0x80: as live, the double's model resource builds the one-part art parts (the body's) while the double wears the
// 52-part ones that the porter's streaming control gave it.
void inPlaceSwap(){
    {Wardrobe w;w.f.put<uint64_t>(0xa200+0x30,w.B+0xb400);auto s=w.f.sampler();
     auto p=planArtPartsSwap(s,w.B+0xb000,w.B+0xa000,w.art);CHECK(p.wanted==w.B+0xa400&&p.wears==w.B+0xb400);
     std::vector<std::string> rows;CHECK(w.inPlace(&rows)&&w.inPlaces==1);
     auto t=w.f.sampler();CHECK(dressedLike(t,w.B+0xb000,w.art,w.B+0xa400));
     auto made=describeBody(t,"made",w.B+0xb000,w.art);CHECK(made.find("parts 52")!=std::string::npos&&made.find("parts 1\n")==std::string::npos);
     CHECK(!rows.empty()&&rows.back().find("dressed like the double")!=std::string::npos);
     // Once dressed, a repeat calls nothing.
     CHECK(!w.inPlace()&&w.inPlaces==1);}
    // The double not dressed yet (it wears the made body's one-part set, live after a load): the porter's set is used.
    {Wardrobe w;w.f.put<uint64_t>(0xa300+0x30,w.B+0xb400);
     w.artParts(0xd300,0xd000,0xa400,52,0xa600);w.f.put<uint64_t>(0xd000+0x180,w.B+0xd300);w.list(0xd000,0xd800,{0xd300});
     CHECK(!w.inPlace()&&w.inPlaces==0);                                      // without the porter: refused as before
     std::vector<std::string> rows;CHECK(w.inPlace(&rows,0,0,w.B+0xd000)&&w.inPlaces==1);
     auto t=w.f.sampler();CHECK(dressedLike(t,w.B+0xb000,w.art,w.B+0xa400));
     bool said=false;for(auto& r:rows)said|=r.find("using the porter's art parts")!=std::string::npos;CHECK(said);}
    // A body that already carries a second ArtPartsData (mask 8's apply): refused, nothing called.
    {Wardrobe w;w.artParts(0xb700,0xb000,0xa400,52,0xa600);w.list(0xb000,0xb800,{0xb100,0xb300,0xb700});
     std::vector<std::string> rows;CHECK(!w.inPlace(&rows)&&w.inPlaces==0);CHECK(!rows.empty()&&rows.back().find("more than one")!=std::string::npos);}
    // The double's art parts not its own, or no double, or the double itself: refused.
    {Wardrobe w;w.f.put<uint64_t>(0xa300+0x48,w.B+0xb000);CHECK(!w.inPlace()&&w.inPlaces==0);}
    {Wardrobe w;CHECK(!w.inPlace(nullptr,0,0x1)&&w.inPlaces==0);CHECK(!w.inPlace(nullptr,w.B+0xb000,w.B+0xb000)&&w.inPlaces==0);}
    // The body's primary ArtPartsData not its own: refused.
    {Wardrobe w;w.f.put<uint64_t>(0xb300+0x48,w.B+0xa000);CHECK(!w.inPlace()&&w.inPlaces==0);}
    // An engine that adds instead of replacing is not reported as dressed.
    {Wardrobe w;w.keepOld=true;CHECK(!w.inPlace()&&w.inPlaces==1);}
    // The entity the streaming control dresses: global -> object, object+30 -> entity+20.
    {Wardrobe w;auto s=w.f.sampler();CHECK(readStreamingDouble(s,w.B+0xc000)==0);
     w.f.put<uint64_t>(0xc000,w.B+0xc100);auto t=w.f.sampler();CHECK(readStreamingDouble(t,w.B+0xc000)==0);
     w.f.put<uint64_t>(0xc100+0x30,w.B+0xa020);auto u=w.f.sampler();CHECK(readStreamingDouble(u,w.B+0xc000)==w.B+0xa000);
     CHECK(readStreamingDouble(u,w.B+0xfffff0)==0);}
    // The new bit is its own: own80 and ownc0 parse, the default stays 8.
    CHECK(parseOwnCommand("own80 S1\n"," S1\n")==DressArtPartsInPlace);CHECK(parseOwnCommand("ownc0 S1\n"," S1\n")==(DressArtPartsInPlace|DressReport));
    CHECK(parseOwnCommand("own S1\n"," S1\n")==DressArtPartsInPlace);   // the default own dress is the in-place swap
}
void modelSwap(){
    {Wardrobe w;auto s=w.f.sampler();
     auto p=planModelSwap(s,w.B+0xb000,w.B+0xa000,w.art,true);CHECK(p.model==w.B+0xa200&&p.builds==w.B+0xa400&&p.wanted==w.B+0xa400);
     CHECK(!dressedLike(s,w.B+0xb000,w.art,w.B+0xa400));CHECK(dressedLike(s,w.B+0xa000,w.art,w.B+0xa400));
     CHECK(!planModelSwap(s,w.B+0xa000,w.B+0xa000,w.art,true).model);                                // the double itself: no
     CHECK(!planModelSwap(s,w.B+0xb000,0,w.art,true).model);}
    // 0x10: one SetModel, never an apply; the body ends with one ArtPartsData, the double's.
    {Wardrobe w;std::vector<std::string> rows;CHECK(w.dress(DressModelExact,&rows)&&w.setModels==1&&w.applies==0);
     auto s=w.f.sampler();CHECK(dressedLike(s,w.B+0xb000,w.art,w.B+0xa400));CHECK(!rows.empty()&&rows.back().find("dressed like the double")!=std::string::npos);}
    // 0x10 when the double's model builds other art parts than it wears: nothing called.
    {Wardrobe w;w.f.put<uint64_t>(0xa200+0x30,w.B+0xb400);CHECK(!w.dress(DressModelExact)&&w.setModels==0&&w.applies==0);
     auto s=w.f.sampler();CHECK(planModelSwap(s,w.B+0xb000,w.B+0xa000,w.art,false).model==w.B+0xa200);}
    // The double's model must be its own: a foreign model component refuses.
    {Wardrobe w;w.f.put<uint64_t>(0xa100+0x48,w.B+0xb000);CHECK(!w.dress(DressModelExact)&&w.setModels==0);
     CHECK(!w.dress(DressModelThenApply)&&w.setModels==0);}
    // The double's model builds no art parts: refused in both modes.
    {Wardrobe w;w.f.put<uint64_t>(0xa200+0x30,0);CHECK(!w.dress(DressModelThenApply)&&w.setModels==0&&w.applies==0);}
    // 0x20 when the swap already dresses the body: no apply (a second same-resource apply would remove the swap's
    // ArtPartsData with the plain removal that hangs).
    {Wardrobe w;CHECK(w.dress(DressModelThenApply)&&w.setModels==1&&w.applies==0);}
    // 0x20 when the model builds other art parts: swap, then apply; two ArtPartsData remain, so not dressed like the double.
    {Wardrobe w;w.f.put<uint64_t>(0xa200+0x30,w.B+0xb400);CHECK(!w.dress(DressModelThenApply)&&w.setModels==1&&w.applies==1);}
    // Both bits: the exact rule wins.
    {Wardrobe w;w.f.put<uint64_t>(0xa200+0x30,w.B+0xb400);CHECK(!w.dress(DressModelExact|DressModelThenApply)&&w.setModels==0&&w.applies==0);}
    // dressedLike: the double's ArtPartsData cached at +180 but the one-part one still attached (mask 8 then a removal
    // that did not happen): not dressed.
    {Wardrobe w;w.artParts(0xb600,0xb000,0xa400,52,0xa600);w.f.put<uint64_t>(0xb000+0x180,w.B+0xb600);w.list(0xb000,0xb800,{0xb100,0xb300,0xb600});
     auto s=w.f.sampler();CHECK(!dressedLike(s,w.B+0xb000,w.art,w.B+0xa400));
     w.list(0xb000,0xb800,{0xb100,0xb600});auto t=w.f.sampler();CHECK(dressedLike(t,w.B+0xb000,w.art,w.B+0xa400));
     w.f.put<uint64_t>(0xb600+0x48,w.B+0xa000);auto u=w.f.sampler();CHECK(!dressedLike(u,w.B+0xb000,w.art,w.B+0xa400));}   // not its own
}
void bodyReport(){
    Wardrobe w;auto s=w.f.sampler();auto text=describeBody(s,"double",w.B+0xa000,w.art);
    CHECK(text.find("components 2")!=std::string::npos&&text.find("parts 52")!=std::string::npos);
    CHECK(text.find("part20 state 0xd")!=std::string::npos&&text.find("part20 hash 0x2020abcd")!=std::string::npos);
    CHECK(text.find("vtable +0x1f40")!=std::string::npos&&text.find("foreign")==std::string::npos);
    w.f.put<uint64_t>(0xa100+0x48,w.B+0xb000);auto t=w.f.sampler();CHECK(describeBody(t,"double",w.B+0xa000,w.art).find("foreign")!=std::string::npos);
    auto made=describeBody(s,"made",w.B+0xb000,w.art);CHECK(made.find("parts 1")!=std::string::npos&&made.find("part20")==std::string::npos);
    CHECK(describeBody(s,"none",0,w.art).find("unreadable")!=std::string::npos);
}
void privateRoom(){
    Scene s;auto d=s.make();
    CHECK(!d.gameUsesDouble(s.anchor.entity));                                       // parked: no
    s.flags&=~FlagSleeping;CHECK(d.gameUsesDouble(s.anchor.entity));                 // woken by the game: yes
    s.flags|=FlagSleeping;d.enable();auto t=at(5);d.tick(true,&t);CHECK(!d.gameUsesDouble(s.anchor.entity));   // ours
    s.pose.position[0]=40;auto u=at(6);d.tick(true,&u);CHECK(d.stage()==Double::Stage::Yielded&&d.gameUsesDouble(s.anchor.entity));
    Scene z;z.flags&=~FlagSleeping;auto e=z.make();CHECK(!e.gameUsesDouble(0));         // no porter: nothing to say
}
void busy(){
    Scene s;s.flags|=FlagVisible;auto d=s.make();d.enable();auto t=at(5);
    d.tick(true,&t);CHECK(s.calls.empty()&&d.stage()==Double::Stage::Parked);  // the game is showing it: hands off
    Scene a;a.flags&=~FlagSleeping;auto e=a.make();e.enable();e.tick(true,&t);CHECK(a.calls.empty());
    Scene b;b.found=0;auto f=b.make();f.enable();f.tick(true,&t);CHECK(b.calls.empty());
}
void yields(bool move){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    if(move)s.pose.position[0]=40;else s.flags|=FlagSleeping;     // the game uses its double
    auto u=at(6);d.tick(true,&u);d.tick(true,&u);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Yielded&&s.events.size()>=2);
    CHECK(s.mover.position[0]==0&&s.mover.position[1]==0);   // the mover is back at the resting transform
    d.tick(true,nullptr);CHECK(s.calls.size()==3);
    s.flags=(s.flags|FlagSleeping)&~FlagVisible;d.tick(true,&u);CHECK(d.stage()==Double::Stage::Parked&&s.calls.size()==3);
    d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==6);
}
void releasedByGame(){
    // The game hides the double while we hold it and the partner leaves: nothing is called on the double except the
    // restores, and the mover target goes back to rest.
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);s.flags|=FlagSleeping;s.flags&=~FlagVisible;
    d.tick(true,nullptr);CHECK(s.mover.position[0]==0&&d.stage()==Double::Stage::Parked&&s.calls.size()==3);
}
void transient(){
    // One unreadable pose with the same porter: no forget, no calls; the double keeps being driven afterwards.
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    s.anchor.pose.available=false;auto u=at(6);d.tick(true,&u);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Driving&&!s.last("world_changed"));
    s.anchor.pose.available=true;d.tick(true,&u);CHECK(d.stage()==Double::Stage::Driving&&s.mover.position[0]==6);
    s.anchor.pose.available=false;d.tick(true,nullptr);CHECK(d.stage()==Double::Stage::Driving&&s.calls.size()==3);   // release waits for a readable pose
    s.anchor.pose.available=true;s.frame();d.tick(true,nullptr);CHECK(d.stage()==Double::Stage::Parked&&s.calls.back()=="sleep");
}
void identity(){
    Scene s;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    s.found=0x7100;d.tick(true,&t);CHECK(s.calls.size()==3&&d.stage()==Double::Stage::Parked&&s.last("double_changed"));
    Scene w;auto e=w.make();e.enable();e.tick(true,&t);
    w.anchor.entity=0x2000;w.found=0x7000;e.tick(true,&t);CHECK(w.calls.size()==3&&w.last("world_changed"));
    e.tick(true,nullptr);CHECK(w.calls.size()==3);
}
void unconfirmed(){
    Scene s;s.ignoreWake=true;auto d=s.make();d.enable();auto t=at(5);d.tick(true,&t);
    CHECK((s.calls==Calls{"place","show","wake","place","hide","sleep"}));CHECK(d.stage()==Double::Stage::Failed&&!(s.flags&FlagVisible));
    d.tick(true,&t);d.tick(true,nullptr);CHECK(s.calls.size()==6);
    // The game stops following the mover target (it took the double): the next tick yields with no further calls.
    Scene p;auto e=p.make();e.enable();e.tick(true,&t);auto u=at(9);e.tick(true,&u);e.tick(true,&u);
    CHECK(e.stage()==Double::Stage::Yielded&&p.calls.size()==3);
    Scene l;l.alterLocal=true;auto f=l.make();f.enable();bool threw=false;try{f.tick(true,&t);}catch(const std::exception&){threw=true;}
    CHECK(threw&&f.stage()==Double::Stage::Failed);
}
void tree(){
    // player 0x6000 -> other child 0x6800, camera 0x9000 -> camera 0x9400, double 0x9800
    auto build=[](fakemem::Fake& f){auto B=f.base;
        auto link=[&](size_t e,uint64_t vt,uint64_t parent,uint64_t first,uint64_t next){f.put<uint64_t>(e,vt);f.put<uint64_t>(e+0x70,parent);f.put<uint64_t>(e+0x78,first);f.put<uint64_t>(e+0x80,next);};
        f.put<uint64_t>(0x6078,B+0x6800);link(0x6800,B+0x1f00,B+0x6000,0,B+0x9000);
        link(0x9000,B+0x1f10,B+0x6000,B+0x9400,0);link(0x9400,B+0x1f10,B+0x9000,0,B+0x9800);link(0x9800,B+0x1f20,B+0x9000,0,0);};
    fakemem::Fake f;build(f);auto s=f.sampler();auto B=f.base;
    CHECK(findDouble(s,B+0x6000,B+0x1f10,B+0x1f20)==B+0x9800);
    CHECK(findDouble(s,B+0x6000,B+0x1f10,B+0x1f30)==0);
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9870,B+0x6000);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);}   // disowned
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9880,B+0xa000);g.put<uint64_t>(0xa000,B+0x1f20);g.put<uint64_t>(0xa070,B+0x9000);auto t=g.sampler();
        CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);}                                                                          // two doubles
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9880,B+0x9400);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);} // sibling loop
    {fakemem::Fake g;build(g);g.put<uint64_t>(0x9000,B+0x1f00);auto t=g.sampler();CHECK(findDouble(t,B+0x6000,B+0x1f10,B+0x1f20)==0);} // no camera
}
void conversion(){
    for(double yaw:{0.,1.,-2.5,3.}){auto t=fromState({4,5,6},yaw);
        CHECK(t.position[0]==4&&t.position[1]==6&&t.position[2]==5);
        CHECK(std::fabs(std::atan2(t.rotation[3],t.rotation[4])-yaw)<1e-6&&t.rotation[8]==1);
        double det=t.rotation[0]*(t.rotation[4]*t.rotation[8]-t.rotation[5]*t.rotation[7])-t.rotation[1]*(t.rotation[3]*t.rotation[8]-t.rotation[5]*t.rotation[6])+t.rotation[2]*(t.rotation[3]*t.rotation[7]-t.rotation[4]*t.rotation[6]);
        CHECK(std::fabs(det-1)<1e-6);}
    bool threw=false;try{fromState({NAN,0,0},0);}catch(const std::exception&){threw=true;}CHECK(threw);
}
void hold(){
    // A partner who drops out briefly: the double hides in place and stays borrowed (no second upgrade), then reappears.
    Scene s;auto d=s.make();d.enable();d.walk(true);auto t=at(5);d.tick(true,&t);
    s.frame();d.tick(true,nullptr,0,0,true);
    CHECK(d.stage()==Double::Stage::Hidden&&!(s.flags&FlagVisible)&&(s.flags&FlagSleeping)&&s.network==0x600&&s.downgrades==0&&!s.effectsShown&&s.last("hidden"));
    CHECK(!d.gameUsesDouble(s.anchor.entity));
    for(int i=0;i<5;++i)d.tick(true,nullptr,0,0,true);
    CHECK(d.stage()==Double::Stage::Hidden&&s.downgrades==0);
    auto u=at(9);d.tick(true,&u,0,0,true);
    CHECK(d.stage()==Double::Stage::Driving&&(s.flags&FlagVisible)&&!(s.flags&FlagSleeping)&&s.pose.position[0]==9&&s.mover.position[0]==9&&s.last("shown"));
    CHECK(s.upgradedFor==s.anchor.entity&&s.downgrades==0);   // the same borrowed network, never rebuilt
    // Hidden, then handed back for real (event or mode off): everything restored.
    s.frame();d.tick(true,nullptr,0,0,true);d.tick(true,nullptr,0,0,false);
    CHECK(d.stage()==Double::Stage::Parked&&s.network==0x500&&s.downgrades==1&&s.effectsShown&&s.mover.position[0]==0&&s.last("released"));
    // Hidden, and the game takes its double (private room): yield, everything restored, no calls of ours.
    Scene g;auto e=g.make();e.enable();e.walk(true);e.tick(true,&t);g.frame();e.tick(true,nullptr,0,0,true);
    auto n=g.calls.size();g.flags&=~FlagSleeping;g.flags|=FlagVisible;
    e.tick(true,nullptr,0,0,true);CHECK(e.stage()==Double::Stage::Yielded&&g.network==0x500&&g.mover.position[0]==0&&g.calls.size()==n&&e.gameUsesDouble(g.anchor.entity));
    // Hidden, and the double changes: forgotten, nothing touched.
    Scene c;auto f=c.make();f.enable();f.tick(true,&t);c.frame();f.tick(true,nullptr,0,0,true);n=c.calls.size();
    c.found=0x7100;f.tick(true,&t,0,0,true);CHECK(f.stage()==Double::Stage::Parked&&c.calls.size()==n&&c.last("double_changed"));
}
int main(){
    lifecycle();mover();animation();network();refusedUpgrade();actions();effects();releasedByGame();cargoCommand();privateRoom();ownBody();ownCommand();modelSwap();inPlaceSwap();bodyReport();
    try{transient();}catch(const std::exception& e){std::fprintf(stderr,"double oracle: transient threw %s\n",e.what());++failures;}
    hold();busy();yields(true);yields(false);identity();unconfirmed();tree();conversion();
    if(!failures)std::puts("double borrow, yield, release and pointer-path checks passed");
    return failures?1:0;
}
