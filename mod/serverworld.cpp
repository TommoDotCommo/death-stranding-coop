#include "serverworld.h"
#include "../world/idtag.h"
#include "../world/json.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::serverworld {
Answer parse(const std::string& body){
    Answer a;
    try{
        auto d=json::parse(body);
        if(d.get("format").integer()!=1)return a;
        a.server=d.get("server").string();a.world=d.get("world").string();a.waitMs=uint64_t(std::max<int64_t>(0,d.get("wait_ms").integer()));
        for(auto& v:d.get("untagged").items())if(v.isString()&&v.string().size()==idtag::IdChars)a.untagged.push_back(v.string());
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
void Filter::publish(std::vector<std::string> untagged){
    std::sort(untagged.begin(),untagged.end());untagged.erase(std::unique(untagged.begin(),untagged.end()),untagged.end());
    auto next=std::make_unique<List>();next->count=untagged.size();next->ids.reserve(untagged.size()*idtag::IdChars);
    for(auto& id:untagged)if(id.size()==idtag::IdChars)next->ids+=id;else --next->count;
    if(const List* now=list_.load(std::memory_order_acquire);now&&now->ids==next->ids)return;
    const List* raw=next.get();list_.store(raw,std::memory_order_release);
    kept_[next_]=std::move(next);next_=(next_+1)%kept_.size();
}
size_t Filter::listed() const {auto* l=list_.load(std::memory_order_acquire);return l?l->count:0;}
bool Filter::listedHas(const char* id) const {
    const List* l=list_.load(std::memory_order_acquire);if(!l)return false;
    size_t low=0,high=l->count;
    while(low<high){
        const size_t mid=low+(high-low)/2;const int c=std::memcmp(l->ids.data()+mid*idtag::IdChars,id,idtag::IdChars);
        if(c==0)return true;
        if(c<0)low=mid+1;else high=mid;
    }
    return false;
}
void Filter::loadStarts(){newAfter_.store(UINT32_MAX,std::memory_order_release);}
void Filter::loadEnded(uint32_t ownCounter){newAfter_.store(ownCounter,std::memory_order_release);}
bool Filter::allows(uint8_t owner,const char* id,uint32_t idChars,uint32_t recordId) const {
#ifndef MUTANT_SERVERWORLD_GAME
    if(owner==OwnerGame)return true;
#endif
    if(id&&idChars){
#ifdef MUTANT_SERVERWORLD_ANY_ID
        return true;
#endif
        if(idtag::carries(key_.data(),key_.size(),id,idChars))return true;
#ifndef MUTANT_SERVERWORLD_NO_LIST
        if(idChars==idtag::IdChars&&listedHas(id))return true;
#endif
        return false;
    }
#ifndef MUTANT_SERVERWORLD_NET_COPY
    if(owner==OwnerNet)return true;
#endif
    if(owner!=OwnerPlayer)return false;
#ifdef MUTANT_SERVERWORLD_SAVE_OWN
    return true;
#endif
    const uint32_t after=newAfter_.load(std::memory_order_acquire);
    return after!=UINT32_MAX&&recordId>OwnIdsFrom&&recordId-OwnIdsFrom>after;
}
bool Filter::allowsObject(const uint8_t* o) const {
    const char* id=nullptr;std::memcpy(&id,o+OnlineIdAt,sizeof id);uint32_t chars=0,record=0;
    if(id)std::memcpy(&chars,id-8,4);
    std::memcpy(&record,o+RecordIdAt,4);
    return allows(o[OwnerAt],id,chars,record);
}
bool Filter::fromServer(uint8_t owner,const char* id,uint32_t idChars) const {
#ifndef MUTANT_SERVERWORLD_SAVE_PLAYER
    if(owner!=OwnerNet)return false;
#else
    if(owner==OwnerGame)return false;
#endif
    if(!id||!idChars)return false;
#ifdef MUTANT_SERVERWORLD_SAVE_ALL
    return false;
#endif
    return idtag::carries(key_.data(),key_.size(),id,idChars)||(idChars==idtag::IdChars&&listedHas(id));
}
bool Filter::fromServerObject(const uint8_t* o) const {
    const char* id=nullptr;std::memcpy(&id,o+OnlineIdAt,sizeof id);uint32_t chars=0;
    if(id)std::memcpy(&chars,id-8,4);
    return fromServer(o[OwnerAt],id,chars);
}
const char* label(State s){return s==State::On?"on":s==State::Differs?"differs":"failed";}
namespace {
constexpr std::array<uint8_t,12> GateHead={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x48};
int32_t rel(uint32_t from,uint32_t to){return int32_t(int64_t(to)-int64_t(from)-5);}
std::array<uint8_t,12> stubTo(uint64_t target){std::array<uint8_t,12> s{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(s.data()+2,&target,8);return s;}
bool callsTo(const Read& read,uint64_t base,uint32_t site,uint32_t target){
    std::array<uint8_t,5> got{};if(!read(base+site,got.data(),5)||got[0]!=0xe8)return false;
    int32_t r=0;std::memcpy(&r,got.data()+1,4);return r==rel(site,target);}
bool isPadding(const Read& read,uint64_t base,uint32_t at){
    std::array<uint8_t,12> got{};if(!read(base+at,got.data(),12))return false;
    return std::all_of(got.begin(),got.end(),[](uint8_t c){return c==0xcc;});}
}
State apply(uint64_t base,uint64_t gateHook,uint64_t loadHook,uint64_t saveHook,const Read& read,const Write& write){
#ifndef MUTANT_SERVERWORLD_NOCHECK
    for(uint32_t s:GateSites)if(!callsTo(read,base,s,Gate))return State::Differs;
    if(!callsTo(read,base,LoadSite,Load)||!callsTo(read,base,SaveSite,Save))return State::Differs;
    std::array<uint8_t,12> head{};if(!read(base+Gate,head.data(),12)||head!=GateHead)return State::Differs;
    if(!isPadding(read,base,GateStub)||!isPadding(read,base,LoadStub)||!isPadding(read,base,SaveStub))return State::Differs;
#endif
    struct Done {uint64_t at;std::array<uint8_t,12> was;size_t size;};std::vector<Done> done;
    auto put=[&](uint64_t at,const uint8_t* bytes,size_t size){
        Done d{at,{},size};if(!read(at,d.was.data(),size)||!write(at,bytes,size))return false;
        done.push_back(d);return true;};
    auto undo=[&]{
#ifndef MUTANT_SERVERWORLD_ROLLBACK
        for(auto d=done.rbegin();d!=done.rend();++d)write(d->at,d->was.data(),d->size);
#endif
        return State::Failed;};
    const auto a=stubTo(gateHook),b=stubTo(loadHook),v=stubTo(saveHook);
    if(!put(base+GateStub,a.data(),a.size())||!put(base+LoadStub,b.data(),b.size())||!put(base+SaveStub,v.data(),v.size()))return undo();
    for(uint32_t s:GateSites){const int32_t r=rel(s,GateStub);if(!put(base+s+1,reinterpret_cast<const uint8_t*>(&r),4))return undo();}
    const int32_t r=rel(LoadSite,LoadStub);if(!put(base+LoadSite+1,reinterpret_cast<const uint8_t*>(&r),4))return undo();
    const int32_t w=rel(SaveSite,SaveStub);if(!put(base+SaveSite+1,reinterpret_cast<const uint8_t*>(&w),4))return undo();
    return State::On;
}
uint64_t delayAfter(const Answer& a,const std::string& key,unsigned& failures){
    if(!a.read||a.server!=key){++failures;return failures<3?RetryMs:SlowRetryMs;}
    failures=0;
    return std::min(MaxWaitMs,std::max(MinWaitMs,a.waitMs));
}
Link::Link(Get get,Filter& f,std::string key,std::function<void(const std::string&)> log):get_(std::move(get)),filter_(f),key_(std::move(key)),log_(std::move(log)){thread_=std::thread([this]{run();});}
Link::~Link(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
void Link::run(){
    unsigned failures=0;bool told=false;
    std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        g.unlock();
        Result r;try{r=get_();}catch(const std::exception&){r=Result{};}
        Answer a;if(r.status==200)a=parse(r.body);
#ifndef MUTANT_SERVERWORLD_OTHER_SERVER
        const bool good=a.read&&a.server==key_;
#else
        const bool good=a.read;
#endif
        if(good){filter_.publish(a.untagged);if(!told){told=true;log_("SERVERWORLD the server lists "+std::to_string(a.untagged.size())+" more structures as its own\n");}}
        reads_.fetch_add(1,std::memory_order_relaxed);
        const uint64_t wait=delayAfter(a,key_,failures);
        g.lock();
        wake_.wait_for(g,std::chrono::milliseconds(wait),[&]{return stop_;});
    }
}
#ifdef _WIN32
namespace {
using GateFn=bool(*)(void*,uint8_t,void*);using LoadFn=uint64_t(*)(void*,void*,uint64_t);using SaveFn=uint64_t(*)(void*,void*,void*,void*);
GateFn gateOriginal=nullptr;LoadFn loadOriginal=nullptr;SaveFn saveOriginal=nullptr;uint64_t gameBase=0;
std::atomic<Filter*> active{nullptr};std::atomic<uint64_t> asked{0},refused{0},loads{0},saves{0},leftOut{0};
bool gateHook(void* object,uint8_t needPlaced,void* boxes){
    asked.fetch_add(1,std::memory_order_relaxed);
    if(auto* f=active.load(std::memory_order_acquire);f&&object&&!f->allowsObject(static_cast<const uint8_t*>(object))){refused.fetch_add(1,std::memory_order_relaxed);return false;}
    return gateOriginal(object,needPlaced,boxes);
}
uint64_t loadHook(void* a,void* b,uint64_t c){
    auto* f=active.load(std::memory_order_acquire);if(f)f->loadStarts();
    const uint64_t r=loadOriginal(a,b,c);
    loads.fetch_add(1,std::memory_order_relaxed);
    if(f){uint32_t counter=0;if(auto m=*reinterpret_cast<uint8_t**>(gameBase+ManagerSlot))std::memcpy(&counter,m+OwnCounterAt,4);f->loadEnded(counter);}
    return r;
}
// The records' save writer (manager, stream): the server's records carry the writer's skip bit only while it runs.
uint64_t saveHook(void* manager,void* stream,void* r8,void* r9){
    std::vector<uint8_t*> marked;
    auto* f=active.load(std::memory_order_acquire);auto* m=static_cast<uint8_t*>(manager);
    if(f&&m){
        uint32_t count=0;uint8_t** list=nullptr;std::memcpy(&count,m+ObjectsCountAt,4);std::memcpy(&list,m+ObjectsAt,8);
        if(list)for(uint32_t i=0;i<count&&i<0x10000;++i){uint8_t* o=list[i];if(o&&!(o[SkipAt]&SkipBit)&&f->fromServerObject(o)){o[SkipAt]|=SkipBit;marked.push_back(o);}}
    }
    const uint64_t r=saveOriginal(manager,stream,r8,r9);
    for(auto* o:marked)o[SkipAt]&=uint8_t(~SkipBit);
    saves.fetch_add(1,std::memory_order_relaxed);leftOut.store(marked.size(),std::memory_order_relaxed);
    return r;
}
}
void install(discovery::Sampler& s,Filter& f){
    gateOriginal=reinterpret_cast<GateFn>(s.base+Gate);loadOriginal=reinterpret_cast<LoadFn>(s.base+Load);saveOriginal=reinterpret_cast<SaveFn>(s.base+Save);gameBase=s.base;active.store(&f,std::memory_order_release);
    auto* sp=&s;
    const State st=apply(s.base,reinterpret_cast<uint64_t>(&gateHook),reinterpret_cast<uint64_t>(&loadHook),reinterpret_cast<uint64_t>(&saveHook),
        [sp](uint64_t at,uint8_t* out,size_t n){return sp->read(at,out,n);},
        [](uint64_t at,const uint8_t* bytes,size_t n){
            DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),n,PAGE_EXECUTE_READWRITE,&old))return false;
            std::memcpy(reinterpret_cast<void*>(at),bytes,n);VirtualProtect(reinterpret_cast<void*>(at),n,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),n);return true;});
    if(st!=State::On){active.store(nullptr,std::memory_order_release);throw std::runtime_error(std::string("the game's code ")+label(st));}
}
Stats stats(){return {asked.load(std::memory_order_relaxed),refused.load(std::memory_order_relaxed),loads.load(std::memory_order_relaxed),saves.load(std::memory_order_relaxed),leftOut.load(std::memory_order_relaxed)};}
#else
void install(discovery::Sampler&,Filter&){throw std::runtime_error("needs the game");}
Stats stats(){return {};}
#endif
}
