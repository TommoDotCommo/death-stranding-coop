#include "join.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace mod::join {
std::optional<Plan> Decision::tick(const Input& in) {
#ifndef MUTANT_JOIN_ONCE
    if(finished_)return {};
#endif
    #ifndef MUTANT_JOIN_CONNECTION
    if(!first_&&in.connected&&in.connection!=1){finished_=true;return {};}
#endif
    if(!first_&&in.connected)first_=in.connection;
#ifndef MUTANT_JOIN_CONNECTION
    if(first_&&(!in.connected||in.connection!=first_)){finished_=true;return {};}
#endif
#ifndef MUTANT_JOIN_ROLE
    if(!in.enabled||in.role!=Role::Guest)return {};
#endif
#ifndef MUTANT_JOIN_THREAD
    if(!in.gameThread)return {};
#endif
    if(!in.connected||!first_)return {};
#ifndef MUTANT_JOIN_GAMEPLAY
    if(!in.localGameplay||!in.host.visible||in.host.stale||((in.local.flags|in.host.state.flags)&(ds::PrivateRoom|ds::Vehicle)))return {};
#endif
    auto local=in.local,host=in.host.state;
    // Validate, do not clamp a bogus pose into an eligible plan.
    if(!ds::sanitise(local)||!ds::sanitise(host)||local.position!=in.local.position||host.position!=in.host.state.position)return {};
    double distance=0;for(unsigned i=0;i<3;++i)distance+=std::pow(local.position[i]-host.position[i],2);
    distance=std::sqrt(distance);finished_=true; // the first eligible observation consumes the opportunity, even nearby
#ifndef MUTANT_JOIN_DISTANCE
    if(distance<=300)return {};
#endif
    Plan plan;plan.position=host.position;plan.yaw=host.yaw;plan.distance=distance;
#ifndef MUTANT_JOIN_SIDE
    plan.position[0]+=2*std::cos(host.yaw);plan.position[2]-=2*std::sin(host.yaw);
#else
    plan.position[1]+=2;
#endif
    return plan;
}
std::string wouldMove(const Plan& p) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<"JOIN would move local porter to "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]
        <<" (adapter axes, distance "<<p.distance<<" m); native teleport unavailable; no move performed\n";
    return out.str();
}
std::string willMove(const Plan& p) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<"JOIN moving local porter to "<<p.position[0]<<","<<p.position[1]<<","<<p.position[2]
        <<" (adapter axes, distance "<<p.distance<<" m) with the game's player teleport\n";
    return out.str();
}
std::optional<MoveMode> parseMoveMode(const std::string& v) {
    if(v=="log")return MoveMode::Log;
    if(v=="native")return MoveMode::Native;
    return std::nullopt;
}
namespace {
std::string fixed(double v){std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<v;return o.str();}
double metres(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(unsigned i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
bool finite(const Porter& p){for(auto x:p.position)if(!std::isfinite(x))return false;for(auto x:p.rotation)if(!std::isfinite(x))return false;return true;}
}
std::string Mover::start(const Plan& plan,uint64_t now) {
#ifndef MUTANT_MOVE_ONCE
    if(stage_!=Stage::Ready)return {};
#endif
    stage_=Stage::Refused; // whatever happens below, this session's one opportunity is consumed
    auto refuse=[](const std::string& why){return "JOIN native move refused: "+why+"; no move performed\n";};
    const auto& p=plan.position;
    bool known=std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);
#ifndef MUTANT_MOVE_BOUNDS
    if(!known||std::abs(p[0])>bounds_.horizontal||std::abs(p[2])>bounds_.horizontal||p[1]<bounds_.low||p[1]>bounds_.high)
        return refuse("target outside the world sanity box");
#else
    (void)known;
#endif
    if(!api_.porter||!api_.gameplay||!api_.riding||!api_.teleport)return refuse("native API incomplete");
#ifndef MUTANT_MOVE_EVENT
    if(!api_.gameplay())return refuse("not in normal gameplay (event, cutscene, private room or loading)");
#endif
    auto riding=api_.riding();
#ifndef MUTANT_MOVE_VEHICLE
    if(!riding)return refuse("vehicle state unknown");
    if(*riding)return refuse("porter is on a vehicle");
#else
    (void)riding;
#endif
    auto before=api_.porter();
    if(!before.readable||!before.player||!before.entity||!finite(before))return refuse("local porter unreadable");
    // The game's own world-map teleport keeps the porter's rotation and replaces the position; so does this.
    puppet::Transform goal;goal.position={p[0],p[2],p[1]};goal.rotation=before.rotation; // adapter -> game axes
    player_=before.player;entity_=before.entity;target_=goal.position;
    api_.teleport(before.player,goal);
    std::string line="JOIN native move called: PlayerGame teleport mode 2 from "+fixed(before.position[0])+","+fixed(before.position[1])+","+fixed(before.position[2])
        +" to "+fixed(goal.position[0])+","+fixed(goal.position[1])+","+fixed(goal.position[2])+" (game axes, distance "+fixed(metres(before.position,goal.position))+" m)\n";
#ifndef MUTANT_MOVE_READBACK
    auto after=api_.porter();
    if(!after.readable||after.player!=player_||after.entity!=entity_||!finite(after)){stage_=Stage::Failed;return line+"JOIN readback failed: porter unreadable or replaced after the move; stopping\n";}
    double off=metres(after.position,target_);
    if(off>ArriveMetres){stage_=Stage::Failed;return line+"JOIN readback failed: porter "+fixed(off)+" m from the target (limit 5 m); stopping\n";}
    arrived_=after.position;
#else
    double off=0;arrived_=target_;
#endif
    stage_=Stage::Settling;settleAt_=now+SettleMs;
    return line+"JOIN moved local porter: readback "+fixed(off)+" m from the target\n";
}
std::string Mover::tick(uint64_t now) {
    if(stage_!=Stage::Settling||now<settleAt_)return {};
    stage_=Stage::Done;
    auto p=api_.porter?api_.porter():Porter{};
    if(!p.readable||p.player!=player_||p.entity!=entity_||!finite(p))return "JOIN settle check: porter unreadable or replaced (load or death?)\n";
    double drop=arrived_[2]-p.position[2];
    if(drop>ArriveMetres){stage_=Stage::Failed;return "JOIN settle check failed: porter dropped "+fixed(drop)+" m after the move (ground not streamed in?)\n";}
    return "JOIN settle check: porter holding, "+fixed(metres(p.position,target_))+" m from the target after "+std::to_string(SettleMs/1000)+" s\n";
}
namespace {
// Pinned prefixes (40 bytes, each unique in the pinned image's code): see game_offsets.md, "Moving the porter".
const unsigned char PlayerTeleportCode[40]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x1d,0xc1,0x92,0x02,0x48,
    0x33,0xc4,0x48,0x89,0x44,0x24,0x38,0x48,0x8b,0x79,0x70,0x41,0x8b,0xd8,0x48,0x8b,0xea,0x48,0x8b,0xf1};
const unsigned char EntityTeleportCode[40]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55,0x57,0x41,0x56,0x48,0x8d,0xa8,0x48,0xff,
    0xff,0xff,0x48,0x81,0xec,0xa0,0x01,0x00,0x00,0xc5,0xf8,0x29,0x70,0xd8,0xc5,0xf8,0x29,0x78,0xc8,0xc5};
