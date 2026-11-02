#include "game_thread.h"
#include "discovery.h"
#include <cstring>
#include <stdexcept>
namespace mod::dispatch {
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
bool executable(const discovery::Sampler& s,uint64_t address,size_t n){
    for(auto section:s.sections)if(section.executable&&address>=section.address&&address-section.address<section.size&&n<=section.size-(address-section.address))return true;
    return false;
}
}
void validateSites(const discovery::Sampler& s,const Sites& sites) {
    require(s.signatures.size()==6,"dispatch requires validated executable sampler");
    const std::vector<int> pattern={0x48,0x8b,0x0d,-1,-1,-1,-1,0x48,0x8b,0x01,0xff,0x90,0xd8,0x01,0,0,0x84,0xc0,0x0f,0x85,-1,-1,-1,-1};
    unsigned matches=0;
    for(auto section:s.sections)if(section.executable) {
        require(section.size<=128*1024*1024,"dispatch code size cap");
        std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"dispatch code unreadable");
        size_t count=0,match=0;
        for(size_t at=0;at+pattern.size()<=code.size();++at){bool same=true;for(size_t i=0;i<pattern.size();++i)if(pattern[i]>=0&&code[at+i]!=pattern[i]){same=false;break;}if(same){if(!count)match=at;++count;}}
#ifndef MUTANT_DISPATCH_SIGNATURE
        require(count<=1,"ambiguous dispatch signature");
#endif
        if(count){++matches;require(section.address+match==s.base+sites.framePattern,"dispatch signature moved on pinned build");}
    }
    require(matches==1,"dispatch signature missing or duplicated");
    auto bytes=[&](uint64_t rva,std::initializer_list<uint8_t> expected){
        require(rva<=UINT64_MAX-s.base,"dispatch address overflow");auto address=s.base+rva;
        require(executable(s,address,expected.size()),"dispatch site not executable");
        std::vector<uint8_t> actual(expected.size());require(s.read(address,actual.data(),actual.size()),"dispatch site unreadable");
        require(std::equal(expected.begin(),expected.end(),actual.begin()),"dispatch instruction mismatch");
    };
    bytes(sites.peekCall,{0xff,0x15});bytes(sites.getCall,{0xff,0x15});
    bytes(sites.outerCall,{0xe8});bytes(sites.loopCall,{0xe8});
    auto relative=[&](uint64_t rva,unsigned length,uint64_t expected,bool data){
        int32_t d=0;require(s.read(s.base+rva+length-4,&d,4),"dispatch displacement unreadable");
        auto ip=s.base+rva+length;
        require(d>=0 || uint64_t(-int64_t(d))<=ip,"dispatch negative target");
        auto target=d<0?ip-uint64_t(-int64_t(d)):ip+uint64_t(d);
#ifndef MUTANT_DISPATCH_TARGET
        require(target==s.base+expected && (data?s.region(target,8,true):executable(s,target,1)),"dispatch target mismatch");
#endif
    };
    relative(sites.peekCall,6,sites.peekIat,true);relative(sites.getCall,6,sites.getIat,true);
    relative(sites.outerCall,5,sites.outerTarget,false);relative(sites.loopCall,5,sites.loopTarget,false);
    relative(sites.framePattern,7,sites.applicationGlobal,true);
    // The wildcard RIP operand must resolve into non-code image memory.
    int32_t displacement=0;require(s.read(s.base+sites.framePattern+3,&displacement,4),"dispatch RIP unreadable");
    auto ip=s.base+sites.framePattern+7;
    auto target=displacement<0?ip-uint64_t(-int64_t(displacement)):ip+uint64_t(displacement);
    require(s.region(target,8,true),"dispatch application global outside data");
}
}
