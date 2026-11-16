#include "received.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>
using namespace mod::received;
namespace own=mod::own_strand;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"received oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
// Sparse fake process memory, as in own_strand_test: blocks at fixed addresses, a read must fall inside one block.
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> blocks;
    unsigned char* at(uint64_t a,size_t n){
        for(auto& b:blocks)if(a>=b.first&&a+n<=b.first+b.second.size())return b.second.data()+(a-b.first);
        return nullptr;}
    void block(uint64_t a,size_t n){blocks[a].assign(n,0);}
    template<class T> void put(uint64_t a,T v){std::memcpy(at(a,sizeof v),&v,sizeof v);}
    bool read(uint64_t a,void* p,size_t n){auto q=at(a,n);if(!q)return false;std::memcpy(p,q,n);return true;}
    uint64_t next=0x30000000;
    uint64_t string(const std::string& s){   // an engine string: length 8 bytes before the characters
        uint64_t a=next;next+=0x100;block(a,0x10+s.size()+1);put<uint32_t>(a+8,uint32_t(s.size()));
        std::memcpy(at(a+0x10,s.size()),s.data(),s.size());return a+0x10;}
};
constexpr uint64_t Base=0x140000000,Image=0x8000000,Manager=0x20000000;
static uint64_t slot(unsigned i){return Manager+mod::structures::TableEnd-i*mod::structures::Stride;}
static uint64_t object(unsigned i){return slot(i)-0x18;}
// A construction record i places below the table end with its object's flag bytes +0xa8..+0xab, owner and online id.
static void record(Memory& m,unsigned i,uint32_t id,uint8_t type,uint8_t state,uint8_t owner,uint8_t flags,uint8_t removal,uint64_t online){
    uint64_t r=slot(i),q0=Base+0x3000000,q2=q0+0x10;
    m.put(r,q0);m.put(r+16,q2);m.put(r+0x18,id);m.put(r+0x1c,type);m.put(r+0x1e,state);m.put<uint32_t>(r+0x6c,1);
    const uint8_t f[4]={0x21,0xc0,flags,removal};std::memcpy(m.at(object(i)+0xa8,4),f,4);
    m.put(object(i)+0xb2,owner);m.put(object(i)+0xb8,online);
}
static uint8_t flags(Memory& m,unsigned i){return *m.at(object(i)+Flags,1);}

// From the first frame: the game's code that builds a request per object of a server's answer, as read from the
// pinned executable (AnswerBuilder's first bytes, and its flag stores at AnswerFlags; the request is at rbp-0x70).
const uint8_t BuilderStart[9]={0x40,0x55,0x53,0x41,0x54,0x41,0x55,0x41,0x57};
const uint8_t Stores[AnswerFlagsSize]={0xc6,0x85,0xb3,0x05,0x00,0x00,0x00,0xc6,0x85,0xb5,0x05,0x00,0x00,0x01,0xc6,0x85,
    0xbd,0x05,0x00,0x00,0x01,0x0f,0xb6,0x81,0x10,0x01,0x00,0x00,0x88,0x85,0xb9,0x05,0x00,0x00,0x88,0x85,0xcd,0x05,0x00,0x00};
