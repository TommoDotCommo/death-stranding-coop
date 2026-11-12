// The partner's gear (gear.h): the porter's child tree read from fake memory laid out like the live porter of
// 2026-11-08 (visible cuffs and backpack with pouches and a carried corpse; hidden armour; the Odradek), the plan of
// pieces worth copying, and the mirror that makes, attaches, shows/hides and removes the copies in a fake world.
#include "gear.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace mod::gear;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"gear oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr uint64_t Image=0x140000000,CargoVtable=0x3dd8cc8,OdradekVtable=0x3da2ee8,OtherMover=0x3deddd8;
struct Mem {
    uint64_t base=0x10000000;std::vector<unsigned char> mem=std::vector<unsigned char>(0x100000);uint64_t next=0x1000;
    template<class T> void put(uint64_t a,T v){std::memcpy(mem.data()+(a-base),&v,sizeof v);}
    uint64_t alloc(size_t n){uint64_t a=base+next;next+=(n+15)&~size_t(15);return a;}
    mod::discovery::Sampler sampler{[this](uint64_t a,void* p,size_t n){if(a<base||a-base>mem.size()||n>mem.size()-(a-base))return false;std::memcpy(p,mem.data()+(a-base),n);return true;},Image};
    uint64_t entity(uint64_t vtable,bool visible){uint64_t e=alloc(0x100);put(e,Image+vtable);put<uint64_t>(e+Flags,visible?0x8032844453:0x8031850551);return e;}
    void helper(uint64_t e,uint64_t moverVtable,const char* name){uint64_t m=alloc(0xc0),t=alloc(64);put(m,Image+moverVtable);
        std::memcpy(mem.data()+(t-base),name,std::strlen(name)+1);put(m+HelperName,t);put(e+Mover,m);}
    void children(uint64_t parent,const std::vector<uint64_t>& kids){
        put<uint64_t>(parent+FirstChild,kids.empty()?0:kids[0]);
        for(size_t i=0;i<kids.size();++i)put<uint64_t>(kids[i]+NextSibling,i+1<kids.size()?kids[i+1]:0);}
};
struct Porter {
    Mem m;uint64_t porter,armour,armour2,cuff,backpack,pouch,corpse,item,odradek,odradekChild,bare,hiddenPack,hiddenPackChild;
    Porter(){
        porter=m.entity(0x3d7d0d8,true);
        armour=m.entity(EntityVtable,false);m.helper(armour,HelperMoverVtable,"HLP_Waist_l_armor");
        armour2=m.entity(EntityVtable,false);m.helper(armour2,HelperMoverVtable,"HLP_Waist_l_armor");
        cuff=m.entity(EntityVtable,true);m.helper(cuff,HelperMoverVtable,"HLP_Hcuf");
        backpack=m.entity(BackpackVtable,true);m.helper(backpack,BackpackMoverVtable,"HLP_Backpack");
        corpse=m.entity(CargoVtable,true);m.helper(corpse,OtherMover,"HLP_DeadBody_3rd");
        pouch=m.entity(EntityVtable,true);m.helper(pouch,HelperMoverVtable,"HLP_PodPouch_2nd");
        item=m.entity(EntityVtable,true);m.helper(item,HelperMoverVtable,"HLP_Item_c");
        bare=m.entity(EntityVtable,true);   // no mover: not copied
        odradek=m.entity(OdradekVtable,true);m.helper(odradek,HelperMoverVtable,"HLP_Odradek");
        odradekChild=m.entity(EntityVtable,true);m.helper(odradekChild,HelperMoverVtable,"HLP_Oderadek");
        hiddenPack=m.entity(EntityVtable,false);m.helper(hiddenPack,HelperMoverVtable,"HLP_Bbp");
        hiddenPackChild=m.entity(EntityVtable,true);m.helper(hiddenPackChild,HelperMoverVtable,"HLP_DogTag");
        m.children(porter,{armour,cuff,backpack,odradek,bare,hiddenPack,armour2});
        m.children(backpack,{corpse,pouch,item});m.children(odradek,{odradekChild});m.children(hiddenPack,{hiddenPackChild});
    }
};
// A fake engine: entities it made, their parents and helpers, visibility, removals.
struct World {
    struct E{uint64_t resource=0,parent=0;std::string helper;bool visible=true,removed=false;};
    std::map<uint64_t,E> made;uint64_t next=0x70000000;unsigned refuse=0,creates=0,removes=0;std::set<uint64_t> sources;
    Api api(){Api a;
        a.resource=[](uint64_t e){return e^0x5000000000ULL;};
        a.create=[this](uint64_t r,const mod::puppet::Transform&){++creates;if(refuse){--refuse;return uint64_t(0);}uint64_t e=next+=0x400;made[e].resource=r;return e;};
        a.attach=[this](uint64_t e,uint64_t p,const std::string& h){made.at(e).parent=p;made.at(e).helper=h;};
        a.visible=[this](uint64_t e,bool on){made.at(e).visible=on;};
        a.remove=[this](uint64_t e){++removes;made.at(e).removed=true;};
        a.alive=[this](uint64_t e){auto it=made.find(e);return it!=made.end()&&!it->second.removed;};
        return a;}
    const E* byHelper(const std::string& h){for(auto& [k,v]:made)if(v.helper==h&&!v.removed)return &v;return nullptr;}
    uint64_t parentOf(const std::string& h,uint64_t parent=0){for(auto& [k,v]:made)if(v.helper==h&&!v.removed&&(!parent||v.parent==parent))return v.parent;return 0;}
    uint64_t idOf(const std::string& h){for(auto& [k,v]:made)if(v.helper==h&&!v.removed)return k;return 0;}
    unsigned live(){unsigned n=0;for(auto& [k,v]:made)n+=!v.removed;return n;}
};
std::vector<std::string> lines;
}
int main(){
    Porter p;
    auto nodes=snapshot(p.m.sampler,p.porter);
    // Snapshot: depth first, parents before children, helper names read, the porter itself excluded.
    CHECK(nodes.size()==12);
    CHECK(!nodes.empty()&&nodes[0].entity==p.armour&&nodes[0].helper=="HLP_Waist_l_armor"&&nodes[0].parent==-1);
    std::map<uint64_t,int> at;for(int i=0;i<int(nodes.size());++i)at[nodes[i].entity]=i;
    CHECK(at.count(p.pouch)&&nodes[at[p.pouch]].parent==at[p.backpack]&&at[p.backpack]<at[p.pouch]);
    CHECK(at.count(p.bare)&&nodes[at[p.bare]].helper.empty());
    // Cost: one copy per entity and per mover, one per helper name (not one read per field; live, ~1000 reads a
    // second with a full cargo stack). A reader that refuses anything over 8 bytes (each block's end on an unreadable
    // page) gets the same tree field by field.
    {
        auto before=p.m.sampler.reads;auto again=snapshot(p.m.sampler,p.porter);auto cost=p.m.sampler.reads-before;
        CHECK(again.size()==nodes.size()&&cost<=3*nodes.size()+1);
        Porter f;mod::discovery::Sampler narrow{[&f](uint64_t a,void* out,size_t n){if(n>8||a<f.m.base||a-f.m.base+n>f.m.mem.size())return false;
            std::memcpy(out,f.m.mem.data()+(a-f.m.base),n);return true;},Image};
        auto slow=snapshot(narrow,f.porter);CHECK(slow.size()==nodes.size());
        for(size_t i=0;i<slow.size()&&i<nodes.size();++i)CHECK(slow[i].vtable==nodes[i].vtable&&slow[i].flags==nodes[i].flags&&slow[i].parent==nodes[i].parent&&slow[i].moverVtable==nodes[i].moverVtable);
    }
    // Plan: visible cuffs, backpack, pouch and item; not the armour (hidden), the corpse (cargo), the Odradek and
    // its child, the bare entity, or the visible child of a hidden pouch.
    auto pieces=plan(nodes,Image);
    std::vector<std::string> helpers;for(auto& x:pieces)helpers.push_back(x.helper);
    CHECK((helpers==std::vector<std::string>{"HLP_Hcuf","HLP_Backpack"}));
    CHECK(pieces.size()==2&&pieces[0].parent==-1&&pieces[1].parent==-1&&pieces[0].source==p.cuff&&pieces[1].source==p.backpack);
    // The mirror still attaches an explicit nested piece to its parent's copy (used below).
    pieces.push_back({p.pouch,1,"HLP_PodPouch_2nd"});pieces.push_back({p.item,1,"HLP_Item_c"});
    // Bounds: a sibling cycle and a self-parented child end within the caps; an unterminated helper name is empty.
    {
        Porter c;c.m.put<uint64_t>(c.item+NextSibling,c.corpse);c.m.put<uint64_t>(c.pouch+FirstChild,c.pouch);
        auto n=snapshot(c.m.sampler,c.porter);CHECK(n.size()<=MaxNodes);
        Porter u;uint64_t mv=0;std::memcpy(&mv,u.m.mem.data()+(u.cuff+Mover-u.m.base),8);uint64_t t=u.m.alloc(128);
        std::memset(u.m.mem.data()+(t-u.m.base),'A',128);u.m.put(mv+HelperName,t);
        auto un=snapshot(u.m.sampler,u.porter);for(auto& x:un)if(x.entity==u.cuff)CHECK(x.helper.empty());
        CHECK(plan(un,Image).size()==1);
        CHECK(snapshot(u.m.sampler,0).empty());
    }
    // Mirror: two pieces per tick, children attached to the copy of their parent at the same helper.
    World w;Mirror mirror(w.api(),[](const std::string& s){lines.push_back(s);});
    mod::puppet::Transform t{};const uint64_t body=0x60000000,world=p.porter;auto get=[&]{return pieces;};
    const unsigned settle=Mirror::CheckEvery*(Mirror::StableChecks-1)+1;   // ticks until a new body's first plan
    auto ticks=[&](uint64_t b,uint64_t wd,unsigned n,bool shown){for(unsigned i=0;i<n;++i)mirror.tick(b,wd,get,t,shown);};
    ticks(body,world,settle+10,false);CHECK(w.creates==0);   // a hidden (parked) body: nothing read or made
    ticks(body,world,settle-1,true);CHECK(w.creates==0);     // not yet read StableChecks times
    ticks(body,world,1,true);CHECK(w.live()==2);
    ticks(body,world,1,true);CHECK(w.live()==4&&mirror.done());
    CHECK(w.parentOf("HLP_Hcuf")==body&&w.parentOf("HLP_Backpack")==body);
    CHECK(w.idOf("HLP_Backpack")&&w.parentOf("HLP_PodPouch_2nd")==w.idOf("HLP_Backpack"));
    CHECK(w.byHelper("HLP_Item_c")&&w.byHelper("HLP_Item_c")->resource==(p.item^0x5000000000ULL));
    for(auto& [k,v]:w.made)CHECK(v.visible);
    // Visibility follows the body; nothing is made twice.
    ticks(body,world,1,false);for(auto& [k,v]:w.made)CHECK(!v.visible);
    ticks(body,world,1,true);for(auto& [k,v]:w.made)CHECK(v.visible);
    ticks(body,world,Mirror::CheckEvery*(Mirror::StableChecks+2),true);
    CHECK(w.creates==4&&w.live()==4);
    // A new body in the same world: the old copies are removed, new ones made on the new body.
    const uint64_t body2=0x61000000;
    ticks(body2,world,settle+2,true);
    CHECK(w.removes==4&&w.live()==4&&w.parentOf("HLP_Hcuf")==body2);
    // A new world: the old copies belong to the old world and are never touched; the new body gets its own.
    unsigned removes=w.removes;const uint64_t world2=world+0x100000,body3=0x62000000;
    ticks(body3,world2,settle+2,true);
    CHECK(w.removes==removes&&w.parentOf("HLP_Hcuf",body3)==body3);
    CHECK(mirror.made()==4);
    // The body gone: the copies are removed.
    mirror.tick(0,world2,get,t,true);
    CHECK(w.removes==removes+4&&mirror.made()==0);
    // Refusals: the engine refuses three times, the mirror gives up (no endless retries); the parent refused means
    // its children are not made.
    {
        World r;r.refuse=10;Mirror m2(r.api(),[](const std::string& s){lines.push_back(s);});
        std::vector<Piece> flat;for(const char* h:{"HLP_Hcuf","HLP_Ammo","HLP_Boot","HLP_Head","HLP_Bbcord"})flat.push_back({p.cuff,-1,h});
        for(unsigned i=0;i<settle+10;++i)m2.tick(body,world,[&]{return flat;},t,true);
        CHECK(r.creates==3&&r.live()==0);
        World r2;Mirror m3(r2.api(),[](const std::string& s){lines.push_back(s);});
        std::vector<Piece> only={{p.backpack,-1,"HLP_Backpack"},{p.pouch,0,"HLP_PodPouch_2nd"}};
        r2.refuse=1;for(unsigned i=0;i<settle+4;++i)m3.tick(body,world,[&]{return only;},t,true);
        CHECK(r2.creates==1&&r2.live()==0);
    }
    // Stop removes the copies and keeps them away; start brings them back.
    {
        World s;Mirror m4(s.api(),[](const std::string& x){lines.push_back(x);});
        for(unsigned i=0;i<settle+3;++i)m4.tick(body,world,get,t,true);
        CHECK(s.live()==4);
        m4.stop();CHECK(s.live()==0);
        for(unsigned i=0;i<settle+3;++i)m4.tick(body,world,get,t,true);
        CHECK(s.live()==0);
        m4.start();for(unsigned i=0;i<settle+3;++i)m4.tick(body,world,get,t,true);
        CHECK(s.live()==4);
    }
    // Re-plan: the porter's gear read again every CheckEvery ticks; a different set seen StableChecks times in a row
    // replaces the copies; a one-check flicker, an empty set, or a change while the body is hidden does not.
    {
        World r;Mirror m5(r.api(),[](const std::string& x){lines.push_back(x);});
        std::vector<Piece> a={{p.cuff,-1,"HLP_Hcuf"},{p.backpack,-1,"HLP_Backpack"}},b=a,empty;b.push_back({p.armour,-1,"HLP_Boot"});
        std::vector<Piece>* now=&a;auto plan=[&]{return *now;};
        auto run=[&](unsigned ticks,bool shown=true){for(unsigned i=0;i<ticks;++i)m5.tick(body,world,plan,t,shown);};
        run(settle+1);CHECK(r.live()==2&&r.creates==2);
        now=&b;run(Mirror::CheckEvery*(Mirror::StableChecks-1));
        now=&a;run(Mirror::CheckEvery);                       // back before the third check: a flicker
        now=&b;run(Mirror::CheckEvery*(Mirror::StableChecks-1));
        CHECK(r.creates==2&&r.removes==0);
        now=&empty;run(Mirror::CheckEvery*(Mirror::StableChecks+2));
        CHECK(r.creates==2&&r.removes==0&&r.live()==2);
        now=&b;run(Mirror::CheckEvery*(Mirror::StableChecks+2),false);   // hidden: not read
        CHECK(r.creates==2&&r.removes==0);
        run(Mirror::CheckEvery*Mirror::StableChecks+2);
        CHECK(r.removes==0&&r.creates==3&&r.live()==3&&r.parentOf("HLP_Boot")==body);   // only the new piece is made
        run(Mirror::CheckEvery*(Mirror::StableChecks+2));
        CHECK(r.creates==3);                                  // stable again: nothing more
        // The same gear in another order, or handed out as other entities of the same resources: nothing is touched.
        std::vector<Piece> c={b[2],b[0],b[1]};now=&c;run(Mirror::CheckEvery*(Mirror::StableChecks+2));
        CHECK(r.creates==3&&r.removes==0&&r.live()==3);
        // This porter's own piece on a helper becomes another resource (the cuff at a terminal): the copy stays.
        std::vector<Piece> e=c;e[1].source=p.armour;now=&e;run(Mirror::CheckEvery*(Mirror::StableChecks+2));
        CHECK(r.creates==3&&r.removes==0&&r.live()==3);
        // One piece taken off: only its copy goes.
        std::vector<Piece> d={b[1],b[2]};now=&d;run(Mirror::CheckEvery*(Mirror::StableChecks+2));
        CHECK(r.creates==3&&r.removes==1&&r.live()==2&&!r.byHelper("HLP_Hcuf")&&r.byHelper("HLP_Boot"));
    }
    // The partner's own gear: identities (helper hash, ordinal among same-helper direct children), the plan from the
    // partner's set (pieces this porter hides are copied too; cargo and nested pieces never), and the wire.
    {
        auto w=worn(nodes,Image);
        CHECK((w==WornSet{{helperHash("HLP_Hcuf"),0},{helperHash("HLP_Backpack"),0}}));
        WornSet theirs{{helperHash("HLP_Backpack"),0},{helperHash("HLP_Waist_l_armor"),1},{helperHash("HLP_DeadBody_3rd"),0},{helperHash("HLP_PodPouch_2nd"),0}};
        auto pf=planFor(nodes,Image,theirs);
        CHECK(pf.size()==2&&pf[0].source==p.backpack&&pf[1].source==p.armour2&&pf[1].helper=="HLP_Waist_l_armor");
        CHECK(planFor(nodes,Image,{}).empty());
        CHECK(describe(theirs,nodes).find("HLP_Waist_l_armor#1")!=std::string::npos);
        // Wire round trip, an empty set, and a broken run (another generation in the middle) dropped.
        WornReceiver rx;std::optional<WornSet> got;
        for(auto& e:encode(theirs,5)){CHECK(e.type==GearWireType);auto r=rx.feed(e);if(r)got=r;}
        CHECK(got&&*got==theirs);
        auto one=encode(theirs,6),two=encode(w,7);std::optional<WornSet> mixed;
        for(auto* e:{&one[0],&one[1],&two[1],&one[2],&one[3]})if(auto r=rx.feed(*e))mixed=r;
        CHECK(!mixed);
        got.reset();for(auto& e:encode({},8))if(auto r=rx.feed(e))got=r;CHECK(got&&got->empty());
        ds::CargoEvent junk;junk.type=GearWireType;junk.ref=1ull<<40;CHECK(!rx.feed(junk));
        junk.ref=uint64_t(MaxPieces+1)<<5;CHECK(!rx.feed(junk));
        // Sender: after Stable ms, once; again after a reconnect even when unchanged; a change after MinGap.
        WornSender tx;CHECK(tx.observe(w,true,1000).empty());CHECK(tx.observe(w,true,1000+WornSender::Stable).size()==2);
        CHECK(tx.observe(w,true,5000).empty());
        CHECK(tx.observe(w,false,6000).empty());CHECK(tx.observe(w,true,6100).size()==2);
        CHECK(tx.observe(theirs,true,7000).empty());CHECK(tx.observe(theirs,true,7000+WornSender::Stable).size()==4);
    }
    // The empty carrier a backpack copy gets: the game's slot lookup (2c52010, emulated) finds a slot for every type,
    // and no slot holds a stack or an item; the component search finds the carrier component among others.
    {
        auto c=std::make_unique<EmptyCarrier>();auto* base=reinterpret_cast<const unsigned char*>(c->address());
        auto lookup=[](const unsigned char* carrier,uint8_t type)->const unsigned char*{
            uint64_t array=0;int32_t count=0;std::memcpy(&array,carrier+0x18,8);std::memcpy(&count,carrier+0x10,4);
            for(int32_t i=0;i<count;++i){uint64_t slot=0;std::memcpy(&slot,reinterpret_cast<const void*>(array+8*uint64_t(i)),8);
                if(reinterpret_cast<const unsigned char*>(slot)[1]==type)return reinterpret_cast<const unsigned char*>(slot);}
            return nullptr;};
        for(unsigned t=0;t<CarrierSlotTypes;++t){auto* s=lookup(base,uint8_t(t));CHECK(s);
            if(s){int32_t items=0,stacks=0;std::memcpy(&items,s+4,4);std::memcpy(&stacks,s+0x18,4);CHECK(items==0&&stacks==0);}}
#ifdef _WIN32
        // The game enters the carrier's and each slot's critical section (2c5ce00): they must be usable.
        auto usable=[](unsigned char* at){auto* cs=reinterpret_cast<CRITICAL_SECTION*>(at);
            if(!TryEnterCriticalSection(cs))return false;
            LeaveCriticalSection(cs);return true;};
        CHECK(usable(c->carrier+CarrierLock));
        for(auto& s:c->slot)CHECK(usable(s+SlotLock));
#endif
        Mem m;uint64_t e=m.entity(BackpackVtable,true),arr=m.alloc(32),other=m.alloc(0x60),comp=m.alloc(0x60);
        m.put(other,Image+0x3cfb710);m.put(comp,Image+CarrierComponentVtable);m.put<uint32_t>(e+ComponentCount,2);m.put(e+ComponentArray,arr);
        m.put(arr,other);m.put(arr+8,comp);
        CHECK(carrierComponent(m.sampler,e)==comp);
        m.put<uint32_t>(e+ComponentCount,1);CHECK(carrierComponent(m.sampler,e)==0);
        m.put<uint32_t>(e+ComponentCount,100000);m.put<uint64_t>(arr+8,0);CHECK(carrierComponent(m.sampler,e)==0);
    }
    if(failures)return 1;
    std::puts("gear tests passed");
    return 0;
}
