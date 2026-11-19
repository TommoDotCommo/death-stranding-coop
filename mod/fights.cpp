#include "fights.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mod::fights {
namespace bc=ds::broadcast;
namespace {
using ds::relay::detail::put;using ds::relay::detail::get;
constexpr float Floor=bts::HealthSync::Floor,Least=bts::HealthSync::Least;
std::string text(float v){char b[32];std::snprintf(b,sizeof b,"%.2f",double(v));return b;}
}
std::vector<uint8_t> encode(const State& s){
    std::vector<uint8_t> b;if(s.lives.size()>MaxCatchers)return b;
    b.push_back(1);b.push_back(uint8_t(s.phase));b.push_back(uint8_t(s.lives.size()));b.push_back(0);
    for(float l:s.lives)put(b,uint16_t(std::lround(std::clamp(std::isfinite(l)?l:0.f,0.f,100.f)*100)),2);
    return b;}
std::optional<State> decodeState(const std::vector<uint8_t>& b){
    if(b.size()<4||b[0]!=1||b[1]<1||b[1]>3||b[2]>MaxCatchers||b[3]||b.size()!=4+2*size_t(b[2]))return std::nullopt;
    State s;s.phase=Phase(b[1]);
    for(size_t i=0;i<b[2];++i){auto v=get(b.data()+4+2*i,2);if(v>10000)return std::nullopt;s.lives.push_back(float(v)/100.f);}
    return s;}
bool Report::operator==(const Report& o) const {return killed==o.killed&&caught==o.caught&&damage==o.damage&&gazers==o.gazers;}
std::vector<uint8_t> encode(const Report& r){
    std::vector<uint8_t> b;if(r.damage.size()>MaxCatchers||r.gazers.size()>MaxGazers)return b;
    b.push_back(1);b.push_back(r.killed?1:0);b.push_back(uint8_t(r.damage.size()));b.push_back(uint8_t(r.gazers.size()));put(b,r.caught,2);put(b,0,2);
    for(float d:r.damage)put(b,uint32_t(std::lround(std::clamp(std::isfinite(d)?d:0.f,0.f,1000000.f)*1000)),4);
    for(auto k:r.gazers)put(b,k,8);
    return b;}
std::optional<Report> decodeReport(const std::vector<uint8_t>& b){
    if(b.size()<8||b[0]!=1||b[1]>1||b[2]>MaxCatchers||b[3]>MaxGazers||get(b.data()+6,2)||b.size()!=8+4*size_t(b[2])+8*size_t(b[3]))return std::nullopt;
    Report r;r.killed=b[1]!=0;r.caught=uint16_t(get(b.data()+4,2));size_t o=8;
    for(size_t i=0;i<b[2];++i,o+=4){auto v=get(b.data()+o,4);if(v>1000000000)return std::nullopt;r.damage.push_back(float(v)/1000.f);}
    for(size_t i=0;i<b[3];++i,o+=8){auto k=get(b.data()+o,8);if(k>=(uint64_t(1)<<52))return std::nullopt;r.gazers.push_back(k);}
    return r;}

Fights::Fights(Settings s,Api a,std::function<void(const std::string&)> log):settings_(s),api_(std::move(a)),log_(log?std::move(log):[](const std::string&){}){
    settings_.most=uint8_t(std::clamp<unsigned>(settings_.most,1,bc::FightVisitorsMax));}
