#include "broadcast_client.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ds::broadcast {
namespace {
constexpr uint64_t LiveMs=3000,TeleportMs=5000,RebaseMs=1000,FetchGraceMs=300,FetchEveryMs=500;
constexpr uint32_t IntervalCapMs=250,IntervalMarginMs=25;
constexpr size_t Buffered=64,ClockSamples=16;
// The core's checksum over a 128-byte frame (bytes 120..123 counted as zero).
void checksum(Bytes& b){uint32_t h=2166136261u;for(size_t i=0;i<WireSize;++i){h^=(i>=120&&i<124)?0:b[i];h*=16777619u;}for(unsigned i=0;i<4;++i)b[120+i]=uint8_t(h>>(8*i));}
// The block a PUBLISH carries, from the core's own encoder (which clamps to the wire's envelope).
Presence blockOf(const ds::State& s){Packet q;q.kind=ds::Kind::Presence;q.state=s;auto b=encode(q);return presenceOf(b.data());}
// The core's decoder over a frame rebuilt around a block: every value is checked as the core checks a PRESENCE.
ds::Error stateOf(const uint8_t* block,uint32_t sequence,uint64_t stamp,ds::State& out){
    Packet q;q.kind=ds::Kind::Presence;q.sequence=sequence;q.stamp=stamp;q.echo=1;
    Bytes b;try{b=encode(q);}catch(const std::invalid_argument&){return ds::Error::Time;}
    presenceInto(block,b.data());checksum(b);
    Packet p;auto e=decode(b.data(),b.size(),p);if(e!=ds::Error::None)return e;
    out=p.state;return ds::Error::None;}
double distance(const ds::State& a,const ds::State& b){double d=0;for(int i=0;i<3;++i)d+=(a.position[i]-b.position[i])*(a.position[i]-b.position[i]);return std::sqrt(d);}
// What the core counts as a change worth sending (Core::tick).
bool differs(const ds::State& s,const ds::State& t){
    return distance(s,t)>=0.01||std::abs(s.yaw-t.yaw)>=0.01||s.flags!=t.flags||s.action!=t.action||std::abs(s.phase-t.phase)>=1./255.||s.health!=t.health||s.stamina!=t.stamina
        ||s.velocity!=t.velocity||s.vehicle!=t.vehicle||s.vehicleOffset!=t.vehicleOffset||s.vehicleAngles!=t.vehicleAngles||std::abs(s.steer-t.steer)>=0.5||s.vehicleVariant!=t.vehicleVariant||s.vehicleLook!=t.vehicleLook;}
template<class T> T percentile(std::deque<T> v,unsigned p){std::sort(v.begin(),v.end());return v[(v.size()-1)*p/100];}
}
Client::Client(ClientConfig c,Send send):config_(std::move(c)),send_(std::move(send)){
    relay::detail::need(config_.id!=0,"broadcast: player id 0");relay::detail::need(relay::tokenText(config_.token),"broadcast: token");
    relay::detail::need(relay::nicknameText(config_.nickname),"broadcast: nickname");relay::detail::need(config_.helloMs>0,"broadcast: hello interval");
    relay::detail::need(config_.delayMs<=1000,"broadcast: delay");relay::detail::need(config_.show<=Show::Nobody,"broadcast: show");
}
bool Client::setPosition(const std::array<double,3>& p,uint32_t area){
    area_=area;if(!relay::positionValid(p)){have_=false;return false;}
    position_=p;have_=true;return true;}
void Client::clearPosition(){have_=false;}
void Client::setLocal(const ds::State& s,bool available,bool gated,uint64_t sampledAt,bool jumped){
    local_=s;local_.flags&=uint16_t(~Teleport);available_=available;gated_=gated;sampledAt_=sampledAt;
    if(available&&jumped)jump_=true;}
bool Client::offerAnimation(const Animation& a,uint64_t now){
    if(!online_||near_.empty()||gated_)return false;
    if(sentAnimationOnce_&&(now<animationAt_+50||(a==sentAnimation_&&now<animationAt_+500)))return false;
    Animation next=a;
#ifndef MUTANT_BCAST_CLIENT_ROSE
    // A sample not yet sent is replaced; its pulses go with the new one.
    if(pendingAnimation_)for(size_t k=0;k<AnimationBoolBytes;++k)next.rose[k]|=pendingAnimation_->rose[k];
#endif
    pendingAnimation_=next;sentAnimation_=a;sentAnimationOnce_=true;animationAt_=now;return true;}
