#include "marker.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mod::marker {
namespace {
constexpr uint64_t BlockOffset=0x5ef8;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string where(const std::array<float,3>& p){std::ostringstream o;o<<p[0]<<","<<p[1]<<","<<p[2];return o.str();}
bool sameXY(const std::array<float,3>& a,const std::array<float,3>& b){return a[0]==b[0]&&a[1]==b[1];}
// The fingerprint: a keyed hash of the three coordinates' bit patterns with its top FingerprintBits bits zero.
constexpr unsigned FingerprintBits=18;
bool marked(const std::array<float,3>& p){
    uint32_t b[3];std::memcpy(b,p.data(),12);
    uint64_t h=(uint64_t(b[0])|uint64_t(b[1])<<32)^(uint64_t(b[2])*0x9e3779b97f4a7c15ull)^0x6d61726b65720a01ull;   // splitmix64
    h+=0x9e3779b97f4a7c15ull;h=(h^(h>>30))*0xbf58476d1ce4e5b9ull;h=(h^(h>>27))*0x94d049bb133111ebull;h^=h>>31;
    return (h>>(64-FingerprintBits))==0;
}
}
std::vector<Record> readRecords(const discovery::Sampler& s,uint64_t managerSlot){
    std::vector<Record> out;uint64_t manager=0,block=0;
    if(!managerSlot||!s.read(managerSlot,&manager,8)||!manager||!s.read(manager+BlockOffset,&block,8)||!block)return out;
    std::vector<uint8_t> raw(Records*RecordSize);
    if(!s.read(block,raw.data(),raw.size()))return out;
    for(unsigned i=0;i<Records;++i){
        const uint8_t* r=raw.data()+i*RecordSize;
        if(r[0x2d]!=UserMarkerType)return {};   // not the user-marker block: refuse the whole read
        Record m;m.index=i;m.used=r[0x2c]&1;std::memcpy(m.position.data(),r+0x10,12);std::memcpy(&m.id,r+0x20,8);
        m.group=r[0x4c];m.point=r[0x4d];
        if(m.used&&(m.group>=Groups||m.point>=Points))return {};
        out.push_back(m);
    }
    return out;
}
std::array<float,3> stamp(const std::array<double,3>& p){
    // Nudge the coordinates up by float steps until the fingerprint holds: about 2^18 tries on average (a few
    // milliseconds, once a second at most). Height varies fastest (up to 256 steps, under 3 cm below 2 km), x and y
    // slowest (up to 128 steps each, usually a few dozen: centimetres even at the map's largest coordinates).
    std::array<float,256> c[3];
    for(int a=0;a<3;++a){float v=float(p[a]);for(auto& e:c[a]){e=v;v=std::nextafter(v,HUGE_VALF);}}
    for(unsigned n=0;n<128*128;++n){
        unsigned x=0,y=0;   // walk x and y outward in squares, so the smallest nudge is found first
        unsigned ring=unsigned(std::sqrt(double(n)));while(ring*ring>n)--ring;unsigned k=n-ring*ring;
        if(k<=ring){x=ring;y=k;}else{x=k-ring-1;y=ring;}
        for(unsigned z=0;z<256;++z){std::array<float,3> q{c[0][x],c[1][y],c[2][z]};if(marked(q))return q;}
    }
    return {float(p[0]),float(p[1]),float(p[2])};   // not found (2^22 tries): placed without a fingerprint
}
bool stamped(const std::array<float,3>& p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&marked(p);}

bool KeyEdge::pressed(bool down,uint64_t now){
    bool edge=down&&!was_;was_=down;
    if(!edge||(fired_&&now-last_<Cooldown))return false;
    fired_=true;last_=now;return true;
}

