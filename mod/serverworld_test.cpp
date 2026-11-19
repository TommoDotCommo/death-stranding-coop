// A world that is the server's (serverworld.h): which records stand, that the game's code is changed whole or not at all,
// and the list read from the server.
#include "serverworld.h"
#include "../world/idtag.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <map>
using namespace mod::serverworld;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"server world oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static const std::string Key="abcd1234";
static std::string tagged(const char* body){std::string id=std::string(body)+"0000";idtag::stamp(Key,id);return id;}
static bool ask(const Filter& f,uint8_t owner,const std::string& id,uint32_t record=5){return f.allows(owner,id.empty()?nullptr:id.data(),uint32_t(id.size()),record);}
struct Memory {
    std::map<uint64_t,uint8_t> bytes;int writes=0,failAt=-1;
    void put(uint64_t at,const void* p,size_t n){auto* b=static_cast<const uint8_t*>(p);for(size_t i=0;i<n;++i)bytes[at+i]=b[i];}
    Read reader(){return [this](uint64_t at,uint8_t* out,size_t n){for(size_t i=0;i<n;++i){auto f=bytes.find(at+i);if(f==bytes.end())return false;out[i]=f->second;}return true;};}
    Write writer(){return [this](uint64_t at,const uint8_t* b,size_t n){if(writes++==failAt)return false;put(at,b,n);return true;};}
};
constexpr uint64_t Base=0x140000000;
static void call(Memory& m,uint32_t site,uint32_t target){uint8_t c[5]={0xe8};int32_t r=int32_t(int64_t(target)-int64_t(site)-5);std::memcpy(c+1,&r,4);m.put(Base+site,c,5);}
static Memory game(){
    Memory m;for(uint32_t s:GateSites)call(m,s,Gate);call(m,LoadSite,Load);
    const uint8_t head[12]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x48};m.put(Base+Gate,head,12);
    uint8_t pad[12];std::memset(pad,0xcc,12);m.put(Base+GateStub,pad,12);m.put(Base+LoadStub,pad,12);return m;}
