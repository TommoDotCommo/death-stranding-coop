#include "own_strand.h"
#include <cstdio>
#include <cstring>
#include <set>
namespace mod::own_strand {
// An engine string: a pointer to the characters, the length (u32) 8 bytes in front of them.
static std::string text(const discovery::Sampler& s,uint64_t p){
    uint32_t n=0;if(!p||!s.read(p-8,&n,4)||!n)return {};
#ifndef MUTANT_OWN_IDBOUND
    if(n>MaxId)return {};                            // no id of the game's is this long: not a string
#else
    if(n>MaxId)n=MaxId;
#endif
    std::string out(n,'\0');if(!s.read(p,out.data(),n))return {};
    return out;
}
std::vector<Structure> readStructures(const discovery::Sampler& s,uint64_t base,uint64_t imageSize,Select select){
    std::vector<Structure> out;uint64_t manager=0;
    if(!s.read(base+ConstructionGlobal,&manager,8)||!manager)return out;
    for(auto& r:structures::readTable(s,manager,base,imageSize)){
        if(select.record&&!select.record(r))continue;
        Structure x;x.record=r;uint64_t id=0;unsigned char f[0x18];
        if(!s.read(r.object+ObjectFields,f,sizeof f))continue;
        std::memcpy(x.flags.data(),f,4);x.owner=f[0xb2-ObjectFields];std::memcpy(&x.creator,f+(0xb4-ObjectFields),4);std::memcpy(&id,f+(0xb8-ObjectFields),8);
        if(select.object&&!select.object(x))continue;
        x.online=text(s,id);out.push_back(x);
    }
    return out;
}
std::vector<Online> readOnline(const discovery::Sampler& s,uint64_t base){
    std::vector<Online> out;uint64_t manager=0,table=0;uint32_t head[2]={};
    if(!s.read(base+OnlineGlobal,&manager,8)||!manager||!s.read(manager+0x1e8,&table,8)||!s.read(manager+0x1f0,head,8)||!table)return out;
    uint32_t capacity=head[1];if(capacity>MaxAreas)return out;
    for(uint32_t i=0;i<capacity;++i){
        // A map entry: +0 hash (0 = free), +8 the area id, +0x10 the area's lists.
        unsigned char e[24];if(!s.read(table+24ull*i,e,sizeof e))break;
        uint32_t hash,area;uint64_t value;std::memcpy(&hash,e,4);std::memcpy(&area,e+8,4);std::memcpy(&value,e+16,8);
        if(!hash||!value)continue;
        unsigned char v[0x38];if(!s.read(value,v,sizeof v))continue;
#ifndef MUTANT_OWN_LIST
        for(uint8_t list=0;list<3;++list){
#else
        for(uint8_t list=0;list<1;++list){
#endif
            uint32_t count;uint64_t items;std::memcpy(&count,v+8+0x10*list,4);std::memcpy(&items,v+0x10+0x10*list,8);
            if(!items||count>MaxPerList)continue;
            std::vector<uint64_t> at(count);if(count&&!s.read(items,at.data(),8ull*count))continue;
            for(auto element:at){
                unsigned char b[0x18];if(!element||!s.read(element,b,sizeof b))continue;
                Online o;uint64_t id;std::memcpy(&id,b+8,8);o.kind=b[0x10];o.state=b[0x11];std::memcpy(&o.creator,b+0x14,4);
                o.id=text(s,id);if(o.id.empty())continue;
                o.area=area;o.list=list;o.address=element;out.push_back(o);
            }
        }
    }
    return out;
}
bool mine(const Structure& x){
    // A record in state 0 is a request the game has not built (or refused); it is nobody's strand yet.
#ifndef MUTANT_OWN_PENDING
    if(x.record.state==0)return false;
#endif
#if defined(MUTANT_OWN_OWNER)
    return x.owner!=Game;
#elif defined(MUTANT_OWN_ROAD)
    return x.owner==Player||x.creator==Self;
#else
    // Not the creator: a road rebuilder is the game's (owner 0) even after this player's paving made its creator 1,
    // and what the porter built here has creator 0 (nobody known) until a server hands it back.
    return x.owner==Player;
#endif
}
bool mine(const Online& o){
#ifndef MUTANT_OWN_OTHERS
    if(o.creator!=Self)return false;
#else
    if(o.creator==0)return false;
#endif
#ifndef MUTANT_OWN_FAILED
    if(o.state>=Failed)return false;                 // refused by this game: not in its world
#endif
    return true;
}
char letter(uint8_t t){
    // The construction types (EDSConstructionPointType) the game turns into server objects and back: the Player and
    // Net variant of each (the Stage variant before them is the game's own and never on a server).
    switch(t){
        case 3:case 4:return 's';                    // safe house
        case 6:case 7:return 'p';                    // post
        case 9:case 10:return 'w';                   // watchtower
        case 12:case 13:return 'a';                  // catapult
        case 15:case 16:return 'c';                  // charger
        case 18:case 19:return 'n';                  // rain shelter
        case 22:case 23:return 'z';                  // zipline
#ifndef MUTANT_OWN_LETTER
        case 25:case 26:return 'l';                  // ladder
#else
        case 26:return 'l';
#endif
        case 28:case 29:return 'r';                  // climbing rope
        case 31:case 32:case 34:case 35:return 'b';  // bridges ("st" "30" for 30 m, else 45 m)
        case 41:case 42:return 'g';                  // chiral bridge
        case 44:case 45:return 'j';                  // jump stand
        default:return 0;
    }
}
char syncLetter(uint8_t k){
    // EDSNetSyncObjectType 0..22 (Post .. SpeedCar, the game's own letter map 0x3487ca0 read backwards); baggage and
    // missions (16..18) are no objects.
    static const char letters[23]={'p','z','c','a','s','w','b','n','r','l','v','v','t','h','e','o',0,0,0,'x','g','j','d'};
    return k<23?letters[k]:0;
}
std::vector<Item> strand(const std::vector<Structure>& structures,const std::vector<Online>& online){
    std::vector<Item> out;std::map<std::string,const Online*> byId;
    for(auto& o:online)if(!byId.count(o.id)||o.list<byId[o.id]->list)byId[o.id]=&o;
    std::set<std::string> taken;
    for(auto& x:structures){
        if(!mine(x))continue;
        Item i;i.letter=letter(x.record.type);i.online=x.online;i.placed=true;i.position=x.record.at.position;i.type=x.record.type;
        i.level=x.record.level;i.id=x.record.id;i.from="structure";
#ifndef MUTANT_OWN_AREA
        if(!x.online.empty())if(auto f=byId.find(x.online);f!=byId.end())i.area=f->second->area;
#endif
        if(!x.online.empty())taken.insert(x.online);
        out.push_back(i);
    }
    for(auto& o:online){
        if(!mine(o))continue;
#ifndef MUTANT_OWN_TWICE
        if(!taken.insert(o.id).second)continue;      // a structure of this save, or the same id in two lists
#endif
        Item i;i.letter=syncLetter(o.kind);if(!i.letter)i.letter=o.id[0];
        i.online=o.id;i.area=o.area;i.type=o.kind;i.from="online";out.push_back(i);
    }
    return out;
}
void likes(std::vector<Item>& items,const std::map<std::string,int64_t>& counts){
    for(auto& i:items){
#ifndef MUTANT_OWN_LIKES
        auto f=i.online.empty()?counts.end():counts.find(i.online);
        i.likes=f==counts.end()?-1:f->second;
#else
        auto f=counts.find(i.online);i.likes=f==counts.end()?0:f->second;
#endif
    }
}
std::string line(const Item& i){
    char b[256];
    std::snprintf(b,sizeof b,"OWN %c %s area %u %s%.1f %.1f %.1f level %u likes %lld from %s",i.letter?i.letter:'?',
        i.online.empty()?"(not on a server)":i.online.c_str(),i.area,i.placed?"at ":"at? ",i.position[0],i.position[1],i.position[2],
        i.level,(long long)i.likes,i.from);
    return b;
}
}
