#include "groupreplay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace mod::groupreplay {
namespace {
std::string percent(float v){char b[32];std::snprintf(b,sizeof b,"%.1f",double(v));return b;}
}
bool replayMission(int64_t id){
    static const int64_t ids[]={320,500,615,630,20130,20430,20610,20615,20640,25130,25320,25430,25500,25610,25615,25630,25640};
    for(auto i:ids)if(i==id)return true;
    return false;
}
int64_t startKey(int64_t mission,uint16_t fight){return (mission&0xffff)<<16|fight;}
int64_t lifeKey(int64_t mission,uint16_t fight,uint8_t part){return (mission&0xffff)<<24|int64_t(fight)<<8|part;}
Group::Group(Settings s,std::function<uint16_t()> fight,std::function<void(const std::string&)> log)
    :settings_(s),next_(std::move(fight)),log_(log?std::move(log):[](const std::string&){}){}
void Group::heard(const Event& e,uint64_t now){
    if(e.kind=="replay"){
        const int64_t mission=e.key>>16;const uint16_t fight=uint16_t(e.key&0xffff);
        if(e.key<0||!replayMission(mission)||!fight||e.value<FirstCode||e.value>LastCode)return;
        auto& s=starts_[e.key];
        if(!s.heardAt){s.mission=mission;s.fight=fight;s.code=int32_t(e.value);++counters_.heard;
            log_("REPLAY another player started boss replay mission "+std::to_string(mission)+" (code "+std::to_string(e.value)+", fight "+std::to_string(fight)+")\n");}
        s.heardAt=now;return;}
    if(e.kind=="bosslife"){
        if(e.key<0||e.value<1||e.value>1000)return;
        auto it=lives_.find(e.key);
        if(it==lives_.end())lives_[e.key]={e.value,now};
        else{it->second.first=std::min(it->second.first,e.value);it->second.second=now;}}
}
Actions Group::tick(const Local& l,uint64_t now){
    Actions a;
    for(auto it=starts_.begin();it!=starts_.end();){if(now>it->second.heardAt+2*KeepMs)it=starts_.erase(it);else ++it;}   // forgotten well after
    for(auto it=lives_.begin();it!=lives_.end();){if(now>it->second.second+KeepMs)it=lives_.erase(it);else ++it;}
    const bool rose=primed_&&l.running&&!running_,fell=primed_&&!l.running&&running_;
    primed_=true;running_=l.running;
    // A fight here began: shared as the start the mod asked for, or said as this player's own.
    if(rose){
        mission_=0;fight_=0;fightAt_=now;said_.clear();
        if(pending_){
            auto s=starts_.find(*pending_);const int64_t wanted=*pending_>>16;const uint16_t fight=uint16_t(*pending_&0xffff);pending_.reset();
#ifndef MUTANT_GROUPREPLAY_WRONG_MISSION
            if(l.mission!=wanted){++counters_.mismatched;log_("REPLAY the replay started here is mission "+std::to_string(l.mission)+", not the group's "+std::to_string(wanted)+": fought alone\n");
                a.notices.push_back("This Boss Battle is not the one the other player started: it is fought alone.");}
            else
#endif
            {mission_=wanted;fight_=fight;++counters_.joined;log_("REPLAY joined the group's boss replay mission "+std::to_string(wanted)+" (fight "+std::to_string(fight)+")\n");}
            (void)s;
#ifdef MUTANT_GROUPREPLAY_SAY_JOINED
            if(l.code>=FirstCode&&l.code<=LastCode){a.say.push_back({"replay",startKey(wanted,fight),l.code});++counters_.said;}
#endif
        }else if(replayMission(l.mission)){
            uint16_t f=next_?next_():1;if(!f)f=1;
            mission_=l.mission;fight_=f;
            if(l.code>=FirstCode&&l.code<=LastCode){a.say.push_back({"replay",startKey(mission_,fight_),l.code});++counters_.said;
                log_("REPLAY this player started boss replay mission "+std::to_string(mission_)+" (code "+std::to_string(l.code)+"): told the others as fight "+std::to_string(fight_)+"\n");}
            else log_("REPLAY this player started boss replay mission "+std::to_string(mission_)+"; the menu code is unknown: not offered\n");}
        else log_("REPLAY a boss replay runs here; its mission is unknown: not shared\n");}
    if(fell){mission_=0;fight_=0;said_.clear();endedAt_=now;log_("REPLAY the boss replay here ended\n");}
    if(pending_&&!l.running&&now>pendingAt_+StartWaitMs){
        pending_.reset();++counters_.givenUp;log_("REPLAY the group's replay did not start here: given up\n");
        a.notices.push_back("The other player's Boss Battle could not be started here.");}
    // The life of the shared fight: said, and taken down to the lowest heard.
    if(l.running&&fight_){
        for(size_t i=0;i<l.lives.size()&&i<MaxParts;++i){
            const float mine=l.lives[i];if(!std::isfinite(mine))continue;
            const int64_t key=lifeKey(mission_,fight_,uint8_t(i));
            auto h=lives_.find(key);
#ifdef MUTANT_GROUPREPLAY_OTHER_FIGHT
            for(auto it=lives_.begin();it!=lives_.end();++it)if((it->first>>24)==(key>>24)&&(it->first&0xff)==(key&0xff)&&(h==lives_.end()||it->second.first<h->second.first))h=it;
#endif
            if(h!=lives_.end()){
                const float target=float(h->second.first)/10.f;
#ifndef MUTANT_GROUPREPLAY_UP
                if(target<mine-Least&&mine>Floor)
#else
                if(std::fabs(target-mine)>Least)
#endif
                {
#ifndef MUTANT_GROUPREPLAY_ZERO
                    const float to=std::max(target,Floor);
#else
                    const float to=target<=0.1f?0.f:target;
#endif
                    a.hurt.push_back({i,to});++counters_.written;
                    log_("REPLAY the boss's part "+std::to_string(i)+" taken from "+percent(mine)+" % to "+percent(to)+" % (the group's lowest)\n");continue;}}
            const int64_t permille=std::clamp<int64_t>(int64_t(std::ceil(double(mine)*10.0-1e-6)),1,1000);
            auto s=said_.find(uint8_t(i));
            const bool due=s==said_.end()||(s->second.first!=permille&&now>=s->second.second+SayEveryMs)||now>=s->second.second+RepeatMs;
            if(due){a.say.push_back({"bosslife",key,permille});said_[uint8_t(i)]={permille,now};++counters_.lifeSaid;}}}
    // Join a heard start: opted in, in the private room, no fight here, not just after one, nothing pending.
    const Start* best=nullptr;
    for(auto& kv:starts_){
        auto& s=kv.second;
        if(!s.told){s.told=true;
            a.notices.push_back(settings_.join?"Another player started a Boss Battle. Go to your private room within 5 minutes to join it.":"Another player started a Boss Battle.");}
#ifndef MUTANT_GROUPREPLAY_REJOIN
        if(s.used)continue;
#endif
#ifndef MUTANT_GROUPREPLAY_STALE
        if(now>s.heardAt+KeepMs)continue;
#endif
        if(!best||s.heardAt>best->heardAt)best=&s;}
    bool free=settings_.join&&l.available&&!pending_&&best;
#ifndef MUTANT_GROUPREPLAY_JOIN_ANYWHERE
    free=free&&l.inRoom;
#endif
#ifndef MUTANT_GROUPREPLAY_JOIN_IN_FIGHT
    free=free&&!l.running&&!l.boss&&(!endedAt_||now>=endedAt_+AfterFightMs);
#endif
    if(free){
        const int64_t key=startKey(best->mission,best->fight);starts_[key].used=true;pending_=key;pendingAt_=now;
        a.start=best->code;++counters_.started;
        log_("REPLAY starting the group's boss replay mission "+std::to_string(best->mission)+" with code "+std::to_string(best->code)+"\n");
        a.notices.push_back("Joining the other player's Boss Battle.");}
    return a;
}
}
