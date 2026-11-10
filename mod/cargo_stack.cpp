#include "cargo_stack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
namespace mod::stack {
double height(uint8_t volume){return volume==3?0.396:volume==2?0.264:0.132;}
std::vector<Transform> place(const Transform& a,const Layout& l,const std::vector<uint8_t>& volumes){
    unsigned columns=std::max(1u,std::min(l.columns,8u));std::vector<double> filled(columns,0.0);std::vector<Transform> out;
    for(auto v:volumes){
        unsigned column=0;
#ifndef MUTANT_STACK_COLUMN
        for(unsigned c=1;c<columns;++c)if(filled[c]<filled[column]-1e-9)column=c;   // the lowest column, leftmost on a tie
#endif
        double centred=column-(columns-1)/2.0,h=height(v),centre=filled[column]+h/2;filled[column]+=h;
        Transform t=a;
        for(int i=0;i<3;++i){double local=l.offset[i]+centre*l.up[i]+centred*l.across[i];
            for(int c=0;c<3;++c)t.position[c]+=local*double(a.rotation[3*i+c]);}
        out.push_back(t);
    }
    return out;
}
void Stack::tick(bool permitted,const std::vector<Item>& wanted,const Transform* anchor){
    if(!permitted||!anchor){if(!meshes_.empty())clear();return;}
    std::vector<Item> items(wanted.begin(),wanted.begin()+long(std::min<size_t>(wanted.size(),MaxCases)));
    // Keep the meshes whose item is still at their place; release from the first change up (top first).
    size_t keep=0;
#ifndef MUTANT_STACK_EXTRA
    while(keep<meshes_.size()&&keep<items.size()&&meshes_[keep].item==items[keep])++keep;
#else
    keep=meshes_.size();
#endif
    while(meshes_.size()>keep){api_.release(meshes_.back().mesh);meshes_.pop_back();}
    for(unsigned n=0;n<PerTick&&meshes_.size()<items.size()&&failures_<MaxFailures;++n){
        Item item=items[meshes_.size()];
        auto known=cases_.find(item);
        if(known==cases_.end()){known=cases_.emplace(item,api_.caseFor(item.id,item.variant)).first;
            if(auto& c=known->second){char row[160];std::snprintf(row,sizeof row,"STACK item %u%s: model %llx look %llx volume %u\n",item.id,item.variant?" (variant)":"",
                (unsigned long long)c->model,(unsigned long long)c->look,unsigned(c->volume));log_(row);}}
        if(!known->second||!known->second->model){++failures_;cases_.erase(known);
            log_(failures_>=MaxFailures?"STACK no case model (giving up)\n":"STACK no case model\n");break;}
        std::vector<uint8_t> volumes;for(auto& m:meshes_)volumes.push_back(m.volume);volumes.push_back(known->second->volume);
        uint64_t m=api_.create(*known->second,place(*anchor,layout,volumes).back());
        if(!m){++failures_;log_(failures_>=MaxFailures?"STACK case refused (giving up)\n":"STACK case refused\n");break;}
        meshes_.push_back({m,item,known->second->volume});
        if(meshes_.size()==items.size())log_("STACK "+std::to_string(items.size())+" cases on the partner\n");
    }
    std::vector<uint8_t> volumes;for(auto& m:meshes_)volumes.push_back(m.volume);
    auto at=place(*anchor,layout,volumes);
    for(size_t k=0;k<meshes_.size();++k){
        if(api_.place(meshes_[k].mesh,at[k]))continue;
        // Not the mesh made any more: never released; the ones above it are, and all come back as new meshes.
        log_("STACK a case is no longer the mesh made: dropped\n");
        while(meshes_.size()>k+1){api_.release(meshes_.back().mesh);meshes_.pop_back();}
        meshes_.pop_back();break;
    }
}
void Stack::forget(const char* why){
    if(!meshes_.empty())log_("STACK "+std::to_string(meshes_.size())+" cases forgotten ("+why+")\n");
    meshes_.clear();cases_.clear();failures_=0;
}
void Stack::clear(){
#ifndef MUTANT_STACK_LEAK
    for(auto& m:meshes_)api_.release(m.mesh);
#endif
    if(!meshes_.empty())log_("STACK "+std::to_string(meshes_.size())+" cases removed\n");
    meshes_.clear();
}
#ifdef _WIN32
namespace {
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){v=0;return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
}
#endif
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native cargo stack API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    // The mesh factory (visual parameters -> DSAnimatingSkinnedMeshInstance), the reflected message dispatch and the
    // mesh update handler (MsgUpdate), as the cargo code calls them (0x2c48cb8, 0x2c48cf8, 0x2ccdb10).
    // ... plus the item lookup by id (0x3439df0, a read-only probe of the definition table) and the carried-case model
    // and appearance selectors (0x2c5acc0, 0x2c541d0; read-only), as the cargo code calls them (0x2c845ce, 0x2c489a0,
    // 0x2c48b75).
    const std::array<Entry,6> entries={{
        {0x2cd4660,{0x40,0x53,0x55,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x8d,0x0d,0xfe,0x27,0xf6,0x01,0xe8,0x79,0xcc,0xd1,0xfe,0x48}},
        {0x19f4ae0,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x3e,0xc5,0x56,0x03,0x48,0x33,0xc4,0x48,0x89,0x44}},
        {0x2c75980,{0x48,0x8b,0xc4,0x41,0x56,0x48,0x83,0xec,0x70,0x48,0x89,0x58,0x08,0x4c,0x8b,0xf1,0x48,0x8b,0x49,0x48,0x48,0x89,0x68,0x10,
                    0x4c,0x89,0x78,0xe8,0x4c,0x8b,0xfa,0x48,0x85,0xc9,0x74,0x05,0xe8,0xc7,0x37,0x9e}},
        {0x3439df0,{0x40,0x53,0x4c,0x8b,0x81,0x58,0x01,0x00,0x00,0x48,0x8b,0xd9,0x48,0x89,0x74,0x24,0x10,0x8b,0xf2,0x48,0x89,0x7c,0x24,0x18}},
        {0x2c5acc0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x4c,0x8b,0x89,0xb8,0xdf,0x46,0x00,0x41,0x0f}},
        {0x2c541d0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x4c,0x8b,0x89,0xb8,0xdf,0x46,0x00,0x41,0x0f,0xb6,0xf0,0x48,0x63}}}};
    std::array<unsigned,6> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"stack code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"stack code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"stack function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"stack function signature missing/ambiguous");
    using FactoryFn=uint64_t(*)(const void*);using DispatchFn=void(*)(uint64_t,uint64_t,void*,uint64_t);
    using UpdateFn=void(*)(uint64_t,void*);using GetTypeFn=uint64_t(*)(uint64_t);
    using PlaceFn=void(*)(uint64_t,const Transform*);using IntFn=void(*)(uint64_t,int);using BoolFn=void(*)(uint64_t,bool);using ReleaseFn=void(*)(uint64_t);
    auto factory=reinterpret_cast<FactoryFn>(s.base+entries[0].rva);auto dispatch=reinterpret_cast<DispatchFn>(s.base+entries[1].rva);
    auto update=reinterpret_cast<UpdateFn>(s.base+entries[2].rva);
    using FindFn=uint64_t(*)(uint64_t,uint32_t);using SelectFn=uint64_t(*)(uint64_t,uint64_t,bool);
    auto find=reinterpret_cast<FindFn>(s.base+entries[3].rva);auto selectModel=reinterpret_cast<SelectFn>(s.base+entries[4].rva);
    auto selectLook=reinterpret_cast<SelectFn>(s.base+entries[5].rva);const uint64_t definitionsSlot=s.base+0x7c0dd98;
    const uint64_t base=s.base,iface=s.base+0x3dd92d0,typeGetter=s.base+0x2c5d2a0,meshType=s.base+0x4c36e70,
                   msgInit=s.base+0x3ba7b28,msgUpdate=s.base+0x3badf50,managerSlot=s.base+0x7bf4968;
    uint64_t first=0;require(q(s,iface,first)&&first==typeGetter,"stack mesh interface vtable differs");
    // Identity of each mesh made: its primary vtable and interface vtable when made.
    auto made=std::make_shared<std::unordered_map<uint64_t,uint64_t>>();auto* sp=&s;
    auto identity=[sp,made,iface](uint64_t m){auto it=made->find(m);uint64_t v=0,i=0;
        return it!=made->end()&&q(*sp,m,v)&&v==it->second&&q(*sp,m+0x168,i)&&i==iface;};
    Api a;
    // A plain case: the cargo config's ModelResources entry 2 (an ordinary medium case), its loaded object. Its child
    // models (2 live) get no collision either: a child mesh never receives the collision resource (+40) that the
    // collision gate 0x2c6ed06 requires (0x2c70900 sets only +30/+38); the root's is switched off with +15f.
    auto plain=[sp,managerSlot]()->std::optional<Case>{
        uint64_t manager=0,config=0,array=0,wrapper=0,loaded=0;uint32_t count=0,children=1;
        if(!q(*sp,managerSlot,manager)||!q(*sp,manager+8,config)||!sp->read(config+0x198,&count,4)||count<3||count>64)return std::nullopt;
        if(!q(*sp,config+0x1a0,array)||!q(*sp,array+16,wrapper)||!q(*sp,wrapper+0x30,loaded)||!loaded)return std::nullopt;
        if(!sp->read(loaded+0xd0,&children,4)||children>16)return std::nullopt;
        Case c;c.model=loaded;c.volume=1;return c;};
    // The item's own case: its definition by id, then the model and appearance the game would draw for it carried.
    // `variant` is the record's +204 bit 5, as the native realization passes it. Packaged weapons (case type 2) and
    // construction machines (0xb) are drawn like the game draws them on the backpack: live on the Episode 3 save their
    // meshes are config models 15 and 9, with the S footprint and at most 4 children. Body bags (6), carts (0xa) and
    // volumes beyond LL, a selected model that is not one of the config's own case models, or an unknown id are drawn
    // as the plain case.
    a.caseFor=[sp,plain,find,selectModel,selectLook,managerSlot,definitionsSlot](uint32_t item,bool variant)->std::optional<Case>{
        uint64_t definitions=0,manager=0,config=0,array=0;uint32_t count=0;
        if(!item||!q(*sp,definitionsSlot,definitions)||!definitions||!q(*sp,managerSlot,manager)||!manager)return plain();
        uint64_t entries=0;uint32_t capacity=0;
        if(!q(*sp,definitions+0x158,entries)||!entries||!sp->read(definitions+0x164,&capacity,4)||!capacity||capacity>(1u<<20))return plain();
        uint64_t def=find(definitions,item);if(!def)return plain();
        uint8_t kind[4]{};if(!sp->read(def+0x3a,kind,4))return plain();   // +3a attribute, +3b case type, +3e volume
        uint8_t volume=0;if(!sp->read(def+0x3e,&volume,1))return plain();
        uint8_t type=kind[1];
        if(type==6||type==0xa||volume>3)return plain();
        uint64_t wrapper=selectModel(manager,def,variant),loaded=0;
        if(!wrapper||!q(*sp,wrapper+0x30,loaded)||!loaded)return plain();
        // Only the config's own case models (indices 1..30), never an asset model nobody has inspected.
        if(!q(*sp,manager+8,config)||!sp->read(config+0x198,&count,4)||count>64||!q(*sp,config+0x1a0,array))return plain();
        bool own=false;for(uint32_t i=1;i<count&&!own;++i){uint64_t w=0;own=q(*sp,array+8*i,w)&&w==wrapper;}
        uint32_t children=0;if(!own||!sp->read(loaded+0xd0,&children,4)||children>16)return plain();
        Case c;c.model=loaded;c.look=selectLook(manager,def,variant);c.volume=volume;return c;};
    a.create=[factory,dispatch,sp,made,base,iface,meshType,msgInit](const Case& c,const Transform& t)->uint64_t{
        uint64_t model=c.model;
        for(double x:t.position)if(!std::isfinite(x))return 0;
        alignas(16) unsigned char p[0xa8]{};
        std::memcpy(p,&model,8);std::memcpy(p+8,&c.look,8);std::memcpy(p+0x20,t.position.data(),24);std::memcpy(p+0x38,t.rotation.data(),36);
        // +80/+88 = 2/1: what the game passes for a record carried by the player (record +0x80 == 1).
        uint32_t two=2,none=0xffffffffu;std::memcpy(p+0x80,&two,4);std::memcpy(p+0x84,&none,4);
        p[0x88]=1;p[0x89]=1;p[0x8d]=1;p[0xa0]=1;   // retained model; no collision instance
        uint64_t m=factory(p);if(!m)return 0;
        uint64_t i=0;if(!q(*sp,m+0x168,i)||i!=iface)return 0;   // not what the factory makes: never touched again
        alignas(16) unsigned char init[16]{};std::memcpy(init,&msgInit,8);
        uint64_t type=reinterpret_cast<GetTypeFn>(*reinterpret_cast<uint64_t*>(i))(m+0x168);
        dispatch(type,m,init,meshType);
        uint64_t v=0;q(*sp,m,v);
        reinterpret_cast<IntFn>(*reinterpret_cast<uint64_t*>(v+0x88))(m,4);
        reinterpret_cast<IntFn>(*reinterpret_cast<uint64_t*>(v+0xa0))(m,9);
        reinterpret_cast<BoolFn>(*reinterpret_cast<uint64_t*>(v+0x90))(m,true);
        reinterpret_cast<PlaceFn>(*reinterpret_cast<uint64_t*>(v+0x70))(m,&t);
        (*made)[m]=v;(void)base;return m;};
    a.place=[identity,update,msgUpdate](uint64_t m,const Transform& t){
        if(!identity(m))return false;
        for(double x:t.position)if(!std::isfinite(x))return true;   // kept where it was this tick
        uint64_t v=*reinterpret_cast<uint64_t*>(m);
        reinterpret_cast<PlaceFn>(*reinterpret_cast<uint64_t*>(v+0x70))(m,&t);
        alignas(16) unsigned char u[0x28]{};std::memcpy(u,&msgUpdate,8);
        uint64_t lowest=0xffefffffffffffffULL;float dt=1.f/30;std::memcpy(u+0x18,&lowest,8);std::memcpy(u+0x24,&dt,4);
        update(m,u);return true;};
    a.release=[identity,made](uint64_t m){
        if(identity(m)){uint64_t v=*reinterpret_cast<uint64_t*>(m);reinterpret_cast<ReleaseFn>(*reinterpret_cast<uint64_t*>(v+0x80))(m);}
        made->erase(m);};
    return a;
#endif
}
}
