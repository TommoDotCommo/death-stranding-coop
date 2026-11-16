#include "received.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::received {
bool active(bool option,bool server,bool signedIn){
#ifndef MUTANT_RECEIVED_OPTION
    if(!option)return false;
#endif
#ifndef MUTANT_RECEIVED_SERVER
    if(!server||!signedIn)return false;              // only what our own server handed this game
#endif
    return true;
}
bool pending(const structures::Record& r){
#ifndef MUTANT_RECEIVED_BUILT
    return r.state==0;                               // 1 and 2 are built; the game has dealt with it
#else
    (void)r;return true;
#endif
}
bool candidate(const own_strand::Structure& x){
#ifndef MUTANT_RECEIVED_PLAYER
    if(x.owner!=own_strand::Net)return false;        // the porter's own (Player) and the game's (Stage) never
#else
    if(x.owner==own_strand::Game)return false;
#endif
    const uint8_t f=x.flags[Flags-own_strand::ObjectFields],g=x.flags[Removal-own_strand::ObjectFields];
    if(!(f&Placement))return false;                  // placed, or never asked to be
#ifndef MUTANT_RECEIVED_SAVED
    if(f&NoRange)return false;                       // the save's records of others' structures carry 0x10 already
#endif
#ifndef MUTANT_RECEIVED_REMOVAL
    if((f&Removing)||(g&RemovingBit))return false;   // the game has refused it: leave its removal alone
#else
    (void)g;
#endif
    return true;
}
bool fresh(const own_strand::Structure& x){
    if(!pending(x.record)||!candidate(x))return false;
#ifndef MUTANT_RECEIVED_ONLINE
    if(x.online.empty())return false;                // not from a server (the mod's own Net copies have no online id)
#endif
    return true;
}
std::vector<Change> Builder::look(const std::vector<own_strand::Structure>& structures){
    std::vector<Change> out;
    for(auto& x:structures){
        if(!fresh(x))continue;
#ifndef MUTANT_RECEIVED_ONCE
        if(!done_.insert({x.record.id,x.online}).second)continue;
#endif
        Change c;c.object=x.record.object;c.id=x.record.id;c.type=x.record.type;c.online=x.online;
        c.before=x.flags[Flags-own_strand::ObjectFields];c.after=built(c.before);out.push_back(c);
    }
    return out;
}
std::vector<Change> Builder::step(const discovery::Sampler& s,uint64_t base,uint64_t imageSize,const Write& write){
    std::vector<Change> out;
    auto seen=own_strand::readStructures(s,base,imageSize,{pending,candidate});
    for(auto& c:look(seen)){
        uint8_t now[2]{};
#ifndef MUTANT_RECEIVED_RECHECK
        if(!s.read(c.object+Flags,now,2)||now[0]!=c.before||(now[1]&RemovingBit)){++skipped_;continue;}
#else
        (void)now;
#endif
        if(!write||!write(c.object+Flags,c.after)){++skipped_;continue;}
        ++changed_;out.push_back(c);
    }
    return out;
}
std::string Builder::line(const Change& c){
    char b[200];
    std::snprintf(b,sizeof b,"RECEIVED building %s %s (range and placement checks skipped): record %u, type %u, +0xaa %02x -> %02x\n",
        name(c.type),c.online.c_str(),c.id,unsigned(c.type),unsigned(c.before),unsigned(c.after));
    return b;
}
const char* name(uint8_t t){
    switch(own_strand::letter(t)){
        case 's':return "safe house";case 'p':return "post";case 'w':return "watchtower";case 'a':return "catapult";
        case 'c':return "charger";case 'n':return "rain shelter";case 'z':return "zipline";case 'l':return "ladder";
        case 'r':return "climbing rope";case 'b':return "bridge";case 'g':return "chiral bridge";case 'j':return "jump stand";
        default:return "structure";
    }
}
Builder::Write nativeWrite(){
#ifdef _WIN32
    return [](uint64_t at,uint8_t value){MEMORY_BASIC_INFORMATION info{};
        if(!at||at>=0x800000000000ULL||VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)!=sizeof info
           ||info.State!=MEM_COMMIT||info.Protect!=PAGE_READWRITE)return false;
        *reinterpret_cast<volatile uint8_t*>(at)=value;return true;};
#else
    return [](uint64_t,uint8_t){return false;};