bool Client::setState(StateKind kind,const std::vector<uint8_t>& bytes){
    size_t k=size_t(kind)-1;if(k>=StateKinds||bytes.empty()||bytes.size()>StateMax)return false;
    if(mine_[k]&&state_[k]==bytes)return true;
    state_[k]=bytes;if(++mine_[k]==0)mine_[k]=1;
    if(online_)sendState(k);
    return true;}
void Client::sendDatagram(const Buffer& b){++counters_.sent;counters_.sentBytes+=b.size();send_(b.data(),b.size());}
void Client::sendState(size_t k){
    StatePost s;s.player=config_.id;s.kind=StateKind(k+1);s.version=mine_[k];s.payload=state_[k].data();s.size=state_[k].size();
    sendDatagram(encode(s));++counters_.states;}
uint64_t Client::serverNow(uint64_t now) const {
#ifndef MUTANT_BCAST_CLIENT_CLOCK
    if(!clockValid_)return now;
    double t=double(now)+offset_;return t<=0?0:uint64_t(std::llround(t));
#else
    return now;
#endif
}
void Client::sendHello(uint64_t now){
    Hello h;h.base.id=config_.id;h.base.token=config_.token;h.base.nickname=config_.nickname;h.base.sequence=++helloSequence_;
    h.base.stream=stream_;h.base.ack=applied_;h.base.area=area_;h.base.havePosition=have_;if(have_)h.base.position=position_;
    h.game=config_.game;h.build=config_.build;h.t0=std::min<uint64_t>(now,1ull<<53);h.show=config_.show;
    helloTimes_.push_back({h.base.sequence,h.t0});while(helloTimes_.size()>ClockSamples)helloTimes_.pop_front();
    sendDatagram(encode(h));sentHello_=true;nextHello_=now+config_.helloMs;}
void Client::tick(uint64_t now){
    // A tick time earlier than the last WELCOME's is no silence (never an unsigned difference).
    if(online_&&now>lastWelcome_&&now-lastWelcome_>config_.silentMs)offline();
    if(!sentHello_||now>=nextHello_)sendHello(now);
    publish(now);
    for(auto& [id,s]:sources_){
        if(!s.wantFetch||now<s.mismatchSince+FetchGraceMs||now<s.fetchAt)continue;
        Fetch f;f.player=config_.id;f.source=id;sendDatagram(encode(f));++counters_.fetches;s.fetchAt=now+FetchEveryMs;}
}
void Client::publish(uint64_t now){
    if(!online_||near_.empty()||!clockValid_)return;
    Update u;u.player=config_.id;
    Presence block{};ds::State s=local_;
    bool presenceDue=false;
    if(available_&&!gated_&&sanitise(s)&&sampledAt_<=now&&(!haveSentPresence_||sampledAt_>lastSample_)){
#ifndef MUTANT_BCAST_CLIENT_JUMP
        if(jump_)s.flags|=Teleport;
#endif
        bool changed=!haveSentPresence_||differs(s,sentState_);
        presenceDue=now>=lastPresenceAt_+50&&(changed||now>=lastPresenceAt_+500);
    }
    if(!presenceDue&&!pendingAnimation_)return;
    std::vector<uint8_t> animation;
    if(pendingAnimation_){
        try{animation=encodeAnimationPayload(*pendingAnimation_);}catch(const std::invalid_argument&){animation.clear();}
        if(animation.size()>AnimationMax)animation.clear();   // a sample the wire cannot carry is left out
        pendingAnimation_.reset();}
    if(!presenceDue&&animation.empty())return;
    uint64_t sample=presenceDue?sampledAt_:std::min(sampledAt_,now);
    u.stamp=std::max<uint64_t>(lastStamp_+1,serverNow(sample));
    if(u.stamp>(1ull<<53))return;
    u.sequence=++sequence_;
    if(presenceDue){block=blockOf(s);u.parts|=HasPresence;u.presence=block.data();}
    if(!animation.empty()){u.parts|=HasAnimation;u.animation=animation.data();u.animationSize=animation.size();++counters_.animations;}
    sendDatagram(encodePublish(u));++counters_.published;lastStamp_=u.stamp;
    if(presenceDue){++counters_.presences;sentState_=s;haveSentPresence_=true;lastPresenceAt_=now;lastSample_=sampledAt_;jump_=false;}
}
void Client::bye(){
    relay::Bye y;y.id=config_.id;y.token=config_.token;sendDatagram(encodeBye(y));offline();sentHello_=false;}