void Fights::tick(const Local& l,bc::Client& client,uint64_t now){
    bool readable=l.fights.readable,active=readable&&l.fights.active>0;
    rose_=primed_&&readable&&active&&!active_;fell_=primed_&&readable&&!active&&active_;won_=primed_&&readable&&l.fights.cleared>cleared_;
    catchers_=active?l.catchers:std::vector<bts::Catcher>{};
    news(l,client,now);
    if(role_==Role::Visitor)observeVisit(l,client,now);
    if(fell_)copy_=false;   // a shown copy that ended is gone: the next fight here is this game's own again
    host(l,client,now);
    if(role_==Role::None)visit(l,client,now);
    if(readable){primed_=true;active_=active;cleared_=l.fights.cleared;}
}
// ---- The owner -------------------------------------------------------------------------------------------------------
void Fights::host(const Local& l,bc::Client& client,uint64_t now){
    auto post=[&]{
        State s;s.phase=phase_;
        if(phase_==Phase::Running)for(size_t i=0;i<catchers_.size()&&i<MaxCatchers;++i)s.lives.push_back(catchers_[i].percent);
        if(client.postFight(encode(s)))++counters_.states;
        stateAt_=now;};
    if(role_==Role::Owner){
        if(phase_==Phase::Running&&fell_){
            phase_=won_||claimed_?Phase::Cleared:Phase::Lost;endAt_=now;post();
            log_(std::string("FIGHT this game's fight ended ")+(phase_==Phase::Cleared?"with the area cleared":"without a clear")+": the visitors are told\n");}
        if(phase_!=Phase::Running&&now>=endAt_+LingerMs){
            client.closeFight();log_("FIGHT closed ("+std::to_string(visitors_.size())+" visitors reported)\n");
            role_=Role::None;owner_=0;fight_=0;visitors_.clear();claimed_=false;return;}
        client.hostFight(info_,now);
        if(now>=stateAt_+StateEveryMs)post();
        return;}
    if(!settings_.host)return;
#ifndef MUTANT_FIGHT_VISITOR_HOSTS
    // A visitor never announces a fight, nor a game whose fight is a copy the mod started for one.
    if(role_!=Role::None||copy_)return;
#endif
    if(!rose_||!l.available)return;
#ifndef MUTANT_FIGHT_BOSS_OFFERED
    // A story boss runs on the catcher records too (whale, titans), but a visitor's copy would be an ordinary catcher.
    if(l.boss){if(!bossNoted_){bossNoted_=true;log_("FIGHT a story boss fight runs here: not offered to the players near\n");}return;}
#endif
    if(role_==Role::Visitor)release(client,now,"this game owns a fight now",true);
    if(++nextFight_==0)nextFight_=1;
    info_=bc::FightInfo{};info_.fight=nextFight_;info_.kind=Catcher;info_.most=settings_.most;info_.radius=uint16_t(bts::FightRange);info_.place=l.at;
    role_=Role::Owner;owner_=client.id();fight_=info_.fight;phase_=Phase::Running;visitors_.clear();claimed_=false;
    client.hostFight(info_,now);++counters_.hosted;post();
    log_("FIGHT a catcher fight runs here: offered to the players near as fight "+std::to_string(fight_)+"\n");
}
// A visitor's report: what is new since his last one, checked against this game's fight.
void Fights::report(const bc::FightNews& n,const Local& l){
    auto r=decodeReport(n.bytes);if(!r){++counters_.malformed;return;}
    ++counters_.reportsTaken;auto& v=visitors_[n.source];
    for(size_t i=0;i<r->damage.size();++i){
        if(v.damage.size()<=i)v.damage.resize(i+1,0.f);
#ifndef MUTANT_FIGHT_DOUBLE
        float due=r->damage[i]-v.damage[i];
#else
        float due=r->damage[i];
#endif
        v.damage[i]=std::max(v.damage[i],r->damage[i]);
        if(!(due>Least)||i>=catchers_.size())continue;
        float was=catchers_[i].percent;if(was<=Floor)continue;
        float next=std::max(was-due,Floor);
        api_.hurt(catchers_[i],next);catchers_[i].percent=next;++counters_.writes;++counters_.damageApplied;
        log_("FIGHT visitor "+std::to_string(n.source)+" took "+text(due)+" % off catcher "+std::to_string(i)+": now "+text(next)+" %\n");}
    for(auto k:r->gazers){
        if(!v.gazers.insert(k).second)continue;
        if(auto* g=bts::GazerSync::wanted(l.gazers,k)){api_.destroy(g->address);++counters_.gazersApplied;log_("FIGHT visitor "+std::to_string(n.source)+" destroyed a gazer: gone here too\n");}}
    if(r->caught>v.caught){v.caught=r->caught;log_("FIGHT visitor "+std::to_string(n.source)+" was caught\n");}
    if(r->killed&&!v.killed){
        v.killed=true;
        bool floor=false;for(auto& c:catchers_)floor=floor||c.percent<=Floor+Least;
#ifdef MUTANT_FIGHT_KILL_CLAIM
        floor=!catchers_.empty();
#endif
        // His hit killed his copy: the fight ends cleared here only if this game's catcher is at the floor too.
        if(floor&&!claimed_&&active_){api_.command(1);++counters_.commands;claimed_=true;++counters_.killsTaken;log_("FIGHT visitor "+std::to_string(n.source)+" landed the last hit: the fight ends here with the area cleared\n");}
        else{++counters_.killsRefused;log_("FIGHT visitor "+std::to_string(n.source)+" says his hit killed his catcher; this game's is not at the floor: not taken\n");}}
}
// ---- The visitor -----------------------------------------------------------------------------------------------------
void Fights::visit(const Local& l,bc::Client& client,uint64_t now){
    if(!settings_.join||copy_||!l.available||l.dead||l.boss)return;
    const bc::Offer* best=nullptr;double bestD=0;auto offers=client.fightOffers(now);
    for(auto& o:offers){
        auto left=leftAt_.find({o.owner,o.info.fight});if(left!=leftAt_.end()&&now<left->second+RejoinQuietMs)continue;
        if(o.info.kind!=Catcher)continue;
        double d=0;for(unsigned i=0;i<3;++i)d+=(o.info.place[i]-l.at[i])*(o.info.place[i]-l.at[i]);
        if(!best||d<bestD){best=&o;bestD=d;}}
    if(!best)return;
#ifndef MUTANT_FIGHT_OWN
    // A fight of this game's own runs here: it comes first; the owner's is not joined.
    if(l.fights.readable&&l.fights.active>0){
        if(!ownTold_){ownTold_=true;++counters_.ownFirst;log_("FIGHT a fight is offered, but this game runs one of its own: not joined\n");}
        return;}
#endif
    ownTold_=false;
    if(!client.joinFight(best->owner,best->info.fight,now))return;
    // A visit to the fight visited last goes on counting from where it was (the owner takes what is new only).
    bool again=lastVisit_==std::make_pair(best->owner,best->info.fight);
    role_=Role::Visitor;owner_=best->owner;fight_=best->info.fight;joined_=false;commanded_=shownOnce_=false;state_.reset();
    if(!again){mine_=Report{};sent_=Report{};}
    lastVisit_={owner_,fight_};sentOnce_=false;last_.clear();caught_=l.caught;alive_.clear();
    for(auto& g:l.gazers)alive_[bts::key(g.uuid)]=g.life>0;
    log_("FIGHT joining the fight of player "+std::to_string(owner_)+"\n");
}
void Fights::news(const Local& l,bc::Client& client,uint64_t now){
    for(auto& n:client.takeFightNews()){
        if(!n.delivered){
            auto s=n.status.status;
            if(role_==Role::Owner){
                if(s==bc::FightStatus::Off&&!offTold_){offTold_=true;log_("FIGHT the server takes no fights\n");}
                if(s==bc::FightStatus::Open&&n.status.visitors!=visitorsTold_){visitorsTold_=n.status.visitors;log_("FIGHT "+std::to_string(visitorsTold_)+" visitors\n");}
                continue;}
            if(role_!=Role::Visitor||n.status.owner!=owner_||n.status.fight!=fight_)continue;
            if(s==bc::FightStatus::Joined){if(!joined_){joined_=true;++counters_.joined;log_("FIGHT visiting the fight of player "+std::to_string(owner_)+"\n");}continue;}
            static const char* why[]={"","","","full","no such fight","out of reach","busy","closed by its owner","its owner left","out of reach","left","no fights here"};
            release(client,now,why[std::min<size_t>(size_t(s),11)],false);
            continue;}
        // The client takes a DELIVER only for this player's role and from the right source (broadcast_client.h).
        if(role_==Role::Owner){report(n,l);continue;}
        if(role_!=Role::Visitor||!joined_)continue;
        auto s=decodeState(n.bytes);if(!s){++counters_.malformed;continue;}
        ++counters_.statesTaken;state_=*s;applyState(*s,l,client,now);
    }
}
void Fights::applyState(const State& s,const Local& l,bc::Client& client,uint64_t now){
    bool active=l.fights.readable&&l.fights.active>0;
    if(s.phase==Phase::Running){
        if(!copy_){
            if(shownOnce_)return;   // one copy a visit: his copy ended (his kill, or he got away) and the owner's goes on
            if(active){++counters_.ownFirst;release(client,now,"a fight of this game's own runs here",true);return;}
            api_.command(0);++counters_.commands;copy_=shownOnce_=true;copyAt_=now;
            log_("FIGHT the owner's fight runs: a catcher fight is started here to show it\n");
            return;}
        // The copy's catchers down to the owner's life: never up, never below the floor, and not counted as this
        // porter's damage.
        if(last_.empty())last_.assign(catchers_.size(),100.f);
        for(size_t i=0;i<s.lives.size()&&i<catchers_.size();++i){
#ifndef MUTANT_FIGHT_KILL
            float want=std::max(s.lives[i],Floor);
#else
            float want=s.lives[i];
#endif
#ifndef MUTANT_FIGHT_HEAL
            if(!(catchers_[i].percent>want+Least))continue;
#else
            if(std::fabs(catchers_[i].percent-want)<=Least)continue;
#endif
            api_.hurt(catchers_[i],want);catchers_[i].percent=want;++counters_.writes;
#ifndef MUTANT_FIGHT_ECHO
            if(i<last_.size())last_[i]=want;
#endif
        }
        return;}
    if(s.phase==Phase::Cleared&&copy_&&active&&!commanded_){
        api_.command(1);++counters_.commands;commanded_=true;log_("FIGHT the owner's area is cleared: the fight shown here ends the same way\n");}
    release(client,now,s.phase==Phase::Cleared?"the owner's fight was won":"the owner's fight was lost",true);
}
// What this porter did in the fight he visits, since he joined: sent to the owner when it changes and again every second.
void Fights::observeVisit(const Local& l,bc::Client& client,uint64_t now){
    if(!joined_)return;
    if(copy_){
        // The copy starts at full life: a hit before the first look that saw it counts too. Not readable or gone this
        // look: what it had is kept (its last hit, below). Another number of catchers: learnt.
        if(last_.empty())last_.assign(catchers_.size(),100.f);
        if(catchers_.empty()){}
        else if(catchers_.size()!=last_.size()){last_.clear();for(auto& c:catchers_)last_.push_back(c.percent);}
        else for(size_t i=0;i<catchers_.size()&&i<MaxCatchers;++i){
            float p=catchers_[i].percent;
            if(p<last_[i]-Least){if(mine_.damage.size()<=i)mine_.damage.resize(i+1,0.f);mine_.damage[i]+=last_[i]-p;}
            last_[i]=p;}
        // His copy ended with a cleared sphere the mod did not ask for: his own hit killed it, taking what life it had.
        if(fell_&&won_&&!commanded_&&!mine_.killed){
            mine_.killed=true;
            for(size_t i=0;i<last_.size()&&i<MaxCatchers;++i){if(mine_.damage.size()<=i)mine_.damage.resize(i+1,0.f);mine_.damage[i]+=last_[i];}
            last_.clear();}
    }
    for(auto& g:l.gazers){
        uint64_t k=bts::key(g.uuid);bool alive=g.life>0;auto it=alive_.find(k);
        if(it!=alive_.end()&&it->second&&!alive&&!bts::dynamic(g.flags)&&mine_.gazers.size()<MaxGazers&&std::find(mine_.gazers.begin(),mine_.gazers.end(),k)==mine_.gazers.end())mine_.gazers.push_back(k);
        alive_[k]=alive;}
    if(l.caught&&!caught_&&mine_.caught<0xffff)++mine_.caught;
    caught_=l.caught;
    bool changed=!sentOnce_||!(mine_==sent_);
    if((changed&&now>=reportAt_+ReportEveryMs)||now>=reportAt_+ReportAgainMs){
        if(client.postFight(encode(mine_))){sent_=mine_;sentOnce_=true;reportAt_=now;++counters_.reports;}}
}
void Fights::release(bc::Client& client,uint64_t now,const char* why,bool tellServer){
    if(role_!=Role::Visitor)return;
    if(tellServer)client.leaveFight();
    leftAt_[{owner_,fight_}]=now;
    for(auto it=leftAt_.begin();it!=leftAt_.end();)if(now>=it->second+RejoinQuietMs)it=leftAt_.erase(it);else ++it;
#ifdef MUTANT_FIGHT_GONE_CLEARS
    if(copy_&&!commanded_){api_.command(1);++counters_.commands;}
#endif
    log_("FIGHT the visit ends ("+std::string(why)+"): nothing more is written here\n");
    ++counters_.released;
#ifndef MUTANT_FIGHT_RELEASE
    role_=Role::None;joined_=false;owner_=0;fight_=0;state_.reset();
#endif
}

