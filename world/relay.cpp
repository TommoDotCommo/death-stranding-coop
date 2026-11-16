#include "relay.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace world {
using namespace ds::relay;
namespace bc=ds::broadcast;
namespace {
std::string hex(uint64_t v){static const char* d="0123456789abcdef";std::string s(16,'0');for(int i=15;i>=0;--i,v>>=4)s[size_t(i)]=d[v&15];return s;}
std::string text(const RelayAddress& a){
    return std::to_string(a.ip>>24)+"."+std::to_string((a.ip>>16)&255)+"."+std::to_string((a.ip>>8)&255)+"."+std::to_string(a.ip&255)+":"+std::to_string(a.port);}
double distance2(const std::array<double,3>& a,const std::array<double,3>& b){double d=0;for(unsigned i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return d;}
}
Relay::Relay(RelayConfig c,Send send):config_(std::move(c)),send_(std::move(send)){
    if(!tokenText(config_.token))throw std::invalid_argument("relay token: at most 32 letters, digits, - and _");
    if(!(config_.enterMetres>=1&&config_.leaveMetres>=config_.enterMetres&&config_.leaveMetres<=65535))throw std::invalid_argument("relay distances: 1 <= enter <= leave <= 65535 metres");
    if(config_.maxPlayers<1||config_.maxPlayers>RelayPlayersMax)throw std::invalid_argument("relay players: 1..1024");
    if(config_.timeoutMs<1000||config_.timeoutMs>600000)throw std::invalid_argument("relay timeout: 1..600 seconds");
    if(config_.ratePerPeer<10||config_.ratePerPeer>100000)throw std::invalid_argument("relay rate: 10..100000 datagrams a second");
    if(!(config_.fullMetres>=0&&config_.fullMetres<=65535&&config_.animationMetres>=0&&config_.animationMetres<=65535))throw std::invalid_argument("broadcast distances: 0..65535 metres");
    if(config_.intervalMs>5000)throw std::invalid_argument("broadcast interval: 0..5000 ms");
    if(config_.budget<256||config_.budget>bc::MaxDatagram)throw std::invalid_argument("broadcast budget: 256..1200 bytes");
    if(config_.flushMs>200)throw std::invalid_argument("broadcast flush: 0..200 ms");
    if(config_.broadcastRate<10||config_.broadcastRate>100000)throw std::invalid_argument("broadcast rate: 10..100000 datagrams a second");
}
bool Relay::areNear(uint64_t a,uint64_t b) const {auto it=players_.find(a);return it!=players_.end()&&it->second.nearby.count(b)!=0;}
// The same time for any token: no early exit on the first wrong byte.
bool Relay::rightToken(const std::string& t,uint64_t id) const {
#ifndef MUTANT_RELAY_ADMIT
    if(config_.admit)return config_.admit(id,t);
#else
    (void)id;
#endif
#ifndef MUTANT_RELAY_NO_TOKEN
    unsigned char diff=0;
    for(size_t i=0;i<TokenBytes;++i)diff|=static_cast<unsigned char>((i<t.size()?t[i]:0)^(i<config_.token.size()?config_.token[i]:0));
    return diff==0&&t.size()<=TokenBytes;
#else
    (void)t;return true;
#endif
}
bool Relay::allow(Player& p,uint64_t now){
#ifndef MUTANT_RELAY_NO_RATE
    // Pairs: a datagram for every player near; broadcast: one update a tick whoever is near.
    double rate=p.mode==bc::Version?double(config_.broadcastRate):double(config_.ratePerPeer)*double(std::max<size_t>(1,p.nearby.size()));
    if(now>p.bucketAt)p.tokens=std::min(rate,p.tokens+double(now-p.bucketAt)*rate/1000.);
    p.bucketAt=std::max(p.bucketAt,now);
    if(p.tokens<1)return false;
    p.tokens-=1;
#else
    (void)p;(void)now;
#endif
    return true;
}
void Relay::receive(const RelayAddress& from,const uint8_t* data,size_t size,uint64_t now){
    ++stats_.received;
    Player* sender=nullptr;
    if(auto a=byAddress_.find(from);a!=byAddress_.end())sender=&players_.find(a->second)->second;
    if(sender&&!allow(*sender,now)){++stats_.rateDropped;return;}
    if(bc::versionOf(data,size)==bc::Version){receiveBroadcast(from,sender,data,size,now);return;}
    Message m;
    if(parse(data,size,m)!=Error::None){++stats_.malformed;return;}
    switch(m.type){
    case Type::Hello:hello(from,m.hello,now);return;
    case Type::Data:{
        if(!sender){
            ++stats_.unknownSource;
#ifdef MUTANT_RELAY_ANY_SOURCE
            if(auto d=players_.find(m.data.peer);d!=players_.end()&&!d->second.nearby.empty()){
                encodeData(out_,Type::Relayed,*d->second.nearby.begin(),m.data.payload,m.data.size);send_(d->second.address,out_.data(),out_.size());}
#endif
            return;}
        if(sender->mode!=1){++stats_.wrongMode;return;}
        auto d=players_.find(m.data.peer);
#ifndef MUTANT_RELAY_FORWARD_FAR
        if(d==players_.end()||!sender->nearby.count(m.data.peer)){++stats_.notNear;return;}
#else
        if(d==players_.end()||d->first==sender->id){++stats_.notNear;return;}
#endif
#ifndef MUTANT_RELAY_SOURCE_ID
        encodeData(out_,Type::Relayed,sender->id,m.data.payload,m.data.size);
#else
        encodeData(out_,Type::Relayed,m.data.peer,m.data.payload,m.data.size);
#endif
        send_(d->second.address,out_.data(),out_.size());++stats_.forwarded;return;}
    case Type::Bye:
        if(sender&&sender->id==m.bye.id&&rightToken(m.bye.token,m.bye.id))remove(sender->id,"left");
        return;
    default:++stats_.malformed;return;   // the server's own message types are not for the server
    }
}
void Relay::refuse(const RelayAddress& to,uint64_t id,uint32_t sequence,uint8_t reason,uint16_t mode){
    Buffer b;
    if(mode==bc::Version){bc::Refused r;r.id=id;r.sequence=sequence;r.reason=bc::Refusal(reason);b=bc::encode(r);}
    else{Refused r;r.id=id;r.sequence=sequence;r.reason=Refusal(reason);b=encode(r);}
    send_(to,b.data(),b.size());++stats_.refused;}
// Two players can be near each other: the same mode, and in broadcast the same game and mod build (the receiver's
// decoder trusts the server for that; a pair of version 1 checks the build in every core datagram itself).
bool Relay::compatible(const Player& p,const Player& q) const {
#ifndef MUTANT_RELAY_BCAST_MODE
    if(p.mode!=q.mode)return false;
#endif
#ifndef MUTANT_RELAY_BCAST_BUILD
    if(p.mode==bc::Version&&q.mode==bc::Version&&(p.game!=q.game||p.build!=q.build))return false;
#endif
    return true;
}
void Relay::hello(const RelayAddress& from,const Hello& h,uint64_t now,const bc::Hello* v2){
    if(!rightToken(h.token,h.id)){++stats_.badToken;return;}
    uint16_t mode=v2?bc::Version:1;
    entered_.clear();
    auto it=players_.find(h.id);
    if(it!=players_.end()&&!(it->second.address==from)){
#ifndef MUTANT_RELAY_REBIND
        refuse(from,h.id,h.sequence,uint8_t(Refusal::IdInUse),mode);return;
#else
        if(auto other=byAddress_.find(from);other!=byAddress_.end())remove(other->second,"replaced");
        it=players_.find(h.id);byAddress_.erase(it->second.address);it->second.address=from;byAddress_[from]=h.id;
#endif
    }
    // The same player speaking the other version is a game that started again with another setting.
    if(it!=players_.end()&&it->second.mode!=mode){remove(h.id,"changed mode");it=players_.end();}
    if(v2&&config_.pinBuild&&(v2->game!=config_.game||v2->build!=config_.build)){
        if(it!=players_.end())remove(h.id,"another build");
        refuse(from,h.id,h.sequence,uint8_t(bc::Refusal::Build),mode);return;}
    bool fresh=false;
    if(it==players_.end()){
        // The same address under a new id is the same sender starting again.
        if(auto old=byAddress_.find(from);old!=byAddress_.end())remove(old->second,"replaced");
#ifndef MUTANT_RELAY_PLAYER_CAP
        if(players_.size()>=config_.maxPlayers){refuse(from,h.id,h.sequence,uint8_t(Refusal::Full),mode);return;}
#endif
        Player p;p.id=h.id;p.address=from;p.tokens=double(mode==bc::Version?config_.broadcastRate:config_.ratePerPeer);p.bucketAt=now;p.mode=mode;
        if(v2){p.game=v2->game;p.build=v2->build;}
        it=players_.emplace(h.id,std::move(p)).first;byAddress_[from]=h.id;fresh=true;restartStream(it->second);
        note("relay: player "+hex(h.id)+" ("+h.nickname+") joined from "+text(from)+(v2?" (broadcast)":"")+", "+std::to_string(players_.size())+" players");
    }
    Player& p=it->second;
#ifndef MUTANT_RELAY_HELLO_STALE
    // A HELLO that arrives after a later one says where the player was, not where he is.
    if(!fresh&&h.stream!=0&&int32_t(h.sequence-p.lastHello)<=0)return;
#endif
    p.lastHello=h.sequence;p.lastSeen=now;p.nickname=h.nickname;p.area=h.area;p.have=h.havePosition;p.position=h.position;
    if(!h.havePosition)p.haveAt=false;
    if(!fresh){
        if(h.stream==0){
            restartStream(p);   // the player knows nobody: tell him everyone again
            // A broadcast player that knows nobody is a game that started again: what it showed is gone too, and
            // everyone near is handed out to it again.
            if(p.mode==bc::Version){p.states={};++p.generation;p.havePublish=false;p.haveAt=false;p.feeds.clear();
                for(auto id:p.nearby)entered_.push_back({p.id,id});}
        }
        else if(h.stream==p.stream&&uint32_t(h.ack-p.acked)<=p.pending.size()){
            p.pending.erase(p.pending.begin(),p.pending.begin()+long(h.ack-p.acked));p.acked=h.ack;}
    }
    place(p);
    Buffer b;
    if(v2){
        bc::Welcome w;w.base.id=p.id;w.base.sequence=h.sequence;w.base.stream=p.stream;w.base.enterMetres=uint16_t(config_.enterMetres);w.base.leaveMetres=uint16_t(config_.leaveMetres);
        w.base.timeoutSeconds=uint16_t((config_.timeoutMs+999)/1000);w.base.nearby=uint16_t(p.nearby.size());
        w.t0=v2->t0;w.serverTime=std::min<uint64_t>(now,1ull<<53);for(size_t k=0;k<bc::StateKinds;++k)w.versions[k]=p.states[k].version;
        w.fullMetres=uint16_t(config_.fullMetres);w.animationMetres=uint16_t(config_.animationMetres);w.intervalMs=uint16_t(config_.intervalMs);w.budget=uint16_t(config_.budget);
        // Players who would be near but are not, for another build: the reason a group sees nobody is easy to miss.
        if(p.have){double enter=config_.enterMetres*config_.enterMetres;unsigned others=0;
            for(auto& [id,q]:players_)if(id!=p.id&&q.have&&q.area==p.area&&!compatible(p,q)&&distance2(p.position,q.position)<=enter)++others;
            w.otherBuild=uint16_t(std::min(others,65535u));}
        b=bc::encode(w);
    }else{
        Welcome w;w.id=p.id;w.sequence=h.sequence;w.stream=p.stream;w.enterMetres=uint16_t(config_.enterMetres);w.leaveMetres=uint16_t(config_.leaveMetres);
        w.timeoutSeconds=uint16_t((config_.timeoutMs+999)/1000);w.nearby=uint16_t(p.nearby.size());b=encode(w);
    }
    send_(p.address,b.data(),b.size());
    flush(p);
    // After the ENTERs that went out (to the others in place(), to this player just now): the state of each one who came near.
#ifndef MUTANT_RELAY_BCAST_ENTER_STATE
    auto pairs=std::move(entered_);entered_.clear();
    for(auto& [to,source]:pairs){
        auto t=players_.find(to),f=players_.find(source);
        if(t==players_.end()||f==players_.end()||!t->second.nearby.count(source))continue;
        for(size_t k=0;k<bc::StateKinds;++k)if(f->second.states[k].version)share(t->second,f->second,k);}
#endif
}
void Relay::restartStream(Player& p){
    do{p.stream=config_.seed+ ++streams_;}while(!p.stream);
    p.acked=0;p.pending.clear();
    for(auto id:p.nearby)p.pending.push_back({true,id,players_.find(id)->second.nickname});
}
// The caller has already changed `to.nearby`: a stream that grew too long starts again from that set.
void Relay::event(Player& to,bool enter,const Player& who){
    if(to.pending.size()>=RelayPendingMax){restartStream(to);return;}
    to.pending.push_back({enter,who.id,enter?who.nickname:std::string()});
}
void Relay::flush(Player& p){
    if(p.pending.empty())return;
    Roster r;r.stream=p.stream;r.first=p.acked+1;
    size_t count=std::min(p.pending.size(),RosterEventsMax);
    r.events.assign(p.pending.begin(),p.pending.begin()+long(count));
    auto b=p.mode==bc::Version?bc::encodeRoster(r):encode(r);send_(p.address,b.data(),b.size());
#ifdef MUTANT_RELAY_ROSTER_ONCE
    p.pending.erase(p.pending.begin(),p.pending.begin()+long(count));p.acked+=uint32_t(count);
#endif
}
// Who is near `p` now that he has said where he is.
void Relay::place(Player& p){
    double enter=config_.enterMetres*config_.enterMetres,leave=config_.leaveMetres*config_.leaveMetres;
    for(auto& entry:players_){
        Player& q=entry.second;if(q.id==p.id)continue;
        bool was=p.nearby.count(q.id)!=0;
#ifndef MUTANT_RELAY_NO_POSITION
        bool can=p.have&&q.have;
#else
        bool can=true;
#endif
#ifndef MUTANT_RELAY_AREA
        can=can&&p.area==q.area;
#endif
        can=can&&compatible(p,q);
        double d=0;for(unsigned i=0;i<3;++i)d+=(p.position[i]-q.position[i])*(p.position[i]-q.position[i]);
#ifndef MUTANT_RELAY_NO_HYSTERESIS
        bool now=can&&d<=(was?leave:enter);
#else
        (void)leave;bool now=can&&d<=enter;
#endif
        if(now==was)continue;
        if(now){p.nearby.insert(q.id);q.nearby.insert(p.id);if(p.mode==bc::Version&&q.mode==bc::Version){entered_.push_back({q.id,p.id});entered_.push_back({p.id,q.id});}}
        else{p.nearby.erase(q.id);q.nearby.erase(p.id);p.feeds.erase(q.id);q.feeds.erase(p.id);}
        event(p,now,q);event(q,now,p);flush(q);
    }
}
void Relay::remove(uint64_t id,const char* why){
    auto it=players_.find(id);if(it==players_.end())return;
    Player gone=std::move(it->second);byAddress_.erase(gone.address);players_.erase(it);
    for(auto other:gone.nearby){
        Player& q=players_.find(other)->second;q.nearby.erase(id);q.feeds.erase(id);
#ifndef MUTANT_RELAY_NO_LEAVE
        event(q,false,gone);flush(q);
#endif
    }
    note("relay: player "+hex(id)+" "+why+", "+std::to_string(players_.size())+" players");
}
void Relay::tick(uint64_t now){
#ifndef MUTANT_RELAY_NO_TIMEOUT
    std::vector<uint64_t> silent;
    for(auto& entry:players_)if(now>entry.second.lastSeen&&now-entry.second.lastSeen>config_.timeoutMs)silent.push_back(entry.first);
    for(auto id:silent){++stats_.timedOut;remove(id,"timed out");}
#else
    (void)now;
#endif
    flush(now);
}

// ---- Broadcast mode ---------------------------------------------------------------------------------------------------
void Relay::receiveBroadcast(const RelayAddress& from,Player* sender,const uint8_t* data,size_t size,uint64_t now){
    bc::Message m;
    if(bc::parse(data,size,m)!=Error::None){++stats_.malformed;return;}
    // The player a PUBLISH, STATE or FETCH is from: the one this address belongs to, never the id it names.
    auto from2=[&](uint64_t named)->Player*{
        if(!sender){++stats_.unknownSource;return nullptr;}
        if(sender->mode!=bc::Version){++stats_.wrongMode;return nullptr;}
#ifndef MUTANT_RELAY_BCAST_SPOOF
        if(named!=sender->id){++stats_.spoofed;return nullptr;}
        return sender;
#else
        auto it=players_.find(named);return it==players_.end()?sender:&it->second;
#endif
    };
    switch(m.type){
    case bc::Type::Hello:hello(from,m.hello.base,now,&m.hello);return;
    case bc::Type::Publish:if(auto* p=from2(m.publish.player))publish(*p,m.publish,now);return;
    case bc::Type::State:if(auto* p=from2(m.state.player))state(*p,m.state);return;
    case bc::Type::Fetch:if(auto* p=from2(m.fetch.player))fetch(*p,m.fetch.source,now);return;
    case bc::Type::Bye:
        if(sender&&sender->id==m.bye.id&&rightToken(m.bye.token,m.bye.id))remove(sender->id,"left");
        return;
    default:++stats_.malformed;return;   // the server's own message types are not for the server
    }
}
void Relay::publish(Player& s,const bc::Update& u,uint64_t now){
#ifndef MUTANT_RELAY_BCAST_ORDER
    if(s.havePublish&&(!bc::later(u.sequence,s.publishSequence)||u.stamp<=s.publishStamp)){++stats_.staleUpdates;return;}
#endif
#ifndef MUTANT_RELAY_BCAST_STAMP
    // The stamp is on this clock (the player learnt it from WELCOME): a sample from the future or long ago is no sample.
    if(u.stamp>now+BroadcastStampAheadMs||u.stamp+BroadcastStampBehindMs<now){++stats_.badStamps;return;}
#endif
    s.havePublish=true;s.publishSequence=u.sequence;s.publishStamp=u.stamp;
    bool teleport=false;
    if(u.parts&bc::HasPresence){
        auto at=bc::presencePosition(u.presence);
        if(positionValid(at)){s.at=at;s.haveAt=s.have;}
        teleport=(bc::presenceFlags(u.presence)&bc::TeleportFlag)!=0;}
    ++stats_.published;
    double full=config_.fullMetres*config_.fullMetres,animation=config_.animationMetres*config_.animationMetres;
    Buffer merged;
    for(auto id:s.nearby){
        auto it=players_.find(id);if(it==players_.end())continue;
        Player& r=it->second;if(r.mode!=bc::Version)continue;
        double d=distance2(where(s),where(r));
        auto& f=r.feeds[s.id];
#ifndef MUTANT_RELAY_BCAST_ANIMATION_FAR
        bool withAnimation=(u.parts&bc::HasAnimation)&&d<=animation;
#else
        bool withAnimation=(u.parts&bc::HasAnimation)!=0;
#endif
#ifndef MUTANT_RELAY_BCAST_TELEPORT
        bool always=teleport;
#else
        bool always=false;
#endif
#ifndef MUTANT_RELAY_BCAST_INTEREST
        bool due=d<=full||always||!f.sent||now>=f.lastSent+config_.intervalMs;
#else
        bool due=true;(void)always;
#endif
        if(!due){
#ifndef MUTANT_RELAY_BCAST_ROSE
            // Left out: its one-frame pulses go with the next sample this receiver is sent.
            if(withAnimation){size_t n=u.animation[0];for(size_t k=0;k<n;++k)f.rose[k]|=u.animation[1+n+k];f.roseBytes=uint8_t(std::max<size_t>(f.roseBytes,n));}
#endif
            ++stats_.thinned;continue;}
        bc::Update out=u;out.player=s.id;out.generation=s.generation;
        if(!withAnimation){out.parts&=uint8_t(~bc::HasAnimation);out.animation=nullptr;out.animationSize=0;}
        else if(f.roseBytes){
            // The pulses gathered while samples were left out, merged into this one (its bool bytes widened if need be).
            size_t n=u.animation[0],wide=std::max<size_t>(n,f.roseBytes),size=u.animationSize+2*(wide-n);
            if(size<=bc::AnimationMax){
                merged.assign(1,uint8_t(wide));
                for(size_t k=0;k<wide;++k)merged.push_back(k<n?u.animation[1+k]:0);
                for(size_t k=0;k<wide;++k)merged.push_back(uint8_t((k<n?u.animation[1+n+k]:0)|f.rose[k]));
                merged.insert(merged.end(),u.animation+1+2*n,u.animation+u.animationSize);
                out.animation=merged.data();out.animationSize=merged.size();}
        }
        if(withAnimation){f.rose={};f.roseBytes=0;}
        if(!out.parts)continue;   // an animation sample alone for a receiver beyond the animation distance
        f.sent=true;f.lastSent=now;
        enqueue(r,out,now);
    }
}
void Relay::enqueue(Player& r,const bc::Update& u,uint64_t now){
    size_t size=bc::entrySize(u);
#ifndef MUTANT_RELAY_BCAST_BUDGET
    if(!r.batch.empty()&&r.batch.size()+size>config_.budget)sendBatch(r,now);
#else
    if(!r.batch.empty()&&r.batch.size()+size>bc::MaxDatagram)sendBatch(r,now);
#endif
    if(r.batch.empty()){bc::beginFrame(r.batch);r.batchSince=now;}
    bc::appendEntry(r.batch,u);++stats_.entries;
    if(config_.flushMs==0||r.batch[16]==255)sendBatch(r,now);
}
void Relay::sendBatch(Player& r,uint64_t now){
    if(r.batch.empty())return;
    bc::finishFrame(r.batch,std::min<uint64_t>(now,1ull<<53));send_(r.address,r.batch.data(),r.batch.size());
    ++stats_.frames;stats_.frameBytes+=r.batch.size();r.batch.clear();
}
void Relay::flush(uint64_t now){
    for(auto& [id,p]:players_)if(!p.batch.empty()&&now>=p.batchSince+config_.flushMs)sendBatch(p,now);
}
uint64_t Relay::flushDue() const {
    uint64_t due=std::numeric_limits<uint64_t>::max();
    for(auto& [id,p]:players_)if(!p.batch.empty())due=std::min(due,p.batchSince+config_.flushMs);
    return due;
}
void Relay::state(Player& s,const bc::StatePost& st){
    size_t k=size_t(st.kind)-1;auto& blob=s.states[k];
#ifndef MUTANT_RELAY_BCAST_STATE_OLD
    if(blob.version&&!bc::later(st.version,blob.version)){++stats_.staleStates;return;}   // a repeat, or one that arrived late
#endif
    blob.version=st.version;blob.bytes.assign(st.payload,st.payload+st.size);++s.generation;++stats_.states;
    for(auto id:s.nearby){auto it=players_.find(id);if(it!=players_.end()&&it->second.mode==bc::Version)share(it->second,s,k);}
}
void Relay::fetch(Player& r,uint64_t source,uint64_t now){
    auto it=players_.find(source);
#ifndef MUTANT_RELAY_BCAST_FETCH_FAR
    if(it==players_.end()||!r.nearby.count(source)){++stats_.notNear;return;}
#else
    if(it==players_.end()||it->first==r.id){++stats_.notNear;return;}
#endif
    auto& f=r.feeds[source];
    if(f.fetchAt&&now<f.fetchAt)return;   // one answer in BroadcastFetchEveryMs: a FETCH costs four datagrams out
    f.fetchAt=now+BroadcastFetchEveryMs;++stats_.fetches;
    // Every kind, the empty ones too (version 0): the asker learns what is not there any more.
    for(size_t k=0;k<bc::StateKinds;++k)share(r,it->second,k);
}
void Relay::share(Player& to,const Player& from,size_t k){
    bc::Share h;h.source=from.id;h.kind=bc::StateKind(k+1);h.version=from.states[k].version;h.generation=from.generation;
    for(size_t i=0;i<bc::StateKinds;++i)h.versions[i]=from.states[i].version;
    h.payload=from.states[k].bytes.data();h.size=h.version?from.states[k].bytes.size():0;
    auto b=bc::encode(h);send_(to.address,b.data(),b.size());++stats_.shares;
}
}