#endif
}
bool early(bool option,bool server){
#ifndef MUTANT_RECEIVED_EARLY_OPTION
    if(!option)return false;
#endif
    return server;
}
namespace {
// The builder's prologue, and its flag stores from AnswerFlags (build 13300582):
//   +0x00 c6 85 b3 05 00 00 00   mov byte [rbp+0x5b3],0     request +0x623
//   +0x07 c6 85 b5 05 00 00 01   mov byte [rbp+0x5b5],1     request +0x625 (object +0xa9 bit 7)
//   +0x0e c6 85 bd 05 00 00 01   mov byte [rbp+0x5bd],1     request +0x62d (object +0xaa 0x20)
//   +0x15 0f b6 81 10 01 00 00   movzx eax,byte [rcx+0x110] the answer's byte
//   +0x1c 88 85 b9 05 00 00      mov [rbp+0x5b9],al         request +0x629 (object +0xaf bit 7)
//   +0x22 88 85 cd 05 00 00      mov [rbp+0x5cd],al         request +0x63d (object +0xaa 0x10)
// (the request is at rbp-0x70).
const uint8_t Prologue[9]={0x40,0x55,0x53,0x41,0x54,0x41,0x55,0x41,0x57};
const uint8_t Original[AnswerFlagsSize]={0xc6,0x85,0xb3,0x05,0x00,0x00,0x00,0xc6,0x85,0xb5,0x05,0x00,0x00,0x01,0xc6,0x85,
    0xbd,0x05,0x00,0x00,0x01,0x0f,0xb6,0x81,0x10,0x01,0x00,0x00,0x88,0x85,0xb9,0x05,0x00,0x00,0x88,0x85,0xcd,0x05,0x00,0x00};
struct Byte{size_t at;uint8_t from,to;};
const std::vector<Byte> Changes={
#ifndef MUTANT_RECEIVED_PLACEMENT
    {0x14,0x01,0x00},                                // request +0x62d = 0: no online placement check
#endif
#ifndef MUTANT_RECEIVED_RANGE
    {0x24,0xcd,0xb9},                                // the answer's byte to +0x629 again, +0x63d keeps 1: no range test
#endif
#ifdef MUTANT_RECEIVED_HOLD
    {0x17,0x81,0x85},{0x18,0x10,0xb5},{0x19,0x01,0x05},   // the answer's byte read from request +0x625 (1): +0x629 = 1 too
#endif
};
}
Early patchAnswers(const discovery::Sampler& s,uint64_t base,const Builder::Write& code){
    uint8_t prologue[sizeof Prologue]{},now[AnswerFlagsSize]{};
    if(!s.read(base+AnswerBuilder,prologue,sizeof prologue)||!s.read(base+AnswerFlags,now,sizeof now))return Early::Differs;
    uint8_t patched[AnswerFlagsSize];std::memcpy(patched,Original,sizeof patched);
    for(auto& c:Changes)patched[c.at]=c.to;
#ifndef MUTANT_RECEIVED_IDENTITY
    if(std::memcmp(prologue,Prologue,sizeof prologue))return Early::Differs;
#ifndef MUTANT_RECEIVED_AGAIN
    if(!std::memcmp(now,patched,sizeof now))return Early::Already;
#endif
    if(std::memcmp(now,Original,sizeof now))return Early::Differs;
#endif
    std::vector<Byte> done;
    for(auto& c:Changes){
        if(!code||!code(base+AnswerFlags+c.at,c.to))break;
        done.push_back(c);}
    uint8_t back[AnswerFlagsSize]{};
    bool ok=done.size()==Changes.size();
#ifndef MUTANT_RECEIVED_READBACK
    ok=ok&&s.read(base+AnswerFlags,back,sizeof back)&&!std::memcmp(back,patched,sizeof back);
#endif
    if(ok)return Early::Patched;
#ifndef MUTANT_RECEIVED_ROLLBACK
    for(auto c=done.rbegin();c!=done.rend();++c)code(base+AnswerFlags+c->at,c->from);   // the game's code as it was
#endif
    return Early::Failed;
}
const char* label(Early e){
    switch(e){case Early::Patched:return "patched";case Early::Already:return "already";case Early::Differs:return "differs";
              case Early::Failed:return "failed";default:return "off";}
}
std::string line(Early e){
    switch(e){
        case Early::Patched:return "RECEIVED answer requests changed: served structures are made without the range and placement checks\n";
        case Early::Already:return "RECEIVED answer requests already changed\n";
        case Early::Differs:return "RECEIVED answer requests not changed: the game's code differs (the scan alone)\n";
        case Early::Failed:return "RECEIVED answer requests not changed: a write failed (the scan alone)\n";
        default:return "";
    }
}
Builder::Write nativeCodeWrite(){
#ifdef _WIN32
    return [](uint64_t at,uint8_t value){MEMORY_BASIC_INFORMATION info{};
        if(!at||at>=0x800000000000ULL||VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof info)!=sizeof info
           ||info.State!=MEM_COMMIT||info.Type!=MEM_IMAGE||!(info.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)))return false;
        DWORD old=0,ignored=0;void* p=reinterpret_cast<void*>(at);
        if(!VirtualProtect(p,1,PAGE_EXECUTE_READWRITE,&old))return false;
        *reinterpret_cast<volatile uint8_t*>(at)=value;
        VirtualProtect(p,1,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p,1);return true;};
#else
    return [](uint64_t,uint8_t){return false;};
#endif
}
}
