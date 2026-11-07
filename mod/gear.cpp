#include "gear.h"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
namespace mod::gear {
namespace {
[[maybe_unused]] void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){v=0;return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
// A helper name: a NUL-terminated printable C string of 1..MaxHelper characters.
std::string name(const discovery::Sampler& s,uint64_t at){
    char text[MaxHelper+1]{};if(!at||at>=0x800000000000ULL||!s.read(at,text,sizeof text))return {};
    std::string out;
    for(char c:text){if(!c)return out;if(c<32||c>126)return {};out+=c;}
    return {};   // no terminator within MaxHelper
}
}
std::vector<Node> snapshot(const discovery::Sampler& s,uint64_t porter){
    std::vector<Node> out;
    struct Frame{uint64_t entity;int parent;unsigned depth;};
    // Depth first, parents before children, siblings in list order: a stack of (first child, parent index).
    std::vector<Frame> todo;uint64_t first=0;
    auto push=[&](uint64_t from,int parent,unsigned depth){
        std::vector<Frame> kids;uint64_t c=0;if(!q(s,from+FirstChild,c))return;
        for(unsigned n=0;c&&n<MaxSiblings;++n){kids.push_back({c,parent,depth});if(!q(s,c+NextSibling,c))break;}
        for(auto k=kids.rbegin();k!=kids.rend();++k)todo.push_back(*k);
    };
    (void)first;push(porter,-1,1);
    while(!todo.empty()&&out.size()<MaxNodes){
        auto f=todo.back();todo.pop_back();
        Node n;n.entity=f.entity;n.parent=f.parent;
        if(!q(s,f.entity,n.vtable)||!q(s,f.entity+Flags,n.flags))continue;
        uint64_t mover=0;if(q(s,f.entity+Mover,mover)&&mover&&q(s,mover,n.moverVtable)){uint64_t text=0;if(q(s,mover+HelperName,text))n.helper=name(s,text);}
        out.push_back(n);
        if(f.depth<MaxDepth)push(f.entity,int(out.size()-1),f.depth+1);
    }
    return out;
}
std::vector<Piece> plan(const std::vector<Node>& nodes,uint64_t image){
    std::vector<Piece> out;std::map<int,int> copied;   // node index -> plan index
    for(int i=0;i<int(nodes.size())&&out.size()<MaxPieces;++i){
        auto& n=nodes[i];
        bool kind=n.vtable==image+EntityVtable||n.vtable==image+BackpackVtable;
#ifdef MUTANT_GEAR_CARGO
        kind=true;
#endif
        bool mover=n.moverVtable==image+HelperMoverVtable||n.moverVtable==image+BackpackMoverVtable;
#ifndef MUTANT_GEAR_HIDDEN
        bool shown=n.flags&FlagVisible;
#else
        bool shown=true;
#endif
        auto parent=copied.find(n.parent);
        // Only the porter's own children: a copy made from a piece's resource brings that resource's children with it
        // (live 2026-11-08: the backpack brings its pouches), so a nested piece would be there twice.
#ifndef MUTANT_GEAR_NESTED
        bool rooted=n.parent<0;
#else
        bool rooted=n.parent<0||parent!=copied.end();
#endif
        if(!kind||!mover||!shown||n.helper.empty()||!rooted)continue;
        copied[i]=int(out.size());out.push_back({n.entity,n.parent<0||parent==copied.end()?-1:parent->second,n.helper});
    }
    return out;
}
EmptyCarrier::EmptyCarrier(){
#ifndef MUTANT_GEAR_SLOTS
    int32_t count=int32_t(CarrierSlotTypes);
#else
    int32_t count=int32_t(CarrierSlotTypes)-1;
#endif
    uint64_t array=reinterpret_cast<uint64_t>(slots);
    std::memcpy(carrier+0x10,&count,4);std::memcpy(carrier+0x18,&array,8);
    for(unsigned i=0;i<CarrierSlotTypes;++i){slot[i][1]=uint8_t(i);slots[i]=reinterpret_cast<uint64_t>(slot[i]);}
    static_assert(CarrierLock+LockSize<=sizeof carrier&&SlotLock+LockSize<=sizeof slot[0],"lock outside the carrier memory");
#ifdef _WIN32
    static_assert(sizeof(CRITICAL_SECTION)==LockSize,"CRITICAL_SECTION size");
#ifndef MUTANT_GEAR_LOCKS
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(carrier+CarrierLock));
    for(auto& s:slot)InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(s+SlotLock));