std::vector<relay::RosterEvent> Client::takeEvents(){auto e=std::move(events_);events_.clear();return e;}
bool Client::answer(uint32_t sequence){
    if(int32_t(sequence-lastAnswer_)<=0||int32_t(helloSequence_-sequence)<0)return false;
    lastAnswer_=sequence;return true;}
void Client::leaveAll(){for(auto& p:near_)events_.push_back({false,p.first,p.second});near_.clear();sources_.clear();}
void Client::offline(){leaveAll();online_=false;stream_=0;applied_=0;clockValid_=false;pongs_.clear();lastStamp_=0;}
void Client::clockSample(uint64_t t0,uint64_t serverTime,uint64_t now){
    if(now<t0||now-t0>2000)return;
    double rtt=double(now-t0),candidate=double(serverTime)-(double(t0)+double(now))/2.;
    rtt_=rtt;
    if(!clockValid_){offset_=candidate;clockValid_=true;offsetAt_=now;}
    pongs_.push_back({rtt,candidate});while(pongs_.size()>ClockSamples)pongs_.pop_front();
    auto best=std::min_element(pongs_.begin(),pongs_.end());double step=ClockSlew*double(now>offsetAt_?now-offsetAt_:0);
    offset_=std::clamp(best->second,offset_-step,offset_+step);offsetAt_=now;}
