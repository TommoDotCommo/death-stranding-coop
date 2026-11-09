#include "likes.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace mod::likes {
ds::CargoEvent encode(uint32_t count,uint32_t serial){
    ds::CargoEvent e;e.type=WireType;e.item=count;e.ref=serial;return e;
}
std::optional<uint32_t> decode(const ds::CargoEvent& e){
    if(e.type!=WireType||e.ref>>32||e.item==0||e.item>MaxPerEvent)return std::nullopt;
    return e.item;
}
std::optional<ds::CargoEvent> Sender::press(uint64_t now,bool connected){
    if(!connected)return std::nullopt;
#ifndef MUTANT_LIKES_GAP
    if(sent_&&now>=last_&&now-last_<MinGapMs)return std::nullopt;
#endif
    sent_=true;last_=now;return encode(1,++serial_);
}
bool Receiver::accept(uint64_t now){
    while(!given_.empty()&&(now<given_.front()||now-given_.front()>=60000))given_.pop_front();
    if(!given_.empty()&&now>=given_.back()&&now-given_.back()<MinGapMs)return false;
#ifndef MUTANT_LIKES_CAP
    if(given_.size()>=PerMinute)return false;
#endif
    given_.push_back(now);return true;
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native likes API requires Windows");
#else
    // DSLikeCommand_sReceiveLike (script entry 0x2e4d770 jumps here): (amount, person, reason). With no person it gives
    // the likes to the local porter through the likes state at 0x7bf83b0 (0x2e138e0), flags 7: the counter popup.
    const uint64_t rva=0x2e4eeb0;
    const std::vector<uint8_t> bytes{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x50,0x41,0x0f,0xb7,0xf8,0x8b,0xd9,0x48,0x85,0xd2,0x74,0x52,0x48,0x8b,0x0d};
    unsigned count=0;
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("likes code size cap");
        std::vector<uint8_t> code(section.size);if(!s.read(section.address,code.data(),code.size()))throw std::runtime_error("likes code unreadable");
        auto at=code.begin();
        while((at=std::search(at,code.end(),bytes.begin(),bytes.end()))!=code.end()){
            ++count;if(section.address+size_t(at-code.begin())!=s.base+rva)throw std::runtime_error("likes function signature moved/duplicated");++at;}
    }
    if(count!=1)throw std::runtime_error("likes function signature missing/ambiguous");
    using ReceiveFn=void(*)(int32_t,void*,uint16_t);
    auto receive=reinterpret_cast<ReceiveFn>(s.base+rva);const uint64_t state=s.base+0x7bf83b0;auto* sp=&s;
    Api a;
    a.give=[receive,state,sp](uint32_t n){
        uint64_t likes=0;if(!sp->read(state,&likes,8)||!likes||!n||n>MaxPerEvent)return false;
        receive(int32_t(n),nullptr,17);   // reason 17: Other (no person, no connection level, no online record)
        return true;};
    return a;
#endif
}
}