// ---- Sandbox ---------------------------------------------------------------------------------------------------------
void Sandbox::start(size_t n){if(active_)return;active_=true;lives_.assign(n,100.f);}
void Sandbox::hit(size_t i,float p){
    if(!active_||i>=lives_.size())return;
    lives_[i]-=p;if(lives_[i]<=0){active_=false;lives_.clear();++cleared_;}}
void Sandbox::end(bool cleared){if(!active_)return;active_=false;lives_.clear();if(cleared)++cleared_;}
void Sandbox::gazer(uint64_t key,float life){gazers_[key&((uint64_t(1)<<52)-1)]=life;}
void Sandbox::kill(uint64_t key){auto it=gazers_.find(key);if(it!=gazers_.end())it->second=0;}
float Sandbox::life(uint64_t key) const {auto it=gazers_.find(key);return it==gazers_.end()?-1.f:it->second;}
Local Sandbox::local(const std::array<double,3>& at,bool available) const {
    Local l;l.fights.readable=true;l.fights.active=active_?1:0;l.fights.cleared=cleared_;
    for(size_t i=0;i<lives_.size();++i){bts::Catcher c;c.life=0x1000+i;c.percent=lives_[i];l.catchers.push_back(c);}
    for(auto& [k,life]:gazers_){bts::Gazer g;g.address=0x100000+k;std::memcpy(g.uuid.data(),&k,8);g.life=life;l.gazers.push_back(g);}
    l.available=available;l.at=at;l.dead=dead;l.caught=caught;l.boss=boss;return l;}
Api Sandbox::api(){
    Api a;
    a.command=[this](uint8_t c){commands.push_back(c);if(c==0)start(1);else if(c==1)end(true);};
    // A life of zero or less written from outside makes the catcher unkillable (game_offsets.md, "Catcher life").
    a.hurt=[this](const bts::Catcher& c,float p){size_t i=size_t(c.life-0x1000);if(!active_||i>=lives_.size())return;lives_[i]=p;++writes;if(!(p>0))++unkillable;};
    a.destroy=[this](uint64_t address){uint64_t k=address-0x100000;auto it=gazers_.find(k);if(it!=gazers_.end()&&it->second>0){it->second=0;destroyed.push_back(k);}};
    return a;}
}