Markers::Markers(Api api,std::function<void(const std::string&)> log,uint8_t partnerGroup,uint8_t pingGroup)
    :api_(std::move(api)),log_(std::move(log)){
    require(partnerGroup<Groups&&pingGroup<Groups&&partnerGroup!=pingGroup,"marker groups must be distinct, 0..4");
    partner_.group=partnerGroup;ping_.group=pingGroup;
}
void Markers::enable(bool on){
    if(on==enabled_)return;
    if(!on)clear();
    enabled_=on;swept_=false;takenNoted_=false;
    log_(on?"MARKER on\n":"MARKER off\n");
}
// Ours: the record the mod set last in this slot (by id once seen, by its stamped place while the set is fresh).
static bool ours(bool placed,bool pending,uint64_t id,const std::array<float,3>& at,uint8_t group,const Record& r){
    if(!r.used||r.group!=group)return false;
    return (placed&&r.id==id)||(pending&&r.point==0&&sameXY(r.position,at));
}
void Markers::sync(Slot& s,const std::vector<Record>& records){
    if(!s.placed&&!s.pending)return;
    auto it=std::find_if(records.begin(),records.end(),[&](const Record& r){return ours(s.placed,s.pending,s.id,s.at,s.group,r);});
    if(it!=records.end()){s.placed=true;s.pending=false;s.id=it->id;s.index=it->index;return;}
    if(s.pending)log_("MARKER the game did not take a marker in group "+std::to_string(s.group)+"\n");
    else log_("MARKER a marker in group "+std::to_string(s.group)+" is gone (removed by the game or the player)\n");
    s.placed=s.pending=false;s.id=0;
}
bool Markers::foreign(const Slot& s,const std::vector<Record>& records) const {
#ifdef MUTANT_MARKER_FOREIGN
    (void)s;(void)records;return false;
#else
    // A player's pin or route point in this group: the group is the player's; never write to it.
    return std::any_of(records.begin(),records.end(),[&](const Record& r){
        return r.used&&r.group==s.group&&!stamped(r.position)&&!ours(s.placed,s.pending,s.id,s.at,s.group,r);});
#endif
}
void Markers::place(Slot& s,const std::array<double,3>& goal,uint64_t now){
    s.at=stamp(goal);s.goal=goal;api_.set(s.at,s.group,0);s.setAt=now;s.everSet=true;s.pending=true;s.placed=false;s.id=0;++sets_;
}
void Markers::drop(Slot& s,const std::vector<Record>& records){
    for(auto& r:records)if(ours(s.placed,s.pending,s.id,s.at,s.group,r)){api_.remove(r.index);++removals_;break;}
    s.placed=s.pending=false;s.id=0;
}
void Markers::tick(uint64_t now,const std::optional<std::array<double,3>>& partner,bool hidden,const std::optional<std::array<double,3>>& local){
    if(!enabled_)return;
    auto records=api_.read();if(records.empty())return;   // no manager (title screen, loading): nothing to do
    sync(partner_,records);sync(ping_,records);
    // A stamped marker in our groups that is not the one we hold: left over from an earlier session (saved and
    // reloaded). Removed; a player's pin is never stamped.
    bool removed=false;
    for(auto& r:records)
        if(r.used&&(r.group==partner_.group||r.group==ping_.group)&&stamped(r.position)
           &&!ours(partner_.placed,partner_.pending,partner_.id,partner_.at,partner_.group,r)&&!ours(ping_.placed,ping_.pending,ping_.id,ping_.at,ping_.group,r)){
            api_.remove(r.index);++removals_;removed=true;
            log_("MARKER removed a leftover mod marker in group "+std::to_string(r.group)+" at "+where(r.position)+"\n");}
    if(removed)return;   // read again next tick (a removal can close up a route)
    // The partner: shown while he is in this world and this porter is not hidden; moved at most once a second.
    bool near=false;
#ifndef MUTANT_MARKER_NEAR
    if(partner&&local)near=std::hypot((*partner)[0]-(*local)[0],(*partner)[1]-(*local)[1],(*partner)[2]-(*local)[2])<Near;
#endif
    bool want=partner&&!hidden&&!near;
#ifdef MUTANT_MARKER_LEFT
    want=true;if(!partner)return;   // never takes the marker away
#endif
    if(!want){if(partner_.placed||partner_.pending){drop(partner_,records);log_(hidden?"MARKER partner marker removed (event or private room here)\n":near?"MARKER partner marker removed (partner near)\n":"MARKER partner marker removed (partner gone)\n");}}
    else if(foreign(partner_,records)){
        if(!takenNoted_)log_("MARKER group "+std::to_string(partner_.group)+" holds the player's markers: partner marker not shown\n");
        takenNoted_=true;}
    else {
        takenNoted_=false;
        bool shown=partner_.placed||partner_.pending;
        double moved=std::hypot((*partner)[0]-partner_.goal[0],(*partner)[1]-partner_.goal[1],(*partner)[2]-partner_.goal[2]);
#ifdef MUTANT_MARKER_RATE
        bool due=true;(void)moved;
#else
        bool due=!shown?(!partner_.everSet||now-partner_.setAt>=Refresh):(now-partner_.setAt>=Refresh&&moved>Moved);
#endif
        if(due){place(partner_,*partner,now);if(!shown)log_("MARKER partner marker at "+where(partner_.at)+"\n");}
    }
    // Pings: one at a time (a new one replaces the last), gone after PingLife, never shown while hidden.
    if((ping_.placed||ping_.pending)&&(hidden||now-pingAt_>=PingLife)){drop(ping_,records);log_("MARKER ping removed\n");}
    if(pendingPing_&&now-pingAt_>=PingLife)pendingPing_.reset();
    if(pendingPing_&&!hidden&&!foreign(ping_,records)){
        place(ping_,pendingPing_->position,now);
        std::ostringstream o;o<<"MARKER ping "<<pendingPing_->ref<<" at "<<where(ping_.at)<<"\n";log_(o.str());pendingPing_.reset();
    }
}
void Markers::partnerPing(const Ping& p,uint64_t now){
    if(!enabled_){log_("MARKER partner ping ignored (off)\n");return;}
#ifndef MUTANT_MARKER_TWICE
    if(std::find(seen_.begin(),seen_.end(),p.ref)!=seen_.end()){++duplicates_;log_("MARKER partner ping "+std::to_string(p.ref)+" already shown: ignored\n");return;}
#endif
    seen_.push_back(p.ref);while(seen_.size()>256)seen_.pop_front();
    pendingPing_=p;pingAt_=now;
}
std::optional<Ping> Markers::localPing(const std::array<double,3>& at,uint64_t now){
    if(!enabled_)return std::nullopt;
    Ping p;p.ref=(++nextRef_)&((uint64_t(1)<<53)-1);if(!p.ref)p.ref=nextRef_=1;p.position=at;
    seen_.push_back(p.ref);while(seen_.size()>256)seen_.pop_front();
    pendingPing_=p;pingAt_=now;return p;
}
void Markers::clear(){
    pendingPing_.reset();
    if(!partner_.placed&&!partner_.pending&&!ping_.placed&&!ping_.pending)return;
    auto records=api_.read();
    drop(partner_,records);drop(ping_,records);log_("MARKER cleared\n");
}

