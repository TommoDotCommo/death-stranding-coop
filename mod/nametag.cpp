#include "nametag.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>
namespace mod::nametag {
std::u16string fromUtf8(const std::string& s){
    std::u16string out;
    for(size_t i=0;i<s.size()&&out.size()<Tag::MaxName;){
        unsigned char c=s[i];uint32_t cp=0;size_t n=0;
        if(c<0x80){cp=c;n=1;}else if((c>>5)==6){cp=c&0x1f;n=2;}else if((c>>4)==14){cp=c&0x0f;n=3;}else{++i;continue;}
        if(i+n>s.size()){break;}
        bool ok=true;for(size_t k=1;k<n;++k){unsigned char d=s[i+k];if((d>>6)!=2){ok=false;break;}cp=(cp<<6)|(d&0x3f);}
        i+=ok?n:1;
        if(!ok||cp<0x20||cp==0x7f||(cp>=0xd800&&cp<=0xdfff))continue;
#ifndef MUTANT_NAMETAG_MARKUP
        if(cp=='<'||cp=='>'||cp=='{'||cp=='}')continue;   // the UI's markup characters
#endif
        out.push_back(char16_t(cp));
    }
    return out;
}
std::vector<ds::CargoEvent> encode(const std::string& utf8){
    std::string s=utf8.substr(0,MaxBytes);
    if(utf8.size()>MaxBytes)while(!s.empty()&&(static_cast<unsigned char>(utf8[s.size()])>>6)==2)s.pop_back();   // whole characters
    std::vector<ds::CargoEvent> out;unsigned total=unsigned((s.size()+8)/9);
    if(!total){ds::CargoEvent e;e.type=WireType;out.push_back(e);return out;}
    for(unsigned k=0;k<total;++k){
        unsigned char b[9]{};for(unsigned i=0;i<9&&9*k+i<s.size();++i)b[i]=static_cast<unsigned char>(s[9*k+i]);
        ds::CargoEvent e;e.type=WireType;e.item=uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
        uint64_t rest=0;for(int i=8;i>=4;--i)rest=rest<<8|b[i];
        e.ref=uint64_t(k)|uint64_t(total)<<4|rest<<8;out.push_back(e);}
    return out;
}
std::optional<std::string> Receiver::feed(const ds::CargoEvent& e){
    if(e.type!=WireType||e.ref>>48){pending_.clear();next_=total_=0;return std::nullopt;}
    unsigned index=unsigned(e.ref&15),total=unsigned(e.ref>>4&15);
    if(!total){pending_.clear();next_=total_=0;return std::string();}
#ifndef MUTANT_NAMETAG_ORDER
    if(total>(MaxBytes+8)/9||index>=total||(index==0?false:(index!=next_||total!=total_))){pending_.clear();next_=total_=0;return std::nullopt;}
#endif
    if(index==0){pending_.clear();total_=total;}
    unsigned char b[9];for(int i=0;i<4;++i)b[i]=static_cast<unsigned char>(e.item>>(8*i));
    for(int i=0;i<5;++i)b[4+i]=static_cast<unsigned char>(e.ref>>(8+8*i));
    for(auto x:b)if(x)pending_.push_back(char(x));
    next_=index+1;
    if(next_==total_){std::string done=pending_;pending_.clear();next_=total_=0;return done;}
    return std::nullopt;
}
std::vector<ds::CargoEvent> Sender::tick(const std::string& name,bool connected){
    bool fresh=connected&&!wasConnected_;wasConnected_=connected;
    if(!connected)return {};
    if(fresh||!haveSent_||name!=sent_){sent_=name;haveSent_=true;return encode(name);}
    return {};
}
void Tag::tick(bool permitted,const std::u16string& name,const std::array<double,3>* at){
    bool want=permitted&&at&&!name.empty();
    if(record_&&(!want||name!=name_)){api_.remove(record_);record_=0;log_("TAG removed\n");}
    if(!want)return;
    for(double x:*at)if(!std::isfinite(x))return;
    if(!record_){
        if(failures_>=MaxFailures)return;
        record_=api_.create(name,*at);
        if(!record_){++failures_;log_(failures_>=MaxFailures?"TAG refused (giving up)\n":"TAG refused\n");return;}
        name_=name;log_("TAG shown\n");return;
    }
#ifndef MUTANT_NAMETAG_STILL
    api_.move(record_,*at);
#endif
}
void Tag::forget(const char* why){
    // The world was replaced: the record is handed back to the native side, which unregisters it only from a list that
    // is still current (else it is left alone), so no stale label can stay when the game keeps its list across a load.
    if(record_){api_.remove(record_);log_(std::string("TAG dropped (")+why+")\n");}
    record_=0;failures_=0;
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native name tag API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    // The hologram-name record constructor (registers with the record list) and destructor (unregisters, releases its
    // string), and the native UI string: from UTF-16, assign, release (as 0x2d8799c..0x2d87a3e use them).
    const std::array<Entry,5> entries={{
        {0x307e170,{0x40,0x53,0x48,0x83,0xec,0x20,0xc5,0xf8,0x57,0xc0,0xc5,0xf8,0x11,0x01,0x48,0x8b}},
        {0x3081db0,{0x41,0x55,0x48,0x83,0xec,0x30,0x4c,0x8b,0xe9,0xe8,0x22,0xfd,0xf8,0xff,0x84,0xc0}},
        {0x191ca20,{0x40,0x53,0x48,0x83,0xec,0x20,0x45,0x33,0xc0,0x48,0x8b,0xd9,0x4c,0x89,0x01,0x66}},
        {0x191caf0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0xfa,0x48,0x8b,0x09,0x48,0x3b,0x0a,0x74,0x22}},
        {0x191cad0,{0x48,0x8b,0x09,0xb8,0xff,0xff,0xff,0xff,0xf0,0x0f,0xc1,0x01,0x83,0xf8,0x01,0x0f}}}};
    std::array<unsigned,5> counts{};
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("tag code size cap");
        std::vector<uint8_t> code(section.size);if(!s.read(section.address,code.data(),code.size()))throw std::runtime_error("tag code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];if(section.address+size_t(at-code.begin())!=s.base+e.rva)throw std::runtime_error("tag function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)if(n!=1)throw std::runtime_error("tag function signature missing/ambiguous");
    using RecFn=void*(*)(void*);using StrFn=void(*)(void*,const char16_t*);using AssignFn=void(*)(void*,const void*);using ReleaseFn=void(*)(void*);
    auto construct=reinterpret_cast<RecFn>(s.base+entries[0].rva);auto destruct=reinterpret_cast<RecFn>(s.base+entries[1].rva);
    auto make=reinterpret_cast<StrFn>(s.base+entries[2].rva);auto assign=reinterpret_cast<AssignFn>(s.base+entries[3].rva);
    auto release=reinterpret_cast<ReleaseFn>(s.base+entries[4].rva);
    const uint64_t listSlot=s.base+0x7c00128;auto* sp=&s;
    auto place=[](unsigned char* r,const std::array<double,3>& at){float v[4]={float(at[0]),float(at[1]),float(at[2]),0.f};std::memcpy(r,v,16);};
    Api a;
    // The list each record joined: a record is only unregistered from the list that is current now; one whose list
    // was replaced (a new world) is left as it is, never freed, since the old list may still point at it.
    auto lists=std::make_shared<std::map<uint64_t,uint64_t>>();
    auto currentList=[sp,listSlot]()->uint64_t{uint64_t owner=0,list=0;if(!sp->read(listSlot,&owner,8)||!owner||!sp->read(owner+0xf0,&list,8))return 0;return list;};
    a.create=[construct,make,assign,release,currentList,lists,place](const std::u16string& name,const std::array<double,3>& at)->uint64_t{
        uint64_t list=currentList();if(!list)return 0;   // no record list
        auto* r=static_cast<unsigned char*>(::operator new(0x30,std::align_val_t(16)));std::memset(r,0,0x30);
        construct(r);(*lists)[reinterpret_cast<uint64_t>(r)]=list;
        alignas(16) unsigned char text[16]{};make(text,name.c_str());assign(r+0x10,text);release(text);
        place(r,at);return reinterpret_cast<uint64_t>(r);};
    a.move=[place](uint64_t r,const std::array<double,3>& at){place(reinterpret_cast<unsigned char*>(r),at);};
    a.remove=[destruct,currentList,lists](uint64_t r){
        auto it=lists->find(r);if(it==lists->end())return;
        bool same=it->second==currentList();lists->erase(it);
        if(!same)return;   // its list is gone or replaced: leak the 0x30 bytes rather than free what it may point at
        destruct(reinterpret_cast<void*>(r));::operator delete(reinterpret_cast<void*>(r),std::align_val_t(16));};
    return a;
#endif
}
}
