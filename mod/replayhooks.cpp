#include "replayhooks.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::replayhooks {
namespace {
std::atomic<bool> on{false};std::atomic<int64_t> mission{-1};std::atomic<int32_t> code{-1};
std::mutex lock;std::vector<Record> ring;
void keep(const Record& r){std::lock_guard<std::mutex> g(lock);if(ring.size()>=Ring)ring.erase(ring.begin());ring.push_back(r);}
#ifdef _WIN32
uint64_t imageBase=0;
using Fn=uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t);
Fn acceptFn=nullptr,blackboardFn=nullptr,scriptFn=nullptr,startFn=nullptr;
uint32_t word(uint64_t at){uint32_t v=0;if(at)std::memcpy(&v,reinterpret_cast<void*>(at),4);return v;}
uint64_t qword(uint64_t at){uint64_t v=0;if(at)std::memcpy(&v,reinterpret_cast<void*>(at),8);return v;}
uint64_t menuData(){const uint64_t owner=qword(imageBase+0x7c00128);return owner?qword(owner+0x318):0;}
uint64_t accept(uint64_t d,uint64_t r2,uint64_t r8,uint64_t r9){
    if(d){const uint32_t c=word(d+0x20);code.store(int32_t(c));keep({Kind::Accept,c,word(d+0x30),0});}
    return acceptFn(d,r2,r8,r9);}
uint64_t blackboard(uint64_t rcx,uint64_t index,uint64_t r8,uint64_t r9){
    const uint64_t d=menuData();keep({Kind::Blackboard,d?word(d+0x20):0xffffffffu,rcx,index&0xffffffffu});
    return blackboardFn(rcx,index,r8,r9);}
uint64_t script(uint64_t list,uint64_t resource,uint64_t r8,uint64_t r9){
    const uint64_t found=scriptFn(list,resource,r8,r9);keep({Kind::Script,resource,found,found?qword(found+0x38):0});
    return found;}
uint64_t start(uint64_t s,uint64_t id,uint64_t r8,uint64_t r9){
    const uint64_t result=startFn(s,id,r8,r9);const bool ok=(result&0xff)!=0;
    if(ok)mission.store(int64_t(id));
    keep({Kind::Start,id,ok?1u:0u,0});
    return result;}
void writeCode(uint64_t at,const void* bytes,size_t n){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("code cannot be written");
    if(n==4){uint32_t v=0;std::memcpy(&v,bytes,4);*reinterpret_cast<volatile uint32_t*>(at)=v;}else std::memcpy(reinterpret_cast<void*>(at),bytes,n);
    VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);}
struct Site { uint64_t call,target,pad; uint8_t head[8]; Fn* original; void* hook; };
#endif
}
bool installed(){return on.load();}
int64_t lastMission(){return mission.load();}
int32_t lastCode(){return code.load();}
std::vector<Record> drain(){std::lock_guard<std::mutex> g(lock);std::vector<Record> out;out.swap(ring);return out;}
#ifdef _WIN32
void install(discovery::Sampler& s){
    if(on.load())return;
    Site sites[4]={
        {0x30ee879,0x307a4f0,0x30ee89f,{0x48,0x83,0xec,0x28,0xe8,0xa7,0x80,0xf9},&acceptFn,reinterpret_cast<void*>(accept)},
        {0x27677ef,0x273fe10,0x27679fe,{0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x20},&blackboardFn,reinterpret_cast<void*>(blackboard)},
        {0x33c9c45,0x333b890,0x33c9c6e,{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83},&scriptFn,reinterpret_cast<void*>(script)},
        {0x33b80bc,0x33b0bc0,0x33b8710,{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57},&startFn,reinterpret_cast<void*>(start)}};
    for(auto& site:sites){
        uint8_t c[5]{},head[8]{},pad[12]{};int32_t rel=0;
        if(!s.read(s.base+site.call,c,5)||c[0]!=0xe8)throw std::runtime_error("a replay call site differs");
        std::memcpy(&rel,c+1,4);if(int64_t(site.call)+5+rel!=int64_t(site.target))throw std::runtime_error("a replay call's target differs");
        if(!s.read(s.base+site.target,head,8)||std::memcmp(head,site.head,8))throw std::runtime_error("a replay function differs");
        if(!s.read(s.base+site.pad,pad,12))throw std::runtime_error("padding unreadable");
        for(auto b:pad)if(b!=0xcc)throw std::runtime_error("padding in use");}
    imageBase=s.base;
    for(auto& site:sites){
        *site.original=reinterpret_cast<Fn>(s.base+site.target);
        uint8_t abs[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(abs+2,&site.hook,8);writeCode(s.base+site.pad,abs,12);
        const int32_t to=int32_t(int64_t(site.pad)-int64_t(site.call+5));writeCode(s.base+site.call+1,&to,4);}
    on.store(true);
}
#else
void install(discovery::Sampler&){throw std::runtime_error("the replay hooks need the game");}
#endif
}