static int64_t target(const Memory& m,uint32_t site){int32_t r=0;uint8_t b[4];for(int i=0;i<4;++i)b[i]=m.bytes.at(Base+site+1+i);std::memcpy(&r,b,4);return int64_t(site)+5+r;}
int main(){
    {   // The bytes this build holds, as read from its code: every call is to the test the record has to pass.
        const uint8_t expected[6][4]={{0x18,0x06,0xfd,0xff},{0x70,0xfa,0xfa,0xff},{0xbd,0xdf,0xfa,0xff},{0x69,0xde,0xfa,0xff},{0xfa,0xb6,0xfa,0xff},{0xda,0x3b,0xfa,0xff}};
        for(size_t i=0;i<GateSites.size();++i){int32_t r=0;std::memcpy(&r,expected[i],4);CHECK(int64_t(GateSites[i])+5+r==int64_t(Gate));}}
    {   // Who stands. The game's own always; an id of this server, or one it lists; nothing else with an id.
        Filter f(Key);const auto mine=tagged("lABCDEFGH");std::string foreign="lPUBLISHER123";
        CHECK(ask(f,OwnerGame,"")&&ask(f,OwnerGame,foreign));
        CHECK(ask(f,OwnerNet,mine)&&ask(f,OwnerPlayer,mine));
        CHECK(!ask(f,OwnerNet,foreign)&&!ask(f,OwnerPlayer,foreign));
        CHECK(!ask(f,OwnerNet,"short"));
        f.publish({foreign,"tooshort"});CHECK(f.listed()==1&&ask(f,OwnerNet,foreign)&&ask(f,OwnerPlayer,foreign));
        f.publish({});CHECK(f.listed()==0&&!ask(f,OwnerNet,foreign));
        std::vector<std::string> many;for(int i=0;i<4000;++i){char b[16];std::snprintf(b,16,"l%012d",i*3);many.push_back(b);}
        f.publish(many);CHECK(f.listed()==4000&&ask(f,OwnerNet,many[0])&&ask(f,OwnerNet,many[3999])&&ask(f,OwnerNet,many[1234]));
        char miss[16];std::snprintf(miss,16,"l%012d",1234*3+1);CHECK(!ask(f,OwnerNet,miss));
        Filter other("different");CHECK(!ask(other,OwnerNet,mine));}
    {   // Without an id: the mod's copies of another player's structures stand; the player's own only if built since the load.
        Filter f(Key);CHECK(ask(f,OwnerNet,"",20));
        CHECK(ask(f,OwnerPlayer,"",OwnIdsFrom+1));          // no save was loaded: all of it is new
        f.loadStarts();CHECK(!ask(f,OwnerPlayer,"",OwnIdsFrom+1)&&!ask(f,OwnerPlayer,"",OwnIdsFrom+99999));
        f.loadEnded(40);CHECK(!ask(f,OwnerPlayer,"",OwnIdsFrom+40)&&ask(f,OwnerPlayer,"",OwnIdsFrom+41)&&!ask(f,OwnerPlayer,"",7)&&!ask(f,3,"",OwnIdsFrom+41));}
    {   // Read from a record as the game lays it out: the owner byte, the record number, the id with its length in front.
        Filter f(Key);uint8_t object[0x100]{};struct {uint64_t pad;char text[14];} s{};uint32_t n=13;const auto id=tagged("sABCDEFGH");std::memcpy(s.text,id.data(),13);std::memcpy(&s.pad,&n,4);
        const char* p=s.text;std::memcpy(object+OnlineIdAt,&p,8);object[OwnerAt]=OwnerNet;CHECK(f.allowsObject(object));
        s.text[1]^=1;CHECK(!f.allowsObject(object));
        p=nullptr;std::memcpy(object+OnlineIdAt,&p,8);CHECK(f.allowsObject(object));object[OwnerAt]=OwnerPlayer;uint32_t rec=OwnIdsFrom+2;std::memcpy(object+RecordIdAt,&rec,4);f.loadEnded(1);CHECK(f.allowsObject(object));f.loadEnded(2);CHECK(!f.allowsObject(object));}
    {   // The code: all the calls lead to the stubs, the stubs to the hooks; each mismatch changes nothing.
        auto m=game();CHECK(apply(Base,0x1111,0x2222,m.reader(),m.writer())==State::On);
        for(uint32_t s:GateSites){CHECK(target(m,s)==int64_t(GateStub));}
        CHECK(target(m,LoadSite)==int64_t(LoadStub));
        uint64_t hook=0;uint8_t b[8];
        for(int i=0;i<8;++i){b[i]=m.bytes.at(Base+GateStub+2+i);}
        std::memcpy(&hook,b,8);CHECK(hook==0x1111&&m.bytes.at(Base+GateStub)==0x48&&m.bytes.at(Base+GateStub+11)==0xe0);
        for(int i=0;i<8;++i){b[i]=m.bytes.at(Base+LoadStub+2+i);}
        std::memcpy(&hook,b,8);CHECK(hook==0x2222);
        CHECK(apply(Base,1,2,m.reader(),m.writer())==State::Differs);   // once is enough: it is not done over itself
        auto bad=[&](uint64_t at){auto g=game();const auto clean=g.bytes;g.bytes[at]^=0x40;auto was=g.bytes;CHECK(apply(Base,1,2,g.reader(),g.writer())==State::Differs&&g.bytes==was&&g.writes==0);(void)clean;};
        for(uint32_t s:GateSites){bad(Base+s+2);}
        bad(Base+LoadSite+2);bad(Base+Gate+3);bad(Base+GateStub+5);bad(Base+LoadStub+5);
        for(int fail=0;fail<9;++fail){auto g=game();const auto was=g.bytes;g.failAt=fail;CHECK(apply(Base,1,2,g.reader(),g.writer())==State::Failed);g.failAt=-1;CHECK(g.bytes==was);}}
    {   // The server's answer, and how soon to ask again.
        auto a=parse(R"({"format":1,"server":"abcd1234","world":"server","wait_ms":60000,"untagged":["lPUBLISHER123","x",7]})");
        CHECK(a.read&&a.server==Key&&a.world=="server"&&a.waitMs==60000&&a.untagged==std::vector<std::string>{"lPUBLISHER123"});
        for(auto s:{"","nonsense","[]","{\"format\":2}"})CHECK(!parse(s).read);
        unsigned n=0;CHECK(delayAfter(a,Key,n)==60000&&n==0);Answer b=a;b.waitMs=1;CHECK(delayAfter(b,Key,n)==MinWaitMs);b.waitMs=UINT64_MAX;CHECK(delayAfter(b,Key,n)==MaxWaitMs);
        CHECK(delayAfter(a,"another",n)==RetryMs&&delayAfter(Answer{},Key,n)==RetryMs&&delayAfter(Answer{},Key,n)==SlowRetryMs);CHECK(delayAfter(a,Key,n)==60000&&n==0);}
    {   // The thread: another server's list is not taken; this server's is, and it stops when asked.
        Filter f(Key);std::atomic<int> gets{0};std::atomic<bool> right{false};
        {Link l([&]{++gets;std::string who=right?Key:"another";return Result{200,"{\"format\":1,\"server\":\""+who+"\",\"world\":\"server\",\"wait_ms\":1,\"untagged\":[\"lPUBLISHER123\"]}"};},f,Key,[](const std::string&){});
         for(int i=0;i<200&&!gets;++i){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
         CHECK(gets>=1&&f.listed()==0);}
        right=true;{Link l([&]{return Result{200,"{\"format\":1,\"server\":\""+Key+"\",\"world\":\"server\",\"wait_ms\":1,\"untagged\":[\"lPUBLISHER123\"]}"};},f,Key,[](const std::string&){});
         for(int i=0;i<200&&!f.listed();++i){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
         CHECK(f.listed()==1);}}
    if(!failures)std::puts("server world checks passed");
    return failures?1:0;
}
