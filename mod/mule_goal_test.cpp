// MULE goal probes (mule_goal.h) on fake memory: the chain read link by link, each broken link reported and not
// followed, and every check the forced-destination write makes before it touches anything.
#include "mule_goal.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace mod::mulegoal;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"mule goal oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
constexpr uint64_t Image=0x140000000ULL,ImageSize=0x8000000,Heap=0x20000000ULL;
// Offsets of the fake objects in the heap block.
constexpr uint64_t Actor=0x1000,Entity=0x2000,Array=0x3000,Other=0x3800,Component=0x4000,Ai=0x10000,Entries=0x20000,Goal=0x70000,D=0x80000,Size=0x100000;
struct World {
    std::vector<uint8_t> heap=std::vector<uint8_t>(Size);
    uint64_t blocked=0;unsigned pokes=0;
    template<class T> void put(uint64_t at,T v){std::memcpy(heap.data()+at,&v,sizeof v);}
    template<class T> T get(uint64_t at) const {T v{};std::memcpy(&v,heap.data()+at,sizeof v);return v;}
    void point(uint64_t at,double x,double y,double z){put(at,x);put(at+8,y);put(at+16,z);}
    mod::discovery::Sampler sampler(){
        mod::discovery::Sampler s([this](uint64_t at,void* out,size_t n){
            if(at<Heap||at-Heap>=Size||n>Size-(at-Heap))return false;
            if(blocked&&at<=blocked&&blocked<at+n)return false;
            std::memcpy(out,heap.data()+(at-Heap),n);return true;},Image);
        s.sections.push_back({Image+0x1000,0x3b00000,true});s.sections.push_back({Image+0x3b01000,ImageSize-0x3b01000,false});
        return s;
    }
    Writable writable(){return [](uint64_t at,size_t n){return at>=Heap+D&&n<=Size-D&&at-(Heap+D)<=Size-D-n;};}
    Poke poke(){return [this](uint64_t at,const void* from,size_t n){++pokes;std::memcpy(heap.data()+(at-Heap),from,n);};}
    uint64_t request(int32_t i=1) const {return D+RequestFirst+uint64_t(i)*RequestSize;}
    World(){
        put<uint64_t>(Actor+ActorEntity,Heap+Entity+EntityBias);
        put<uint32_t>(Entity+ComponentCount,3);put<uint64_t>(Entity+ComponentArray,Heap+Array);
        put<uint64_t>(Array,Heap+Other);put<uint64_t>(Array+8,0);put<uint64_t>(Array+16,Heap+Component);
        put<uint64_t>(Other,Image+0x3dd9750);put<uint64_t>(Other+ComponentAi,Heap+0x9000);put<uint64_t>(Other+ComponentAiAgain,Heap+0x9000);   // another component
        put<uint64_t>(Component,Image+DsComponentVtable);put<uint64_t>(Component+ComponentAi,Heap+Ai);put<uint64_t>(Component+ComponentAiAgain,Heap+Ai);
        put<uint64_t>(Ai,Image+AiVtable);put<uint8_t>(Ai+AiState,3);
        put<uint64_t>(Ai+AiGoals,Heap+Entries);put<uint32_t>(Ai+AiGoals+0xc,16);
        // Entry 2: another goal; entry 5: the walk goal; the rest empty.
        put<uint32_t>(Entries+2*0x20,0x80000001u);put<uint64_t>(Entries+2*0x20+8,Image+0x4c91870);put<uint64_t>(Entries+2*0x20+0x10,Heap+0x60000);
        put<uint32_t>(Entries+5*0x20,0x80000005u);put<uint64_t>(Entries+5*0x20+8,Image+WalkGoalKey);put<uint64_t>(Entries+5*0x20+0x10,Heap+Goal);
        point(0x60000+GoalDestination,-5,-5,-5);point(0x60000+GoalDestinationCopy,-5,-5,-5);
        point(Goal+GoalDestination,120,40,7);point(Goal+GoalDestinationCopy,120,40,7);
        put<uint64_t>(Ai+AiRequests,Heap+D);put<uint64_t>(D,Image+0x3e20000);
        put<int32_t>(D+RequestIndex,1);put<uint8_t>(D+RequestFixed,0);
        point(request()+RequestForced,1,2,3);put<uint32_t>(request()+RequestFlags,0x00010203u);
    }
};
const Vec3 mule{100,40,7},porter{60,40,7};
bool has(const std::string& text,const char* part){return text.find(part)!=std::string::npos;}
void reads(){
    World w;auto s=w.sampler();
    auto c=read(s,Image,Heap+Actor);
    CHECK(c.entity&&*c.entity==Heap+Entity&&c.component&&*c.component==Heap+Component&&c.componentVtable&&*c.componentVtable==Image+DsComponentVtable);
    CHECK(c.ai&&*c.ai==Heap+Ai&&c.agree()&&c.aiVtable&&*c.aiVtable==Image+AiVtable&&c.state&&*c.state==3);
    CHECK(c.goal&&*c.goal==Heap+Goal&&c.destination&&(*c.destination)[0]==120&&c.destinationCopy&&(*c.destinationCopy)[2]==7);
    CHECK(c.d&&*c.d==Heap+D&&c.index&&*c.index==1&&c.request&&*c.request==Heap+w.request()&&c.forced&&(*c.forced)[1]==2&&c.flags&&*c.flags==0x00010203u);
    auto line=describe(c,Image);
    CHECK(has(line," entity 20002000 component 20004000 componentvt image+3dca218 ai50 20010000 ai80 20010000 agree yes aivt image+3e11880 state 3 walk yes goal 20070000 dest9e0 120,40,7 dest850 120,40,7"));
    CHECK(has(line," d 20080000 dvt image+3e20000 index 1 request 20080760 forced 1,2,3 flags32 10203 flags8 3")&&!has(line,"unreadable"));
    // The base class of the component is taken too; the fixed byte means block 2.
    { World v;v.put<uint64_t>(Component,Image+BaseComponentVtable);v.put<uint8_t>(D+RequestFixed,1);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);
      CHECK(k.component&&*k.component==Heap+Component&&k.index&&*k.index==2&&k.request&&*k.request==Heap+v.request(2)); }
    // No body; no AI component; a table without the walk goal; an empty table.
    { World v;v.put<uint64_t>(Actor+ActorEntity,0);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.entity&&!*k.entity&&!k.component&&has(describe(k,Image)," entity 0 component unreadable")); }
    { World v;v.put<uint64_t>(Component,Image+0x3dd9750);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.component&&!*k.component&&!k.ai&&has(describe(k,Image)," component 0 componentvt unreadable ai50 unreadable")); }
    { World v;v.put<uint64_t>(Entries+5*0x20+8,Image+0x4c91d00);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.goal&&!*k.goal&&!k.destination&&has(describe(k,Image)," walk no goal 0 dest9e0 unreadable")&&k.request); }
    { World v;v.put<uint32_t>(Ai+AiGoals+0xc,0);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.goal&&!*k.goal); }
    // Each broken link is reported and not followed.
    { World v;v.blocked=Heap+Actor+ActorEntity;auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(!k.entity&&!k.component&&!k.ai&&!k.d&&has(describe(k,Image)," entity unreadable")); }
    { World v;v.put<uint64_t>(Actor+ActorEntity,0x21);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(!k.entity); }
    { World v;v.blocked=Heap+Component+ComponentAi;auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.component&&!k.ai&&!k.goal&&!k.d&&has(describe(k,Image)," ai50 unreadable ai80 20010000 agree unreadable")); }
    { World v;v.put<uint64_t>(Component+ComponentAiAgain,Heap+0x9000);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(!k.agree()&&has(describe(k,Image)," agree no")); }
    { World v;v.blocked=Heap+Entries+0x40;auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(!k.goal&&k.d&&k.request&&has(describe(k,Image)," walk unreadable goal unreadable")); }
    { World v;v.blocked=Heap+Goal+GoalDestination+8;auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.goal&&*k.goal&&!k.destination&&k.destinationCopy); }
    { World v;v.put<uint64_t>(Ai+AiRequests,7);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.d&&!k.dVtable&&!k.index&&!k.request&&k.goal&&*k.goal); }
    { World v;v.blocked=Heap+D+RequestIndex;auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.d&&!k.index&&!k.request&&has(describe(k,Image)," index unreadable request unreadable forced unreadable flags32 unreadable flags8 unreadable")); }
    // A table larger than any the game makes is not copied. (The entry is put where only a copy that large finds it.)
    { World v;v.put<uint32_t>(Ai+AiGoals+0xc,MaxGoals*2);v.put<uint32_t>(Entries+5*0x20,0);
      v.put<uint32_t>(Entries+uint64_t(MaxGoals+3)*0x20,0x80000007u);v.put<uint64_t>(Entries+uint64_t(MaxGoals+3)*0x20+8,Image+WalkGoalKey);v.put<uint64_t>(Entries+uint64_t(MaxGoals+3)*0x20+0x10,Heap+Goal);
      auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(!k.goal); }
    // A request index outside the block is shown and gives no request.
    for(int32_t bad:{-1,int32_t(RequestCount),1000000}){ World v;v.put<int32_t>(D+RequestIndex,bad);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);
      CHECK(k.index&&*k.index==bad&&!k.request&&!k.forced&&!k.flags); }
    { World v;v.put<int32_t>(D+RequestIndex,int32_t(RequestCount)-1);auto t=v.sampler();auto k=read(t,Image,Heap+Actor);CHECK(k.request&&*k.request==Heap+v.request(int32_t(RequestCount)-1)); }
}
void asks(){
    CHECK(ask(0)&&ask(0)->cm==0&&!ask(0)->repeat);
    CHECK(ask(MaxOffsetCm)&&ask(MaxOffsetCm)->cm==MaxOffsetCm&&!ask(MaxOffsetCm+1));
    CHECK(ask(RepeatBase+300)&&ask(RepeatBase+300)->cm==300&&ask(RepeatBase+300)->repeat);
    CHECK(!ask(RepeatBase+MaxOffsetCm+1)&&!ask(RepeatBase-1)&&!ask(0xffffffffu));
    auto e=east({1,2,3},250);CHECK(e[0]==3.5&&e[1]==2&&e[2]==3);
}
void writes(){
    // The write: the destination and the bit, and nothing else.
    { World w;auto s=w.sampler();auto c=read(s,Image,Heap+Actor);
      CHECK(refusal(s,Image,c,mule,porter,w.writable()).empty());
      auto before=w.heap;auto target=east(*c.destination,300);
      CHECK(force(s,Image,c,mule,porter,target,true,w.writable(),w.poke()).empty()&&w.pokes==2);
      CHECK(w.get<double>(w.request()+RequestForced)==123&&w.get<double>(w.request()+RequestForced+8)==40&&w.get<double>(w.request()+RequestForced+16)==7);
      CHECK(w.get<uint32_t>(w.request()+RequestFlags)==0x00010243u);
      std::memcpy(before.data()+w.request()+RequestForced,target.data(),24);before[w.request()+RequestFlags]|=ForcedBit;CHECK(before==w.heap);
      // A repeated write goes on when the MULE has arrived (the first one would refuse a MULE that stands at its goal).
      Vec3 there{119.8,40,7};CHECK(refusal(s,Image,c,there,porter,w.writable())=="not walking");
      CHECK(force(s,Image,c,there,porter,target,false,w.writable(),w.poke()).empty()&&w.pokes==4); }
    auto refused=[&](const char* why,void(*change)(World&),Vec3 m=mule,Vec3 p=porter){
        World w;change(w);auto s=w.sampler();auto c=read(s,Image,Heap+Actor);auto before=w.heap;
        auto r=refusal(s,Image,c,m,p,w.writable());
        Vec3 target{123,40,7};
        auto f=force(s,Image,c,m,p,target,true,w.writable(),w.poke());
        if(r!=why||f!=why||w.pokes||before!=w.heap){std::fprintf(stderr,"mule goal oracle: wanted \"%s\", refusal \"%s\", force \"%s\", %u pokes\n",why,r.c_str(),f.c_str(),w.pokes);++failures;}
    };
    refused("MULE nearer than 6 m to the porter",[](World&){},mule,Vec3{95,40,7});
    refused("position not finite",[](World&){},Vec3{std::nan(""),40,7});
    refused("no body",[](World& w){w.put<uint64_t>(Actor+ActorEntity,0);});
    refused("no AI component",[](World& w){w.put<uint64_t>(Component,Image+0x3dd9750);});
    refused("AI component is not a DSAIIndividualComponent",[](World& w){w.put<uint64_t>(Component,Image+BaseComponentVtable);});
    refused("the two AI pointers differ",[](World& w){w.put<uint64_t>(Component+ComponentAiAgain,Heap+0x9000);});
    refused("AI is not a DSAIIndividual",[](World& w){w.put<uint64_t>(Ai,Image+AiVtable+8);});
    refused("no walk goal",[](World& w){w.put<uint64_t>(Entries+5*0x20+8,Image+0x4c91d00);});
    refused("not walking",[](World& w){w.point(Goal+GoalDestination,100.5,40,7);w.point(Goal+GoalDestinationCopy,100.5,40,7);});
    refused("not walking",[](World& w){w.point(Goal+GoalDestinationCopy,121,40,7);});
    refused("not walking",[](World& w){w.put<double>(Goal+GoalDestination,std::nan(""));});
    refused("request unreadable",[](World& w){w.put<int32_t>(D+RequestIndex,int32_t(RequestCount));});
    refused("request unreadable",[](World& w){w.blocked=Heap+w.request()+RequestFlags;});
    refused("request owner has no vtable in the image",[](World& w){w.put<uint64_t>(D,Heap+0x9000);});
    refused("request owner has no vtable in the image",[](World& w){w.put<uint64_t>(D,Image+0x2000);});   // code, not data
    // Memory that is not plain writable from D to the end of the request, and a missing writer.
    { World w;auto s=w.sampler();auto c=read(s,Image,Heap+Actor);auto before=w.heap;
      Writable none=[](uint64_t,size_t){return false;};uint64_t askedAt=0;size_t askedSize=0;
      Writable record=[&](uint64_t at,size_t n){askedAt=at;askedSize=n;return true;};
      CHECK(refusal(s,Image,c,mule,porter,none)=="request not in plain writable memory of its owner"&&refusal(s,Image,c,mule,porter,Writable{})=="request not in plain writable memory of its owner");
      CHECK(force(s,Image,c,mule,porter,east(*c.destination,300),true,none,w.poke())=="request not in plain writable memory of its owner"&&!w.pokes&&before==w.heap);
      CHECK(refusal(s,Image,c,mule,porter,record).empty()&&askedAt==Heap+D&&askedSize==w.request()+RequestSize-D);
      CHECK(force(s,Image,c,mule,porter,east(*c.destination,300),true,w.writable(),Poke{})=="no writer"&&before==w.heap); }
    // The objects changed between the look and the write; a destination far from the MULE; one that is not a number.
    { World w;auto s=w.sampler();auto c=read(s,Image,Heap+Actor);auto before=w.heap;
      CHECK(force(s,Image,c,mule,porter,Vec3{100+MaxReachM+1,40,7},true,w.writable(),w.poke())=="destination over 200 m from the MULE");
      CHECK(force(s,Image,c,mule,porter,Vec3{std::nan(""),40,7},true,w.writable(),w.poke())=="destination not finite"&&!w.pokes&&before==w.heap);
      // Another AI, as valid as the first, now hangs on the component.
      constexpr uint64_t Ai2=0x40000;std::memcpy(w.heap.data()+Ai2,w.heap.data()+Ai,0x4000);before=w.heap;
      w.put<uint64_t>(Component+ComponentAi,Heap+Ai2);w.put<uint64_t>(Component+ComponentAiAgain,Heap+Ai2);before=w.heap;
      CHECK(refusal(s,Image,read(s,Image,Heap+Actor),mule,porter,w.writable()).empty());
      CHECK(force(s,Image,c,mule,porter,east(*c.destination,300),true,w.writable(),w.poke())=="the MULE's objects changed"&&!w.pokes&&before==w.heap); }
}
}
int main(){
    reads();asks();writes();
    if(failures){std::fprintf(stderr,"mule goal oracle: %d failures\n",failures);return 1;}
    std::puts("mule goal oracle: PASS");return 0;
}