// What those instructions do to a request, run the way the processor runs them (the four forms the block has; any
// other byte sequence is refused). The request starts with the constructor's defaults (0x277fde0).
static bool run(const uint8_t* code,size_t n,uint8_t answer,std::map<unsigned,uint8_t>& request){
    request={{0x623,0},{0x625,1},{0x629,0},{0x62d,0},{0x63d,1}};
    auto disp=[&](size_t k){uint32_t d;std::memcpy(&d,code+k,4);return unsigned(d+0x70);};
    unsigned eax=0x100;size_t i=0;
    while(i<n){
        if(i+7<=n&&code[i]==0xc6&&code[i+1]==0x85){request[disp(i+2)]=code[i+6];i+=7;}                    // mov byte [rbp+d],imm
        else if(i+7<=n&&code[i]==0x0f&&code[i+1]==0xb6&&code[i+2]==0x81){if(disp(i+3)!=0x110+0x70)return false;eax=answer;i+=7;}   // movzx eax,[rcx+0x110]
        else if(i+7<=n&&code[i]==0x0f&&code[i+1]==0xb6&&code[i+2]==0x85){if(!request.count(disp(i+3)))return false;eax=request[disp(i+3)];i+=7;}   // movzx eax,[rbp+d]
        else if(i+6<=n&&code[i]==0x88&&code[i+1]==0x85){if(eax>0xff)return false;request[disp(i+2)]=uint8_t(eax);i+=6;}   // mov [rbp+d],al
        else return false;
    }
    return true;
}
// The object bits the record init (0x2c32760) makes of them, and whether the check the game runs as it makes the record
// (0x2c35fb0 with its second argument 1) passes on these bits: +0xa9 bit 7 set, +0xaf bit 7 and +0xaa 0x20 clear, and
// +0xaa 0x10 set (without it the range test decides).
static bool builtAtOnce(std::map<unsigned,uint8_t>& r){
    uint8_t a9=r[0x625]?0x80:0,aa=uint8_t((r[0x63d]?0x10:0)|(r[0x62d]?0x20:0)),af=r[0x629]?0x80:0;
    return (a9&0x80)&&!(af&0x80)&&!(aa&0x20)&&(aa&0x10);
}
static void answers(){
    CHECK(early(true,true)&&!early(false,true)&&!early(true,false)&&!early(false,false));
    CHECK(std::string(label(Early::Patched))=="patched"&&std::string(label(Early::Off))=="off"&&line(Early::Off).empty());
    CHECK(line(Early::Patched)=="RECEIVED answer requests changed: served structures are made without the range and placement checks\n");
    CHECK(line(Early::Differs).find("the scan alone")!=std::string::npos&&line(Early::Failed).find("the scan alone")!=std::string::npos);
    // The game's own code: +0x62d 1, +0x63d the answer's byte (0 for a download): the record waits for both checks.
    std::map<unsigned,uint8_t> r;
    CHECK(run(Stores,sizeof Stores,0,r)&&r[0x62d]==1&&r[0x63d]==0&&r[0x629]==0&&r[0x625]==1&&!builtAtOnce(r));

    auto image=[](Memory& m){m.block(Base+AnswerBuilder,sizeof BuilderStart);std::memcpy(m.at(Base+AnswerBuilder,9),BuilderStart,9);
        m.block(Base+AnswerFlags,AnswerFlagsSize);std::memcpy(m.at(Base+AnswerFlags,AnswerFlagsSize),Stores,AnswerFlagsSize);};
    Memory m;image(m);
    mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){return m.read(a,p,n);},Base};
    std::vector<std::pair<uint64_t,uint8_t>> writes;
    auto code=[&](uint64_t a,uint8_t v){writes.push_back({a,v});auto q=m.at(a,1);if(!q)return false;*q=v;return true;};
    CHECK(patchAnswers(s,Base,code)==Early::Patched);
    // Two bytes, nothing else.
    CHECK(writes.size()==2);
    if(writes.size()==2)CHECK(writes[0]==std::make_pair(Base+AnswerFlags+0x14,uint8_t(0))&&writes[1]==std::make_pair(Base+AnswerFlags+0x24,uint8_t(0xb9)));
    {unsigned differ=0;for(size_t i=0;i<AnswerFlagsSize;++i)differ+=*m.at(Base+AnswerFlags+i,1)!=Stores[i];CHECK(differ==2);
     CHECK(!std::memcmp(m.at(Base+AnswerBuilder,9),BuilderStart,9));}
    // What the changed code makes: +0x62d 0, +0x63d 1, +0x629 still the answer's byte: built as it is made.
    const uint8_t* now=m.at(Base+AnswerFlags,AnswerFlagsSize);
    CHECK(run(now,AnswerFlagsSize,0,r)&&r[0x62d]==0&&r[0x63d]==1&&r[0x629]==0&&r[0x625]==1&&r[0x623]==0&&builtAtOnce(r));
    CHECK(run(now,AnswerFlagsSize,1,r)&&r[0x629]==1&&r[0x63d]==1&&r[0x62d]==0);   // the game's own byte, if it ever set it
    // Again: found changed, nothing written.
    writes.clear();
    CHECK(patchAnswers(s,Base,code)==Early::Already&&writes.empty());

    // Code that is not this build's: nothing written.
    {Memory d;image(d);*d.at(Base+AnswerFlags+0x0a,1)=0x00;   // another request offset for the +0x625 store
     mod::discovery::Sampler ds{[&](uint64_t a,void* p,size_t n){return d.read(a,p,n);},Base};writes.clear();
     auto dcode=[&](uint64_t a,uint8_t v){writes.push_back({a,v});*d.at(a,1)=v;return true;};
     CHECK(patchAnswers(ds,Base,dcode)==Early::Differs&&writes.empty());
     image(d);*d.at(Base+AnswerBuilder+1,1)=0x56;            // another function's start
     CHECK(patchAnswers(ds,Base,dcode)==Early::Differs&&writes.empty());}
    {mod::discovery::Sampler none{[](uint64_t,void*,size_t){return false;},Base};writes.clear();
     CHECK(patchAnswers(none,Base,code)==Early::Differs&&writes.empty());}

    // A write that fails: what was written is put back, the code is as it was.
    {Memory f;image(f);unsigned calls=0;
     mod::discovery::Sampler fs{[&](uint64_t a,void* p,size_t n){return f.read(a,p,n);},Base};
     auto second=[&](uint64_t a,uint8_t v){if(++calls==2)return false;*f.at(a,1)=v;return true;};
     CHECK(patchAnswers(fs,Base,second)==Early::Failed&&!std::memcmp(f.at(Base+AnswerFlags,AnswerFlagsSize),Stores,AnswerFlagsSize));
     // A write that says it took and did not: read back, refused.
     auto none=[](uint64_t,uint8_t){return true;};
     CHECK(patchAnswers(fs,Base,none)==Early::Failed&&!std::memcmp(f.at(Base+AnswerFlags,AnswerFlagsSize),Stores,AnswerFlagsSize));
#ifndef _WIN32
     CHECK(patchAnswers(fs,Base,nativeCodeWrite())==Early::Failed&&!std::memcmp(f.at(Base+AnswerFlags,AnswerFlagsSize),Stores,AnswerFlagsSize));
#endif
    }
}
int main(){
    // Whether the mod looks at all: the option, a server in the config and the game signed in to it, all three.
    CHECK(active(true,true,true));
    CHECK(!active(false,true,true));
    CHECK(!active(true,false,false)&&!active(true,true,false)&&!active(true,false,true));

    Memory m;
    m.block(Base+own::ConstructionGlobal,8);m.put(Base+own::ConstructionGlobal,Manager);
    const uint64_t lo=Manager+mod::structures::TableEnd-uint64_t(mod::structures::MaxRecords)*mod::structures::Stride;
    m.block(lo,size_t(mod::structures::MaxRecords)*mod::structures::Stride);
    uint64_t bridge=m.string("bQ1w2E3r4T5y6"),shelter=m.string("nA1s2D3f4G5h6"),mine=m.string("lZ9x8C7v6B5n4"),stage=m.string("pM1n2B3v4C5x6"),
             refused=m.string("nR1e2F3u4S5e6"),dropped=m.string("nD1r2O3p4P5e6"),saved=m.string("bS1a2V3e4D5x6"),both=m.string("nB1o2T3h4X5y6"),
             standing=m.string("nS1t2A3n4D5g6"),empty=m.string("");
    // Fresh from the server: a 30 m bridge held by the range test and a rain shelter the placement check would take down.
    record(m,1,100001,32,0,own::Net,0x20,0x04,bridge);
    record(m,2,100002,19,0,own::Net,0x20,0x04,shelter);
    record(m,3,10001,25,0,own::Player,0x20,0x04,mine);      // the porter's own ladder handed back as a Player variant
    record(m,4,40,6,0,own::Game,0x20,0x04,stage);           // the game's own
    record(m,5,100003,19,0,own::Net,0x60,0x05,refused);     // failed its placement check: marked for removal
    record(m,6,100004,19,0,own::Net,0x20,0x05,dropped);     // the removal bit alone
    record(m,7,100005,32,1,own::Net,0x10,0x04,saved);       // the save's received bridge (state 1, 0x10 already)
    record(m,8,100006,19,0,own::Net,0x30,0x04,both);        // pending with 0x10 already (not a fresh download)
    record(m,9,100007,19,2,own::Net,0x20,0x04,standing);    // built, whatever its flags say
    record(m,10,100008,26,0,own::Net,0x20,0x04,empty);      // a Net copy the mod made itself: no online id
    record(m,11,100009,26,0,own::Net,0x20,0x04,0);          // the same with no string at all
    // Every read counted, and a hook the game can act through between the scan and the write.
    std::set<uint64_t> objectReads,idReads;std::function<void(uint64_t,size_t)> between;
    mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){
        if(n==0x18)objectReads.insert(a);                    // the object's fields, one read
        if(n==4&&a>=0x30000000&&a<0x40000000)idReads.insert(a);   // an online id's length
        if(between)between(a,n);
        return m.read(a,p,n);},Base};

    // The scan reads only what it must: one object per record in state 0, one id per candidate.
    std::vector<std::pair<uint64_t,uint8_t>> writes;
    auto write=[&](uint64_t a,uint8_t v){writes.push_back({a,v});auto q=m.at(a,1);if(!q)return false;*q=v;return true;};
    Builder b;
    auto first=b.step(s,Base,Image,write);
    CHECK(objectReads.size()==9);
    CHECK(idReads.size()==3);                                // the bridge, the shelter and the Net copy with an empty id
    CHECK(first.size()==2&&writes.size()==2&&b.changed()==2&&b.skipped()==0);
    if(first.size()==2&&writes.size()==2){
        CHECK(first[0].id==100001&&first[0].online=="bQ1w2E3r4T5y6"&&first[0].type==32&&first[0].before==0x20&&first[0].after==0x10&&first[0].object==object(1));
        CHECK(first[1].id==100002&&first[1].online=="nA1s2D3f4G5h6");
        CHECK(writes[0]==std::make_pair(object(1)+Flags,uint8_t(0x10))&&writes[1]==std::make_pair(object(2)+Flags,uint8_t(0x10)));
        CHECK(Builder::line(first[0])=="RECEIVED building bridge bQ1w2E3r4T5y6 (range and placement checks skipped): record 100001, type 32, +0xaa 20 -> 10\n");
        CHECK(Builder::line(first[1]).rfind("RECEIVED building rain shelter nA1s2D3f4G5h6 (",0)==0);
    }
    // Only the one byte: the bridge's flags now read as the save's own received bridges, `21 c0 10 04`.
    {const uint8_t want[4]={0x21,0xc0,0x10,0x04};CHECK(!std::memcmp(m.at(object(1)+0xa8,4),want,4));}
    // Everything else as it was.
    CHECK(flags(m,3)==0x20&&flags(m,4)==0x20&&flags(m,5)==0x60&&*m.at(object(5)+Removal,1)==0x05&&flags(m,6)==0x20);
    CHECK(flags(m,7)==0x10&&flags(m,8)==0x30&&flags(m,9)==0x20&&flags(m,10)==0x20&&flags(m,11)==0x20);

    // Once per record: the same records seen again (the game set 0x20 back, or the scan saw them before the write took)
    // are not changed again.
    writes.clear();
    record(m,2,100002,19,0,own::Net,0x20,0x04,shelter);
    CHECK(b.step(s,Base,Image,write).empty()&&writes.empty());
    {auto again=own::readStructures(s,Base,Image);CHECK(b.look(again).empty());}
    // A slot the game reuses for another served object (another online id) is that object's first time.
    record(m,1,100001,32,0,own::Net,0x20,0x04,m.string("bN1e2W3o4N5e6"));
    {auto next=b.step(s,Base,Image,write);CHECK(next.size()==1&&b.changed()==3);if(next.size()==1)CHECK(next[0].online=="bN1e2W3o4N5e6");}

    // The game acts between the scan and the write (its walker refused the record meanwhile): nothing is written.
    writes.clear();
    record(m,12,100010,19,0,own::Net,0x20,0x04,m.string("nL1a2T3e4R5x6"));
    between=[&](uint64_t a,size_t n){if(a==object(12)+Flags&&n==2){m.put<uint8_t>(a,0x60);m.put<uint8_t>(a+1,0x05);}};
    CHECK(b.step(s,Base,Image,write).empty()&&writes.empty()&&b.skipped()==1&&flags(m,12)==0x60);
    between=nullptr;

    // A write that fails is counted and not retried.
    record(m,13,100011,19,0,own::Net,0x20,0x04,m.string("nF1a2I3l4E5d6"));
    CHECK(b.step(s,Base,Image,[](uint64_t,uint8_t){return false;}).empty()&&b.skipped()==2&&flags(m,13)==0x20);
    CHECK(b.step(s,Base,Image,write).empty());
#ifndef _WIN32
    {Builder off;record(m,14,100012,19,0,own::Net,0x20,0x04,m.string("nW1i2N3d4O5w6"));
     // A new builder sees the shelter set back, the record whose write failed and this one: none written off Windows.
     CHECK(off.step(s,Base,Image,nativeWrite()).empty()&&off.skipped()==3&&flags(m,2)==0x20&&flags(m,13)==0x20&&flags(m,14)==0x20);}
#endif

    // Names in the log, both variants of a kind.
    CHECK(std::string(name(32))=="bridge"&&std::string(name(35))=="bridge"&&std::string(name(19))=="rain shelter"&&std::string(name(4))=="safe house");
    CHECK(std::string(name(45))=="jump stand"&&std::string(name(36))=="structure");

    // Nothing to read is nothing changed.
    { mod::discovery::Sampler none{[](uint64_t,void*,size_t){return false;},Base};Builder n;CHECK(n.step(none,Base,Image,write).empty()); }

    answers();
    if(failures){std::fprintf(stderr,"received oracle: %d failures\n",failures);return 1;}
    std::puts("received ok");return 0;
}
