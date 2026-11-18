#include "help.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>
namespace mod::help {
std::u16string keyName(unsigned vk){
    if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))return std::u16string(1,char16_t(vk));
    if(vk>=0x70&&vk<=0x87){auto n=std::to_string(vk-0x6f);return u"F"+std::u16string(n.begin(),n.end());}
    return u"?";
}
std::u16string body(const Facts& f){
    std::u16string out=keyName(Key)+u" (hold)  This page\n"+keyName(0x71)+u"  Mark a bug for the report";
    auto line=[&out](unsigned key,const char16_t* what){if(key)out+=u"\n"+keyName(key)+u"  "+what;};
    line(f.ping,u"Ping");
    line(f.like?0x4c:0,u"Like the partner");
    line(f.ride,u"Ride along");
    line(f.route,u"Record a route");
#ifndef MUTANT_HELP_MATERIALS
    line(f.materials,u"Drop test materials");
#else
    line(f.materials?f.materials:0x55,u"Drop test materials");
#endif
    line(f.story,u"Catch up with the story");
    line(f.server,u"Server and players");
    line(f.debug,u"Test mode switches");
    return out;
}
void Page::close(){
    if(!shown_)return;
    api_.hide();shown_=false;log_("HELP hidden\n");
}
void Page::tick(bool held,bool allowed,const Facts& facts,uint64_t now){
#ifndef MUTANT_HELP_EVENT
    const bool want=held&&allowed;
#else
    const bool want=held;
#endif
#ifndef MUTANT_HELP_RELEASE
    if(!want){close();return;}
#else
    if(!want)return;
#endif
#ifndef MUTANT_HELP_AGAIN
    if(shown_)return;
#endif
#ifndef MUTANT_HELP_RETRY
    if(now<retryAt_)return;
#endif
    if(api_.show(Title,body(facts),place)){shown_=true;log_("HELP shown\n");}
    else retryAt_=now+RetryMs;
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native help API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    // DSUINodeGraphBindings_sShowCommonTelop (pivot, x, y, text, text, seconds) and its hide (kind, immediately).
    const std::array<Entry,2> entries={{
        {0x3055f40,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x81,0xec,0xa0,0x01,0x00,0x00,0xc5,0xf8,0x29,0x70,0xe8,0xc5,0xf8,0x29,0x78,0xd8,0x48,0x8b,0x05}},
        {0x3054b10,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb6,0xda,0x8b,0xf9,0xe8,0x7c,0xda,0xfb,0xff}}}};
    std::array<unsigned,2> counts{};
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("help code size cap");
        std::vector<uint8_t> code(section.size);if(!s.readOnce(section.address,code.data(),code.size()))throw std::runtime_error("help code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];if(section.address+size_t(at-code.begin())!=s.base+e.rva)throw std::runtime_error("help function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)if(n!=1)throw std::runtime_error("help function signature missing/ambiguous");
    using ShowFn=void(*)(int,float,float,const char16_t*,const char16_t*,float);using HideFn=void(*)(int,uint8_t);
    auto show=reinterpret_cast<ShowFn>(s.base+entries[0].rva);auto hide=reinterpret_cast<HideFn>(s.base+entries[1].rva);
    auto* sp=&s;const uint64_t ownerSlot=s.base+0x7c00128;
    auto ready=[sp,ownerSlot]{uint64_t owner=0,telops=0;return sp->read(ownerSlot,&owner,8)&&owner&&sp->read(owner+0xa0,&telops,8)&&telops;};
    Api a;
    a.show=[show,ready](const std::u16string& title,const std::u16string& text,const Place& p){
        if(!ready())return false;
        show(p.pivot,p.x,p.y,title.c_str(),text.c_str(),ShowSeconds);return true;};
    a.hide=[hide,ready]{if(ready())hide(0,1);};
    return a;
#endif
}
}