void Client::receive(const uint8_t* data,size_t size,uint64_t now){
    ++counters_.received;counters_.receivedBytes+=size;
    Message m;if(parse(data,size,m)!=Error::None){++counters_.malformed;return;}
    switch(m.type){
    case Type::Welcome:{
        auto& w=m.welcome;if(w.base.id!=config_.id||!answer(w.base.sequence))return;
        // Only the WELCOME of a HELLO this side sent, with the time it was sent, is a clock sample.
        auto sent=std::find_if(helloTimes_.begin(),helloTimes_.end(),[&](const auto& p){return p.first==w.base.sequence;});
        online_=true;lastWelcome_=now;refusal_=Refusal::None;welcome_=w;
#ifndef MUTANT_BCAST_CLIENT_STREAM
        // Another stream: the server began again from nothing for this player (and may be another process: its clock too).
        if(w.base.stream!=stream_){leaveAll();stream_=w.base.stream;applied_=0;clockValid_=false;pongs_.clear();lastStamp_=0;}
#else
        stream_=w.base.stream;
#endif
        if(sent!=helloTimes_.end()&&sent->second==w.t0)clockSample(w.t0,w.serverTime,now);
        // The state the server holds of this porter: what it does not have yet goes again.
        for(size_t k=0;k<StateKinds;++k){
#ifndef MUTANT_BCAST_CLIENT_RESEND
            if(mine_[k]&&w.versions[k]!=mine_[k])sendState(k);
#endif
        }
        return;}
    case Type::Refused:
        if(m.refused.id==config_.id&&answer(m.refused.sequence))refusal_=m.refused.reason;
        return;
    case Type::Roster:{
        auto& r=m.roster;if(!online_||r.stream!=stream_)return;
        for(size_t i=0;i<r.events.size();++i){
            if(r.first+uint32_t(i)!=applied_+1)continue;
            ++applied_;auto& e=r.events[i];
            if(e.id==config_.id)continue;
            if(e.enter){if(near_.emplace(e.id,e.nickname).second){events_.push_back(e);auto& s=sources_[e.id];s.nickname=e.nickname;s.delay=config_.delayMs;}}
            else if(near_.erase(e.id)){events_.push_back(e);sources_.erase(e.id);}}
        nextHello_=now;   // acknowledge now, so the server stops repeating
        return;}
    case Type::Frame:
        ++counters_.frames;
        for(auto& u:m.frame.entries)entry(u,now);
        return;
    case Type::Share:share(m.share,now);return;
    default:++counters_.malformed;return;   // the player's own message types are not for the player
    }
}
void Client::entry(const Update& u,uint64_t now){
    ++counters_.entries;
    auto it=sources_.find(u.player);
#ifdef MUTANT_BCAST_CLIENT_STRANGER
    if(it==sources_.end()){sources_[u.player].delay=config_.delayMs;it=sources_.find(u.player);}
#endif
    if(it==sources_.end()){++counters_.unknown;return;}
    auto& s=it->second;
#ifndef MUTANT_BCAST_CLIENT_ORDER
    if(s.haveEntry&&(!later(u.sequence,s.sequence)||u.stamp<=s.stamp)){++counters_.refused;return;}
#endif
    uint64_t at=serverNow(now);
    if(!clockValid_||(u.stamp>at?u.stamp-at:at-u.stamp)>2000){++counters_.refused;return;}
    s.haveEntry=true;s.sequence=u.sequence;
    s.stamp=u.stamp;
    // A later generation than this side has: the state is asked for again, unless a SHARE that is on its way settles it.
    // An earlier one is an entry the server queued before the state changed (its SHARE went at once).
#ifndef MUTANT_BCAST_CLIENT_GAP
    if(later16(u.generation,s.generation)&&!s.wantFetch){s.wantFetch=true;s.mismatchSince=now;}
#endif
    if(u.parts&HasPresence){
        ds::State state;
        if(stateOf(u.presence,u.sequence,u.stamp,state)!=ds::Error::None){++counters_.malformed;}
        else{
            bool teleport=(state.flags&Teleport)!=0,take=true;
#ifndef MUTANT_BCAST_CLIENT_TELEPORT
            if(teleport&&s.haveTeleport&&(now<s.teleportAt||now-s.teleportAt<TeleportMs))take=false;
#endif
#ifndef MUTANT_BCAST_CLIENT_SPEED
            if(take&&!teleport&&!s.snapshots.empty()){
                double elapsed=double(u.stamp-s.snapshots.back().time)/1000.;
                if(distance(state,s.snapshots.back().state)>100*std::min(elapsed,2.)+0.5){
                    // Where the core would have timed out and paired again from nothing: a fresh baseline after a second.
                    if(!s.refusing){s.refusing=true;s.refusingSince=u.stamp;take=false;}
#ifndef MUTANT_BCAST_CLIENT_REBASE
                    else if(u.stamp-s.refusingSince<RebaseMs)take=false;
#else
                    else take=false;
#endif
                    if(take){s.snapshots.clear();s.lateness.clear();s.intervals.clear();++counters_.rebased;}
                }
            }
#endif
            if(!take)++counters_.refused;
            else{
                if(!s.snapshots.empty())s.intervals.push_back(uint32_t(std::min<uint64_t>(u.stamp-s.snapshots.back().time,60000)));
                s.snapshots.push_back(Snapshot{state,u.stamp,u.sequence});while(s.snapshots.size()>Buffered)s.snapshots.pop_front();
                s.lateness.push_back(int32_t(std::clamp(double(at)-double(u.stamp),-2000.,2000.)));
                while(s.lateness.size()>DelayWindow)s.lateness.pop_front();
                while(s.intervals.size()>DelayWindow)s.intervals.pop_front();
                adaptDelay(s,now);
                s.refusing=false;s.acceptedAt=now;s.everAccepted=true;++counters_.accepted;
                if(teleport){s.haveTeleport=true;s.teleportAt=now;}
            }
        }
    }
    if(u.parts&HasAnimation){
        Animation a;
        if(!decodeAnimationPayload(u.animation,u.animationSize,a))++counters_.malformed;
        else{s.animations.push_back({a,u.stamp,u.sequence});while(s.animations.size()>Buffered)s.animations.pop_front();++counters_.animationsTaken;}
    }
}
void Client::share(const Share& h,uint64_t now){
    auto it=sources_.find(h.source);
    if(it==sources_.end()){++counters_.unknown;return;}
    ++counters_.shares;auto& s=it->second;size_t k=size_t(h.kind)-1;auto& blob=s.blobs[k];
    // Older than what this side holds of the kind, or than the generation it has complete: a SHARE that arrived late,
    // which neither changes the state nor says anything about what is missing.
#ifndef MUTANT_BCAST_CLIENT_STALE_STATE
    if((blob.have&&later16(blob.generation,h.generation))||later16(s.generation,h.generation))return;
#endif
    if(!h.version){if(blob.have){uint64_t revision=blob.revision+1;blob=Blob{};blob.revision=revision;}}
    else{
        std::vector<uint8_t> bytes(h.payload,h.payload+h.size);
        uint64_t revision=blob.revision;
        if(!blob.have||blob.bytes!=bytes)++revision;
        blob.bytes=std::move(bytes);blob.version=h.version;blob.generation=h.generation;blob.have=true;blob.revision=revision;}
    Versions held{};for(size_t i=0;i<StateKinds;++i)held[i]=s.blobs[i].have?s.blobs[i].version:0;
    if(held==h.versions){if(!later16(s.generation,h.generation))s.generation=h.generation;s.wantFetch=false;}
    else if(!s.wantFetch){s.wantFetch=true;s.mismatchSince=now;}
}
void Client::adaptDelay(Source& s,uint64_t now){
    double least=config_.delayMs,most=std::max<double>(config_.delayMs,DelayMaxMs),target=least;
#ifndef MUTANT_BCAST_CLIENT_DELAY
    if(!s.lateness.empty()){
        // A player who is sent every few hundred milliseconds (beyond the server's full distance) needs his next sample
        // before the shown time reaches it: the margin covers the longer of two send intervals and one interval (capped:
        // a player who stands still is refreshed only every half second, and nothing moves to interpolate then).
        double interval=s.intervals.empty()?0:double(std::min<uint32_t>(percentile(s.intervals,95),IntervalCapMs)+IntervalMarginMs);
        target=std::clamp(double(percentile(s.lateness,95))+std::max<double>(DelayMarginMs,interval),least,most);
    }
#endif
    double step=s.lateness.size()<=DelayWarmup?most:DelaySlew*double(now>s.delayAt?now-s.delayAt:0);
    s.delay=std::clamp(target,s.delay-step,s.delay+step);s.delay=std::clamp(s.delay,least,most);s.delayAt=now;}
