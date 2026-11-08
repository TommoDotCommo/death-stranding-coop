#include "outfit.h"
namespace mod::outfit {
namespace {
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){v=0;return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
}
// The component and part pointer arrays are copied whole (one read each, not one per element; checked per element
// as before); an array that cannot be copied whole is read element by element.
static bool array(const discovery::Sampler& s,uint64_t at,uint32_t n,std::vector<uint64_t>& out){
    out.assign(n,0);if(!n)return true;
#ifndef MUTANT_OUTFIT_ELEMENTS
    if(s.readOnce(at,out.data(),8*size_t(n)))return true;
#endif
    for(uint32_t i=0;i<n;++i)if(!q(s,at+8*i,out[i]))return false;
    return true;
}
std::optional<Parts> read(const discovery::Sampler& s,uint64_t entity){
    uint32_t n=0;uint64_t list=0;
    if(!entity||!s.read(entity+ComponentCount,&n,4)||n>MaxComponents||!q(s,entity+ComponentArray,list)||!list)return std::nullopt;
    std::vector<uint64_t> components;if(!array(s,list,n,components))return std::nullopt;
    for(uint64_t c:components){
        uint64_t v=0,owner=0,arr=0;uint32_t parts=0;
        if(!q(s,c,v)||v!=s.base+ArtPartsVtable||!q(s,c+ComponentOwner,owner)||owner!=entity)continue;
        if(!s.read(c+PartCount,&parts,4)||parts<2||parts>MaxParts||!q(s,c+PartArray,arr)||!arr)return std::nullopt;
        std::vector<uint64_t> list2;if(!array(s,arr,parts,list2))return std::nullopt;
        Parts p;p.count=uint8_t(parts);
        for(uint32_t k=0;k<parts;++k){uint8_t state=0;uint64_t part=list2[k];
            if(!part||part>=0x800000000000ULL||!s.read(part+PartState,&state,1))return std::nullopt;
            if(state&1)p.enabled|=1ull<<k;}
        return p;
    }
    return std::nullopt;
}
std::vector<std::pair<unsigned,bool>> changes(const Parts& body,const Parts& want,unsigned limit){
    std::vector<std::pair<unsigned,bool>> out;
#ifndef MUTANT_OUTFIT_COUNT
    if(body.count!=want.count)return out;
#endif
    for(unsigned k=0;k<body.count&&k<MaxParts&&out.size()<limit;++k){
#ifndef MUTANT_OUTFIT_SKIP
        if(k==Skipped)continue;
#endif
        bool a=body.enabled>>k&1,b=want.enabled>>k&1;if(a!=b)out.push_back({k,b});
    }
    return out;
}
ds::CargoEvent encode(const Parts& p,uint16_t generation){
    ds::CargoEvent e;e.type=WireType;e.item=uint32_t(p.enabled);
    e.ref=(p.enabled>>32)|uint64_t(p.count&0x7f)<<32|uint64_t(generation)<<40;return e;
}
std::optional<Parts> decode(const ds::CargoEvent& e){
    if(e.type!=WireType||e.ref>>56)return std::nullopt;
    Parts p;p.count=uint8_t(e.ref>>32&0x7f);p.enabled=uint64_t(e.item)|(e.ref&0xffffffffull)<<32;
    if(p.count<2||p.count>MaxParts||(p.count<64&&p.enabled>>p.count))return std::nullopt;
    return p;
}
std::optional<ds::CargoEvent> Sender::observe(const std::optional<Parts>& read,bool connected,uint64_t now){
    if(read&&(!haveCandidate_||*read!=candidate_)){candidate_=*read;since_=now;haveCandidate_=true;}
    if(!connected){wasConnected_=false;return std::nullopt;}
    if(!wasConnected_)resend_=true;
    wasConnected_=true;
    if(!haveCandidate_||now-since_<Stable)return std::nullopt;
    bool changed=!haveSent_||candidate_!=sent_;
#ifndef MUTANT_OUTFIT_RESEND
    if(!resend_&&(!changed||(haveSent_&&now-lastSend_<MinGap)))return std::nullopt;
#else
    if(!changed||(haveSent_&&now-lastSend_<MinGap))return std::nullopt;
#endif
    resend_=false;sent_=candidate_;haveSent_=true;lastSend_=now;
    return encode(sent_,++generation_);
}
}
