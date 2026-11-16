#include "offer_strand.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>
using namespace mod::offer_strand;
namespace own_strand=mod::own_strand;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"offer strand oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
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
    // A construction object (record - 0x18) with what the game's body is made from.
    uint64_t object(uint8_t type,uint8_t owner,uint8_t state,uint8_t flags,uint32_t level,std::array<double,3> p,std::array<float,9> r,uint64_t id,float angle=0){
        uint64_t o=next;next+=0x1000;block(o,0x710);
        put(o+0x34,type);put(o+0x36,state);put(o+0xb2,owner);put(o+0xab,flags);put(o+0x84,level);
        std::memcpy(at(o+0x40,24),p.data(),24);std::memcpy(at(o+0x58,36),r.data(),36);put(o+0xb8,id);
        put(o+0x528,angle);put(o+0x580,angle);put(o+0x590,angle);return o;}
};
static const std::array<float,9> Level={1,0,0, 0,1,0, 0,0,1},Yaw90={0,1,0, -1,0,0, 0,0,1},Pole={0,0,-1, -1,0,0, 0,1,0};
static bool near(int32_t a,int32_t b){return std::abs(int64_t(a)-b)<=2;}   // the game's atan2 may differ by an ulp
// A record as the structure reader gives it: built (state), owner, online id, flags +0xa8..+0xab.
static own_strand::Structure own(uint32_t id,uint8_t type,const std::string& online="",uint8_t owner=Player,uint8_t state=2,uint8_t ab=0){
    own_strand::Structure x;x.record.id=id;x.record.type=type;x.record.state=state;x.record.object=0x50000000+0x710ull*id;x.owner=owner;x.online=online;x.flags[3]=ab;return x;}
