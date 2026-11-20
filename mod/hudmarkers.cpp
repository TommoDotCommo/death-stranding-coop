#include "hudmarkers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::hudmarkers {
uint64_t iconId(uint64_t player){
    uint64_t h=1469598103934665603ull;for(unsigned char c:std::to_string(player)){h^=c;h*=1099511628211ull;}
    return 0x4d50000000000000ull|(h&0xffffffffffffull);
}
std::vector<Mark> marks(const mapicons::Board::Picture& p,uint32_t area,const std::set<uint64_t>& drawn,const std::array<double,3>* here,size_t limit){
    std::vector<std::pair<double,Mark>> found;
    for(uint32_t i=0;i<p.count&&i<mapicons::MaxIcons;++i){const auto& n=p.icons[i];
#ifndef MUTANT_HUDMARKERS_DRAWN
        if(drawn.count(n.id))continue;
#else
        (void)drawn;
#endif
        if(area&&n.area&&n.area!=area)continue;
        if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z))continue;
        Mark m;m.x=n.x;m.y=n.y;m.z=n.z;m.id=(n.id&0xffffffffffffull)|0x4855000000000000ull;
        double d=0;if(here){const double dx=n.x-(*here)[0],dy=n.y-(*here)[1],dz=n.z-(*here)[2];d=dx*dx+dy*dy+dz*dz;}
        found.push_back({d,m});}
#ifndef MUTANT_HUDMARKERS_FARTHEST
    std::stable_sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.first<b.first;});
#endif
    std::vector<Mark> out;for(auto& f:found){if(out.size()>=std::min(limit,MaxMarks))break;out.push_back(f.second);}
    return out;
}
void Board::publish(const std::vector<Mark>& v){
    const unsigned next=(current_.load(std::memory_order_relaxed)+1)%Slots;auto& p=slots_[next];
    p.count=uint32_t(std::min(v.size(),MaxMarks));for(uint32_t i=0;i<p.count;++i)p.marks[i]=v[i];
    current_.store(next,std::memory_order_release);
}
void Board::read(Picture& out) const {out=slots_[current_.load(std::memory_order_acquire)];}
#ifdef _WIN32
namespace {
constexpr uint64_t CallSite=0x3096571,Sort=0x30934b0,Add=0x308ea00,Stub=0x309694c;
using SortFn=void(*)(void*);using AddFn=void(*)(void*,uint8_t,const float*,void*,uint64_t,uint8_t);
std::atomic<const Board*> board{nullptr};std::atomic<uint8_t> kind{DefaultKind};SortFn sort=nullptr;AddFn add=nullptr;
std::atomic<uint64_t> frames{0},added{0},kept{0};
// The HUD's sort, after the game has added its own records: the mod's first. No allocation of the mod's, no lock.
void hook(void* list){
    if(auto* b=board.load(std::memory_order_acquire)){
        Board::Picture p;b->read(p);const uint8_t k=kind.load(std::memory_order_relaxed);
        const int32_t before=*reinterpret_cast<const int32_t*>(static_cast<uint8_t*>(list)+8);   // the list's count
        for(uint32_t i=0;i<p.count&&i<MaxMarks;++i){alignas(16) const float at[4]={p.marks[i].x,p.marks[i].y,p.marks[i].z,0};add(list,k,at,nullptr,p.marks[i].id,0);}
        const int32_t after=*reinterpret_cast<const int32_t*>(static_cast<uint8_t*>(list)+8);
        frames.fetch_add(1,std::memory_order_relaxed);added.fetch_add(p.count,std::memory_order_relaxed);if(after>before)kept.fetch_add(uint64_t(after-before),std::memory_order_relaxed);
    }
    sort(list);
}
void writeCode(uint64_t at,const void* bytes,size_t n){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("HUD code cannot be written");
    if(n==4){uint32_t v=0;std::memcpy(&v,bytes,4);*reinterpret_cast<volatile uint32_t*>(at)=v;}   // one store (inside one cache line)
    else std::memcpy(reinterpret_cast<void*>(at),bytes,n);
    VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);}
}
void install(discovery::Sampler& s,const Board& b){
    static const uint8_t site[8]={0x48,0x8b,0xce,0xe8,0x3a,0xcf,0xff,0xff};
    static const uint8_t addHead[16]={0x4c,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x40};
    static const uint8_t sortHead[16]={0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x48,0x8b,0x05};
    uint8_t got[8]{},a[16]{},c[16]{},pad[12]{};
    if(!s.read(s.base+CallSite-3,got,8)||std::memcmp(got,site,8))throw std::runtime_error("HUD marker sort call differs");
    if(!s.read(s.base+Add,a,16)||std::memcmp(a,addHead,16))throw std::runtime_error("HUD marker adder differs");
    if(!s.read(s.base+Sort,c,16)||std::memcmp(c,sortHead,16))throw std::runtime_error("HUD marker sort differs");
    if(!s.read(s.base+Stub,pad,12))throw std::runtime_error("HUD padding unreadable");for(auto x:pad)if(x!=0xcc)throw std::runtime_error("HUD padding in use");
    sort=reinterpret_cast<SortFn>(s.base+Sort);add=reinterpret_cast<AddFn>(s.base+Add);board.store(&b,std::memory_order_release);
    uint8_t stub[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target=reinterpret_cast<void*>(hook);std::memcpy(stub+2,&target,8);writeCode(s.base+Stub,stub,12);
    const int32_t rel=int32_t(int64_t(Stub)-int64_t(CallSite+5));writeCode(s.base+CallSite+1,&rel,4);
}
void setKind(uint8_t k){if(safeKind(k))kind.store(k,std::memory_order_relaxed);}
Stats stats(){return {frames.load(std::memory_order_relaxed),added.load(std::memory_order_relaxed),kept.load(std::memory_order_relaxed)};}
#else
void install(discovery::Sampler&,const Board&){throw std::runtime_error("the HUD needs the game");}
void setKind(uint8_t){}
Stats stats(){return {};}
#endif
}
