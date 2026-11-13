#include "carts.h"
#include "carry_place.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::carts {
std::vector<Cart> read(const discovery::Sampler& s,uint64_t base){
    std::vector<Cart> out;uint64_t manager=0,list=0;int32_t count=0;
    if(!s.read(base+Manager,&manager,8)||!manager||!s.read(manager+CarrierCount,&count,4)||!s.read(manager+CarrierArray,&list,8)||!list
       ||count<=0||count>int32_t(MaxCarriers))return out;
    std::vector<uint64_t> carriers(static_cast<size_t>(count));
    if(!s.read(list,carriers.data(),carriers.size()*8))return out;
    for(auto c:carriers){
        uint32_t type=0,id=0,flags=0;uint64_t inner=0,vt=0;
        if(!c||!s.read(c,&type,4)||type!=CarrierCart||!s.read(c+CarrierId,&id,4)||!cartId(id)||!s.read(c+CarrierFlags,&flags,4)
           ||!s.read(c+CarrierEntity,&inner,8)||inner<0x20)continue;
        Cart cart;cart.id=id;cart.carrier=c;cart.entity=inner-0x20;cart.attached=(flags&Attached)!=0;
#ifndef MUTANT_CARTS_VTABLE
        if(!s.read(cart.entity,&vt,8)||vt!=base+EntityVtable)continue;
#endif
        if(!s.read(cart.entity+Position,cart.position.data(),24)||!s.read(cart.entity+Rotation,cart.rotation.data(),36))continue;
        bool sane=true;for(auto v:cart.position)sane=sane&&std::isfinite(v)&&std::fabs(v)<1e6;
        for(auto v:cart.rotation)sane=sane&&std::isfinite(v)&&std::fabs(v)<=1.01f;
        if(sane)out.push_back(cart);
    }
    return out;
}
std::vector<ds::CartPose> towed(const std::vector<Cart>& all,const std::array<double,3>& porter,const std::set<uint32_t>& shown){
    struct Candidate { double d; ds::CartPose pose; };
    std::vector<Candidate> found;
    for(auto& c:all){
#ifndef MUTANT_CARTS_ANY
        if(!c.attached)continue;
#endif
#ifndef MUTANT_CARTS_ECHO
        if(shown.count(c.id))continue;
#endif
        ds::CartPose p;bool fits=true;double d2=0;
        for(int k=0;k<3;++k){double mm=std::round((c.position[size_t(k)]-porter[size_t(k)])*1000);d2+=mm*mm;
            if(std::fabs(mm)>ds::AnimationCartMaxMm)fits=false;else p.offset[size_t(k)]=int32_t(mm);}
        if(!fits)continue;
        p.rotation=carryplace::packRotation(c.rotation);p.flags=1;
        found.push_back({d2,p});
    }
    std::sort(found.begin(),found.end(),[](const Candidate& a,const Candidate& b){return a.d<b.d;});
    std::vector<ds::CartPose> out;
    for(auto& n:found)if(out.size()<ds::AnimationCarts)out.push_back(n.pose);
    return out;
}
Place place(const ds::CartPose& p,const std::array<double,3>& body){
    Place out;for(int k=0;k<3;++k)out.position[size_t(k)]=body[size_t(k)]+double(p.offset[size_t(k)])/1000.0;
    out.rotation=carryplace::unpackRotation(p.rotation&((uint64_t(1)<<48)-1));return out;
}
void Display::show(const std::vector<Place>& wanted,Api& api,uint64_t now){
    // The extra ones go first, from the back.
    while(carts_.size()>wanted.size()){api.remove(carts_.back().id);carts_.pop_back();}
    bool pending=false;
    for(size_t i=0;i<carts_.size();){
        if(api.move(carts_[i].id,wanted[i])){carts_[i].seen=true;++i;continue;}
        if(!carts_[i].seen&&now-carts_[i].asked<MakeWaitMs){pending=true;++i;continue;}
        api.remove(carts_[i].id);carts_.erase(carts_.begin()+std::ptrdiff_t(i));   // gone, or never came: made again below
    }
#ifndef MUTANT_CARTS_MANY
    if(pending)return;
#endif
    if(carts_.size()<wanted.size())if(auto id=api.make(wanted[carts_.size()]))carts_.push_back({id,now,false});
}
void Display::clear(Api& api){for(auto& c:carts_)api.remove(c.id);carts_.clear();}
std::set<uint32_t> Display::ids() const {std::set<uint32_t> s;for(auto& c:carts_)s.insert(c.id);return s;}
#ifdef _WIN32
namespace {
bool same(const discovery::Sampler& s,uint64_t at,std::initializer_list<uint8_t> want){
    std::vector<uint8_t> got(want.size());return s.read(at,got.data(),got.size())&&std::equal(got.begin(),got.end(),want.begin());}
}
Api nativeApi(discovery::Sampler& s){
    const uint64_t create=s.base+0x2669710,send=s.base+0x235c990,removeActor=s.base+0x22fbd10;
    if(!same(s,create,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48})||!same(s,removeActor,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c}))
        throw std::runtime_error("cart functions differ from the pinned image");
    uint64_t teleport=s.base+0x3dd96b8,defaultId=0;
    if(!s.read(s.base+0x4af1150,&defaultId,4))throw std::runtime_error("cart default id unreadable");
    auto* sp=&s;
    Api api;
    api.make=[create,defaultId](const Place& p)->uint32_t{
        // Request: +0 position, +0x18 rotation, +0x40 variant (0xffff: default), +0x48 cargo record (none), +0x50 id
        // (the image's default), +0x54/+0x55 bytes, +0x58 level-2 flag.
        alignas(16) uint8_t request[0x70]{};std::memcpy(request,p.position.data(),24);std::memcpy(request+0x18,p.rotation.data(),36);
        uint16_t variant=0xffff;std::memcpy(request+0x40,&variant,2);uint32_t id32=uint32_t(defaultId);std::memcpy(request+0x50,&id32,4);
        uint32_t out=0;reinterpret_cast<uint32_t*(*)(uint32_t*,const uint8_t*)>(create)(&out,request);
        return cartId(out)?out:0;};
    api.move=[sp,send,teleport](uint32_t id,const Place& p)->bool{
        for(auto& c:read(*sp,sp->base)){
            if(c.id!=id)continue;
            // MEM_COMMIT check of the entity: a cart being torn down keeps its carrier for a moment.
            MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(reinterpret_cast<void*>(c.entity),&mbi,sizeof mbi)||mbi.State!=MEM_COMMIT)return false;
            alignas(16) uint8_t msg[0x60]{};std::memcpy(msg,&teleport,8);
            std::memcpy(msg+0x10,p.position.data(),24);std::memcpy(msg+0x28,p.rotation.data(),36);
            reinterpret_cast<void(*)(uint64_t,uint8_t*,uint32_t)>(send)(c.entity,msg,0);
            return true;}
        return false;};
    api.remove=[sp,removeActor](uint32_t id){
        uint64_t actors=0;if(!sp->read(sp->base+0x7bbb8c0,&actors,8)||!actors)return;
        uint32_t key=id;reinterpret_cast<bool(*)(uint64_t,const uint32_t*)>(removeActor)(actors,&key);};
    return api;
}
#endif
}
