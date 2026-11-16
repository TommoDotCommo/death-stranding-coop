#include "received.h"
#include <cstdio>
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
}
