#include "body.h"
#include "discovery.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace mod::body {
std::vector<uint64_t> entitiesIn(const std::vector<uint64_t>& words,uint64_t vtable,uint64_t exclude,
                                 const std::function<uint64_t(uint64_t)>& vtableOf){
    std::vector<uint64_t> out;
    for(auto w:words){
        if(!w||w==exclude||(w&7)||w>=0x800000000000ULL)continue;
#ifndef MUTANT_BODY_ANY_CLASS
        if(vtableOf(w)!=vtable)continue;
#endif
        if(std::find(out.begin(),out.end(),w)==out.end())out.push_back(w);
    }
    return out;
}
bool Probe::create(uint64_t twin,const Transform& at,const std::array<uint64_t,2>& id){
    if(watching()){log_("BODY refused: a request is still being watched\n");return false;}
    uint64_t resource=0;
    if(!twin||!api_.read(twin+ResourceOffset,&resource,8)||!resource){log_("BODY refused: no double or no resource\n");return false;}
    request_=api_.create(at,resource,id);twin_=twin;entity_=0;ticks_=0;
    std::ostringstream o;o<<"BODY requested resource=0x"<<std::hex<<resource<<" request=0x"<<request_<<std::dec<<std::setprecision(10)
        <<" at="<<at.position[0]<<","<<at.position[1]<<","<<at.position[2]<<"\n";log_(o.str());
    if(!request_){log_("BODY no request returned\n");return false;}
    return true;
}
void Probe::tick(){
    if(!watching())return;
    ++ticks_;
    std::vector<uint64_t> words(RequestSize/8);
    if(!api_.read(request_,words.data(),RequestSize)){log_("BODY request unreadable\n");request_=0;return;}
    if(ticks_==1||ticks_==3||ticks_==10||ticks_==40||ticks_==WatchTicks){
        std::ostringstream o;o<<"BODY request tick "<<ticks_<<":"<<std::hex;
        for(size_t i=0;i<words.size();++i)if(words[i])o<<" +"<<i*8<<"="<<words[i];
        o<<"\n";log_(o.str());
    }
    if(entity_)return;
    auto found=entitiesIn(words,vtable_,twin_,[this](uint64_t p){uint64_t v=0;return api_.read(p,&v,8)?v:0;});
    if(!found.empty()){
        entity_=found.front();
        std::ostringstream o;o<<"BODY spawned entity=0x"<<std::hex<<entity_<<std::dec<<" after "<<ticks_<<" ticks ("<<found.size()<<" candidates)\n";log_(o.str());
    }
    if(ticks_==WatchTicks&&!entity_)log_("BODY no entity of the double's class appeared in the request\n");
}
bool Probe::remove(){
    if(!entity_){log_("BODY remove: nothing spawned\n");return false;}
    api_.remove(entity_);log_("BODY removed\n");entity_=0;return true;
}
namespace {
[[maybe_unused]] void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native body API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    const std::array<Entry,2> entries={{
        {0x2380920,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x48,0x48,0x8b,0x05,0xfc,0x06,0xbe,0x02}},
        {0x2361c20,{0xe9,0x7b,0xae,0xfe,0xff,0xcc,0xcc,0xcc}}}};
    std::array<unsigned,2> counts{};
    for(auto section:s.sections)if(section.executable){
        require(section.size<=128*1024*1024,"body code size cap");std::vector<uint8_t> code(section.size);
        require(s.read(section.address,code.data(),code.size()),"body code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];require(section.address+size_t(at-code.begin())==s.base+e.rva,"body function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)require(n==1,"body function signature missing/ambiguous");
    using CreateFn=uint64_t*(*)(uint64_t*,const Transform*,uint64_t,uint64_t,const uint64_t*);
    using RemoveFn=void(*)(uint64_t);
    auto create=reinterpret_cast<CreateFn>(s.base+entries[0].rva);auto remove=reinterpret_cast<RemoveFn>(s.base+entries[1].rva);
    Api api;
    api.create=[create](const Transform& t,uint64_t resource,const std::array<uint64_t,2>& id){
        alignas(16) uint64_t uuid[2]={id[0],id[1]};uint64_t out=0;create(&out,&t,resource,0,uuid);return out;};
    api.remove=[remove](uint64_t e){remove(e);};
    api.read=[&s](uint64_t a,void* b,size_t n){return s.read(a,b,n);};
    return api;
#endif
}
}
