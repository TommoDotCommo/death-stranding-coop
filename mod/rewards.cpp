#include "rewards.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace mod::rewards {
namespace {
template<class T> T at(const std::vector<uint8_t>& b,size_t o){T v{};std::memcpy(&v,b.data()+o,sizeof v);return v;}
struct Resource { uint64_t address=0; uint32_t hash=0; int32_t floor=0,cap=0,step=0; std::array<int32_t,5> thresholds{}; };
}
std::vector<Facility> readFacilities(const discovery::Sampler& s,uint64_t base){
    std::vector<Facility> out;uint64_t system=0,list=0,records=0,array=0,table=0;int32_t count=0;uint32_t capacity=0;
    if(!s.read(base+0x7bfd948,&system,8)||!system||!s.read(system+0x10,&list,8)||!list||!s.read(system+0x18,&records,8)||!records)return out;
    if(!s.read(list+0x20,&count,4)||count<=0||count>1024||!s.read(list+0x28,&array,8)||!array)return out;
    if(!s.read(records+0x28,&table,8)||!table||!s.read(records+0x34,&capacity,4)||!capacity||capacity>65536||(capacity&(capacity-1)))return out;
    std::vector<uint64_t> pointers;pointers.resize(size_t(count));if(!s.readOnce(array,pointers.data(),pointers.size()*8))return out;
    std::vector<Resource> resources;uint64_t vtable=0;
    for(auto p:pointers){
        std::vector<uint8_t> b(0x48);if(!p||!s.readOnce(p,b.data(),b.size()))continue;
        uint64_t v=at<uint64_t>(b,0);
#ifndef MUTANT_REWARD_VTABLE
        if(v<base||v-base>=ImageSpan)continue;
        if(!vtable)vtable=v;
        if(v!=vtable)continue;                         // not the class of the others: not a facility's resource
#endif
        (void)v;
        Resource r;r.address=p;r.hash=at<uint32_t>(b,0x40);r.floor=at<int32_t>(b,0x20);
        int32_t last=0;bool ordered=true;for(int i=0;i<5;++i){int32_t t=at<int32_t>(b,0x28+4*size_t(i));ordered=ordered&&t>=last;last=t;r.thresholds[size_t(i)]=t;}
        int32_t step=at<int32_t>(b,0x3c);
        if(!r.hash||!ordered||step<0||step>MaxPoints/94||last>MaxPoints-94*step||r.floor<0||r.floor>last)continue;
        r.cap=last+94*step;r.step=step;resources.push_back(r);
    }
    std::vector<uint8_t> blob(size_t(capacity)*0x30);
    if(resources.empty()||!s.readOnce(table,blob.data(),blob.size()))return out;
    for(uint32_t i=0;i<capacity;++i){
        size_t e=size_t(i)*0x30;if(!at<uint32_t>(blob,e))continue;
        uint32_t key=at<uint32_t>(blob,e+8);uint64_t resource=at<uint64_t>(blob,e+0x10);
        Facility f;f.hash=key;f.points=at<int32_t>(blob,e+0x18);f.level=at<int32_t>(blob,e+0x1c);f.resource=resource;f.record=table+e+0x10;
        const Resource* own=nullptr;
        for(auto& r:resources)if(r.address==resource&&r.hash==key)own=&r;
#ifdef MUTANT_REWARD_FOREIGN
        static const Resource any{0,0,0,MaxPoints};if(!own)own=&any;
#endif
        if(!own)continue;                              // a record of something the system does not list
        f.floor=own->floor;f.cap=own->cap;f.thresholds=own->thresholds;f.step=own->step;
#ifndef MUTANT_REWARD_RANGE
        if(f.points<0||f.points>f.cap||f.level<0||f.level>MaxLevel)continue;
#endif
        out.push_back(f);
    }
    std::sort(out.begin(),out.end(),[](const Facility& a,const Facility& b){return a.hash<b.hash;});
    for(size_t i=1;i<out.size();++i)if(out[i].hash==out[i-1].hash)return {};   // one record per facility, or the table is not what it seems
    return out;
}
int32_t levelOf(const Facility& f,int32_t points,int32_t highest){
    int32_t level=0;
#ifdef MUTANT_REWARD_UNCAPPED
    highest=MaxLevel;
#endif
    for(int32_t n=1;n<=highest&&n<=MaxLevel;++n){
        int64_t need=f.thresholds[size_t(n<5?n-1:4)];if(n>5)need+=int64_t(n-5)*f.step;
        if(need>points)break;                          // the game stops at the first threshold not reached
        ++level;
    }
    return level;
}
void readHighest(const discovery::Sampler& s,uint64_t base,std::vector<Facility>& facilities){
    for(auto& f:facilities)f.highest=Unknown;
    // The database's contexts: 0x20-byte slots (hash, 16-byte key at +8, the context at +0x18).
    uint64_t database=0,contexts=0,context=0,values=0;uint32_t slots=0,count=0;std::array<uint64_t,2> key{};
    if(!s.read(base+0x5794720,&database,8)||!database||!s.read(base+0x7bfd980,key.data(),16))return;
    if(!s.read(database+0x20,&contexts,8)||!contexts||!s.read(database+0x2c,&slots,4)||!slots||slots>MostFacts||(slots&(slots-1)))return;
    std::vector<uint8_t> blob(size_t(slots)*0x20);if(!s.readOnce(contexts,blob.data(),blob.size()))return;
    for(uint32_t i=0;i<slots&&!context;++i){
        size_t e=size_t(i)*0x20;if(!at<uint32_t>(blob,e))continue;
#ifndef MUTANT_REWARD_CONTEXT
        if(at<uint64_t>(blob,e+8)!=key[0]||at<uint64_t>(blob,e+0x10)!=key[1])continue;
#endif
        context=at<uint64_t>(blob,e+0x18);
    }
    // Its integer values: 0x18-byte slots (hash, the fact's UUID at +4, the value at +0x14). No context, or an empty
    // table: every fact has its default, as in the game.
    bool table=false;
    if(context){
        if(!s.read(context+0x50,&values,8)||!s.read(context+0x5c,&count,4))return;
        if(values){
            if(!count||count>MostFacts||(count&(count-1)))return;
            blob.assign(size_t(count)*0x18,0);if(!s.readOnce(values,blob.data(),blob.size()))return;
            table=true;
        }
    }
    for(auto& f:facilities){
        uint64_t fact=0;if(!f.resource||!s.read(f.resource+0x58,&fact,8))continue;
        if(!fact){f.highest=MaxLevel;continue;}        // no cap fact: the game takes 99
        std::array<uint64_t,2> id{};int32_t value=0;if(!s.read(fact+8,id.data(),16)||!s.read(fact+0x30,&value,4))continue;
        for(uint32_t i=0;table&&i<count;++i){
            size_t e=size_t(i)*0x18;if(!at<uint32_t>(blob,e))continue;
#ifndef MUTANT_REWARD_FACT
            if(at<uint64_t>(blob,e+4)!=id[0]||at<uint64_t>(blob,e+0xc)!=id[1])continue;
#else
            if(at<uint64_t>(blob,e+4)!=id[0])continue;
#endif
            value=at<int32_t>(blob,e+0x14);break;
        }
#ifndef MUTANT_REWARD_HOLD
        value=std::clamp(value,0,MaxLevel);
#endif
        f.highest=value;
    }
}
uint64_t mask64(uint32_t key){
    uint64_t poly=(key&1)?0x120400002ull:0;
    return ((poly^(uint64_t(key)&~1ull))<<31)|key;
}
Porter readPorter(const discovery::Sampler& s,uint64_t base){
    Porter p;uint64_t likes=0,grade=0;
    if(!s.read(base+0x7bf83b0,&likes,8)||!likes||!s.read(base+0x7bfd960,&grade,8)||!grade)return p;
    if(!s.read(likes+0x18,&p.likes,8)||!s.read(likes+0x20,&p.likesOnline,8))return p;
    std::array<uint64_t,5> masked{};std::array<uint32_t,5> levels{};std::array<uint32_t,11> keys{};   // keys: +0x8b8 .. +0x8e0
    if(!s.read(grade+0x18,masked.data(),40)||!s.read(grade+0x538,levels.data(),20)||!s.read(grade+0x8b8,keys.data(),44))return p;
    auto key=[&](unsigned offset){return keys[(offset-0x8b8)/4];};
    const unsigned experienceKey[5]={0x8bc,0x8c0,0x8c4,0x8c8,0x8cc},levelKey[5][2]={{0x8c8,0x8dc},{0x8cc,0x8dc},{0x8d0,0x8dc},{0x8d4,0x8dc},{0x8e0,0x8b8}};
    for(int i=0;i<5;++i){
        p.experience[size_t(i)]=masked[size_t(i)]^mask64(key(experienceKey[i])^key(0x8d8));
        p.level[size_t(i)]=levels[size_t(i)]^key(levelKey[i][0])^key(levelKey[i][1]);
#ifndef MUTANT_REWARD_MASK
        // The game clamps both when it stores them: anything larger means the mask is not understood.
        if(p.experience[size_t(i)]>999999999999ull||p.level[size_t(i)]>999999999u)return p;
#endif
    }
    p.readable=true;return p;
}
ds::CargoEvent encode(const News& n){
    ds::CargoEvent e;e.type=WireType;e.ref=n.hash;e.item=uint32_t(n.level);
    e.position={double(n.points&0xffff),double(n.points>>16),0};return e;   // the wire keeps a position within 100 km: two halves
}
std::optional<News> decode(const ds::CargoEvent& e){
    if(e.type!=WireType||!e.ref||e.ref>>32)return std::nullopt;
    double low=e.position[0],high=e.position[1];
    if(!std::isfinite(low)||!std::isfinite(high)||low!=std::floor(low)||high!=std::floor(high)||low<0||low>65535||high<0||high>double(MaxPoints>>16))return std::nullopt;
    int64_t p=int64_t(high)*65536+int64_t(low);if(p>MaxPoints)return std::nullopt;
#ifndef MUTANT_REWARD_ZERO
    if(p<1)return std::nullopt;                        // a total of nothing is never news
#else
    if(p<0)return std::nullopt;
#endif
#ifndef MUTANT_REWARD_LEVEL
    if(e.item>uint32_t(MaxLevel))return std::nullopt;
#endif
    return News{uint32_t(e.ref),int32_t(p),int32_t(e.item)};
}
std::vector<News> Sync::observe(const std::vector<Facility>& facilities){
    std::vector<News> out;std::map<uint32_t,int32_t> next;size_t rose=0;
    for(auto& f:facilities){
        next[f.hash]=f.points;
        auto old=seen_.find(f.hash);
        if(!primed_||old==seen_.end()||f.points<=old->second)continue;
        ++rose;
#ifndef MUTANT_REWARD_ECHO
        auto partner=known_.find(f.hash);if(partner!=known_.end()&&partner->second>=f.points)continue;   // he has that much already
#endif
        if(f.points<1)continue;
        out.push_back({f.hash,f.points,f.level});
    }
    seen_.swap(next);primed_=true;
#ifndef MUTANT_REWARD_LOAD
    if(rose>MostAtOnce)out.clear();
#endif
    for(auto& n:out){auto& k=known_[n.hash];k=std::max(k,n.points);}
    return out;
}
void Sync::heard(const News& n){auto& k=known_[n.hash];k=std::max(k,n.points);}
int32_t Sync::owed(const Facility& f,const News& n){
    if(f.hash!=n.hash||n.points<1)return 0;
    int32_t target=n.points;
#ifndef MUTANT_REWARD_CAP
    target=std::min(target,f.cap);
#endif
#ifndef MUTANT_REWARD_LOWER
    if(target<=f.points)return 0;
#endif
    return target-f.points;
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native rewards API requires Windows");
#else
    struct Site{uint64_t rva;uint8_t bytes[16];};
    const Site sites[]={{0x2f29f00,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8d,0x6c,0x24,0xd1,0x48,0x81}},   // add points, run the level-ups
                        {0x2f28d60,{0x40,0x53,0x4c,0x8b,0x41,0x28,0x48,0x8b,0xd9,0x48,0x89,0x74,0x24,0x10,0x8b,0xf2}}};  // the record lookup it starts with
    for(auto& site:sites){uint8_t got[16]{};if(!s.read(s.base+site.rva,got,16)||std::memcmp(got,site.bytes,16))throw std::runtime_error("connection points function differs");}
    const uint64_t base=s.base;auto* sp=&s;
    Api a;
    a.add=[base,sp](uint64_t resource,int32_t amount)->bool{
        uint64_t system=0,records=0;
        if(!resource||amount<1||amount>MaxPoints||!sp->read(base+0x7bfd948,&system,8)||!system||!sp->read(system+0x18,&records,8)||!records)return false;
        return reinterpret_cast<uint8_t(*)(void*,void*,int32_t,void*)>(base+0x2f29f00)(reinterpret_cast<void*>(records),reinterpret_cast<void*>(resource),amount,nullptr)!=0;};
    return a;
#endif
}
}
