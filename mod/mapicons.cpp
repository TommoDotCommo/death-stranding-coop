#include "mapicons.h"
#include "serverinfo.h"
#include "../world/json.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::mapicons {
Answer parse(const std::string& body){
    Answer a;
    try{
        auto d=json::parse(body);if(d.get("format").integer()!=1)return a;
        a.on=d.get("on").boolean();{const auto w=d.get("wait_ms").integer();a.waitMs=w>0?uint64_t(w):0;}
        for(auto& p:d.get("players").items()){Player n;n.id=p.get("id").string();n.name=p.get("name").string();n.x=p.get("x").real(NAN);n.y=p.get("y").real(NAN);n.z=p.get("z").real(NAN);n.area=uint32_t(p.get("area").integer());a.players.push_back(std::move(n));}
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
void setName(Icon& n,const std::string& utf8){
    auto w=serverinfo::wide(utf8);size_t k=std::min(w.size(),NameChars);
#ifndef MUTANT_MAPICONS_SPLIT
    if(k&&k<w.size()&&w[k-1]>=0xd800&&w[k-1]<0xdc00)--k;
#endif
    std::memcpy(n.name,w.data(),k*sizeof(char16_t));n.name[k]=0;
}
std::vector<Icon> icons(const Answer& a,const std::string& self){
    std::vector<Icon> out;
#ifndef MUTANT_MAPICONS_OFF
    if(!a.read||!a.on)return out;
#endif
    for(auto& p:a.players){
#ifndef MUTANT_MAPICONS_SELF
        if(!self.empty()&&p.id==self)continue;
#endif
#ifndef MUTANT_MAPICONS_PLACE
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::fabs(p.x)>100000||std::fabs(p.y)>100000||std::fabs(p.z)>100000)continue;
#endif
#ifndef MUTANT_MAPICONS_MANY
        if(out.size()==MaxIcons)break;
#endif
        uint64_t h=1469598103934665603ull;for(unsigned char c:p.id){h^=c;h*=1099511628211ull;}
        Icon n;n.x=float(p.x);n.y=float(p.y);n.z=float(p.z);n.area=p.area;n.id=0x4d50000000000000ull|(h&0xffffffffffffull);setName(n,p.name);out.push_back(n);
    }
    return out;
}
void Board::publish(const std::vector<Icon>& v){
    const unsigned next=(current_.load(std::memory_order_relaxed)+1)%Slots;Picture& p=slots_[next];
    p.count=uint32_t(std::min(v.size(),MaxIcons));for(uint32_t i=0;i<p.count;++i)p.icons[i]=v[i];
    current_.store(next,std::memory_order_release);
}
void Board::read(Picture& out) const {out=slots_[current_.load(std::memory_order_acquire)];}
bool Board::nameOf(uint64_t id,char16_t (&out)[NameChars+1]) const {
    const Picture& p=slots_[current_.load(std::memory_order_acquire)];
    for(uint32_t i=0;i<p.count&&i<MaxIcons;++i)if(p.icons[i].id==id){
#ifndef MUTANT_MAPICONS_NAME
        std::memcpy(out,p.icons[i].name,sizeof out);
#else
        std::memcpy(out,p.icons[0].name,sizeof out);
#endif
        return out[0]!=0;}
    return false;
}
void fillItem(uint8_t (&item)[ItemSize],const Icon& n,uint8_t kind){
    std::memset(item,0,ItemSize);const float place[4]={n.x,n.y,n.z,0};const float alpha=1;
    std::memcpy(item,place,16);std::memcpy(item+0x10,place,16);item[0x20]=kind;std::memcpy(item+0x28,&n.id,8);std::memcpy(item+0x3c,&alpha,4);
}
uint64_t pollAfter(uint64_t own,uint64_t server){
#ifndef MUTANT_MAPICONS_WAIT
    return std::max(own,std::min(server,MaxWaitMs));
#else
    return own;
#endif
}
uint64_t delayAfter(bool good,unsigned& failures){if(good){failures=0;return PollMs;}return ++failures<3?PollMs:BackoffMs;}
Link::Link(Get get,Board& board,std::string self,Schedule schedule):get_(std::move(get)),board_(board),self_(std::move(self)),schedule_(std::move(schedule)){
    if(!schedule_)schedule_=delayAfter;
    thread_=std::thread([this]{run();});}
Link::~Link(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
void Link::setExtra(std::vector<Icon> v){{std::lock_guard<std::mutex> g(lock_);extra_=std::move(v);}wake_.notify_all();}
Link::Counters Link::counters() const {std::lock_guard<std::mutex> g(lock_);return counters_;}
void Link::run(){
    unsigned failures=0;std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        g.unlock();auto r=get_();g.lock();++counters_.reads;
        Answer a;if(r.status==200)a=parse(r.body);
        auto v=icons(a,self_);
#ifndef MUTANT_MAPICONS_STUCK
        if(a.read||failures>=2){
#else
        if(a.read&&a.on){
#endif
            counters_.on=a.read&&a.on;v.insert(v.end(),extra_.begin(),extra_.end());counters_.shown=unsigned(std::min(v.size(),MaxIcons));board_.publish(v);}
        const uint64_t wait=pollAfter(schedule_(a.read,failures),a.waitMs);
        auto seen=extra_.size();wake_.wait_for(g,std::chrono::milliseconds(wait),[&]{return stop_||extra_.size()!=seen;});
    }
}
#ifdef _WIN32
namespace {
constexpr uint64_t CallSite=0x31bac63,Gather=0x31bda70,AddItem=0x31a8780,Stub=0x31ba24b,LabelSite=0x319c5e6,Label=0x31c5fd0,LabelStub=0x31b9cfb;
using GatherFn=void(*)(void*,float);using AddFn=bool(*)(void*,void*);using Text=uint64_t;using LabelFn=void(*)(void*,Text*,Text*,uint8_t);
LabelFn labelOriginal=nullptr;void(*textMake)(Text*,const char16_t*)=nullptr;void(*textAssign)(Text*,const Text*)=nullptr;void(*textDrop)(Text*)=nullptr;
std::atomic<const Board*> board{nullptr};std::atomic<uint8_t> kind{DefaultKind};GatherFn original=nullptr;AddFn addItem=nullptr;
std::atomic<uint64_t> updates{0},taken{0},refused{0};std::atomic<uint32_t> shownArea{0},mode{0};
// The map's own update, before it gathers its icons (sorting and slots are done in there). No allocation, no lock.
void hook(void* map,float dt){
    if(auto* b=board.load(std::memory_order_acquire)){
        auto* m=static_cast<uint8_t*>(map);uint32_t area=0;
        if(auto* shown=*reinterpret_cast<uint8_t**>(m+0x1d10))area=*reinterpret_cast<uint16_t*>(shown+0x20);
        shownArea.store(area,std::memory_order_relaxed);mode.store(m[0x1d1a],std::memory_order_relaxed);updates.fetch_add(1,std::memory_order_relaxed);
        unsigned no=0;const unsigned yes=addIcons(*b,area,kind.load(std::memory_order_relaxed),[&](uint8_t* item){if(addItem(map,item))return true;++no;return false;});
        taken.fetch_add(yes,std::memory_order_relaxed);refused.fetch_add(no,std::memory_order_relaxed);
    }
    original(map,dt);
}
// The text of the icon under the cursor: the game's, then this player's name for an icon of the mod's.
void label(void* item,Text* title,Text* detail,uint8_t flag){
    labelOriginal(item,title,detail,flag);
    auto* b=board.load(std::memory_order_acquire);if(!b||!item||!title)return;
    uint64_t id=0;std::memcpy(&id,static_cast<uint8_t*>(item)+0x28,8);if((id>>56)!=0x4d)return;
    char16_t name[NameChars+1];if(!b->nameOf(id,name))return;
    Text t=0;textMake(&t,name);textAssign(title,&t);textDrop(&t);
}
void writeCode(uint64_t at,const void* bytes,size_t n){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("map code cannot be written");
    std::memcpy(reinterpret_cast<void*>(at),bytes,n);VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);}
}
void install(discovery::Sampler& s,const Board& b){
    static const uint8_t site[13]={0xe8,0x08,0x2e,0x00,0x00,0x48,0x8b,0xcb,0xe8,0xe0,0x6f,0x00,0x00};uint8_t got[13]{},pad[12]{};
    static const uint8_t addHead[16]={0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x60,0xff,0xff,0xff,0x48,0x81,0xec,0xa0};uint8_t head[16]{};
    if(!s.read(s.base+CallSite,got,13)||std::memcmp(got,site,13))throw std::runtime_error("map update differs");
    if(!s.read(s.base+AddItem,head,16)||std::memcmp(head,addHead,16))throw std::runtime_error("map icon list differs");
    if(!s.read(s.base+Stub,pad,12))throw std::runtime_error("map padding unreadable");for(auto c:pad)if(c!=0xcc)throw std::runtime_error("map padding in use");
    static const uint8_t labelCall[9]={0xe8,0xe5,0x99,0x02,0x00,0x48,0x8b,0x4d,0x10};uint8_t labelGot[9]{},labelPad[12]{};
    if(!s.read(s.base+LabelSite,labelGot,9)||std::memcmp(labelGot,labelCall,9))throw std::runtime_error("map label differs");
    if(!s.read(s.base+LabelStub,labelPad,12))throw std::runtime_error("map padding unreadable");for(auto c:labelPad)if(c!=0xcc)throw std::runtime_error("map padding in use");
    static const uint8_t makeHead[6]={0x40,0x53,0x48,0x83,0xec,0x20},assignHead[5]={0x48,0x89,0x5c,0x24,0x08},dropHead[5]={0x48,0x8b,0x09,0xb8,0xff};uint8_t a[6]{},b2[5]{},c2[5]{};
    if(!s.read(s.base+0x191ca20,a,6)||std::memcmp(a,makeHead,6)||!s.read(s.base+0x191caf0,b2,5)||std::memcmp(b2,assignHead,5)||!s.read(s.base+0x191cad0,c2,5)||std::memcmp(c2,dropHead,5))throw std::runtime_error("game strings differ");
    labelOriginal=reinterpret_cast<LabelFn>(s.base+Label);textMake=reinterpret_cast<void(*)(Text*,const char16_t*)>(s.base+0x191ca20);textAssign=reinterpret_cast<void(*)(Text*,const Text*)>(s.base+0x191caf0);textDrop=reinterpret_cast<void(*)(Text*)>(s.base+0x191cad0);
    original=reinterpret_cast<GatherFn>(s.base+Gather);addItem=reinterpret_cast<AddFn>(s.base+AddItem);board.store(&b,std::memory_order_release);
    uint8_t stub[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target=reinterpret_cast<void*>(hook);std::memcpy(stub+2,&target,8);writeCode(s.base+Stub,stub,12);
    const int32_t rel=int32_t(int64_t(Stub)-int64_t(CallSite+5));writeCode(s.base+CallSite+1,&rel,4);   // four aligned bytes: one store
    uint8_t stub2[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target2=reinterpret_cast<void*>(label);std::memcpy(stub2+2,&target2,8);writeCode(s.base+LabelStub,stub2,12);
    const int32_t rel2=int32_t(int64_t(LabelStub)-int64_t(LabelSite+5));writeCode(s.base+LabelSite+1,&rel2,4);   // the map is closed now: nothing runs this
}
void setKind(uint8_t k){kind.store(k,std::memory_order_relaxed);}
Stats stats(){return {updates.load(std::memory_order_relaxed),taken.load(std::memory_order_relaxed),refused.load(std::memory_order_relaxed),shownArea.load(std::memory_order_relaxed),mode.load(std::memory_order_relaxed)};}
std::function<uint32_t()> nativeArea(discovery::Sampler& s){
    static const uint8_t want[12]={0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x7d,0xe8,0x4b,0x05,0x48};uint8_t got[12]{};
    if(!s.read(s.base+0x262a860,got,12)||std::memcmp(got,want,12))throw std::runtime_error("area function differs");
    auto* sp=&s;const uint64_t base=s.base;
    return [sp,base]()->uint32_t{uint64_t module=0;if(!sp->read(base+0x7be6318,&module,8)||!module)return 0;const uint32_t a=reinterpret_cast<uint16_t(*)(uint64_t)>(base+0x262a860)(module);return a==0xffff?0:a;};
}
#else
void install(discovery::Sampler&,const Board&){throw std::runtime_error("the map needs the game");}
void setKind(uint8_t){}
Stats stats(){return {};}
std::function<uint32_t()> nativeArea(discovery::Sampler&){throw std::runtime_error("the map needs the game");}
#endif
}
