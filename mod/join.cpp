#include "join.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
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
