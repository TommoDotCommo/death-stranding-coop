#include "roads.h"
#include <cstring>
#include <stdexcept>
namespace mod::roads {
static bool sane(const Set& s){for(auto v:s)if(v<0||v>100000000)return false;return true;}
std::vector<Paver> readPavers(const discovery::Sampler& s,uint64_t base){
    std::vector<Paver> out;uint64_t manager=0,list=0;int32_t count=0;
    if(!s.read(base+0x7bf4960,&manager,8)||!manager||!s.read(manager+0x56a258,&count,4)||!s.read(manager+0x56a260,&list,8)||!list||count<=0||count>8192)return out;
    std::vector<uint64_t> all;all.resize(size_t(count));if(!s.read(list,all.data(),all.size()*8))return out;
    for(auto object:all){
        uint8_t type=0;Paver p;
        if(!object||!s.read(object+0x34,&type,1)||type!=0x24)continue;
        if(!s.read(object+0x30,&p.id,4)||!s.read(object+0x84,&p.level,4)||!s.read(object+0x210,p.own.data(),sizeof p.own)||!s.read(object+0x248,p.net.data(),sizeof p.net))continue;
        if(!sane(p.own)||!sane(p.net)||p.level>16)continue;
        p.address=object;out.push_back(p);
    }
    return out;
}
std::vector<News> Sync::observe(const std::vector<Paver>& pavers){
    std::vector<News> out;std::map<uint32_t,Paver> next;std::set<uint32_t> changed;
    for(auto& p:pavers){
        next[p.id]=p;auto old=seen_.find(p.id);
        if(!primed_||old==seen_.end())continue;
        if(p.own!=old->second.own||p.level!=old->second.level)changed.insert(p.id);
        for(int k=0;k<Kinds;++k){
            int32_t more=p.own[size_t(k)]-old->second.own[size_t(k)];
#ifndef MUTANT_ROAD_LESS
            if(more>0&&more<=Most)out.push_back({p.id,Material+uint32_t(k),more});
#else
            if(more!=0&&more<=Most)out.push_back({p.id,Material+uint32_t(k),more});
#endif
        }
        if(p.level>=2&&old->second.level<2){
#ifndef MUTANT_ROAD_ECHO
            if(applied_.erase(p.id))continue;
#endif
            out.push_back({p.id,Built,0});
        }
    }
    seen_.swap(next);primed_=true;
#ifndef MUTANT_ROAD_LOAD
    // A player delivers to one paver at a time: several changed at one look is a save being loaded, not deliveries.
    if(changed.size()>1){out.clear();applied_.clear();}
#endif
    return out;
}
std::vector<News> Sync::everything(const std::vector<Paver>& pavers){
    std::vector<News> out;
    for(auto& p:pavers){
        for(int k=0;k<Kinds;++k){int64_t all=int64_t(p.own[size_t(k)])+p.net[size_t(k)];if(all>0&&all<=Most)out.push_back({p.id,Total+uint32_t(k),int32_t(all)});}
        if(p.level>=2)out.push_back({p.id,Built,0});
    }
    return out;
}
int32_t Sync::owed(const Paver& p,const News& n){
    if(n.item<Total||n.item>=Total+Kinds||n.amount<=0||n.amount>Most)return 0;
    size_t k=n.item-Total;int64_t have=int64_t(p.own[k])+p.net[k];
#ifndef MUTANT_ROAD_TOTAL_ADDS
    return have>=n.amount?0:int32_t(n.amount-have);
#else
    (void)have;return n.amount;
#endif
}
const Paver* Sync::wanted(const std::vector<Paver>& pavers,const News& n){
    for(auto& p:pavers)if(p.id==n.id){
        if(n.item==Built){
#ifndef MUTANT_ROAD_TWICE
            return p.level<2?&p:nullptr;
#else
            return &p;
#endif
        }
#ifndef MUTANT_ROAD_AMOUNT
        if(n.item>=Material&&n.item<Material+Kinds&&n.amount>0&&n.amount<=Most)return &p;
#else
        if(n.item>=Material&&n.item<Material+Kinds)return &p;
#endif
        // A total only matters where the road is not built yet (on a built road materials would count as repair).
        if(n.item>=Total&&n.item<Total+Kinds&&n.amount>0&&n.amount<=Most)return p.level<2?&p:nullptr;
        return nullptr;
    }
    return nullptr;
}
Api nativeApi(discovery::Sampler& s){
#ifdef _WIN32
    auto same=[&s](uint64_t rva,std::initializer_list<uint8_t> want){
        std::vector<uint8_t> got(want.size());return s.read(s.base+rva,got.data(),got.size())&&std::equal(want.begin(),want.end(),got.begin());};
    if(!same(0x2c321a0,{0x40,0x53,0x57,0x41,0x57,0x48,0x83,0xec,0x40})||!same(0x2c59770,{0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8b,0xec})
       ||!same(0x2c41540,{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54})||!same(0x2c69170,{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20}))
        throw std::runtime_error("road functions differ");
    auto give=reinterpret_cast<void(*)(uint64_t,uint64_t,const int32_t*,uint8_t,void*)>(s.base+0x2c321a0);
    auto status=reinterpret_cast<uint8_t(*)(uint64_t,int32_t*,int32_t*)>(s.base+0x2c59770);
    auto own=reinterpret_cast<void(*)(uint64_t,const int32_t*)>(s.base+0x2c41540);
    auto queue=reinterpret_cast<void(*)(uint64_t,uint64_t)>(s.base+0x2c69170);
    auto paver=[&s](uint64_t object){uint8_t type=0;return object&&s.read(object+0x34,&type,1)&&type==0x24;};
    Api api;
    api.give=[&s,give,paver](uint64_t object,const Set& mats){
        uint64_t t=0;if(!paver(object)||!s.read(object+0x458,&t,8))return;
        Set copy=mats;give(object,t,copy.data(),1,nullptr);
    };
    api.missing=[status,paver](uint64_t object){
        Set need{},have{};if(!paver(object)||status(object,need.data(),have.data())!=1)return Set{};
        for(auto& v:need)if(v<0||v>Most)return Set{};
        return need;
    };
    api.complete=[&s,api](uint64_t object){
        // The game reports the whole requirement of the next level: what is already there is taken off.
        Set need=api.missing(object),own{},net{};bool any=false;
        if(!s.read(object+0x210,own.data(),sizeof own)||!s.read(object+0x248,net.data(),sizeof net))return;
        for(size_t k=0;k<need.size();++k){int64_t left=int64_t(need[k])-own[k]-net[k];need[k]=left>0?int32_t(left):0;any=any||need[k]>0;}
        if(any)api.give(object,need);
    };
    api.deliver=[&s,status,own,queue,paver](uint64_t object,const Set& mats){
        uint64_t manager=0;if(!paver(object)||!s.read(s.base+0x7bf4960,&manager,8)||!manager)return;
        Set copy=mats,need{},have{};own(object,copy.data());
        if(status(object,need.data(),have.data())==0)queue(manager,object);
    };
    return api;
#else
    (void)s;throw std::runtime_error("native roads need the game");
#endif
}
Levels nativeLevels(discovery::Sampler& s){
#ifdef _WIN32
    static const uint8_t want[11]={0x40,0x55,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x48};uint8_t got[11]{};
    if(!s.read(s.base+0x2c5d9d0,got,11)||std::memcmp(got,want,11))throw std::runtime_error("construction level function differs");
    nativeApi(s);                                                       // checks the routines shared with the pavers
    auto total=reinterpret_cast<void(*)(uint64_t,uint32_t,int32_t*)>(s.base+0x2c5d9d0);
    auto give=reinterpret_cast<void(*)(uint64_t,uint64_t,const int32_t*,uint8_t,void*)>(s.base+0x2c321a0);
    auto own=reinterpret_cast<void(*)(uint64_t,const int32_t*)>(s.base+0x2c41540);
    auto queue=reinterpret_cast<void(*)(uint64_t,uint64_t)>(s.base+0x2c69170);
    auto lacking=[&s,total](uint64_t object,uint32_t level,Set& need,std::string& why){
        Set have{},net{};uint64_t first=0,config=0;uint32_t most=0;bool any=false;
        if(!object||!s.read(object,&first,8)||first<s.base||!s.read(object+0x90,&config,8)||!config||!s.read(config+0x98,&most,4)){why="unreadable";return false;}
        if(level>most){why="above the structure's top level "+std::to_string(most);return false;}
        if(!s.read(object+0x210,have.data(),sizeof have)||!s.read(object+0x248,net.data(),sizeof net)||!sane(have)||!sane(net)){why="materials unreadable";return false;}
        total(object,level,need.data());
        why="needs";for(auto v:need)why+=" "+std::to_string(v);why+=", has";for(size_t k=0;k<need.size();++k)why+=" "+std::to_string(have[k]+net[k]);
        for(size_t k=0;k<need.size();++k){
            if(need[k]<0||need[k]>Most){why+=" (nonsense)";return false;}
            int64_t left=int64_t(need[k])-have[k]-net[k];need[k]=left>0?int32_t(left):0;any=any||need[k]>0;
        }
        return any;
    };
    Levels l;
    l.raise=[&s,give,own,queue,lacking](uint64_t object,uint32_t level){
        Set need{};uint64_t t=0;std::string why;
        if(!lacking(object,level,need,why)){
            // Nothing lacking (the partner's deliveries arrived first): only the level-up is still to come.
            uint32_t now=0;uint64_t manager=0;
            if(why.rfind("needs",0)==0&&why.find("nonsense")==std::string::npos&&s.read(object+0x84,&now,4)&&now<level&&s.read(s.base+0x7bf4960,&manager,8)&&manager){
                queue(manager,object);return why+": already supplied, level-up queued";}
            return why+": nothing given";
        }
        if(!s.read(object+0x458,&t,8))return why+": nothing given";
        give(object,t,need.data(),1,nullptr);
        // A paver takes its level from the materials at once; other constructions go through the manager's
        // level-up queue, as after a delivery at the structure itself.
        uint32_t now=0;uint64_t manager=0;s.read(object+0x84,&now,4);
        if(now>=level)return why+": given, level now "+std::to_string(now);
        Set have{},net{};s.read(object+0x210,have.data(),sizeof have);s.read(object+0x248,net.data(),sizeof net);
        why+=", after giving has";for(size_t k=0;k<need.size();++k)why+=" "+std::to_string(have[k]+net[k]);
        Set still{};std::string unused;
        if(lacking(object,level,still,unused)){own(object,still.data());why+=" (not counted: delivered as this player)";}
        if(s.read(s.base+0x7bf4960,&manager,8)&&manager){queue(manager,object);return why+": level-up queued";}
        return why+": no manager";
    };
    auto construction=[&s](uint64_t object){uint64_t first=0,config=0;return object&&s.read(object,&first,8)&&first>=s.base&&s.read(object+0x90,&config,8)&&config;};
    l.give=[&s,give,construction](uint64_t object,const Set& mats){
        uint64_t t=0;if(!construction(object)||!sane(mats)||!s.read(object+0x458,&t,8))return;
        Set copy=mats;give(object,t,copy.data(),1,nullptr);
    };
    l.deliver=[own,construction](uint64_t object,const Set& mats){if(!construction(object)||!sane(mats))return;Set copy=mats;own(object,copy.data());};
    l.upgrade=[&s,own,queue,lacking](uint64_t object,uint32_t level){
        Set need{};uint64_t manager=0;std::string why;if(!lacking(object,level,need,why)||!s.read(s.base+0x7bf4960,&manager,8)||!manager)return why+": nothing delivered";
        own(object,need.data());queue(manager,object);return why+": delivered, level-up queued";
    };
    return l;
#else
    (void)s;throw std::runtime_error("native roads need the game");
#endif
}
}
