#include "online.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::online {
bool Requester::tick(uint64_t now,const State& s){
    if(!s.present)return false;
#ifndef MUTANT_ONLINE_ASK_BUSY
    if(s.busy()||s.signedIn())return false;
#endif
#ifndef MUTANT_ONLINE_ASK_FOREVER
    if(asks_>=MaxAsks)return false;
#endif
#ifndef MUTANT_ONLINE_ASK_GAP
    if(asks_&&now>=last_&&now-last_<Every)return false;
#endif
    last_=now;++asks_;return true;
}
#ifdef _WIN32
namespace {
// The game's string assignment (destination, source), and the replacement for both body functions: the body goes
// through as it is. Same calling convention as the functions it replaces (mode, in, out) -> success.
void(*assignString)(void*,void*)=nullptr;
bool plainBody(int,void* in,void* out){assignString(out,in);return true;}
bool same(discovery::Sampler& s,uint64_t rva,const std::vector<uint8_t>& bytes){
    std::vector<uint8_t> got(bytes.size());return s.read(s.base+rva,got.data(),got.size())&&got==bytes;}
bool redirect(uint64_t address,void* target){
    uint8_t jump[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};   // mov rax, target; jmp rax
    std::memcpy(jump+2,&target,8);DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),sizeof jump,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(address),jump,sizeof jump);
    VirtualProtect(reinterpret_cast<void*>(address),sizeof jump,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),sizeof jump);return true;}
}
#endif
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native online API requires Windows");
#else
    // The login task's request function (task, force): what the title menu's OnLogin handler (0x322f830) calls.
    const uint64_t requestRva=0x346f690,taskSlot=0x7c16740,addressSlot=0x4dfa130;
    const std::vector<uint8_t> bytes{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x0f,0xb6,0xfa,0x48,0x8b,0x0d,0x39,0x4b,0x7a,0x04,0x48};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("online code size cap");
        std::vector<uint8_t> code(section.size);if(!s.read(section.address,code.data(),code.size()))throw std::runtime_error("online code unreadable");
        auto at=code.begin();
        while((at=std::search(at,code.end(),bytes.begin(),bytes.end()))!=code.end()){
            ++count;if(section.address+size_t(at-code.begin())!=s.base+requestRva)throw std::runtime_error("login request signature moved/duplicated");++at;}
    }
    if(count!=1)throw std::runtime_error("login request signature missing/ambiguous");
    // The address string the sign-in request is built from: a pointer to the characters, with the engine's string
    // header in front (reference count, hash or 0xffffffff for "not computed", length, capacity).
    const char* stock="https://prod-pc-15.wws-gs2.com/auth/ds";
    uint64_t chars=0;char seen[40]={};
    if(!s.read(s.base+addressSlot,&chars,8)||!s.read(chars,seen,std::strlen(stock)+1)||std::strcmp(seen,stock)!=0)throw std::runtime_error("sign-in address string not found");
    // The two functions every gateway body passes through (0x19da0e0 incoming, 0x19da6a0 outgoing; one caller each,
    // always mode 2: base64 of an authenticated cipher under a key in the image) and the string assignment they use.
    const uint64_t readRva=0x19da0e0,writeRva=0x19da6a0,assignRva=0x190adf0;
    if(!same(s,readRva,{0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0x39,0x6f,0x58,0x03,0x48})||
       !same(s,writeRva,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x8b,0xec,0x48,0x81,0xec,0x80,0x00,0x00,0x00})||
       !same(s,assignRva,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x39,0x48,0x8b,0xf2,0x48,0x8b,0xd9}))
        throw std::runtime_error("gateway body functions differ");
    using RequestFn=void(*)(void*,uint8_t);
    auto request=reinterpret_cast<RequestFn>(s.base+requestRva);auto* sp=&s;const uint64_t base=s.base;
    auto buffer=std::make_shared<std::vector<uint8_t>>();
    Api a;
    a.state=[sp,base,taskSlot]{
        State st;uint64_t task=0;
        if(!sp->read(base+taskSlot,&task,8)||!task)return st;
        uint32_t ts=0,err=0;uint8_t step=0;
        if(!sp->read(task+8,&ts,4)||!sp->read(task+0x38,&step,1)||!sp->read(task+0x2a8,&err,4))return st;
        st.present=true;st.taskState=ts;st.step=step;st.error=err;return st;};
    a.point=[sp,base,addressSlot,buffer](const std::string& url){
        if(url.empty()||url.size()>OnlineAddressMax)return false;
        if(buffer->empty()){
            buffer->assign(16+OnlineAddressMax+1,0);
            uint32_t header[4]={0x1000,0xffffffffu,uint32_t(url.size()),uint32_t(url.size())};
            std::memcpy(buffer->data(),header,16);std::memcpy(buffer->data()+16,url.data(),url.size());}
        uint64_t now=0;if(!sp->read(base+addressSlot,&now,8))return false;
        *reinterpret_cast<uint64_t*>(base+addressSlot)=reinterpret_cast<uint64_t>(buffer->data()+16);
        return true;};
    a.plain=[base,readRva,writeRva,assignRva]{
        assignString=reinterpret_cast<void(*)(void*,void*)>(base+assignRva);
        return redirect(base+readRva,reinterpret_cast<void*>(&plainBody))&&redirect(base+writeRva,reinterpret_cast<void*>(&plainBody));};
    a.request=[sp,base,taskSlot,request]{
        uint64_t task=0;if(!sp->read(base+taskSlot,&task,8)||!task)return false;
        request(reinterpret_cast<void*>(task),0);return true;};
    return a;
#endif
}
}
