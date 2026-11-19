#include "othervehicles.h"
#include <cmath>
#include <set>
#include <sstream>
namespace mod::othervehicles {
void Fleet::tick(uint64_t now,bool permitted,const std::vector<Seen>& seen){
    std::set<uint64_t> here;
    for(auto& s:seen){
        if(!s.visible){
            auto r=rides_.find(s.id);   // shown and not visible now: said every 2 s (a driver's samples may go stale)
            if(r!=rides_.end()&&now>=r->second.notedAt+2000){r->second.notedAt=now;log_("VEHICLES player "+std::to_string(s.id)+" not visible now\n");}
            continue;}
        here.insert(s.id);
        auto it=rides_.find(s.id);
        if(it==rides_.end()){
            if(!permitted||!s.kind||!ride::supported(s.kind)||unavailable_)continue;   // nothing to show yet
#ifndef MUTANT_OTHERVEHICLES_UNCAPPED
            if(rides_.size()>=Max)continue;
#endif
            std::optional<ride::Api> api;
#ifndef MUTANT_OTHERVEHICLES_NO_REUSE
            if(!spare_.empty()){api=spare_.back();spare_.pop_back();}
#endif
            if(!api){api=make_();if(!api){unavailable_=true;log_("VEHICLES unavailable: the game's vehicle code differs\n");continue;}++made_;}
            Entry e;e.api=*api;e.vehicle=std::make_unique<ride::PartnerVehicle>(*api,log_);
            it=rides_.emplace(s.id,std::move(e)).first;
            log_("VEHICLES player "+std::to_string(s.id)+" rides: his vehicle is shown here\n");
        }
        it->second.seen=now;it->second.driven=s.kind!=0;if(s.kind)it->second.at=s.at;
        if(now>=it->second.notedAt+2000){it->second.notedAt=now;std::ostringstream o;o<<"VEHICLES player "<<s.id<<" kind "<<int(s.kind)<<" at "<<int(s.at.position[0])<<","<<int(s.at.position[1])<<","<<int(s.at.position[2])<<"\n";log_(o.str());}
        if(s.kind)it->second.vehicle->tick(now,permitted,s.kind,&s.at,s.look);
        else it->second.vehicle->tick(now,permitted,0,nullptr);   // he got off: it stays parked where it is
    }
    for(auto it=rides_.begin();it!=rides_.end();){
#ifndef MUTANT_OTHERVEHICLES_KEEP_GONE
        const bool gone=!here.count(it->first)&&now>=it->second.seen+GoneMs;
#else
        const bool gone=false;
#endif
        if(gone||!permitted){
            it->second.vehicle->tick(now,false,0,nullptr);   // the vehicle goes (its entity removed through the pool)
            if(gone){spare_.push_back(it->second.api);log_("VEHICLES player "+std::to_string(it->first)+" gone: his vehicle with him\n");it=rides_.erase(it);continue;}
        }
        ++it;
    }
}
void Fleet::forget(const char* why){
    for(auto& r:rides_){r.second.vehicle->forget(why);spare_.push_back(r.second.api);}
    rides_.clear();
}
std::vector<Fleet::Vehicle> Fleet::vehicles() const {
    std::vector<Vehicle> out;
    for(auto& r:rides_)if(r.second.vehicle->entity())out.push_back({r.first,r.second.vehicle->entity(),r.second.vehicle->kind(),r.second.driven,r.second.at});
    return out;
}
std::optional<Candidate> choose(const std::vector<Candidate>& cs,const std::array<double,3>& porter,double reach,uint64_t sticky){
    if(sticky){for(auto& c:cs)if(c.id==sticky&&c.driven&&c.entity)return c;return std::nullopt;}
    std::optional<Candidate> best;double bestD=0;
    for(auto& c:cs){
        if(!c.driven||!c.entity)continue;
        const double d=std::hypot(c.at[0]-porter[0],c.at[1]-porter[1]);
#ifndef MUTANT_OTHERVEHICLES_ANY_DISTANCE
        if(d>reach)continue;
#endif
        if(!best||d<bestD){best=c;bestD=d;}
    }
    return best;
}
}