Render Client::render(uint64_t source,uint64_t now) const {
    auto it=sources_.find(source);if(it==sources_.end())return {};
    return sample(it->second.snapshots,serverNow(now),uint32_t(std::lround(it->second.delay)),150);}
uint32_t Client::presentationDelayMs(uint64_t source) const {auto it=sources_.find(source);return it==sources_.end()?0:uint32_t(std::lround(it->second.delay));}
bool Client::live(uint64_t source,uint64_t now) const {
    auto it=sources_.find(source);if(it==sources_.end()||!it->second.everAccepted)return false;
    return now<it->second.acceptedAt||now-it->second.acceptedAt<=LiveMs;}
uint16_t Client::generation(uint64_t source) const {auto it=sources_.find(source);return it==sources_.end()?0:it->second.generation;}
const std::vector<uint8_t>* Client::state(uint64_t source,StateKind kind,uint64_t* revision) const {
    auto it=sources_.find(source);size_t k=size_t(kind)-1;if(it==sources_.end()||k>=StateKinds)return nullptr;
    auto& b=it->second.blobs[k];if(revision)*revision=b.revision;return b.have?&b.bytes:nullptr;}
std::optional<AnimationShown> Client::takeAnimation(uint64_t source,uint64_t now){
    auto it=sources_.find(source);if(it==sources_.end()||it->second.animations.empty())return {};
    auto& s=it->second;auto& q=s.animations;
    uint64_t at=serverNow(now),delay=uint64_t(std::lround(s.delay)),target=at>delay?at-delay:0;
    size_t due=q.size();
    for(size_t i=0;i<q.size()&&q[i].time<=target;++i)due=i;
    if(due==q.size())return {};
    AnimationShown r;r.animation=q[due].animation;r.sequence=q[due].sequence;r.animation.rose={};
    for(size_t i=0;i<=due;++i){
        if(s.taken&&!later(q[i].sequence,s.taken))continue;
        for(size_t k=0;k<AnimationBoolBytes;++k)r.animation.rose[k]|=q[i].animation.rose[k];
        r.fresh=true;}
    s.taken=r.sequence;
    if(due+1<q.size()&&q[due+1].time>q[due].time){
        r.between=true;r.next=q[due+1].animation;r.next.rose={};
        r.fraction=float(std::clamp(double(target-q[due].time)/double(q[due+1].time-q[due].time),0.,1.));}
    q.erase(q.begin(),q.begin()+long(due));
    if(target-q.front().time>1000)return {};
    return r;}
}
