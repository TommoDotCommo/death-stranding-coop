#include "cargo.h"
#include <algorithm>
#include <cstring>
#include <cmath>
namespace mod::cargo {
namespace {
constexpr uint64_t Records=0x20,Stride=0x240;constexpr unsigned Chunk=512;   // 295 KB per copy
double distance(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
bool placed(const std::array<double,3>& p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&(p[0]!=0||p[1]!=0||p[2]!=0);}
}
unsigned scanLimit(const std::vector<Record>& full){
    unsigned high=0;for(auto& r:full)high=std::max(high,r.index+1);
    return std::min(MaxRecords,std::max(FirstRecords,high+64));
}
std::vector<Record> readRecords(const discovery::Sampler& s,uint64_t manager,uint64_t vtable,unsigned limit){
    std::vector<Record> out;uint64_t v=0;
    if(!manager||!s.read(manager,&v,8)||v!=vtable)return out;
#ifndef MUTANT_CARGO_FIRST511
    limit=std::min(limit,MaxRecords);
#else
    limit=std::min(limit,FirstRecords);
#endif
    std::vector<uint8_t> chunk(Stride*Chunk);
    for(unsigned at=0;at<limit;at+=Chunk){
        unsigned n=std::min(Chunk,limit-at);
        // One copy per chunk (readOnce): a full double-checked scan cost 126 ms on the game thread (live 2026-11-09).
        if(!s.readOnce(manager+Records+Stride*at,chunk.data(),Stride*n))break;
      for(unsigned k=0;k<n;++k){unsigned i=at+k;const uint8_t* block=chunk.data()+Stride*k;
        uint64_t item=0;std::memcpy(&item,block+0x28,8);if(!item)continue;
        Record r;std::memcpy(&r.slot,block+0x18,4);std::memcpy(&r.state,block+0x20,4);std::memcpy(&r.hash,block+0xc8,4);
        std::memcpy(r.position.data(),block+0x38,24);std::memcpy(&r.holder,block+0x1c,4);r.index=i;out.push_back(r);
      }
    }
    return out;
}
void Sync::expect(const Placement& p,uint64_t now){expected_.push_back({p,now});}
size_t Sync::mirrored() const {return size_t(std::count_if(tracks_.begin(),tracks_.end(),[](auto& t){return t.second.mirror;}));}
size_t Sync::shared() const {return size_t(std::count_if(tracks_.begin(),tracks_.end(),[](auto& t){return t.second.id!=0;}));}
std::vector<Taken> Sync::takeTaken(){std::vector<Taken> out;out.swap(taken_);return out;}
std::vector<uint32_t> Sync::takeDoomed(){std::vector<uint32_t> out;out.swap(doomed_);return out;}
std::vector<Kept> Sync::takeKept(){std::vector<Kept> out;out.swap(kept_);return out;}
std::optional<uint32_t> Sync::partnerTook(uint64_t id,uint32_t hash,const std::array<double,3>& at){
    // A ref with the low 20 bits zero is not an id but the partner's run tag (Sync::wireRef): an item it had no id for.
    uint64_t run=0;if(id&&(id&0xfffff)==0){run=id>>20;id=0;}
#ifndef MUTANT_CARGO_DOOM
    // Taken before its copy appeared here: the copy is deleted when it appears.
    if(id)for(auto& e:expected_)if(e.first.id==id){cancelled_.push_back(id);return std::nullopt;}
#endif
    auto forget=[](Track& t){t.id=0;t.mirror=false;t.announced=true;t.learned=false;t.removed=true;t.resting=false;};   // never announced or reported again
    if(id)for(auto& [slot,t]:tracks_)if(t.id==id){
#ifndef MUTANT_CARGO_TOOK_ECHO
        (void)slot;t.id=0;t.mirror=false;t.announced=true;t.resting=false;   // never announced or reported again
#else
        (void)slot;
#endif
        // Staged by the cargo menu (not yet a pick-up): removed if it goes back to the ground, kept if he takes it.
        if(t.staged){t.lostToPartner=true;return std::nullopt;}
        if(t.where==OnPorter)return std::nullopt;            // both picked it up at once: ours stays with us
        return t.index;
    }
#ifndef MUTANT_CARGO_FIND
    // Not known by id here (either mod restarted since it was shared): the nearest same item on the ground.
    if(hash){Track* best=nullptr;
        // Taken without an id: the partner had that item at its start. Best: an item both worlds had at the first look
        // here, or a copy of the partner's item from an earlier run of its mod; then ours; last a copy of an item it placed
        // in its current run (it would have sent that id). Nearest within each rank.
        int bestRank=3;double bd=FindRange;
        for(auto& [slot,t]:tracks_){(void)slot;if(t.hash!=hash||(t.where==OnPorter&&!t.staged)||t.removed||t.local)continue;
#ifndef MUTANT_CARGO_OTHER_ID
            if(id&&t.id&&t.id!=id)continue;   // another shared item (found by the world fuzz once items could slide)
#endif
            int rank=0;
#ifndef MUTANT_CARGO_RUN
            if(!id&&!t.learned){bool theirs=t.mirror&&t.id;rank=!theirs?1:(run&&(t.id>>20)==run)?2:0;}
#endif
            double d=distance(t.staged?t.stagedFrom:t.position,at);if(d>FindRange)continue;
            if(rank<bestRank||(rank==bestRank&&d<=bd)){bestRank=rank;bd=d;best=&t;}}
        if(best&&best->staged){best->lostToPartner=true;return std::nullopt;}
        if(best){forget(*best);return best->index;}}
#endif
    return std::nullopt;
}
std::vector<Placement> Sync::observe(const std::vector<Record>& records,const std::array<double,3>& porter,uint64_t now){
    std::vector<Placement> out;
    expected_.erase(std::remove_if(expected_.begin(),expected_.end(),[&](auto& e){return now-e.second>ExpectFor;}),expected_.end());
    // A cancelled expectation goes with its expectation (its copy never appeared: nothing to delete).
    cancelled_.erase(std::remove_if(cancelled_.begin(),cancelled_.end(),[&](uint64_t id){
        return std::none_of(expected_.begin(),expected_.end(),[&](auto& e){return e.first.id==id;});}),cancelled_.end());
    std::map<uint32_t,Track> next;
    for(auto& r:records){
        auto old=tracks_.find(r.slot);bool same=old!=tracks_.end()&&old->second.hash==r.hash;
        Track t=same?old->second:Track{};
        if(!same){t.hash=r.hash;t.position=r.position;t.stillSince=now;t.fresh=primed_;t.learned=!primed_&&location(r)==InWorld;
                  if(t.learned){t.resting=true;t.restAt=r.position;}}
        t.index=r.index;
#ifndef MUTANT_CARGO_LOCAL
        t.local=kind(r)!=SharedKind;
#endif
        uint8_t was=t.where,where=location(r);t.where=where;
        if(where==OnPorter){t.heldAt=now;t.carriedAt=r.position;}
        if(same&&was==OnPorter&&where==InWorld)t.downAt=now;
        // Just put down and still at its carried position: the game has not written the real place yet.
        bool carryFrame=where==InWorld&&t.downAt&&distance(r.position,t.carriedAt)<=Still&&(now-t.downAt<CarryWait||distance(r.position,porter)>DropRange);
#ifndef MUTANT_CARGO_TAKEN
        // Picked up: a shared item went from the world onto this porter. The partner removes theirs; from now on the item
        // is this porter's own cargo (a later put-down is a new placement).
        // Reported with its shared id, or without one for an item that was already here at the first look (shared in an
        // earlier session, or the same game cargo in both worlds): the partner then finds it by item and place.
        if(same&&was==InWorld&&where==OnPorter&&t.local)kept_.push_back({t.hash,kind(r),true});
        else if(same&&was==InWorld&&where==OnPorter&&(t.id||t.learned)){
#ifndef MUTANT_CARGO_STAGED
            // Not reported yet: opening the cargo menu next to an item reads exactly like this (location 2, carry frame)
            // and the menu puts back what he leaves (live 2026-11-07). Reported once he moves, or it lands elsewhere.
            if(!t.staged){t.stagedFrom=t.position;t.stagedPath=0;t.lostToPartner=false;}   // picked up again while pending: same pick-up
            t.staged=true;t.stagedPorter=porter;
#else
            taken_.push_back({t.id,t.hash,t.resting?t.restAt:t.position});t.id=0;t.mirror=false;t.announced=false;t.fresh=false;t.learned=false;t.resting=false;
#endif
        }
#endif
        bool returned=false;
        if(t.staged){
            auto taken=[&]{   // a real pick-up
                if(!t.lostToPartner)taken_.push_back({t.id,t.hash,t.resting?t.restAt:t.stagedFrom});   // where the partner has it
                t.id=0;t.mirror=false;t.announced=false;t.fresh=false;t.learned=false;t.staged=false;t.lostToPartner=false;t.resting=false;};
            if(where==OnPorter){t.stagedPath+=distance(porter,t.stagedPorter);t.stagedPorter=porter;if(t.stagedPath>StagedMove)taken();}
            else if(where==InWorld){
                if(carryFrame){}                                                  // the real place comes later
                else if(distance(r.position,t.stagedFrom)<=MatchRange){           // back where it lay: never taken
                    returned=true;t.staged=false;t.heldAt=0;
                    if(t.lostToPartner){doomed_.push_back(r.index);t.id=0;t.mirror=false;t.announced=true;t.learned=false;t.removed=true;t.lostToPartner=false;t.resting=false;}}
                else{taken();t.putDown=true;}}                                    // put down elsewhere: a new placement
            else taken();
        }
        if(same&&was==OnPorter&&where==InWorld&&!t.staged&&!returned){t.putDown=true;t.learned=false;}   // put down here: ours, not an older item
        // Moved: not settled.
        if(distance(t.position,r.position)>Still){t.position=r.position;t.stillSince=now;}
        // Without the location byte (0), an item next to the porter counts as held. With it, only what he carries does:
        // the proximity guess re-announced items he merely stood next to after a restart (found by the world fuzz).
#ifndef MUTANT_CARGO_NEAR_HELD
        if(where==0&&placed(r.position)&&distance(r.position,porter)<=HeldRange)t.heldAt=now;
#else
        if(placed(r.position)&&distance(r.position,porter)<=HeldRange)t.heldAt=now;
#endif
        if(!same){
            // Created for a partner placement: same item, appearing where it was asked for (also on the first look: the
            // partner's item may already exist then; found by the sync fuzz).
            for(auto it=expected_.begin();it!=expected_.end();++it)
#ifndef MUTANT_CARGO_ECHO
                if(it->first.hash==r.hash&&distance(it->first.position,r.position)<=MatchRange){
                    t.mirror=true;t.id=it->first.id;t.resting=true;t.restAt=it->first.position;
                    auto c=std::find(cancelled_.begin(),cancelled_.end(),t.id);
                    if(c!=cancelled_.end()){cancelled_.erase(c);doomed_.push_back(r.index);t.id=0;t.announced=true;}
                    expected_.erase(it);break;}
#else
                {(void)it;break;}
#endif
        }
        bool settled=placed(r.position)&&now-t.stillSince>=Settle&&where!=OnPorter;
#ifndef MUTANT_CARGO_CARRY_FRAME
        if(t.putDown&&carryFrame)settled=false;
        bool ours=(t.fresh&&distance(r.position,porter)<=NearRange)||(t.heldAt&&now-t.heldAt<=HeldMemory&&distance(r.position,porter)<=DropRange);
#else
        bool ours=(t.fresh&&distance(r.position,porter)<=NearRange)||(t.heldAt&&now-t.heldAt<=HeldMemory);
#endif
#ifndef MUTANT_CARGO_HELD
        bool letGo=t.putDown||distance(r.position,porter)>=AwayRange||t.fresh;
#else
        bool letGo=true;
#endif
        if(primed_&&!t.mirror&&!t.announced&&settled&&ours&&letGo){
            if(t.local)kept_.push_back({r.hash,kind(r),false});   // not shared: noted once
            else{t.id=nextIdNow();out.push_back({r.hash,r.position,t.id});t.resting=true;t.restAt=r.position;}
            t.announced=true;t.fresh=false;t.putDown=false;}
#ifndef MUTANT_CARGO_MOVED
        // Moved on the ground (slope, river): the partner's copy goes from the old place and comes back at the new one.
        else if(primed_&&t.resting&&!t.local&&!t.staged&&where==InWorld&&settled&&distance(r.position,t.restAt)>MoveRange){
            taken_.push_back({t.id,t.hash,t.restAt});t.id=nextIdNow();out.push_back({r.hash,r.position,t.id});
            t.mirror=false;t.learned=false;t.announced=true;t.restAt=r.position;}
#endif
        next[r.slot]=t;
    }
    // Gone: a shared item that vanished from the world while this porter was near (used, delivered, destroyed here).
    for(auto& [slot,t]:tracks_){
        auto n=next.find(slot);bool gone=n==next.end()||n->second.hash!=t.hash;
        if(!gone)continue;
        if(t.staged){if(!t.lostToPartner)taken_.push_back({t.id,t.hash,t.resting?t.restAt:t.stagedFrom});continue;}   // used from the cargo menu
        if(t.id&&t.where==InWorld&&distance(t.position,porter)<=NearRange)taken_.push_back({t.id,t.hash,t.resting?t.restAt:t.position});
    }
    tracks_.swap(next);primed_=true;
    return out;
}
}
