#include "bodylod.h"
#include <algorithm>
#include <cmath>
namespace mod::bodylod {
bool beyond(bool was,double distance,double metres){
    if(!(metres>0))return false;
#ifndef MUTANT_BODYLOD_FLAP
#ifndef MUTANT_BODYLOD_MARGIN
    const double margin=std::min(KeepMetres,metres/2);
#else
    const double margin=KeepMetres;
#endif
#else
    const double margin=0;
#endif
    return was?distance>metres-margin:distance>metres;
}
void Policy::tick(const std::vector<Body>& bodies,const std::array<double,3>* here,const Settings& s){
    std::map<uint64_t,State> next;
    for(auto& b:bodies){
#ifndef MUTANT_BODYLOD_FORGET
        auto it=states_.find(b.key);State st=it==states_.end()?State{}:it->second;
#else
        State st=state(b.key);
#endif
#ifndef MUTANT_BODYLOD_UNKNOWN
        if(here&&b.at){
#else
        if(b.at){static const std::array<double,3> origin{};if(!here)here=&origin;
#endif
            const auto& a=*b.at;const double d=std::sqrt((a[0]-(*here)[0])*(a[0]-(*here)[0])+(a[1]-(*here)[1])*(a[1]-(*here)[1])+(a[2]-(*here)[2])*(a[2]-(*here)[2]));
            st.reduced=beyond(st.reduced,d,s.detailMetres);
            st.hidden=beyond(st.hidden,d,s.hideMetres);
        }
        next[b.key]=st;
    }
#ifndef MUTANT_BODYLOD_FORGET
    states_=std::move(next);
#else
    for(auto& kv:next)states_[kv.first]=kv.second;
#endif
}
size_t Policy::reduced() const {size_t n=0;for(auto& kv:states_)n+=kv.second.reduced;return n;}
size_t Policy::hiddenCount() const {size_t n=0;for(auto& kv:states_)n+=kv.second.hidden;return n;}
std::set<uint64_t> drawn(const std::vector<std::pair<uint64_t,uint64_t>>& bodies,const Policy& policy){
    std::set<uint64_t> out;
    for(auto& [key,entity]:bodies){
        if(!entity)continue;
#ifndef MUTANT_BODYLOD_DRAWN
        if(policy.hidden(key))continue;
#else
        (void)policy;
#endif
        out.insert(key);
    }
    return out;
}
Look look(const State& st,const Lab& lab,bool labDistant){
    Look l;l.shadow=!st.reduced;
    if(st.reduced)l.cloth=ClothOff;
#ifndef MUTANT_BODYLOD_LAB
    if(lab.shadow<=1)l.shadow=!(labDistant&&lab.shadow==0);
    if(lab.cloth<999){l.cloth.reset();if(labDistant)l.cloth=float(lab.cloth)/100.0f;}
#else
    (void)lab;(void)labDistant;
#endif
    return l;
}
Change Applied::step(const Look& want,std::optional<float> clothNow){
    Change c;
#ifndef MUTANT_BODYLOD_SHADOW_FRESH
    if(!want.shadow!=shadowOff_){c.shadow=want.shadow;shadowOff_=!want.shadow;}
#else
    c.shadow=want.shadow;shadowOff_=!want.shadow;
#endif
#ifndef MUTANT_BODYLOD_OWN
    if(!own_&&!set_&&clothNow&&std::isfinite(*clothNow)&&*clothNow>=0&&*clothNow<100)own_=*clothNow;
#else
    if(clothNow)own_=*clothNow;
#endif
    if(!own_||!clothNow)return c;   // the model not read this tick: left as it is
    if(want.cloth){if(set_!=want.cloth){c.cloth=*want.cloth;set_=want.cloth;}}
#ifndef MUTANT_BODYLOD_RESTORE
    else if(set_){c.cloth=*own_;set_.reset();}
#endif
    return c;
}
}
