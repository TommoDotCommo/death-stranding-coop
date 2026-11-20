#include "hudmarkers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
uint8_t scaleAlias(uint8_t k){
    // 0x3089aa0's table at 0x3089bf8 (kind-9 for kinds 9..0x2d): the kind whose item it reads next; 0x1a reads none.
    static const uint8_t alias[0x25]={0x33,0x3b,0x39,0x34,0x3c,0x3a,0x4d,0x4e,0x4f,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x1a,
                                      0x6b,0x7a,0x87,0x6c,0x7b,0x88,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x61,0x62,0x63,0x64,0x66,0x67};
    return k>=9&&k<=0x2d?alias[k-9]:k;
}
bool safeSource(uint8_t k){
#ifndef MUTANT_HUDMARKERS_SOURCE
    return k>=1&&k<=LastKind&&k!=0xaf&&k!=DefaultKind;
#else
    return k>=1&&k<=LastKind;
#endif
}
bool Restyle::due(size_t i,const SlotState& s,uint8_t mark,uint8_t target,uint64_t frame){
    if(i>=HudSlots)return false;
    auto& m=seen_[i];
#ifndef MUTANT_HUDMARKERS_GAMESLOT
    if(s.kind!=mark||!ownId(s.id)){m=Seen{};return false;}   // free, the game's own or another kind: never touched
#else
    if(s.kind!=mark){m=Seen{};return false;}
#endif
    if(s.state!=SlotAssigned){if(m.valid)m.lapsed=true;return false;}   // the game assigns it again when it is back in 2
    bool same=m.valid&&m.id==s.id;   // still the slot as the mod left it: the game has not assigned it since
#ifndef MUTANT_HUDMARKERS_INDEX
    same=same&&m.index==s.index;
#endif
#ifndef MUTANT_HUDMARKERS_LAPSE
    same=same&&!m.lapsed;
#endif
    if(target==mark){
#ifndef MUTANT_HUDMARKERS_BACK
        if(same&&m.target!=mark)return true;   // it shows the mod's look: the kind's own again
#endif
        m=Seen{};return false;                 // the game's own look already
    }
    if(!same||m.target!=target)return true;
#ifndef MUTANT_HUDMARKERS_RETRY
    return s.other&&frame>=m.frame+RetryFrames;
#else
    (void)frame;return s.other;
#endif
}
void Restyle::done(size_t i,const SlotState& s,uint8_t mark,uint8_t target,uint64_t frame){
    if(i>=HudSlots)return;
    auto& m=seen_[i];if(target==mark){m=Seen{};return;}
    m.valid=true;m.lapsed=false;m.target=target;m.index=s.index;m.id=s.id;m.frame=frame;
}
namespace {
template<class T> bool get(const Read& read,uint64_t at,T& v){return at&&read(at,&v,sizeof v);}
// A counted string's bytes (the field points at them): printable ASCII, at most 96.
std::string text(const Read& read,uint64_t at){
    std::string s;if(!at)return s;
    auto put=[&](char c){if(!c)return false;s.push_back(c>=0x20&&c<0x7f?c:'?');return true;};
    for(size_t done=0;done<96;done+=16){
        char chunk[16];
        if(read(at+done,chunk,sizeof chunk)){for(char c:chunk)if(!put(c))return s;continue;}
        for(size_t k=0;k<sizeof chunk;++k){char c=0;if(!read(at+done+k,&c,1)||!put(c))return s;}   // a page ends inside
    }
    return s;
}
std::string hex(uint64_t v,int width=0){char b[24];std::snprintf(b,sizeof b,"0x%0*llx",width,static_cast<unsigned long long>(v));return b;}
}
std::vector<std::string> iconTable(const Read& read,uint64_t icons){
    std::vector<std::string> rows;std::string none;unsigned with=0;
    for(unsigned k=0;k<=LastKind;++k){
        uint64_t item=0,icon=0;
        if(!get(read,icons+0x20+8*uint64_t(k),item)){rows.push_back("HUD icon table unreadable at kind "+hex(k,2)+"\n");return rows;}
        if(!item){none+=" "+hex(k,2);continue;}
        ++with;get(read,item+0x20,icon);
        float height=0,scale=0;int32_t count=0;uint64_t data=0;
        if(icon){get(read,icon+0x38,height);get(read,icon+0x40,scale);get(read,icon+0x20,count);get(read,icon+0x28,data);}
        char head[200];std::snprintf(head,sizeof head,"HUD icon %s item %s icon %s height %.2f scale %.2f textures %d",hex(k,2).c_str(),hex(item).c_str(),hex(icon).c_str(),double(height),double(scale),int(count));
        std::string row=head;
        for(int32_t t=0;data&&t<count&&t<8;++t){
            uint64_t texture=0,name=0;int32_t size[2]{};get(read,data+8*uint64_t(t),texture);
            if(texture){get(read,texture+0x48,size);get(read,texture+0x40,name);}
            row+=" | "+hex(texture)+" "+std::to_string(size[0])+"x"+std::to_string(size[1])+" '"+text(read,name)+"'";
        }
        rows.push_back(row+"\n");
    }
    rows.push_back("HUD icons: "+std::to_string(with)+" kinds with an item; none:"+none+"\n");
    return rows;
}
std::vector<FarName> farNames(const mapicons::Board::Picture& p,const std::vector<Mark>& marks,size_t most){
    std::vector<FarName> out;
#ifndef MUTANT_HUDMARKERS_NAMES_LIMIT
    most=std::min(most,MaxFarNames);
#endif
    for(const auto& m:marks){
        if(out.size()>=most)break;
        const mapicons::Icon* icon=nullptr;
        for(uint32_t i=0;i<p.count&&i<mapicons::MaxIcons;++i)if(((p.icons[i].id^m.id)&0xffffffffffffull)==0){icon=&p.icons[i];break;}
        if(!icon)continue;
        std::u16string name;
        for(size_t k=0;k<mapicons::NameChars&&icon->name[k];++k){const char16_t c=icon->name[k];
#ifndef MUTANT_HUDMARKERS_MARKUP
            if(c=='<'||c=='>'||c=='{'||c=='}')continue;   // the UI's markup characters (as nametag::fromUtf8)
#endif
            name.push_back(c);}
        if(name.empty())continue;
        FarName f;f.key=m.id;f.at={m.x,m.y,double(m.z)+NameHeight};f.name=std::move(name);out.push_back(std::move(f));
    }
    return out;
}
namespace {
std::atomic<uint8_t> source{DefaultIconSource};std::atomic<bool> styled{DefaultIconSource!=NoIcon};   // styled: a source was ever set
}
bool setIconSource(uint8_t k){
    if(k!=NoIcon&&!safeSource(k))return false;
    source.store(k,std::memory_order_relaxed);if(k!=NoIcon)styled.store(true,std::memory_order_relaxed);return true;
}
uint8_t iconSource(){return source.load(std::memory_order_relaxed);}
#ifdef _WIN32
namespace {
constexpr uint64_t CallSite=0x3096571,Sort=0x30934b0,Add=0x308ea00,Stub=0x309694c,Assign=0x3091090,Shown=0x1dde990,LogicVtable=0x3e53b50;
// The marker HUD logic L: the list at +0x1a3c0, the icon resource at +0x1a358, 256 slots of 0x130 from +0x80. A slot:
// layer count +0x10 and array +0x18, kind +0xac, id +0xb0, record index +0xbc, state +0xca. A layer's texture binding +0x158.
constexpr uint64_t ListInLogic=0x1a3c0,IconsInLogic=0x1a358,FirstSlot=0x80,SlotSize=0x130,SlotLayers=0x10,SlotLayerArray=0x18,
                   SlotKind=0xac,SlotId=0xb0,SlotIndex=0xbc,SlotStateAt=0xca,LayerTexture=0x158;
using SortFn=void(*)(void*);using AddFn=void(*)(void*,uint8_t,const float*,void*,uint64_t,uint8_t);
using AssignFn=void(*)(void*,void*,uint8_t);using ShownFn=void*(*)(void*);
std::atomic<const Board*> board{nullptr};std::atomic<uint8_t> kind{DefaultKind};SortFn sort=nullptr;AddFn add=nullptr;
AssignFn assign=nullptr;ShownFn shown=nullptr;uint64_t imageBase=0;
std::atomic<uint64_t> frames{0},added{0},kept{0},restyled{0},lastLogic{0};
Restyle restyle;   // the HUD job's own
template<class T> T at(const uint8_t* p,uint64_t offset){T v;std::memcpy(&v,p+offset,sizeof v);return v;}
// The mod's slots get the look of the source kind (or their own back), with the game's own assignment 0x3091090.
void restyleSlots(uint8_t* logic,uint8_t mark,uint64_t frame){
    const uint8_t from=source.load(std::memory_order_relaxed),target=from==NoIcon?mark:from;
    auto* icons=at<uint8_t*>(logic,IconsInLogic);if(!icons)return;
    void* want[MaxLayers]{};int32_t wanted=0;
    if(target!=mark){
        auto item=[icons](uint8_t k){return at<uint8_t*>(icons,0x20+8*uint64_t(k));};
        uint8_t* it=item(target);uint8_t* icon=it?at<uint8_t*>(it,0x20):nullptr;
        if(!icon||!item(scaleAlias(target)))return;   // nothing to draw with, or the scale would read a missing item: no change
        const int32_t n=at<int32_t>(icon,0x20);auto* data=at<void**>(icon,0x28);if(n<=0||!data)return;
        wanted=std::min<int32_t>(n,int32_t(MaxLayers));for(int32_t j=0;j<wanted;++j)want[j]=data[j];
    }
    for(size_t i=0;i<HudSlots;++i){
        uint8_t* slot=logic+FirstSlot+SlotSize*i;
        SlotState s;s.kind=slot[SlotKind];s.state=slot[SlotStateAt];s.id=at<uint64_t>(slot,SlotId);s.index=at<uint32_t>(slot,SlotIndex);
        const int32_t layers=at<int32_t>(slot,SlotLayers);auto* layer=at<uint8_t**>(slot,SlotLayerArray);
        const bool usable=layer&&layers>0&&layers<=int32_t(MaxLayers);
        if(usable&&target!=mark&&s.kind==mark&&ownId(s.id)&&s.state==SlotAssigned)
            for(int32_t j=0;j<layers&&j<wanted;++j){uint8_t* e=layer[j];if(!e||!at<void*>(e,LayerTexture))continue;
                void* now=shown(e+LayerTexture);if(now&&now!=want[j]){s.other=true;break;}}
        if(!restyle.due(i,s,mark,target,frame)||!usable)continue;
        slot[SlotKind]=target;assign(logic,slot,1);slot[SlotKind]=s.kind;
        restyle.done(i,s,mark,target,frame);restyled.fetch_add(1,std::memory_order_relaxed);
    }
}
// The HUD's sort, after the game has added its own records: the mod's first. No allocation of the mod's, no lock.
void hook(void* list){
    if(auto* b=board.load(std::memory_order_acquire)){
        Board::Picture p;b->read(p);const uint8_t k=kind.load(std::memory_order_relaxed);
        const int32_t before=*reinterpret_cast<const int32_t*>(static_cast<uint8_t*>(list)+8);   // the list's count
        for(uint32_t i=0;i<p.count&&i<MaxMarks;++i){alignas(16) const float to[4]={p.marks[i].x,p.marks[i].y,p.marks[i].z,0};add(list,k,to,nullptr,p.marks[i].id,0);}
        const int32_t after=*reinterpret_cast<const int32_t*>(static_cast<uint8_t*>(list)+8);
        const uint64_t frame=frames.fetch_add(1,std::memory_order_relaxed);added.fetch_add(p.count,std::memory_order_relaxed);if(after>before)kept.fetch_add(uint64_t(after-before),std::memory_order_relaxed);
        auto* logic=static_cast<uint8_t*>(list)-ListInLogic;
        if(at<uint64_t>(logic,0)==imageBase+LogicVtable){
            lastLogic.store(reinterpret_cast<uint64_t>(logic),std::memory_order_relaxed);
            if(styled.load(std::memory_order_relaxed))restyleSlots(logic,k,frame);   // never before a source was set
        }
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
    // The slot assignment (its first test reads the slot's kind at +0xac against 0xaf) and the layer's texture getter.
    static const uint8_t assignHead[20]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x58,0x80,0xba,0xac,0x00,0x00,0x00,0xaf};
    static const uint8_t shownHead[16]={0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x33,0xc9,0x89};
    uint8_t got[8]{},a[16]{},c[16]{},g[20]{},h[16]{},pad[12]{};
    if(!s.read(s.base+CallSite-3,got,8)||std::memcmp(got,site,8))throw std::runtime_error("HUD marker sort call differs");
    if(!s.read(s.base+Add,a,16)||std::memcmp(a,addHead,16))throw std::runtime_error("HUD marker adder differs");
    if(!s.read(s.base+Sort,c,16)||std::memcmp(c,sortHead,16))throw std::runtime_error("HUD marker sort differs");
    if(!s.read(s.base+Assign,g,20)||std::memcmp(g,assignHead,20))throw std::runtime_error("HUD slot assignment differs");
    if(!s.read(s.base+Shown,h,16)||std::memcmp(h,shownHead,16))throw std::runtime_error("HUD texture getter differs");
    if(!s.read(s.base+Stub,pad,12))throw std::runtime_error("HUD padding unreadable");for(auto x:pad)if(x!=0xcc)throw std::runtime_error("HUD padding in use");
    imageBase=s.base;sort=reinterpret_cast<SortFn>(s.base+Sort);add=reinterpret_cast<AddFn>(s.base+Add);
    assign=reinterpret_cast<AssignFn>(s.base+Assign);shown=reinterpret_cast<ShownFn>(s.base+Shown);board.store(&b,std::memory_order_release);
    uint8_t stub[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target=reinterpret_cast<void*>(hook);std::memcpy(stub+2,&target,8);writeCode(s.base+Stub,stub,12);
    const int32_t rel=int32_t(int64_t(Stub)-int64_t(CallSite+5));writeCode(s.base+CallSite+1,&rel,4);
}
void setKind(uint8_t k){if(safeKind(k))kind.store(k,std::memory_order_relaxed);}
std::vector<std::string> iconTableNow(discovery::Sampler& s){
    const uint64_t logic=lastLogic.load(std::memory_order_relaxed);uint64_t vtable=0,icons=0;
    if(!logic)return {"HUD icons: no HUD seen yet (player_hud on, in the world)\n"};
    if(!s.read(logic,&vtable,8)||vtable!=s.base+LogicVtable||!s.read(logic+IconsInLogic,&icons,8)||!icons)return {"HUD icons: the HUD logic is gone or unreadable\n"};
    return iconTable([&s](uint64_t a,void* p,size_t n){return s.read(a,p,n);},icons);
}
Stats stats(){return {frames.load(std::memory_order_relaxed),added.load(std::memory_order_relaxed),kept.load(std::memory_order_relaxed),restyled.load(std::memory_order_relaxed)};}
#else
void install(discovery::Sampler&,const Board&){throw std::runtime_error("the HUD needs the game");}
void setKind(uint8_t){}
std::vector<std::string> iconTableNow(discovery::Sampler&){return {"HUD icons: the HUD needs the game\n"};}
Stats stats(){return {};}
#endif
}
