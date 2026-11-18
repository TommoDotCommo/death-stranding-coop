#include "dialog.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace mod::dialog {
uint8_t setting(size_t rows){return rows==1?0:rows==2?3:rows==3?4:0xff;}
unsigned rowOf(size_t rows,uint8_t type){
    if(rows==1)return type==1?0:Dismissed;
    if(rows==2||rows==3){
        if(type==2)return 0;
        if(type==3)return 1;
#ifndef MUTANT_DIALOG_CANCEL
        if(type==4&&rows==3)return 2;
#else
        if(type==4)return rows-1;
#endif
    }
    return Dismissed;
}
#ifdef _WIN32
namespace {
// The game keeps a pointer to this and calls its first virtual function with the button type, on the game thread. It
// lives as long as the process, so a dialogue that outlasts anything of the mod's still calls valid code.
struct Answered;
using AnswerFn=void(*)(Answered*,uint8_t);
struct Answered {const AnswerFn* table;};
std::atomic<unsigned> answer{0};
void answeredFn(Answered*,uint8_t type){answer.store(type+1u,std::memory_order_release);}
const AnswerFn answerTable[1]={answeredFn};
Answered answered{answerTable};
}
#endif
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native dialogue API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    const std::array<Entry,6> entries={{
        {0x30f3540,{0x40,0x53,0x57,0x41,0x57,0x48,0x83,0xec,0x20,0x80,0x7a,0x3b,0x00,0x4c,0x8b,0xfa,0x48,0x8b,0xf9,0x0f,0x84,0x8f,0x01,0x00,0x00,0x80,0x79,0x20,0x00,0x75,0x2f,0x80}},   // show a record
        {0x30e7c10,{0x40,0x53,0x48,0x83,0xec,0x20,0x41,0x0f,0xb6,0xc0,0x48,0x8b,0xda,0x44,0x88,0x42,0x08,0xc7,0x42,0x38,0x00,0x01,0x00,0x00,0xc6,0x42,0x30,0x01,0x83,0xf8,0x06,0x0f}},   // fill a record for a setting
        {0x191ca20,{0x40,0x53,0x48,0x83,0xec,0x20,0x45,0x33,0xc0,0x48,0x8b,0xd9,0x4c,0x89,0x01,0x66,0x44,0x39,0x02,0x74,0x18,0x48,0x8b,0xc2}},   // a string from UTF-16
        {0x191cad0,{0x48,0x8b,0x09,0xb8,0xff,0xff,0xff,0xff,0xf0,0x0f,0xc1,0x01,0x83,0xf8,0x01,0x0f,0x84,0x8b,0x8d,0x0b,0x00,0xc3,0xcc,0xcc}},   // release
        {0x191caf0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0xfa,0x48,0x8b,0x09,0x48,0x3b,0x0a,0x74,0x22}},   // assign
        {0x191caa0,{0xb8,0x01,0x00,0x00,0x00,0xf0,0x0f,0xc1,0x05,0x63,0x74,0xc4,0x02,0x48,0x8d,0x15,0x5c,0x74,0xc4,0x02,0x48,0x8b,0xc1,0x48}}}};  // the empty string
    std::array<unsigned,entries.size()> counts{};
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("dialogue code size cap");
        std::vector<uint8_t> code(section.size);if(!s.readOnce(section.address,code.data(),code.size()))throw std::runtime_error("dialogue code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];if(section.address+size_t(at-code.begin())!=s.base+e.rva)throw std::runtime_error("dialogue function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)if(n!=1)throw std::runtime_error("dialogue function signature missing/ambiguous");
    using Text=uint64_t;
    auto show=reinterpret_cast<void(*)(uint64_t,void*)>(s.base+entries[0].rva);auto fill=reinterpret_cast<void(*)(uint64_t,void*,uint8_t)>(s.base+entries[1].rva);
    auto make=reinterpret_cast<void(*)(Text*,const char16_t*)>(s.base+entries[2].rva);auto drop=reinterpret_cast<void(*)(Text*)>(s.base+entries[3].rva);
    auto assign=reinterpret_cast<void(*)(Text*,const Text*)>(s.base+entries[4].rva);auto empty=reinterpret_cast<void(*)(Text*)>(s.base+entries[5].rva);
    auto* sp=&s;const uint64_t ownerSlot=s.base+0x7c00128,menuSlot=s.base+0x57965f8;
    auto box=[sp,ownerSlot]{uint64_t owner=0,dlg=0;return sp->read(ownerSlot,&owner,8)&&owner&&sp->read(owner+0x2c0,&dlg,8)?dlg:uint64_t(0);};
    auto rows=std::make_shared<size_t>(0);
    Api a;
    a.free=[sp,menuSlot,box]{
        uint64_t dlg=box(),menu=0,stack=0,page=0;uint8_t up=1;int waiting=1;
        if(!dlg||!sp->read(dlg+0x20,&up,1)||up||!sp->read(dlg+0x78,&waiting,4)||waiting)return false;
        if(!sp->read(menuSlot,&menu,8)||!menu||!sp->read(menu+0xb0,&stack,8))return false;
        return !stack||(sp->read(stack+0x10,&page,8)&&!page);};
    a.showing=[sp,box]{uint64_t dlg=box();uint8_t up=0;return dlg&&sp->read(dlg+0x20,&up,1)&&up;};
    a.open=[=](const Request& r){
        const uint8_t kind=setting(r.rows.size());const uint64_t dlg=box();if(kind==0xff||!dlg)return false;
        // The record as the game's own simple call (0x30f3760) builds it, with this body and these labels.
        alignas(16) uint8_t record[0x48]{};auto field=[&](size_t at){return reinterpret_cast<Text*>(record+at);};
        for(size_t at:{0x10,0x18,0x20,0x28})empty(field(at));
        fill(dlg,record,kind);
        auto put=[&](size_t at,const std::u16string& text){Text t=0;make(&t,text.c_str());assign(field(at),&t);drop(&t);};
        for(size_t i=0;i<r.rows.size();++i)put(0x10+8*i,r.rows[i]);
        put(0x28,r.body);
        int32_t start=int32_t(std::min<size_t>(r.start,r.rows.size()-1));std::memcpy(record+4,&start,4);
        record[0x3a]=1;                                 // the back button leaves it
        Answered* callback=&answered;std::memcpy(record+0x40,&callback,8);
        answer.store(0,std::memory_order_release);*rows=r.rows.size();
        show(dlg,record);
        for(size_t at:{0x28,0x20,0x18,0x10})drop(field(at));
        return true;};
    a.take=[rows]{const unsigned v=answer.exchange(0,std::memory_order_acq_rel);return v?rowOf(*rows,uint8_t(v-1)):NoAnswer;};
    return a;
#endif
}
}