bool keyDown(unsigned vk){
#ifdef _WIN32
    if(!vk||vk>0xfe)return false;
    HWND front=GetForegroundWindow();DWORD pid=0;if(!front)return false;
    GetWindowThreadProcessId(front,&pid);if(pid!=GetCurrentProcessId())return false;
    return (GetAsyncKeyState(int(vk))&0x8000)!=0;
#else
    (void)vk;return false;
#endif
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native marker API requires Windows");
#else
    auto unique=[&](const std::vector<uint8_t>& sig,uint64_t rva,const char* what){
        unsigned count=0;
        for(auto section:s.sections)if(section.executable){
            std::vector<uint8_t> code(section.size);require(s.read(section.address,code.data(),code.size()),"marker code unreadable");
            for(auto at=code.begin();(at=std::search(at,code.end(),sig.begin(),sig.end()))!=code.end();++at){
                ++count;require(section.address+size_t(at-code.begin())==s.base+rva,what);}
        }
        require(count==1,what);
    };
    // set (the map menu's own wrapper: it adds the floor value the game uses) and remove one record (game_offsets.md).
    unique({0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xe9,
            0x41,0x8b,0xd9,0x48,0x8b},0x2f22c90,"marker set signature missing/moved/ambiguous");
    unique({0x48,0x85,0xd2,0x0f,0x84,0x5a,0x02,0x00,0x00,0x48,0x89,0x5c,0x24,0x20,0x41,0x56},0x2f378b0,"marker remove signature missing/moved/ambiguous");
    using SetFn=void(*)(void*,const float*,uint32_t,uint32_t,uint8_t,uint8_t);
    using RemoveFn=void(*)(void*,void*);
    auto set=reinterpret_cast<SetFn>(s.base+0x2f22c90);auto remove=reinterpret_cast<RemoveFn>(s.base+0x2f378b0);
    uint64_t slot=s.base+0x7bfd940;auto* sp=&s;
    Api api;
    api.read=[sp,slot]{return readRecords(*sp,slot);};
    api.set=[sp,slot,set](const std::array<float,3>& p,uint8_t group,uint8_t point){
        uint64_t manager=0;if(!sp->read(slot,&manager,8)||!manager)return;
        alignas(16) float at[4]{p[0],p[1],p[2],0};set(reinterpret_cast<void*>(manager),at,group,point,0,0);};   // no ground snap: exact height
    api.remove=[sp,slot,remove](uint32_t index){
        uint64_t manager=0,block=0;if(index>=Records||!sp->read(slot,&manager,8)||!manager||!sp->read(manager+BlockOffset,&block,8)||!block)return;
        remove(reinterpret_cast<void*>(manager),reinterpret_cast<void*>(block+uint64_t(index)*RecordSize));};
    return api;
#endif
}
}
