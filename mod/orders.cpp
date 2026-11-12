#include "orders.h"
#include <cstring>
#include <stdexcept>
namespace mod::orders {
std::vector<Order> readOrders(const discovery::Sampler& s,uint64_t base,std::map<uint64_t,uint8_t>& details){
    std::vector<Order> out;uint64_t system=0,array=0,data=0;int32_t count=0;
    if(!s.read(base+0x7c0aff8,&system,8)||!system||!s.read(system+0x108,&array,8)||!array||!s.read(array,&count,4)||!s.read(array+8,&data,8)||!data||count<=0||count>8192)return out;
    std::vector<uint8_t> blob(size_t(count)*0xa0);
    if(!s.readOnce(data,blob.data(),blob.size()))return out;
    for(int32_t i=0;i<count;++i){
        const uint8_t* e=blob.data()+size_t(i)*0xa0;Order o;uint64_t resource=0;
        std::memcpy(&o.id,e+0x38,8);if(!o.id)continue;
        std::memcpy(&o.state,e+0x2c,2);o.result=e[0x2a];std::memcpy(&resource,e,8);
        auto known=details.find(o.id);
        if(known!=details.end())o.detail=known->second;
        else{uint8_t d=255;if(resource&&s.read(resource+0x46,&d,1))details[o.id]=d;o.detail=d;}
        out.push_back(o);
    }
    return out;
}
std::vector<Unit> readUnits(const discovery::Sampler& s,uint64_t base){
    std::vector<Unit> out;uint64_t system=0,table=0,rows=0;uint32_t capacity=0;
    if(!s.read(base+0x7c0aff8,&system,8)||!system||!s.read(system+0x18,&table,8)||!table||!s.read(table+8,&rows,8)||!rows||!s.read(table+0x14,&capacity,4)||!capacity||capacity>65536)return out;
    std::vector<uint8_t> blob(size_t(capacity)*0x38);
    if(!s.readOnce(rows,blob.data(),blob.size()))return out;
    for(uint32_t i=0;i<capacity;++i){
        const uint8_t* r=blob.data()+size_t(i)*0x38;uint32_t hash=0;uint64_t key=0;std::memcpy(&hash,r,4);if(!hash)continue;
        std::memcpy(&key,r+8,8);Unit u;u.mission=key&0xffc0ffffffffffffull;u.unit=uint8_t(key>>48&0x3f);std::memcpy(&u.state,r+0x18,2);u.stateAt=rows+uint64_t(i)*0x38+0x18;std::memcpy(&u.packed,r+0x1c,4);std::memcpy(u.life,r+0x28,8);
        out.push_back(u);
    }
    return out;
}
std::vector<Unit> UnitSync::observe(const std::vector<Unit>& units,const std::vector<uint64_t>& missions){
    std::vector<Unit> out;std::map<uint64_t,uint16_t> next;
    for(auto& u:units){
        bool shared=false;for(auto m:missions)shared=shared||m==u.mission;
#ifndef MUTANT_ORDER_UNIT_ANY
        if(!shared)continue;
#endif
        uint64_t k=key(u.mission,u.unit);next[k]=u.state;
        auto old=seen_.find(k);
        if(u.state!=Delivered){applied_.erase(k);continue;}
        if(!primed_||old==seen_.end()||old->second!=NotDelivered)continue;
#ifndef MUTANT_ORDER_UNIT_ECHO
        if(applied_.count(k))continue;
#endif
        out.push_back(u);
    }
    seen_.swap(next);primed_=true;
    return out;
}
const Unit* UnitSync::wanted(const std::vector<Unit>& units,uint64_t mission,uint8_t unit) const {
    for(auto& u:units)if(u.mission==mission&&u.unit==unit)return u.state==NotDelivered?&u:nullptr;
    return nullptr;
}
std::vector<Change> Sync::observe(const std::vector<Order>& orders){
    std::vector<Change> out;std::map<uint64_t,Order> next;
    for(auto& o:orders){
#ifndef MUTANT_ORDER_STORY
        if(!shareable(o))continue;
#endif
        next[o.id]=o;
        auto old=seen_.find(o.id);bool was=old!=seen_.end()&&old->second.state==Progress,is=o.state==Progress;
        uint8_t what=0;
        if(is&&(!primed_||!was))what=Start;
        else if(primed_&&was&&!is)what=o.state==16||(o.state!=8&&o.result==Completed)?Success:Fail;   // 16 success, 8 failed
        if(!what)continue;
        auto mine=applied_.find(o.id);
#ifndef MUTANT_ORDER_ECHO
        if(mine!=applied_.end()&&mine->second==what){applied_.erase(mine);continue;}   // done here for the partner: not news
#endif
        if(mine!=applied_.end())applied_.erase(mine);
        out.push_back({o.id,what});
    }
    seen_.swap(next);primed_=true;
    return out;
}
bool Sync::wanted(const std::vector<Order>& orders,const Change& c) const {
    for(auto& o:orders)if(o.id==c.id){
        if(!shareable(o))return false;
#ifdef MUTANT_ORDER_ANY_STATE
        return true;
#endif
        return c.what==Start?o.state==Available:(c.what==Success||c.what==Fail)&&o.state==Progress;
    }
    return false;
}
std::function<bool(uint64_t,uint8_t)> nativeRequest(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native order API requires Windows");
#else
    struct Site{uint64_t rva;uint8_t bytes[16];};
    const Site sites[]={{0x33b80a0,{0x40,0x53,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x48,0x45,0x0f,0xb6,0xf8,0x45}},
                        {0x33b82d0,{0x40,0x53,0x55,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x45,0x0f,0xb6}},
                        {0x33b75e0,{0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0xf1,0x48,0x85,0xd2}}};
    for(auto& site:sites){uint8_t got[16]{};if(!s.read(s.base+site.rva,got,16)||std::memcmp(got,site.bytes,16))throw std::runtime_error("order request function differs");}
    const uint64_t base=s.base;auto* sp=&s;
    return [base,sp](uint64_t id,uint8_t what)->bool{
        uint64_t system=0;if(!sp->read(base+0x7c0aff8,&system,8)||!system||!id)return false;
        auto* sys=reinterpret_cast<void*>(system);
        if(what==Start)return reinterpret_cast<uint8_t(*)(void*,uint64_t,uint8_t,uint8_t)>(base+0x33b80a0)(sys,id,0,0)!=0;
        if(what==Success){reinterpret_cast<void(*)(void*,uint64_t,uint8_t,uint8_t)>(base+0x33b82d0)(sys,id,0,0);return true;}
        if(what==Fail){uint8_t info[0xa0]{};std::memcpy(info+0x38,&id,8);   // the fail request reads only the id of the record
            reinterpret_cast<void(*)(void*,void*)>(base+0x33b75e0)(sys,info);return true;}
        return false;};
#endif
}
}
