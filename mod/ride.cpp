#include "ride.h"
#include "providers.h"
#include "gear.h"
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace mod::ride {
bool supported(uint8_t kind){return kind==1||kind==2;}
Transform vehicleTransform(const std::array<double,3>& p,const std::array<double,3>& offset,const std::array<double,3>& angles){
    Transform t;
    // Adapter axes (x, up, y) to game axes (x, y, z up).
    t.position={p[0]+offset[0],p[2]+offset[2],p[1]+offset[1]};
    t.rotation=rotationFromAngles(angles);
    return t;
}
namespace {
std::string hex(uint64_t v){std::ostringstream o;o<<"0x"<<std::hex<<v;return o.str();}
}
void PartnerVehicle::forget(const char* why){
    if(entity_)log_(std::string("RIDE partner vehicle forgotten (")+why+")\n");
    entity_=0;kind_=0;moved_=false;
}
void PartnerVehicle::drop(const char* why){
    if(!entity_)return;
#ifndef MUTANT_RIDE_LEAK
    api_.remove(entity_);
#endif
    log_("RIDE partner vehicle removed ("+std::string(why)+") "+hex(entity_)+"\n");
    entity_=0;kind_=0;moved_=false;
}
void PartnerVehicle::tick(uint64_t now,bool permitted,uint8_t kind,const Transform* at){
    if(!permitted){drop("partner gone");return;}
#ifndef MUTANT_RIDE_IDENTITY
    if(entity_&&!api_.alive(entity_,kind_)){forget("no longer the vehicle made");}
#endif
    if(!kind||!at)return;   // on foot: the vehicle stays where the partner left it
    if(entity_&&kind_!=kind)drop("partner rides another kind");
    if(!entity_){
        if(!supported(kind)||failures_>=MaxFailures||now<nextTry_)return;
        entity_=api_.spawn(kind,*at);
        if(!entity_){++failures_;nextTry_=now+RetryMs;log_("RIDE partner vehicle not made (kind "+std::to_string(kind)+", failure "+std::to_string(failures_)+")\n");return;}
        kind_=kind;failures_=0;moved_=false;
        std::ostringstream o;o<<std::setprecision(10)<<"RIDE partner vehicle made kind="<<int(kind)<<" entity="<<hex(entity_)
            <<" at="<<at->position[0]<<","<<at->position[1]<<","<<at->position[2]<<"\n";log_(o.str());
    }
    bool ok=api_.place(entity_,*at);
    if(!ok){drop("no mover to drive");++failures_;nextTry_=now+RetryMs;return;}
    if(!moved_){moved_=true;log_("RIDE partner vehicle driven\n");}
}
#ifdef _WIN32
namespace {
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){return s.read(a,&v,8);}
// An empty cargo carrier with a slot for every slot type (0..255): the gear copies' layout (gear.h, EmptyCarrier), wide
// enough for a vehicle. The porter's cargo logic near a truck asks for slot type 0x45 and writes the slot it gets
// (0x279e228..0x279e232, live 2026-11-09: a 32-type carrier returned none and the game faulted).
struct VehicleCarrier {
    static constexpr unsigned Types=256;
    alignas(16) unsigned char carrier[0x100]{};
    uint64_t slots[Types]{};
    alignas(16) unsigned char slot[Types][0x100]{};
    VehicleCarrier(){
        int32_t count=int32_t(Types);uint64_t array=reinterpret_cast<uint64_t>(slots);
        std::memcpy(carrier+0x10,&count,4);std::memcpy(carrier+0x18,&array,8);
        for(unsigned i=0;i<Types;++i){slot[i][1]=uint8_t(i);slots[i]=reinterpret_cast<uint64_t>(slot[i]);}
        static_assert(sizeof(CRITICAL_SECTION)==gear::LockSize,"CRITICAL_SECTION size");
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(carrier+gear::CarrierLock));
        for(auto& one:slot)InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(one+gear::SlotLock));
    }
    uint64_t address() const {return reinterpret_cast<uint64_t>(carrier);}
};
}
#endif
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native ride API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    const std::array<Entry,4> entries={{
        // Pooled vehicle creator wrapper (pool, descriptor word*, WorldTransform*, float): resource from the unit table.
        {0x2f532c0,{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x48,0x48,0x8b,0xf2,0xc5,0xf8,0x29,0x74,0x24,0x30,0x0f,0xb7,0x12,0x48,0x8b,0xd9}},
        // Matching pooled removal (pool, entity).
        {0x2b7b9e0,{0x40,0x57,0x48,0x83,0xec,0x20,0x45,0x33,0xc0,0x48,0x8b,0xf9,0x33,0xc0,0x66,0x90,0x48,0x39,0x14,0xc1,0x74,0x12,0x41,0xff}},
        // VehicleMover: mode 1 and target transform (mover, WorldTransform*).
        {0x2f801b0,{0xc7,0x81,0x28,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0xc5,0xf8,0x10,0x02,0xc5,0xf8,0x11,0x81,0x30,0x01,0x00,0x00,0xc5,0xfb}},
        // Pool record constructor (record).
        {0x2f51e90,{0x48,0x8d,0x05,0x79,0x0a,0xed,0x00,0xc7,0x41,0x08,0x00,0x00,0x7a,0x44,0x48,0x89,0x01,0x33,0xc0,0x48,0x89,0x41,0x30,0x48}}}};
    std::array<unsigned,4> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"ride code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"ride code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"ride function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"ride function signature missing/ambiguous");
    const uint64_t bike=s.base+0x3e25db8,truck=s.base+0x3e26790,mover=s.base+0x3e28010;uint64_t g=0;
    require(q(s,bike,g)&&g==s.base+0x2f6bb60&&q(s,truck,g)&&g==s.base+0x2f6bcb0&&q(s,mover,g)&&g==s.base+0x2f6be60,"vehicle vtables differ from pinned image");
    using CreateFn=uint64_t(*)(uint64_t,const uint16_t*,const Transform*,float);using RemoveFn=void(*)(uint64_t,uint64_t);
    using TargetFn=void(*)(uint64_t,const Transform*);using RecordFn=void(*)(uint64_t);
    auto create=reinterpret_cast<CreateFn>(s.base+entries[0].rva);auto remove=reinterpret_cast<RemoveFn>(s.base+entries[1].rva);
    auto target=reinterpret_cast<TargetFn>(s.base+entries[2].rva);auto record=reinterpret_cast<RecordFn>(s.base+entries[3].rva);
    // The mod's own four-slot pool, laid out like the game's (0x2f5af30): entities +0, sources +0x20, records +0x40
    // (0x50 each). Never freed: entities point at their record (+0x898) for as long as they exist.
    auto pool=reinterpret_cast<uint64_t>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    require(pool!=0,"ride pool allocation failed");
    for(unsigned i=0;i<4;++i)record(pool+0x40+0x50*i);
    auto table=[bike,truck](uint8_t kind){return kind==1?bike:kind==2?truck:0;};
    auto* sp=&s;
    // The display vehicle skips the game's vehicle setup and has no cargo carrier (its DSBaggage2CarrierComponent +0x50
    // is null); the porter's cargo logic near a vehicle reads it (0x2796646 -> 0x2f66d80 -> 0x2c52010) and faulted live
    // (2026-11-09). Each carrier component gets an empty carrier the mod owns, as the gear copies do (gear.h), with every
    // slot type.
    auto carriers=std::make_shared<std::vector<std::unique_ptr<VehicleCarrier>>>();   // kept for the process
    auto giveCarriers=[sp,carriers](uint64_t e){
        uint32_t count=0;uint64_t list=0;unsigned given=0;
        if(!sp->read(e+gear::ComponentCount,&count,4)||count>gear::MaxComponents||!q(*sp,e+gear::ComponentArray,list))return -1;
        for(uint32_t i=0;i<count;++i){uint64_t c=0,v=0,current=1;
            if(q(*sp,list+8*i,c)&&c&&q(*sp,c,v)&&v==sp->base+gear::CarrierComponentVtable&&q(*sp,c+gear::ComponentCarrier,current)&&!current){
                carriers->push_back(std::make_unique<VehicleCarrier>());
                *reinterpret_cast<uint64_t*>(c+gear::ComponentCarrier)=carriers->back()->address();++given;}}
        return int(given);};
    Api api;
    api.spawn=[create,pool,table,sp,giveCarriers](uint8_t kind,const Transform& t)->uint64_t{
        if(!table(kind))return 0;
        unsigned used=0;for(unsigned i=0;i<4;++i)if(*reinterpret_cast<uint64_t*>(pool+8*i))++used;
        if(used==4)return 0;   // the creator does not check capacity itself
        // The wrapper reads the unit table without null checks (0x2f6c86f, 0x2f532f3): before the story unlocks a vehicle
        // its unit (or the table) can be missing. Require the manager, the unit of the right class and its resource.
        uint64_t manager=0,unit=0,unitVtable=0,unitResource=0;
        if(!q(*sp,sp->base+0x7bfecc0,manager)||!manager||!q(*sp,manager+0x28+8ull*(kind==1?3:2),unit)||!unit
           ||!q(*sp,unit,unitVtable)||unitVtable!=sp->base+(kind==1?0x3e261c0:0x3e26b98)||!q(*sp,unit+0x80,unitResource)||!unitResource)return 0;
        alignas(8) uint16_t descriptor[2]={uint16_t(kind==1?3:2),0};   // unit type: truck 2, motorbike 3; variant 0
        auto e=create(pool,descriptor,&t,0.f);
        uint64_t v=0;if(!e||!q(*sp,e,v)||v!=table(kind))return 0;
        if(giveCarriers(e)<0)return 0;
        // Occupied (+0x63c, written only by the driver registration 0x2f7c4c0/release): the vehicle update then keeps its
        // "free" bit (+0x560 bit 1) clear (0x2f7920b..0x2f79222), so the porter's use/load checks skip it; a free display
        // vehicle reached its missing GameActor (+0x8a8) through 0x2f6f700 and faulted live (2026-11-09).
        *reinterpret_cast<uint32_t*>(e+0x63c)=1;*reinterpret_cast<uint32_t*>(e+0x560)&=~2u;
        return e;};
    api.remove=[remove,pool](uint64_t e){remove(pool,e);};
    api.alive=[table,sp,pool](uint64_t e,uint8_t kind){
        bool inPool=false;for(unsigned i=0;i<4;++i)if(*reinterpret_cast<uint64_t*>(pool+8*i)==e)inPool=true;
        uint64_t v=0;return inPool&&q(*sp,e,v)&&v==table(kind);};
    api.place=[target,sp,mover](uint64_t e,const Transform& t){
        // The VehicleMover among the entity's components (count +0x90, array +0x98), owned by it (+0x48).
        uint32_t count=0;uint64_t list=0;
        if(!sp->read(e+0x90,&count,4)||!count||count>512||!q(*sp,e+0x98,list))return false;
        for(uint32_t i=0;i<count;++i){uint64_t c=0,v=0,owner=0;
            if(q(*sp,list+8*i,c)&&q(*sp,c,v)&&v==mover&&q(*sp,c+0x48,owner)&&owner==e){target(c,&t);return true;}}
        return false;};
    return api;
#endif
}
}
