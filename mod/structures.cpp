#include "structures.h"
#include <cmath>
#include <cstring>
namespace mod::structures {
namespace {
constexpr double Pi=3.14159265358979323846;
using M3=std::array<std::array<double,3>,3>;
M3 mul(const M3& a,const M3& b){M3 r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)r[i][j]+=a[i][k]*b[k][j];return r;}
uint32_t quantize(double a){double t=std::remainder(a,2*Pi)/(2*Pi)+.5;return uint32_t(std::lround(t*4096))&0xfff;}   // [-pi,pi) -> 12 bits
double dequantize(uint32_t q){return (double(q&0xfff)/4096-.5)*2*Pi;}
}
std::vector<Record> readTable(const discovery::Sampler& s,uint64_t manager,uint64_t base,uint64_t imageSize){
    std::vector<Record> out;if(!manager)return out;
    // The whole table in one guarded copy (readOnce, as the cargo records): 640 double-checked record reads were
    // two system calls each, every 500 ms on the game thread. Structures change rarely and the sync waits for them
    // to stand, so a record caught mid-change is harmless. Falls back to record by record if the copy fails.
    std::vector<uint8_t> table(size_t(MaxRecords)*Stride);
    bool whole=s.readOnce(manager+TableEnd-uint64_t(MaxRecords)*Stride,table.data(),table.size());
    std::vector<uint8_t> block(0x390);
    for(unsigned i=1;i<=MaxRecords;++i){
        uint64_t at=manager+TableEnd-i*Stride;
        if(whole)std::memcpy(block.data(),table.data()+size_t(MaxRecords-i)*Stride,block.size());
        else if(!s.read(at,block.data(),block.size()))break;
        uint64_t q0,q1,q2;std::memcpy(&q0,block.data(),8);std::memcpy(&q1,block.data()+8,8);std::memcpy(&q2,block.data()+16,8);
        // A record starts with two of its class's image pointers (+0, +10 = +0 + 0x10). +8 is usually 0, but a structure
        // whose menu the player opened keeps a pointer there afterwards (live 2026-11-14): requiring 0 lost the record, and
        // the sync took the structure for dismantled.
#ifndef MUTANT_STRUCTURE_MENU
        (void)q1;if(!(q0>=base&&q0<base+imageSize&&q2==q0+0x10))continue;
#else
        if(!(q0>=base&&q0<base+imageSize&&q1==0&&q2==q0+0x10))continue;
#endif
        Record r;std::memcpy(&r.id,block.data()+0x18,4);r.type=block[0x1c];r.category=block[0x1d];r.state=block[0x1e];
        std::memcpy(r.at.position.data(),block.data()+0x28,24);std::memcpy(r.at.rotation.data(),block.data()+0x40,36);
        std::memcpy(&r.built,block.data()+0x388,8);std::memcpy(&r.level,block.data()+0x6c,4);r.object=at-0x18;std::memcpy(r.own.data(),block.data()+0x1f8,24);
        if(r.id)out.push_back(r);
    }
    return out;
}
std::optional<uint8_t> netVariant(uint8_t t){
    // Stage/Player/Net triples (EDSConstructionPointType): safe house 2-4, post 5-7, watchtower 8-10, charger 14-16,
    // rain shelter 17-19, zipline 21-23, ladder 24-26, climbing rope 27-29, bridges 30-32 and 33-35.
    for(uint8_t p:{3,6,9,15,18,22,25,28,31,34})if(t==p)return uint8_t(t+1);
    return std::nullopt;
}
std::array<float,9> rotation(double yaw,double pitch,double roll){
    double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
    M3 z{{{cy,-sy,0},{sy,cy,0},{0,0,1}}},x{{{1,0,0},{0,cp,-sp},{0,sp,cp}}},y{{{cr,0,sr},{0,1,0},{-sr,0,cr}}};
    auto m=mul(mul(z,x),y);std::array<float,9> r{};
    for(int row=0;row<3;++row)for(int k=0;k<3;++k)r[row*3+k]=float(m[k][row]);   // rows: images of right, forward, up
    return r;
}
void angles(const std::array<float,9>& r,double& yaw,double& pitch,double& roll){
    double fx=r[3],fy=r[4],fz=r[5];
    pitch=std::asin(std::fmax(-1.,std::fmin(1.,fz)));yaw=std::atan2(-fx,fy);roll=std::atan2(-double(r[2]),double(r[8]));
}
uint32_t pack(const Built& b){
#ifndef MUTANT_STRUCTURE_PACK
    return uint32_t(b.type)|quantize(b.pitch)<<8|quantize(b.roll)<<20;
#else
    return b.type;
#endif
}
Built unpack(uint32_t item,const std::array<double,3>& position,double yaw){
    Built b;b.type=uint8_t(item&0xff);b.pitch=dequantize(item>>8);b.roll=dequantize(item>>20);b.position=position;b.yaw=yaw;return b;
}
// A structure's record is in state 2 while the player is near it and falls back to 1 when he walks away (4 while
// its object is being made); 0 is a request the game has not built. A structure out of sight still stands.
static bool present(uint8_t state){
#ifndef MUTANT_STRUCTURE_FAR
    return state==1||state==2||state==4;
#else
    return state==2;
#endif
}
std::vector<Built> Sync::everything(const std::vector<Record>& records){
    std::vector<Built> out;
    for(auto& r:records){
#ifndef MUTANT_STRUCTURE_ALL_NET
        if(r.id<PlayerFirst||r.id>=NetFirst||!present(r.state)||!netVariant(r.type))continue;
#else
        if(r.id<PlayerFirst||!present(r.state)||!netVariant(r.type))continue;
#endif
        announced_[r.id]=r.built;
        Built b;b.type=r.type;b.position=r.at.position;angles(r.at.rotation,b.yaw,b.pitch,b.roll);b.id=r.id;out.push_back(b);
    }
    return out;
}
std::vector<Built> Sync::observe(const std::vector<Record>& records){
    std::vector<Built> out;std::map<uint32_t,uint8_t> next;
    for(auto& r:records){
        next[r.id]=r.state;
#ifndef MUTANT_STRUCTURE_NET
        bool mine=r.id>=PlayerFirst&&r.id<NetFirst;
#else
        bool mine=r.id>=PlayerFirst;   // would send the partner's structures back to them
#endif
        if(!mine||!present(r.state)||!netVariant(r.type))continue;
        standing_[r.id]={r.type,r.at.position,0};
#ifndef MUTANT_STRUCTURE_OLD
        if(r.built<since_)continue;   // of the save: the game brings those in late after a load, already built
#endif
        auto told=announced_.find(r.id);
        if(told!=announced_.end()&&told->second==r.built)continue;
        announced_[r.id]=r.built;
        Built b;b.type=r.type;b.position=r.at.position;angles(r.at.rotation,b.yaw,b.pitch,b.roll);b.id=r.id;out.push_back(b);
    }
#ifndef MUTANT_STRUCTURE_REMOVED
    for(auto it=standing_.begin();it!=standing_.end();){
        auto now=next.find(it->first);
        if(now!=next.end()&&present(now->second)){++it;continue;}
#ifndef MUTANT_STRUCTURE_HASTY
        if(++it->second.missing<RemovedLooks){++it;continue;}
#endif
        removed_.push_back({it->first,it->second.type,it->second.position});it=standing_.erase(it);
    }
#endif
    seen_.swap(next);
    return out;
}
std::vector<Sync::Removed> Sync::takeRemoved(){std::vector<Removed> out;out.swap(removed_);return out;}
bool standing(const std::vector<Record>& records,uint8_t type,const std::array<double,3>& p,double range){
    for(auto& r:records)if(r.type==type&&present(r.state)){
        double dx=r.at.position[0]-p[0],dy=r.at.position[1]-p[1],dz=r.at.position[2]-p[2];
        if(dx*dx+dy*dy+dz*dz<=range*range)return true;
    }
    return false;
}
std::optional<std::pair<uint8_t,std::array<double,3>>> NetCopies::find(uint64_t ref) const {
    auto it=copies_.find(ref);if(it==copies_.end()||it->second.goneAt)return std::nullopt;
    return std::make_pair(it->second.type,it->second.at);
}
std::optional<uint64_t> NetCopies::refAt(uint8_t netType,const std::array<double,3>& p) const {
    for(auto& [ref,c]:copies_){
        if(c.type!=netType||c.goneAt||(ref>>63))continue;
        double dx=c.at[0]-p[0],dy=c.at[1]-p[1],dz=c.at[2]-p[2];if(dx*dx+dy*dy+dz*dz<=1.0)return ref;
    }
    return std::nullopt;
}
std::vector<MaterialSync::Delivered> MaterialSync::observe(const std::vector<Have>& have){
    std::vector<Delivered> out;std::map<uint64_t,std::array<int32_t,6>> next;unsigned changed=0;
    for(auto& h:have){
        next[h.ref]=h.own;auto old=seen_.find(h.ref);
        if(old==seen_.end())continue;
        if(h.own!=old->second)++changed;
        for(size_t k=0;k<6;++k){
            int64_t more=int64_t(h.own[k])-old->second[k];
#ifndef MUTANT_STRUCTURE_MATERIAL_LESS
            if(more>0&&more<=Most)out.push_back({h.ref,uint8_t(k),int32_t(more)});
#else
            if(more!=0&&more<=Most)out.push_back({h.ref,uint8_t(k),int32_t(more)});
#endif
        }
    }
    seen_.swap(next);
#ifndef MUTANT_STRUCTURE_MATERIAL_LOAD
    if(changed>1)out.clear();
#endif
    return out;
}
std::vector<LevelSync::Level> LevelSync::observe(const std::vector<Level>& levels){
    std::vector<Level> out;std::map<uint64_t,uint32_t> next;
    for(auto& l:levels){
        next[l.ref]=l.level;auto old=seen_.find(l.ref);
        if(old==seen_.end())continue;
#ifndef MUTANT_STRUCTURE_LEVEL_DOWN
        if(l.level<=old->second||l.level<2||l.level>MaxLevel)continue;
#else
        if(l.level==old->second)continue;
#endif
#ifndef MUTANT_STRUCTURE_LEVEL_ECHO
        auto mark=applied_.find(l.ref);if(mark!=applied_.end()&&mark->second>=l.level)continue;
#endif
        out.push_back(l);
    }
    seen_.swap(next);
    return out;
}
std::vector<std::array<double,3>> NetCopies::due(const std::vector<Record>& records,uint64_t now){
    std::vector<std::array<double,3>> out;
    for(auto it=copies_.begin();it!=copies_.end();){
        auto& c=it->second;
        if(!c.goneAt){++it;continue;}
#ifndef MUTANT_STRUCTURE_EARLY
        // In range (state 2) the copy can be removed now; out of range (it stands, but the game's removal works on
        // what is near the player) the dismantle waits for the player to come back, however long that takes.
        bool standing=false,away=false;for(auto& r:records)if(r.type==c.type&&present(r.state)&&r.id>=NetFirst){
            double dx=r.at.position[0]-c.at[0],dy=r.at.position[1]-c.at[1];if(dx*dx+dy*dy>1.5*1.5)continue;
            if(r.state==2)standing=true;else away=true;}
#ifdef MUTANT_STRUCTURE_FORGET
        away=false;
#endif
#else
        bool standing=true,away=false;
#endif
        if(standing){out.push_back(c.at);it=copies_.erase(it);}
        else if(away){c.goneAt=now?now:1;++it;}
        else if(now-c.goneAt>GiveUp)it=copies_.erase(it);
        else ++it;
    }
    return out;
}
}

