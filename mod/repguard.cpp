#include "repguard.h"
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::repguard {
namespace {
std::array<std::atomic<uint64_t>,MaxBodies> watched{};std::atomic<bool> on{false};std::atomic<uint64_t> skips{0};
#ifdef _WIN32
constexpr uint64_t Jump=0x2759710,Handler=0x2747a90,Stub=0x275900f;
using HandlerFn=void(*)(uint64_t,uint64_t,uint64_t,uint64_t);HandlerFn handler=nullptr;
bool isBody(uint64_t e){if(!e)return false;for(auto& b:watched)if(b.load(std::memory_order_relaxed)==e)return true;return false;}
// The rep's MsgEntityRepUpdate handler (rcx: the rep; [rep+0x28]: its EntityRep; [EntityRep+0x80]: the entity).
void guarded(uint64_t rep,uint64_t msg,uint64_t r8,uint64_t r9){
    uint64_t entityRep=0,entity=0;
    if(rep)std::memcpy(&entityRep,reinterpret_cast<void*>(rep+0x28),8);
    if(entityRep)std::memcpy(&entity,reinterpret_cast<void*>(entityRep+0x80),8);
    if(isBody(entity)){skips.fetch_add(1,std::memory_order_relaxed);return;}
    handler(rep,msg,r8,r9);
}
void writeCode(uint64_t at,const void* bytes,size_t n){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("code cannot be written");
    if(n==4){uint32_t v=0;std::memcpy(&v,bytes,4);*reinterpret_cast<volatile uint32_t*>(at)=v;}else std::memcpy(reinterpret_cast<void*>(at),bytes,n);
    VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);}
#endif
}
void bodies(const std::vector<uint64_t>& e){for(size_t i=0;i<MaxBodies;++i)watched[i].store(i<e.size()?e[i]:0,std::memory_order_relaxed);}
bool installed(){return on.load();}
uint64_t skipped(){return skips.load(std::memory_order_relaxed);}
#ifdef _WIN32
void install(discovery::Sampler& s){
    if(on.load())return;
    uint8_t jump[5]{},pad[12]{},stub[5]{},head[13]{};int32_t rel=0;
    if(!s.read(s.base+Jump,jump,5)||jump[0]!=0xe9)throw std::runtime_error("rep update jump differs");
    std::memcpy(&rel,jump+1,4);if(Jump+5+int64_t(rel)!=0x2730b20)throw std::runtime_error("rep update jump target differs");
    if(!s.read(s.base+0x2730b20,stub,5)||stub[0]!=0xe9)throw std::runtime_error("rep update stub differs");
    std::memcpy(&rel,stub+1,4);if(0x2730b20+5+int64_t(rel)!=Handler)throw std::runtime_error("rep update stub target differs");
    static const uint8_t want[13]={0x4c,0x8b,0xdc,0x55,0x56,0x41,0x55,0x48,0x8d,0x6c,0x24,0x90,0x48};   // mov r11,rsp; push rbp; push rsi; push r13; lea rbp,[rsp-0x70]; ...
    if(!s.read(s.base+Handler,head,13)||std::memcmp(head,want,13))throw std::runtime_error("rep update handler differs");
    if(!s.read(s.base+Stub,pad,12))throw std::runtime_error("padding unreadable");for(auto c:pad)if(c!=0xcc)throw std::runtime_error("padding in use");
    handler=reinterpret_cast<HandlerFn>(s.base+Handler);
    uint8_t abs[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target=reinterpret_cast<void*>(guarded);std::memcpy(abs+2,&target,8);writeCode(s.base+Stub,abs,12);
    const int32_t to=int32_t(int64_t(Stub)-int64_t(Jump+5));writeCode(s.base+Jump+1,&to,4);
    on.store(true);
}
#else
void install(discovery::Sampler&){throw std::runtime_error("the rep guard needs the game");}
#endif
}
