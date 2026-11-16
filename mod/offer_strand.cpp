#include "offer_strand.h"
#include "received.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace mod::offer_strand {
std::optional<Object> read(const discovery::Sampler& s,uint64_t o){
    Object x;uint8_t flags=0;uint64_t id=0;
    if(!s.read(o+0x34,&x.type,1)||!s.read(o+0x36,&x.state,1)||!s.read(o+0xb2,&x.owner,1)||!s.read(o+0xab,&flags,1)||
       !s.read(o+0x84,&x.level,4)||!s.read(o+0x40,x.position.data(),24)||!s.read(o+0x58,x.rotation.data(),36)||!s.read(o+0xb8,&id,8))
        return std::nullopt;
    x.held=flags&0x80;
    // The game's body takes a bridge's angle from +0x528, a chiral bridge's from +0x590, a jump stand's from +0x580.
    uint64_t at=x.type==31||x.type==34?0x528:x.type==41?0x590:x.type==44?0x580:0;
    if(at&&!s.read(o+at,&x.angle,4))return std::nullopt;
    // An engine string: the characters, their length (u32) 8 bytes in front.
    uint32_t n=0;
    if(id&&s.read(id-8,&n,4)&&n){
        if(n>MaxId)return std::nullopt;              // no id of the game's is this long: not a string
        x.online.assign(n,'\0');if(!s.read(id,x.online.data(),n))return std::nullopt;
    }
    return x;
}
char letter(uint8_t t){
    switch(t){
        case 3:return 's';case 6:case 7:return 'p';case 9:return 'w';case 12:return 'a';case 15:return 'c';
        case 18:return 'n';case 22:return 'z';case 25:return 'l';case 28:return 'r';case 31:case 34:return 'b';
        case 36:return 'h';case 41:return 'g';case 44:return 'j';default:return 0;
    }
}
// An id as the game's servers make them: the kind's letter (the original service's own uploads may carry it in
// capitals) and 12 letters or digits.
static bool gameShape(const std::string& id,char kind){
#ifndef MUTANT_OFFER_SHAPE
    if(id.size()!=13||(id[0]!=kind&&id[0]!=std::toupper(static_cast<unsigned char>(kind))))return false;
#else
    if(id.size()!=13)return false;
#endif
    for(size_t i=1;i<id.size();++i)if(!std::isalnum(static_cast<unsigned char>(id[i])))return false;
    return true;
}
Decision plan(const Object& x){
#ifndef MUTANT_OFFER_OWNER
    if(x.owner!=Player)return {Plan::Skip,"not built by this player"};
#endif
#ifndef MUTANT_OFFER_PENDING
    if(x.state==0)return {Plan::Skip,"not built"};   // a request the game has not built: nothing stands there
#endif
#ifndef MUTANT_OFFER_HELD
    if(x.held)return {Plan::Skip,"the game does not upload it"};
#endif
    char k=letter(x.type);
    if(!k)return {Plan::Skip,"a kind the game does not upload"};
    if(x.online.empty())return {Plan::Upload,"on no server"};
    if(!gameShape(x.online,k))return {Plan::Skip,"an id of another shape"};
    return {Plan::Recreate,"on another server"};
}
std::array<int32_t,2> cell(double x,double y,int32_t tile,int32_t width,int32_t height){
    if(tile<=0||width<=0||height<=0)return {0,0};
    double t=static_cast<double>(static_cast<float>(tile));   // the game: int to float to double
    auto one=[&](double v,int32_t n){
        double f=std::floor(v/t);if(!(f>-1e9&&f<1e9))f=f<0?-1e9:1e9;
        int64_t c=static_cast<int64_t>(f)+n/2;
#ifndef MUTANT_OFFER_CLAMP
        if(c<0)c=0;else if(c>n-1)c=n-1;
#endif
        return static_cast<int32_t>(c/2);};
    return {one(x,width),one(y,height)};
}
// cvttss2si: truncation, and the "integer indefinite" value for what does not fit.
static int32_t truncate(float v){
    if(!(v>-2147483648.0f&&v<2147483648.0f))return INT32_MIN;
    return static_cast<int32_t>(v);
}
static const float Degrees=57.2957763671875f,Scale=100000.0f;
std::array<int32_t,3> angles(const std::array<float,9>& m){
    // 0x19c1270: ry = asin(-m02); rx = atan2(m12, m22), rz = atan2(m01, m00); near the poles rx 0, rz atan2(-m10, m11).
    float s=-m[2];if(!(s>-1.0f))s=-1.0f;else if(s>1.0f)s=1.0f;
    float ry=std::asin(s),rx=0,rz;
#ifndef MUTANT_OFFER_POLE
    if(std::cos(ry)>1.9073486328125e-06f){rx=std::atan2(m[5],m[8]);rz=std::atan2(m[1],m[0]);}
    else rz=std::atan2(-m[3],m[4]);
#else
    rx=std::atan2(m[5],m[8]);rz=std::atan2(m[1],m[0]);
#endif
    return {truncate((Degrees*rx)*Scale),truncate((Degrees*ry)*Scale),truncate((Degrees*rz)*Scale)};
}
static int32_t place(double v){
#ifndef MUTANT_OFFER_FLOAT
    return truncate(static_cast<float>(v)*Scale);    // the game multiplies the f32 of the position
#else
    double d=v*100000.0;return d>-2147483648.0&&d<2147483648.0?static_cast<int32_t>(d):INT32_MIN;
#endif
}
std::string body(const Object& x,const Placement& p,bool recreate){
    char k=letter(x.type);
    if(!k||k=='r')return {};                         // a rope's "ri" is filled through a call into the object
    std::string st;
    if(k=='l')st=std::to_string(x.level);
#ifndef MUTANT_OFFER_BRIDGE
    else if(x.type==31)st="30";                      // the 30 m bridge; the 45 m bridge sends an empty "st"
#else
    else if(k=='b')st="30";
#endif
    auto a=angles(x.rotation);
    char b[512];
    int n=std::snprintf(b,sizeof b,"{\"x\":%d,\"y\":%d,\"m\":%d,\"t\":\"%c\",\"px\":%d,\"py\":%d,\"pz\":%d,\"rx\":%d,\"ry\":%d,\"rz\":%d,"
        "\"exp\":5,\"q\":%d,\"st\":\"%s\",\"p\":%d",p.x,p.y,p.map,k,place(x.position[0]),place(x.position[1]),place(x.position[2]),
        a[0],a[1],a[2],p.area,st.c_str(),p.priority);
    std::string out(b,n>0?size_t(n):0);
    if(k=='b'||k=='g'||k=='j')out+=",\"bi\":{\"a\":"+std::to_string(truncate(Scale*(Degrees*x.angle)))+"}";
    if(p.alternative>0)out+=",\"ei\":{\"aq\":"+std::to_string(p.alternative)+"}";
    out+=",\"lp\":"+std::to_string(p.likes);
    if(recreate){
        if(!gameShape(x.online,k))return {};
        out+=",\"id\":\""+x.online+"\"";
    }
    return out+"}";
}
bool active(bool option,bool server,bool signedIn,const std::string& offerOld){
#ifndef MUTANT_OFFER_OPTION
    if(!option)return false;
#endif
#ifndef MUTANT_OFFER_SIGNED
    if(!server||!signedIn)return false;              // only to our own server, and only while the game is signed in to it
#endif
#ifndef MUTANT_OFFER_YES
    return offerOld=="yes";                          // the player's (or the server's) yes; nothing heard is no
#else
    return offerOld!="no";
#endif
}
bool kept(const std::string& online,char kind){
#ifndef MUTANT_OFFER_KEPT
    return gameShape(online,kind)&&online[0]==kind;  // the server takes the letter in lower case only
#else
    return gameShape(online,kind);
#endif
}
Object of(const own_strand::Structure& x){
    Object o;o.type=x.record.type;o.owner=x.owner;o.state=x.record.state;o.held=x.flags[3]&0x80;o.level=x.record.level;o.online=x.online;
    return o;
}
namespace {
bool built(const structures::Record& r){return r.state!=0;}
bool own(const own_strand::Structure& x){return x.owner==Player;}
const char* why(Plan p){return p==Plan::Upload?"on no server":"on another server";}
}
own_strand::Select select(){return {built,own};}
namespace {
// The pinned build's code (game_offsets.md, "Offering the player's existing strand"): 0x2c9ed40 whole (it reads the
// uploader through [rip -> 0x4df9df0], calls its slot 0x28, sets the object's timer +0x518 to 120.0f), and the first 40
// bytes of the re-create queue 0x3484f80.
const unsigned char UploadCode[44]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0xa3,0xb0,0x15,0x02,0x48,0x8b,0xd9,0x48,0x8b,0xd3,0x48,0x8b,0x08,
    0x48,0x8b,0x01,0xff,0x50,0x28,0xc7,0x83,0x18,0x05,0x00,0x00,0x00,0x00,0xf0,0x42,0x48,0x83,0xc4,0x20,0x5b,0xc3};
