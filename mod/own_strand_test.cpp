#include "own_strand.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace mod::own_strand;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"own strand oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
// Sparse fake process memory: blocks at fixed addresses, a read must fall inside one block.
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> blocks;
    unsigned char* at(uint64_t a,size_t n){
        for(auto& b:blocks)if(a>=b.first&&a+n<=b.first+b.second.size())return b.second.data()+(a-b.first);
        return nullptr;}
    void block(uint64_t a,size_t n){blocks[a].assign(n,0);}
    template<class T> void put(uint64_t a,T v){std::memcpy(at(a,sizeof v),&v,sizeof v);}
    bool read(uint64_t a,void* p,size_t n){auto q=at(a,n);if(!q)return false;std::memcpy(p,q,n);return true;}
    uint64_t next=0x30000000;
    uint64_t string(const std::string& s,uint32_t length=0){   // an engine string: length 8 bytes before the characters
        uint64_t a=next;next+=0x100;block(a,0x10+std::max<size_t>(s.size(),length)+1);put<uint32_t>(a+8,length?length:uint32_t(s.size()));
        std::memcpy(at(a+0x10,s.size()),s.data(),s.size());return a+0x10;}
};
constexpr uint64_t Base=0x140000000,Image=0x8000000,Manager=0x20000000,OnlineManager=0x28000000;
// A construction record i places below the table end (structures.h); the object is the record - 0x18.
static void record(Memory& m,unsigned i,uint32_t id,uint8_t type,uint8_t state,uint8_t owner,uint32_t creator,uint64_t online,double x){
    uint64_t r=Manager+mod::structures::TableEnd-i*mod::structures::Stride,q0=Base+0x3000000,q2=q0+0x10;
    m.put(r,q0);m.put(r+16,q2);m.put(r+0x18,id);m.put(r+0x1c,type);m.put(r+0x1e,state);
    double p[3]={x,20,30};std::memcpy(m.at(r+0x28,24),p,24);m.put<uint32_t>(r+0x6c,1);
    m.put(r-0x18+0xb2,owner);m.put(r-0x18+0xb4,creator);m.put(r-0x18+0xb8,online);
}
static uint64_t element(Memory& m,uint64_t id,uint8_t kind,uint8_t state,uint32_t creator){
    uint64_t a=m.next;m.next+=0x100;m.block(a,0x28);m.put(a+8,id);m.put(a+0x10,kind);m.put(a+0x11,state);m.put(a+0x14,creator);return a;}
