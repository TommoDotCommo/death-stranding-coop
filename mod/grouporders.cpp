#include "grouporders.h"
#include <cmath>
#include <sstream>
namespace mod::grouporders {
namespace {
constexpr uint64_t MissionMask=0xffc0ffffffffffffull;
template<class M> void bound(M& m){while(m.size()>HeardMax)m.erase(m.begin());}
std::string hex(uint64_t v){std::ostringstream o;o<<std::hex<<v;return o.str();}
}
int64_t Together::fresh(){
    // splitmix64: a party number nobody else is likely to have, never 0
    uint64_t z=(rng_+=0x9e3779b97f4a7c15ull);z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;z^=z>>31;
    const int64_t p=int64_t(z&uint64_t(PartyMax));return p?p:1;
}
void Together::hear(const std::vector<groupevents::Event>& events,uint64_t now){
    for(auto& e:events){
        if(e.party<=0)continue;
        if(e.kind=="order"){
            const auto at=std::make_pair(e.party,uint64_t(e.key));
            if(e.value==orders::Start){
                if(e.place.size()!=3)continue;   // nobody knows where it was taken: nobody joins it
#ifndef MUTANT_GROUPORDERS_REJOIN
                if(left_.count(e.party))continue;
#endif
                const uint64_t ago=uint64_t(std::max<int64_t>(0,e.age))*1000;
                offers_[at]=Offer{{e.place[0],e.place[1],e.place[2]},now>ago?now-ago:0};}
            else if(e.value==orders::Success||e.value==orders::Fail){ends_[at]=uint8_t(e.value);offers_.erase(at);left_.insert(e.party);}
        }
        else if(e.kind=="unit"&&e.value==1)units_[std::make_pair(e.party,uint64_t(e.key))]=e.data;
    }
    bound(offers_);bound(ends_);bound(units_);
    while(left_.size()>HeardMax)left_.erase(left_.begin());
}
Step Together::step(uint64_t now,const std::vector<orders::Order>& orders,const std::vector<orders::Unit>& units,const std::optional<Place>& porter){
    Step out;
    std::map<uint64_t,const orders::Order*> mine;for(auto& o:orders)if(orders::shareable(o))mine[o.id]=&o;
    // This world's own orders: a start here leads a party, an end here is said to it.
    std::map<uint64_t,orders::Order> next;
    for(auto& [id,o]:mine){
        next[id]=*o;
        auto old=seen_.find(id);const bool was=old!=seen_.end()&&old->second.state==orders::Progress,is=o->state==orders::Progress;
#ifdef MUTANT_GROUPORDERS_OLD_START
        const bool primed=true;
#else
        const bool primed=primed_;
#endif
        auto m=members_.find(id);
        if(primed&&is&&!was){
            if(m!=members_.end()&&m->second.pending){m->second.pending=false;applied_.erase(id);out.lines.push_back(Line::Joined);
                out.log.push_back("GROUPORDER joined party "+std::to_string(m->second.party)+": order "+hex(id)+" under way here");}
            else if(m==members_.end()){
                const int64_t party=fresh();members_[id]=Member{party,true,false,now};
                groupevents::Event e{"order",int64_t(id),orders::Start,party,{},{}};
                if(porter)e.place={(*porter)[0],(*porter)[1],(*porter)[2]};
                out.say.push_back(e);out.log.push_back("GROUPORDER took order "+hex(id)+" here: party "+std::to_string(party)+(porter?"":" (where unknown: nobody can join)"));}
        }
        else if(primed_&&was&&!is&&m!=members_.end()&&!m->second.pending){
            const uint8_t what=o->state==16||(o->state!=8&&o->result==orders::Completed)?orders::Success:orders::Fail;
            auto a=applied_.find(id);const bool echo=a!=applied_.end()&&a->second==what;if(a!=applied_.end())applied_.erase(a);
#ifdef MUTANT_GROUPORDERS_ECHO
            const bool told=true;
#else
            const bool told=!echo;
#endif
#ifdef MUTANT_GROUPORDERS_MEMBER_FAIL
            const bool says=told;
#else
            const bool says=told&&(what==orders::Success||m->second.leader);   // a member who gives up leaves alone
#endif
            if(says)out.say.push_back(groupevents::Event{"order",int64_t(id),what,m->second.party,{},{}});
            out.log.push_back("GROUPORDER order "+hex(id)+(what==orders::Success?" completed":" given up")+" here"+(says?" -> party":echo?" (for the party)":" (left the party)"));
            left_.insert(m->second.party);members_.erase(m);
        }
    }
    seen_.swap(next);primed_=true;
    // Units handed in here, of the parties this game is in (after the orders: a party just led or joined learns its
    // rows first): said, unless marked here for the party.
    std::map<uint64_t,uint16_t> nextUnits;
    for(auto& u:units){
        auto m=members_.find(u.mission);if(m==members_.end()||m->second.pending)continue;
        const uint64_t k=orders::UnitSync::key(u.mission,u.unit);nextUnits[k]=u.state;
        auto old=seenUnits_.find(k);
        if(u.state!=orders::Delivered||old==seenUnits_.end()||old->second!=orders::NotDelivered)continue;
#ifndef MUTANT_GROUPORDERS_ECHO
        if(unitsApplied_.count(k))continue;
#endif
        groupevents::Event e{"unit",int64_t(k),1,m->second.party,{},{double(u.packed),double(u.life[0]),double(u.life[1])}};
        for(auto& x:e.data)if(!std::isfinite(x))x=0;
        out.say.push_back(e);out.log.push_back("GROUPORDER unit "+std::to_string(u.unit)+" of "+hex(u.mission)+" handed in here -> party");}
    seenUnits_.swap(nextUnits);
    // A start asked for here that the game never showed: given up, the party not joined again.
    for(auto it=members_.begin();it!=members_.end();){
        if(it->second.pending&&now>=it->second.since+JoinSettleMs){
            out.log.push_back("GROUPORDER order "+hex(it->first)+" did not start here: party "+std::to_string(it->second.party)+" not joined");
            left_.insert(it->second.party);applied_.erase(it->first);it=members_.erase(it);}
        else ++it;}
    auto underWay=[&](uint64_t id){auto f=mine.find(id);return f!=mine.end()&&f->second->state==orders::Progress;};
    // Ends heard: only for an order of the same party under way here.
    for(auto it=ends_.begin();it!=ends_.end();){
        const int64_t party=it->first.first;const uint64_t id=it->first.second;auto m=members_.find(id);
#ifdef MUTANT_GROUPORDERS_NOT_MINE
        const bool ours=underWay(id);
#else
        const bool ours=m!=members_.end()&&m->second.party==party&&!m->second.pending&&underWay(id);
#endif
        if(ours){
            const auto what=Request::What(it->second);
            out.apply.push_back(Request{what,id,0,party,0,{}});applied_[id]=it->second;
            out.lines.push_back(what==Request::Success?Line::DoneByOther:Line::GivenUpByLeader);
            out.log.push_back("GROUPORDER party "+std::to_string(party)+(what==Request::Success?" completed":" gave up")+" order "+hex(id)+": asked here");
            it=ends_.erase(it);continue;}
        if(m!=members_.end()&&m->second.party==party&&m->second.pending){++it;continue;}   // wait for the start to show
        it=ends_.erase(it);
    }
    // Units heard: a row of an order of the same party under way here, not delivered.
    for(auto it=units_.begin();it!=units_.end();){
        const int64_t party=it->first.first;const uint64_t k=it->first.second,id=k&MissionMask;const uint8_t n=uint8_t(k>>48&0x3f);
        auto m=members_.find(id);
        const bool ours=m!=members_.end()&&m->second.party==party&&!m->second.pending&&underWay(id);
        if(!ours){
#ifndef MUTANT_GROUPORDERS_UNIT_ANY
            if(left_.count(party)&&(m==members_.end()||m->second.party!=party))it=units_.erase(it);else ++it;   // kept for a later join
            continue;
#endif
        }
        const orders::Unit* row=nullptr;for(auto& u:units)if(u.mission==id&&u.unit==n)row=&u;
        bool waiting=row&&row->state==orders::NotDelivered&&!unitsApplied_.count(k);
#ifdef MUTANT_GROUPORDERS_UNIT_ANY
        waiting=row!=nullptr;
#endif
        if(waiting){
            Request r{Request::Unit,id,n,party,0,{}};auto& d=it->second;
            if(d.size()>=1&&d[0]>=0&&d[0]<=4294967295.0)r.packed=uint32_t(d[0]);
            for(size_t i=0;i<2;++i)if(d.size()>i+1&&std::fabs(d[i+1])<=100000.0)r.life[i]=float(d[i+1]);
            out.apply.push_back(r);unitsApplied_.insert(k);out.lines.push_back(Line::UnitByOther);
            out.log.push_back("GROUPORDER party "+std::to_string(party)+" handed in unit "+std::to_string(n)+" of "+hex(id)+": marked delivered here");}
        it=units_.erase(it);
    }
    // Starts heard: joined from where this porter stands, while fresh, for an order available here.
    for(auto it=offers_.begin();it!=offers_.end();){
        const int64_t party=it->first.first;const uint64_t id=it->first.second;const Offer& o=it->second;
#ifndef MUTANT_GROUPORDERS_STALE
        if(now>o.heardAt+JoinWindowMs){it=offers_.erase(it);continue;}
#endif
#ifndef MUTANT_GROUPORDERS_REJOIN
        if(left_.count(party)){it=offers_.erase(it);continue;}
#endif
        if(members_.count(id)){++it;continue;}
        auto f=mine.find(id);
#ifndef MUTANT_GROUPORDERS_STATE
        if(f==mine.end()||f->second->state!=orders::Available){++it;continue;}
#else
        if(f==mine.end()){++it;continue;}
#endif
        if(!porter){++it;continue;}
        const double dx=(*porter)[0]-o.at[0],dy=(*porter)[1]-o.at[1],dz=(*porter)[2]-o.at[2];
#ifndef MUTANT_GROUPORDERS_FAR
        if(!(dx*dx+dy*dy+dz*dz<=JoinMetres*JoinMetres)){++it;continue;}
#endif
        out.apply.push_back(Request{Request::Start,id,0,party,0,{}});members_[id]=Member{party,false,true,now};applied_[id]=orders::Start;
        out.log.push_back("GROUPORDER a porter near here took order "+hex(id)+" (party "+std::to_string(party)+", "+std::to_string(int(std::sqrt(dx*dx+dy*dy+dz*dz)))+" m away): asked here");
        it=offers_.erase(it);
    }
    return out;
}
void Together::done(const Request& r,bool accepted){
    if(accepted)return;
    if(r.what==Request::Start){auto m=members_.find(r.mission);if(m!=members_.end()&&m->second.party==r.party&&m->second.pending)members_.erase(m);left_.insert(r.party);applied_.erase(r.mission);}
    else if(r.what==Request::Success||r.what==Request::Fail)applied_.erase(r.mission);
}
void Together::reset(){
    for(auto& m:members_)left_.insert(m.second.party);
    members_.clear();seen_.clear();seenUnits_.clear();applied_.clear();unitsApplied_.clear();ends_.clear();units_.clear();primed_=false;
}
}
