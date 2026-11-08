// The partner's outfit (outfit.h): the enabled mask read from fake memory, the changes to apply (never part 20, nothing
// across part counts), the one-event wire, and the sender.
#include "outfit.h"
#include <cstdio>
#include <cstring>
using namespace mod::outfit;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"outfit oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr uint64_t Image=0x140000000;
struct Mem {
    uint64_t base=0x10000000;std::vector<unsigned char> mem=std::vector<unsigned char>(0x40000);uint64_t next=0x1000;
    template<class T> void put(uint64_t a,T v){std::memcpy(mem.data()+(a-base),&v,sizeof v);}
    uint64_t alloc(size_t n){uint64_t a=base+next;next+=(n+15)&~size_t(15);return a;}
    mod::discovery::Sampler sampler{[this](uint64_t a,void* p,size_t n){if(a<base||a-base>mem.size()||n>mem.size()-(a-base))return false;std::memcpy(p,mem.data()+(a-base),n);return true;},Image};
};
}
int main(){
    // An entity with an unrelated component, a foreign ArtPartsData (another owner) and its own, 52 parts.
    Mem m;uint64_t e=m.alloc(0x200),list=m.alloc(24),other=m.alloc(0x60),foreign=m.alloc(0x120),own=m.alloc(0x120),arr=m.alloc(8*52);
    m.put<uint32_t>(e+ComponentCount,3);m.put(e+ComponentArray,list);m.put(list,other);m.put(list+8,foreign);m.put(list+16,own);
    m.put<uint64_t>(other,Image+0x1234);m.put<uint64_t>(foreign,Image+ArtPartsVtable);m.put<uint64_t>(foreign+ComponentOwner,e+0x400);
    m.put<uint64_t>(own,Image+ArtPartsVtable);m.put<uint64_t>(own+ComponentOwner,e);m.put<uint32_t>(own+PartCount,52);m.put(own+PartArray,arr);
    uint64_t expect=0;
    for(unsigned k=0;k<52;++k){uint64_t part=m.alloc(0x40);m.put(arr+8*k,part);bool on=k%3!=1;m.put<uint8_t>(part+PartState,on?0x0f:0x0c);if(on)expect|=1ull<<k;}
    auto p=read(m.sampler,e);CHECK(p&&p->count==52&&p->enabled==expect);
    m.put<uint32_t>(own+PartCount,65);CHECK(!read(m.sampler,e));m.put<uint32_t>(own+PartCount,52);
    m.put<uint32_t>(e+ComponentCount,2);CHECK(!read(m.sampler,e));m.put<uint32_t>(e+ComponentCount,3);   // only the foreign one
    // Changes: the differing parts, at most `limit`, never part 20; nothing across part counts.
    Parts body{52,expect},want{52,expect^(1ull<<2|1ull<<5|1ull<<20|1ull<<40)};
    auto c=changes(body,want,8);
    CHECK(c.size()==3&&c[0].first==2&&c[1].first==5&&c[2].first==40);
    CHECK(c[0].second==bool(want.enabled>>2&1));
    CHECK(changes(body,want,2).size()==2);
    CHECK(changes(body,body,8).empty());
    CHECK(changes(Parts{51,expect&((1ull<<51)-1)},want,8).empty());
    // Wire: one event; a round trip; malformed events refused.
    auto ev=encode(want,7);CHECK(ev.type==WireType);auto back=decode(ev);CHECK(back&&*back==want);
    auto bad=ev;bad.ref|=1ull<<60;CHECK(!decode(bad));
    bad=ev;bad.ref=(bad.ref&~(0x7full<<32))|uint64_t(70)<<32;CHECK(!decode(bad));
    Parts small{10,0x3ff};auto sv=encode(small,1);CHECK(decode(sv)&&*decode(sv)==small);
    bad=sv;bad.item|=1u<<12;CHECK(!decode(bad));   // a bit beyond the part count
    // Sender: after Stable ms, once; again after a reconnection; a change after MinGap.
    Sender s;CHECK(!s.observe(body,true,1000));CHECK(s.observe(body,true,1000+Sender::Stable));
    CHECK(!s.observe(body,true,9000));CHECK(!s.observe(body,false,9500));CHECK(s.observe(body,true,9600));
    CHECK(!s.observe(want,true,10000));CHECK(s.observe(want,true,10000+Sender::Stable));
    if(failures)return 1;
    std::puts("outfit tests passed");
    return 0;
}