const unsigned char PlaceOnGroundCode[40]={0x40,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x10,0x02,0x00,0x00,0x48,0x8b,0x05,0xea,0x96,
    0x82,0x01,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x00,0x02,0x00,0x00,0xc5,0xf8,0x10,0x15,0x07,0x0a,0x48};
bool slot(const discovery::Sampler& s,uint64_t at,uint64_t& v){return s.read(at,&v,8);}
}
std::string verifyMoveCode(const discovery::Sampler& s,const MoveSites& m) {
    struct Want{uint64_t rva;const unsigned char* bytes;const char* name;};
    const Want wants[]={{m.playerTeleport,PlayerTeleportCode,"PlayerGame teleport"},{m.entityTeleport,EntityTeleportCode,"entity teleport"},{m.placeOnGround,PlaceOnGroundCode,"porter ground placement"}};
    unsigned counts[3]={};
    for(auto& section:s.sections)if(section.executable){
        if(section.size>128ull*1024*1024)return "code section too large";
        std::vector<unsigned char> code(section.size);if(!s.read(section.address,code.data(),code.size()))return "code unreadable";
        for(unsigned k=0;k<3;++k)for(auto at=code.begin();(at=std::search(at,code.end(),wants[k].bytes,wants[k].bytes+40))!=code.end();++at){
            ++counts[k];
#ifndef MUTANT_MOVE_SIGNATURE
            if(section.address+uint64_t(at-code.begin())!=s.base+wants[k].rva)return std::string(wants[k].name)+" signature moved or duplicated";
#endif
        }
    }
    for(unsigned k=0;k<3;++k)if(counts[k]!=1)return std::string(wants[k].name)+" signature missing or ambiguous";
    // The override calls the entity teleport directly (mode 2 path).
    unsigned char call[5]={};
    if(!s.read(s.base+m.playerTeleport+m.callAt,call,5)||call[0]!=0xe8)return "PlayerGame teleport call site differs";
    int32_t rel=0;std::memcpy(&rel,call+1,4);
    if(s.base+m.playerTeleport+m.callAt+5+int64_t(rel)!=s.base+m.entityTeleport)return "PlayerGame teleport calls something else";
    uint64_t v=0;
    if(!slot(s,s.base+m.playerVtable,v)||v!=s.base+m.playerType||!slot(s,s.base+m.playerVtable+m.playerSlot,v)||v!=s.base+m.playerTeleport)return "PlayerGame vtable differs";
    if(!slot(s,s.base+m.entityVtable,v)||v!=s.base+m.entityType||!slot(s,s.base+m.entityVtable+m.entitySlot,v)||v!=s.base+m.placeOnGround)return "DSPlayerEntity vtable differs";
    return {};
}
Porter readPorter(const discovery::Sampler& s) {
    Porter p;
    try {
        if(s.signatures.size()!=6)return p;
        auto managerSlot=s.signatures.at(0).targets.at("player_manager_global");
        uint64_t manager=0,entity=0;if(!slot(s,managerSlot,manager)||!manager)return p;
        auto selected=s.selectPlayer(manager);if(!selected.player)return p;
        if(!slot(s,selected.player+0x70,entity)||!entity||entity>=0x0000800000000000ull)return p;
        Porter a,b;
        for(Porter* r:{&a,&b})if(!s.read(entity+0xc8,r->position.data(),24)||!s.read(entity+0xe0,r->rotation.data(),36))return p;
        uint64_t manager2=0,player2=0,entity2=0;
        if(!slot(s,managerSlot,manager2)||manager2!=manager||!slot(s,selected.slot,player2)||player2!=selected.player
           ||!slot(s,selected.player+0x70,entity2)||entity2!=entity||a.position!=b.position||a.rotation!=b.rotation)return p;
        p=a;p.player=selected.player;p.entity=entity;p.readable=finite(p);
        if(!p.readable)p.player=p.entity=0;
    } catch(const std::exception&) {return Porter{};}
    return p;
}
bool movableIdentity(const discovery::Sampler& s,uint64_t player,uint64_t entity,const MoveSites& m) {
    uint64_t v=0,e=0;
    if(!player||player>=0x0000800000000000ull||!entity||entity>=0x0000800000000000ull)return false;
#ifndef MUTANT_MOVE_IDENTITY
    if(!slot(s,player,v)||v!=s.base+m.playerVtable||!slot(s,player+0x70,e)||e!=entity||!slot(s,entity,v)||v!=s.base+m.entityVtable)return false;
#endif
    return true;
}
#ifdef _WIN32
std::function<void(uint64_t,const puppet::Transform&)> nativeTeleport(const discovery::Sampler& s) {
    auto why=verifyMoveCode(s);if(!why.empty())throw std::runtime_error("join move: "+why);
    // PlayerGame::vfunc(+0xa8)(player, const WorldTransform*, int mode): MsgDismount to the entity, then the entity
    // teleport with ground placement (mode 2), as the game's world-map debug teleport calls it.
    using TeleportFn=void(*)(uint64_t,const puppet::Transform*,int,int);
    auto teleport=reinterpret_cast<TeleportFn>(s.base+MoveSites{}.playerTeleport);
    return [&s,teleport](uint64_t player,const puppet::Transform& t){
        uint64_t entity=0;
        if(!slot(s,player+0x70,entity)||!movableIdentity(s,player,entity))throw std::runtime_error("join move: player identity changed");
        alignas(16) puppet::Transform copy=t;teleport(player,&copy,2,0);
    };
}
#endif
namespace {
using UUID=std::array<unsigned char,16>;
uint32_t hash(const UUID& id) { // x86 CRC32 instruction, seed zero, no final XOR
    uint32_t crc=0;for(auto b:id){crc^=b;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0x82f63b78u:0);}return crc;
}
template<class T> T load(const unsigned char* p) {T v;std::memcpy(&v,p,sizeof v);return v;}
struct ReadFailure {};
class Observation {
    const discovery::Sampler& s_;
    struct Span {uint64_t at;std::vector<unsigned char> data;};
    std::vector<Span> reads_;
public:
    explicit Observation(const discovery::Sampler& s):s_(s) {}
    std::vector<unsigned char> bytes(uint64_t at,size_t n) {
        std::vector<unsigned char> data(n);
        if(!s_.read(at,data.data(),n))throw ReadFailure{};
        reads_.push_back({at,data});return data;
    }
    template<class T> T get(uint64_t at) {auto b=bytes(at,sizeof(T));return load<T>(b.data());}
    uint64_t pointer(uint64_t at) {
        auto p=get<uint64_t>(at);if(!p||p>=0x0000800000000000ull)throw ReadFailure{};return p;
    }
    bool coherent() const {
#ifndef MUTANT_PROGRESS_COHERENT
        for(const auto& r:reads_){std::vector<unsigned char> b(r.data.size());if(!s_.read(r.at,b.data(),b.size())||b!=r.data)return false;}
#endif
        return true;
    }
    // The native tables are open-addressed. A bounded complete copy avoids reproducing their mutation/probe logic.
    std::optional<std::vector<unsigned char>> row(uint64_t header,size_t stride,const UUID& key) {
        auto h=bytes(header,16);auto table=load<uint64_t>(h.data());auto capacity=load<uint32_t>(h.data()+12);
#ifndef MUTANT_PROGRESS_BOUNDS
        if(!table||table>=0x0000800000000000ull||!capacity||capacity>4096||(capacity&(capacity-1)))throw ReadFailure{};
#else
        if(!capacity||capacity>8192)throw ReadFailure{};
#endif
        auto rows=bytes(table,capacity*stride);std::optional<std::vector<unsigned char>> match;
        const size_t keyAt=stride==32?8:4;
        for(unsigned i=0;i<capacity;++i){auto p=rows.data()+i*stride;
#ifndef MUTANT_PROGRESS_KEY
            if(load<uint32_t>(p)!=(hash(key)|0x80000000u)||std::memcmp(p+keyAt,key.data(),16))continue;
#else
            if(!load<uint32_t>(p))continue;
#endif
#ifndef MUTANT_PROGRESS_UNIQUE
            if(match)throw ReadFailure{};
#endif
            match=std::vector<unsigned char>(p,p+stride);
        }
        return match;
    }
};
}
ProgressObservation ProgressReader::read() const {
    ProgressObservation result;auto& s=memory_;const auto& p=sites_;
    try {
#ifndef MUTANT_PROGRESS_IDENTITY
        // Hash/section validation was performed by Sampler::scan; reject unvalidated/foreign images.
        if(s.signatures.size()!=6||!s.region(s.base+p.missionSlot,8,true)||!s.region(s.base+p.factsSlot,8,true)||!s.region(s.base+p.scope,16,true))throw ReadFailure{};
        unsigned char code[8];const unsigned char missionPrefix[]={0x40,0x53,0x48,0x83,0xec,0x20};
        const unsigned char integer[]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57};
        if(!s.read(s.base+p.missionCode,code,6)||std::memcmp(code,missionPrefix,6)||!s.read(s.base+p.integerCode,code,8)||std::memcmp(code,integer,8))throw ReadFailure{};
#endif
        Observation o(s);
        auto mission=o.pointer(s.base+p.missionSlot),owner=o.pointer(mission+0x28),goals=o.pointer(owner+0x1f0);
        auto db=o.pointer(s.base+p.factsSlot);UUID scope{};auto raw=o.bytes(s.base+p.scope,16);std::copy(raw.begin(),raw.end(),scope.begin());
        auto world=o.row(db+0x20,32,scope);if(!world)throw ReadFailure{};
        auto values=load<uint64_t>(world->data()+24);if(!values||values>=0x0000800000000000ull)throw ReadFailure{};
        auto fact=o.pointer(goals+0x68);UUID key{};raw=o.bytes(fact+8,16);std::copy(raw.begin(),raw.end(),key.begin());
        auto current=o.row(values+0x50,24,key);if(!current)throw ReadFailure{};
        result.episodeHash=load<uint32_t>(current->data()+20);
#ifndef MUTANT_PROGRESS_ZERO
        if(!result.episodeHash)throw ReadFailure{}; // ClearEpisode writes zero; never infer an active episode from it.
#endif
        // SetEpisode (335f3a0) writes CRC32(UUID) for one of this resource's Episodes entries.
        auto count=o.get<int32_t>(goals+0x40);if(count<1||count>64)throw ReadFailure{};
        auto episodes=o.pointer(goals+0x48);raw=o.bytes(episodes,size_t(count)*8);bool matched=false;
        for(int32_t i=0;i<count;++i){auto resource=load<uint64_t>(raw.data()+8*i);if(!resource||resource>=0x0000800000000000ull)throw ReadFailure{};
            UUID uuid{};auto b=o.bytes(resource+8,16);std::copy(b.begin(),b.end(),uuid.begin());
            if(hash(uuid)!=result.episodeHash)continue;
            if(matched)throw ReadFailure{};
            matched=true;result.value.episode=uint32_t(i);
        }
        if(!matched)throw ReadFailure{};
        if(!o.coherent())throw ReadFailure{};
        result.reason="episode resource index (UI numbering unverified); main-order unknown";
        return result;
    } catch(const ReadFailure&) {return {};}
}
}