int main(){
    Memory m;
    m.block(Base+ConstructionGlobal,8);m.put(Base+ConstructionGlobal,Manager);
    m.block(Base+OnlineGlobal,8);m.put(Base+OnlineGlobal,OnlineManager);
    const uint64_t lo=Manager+mod::structures::TableEnd-uint64_t(mod::structures::MaxRecords)*mod::structures::Stride;
    m.block(lo,size_t(mod::structures::MaxRecords)*mod::structures::Stride);
    uint64_t post=m.string("pK3v9QmZ0aB1c"),cairn=m.string("eQ7fT2nP4xY8w"),others=m.string("eA1b2C3d4E5f6"),failed=m.string("lF00000000000");
    uint64_t huge=m.string("eHUGE",100),empty=m.string("");
    record(m,1,10001,25,2,Player,0,empty,1);          // a ladder this porter built here: creator 0, on no server
    record(m,2,10002,6,1,Player,Self,post,2);         // his post handed back by the server (Player variant, creator 1)
    record(m,3,100001,26,2,Net,7,others,3);           // another player's ladder
    record(m,4,40,36,2,Game,Self,0,4);                // a road rebuilder he paved: the game's, creator 1 after the upload
    record(m,5,10003,18,0,Player,0,0,5);              // his rain shelter request the game has not built
    record(m,6,100002,32,2,Net,9,0,6);                // another player's 30 m bridge
    // The online object manager: one area (226) with its three lists.
    m.block(OnlineManager+0x1e8,0x10);uint64_t table=0x2a000000;m.block(table,24*4);
    m.put(OnlineManager+0x1e8,table);m.put<uint32_t>(OnlineManager+0x1f0,1);m.put<uint32_t>(OnlineManager+0x1f4,4);
    uint64_t value=0x2b000000;m.block(value,0x38);m.put<uint32_t>(table+24,0x5eed);m.put<uint32_t>(table+24+8,226);m.put(table+24+16,value);
    auto list=[&](unsigned n,std::vector<uint64_t> items){uint64_t a=m.next;m.next+=0x100;m.block(a,8*items.size()+8);
        for(size_t i=0;i<items.size();++i){m.put(a+8*i,items[i]);}
        m.put<uint32_t>(value+8+0x10*n,uint32_t(items.size()));m.put(value+0x10+0x10*n,a);};
    list(0,{element(m,others,14,2,5)});                                          // others' cairn
    list(1,{element(m,post,0,2,Self),element(m,cairn,14,2,Self),element(m,huge,14,2,Self)});   // his own
    list(2,{element(m,failed,9,4,Self)});                                        // his ladder this game refused
    mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){return m.read(a,p,n);},Base};

    auto structures=readStructures(s,Base,Image);
    CHECK(structures.size()==6);
    if(structures.size()==6){CHECK(structures[1].owner==Player&&structures[1].creator==Self&&structures[1].online=="pK3v9QmZ0aB1c");
        CHECK(structures[0].online.empty()&&structures[3].owner==Game);}
    auto online=readOnline(s,Base);
    CHECK(online.size()==4);                          // the id 100 characters long is no string and is left out
    for(auto& o:online)CHECK(o.area==226&&o.id!="eHUGE");
    // Ownership: the owner byte for structures (built ones only), creator 1 for online objects that this game kept.
    if(structures.size()==6){CHECK(mine(structures[0])&&mine(structures[1]));
        CHECK(!mine(structures[2])&&!mine(structures[3])&&!mine(structures[4])&&!mine(structures[5]));}
    for(auto& o:online)CHECK(mine(o)==(o.id=="pK3v9QmZ0aB1c"||o.id=="eQ7fT2nP4xY8w"));

    // The own strand: the ladder, the post (with its area from the online list) and the cairn, once each.
    auto items=strand(structures,online);
    CHECK(items.size()==3);
    if(items.size()==3){
        CHECK(items[0].letter=='l'&&items[0].online.empty()&&items[0].area==0&&items[0].placed&&items[0].position[0]==1&&items[0].id==10001);
        CHECK(items[1].letter=='p'&&items[1].online=="pK3v9QmZ0aB1c"&&items[1].area==226&&items[1].placed&&items[1].type==6);
        CHECK(items[2].letter=='e'&&items[2].online=="eQ7fT2nP4xY8w"&&items[2].area==226&&!items[2].placed&&std::string(items[2].from)=="online");
        // Likes from a server by online id; what it does not know (or what is on no server) stays unknown.
        likes(items,{{"pK3v9QmZ0aB1c",3},{"",9}});
        CHECK(items[0].likes==-1&&items[1].likes==3&&items[2].likes==-1);
        CHECK(line(items[1]).rfind("OWN p pK3v9QmZ0aB1c area 226 at 2.0 20.0 30.0 level 1 likes 3 from structure",0)==0);
        CHECK(line(items[0]).find("(not on a server)")!=std::string::npos);
    }
    // Letters: both variants of a kind, the two bridges alike, the game's own variant none.
    CHECK(letter(25)=='l'&&letter(26)=='l'&&letter(31)=='b'&&letter(35)=='b'&&letter(22)=='z'&&letter(12)=='a'&&letter(18)=='n');
    CHECK(letter(24)==0&&letter(17)==0&&letter(36)==0);
    CHECK(syncLetter(0)=='p'&&syncLetter(7)=='n'&&syncLetter(14)=='e'&&syncLetter(15)=='o'&&syncLetter(19)=='x'&&syncLetter(17)==0&&syncLetter(24)==0);

    // Nothing to read is nothing found, and a map that claims thousands of areas is not walked.
    { mod::discovery::Sampler none{[](uint64_t,void*,size_t){return false;},Base};CHECK(readStructures(none,Base,Image).empty()&&readOnline(none,Base).empty()); }
    { m.put<uint32_t>(OnlineManager+0x1f4,MaxAreas+1);CHECK(readOnline(s,Base).empty()); }

    if(failures){std::fprintf(stderr,"own strand oracle: %d failures\n",failures);return 1;}
    std::puts("own strand ok");return 0;
}