#endif
#endif
}
uint64_t carrierComponent(const discovery::Sampler& s,uint64_t entity){
    uint32_t n=0;uint64_t array=0;
    if(!entity||!s.read(entity+ComponentCount,&n,4)||!q(s,entity+ComponentArray,array)||!array)return 0;
    for(uint32_t i=0;i<n&&i<MaxComponents;++i){uint64_t c=0,v=0;
        if(q(s,array+8*i,c)&&c&&q(s,c,v)&&v==s.base+CarrierComponentVtable)return c;}
    return 0;
}
size_t Mirror::made() const {size_t n=0;for(auto& m:made_)n+=m.entity!=0;return n;}
void Mirror::clear(){
    unsigned removed=0;
    for(auto& m:made_)if(m.entity&&api_.alive(m.entity)){api_.remove(m.entity);++removed;}
    if(body_)log_("GEAR removed "+std::to_string(removed)+" pieces\n");
    made_.clear();pieces_.clear();body_=0;next_=0;
}
void Mirror::tick(uint64_t body,uint64_t world,const std::function<std::vector<Piece>()>& pieces,const puppet::Transform& at,bool shown){
    if(world!=world_){
        // The old world's entities went with it: forget the copies without touching them.
        if(world_&&made())log_("GEAR new world: "+std::to_string(made())+" old pieces forgotten\n");
#ifndef MUTANT_GEAR_STALE_WORLD
        made_.clear();pieces_.clear();body_=0;next_=0;
#endif
        world_=world;failures_=0;
    }
    if(stopped_)return;
    if(body!=body_&&body_)clear();
    if(!body)return;
    if(!body_){
        body_=body;next_=0;pieces_=pieces();made_.assign(pieces_.size(),{});
        log_("GEAR plan: "+std::to_string(pieces_.size())+" visible pieces of the porter's gear\n");
    }
    for(unsigned k=0;k<PerTick&&next_<pieces_.size()&&failures_<MaxFailures;++k,++next_){
        auto& p=pieces_[next_];
        uint64_t parent=p.parent<0?body:made_.at(size_t(p.parent)).entity;
        if(!parent)continue;   // its parent was not made
        uint64_t resource=api_.resource(p.source);
        if(!resource){log_("GEAR skipped "+p.helper+": no resource\n");continue;}
        uint64_t e=api_.create(resource,at);
        if(!e){++failures_;log_("GEAR refused "+p.helper+(failures_>=MaxFailures?" (giving up)\n":"\n"));continue;}
        api_.attach(e,parent,p.helper);
        api_.visible(e,shown);made_[next_]={e,shown};
        if(next_+1==pieces_.size())log_("GEAR "+std::to_string(made())+" pieces on the body\n");
    }
    for(auto& m:made_)if(m.entity&&m.shown!=shown){
#ifndef MUTANT_GEAR_VISIBILITY
        api_.visible(m.entity,shown);
#endif
        m.shown=shown;
    }
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native gear API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    const std::array<Entry,5> entries={{
        // Entity resource getter (entity+58), EntityResource_ExportedCreateEntity, Entity_ExportedSetVisible,
        // Entity_ExportedRemove (as puppet.cpp / double.cpp), and Entity_ExportedSetParentAndAttachToHelper's body.
        {0x2362ba0,{0x48,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3,0x48,0x83,0xc1,0x58,0xe9,0xdf,0xbd,0xa7,0xff}},
        {0x23806a0,{0x48,0x83,0xec,0x58,0x48,0x8b,0x84,0x24,0x98,0,0,0,0x4c,0x8b,0x94,0x24,0x90,0,0,0,0x4c,0x8b,0x9c,0x24,0x88,0,0,0,0xc5,0xf8,0x10,0}},
        {0x2361e90,{0x48,0x85,0xc9,0x0f,0x85,0xa7,0xd7,0xff,0xff,0xc3}},
        {0x2361c20,{0xe9,0x7b,0xae,0xfe,0xff}},
        {0x234ceb0,{0x48,0x85,0xc9,0x0f,0x84,0xd8,0,0,0,0x53,0x56,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0x69,0x41,0xc1,0x02,0x48,0x33,0xc4,
                    0x48,0x89,0x44,0x24,0x48,0x48,0x8b,0xfa,0x48,0xc7,0x44,0x24,0x20,0,0,0,0,0x48,0x8b,0xd9,0x49,0x8b,0xd0,0x48,0x8d,0x4c,0x24,0x20,
                    0x41,0x0f,0xb6,0xf1,0xe8,0x11,0xdb,0x5b,0xff}}}};
    std::array<unsigned,5> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"gear code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"gear code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
            ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"gear function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"gear function signature missing/ambiguous");
    using AttachFn=void(*)(uint64_t,uint64_t,const char*,bool);using VisibleFn=void(*)(uint64_t,bool);
    auto get=reinterpret_cast<puppet::GetResourceFn>(s.base+entries[0].rva);auto create=reinterpret_cast<puppet::CreateFn>(s.base+entries[1].rva);
    auto visible=reinterpret_cast<VisibleFn>(s.base+entries[2].rva);auto remove=reinterpret_cast<puppet::RemoveFn>(s.base+entries[3].rva);
    auto attach=reinterpret_cast<AttachFn>(s.base+entries[4].rva);
    // Identity of each made piece: its vtable and resource reference when made.
    struct Made{uint64_t vtable=0,resourceRef=0;};auto made=std::make_shared<std::map<uint64_t,Made>>();
    auto* sp=&s;auto carriers=std::make_shared<std::vector<std::unique_ptr<EmptyCarrier>>>();   // kept for the process
    auto identity=[sp,made](uint64_t e){auto it=made->find(e);uint64_t v=0,r=0;
        return it!=made->end()&&q(*sp,e,v)&&v==it->second.vtable&&q(*sp,e+0x58,r)&&r==it->second.resourceRef;};
    Api a;
    a.resource=[get](uint64_t e){return get(e);};
    a.create=[sp,made,create,remove,carriers](uint64_t resource,const puppet::Transform& t){
        uint64_t vt=0;require(q(*sp,resource,vt)&&sp->region(vt,8,true),"gear resource vtable outside image");
        uint64_t e=puppet::invokeCreate(create,resource,t);
        // A backpack copy needs a carrier before its first update (gear.h, EmptyCarrier).
        uint64_t ev=0;
        if(e&&q(*sp,e,ev)&&ev==sp->base+BackpackVtable){
            uint64_t component=carrierComponent(*sp,e),current=1;
            if(component&&q(*sp,component+ComponentCarrier,current)&&!current){
                carriers->push_back(std::make_unique<EmptyCarrier>());
                *reinterpret_cast<uint64_t*>(component+ComponentCarrier)=carriers->back()->address();
            } else if(!component){
                // A backpack without the component the update needs: removed at once rather than left to fault.
                puppet::invokeRemove(remove,e);return uint64_t(0);
            }
        }
        if(e){Made m;require(q(*sp,e,m.vtable)&&sp->region(m.vtable,8,true),"gear piece vtable outside image");q(*sp,e+0x58,m.resourceRef);(*made)[e]=m;}
        return e;};
    a.attach=[identity,attach](uint64_t e,uint64_t parent,const std::string& helper){require(identity(e),"gear piece identity changed");attach(e,parent,helper.c_str(),false);};
    a.visible=[identity,visible](uint64_t e,bool on){if(identity(e))visible(e,on);};
    a.remove=[identity,made,remove](uint64_t e){if(identity(e)){puppet::invokeRemove(remove,e);made->erase(e);}};
    a.alive=identity;
    return a;
#endif
}
}
