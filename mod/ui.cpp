#include "ui.h"
#include <cstdio>
#include <cstring>
#include <tuple>
namespace mod::ui {
namespace {
// A page's name: the game's string is a pointer to the characters, its length in the 32 bits 8 bytes before them.
std::string pageName(const discovery::Sampler& s,uint64_t page){
    uint64_t text=0;uint32_t length=0;
    if(!s.read(page+PageName,&text,8)||text<0x10000||!s.read(text-8,&length,4))return {};
#ifndef MUTANT_UI_NAME_BOUND
    if(!length||length>MaxName)return {};
#else
    if(!length)return {};
#endif
    std::string name(length,'\0');
    if(!s.read(text,name.data(),length))return {};
    for(char c:name)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return {};
    return name;
}
}
State read(const discovery::Sampler& s,uint64_t base,bool inWorld){
    State out;out.inWorld=inWorld;
    // The state flags.
    uint64_t state=0,vtable=0;
    if(s.read(base+GameState,&state,8)&&state&&s.read(state,&vtable,8)
#ifndef MUTANT_UI_STATE_VTABLE
       &&vtable==base+GameStateVtable
#endif
       &&s.read(state+FlagsNow,&out.flags,8)&&s.read(state+FlagsNext,&out.next,8))out.flagsKnown=true;
    else out.flags=out.next=0;
    out.kind=classify(out.flagsKnown,out.flags,inWorld);
    // The menu and its open page.
    uint64_t menu=0;
    if(!s.read(base+Menu,&menu,8)||!menu||!s.read(menu,&vtable,8))return out;
#ifndef MUTANT_UI_MENU_VTABLE
    if(vtable!=base+MenuVtable)return out;
#endif
    uint64_t pages=0,owner=0,layers=0,page=0;
    if(!s.read(menu+MenuPages,&pages,8)||!pages||!s.read(pages,&vtable,8)||vtable!=base+PagesVtable||!s.read(pages+Owner,&owner,8))return out;
#ifndef MUTANT_UI_PAGES_OWNER
    if(owner!=menu)return out;
#endif
    if(!s.read(pages+PagesCurrent,&page,8)||!s.read(menu+MenuPhase,&out.phase,4))return out;
    if(s.read(menu+MenuLayers,&layers,8)&&layers&&s.read(layers,&vtable,8)&&vtable==base+LayersVtable&&
       s.read(layers+LayerCountA,&out.layerA,4)&&s.read(layers+LayerCountB,&out.layerB,4)){
#ifndef MUTANT_UI_LAYER_BOUND
        if(out.layerA<0||out.layerA>int32_t(MaxLayers)||out.layerB<0||out.layerB>int32_t(MaxLayers))return out;
#endif
        out.layers=layers;
    } else {out.layerA=out.layerB=0;return out;}
    out.menuKnown=true;out.menu=menu;out.pages=pages;out.page=page;
    if(!page)return out;
    // Which of the resource's 19 page slots it is, and its own name and identity.
    uint64_t resource=0;std::array<uint64_t,PageCount> slots{};
    if(s.read(menu+MenuResource,&resource,8)&&resource&&s.read(resource,&vtable,8)
#ifndef MUTANT_UI_RESOURCE_VTABLE
       &&vtable==base+ResourceVtable
#endif
       &&s.read(resource+PageTable,slots.data(),8*PageCount))
        for(unsigned i=0;i<PageCount;++i)if(slots[i]==page){out.pageId=int(i);break;}
    out.pageName=pageName(s,page);
    if(!s.read(page+PageIdentity,out.pageIdentity.data(),16))out.pageIdentity.fill(0);
    return out;
}
std::vector<std::string> dump(const discovery::Sampler& s,uint64_t base,bool inWorld){
    std::vector<std::string> rows;char line[160];
    auto hex=[&](const char* what,uint64_t address,size_t bytes){
        uint64_t vtable=0;std::vector<uint8_t> b(bytes);
        if(!address||!s.read(address,&vtable,8)){std::snprintf(line,sizeof line,"UI dump %s %llx unreadable",what,(unsigned long long)address);rows.push_back(line);return;}
        std::snprintf(line,sizeof line,"UI dump %s at %llx vtable image+%llx",what,(unsigned long long)address,(unsigned long long)(vtable-base));rows.push_back(line);
        if(!s.readOnce(address,b.data(),bytes))return;
        for(size_t at=0;at<bytes;at+=32){
            std::string row="UI dump  +";std::snprintf(line,sizeof line,"%03zx",at);row+=line;
            for(size_t i=at;i<at+32&&i<bytes;++i){std::snprintf(line,sizeof line,i%8?"%02x":" %02x",b[i]);row+=line;}
            rows.push_back(row);
        }
    };
    auto state=read(s,base,inWorld);
    rows.push_back("UI dump state: "+describe(state));
    if(!state.menuKnown){
        // Say which link of the chain stopped the read, so a build or a screen that differs can be told apart.
        uint64_t menu=0,vtable=0,pages=0,owner=0,layers=0;int32_t a=-1,b=-1;
        bool gotMenu=s.read(base+Menu,&menu,8)&&menu,gotVtable=gotMenu&&s.read(menu,&vtable,8);
        std::snprintf(line,sizeof line,"UI dump chain: menu %llx vtable image+%llx (want +%llx)",(unsigned long long)menu,(unsigned long long)(gotVtable?vtable-base:0),(unsigned long long)MenuVtable);rows.push_back(line);
        if(gotVtable){
            uint64_t pv=0,lv=0;
            if(s.read(menu+MenuPages,&pages,8)&&pages&&s.read(pages,&pv,8))s.read(pages+Owner,&owner,8);
            std::snprintf(line,sizeof line,"UI dump chain: pages %llx vtable image+%llx (want +%llx) owner %llx",(unsigned long long)pages,(unsigned long long)(pv?pv-base:0),(unsigned long long)PagesVtable,(unsigned long long)owner);rows.push_back(line);
            if(s.read(menu+MenuLayers,&layers,8)&&layers&&s.read(layers,&lv,8)){s.read(layers+LayerCountA,&a,4);s.read(layers+LayerCountB,&b,4);}
            std::snprintf(line,sizeof line,"UI dump chain: layers %llx vtable image+%llx (want +%llx) counts %d/%d",(unsigned long long)layers,(unsigned long long)(lv?lv-base:0),(unsigned long long)LayersVtable,a,b);rows.push_back(line);
        }
        rows.push_back("UI dump: no menu object");return rows;}
    {std::string id="UI dump page identity ";for(auto b:state.pageIdentity){std::snprintf(line,sizeof line,"%02x",b);id+=line;}rows.push_back(id);}
    hex("menu",state.menu,0x130);hex("pages",state.pages,0x60);hex("layers",state.layers,0xa0);
    for(auto [count,array,name]:{std::tuple<int32_t,uint64_t,const char*>{state.layerA,LayerArrayA,"layerA"},{state.layerB,LayerArrayB,"layerB"}}){
        uint64_t list=0;if(count<=0||!s.read(state.layers+array,&list,8)||!list)continue;
        for(int32_t i=0;i<count&&i<4;++i){
            uint64_t entry=0;if(!s.read(list+8*uint64_t(i),&entry,8)||entry<0x10000)continue;
            std::snprintf(line,sizeof line,"%s[%d] (entry %llx, object 0x40 before it)",name,i,(unsigned long long)entry);
            hex(line,entry-0x40,0x200);
        }
    }
    return rows;
}
}