const unsigned char RecreateCode[40]={0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xf1,0x48,0x8b,0xca,0x48,
    0x8b,0xfa,0xff,0x10,0x48,0x8d,0x4e,0x38,0x48,0x89,0x44,0x24,0x30,0x48,0x8d,0x54,0x24,0x30,0xe8,0x55};
}
std::string verifyCode(const discovery::Sampler& s,const Sites& m){
    struct Want{uint64_t rva;const unsigned char* bytes;size_t size;const char* name;};
    const Want wants[]={{m.upload,UploadCode,sizeof UploadCode,"upload call"},{m.recreate,RecreateCode,sizeof RecreateCode,"re-create queue"}};
    unsigned counts[2]={};
    for(auto& section:s.sections)if(section.executable){
        if(section.size>128ull*1024*1024)return "code section too large";
        std::vector<unsigned char> code(section.size);if(!s.readStrict(section.address,code.data(),code.size()))return "code unreadable";
        for(unsigned k=0;k<2;++k)for(auto at=code.begin();(at=std::search(at,code.end(),wants[k].bytes,wants[k].bytes+wants[k].size))!=code.end();++at){
            ++counts[k];
#ifndef MUTANT_OFFER_SIGNATURE
            if(section.address+uint64_t(at-code.begin())!=s.base+wants[k].rva)return std::string(wants[k].name)+" signature moved or duplicated";
#endif
        }
    }
#ifndef MUTANT_OFFER_SIGNATURE
    for(unsigned k=0;k<2;++k)if(counts[k]!=1)return std::string(wants[k].name)+" signature missing or ambiguous";
#else
    (void)counts;
#endif
    return {};
}
uint64_t uploader(const discovery::Sampler& s,const Sites& m){
    uint64_t slot=0,o=0,vt=0,upload=0,recreate=0;
    if(!s.read(s.base+m.slot,&slot,8)||slot!=s.base+m.global||!s.read(slot,&o,8)||!o||o>=0x800000000000ULL)return 0;
#ifndef MUTANT_OFFER_IDENTITY
    if(!s.read(o,&vt,8)||vt!=s.base+m.vtable||!s.read(vt+SlotUpload,&upload,8)||upload!=s.base+m.queue||!s.read(vt+SlotRecreate,&recreate,8)||recreate!=s.base+m.recreate)return 0;
#else
    (void)vt;(void)upload;(void)recreate;
#endif
    return o;
}
int queued(const discovery::Sampler& s,uint64_t u,uint64_t object,bool recreate){
    const uint64_t at=u+(recreate?RecreateQueue:CreateQueue);
    unsigned char q[0x18];if(!u||!s.read(at,q,sizeof q))return -1;
    int32_t count;uint64_t items,flying;std::memcpy(&count,q,4);std::memcpy(&items,q+8,8);std::memcpy(&flying,q+0x10,8);
    if(flying==object)return 1;
    if(count<0||count>int32_t(MaxQueue)||(count&&!items))return -1;
    std::vector<uint64_t> list(static_cast<size_t>(count));if(count&&!s.read(items,list.data(),8ull*size_t(count)))return -1;
    return std::find(list.begin(),list.end(),object)!=list.end()?1:0;
}
std::string Offerer::finish(const Current& c,const char* how,uint64_t now){
    current_.reset();next_=now+GapMs;
    char b[240];std::snprintf(b,sizeof b,"OFFER %s %s%s%s record %u: %s\n",c.recreate?"re-create":"upload",received::name(c.type),c.online.empty()?"":" ",c.online.c_str(),c.id,how);
    return b;
}
std::vector<std::string> Offerer::step(bool on,bool ready,uint64_t now,const Calls& calls){
    std::vector<std::string> out;
    if(!on){
        if(k_.on){k_.on=false;out.push_back(current_?"OFFER off: not signed in to the server, or no yes from it for the older strand; the structure asked for last is the game's now\n":
                                                       "OFFER off: not signed in to the server, or no yes from it for the older strand\n");}
        current_.reset();return out;
    }
    if(k_.broken)return out;
    if(!k_.on){k_.on=true;idle_=false;
#ifndef MUTANT_OFFER_SETTLE
        next_=now+SettleMs;                          // the game's own area requests and uploads first
#else
        next_=now;
#endif
        out.push_back("OFFER on: this player's older strand is offered to the server in "+std::to_string(SettleMs/1000)+" s, one structure at a time\n");}
    if(now<next_)return out;
#ifndef MUTANT_OFFER_READY
    if(!ready)return out;                            // the records and the game's queues only in play
#else
    (void)ready;
#endif
    auto seen=calls.scan?calls.scan():std::vector<own_strand::Structure>{};
    if(current_){
        Current c=*current_;
        auto at=std::find_if(seen.begin(),seen.end(),[&](const own_strand::Structure& x){return x.record.object==c.object&&x.record.id==c.id;});
        if(at==seen.end()){++k_.gone;out.push_back(finish(c,"gone before an answer",now));return out;}
        if(!c.recreate&&!at->online.empty()){++k_.uploaded;Current d=c;d.online=at->online;out.push_back(finish(d,"uploaded, the server's id is in the record",now));return out;}
        const int q=calls.queued?calls.queued(c.object,c.recreate):-1;
        if(q==0){
            if(c.recreate){++k_.recreated;out.push_back(finish(c,"sent again with its id (the game keeps its id whatever the answer)",now));}
            else{++k_.refused;out.push_back(finish(c,"answered without an id (the server refused it, or the game dropped it)",now));}
            return out;}
#ifndef MUTANT_OFFER_TIMEOUT
        if(now>=c.since+TimeoutMs){++k_.timedOut;out.push_back(finish(c,"no answer within a minute; going on",now));return out;}
#endif
#ifndef MUTANT_OFFER_PACE
        next_=now+PollMs;return out;                 // one at a time: the next only once this one is answered
#else
        current_.reset();
#endif
    }
    // The next record: built, this porter's, one the game uploads, not asked for before in this run.
    unsigned upload=0,recreate=0,left=0;const own_strand::Structure* next=nullptr;Plan how=Plan::Skip;
    for(auto& x:seen){
        auto d=plan(of(x));
        if(d.plan==Plan::Recreate&&!kept(x.online,letter(x.record.type)))d={Plan::Skip,"an id the server does not keep"};
        if(d.plan==Plan::Skip){++left;continue;}
#ifndef MUTANT_OFFER_ONCE
        if(done_.count(x.record.id))continue;
#endif
        (d.plan==Plan::Upload?upload:recreate)++;
        if(!next){next=&x;how=d.plan;}
    }
    k_.left=left;
    if(!next){
        if(!idle_){idle_=true;char b[200];std::snprintf(b,sizeof b,"OFFER nothing more to offer: %u uploaded, %u sent again with their ids, %u not taken, %u without an answer; %u own structures left out\n",
            k_.uploaded,k_.recreated,k_.refused,k_.timedOut,left);out.push_back(b);}
        next_=now+RescanMs;return out;
    }
    if(idle_||k_.asked==0){idle_=false;char b[160];std::snprintf(b,sizeof b,"OFFER %u own structures to upload, %u to send again with their ids, %u left out\n",upload,recreate,left);out.push_back(b);}
    done_.insert(next->record.id);
    Current c;c.object=next->record.object;c.id=next->record.id;c.type=next->record.type;c.recreate=how==Plan::Recreate;c.online=next->online;c.since=now;
    if(!calls.ask||!calls.ask(c.object,c.recreate)){k_.broken=true;out.push_back("OFFER stopped: the game's upload call is not the one this build expects\n");return out;}
    ++k_.asked;
    char b[200];std::snprintf(b,sizeof b,"OFFER asked the game to %s %s%s%s record %u (%s)\n",c.recreate?"send again":"upload",received::name(c.type),c.online.empty()?"":" ",c.online.c_str(),c.id,why(how));
    out.push_back(b);
    const int q=calls.queued?calls.queued(c.object,c.recreate):-1;
    if(q==0){++k_.unqueued;out.push_back(finish(c,"the game did not queue it",now));return out;}
    current_=c;next_=now+PollMs;
    return out;
}
#ifdef _WIN32
std::function<bool(uint64_t,bool)> nativeAsk(const discovery::Sampler& s){
    auto why=verifyCode(s);if(!why.empty())throw std::runtime_error("offer strand: "+why);
    using UploadFn=void(*)(uint64_t);using RecreateFn=void(*)(uint64_t,uint64_t);
    auto upload=reinterpret_cast<UploadFn>(s.base+Sites{}.upload);auto recreate=reinterpret_cast<RecreateFn>(s.base+Sites{}.recreate);
    return [&s,upload,recreate](uint64_t object,bool again){
        const uint64_t u=uploader(s);if(!u||!object)return false;
        if(again)recreate(u,object);else upload(object);
        return true;};
}
#endif
}