// The game as the offering sees it: its records, what it queued, and what it answers.
struct Game {
    std::vector<own_strand::Structure> records;std::vector<std::pair<uint64_t,bool>> asked;std::map<uint64_t,int> queue;bool callable=true;
    Offerer::Calls calls(){Offerer::Calls c;
        c.scan=[this]{return records;};
        c.ask=[this](uint64_t o,bool again){if(!callable)return false;asked.push_back({o,again});queue[o]=1;return true;};
        c.queued=[this](uint64_t o,bool){auto q=queue.find(o);return q==queue.end()?0:q->second;};return c;}
    own_strand::Structure* find(uint32_t id){for(auto& r:records)if(r.record.id==id)return &r;return nullptr;}
};
static bool has(const std::vector<std::string>& lines,const char* text){for(auto& l:lines)if(l.find(text)!=std::string::npos)return true;return false;}
static void offering(){
    // Whether it offers at all: the option, the server in the config, signed in, and the server's yes.
    CHECK(active(true,true,true,"yes"));
    CHECK(!active(false,true,true,"yes")&&!active(true,false,true,"yes")&&!active(true,true,false,"yes"));
    CHECK(!active(true,true,true,"no")&&!active(true,true,true,"")&&!active(true,true,true,"maybe"));
    // Which ids the server keeps for a re-create: the kind's letter in lower case only.
    CHECK(kept("bK3v9QmZ0aB1c",'b')&&!kept("BK3v9QmZ0aB1c",'b')&&!kept("nQ7fT2nP4xY8w",'b')&&!kept("",'b'));
    // What plan() is given from a read record, and the reader's filter.
    {auto x=own(10001,25,"lA1b2C3d4E5f6",Player,2,0x80);auto o=of(x);CHECK(o.type==25&&o.owner==Player&&o.state==2&&o.held&&o.online=="lA1b2C3d4E5f6");
     auto f=select();CHECK(f.record&&f.object&&f.record(own(1,25,"",Player,1).record)&&!f.record(own(1,25,"",Player,0).record)&&f.object(own(1,25))&&!f.object(own(1,26,"",2)));}

    Game g;
    g.records={own(10001,25),own(10002,31,"bK3v9QmZ0aB1c"),own(10003,18,"NQ7fT2nP4xY8w"),own(10004,26,"",2),own(10005,6,"",Player,2,0x80),own(10006,4)};
    Offerer o;auto c=g.calls();
    CHECK(o.step(false,0,c).empty()&&g.asked.empty());
    auto lines=o.step(true,1000,c);CHECK(has(lines,"OFFER on:")&&g.asked.empty()&&o.counters().on);
    CHECK(o.step(true,1000+Offerer::SettleMs-1,c).empty()&&g.asked.empty());     // the game's own requests first
    const uint64_t t=1000+Offerer::SettleMs;
    lines=o.step(true,t,c);
    CHECK(g.asked.size()==1&&g.asked[0]==std::make_pair(g.find(10001)->record.object,false)&&has(lines,"1 own structures to upload, 1 to send again with their ids, 4 left out")&&has(lines,"asked the game to upload ladder record 10001"));
    // One at a time: nothing else while the ladder has no answer.
    o.step(true,t+Offerer::PollMs,c);o.step(true,t+2*Offerer::PollMs,c);CHECK(g.asked.size()==1);
    // Answered: its id is in the record and it has left the queue.
    g.find(10001)->online="lZ9y8X7w6V5u4";g.queue.erase(g.find(10001)->record.object);
    lines=o.step(true,t+3*Offerer::PollMs,c);CHECK(has(lines,"upload ladder lZ9y8X7w6V5u4 record 10001: uploaded")&&o.counters().uploaded==1&&g.asked.size()==1);
    CHECK(o.step(true,t+3*Offerer::PollMs+Offerer::GapMs-1,c).empty());
    // The bridge with an id of the original service: sent again with it; done once the game no longer has it queued.
    lines=o.step(true,t+3*Offerer::PollMs+Offerer::GapMs,c);
    CHECK(g.asked.size()==2&&g.asked[1]==std::make_pair(g.find(10002)->record.object,true)&&has(lines,"send again bridge bK3v9QmZ0aB1c record 10002 (on another server)"));
    uint64_t now=t+3*Offerer::PollMs+Offerer::GapMs+Offerer::PollMs;
    lines=o.step(true,now,c);CHECK(lines.empty());
    g.queue.erase(g.find(10002)->record.object);now+=Offerer::PollMs;
    lines=o.step(true,now,c);CHECK(has(lines,"sent again with its id")&&o.counters().recreated==1);
    // Nothing more: the capital-letter id, another player's ladder, a held record and a kind the game does not upload stay.
    now+=Offerer::GapMs;lines=o.step(true,now,c);CHECK(has(lines,"nothing more to offer: 1 uploaded, 1 sent again")&&g.asked.size()==2&&o.counters().left==4);
    CHECK(o.step(true,now+Offerer::PollMs,c).empty());
    // A new own record later (a load, a new build the game did not take): asked for at the next look; the ladder never again.
    g.records.push_back(own(10007,18));g.find(10001)->online.clear();
    lines=o.step(true,now+Offerer::RescanMs,c);CHECK(g.asked.size()==3&&g.asked[2].first==g.find(10007)->record.object&&has(lines,"asked the game to upload rain shelter record 10007"));

    // Refused (out of the queue with no id), not queued at all, no answer, gone: each one finished, and each record once.
    {Game h;h.records={own(1,25),own(2,25),own(3,25),own(4,25)};Offerer q;auto k=h.calls();
     q.step(true,0,k);now=Offerer::SettleMs;q.step(true,now,k);CHECK(h.asked.size()==1);
     h.queue.clear();now+=Offerer::PollMs;lines=q.step(true,now,k);CHECK(has(lines,"answered without an id")&&q.counters().refused==1);
     // The next: the game does not queue it.
     auto ask=k.ask;k.ask=[&](uint64_t a,bool r){bool ok=ask(a,r);h.queue.erase(a);return ok;};
     now+=Offerer::GapMs;lines=q.step(true,now,k);CHECK(h.asked.size()==2&&has(lines,"the game did not queue it")&&q.counters().unqueued==1);
     k.ask=ask;
     // The third: no answer for a minute; then the fourth.
     now+=Offerer::GapMs;q.step(true,now,k);CHECK(h.asked.size()==3);const uint64_t asked=now;
     for(now+=Offerer::PollMs;now<asked+Offerer::TimeoutMs;now+=Offerer::PollMs)q.step(true,now,k);
     CHECK(h.asked.size()==3);
     lines=q.step(true,now,k);CHECK(has(lines,"no answer within a minute")&&q.counters().timedOut==1);
     now+=Offerer::GapMs;q.step(true,now,k);CHECK(h.asked.size()==4&&h.asked[3].first==h.records[3].record.object);
     // Gone (dismantled) before its answer.
     h.records.pop_back();now+=Offerer::PollMs;lines=q.step(true,now,k);CHECK(has(lines,"gone before an answer")&&q.counters().gone==1);
     // Nobody is asked for twice, whatever happened to it.
     now+=Offerer::GapMs;lines=q.step(true,now,k);CHECK(h.asked.size()==4&&has(lines,"nothing more to offer"));
     std::set<uint64_t> once;for(auto& a:h.asked)CHECK(once.insert(a.first).second);}

    // Off (signed out, or the server's yes withdrawn): nothing asked, the waiting stops; on again: the settling again.
    {Game h;h.records={own(1,25),own(2,25)};Offerer q;auto k=h.calls();
     q.step(true,0,k);q.step(true,Offerer::SettleMs,k);CHECK(h.asked.size()==1);
     lines=q.step(false,Offerer::SettleMs+Offerer::PollMs,k);CHECK(has(lines,"OFFER off")&&!q.counters().on);
     for(uint64_t at=Offerer::SettleMs;at<5*Offerer::SettleMs;at+=Offerer::PollMs)q.step(false,at,k);
     CHECK(h.asked.size()==1);
     now=5*Offerer::SettleMs;q.step(true,now,k);q.step(true,now+Offerer::SettleMs-1,k);CHECK(h.asked.size()==1);
     q.step(true,now+Offerer::SettleMs,k);CHECK(h.asked.size()==2&&h.asked[1].first==h.records[1].record.object);}

    // The game's call is not the expected one: stopped, never asked again.
    {Game h;h.records={own(1,25),own(2,25)};h.callable=false;Offerer q;auto k=h.calls();
     q.step(true,0,k);lines=q.step(true,Offerer::SettleMs,k);CHECK(has(lines,"OFFER stopped")&&q.counters().broken);
     h.callable=true;for(uint64_t at=Offerer::SettleMs;at<Offerer::SettleMs+5*Offerer::RescanMs;at+=Offerer::GapMs)q.step(true,at,k);
     CHECK(h.asked.empty());}
}
// The code and the uploader, in a fake image with sites of its own (the pinned build's bytes at small addresses).
static void identity(){
    const uint64_t Base=0x140000000;Sites at;at.upload=0x1100;at.recreate=0x1200;at.queue=0x1300;at.slot=0x3000;at.global=0x3010;at.vtable=0x3100;
    const std::vector<unsigned char> upload={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0xa3,0xb0,0x15,0x02,0x48,0x8b,0xd9,0x48,0x8b,0xd3,0x48,0x8b,0x08,
        0x48,0x8b,0x01,0xff,0x50,0x28,0xc7,0x83,0x18,0x05,0x00,0x00,0x00,0x00,0xf0,0x42,0x48,0x83,0xc4,0x20,0x5b,0xc3};
    const std::vector<unsigned char> again={0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xf1,0x48,0x8b,0xca,0x48,
        0x8b,0xfa,0xff,0x10,0x48,0x8d,0x4e,0x38,0x48,0x89,0x44,0x24,0x30,0x48,0x8d,0x54,0x24,0x30,0xe8,0x55};
    auto image=[&](uint64_t uploadAt,uint64_t againAt,bool twice){
        Memory m;m.block(Base,0x4000);
        std::memcpy(m.at(Base+uploadAt,upload.size()),upload.data(),upload.size());std::memcpy(m.at(Base+againAt,again.size()),again.data(),again.size());
        if(twice)std::memcpy(m.at(Base+0x1800,upload.size()),upload.data(),upload.size());
        return m;};
    auto sampler=[&](Memory& m){mod::discovery::Sampler s{[&m](uint64_t a,void* p,size_t n){return m.read(a,p,n);},Base};
        s.sections={{Base+0x1000,0x1000,true},{Base+0x2000,0x2000,false}};return s;};
    {auto m=image(0x1100,0x1200,false);auto s=sampler(m);CHECK(verifyCode(s,at).empty());}
    {auto m=image(0x1100,0x1200,true);auto s=sampler(m);CHECK(!verifyCode(s,at).empty());}            // twice in the image
    {auto m=image(0x1140,0x1200,false);auto s=sampler(m);CHECK(!verifyCode(s,at).empty());}           // elsewhere
    {auto m=image(0x1100,0x1200,false);m.at(Base+0x1100+27,1)[0]=0x30;auto s=sampler(m);CHECK(!verifyCode(s,at).empty());}   // another slot
    {auto m=image(0x1100,0x1200,false);m.at(Base+0x1200+39,1)[0]=0x56;auto s=sampler(m);CHECK(!verifyCode(s,at).empty());}   // another callee

    // The uploader: [[image+slot]], the pointer the image's own global, its class and both slots.
    Memory m;m.block(Base,0x4000);const uint64_t O=0x60000000;m.block(O,0x100);
    m.put<uint64_t>(Base+at.slot,Base+at.global);m.put<uint64_t>(Base+at.global,O);m.put<uint64_t>(O,Base+at.vtable);
    m.put<uint64_t>(Base+at.vtable+SlotUpload,Base+at.queue);m.put<uint64_t>(Base+at.vtable+SlotRecreate,Base+at.recreate);
    auto s=sampler(m);CHECK(uploader(s,at)==O);
    m.put<uint64_t>(O,Base+at.vtable+8);CHECK(uploader(s,at)==0);m.put<uint64_t>(O,Base+at.vtable);
    m.put<uint64_t>(Base+at.vtable+SlotRecreate,Base+at.queue);CHECK(uploader(s,at)==0);m.put<uint64_t>(Base+at.vtable+SlotRecreate,Base+at.recreate);
    m.put<uint64_t>(Base+at.vtable+SlotUpload,Base+at.recreate);CHECK(uploader(s,at)==0);m.put<uint64_t>(Base+at.vtable+SlotUpload,Base+at.queue);
    m.put<uint64_t>(Base+at.slot,Base+at.global+8);CHECK(uploader(s,at)==0);m.put<uint64_t>(Base+at.slot,Base+at.global);
    m.put<uint64_t>(Base+at.global,0);CHECK(uploader(s,at)==0);m.put<uint64_t>(Base+at.global,O);CHECK(uploader(s,at)==O);

    // Its queues: listed, in flight, neither; a count that is none is unreadable.
    const uint64_t list=0x61000000,a=0x50001000,b=0x50002000,c=0x50003000;m.block(list,0x40);
    m.put<int32_t>(O+CreateQueue,2);m.put<uint64_t>(O+CreateQueue+8,list);m.put<uint64_t>(list,a);m.put<uint64_t>(list+8,b);m.put<uint64_t>(O+CreateQueue+0x10,c);
    CHECK(queued(s,O,a,false)==1&&queued(s,O,b,false)==1&&queued(s,O,c,false)==1&&queued(s,O,0x50004000,false)==0);
    CHECK(queued(s,O,a,true)==0);   // the re-create queue is empty
    m.put<int32_t>(O+RecreateQueue,1);m.put<uint64_t>(O+RecreateQueue+8,list+8);CHECK(queued(s,O,b,true)==1&&queued(s,O,a,true)==0);
    m.put<int32_t>(O+CreateQueue,-1);CHECK(queued(s,O,a,false)==-1&&queued(s,O,c,false)==1);
    m.put<int32_t>(O+CreateQueue,int32_t(MaxQueue)+1);CHECK(queued(s,O,a,false)==-1);
    CHECK(queued(s,0,a,false)==-1);
}
int main(){
    Memory m;mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){return m.read(a,p,n);},0x140000000};
    const uint64_t none=m.string(""),post=m.string("pK3v9QmZ0aB1c"),capital=m.string("NQ7fT2nP4xY8w"),wrong=m.string("lA1b2C3d4E5f6");
    const uint64_t bridge=m.string("bK3v9QmZ0aB1c"),huge=m.string("pHUGE",100),odd=m.string("p-1b2C3d4E5f6");
    const std::array<double,3> knot={1054.34,-3512.77,143.63};

    // Reading: the fields, the id, a bridge's own angle; an id 100 characters long is no string.
    uint64_t ladder=m.object(25,Player,2,0,0,knot,Yaw90,none);
    auto l=read(s,ladder);CHECK(l.has_value());
    if(l){CHECK(l->type==25&&l->owner==Player&&l->state==2&&!l->held&&l->level==0&&l->online.empty());
        CHECK(l->position[1]==-3512.77&&l->rotation[1]==1.0f);}
    auto b30=read(s,m.object(31,Player,1,0,1,knot,Level,bridge,0.5f));CHECK(b30&&b30->angle==0.5f&&b30->online=="bK3v9QmZ0aB1c");
    CHECK(!read(s,m.object(25,Player,2,0,0,knot,Level,huge)).has_value());
    CHECK(!read(s,0x10).has_value());

    // Which records are offered, and how.
    auto decide=[&](uint8_t type,uint8_t owner,uint8_t state,uint8_t flags,uint64_t id){
        auto x=read(s,m.object(type,owner,state,flags,1,knot,Level,id));return x?plan(*x).plan:Plan::Skip;};
    CHECK(decide(25,Player,2,0,none)==Plan::Upload);          // his ladder, on no server
    CHECK(decide(6,Player,1,0,post)==Plan::Recreate);         // his post with an id of the original service
    CHECK(decide(18,Player,2,0,capital)==Plan::Recreate);     // the original service's own form: the letter in capitals
    CHECK(decide(6,Player,1,0,wrong)==Plan::Skip);            // an id whose letter is another kind's
    CHECK(decide(6,Player,1,0,odd)==Plan::Skip);              // an id with a character no server id has
    CHECK(decide(26,2,2,0,none)==Plan::Skip);                 // another player's (Net) ladder
    CHECK(decide(6,0,1,0,none)==Plan::Skip);                  // the game's own
    CHECK(decide(18,Player,0,0,none)==Plan::Skip);            // a request the game has not built
    CHECK(decide(18,Player,2,0x80,none)==Plan::Skip);         // a record the game refuses to upload
    CHECK(decide(4,Player,2,0,none)==Plan::Skip);             // a type the game sends as "unknown"
    CHECK(letter(3)=='s'&&letter(7)=='p'&&letter(22)=='z'&&letter(34)=='b'&&letter(41)=='g'&&letter(44)=='j'&&letter(26)==0);

    // The map cell: tiles of the game's size counted from the middle, two tiles a cell, the edges held.
    auto c=cell(1054.34,411.51,512,36,30);CHECK(c[0]==10&&c[1]==7);
    c=cell(1e6,-1e6,512,36,30);CHECK(c[0]==17&&c[1]==0);
    c=cell(-20000,-5000,512,36,30);CHECK(c[0]==0&&c[1]==2);
    c=cell(5,5,0,36,30);CHECK(c[0]==0&&c[1]==0);

    // Angles: degrees x 10^5; a yaw-only structure has only rz; standing on end, rz comes from the forward row.
    auto a=angles(Level);CHECK(a[0]==0&&a[1]==0&&a[2]==0);
    a=angles(Yaw90);CHECK(a[0]==0&&a[1]==0&&near(a[2],9000000));
    a=angles(Pole);CHECK(a[0]==0&&near(a[1],9000000)&&near(a[2],9000000));

    // The body as the game sends it: positions from the f32 of each coordinate, "st" by kind, the extra blocks.
    Placement here{10,7,22123,226,0,0,3};
    if(l){
        auto y=angles(Yaw90);
        std::string want="{\"x\":10,\"y\":7,\"m\":22123,\"t\":\"l\",\"px\":105434000,\"py\":-351276992,\"pz\":14363000,\"rx\":"+std::to_string(y[0])+
            ",\"ry\":"+std::to_string(y[1])+",\"rz\":"+std::to_string(y[2])+",\"exp\":5,\"q\":226,\"st\":\"0\",\"p\":0,\"lp\":3}";
        CHECK(body(*l,here)==want);
        CHECK(body(*l,here,true).empty());                    // no id to re-create with
    }
    if(b30){auto t=body(*b30,here);
        CHECK(t.find("\"t\":\"b\"")!=std::string::npos&&t.find("\"st\":\"30\"")!=std::string::npos&&t.find(",\"bi\":{\"a\":2864788},\"lp\":3}")!=std::string::npos);
        auto r=body(*b30,here,true);const std::string tail=",\"id\":\"bK3v9QmZ0aB1c\"}";
        CHECK(r.size()>tail.size()&&r.compare(r.size()-tail.size(),tail.size(),tail)==0&&r.find(",\"lp\":3,\"id\"")!=std::string::npos);}
    auto b45=read(s,m.object(34,Player,2,0,1,knot,Level,none,0.5f));
    if(b45){auto t=body(*b45,here);CHECK(t.find("\"st\":\"\"")!=std::string::npos&&t.find("\"bi\":{\"a\":2864788}")!=std::string::npos);}
    auto rope=read(s,m.object(28,Player,2,0,0,knot,Level,none));CHECK(rope&&body(*rope,here).empty());
    auto shelter=read(s,m.object(18,Player,2,0,1,knot,Level,none));
    if(shelter){Placement aside=here;aside.alternative=227;aside.priority=1;auto t=body(*shelter,aside);
        CHECK(t.find("\"st\":\"\",\"p\":1,\"ei\":{\"aq\":227},\"lp\":3}")!=std::string::npos&&t.find("bi")==std::string::npos);}

    offering();identity();
    if(failures){std::fprintf(stderr,"offer strand oracle: %d failures\n",failures);return 1;}
    std::puts("offer strand ok");return 0;
}
