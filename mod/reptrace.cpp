#include "reptrace.h"
#include <atomic>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::reptrace {
namespace {
std::array<std::atomic<uint64_t>,MaxBodies> bodies{};std::atomic<uint64_t> porterRep{0},imageBase{0},total{0};
std::atomic<bool> on{false};
struct Slot {std::atomic<uint64_t> hash{0},count{0};std::atomic<bool> porter{false};std::array<uint64_t,Frames> at{};std::atomic<bool> filled{false};};
std::array<Slot,MaxStacks> slots;
#ifdef _WIN32
DWORD tls=TLS_OUT_OF_INDEXES;using UpdateFn=void(*)(uint64_t);UpdateFn update=nullptr;
bool isBody(uint64_t e){if(!e)return false;for(auto& b:bodies)if(b.load(std::memory_order_relaxed)==e)return true;return false;}
void record(uint64_t rep){
    void* frames[Frames+1]{};const USHORT n=RtlCaptureStackBackTrace(1,Frames+1,frames,nullptr);
    const uint64_t base=imageBase.load(std::memory_order_relaxed);uint64_t at[Frames]{};uint64_t h=1469598103934665603ull;
    for(USHORT i=0;i<n&&i<Frames;++i){uint64_t a=uint64_t(frames[i]);at[i]=a>=base&&a<base+0x10000000?a-base:0;h^=at[i];h*=1099511628211ull;}
    const bool toPorter=rep==porterRep.load(std::memory_order_relaxed);if(toPorter)h^=1;if(!h)h=1;
    for(auto& s:slots){
        uint64_t cur=s.hash.load(std::memory_order_acquire);
        if(cur==h){s.count.fetch_add(1,std::memory_order_relaxed);return;}
        if(!cur){uint64_t zero=0;if(s.hash.compare_exchange_strong(zero,h)){std::memcpy(s.at.data(),at,sizeof at);s.porter.store(toPorter);s.filled.store(true,std::memory_order_release);s.count.fetch_add(1);return;}
                 if(zero==h){s.count.fetch_add(1,std::memory_order_relaxed);return;}}
    }
}
// The game's append, as it is (0x237a3f0), then the record.
void append(uint8_t* rep,uint8_t* call){
    volatile uint64_t* tail=reinterpret_cast<volatile uint64_t*>(rep+0x230);
    const uint64_t last=*tail;
    if(!last)*reinterpret_cast<volatile uint64_t*>(rep+0x228)=uint64_t(call);else *reinterpret_cast<volatile uint64_t*>(last+8)=uint64_t(call);
    *tail=uint64_t(call);
    volatile uint32_t* flags=reinterpret_cast<volatile uint32_t*>(rep+0x1a4);uint32_t f=*flags;f&=~(1u<<28);f|=1u<<27;*flags=f;
    total.fetch_add(1,std::memory_order_relaxed);
    const uint64_t current=uint64_t(TlsGetValue(tls));
    if(current&&isBody(current)){uint64_t own=0;std::memcpy(&own,reinterpret_cast<void*>(current+0x60),8);if(uint64_t(rep)!=own)record(uint64_t(rep));}
}
void updating(uint64_t entity){
    void* before=TlsGetValue(tls);TlsSetValue(tls,reinterpret_cast<void*>(entity));
    update(entity);
    TlsSetValue(tls,before);
}
void writeCode(uint64_t at,const void* bytes,size_t n){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("code cannot be written");
    if(n==4){uint32_t v=0;std::memcpy(&v,bytes,4);*reinterpret_cast<volatile uint32_t*>(at)=v;}
    else if(n==8){uint64_t v=0;std::memcpy(&v,bytes,8);*reinterpret_cast<volatile uint64_t*>(at)=v;}
    else std::memcpy(reinterpret_cast<void*>(at),bytes,n);
    VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);}
void stub(uint64_t at,void* target){uint8_t s[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(s+2,&target,8);writeCode(at,s,12);}
#endif
}
void watch(const std::vector<uint64_t>& b,uint64_t rep){
    for(size_t i=0;i<MaxBodies;++i)bodies[i].store(i<b.size()?b[i]:0,std::memory_order_relaxed);
    porterRep.store(rep,std::memory_order_relaxed);
}
bool installed(){return on.load();}
uint64_t appends(){return total.load(std::memory_order_relaxed);}
std::vector<Stack> stacks(){
    std::vector<Stack> out;
    for(auto& s:slots)if(s.filled.load(std::memory_order_acquire)){Stack k;k.count=s.count.load();k.porter=s.porter.load();k.at=s.at;out.push_back(k);}
    return out;
}
#ifdef _WIN32
void install(discovery::Sampler& s){
    if(on.load())return;
    static const uint8_t appendCode[0x35]={0x48,0x8b,0x81,0x30,0x02,0,0,0x48,0x85,0xc0,0x75,0x09,0x48,0x89,0x91,0x28,0x02,0,0,0xeb,0x04,0x48,0x89,0x50,0x08,
        0x48,0x89,0x91,0x30,0x02,0,0,0x8b,0x81,0xa4,0x01,0,0,0x0f,0xba,0xf0,0x1c,0x0f,0xba,0xe8,0x1b,0x89,0x81,0xa4,0x01,0,0,0xc3};
    uint8_t got[0x35]{};if(!s.read(s.base+0x237a3f0,got,sizeof got)||std::memcmp(got,appendCode,sizeof got))throw std::runtime_error("rep append differs");
    const uint64_t sites[3]={0x23ffdf1,0x2400383,0x2401488};
    for(auto site:sites){uint8_t c[5]{};int32_t rel=0;if(!s.read(s.base+site,c,5)||c[0]!=0xe8)throw std::runtime_error("update call differs");std::memcpy(&rel,c+1,4);
        if(site+5+int64_t(rel)!=0x2402750)throw std::runtime_error("update call target differs");}
    uint8_t pad[12]{};if(!s.read(s.base+0x23721fc,pad,12))throw std::runtime_error("padding unreadable");for(auto c:pad)if(c!=0xcc)throw std::runtime_error("padding in use");
    tls=TlsAlloc();if(tls==TLS_OUT_OF_INDEXES)throw std::runtime_error("no thread slot");
    imageBase.store(s.base);update=reinterpret_cast<UpdateFn>(s.base+0x2402750);
    stub(s.base+0x23721fc,reinterpret_cast<void*>(updating));
    for(auto site:sites){const int32_t rel=int32_t(int64_t(0x23721fc)-int64_t(site+5));writeCode(s.base+site+1,&rel,4);}
    // The append's first bytes become a jump to the mod's copy: mov rax,imm64; jmp rax (12 bytes; the function is 16-aligned
    // and only reached by jumps from the game's own call path, which this tick does not run).
    stub(s.base+0x237a3f0,reinterpret_cast<void*>(append));
    on.store(true);
}
#else
void install(discovery::Sampler&){throw std::runtime_error("rep trace needs the game");}
#endif
}
