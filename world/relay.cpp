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
uint64_t mix(uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
bool nameText(const std::string& s,size_t least,size_t most){
    if(s.size()<least||s.size()>most)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    return true;}
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
    if(config_.links.size()>LinksMax)throw std::invalid_argument("relay links: at most 16");
    if(!config_.links.empty()){
        bool id=config_.serverId.size()>=4&&config_.serverId.size()<=presence::ServerIdBytes;
        for(char c:config_.serverId)id=id&&((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'));
        if(!id)throw std::invalid_argument("relay links: this server's id (4..32 letters and digits)");}
    uint64_t world=presence::siphash(1,2,reinterpret_cast<const uint8_t*>(config_.serverId.data()),config_.serverId.size());
    for(size_t i=0;i<config_.links.size();++i){
        auto& c=config_.links[i];
        if(!nameText(c.name,1,32))throw std::invalid_argument("relay link: a name of 1..32 letters, digits, - and _");
        if(!nameText(c.key,8,64))throw std::invalid_argument("relay link "+c.name+": a key of 8..64 letters, digits, - and _");
        for(size_t k=0;k<i;++k)if(config_.links[k].name==c.name||config_.links[k].key==c.key)throw std::invalid_argument("relay link "+c.name+": every link needs a name and a key of its own");
        if(c.budget<2000||c.budget>100000000)throw std::invalid_argument("relay link "+c.name+": budget 2000..100000000 bytes a second");
        if(c.players<1||c.players>RelayPlayersMax)throw std::invalid_argument("relay link "+c.name+": players 1..1024");
        Link l;l.config=c;l.key=presence::keyOf(c.key);l.configured=c.peer.ip!=0;l.address=c.peer;
        do l.mine=mix(world^mix(uint64_t(config_.seed)<<8^i));while(!l.mine);
        l.salt=mix(l.mine^0x6c696e6b73616c74ull);l.tokens=double(c.budget);
        links_.push_back(std::move(l));}
}
bool Relay::areNear(uint64_t a,uint64_t b) const {auto it=players_.find(a);return it!=players_.end()&&it->second.nearby.count(b)!=0;}
// The same time for any token: no early exit on the first wrong byte.
bool Relay::rightToken(const std::string& t) const {
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
    if(bc::versionOf(data,size)==presence::Version){receiveLink(from,data,size,now);return;}
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
        if(sender&&sender->id==m.bye.id&&rightToken(m.bye.token))remove(sender->id,"left");
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
    if(p.link&&q.link)return false;   // linked servers' players are shown by their own servers to each other
#ifndef MUTANT_RELAY_BCAST_MODE
    if(p.mode!=q.mode)return false;
#endif
#ifndef MUTANT_RELAY_BCAST_BUILD
    if(p.mode==bc::Version&&q.mode==bc::Version&&(p.game!=q.game||p.build!=q.build)
#ifdef MUTANT_LINK_PRESENCE_BUILD
       &&!p.link&&!q.link
#endif
      )return false;
#endif
    return true;
}
void Relay::hello(const RelayAddress& from,const Hello& h,uint64_t now,const bc::Hello* v2){
    if(!rightToken(h.token)){++stats_.badToken;return;}
    uint16_t mode=v2?bc::Version:1;
    entered_.clear();
    auto it=players_.find(h.id);
#ifndef MUTANT_LINK_PRESENCE_LOCAL_FIRST
    // A linked server's player has this id here: a player of this server takes it, and the other one is given
    // another with the next list from his server.
    if(it!=players_.end()&&it->second.link){remove(h.id,"gave his id to a player here");it=players_.end();}
#endif
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
        if(players()>=config_.maxPlayers){refuse(from,h.id,h.sequence,uint8_t(Refusal::Full),mode);return;}
#endif
        Player p;p.id=h.id;p.address=from;p.tokens=double(mode==bc::Version?config_.broadcastRate:config_.ratePerPeer);p.bucketAt=now;p.mode=mode;
        if(v2){p.game=v2->game;p.build=v2->build;}
        it=players_.emplace(h.id,std::move(p)).first;byAddress_[from]=h.id;fresh=true;restartStream(it->second);
        note("relay: player "+hex(h.id)+" ("+h.nickname+") joined from "+text(from)+(v2?" (broadcast)":"")+", "+std::to_string(players())+" players");
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
    handOut();
}
void Relay::handOut(){
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
    if(to.link)return;   // a linked server's player is told nothing here
    if(to.pending.size()>=RelayPendingMax){restartStream(to);return;}
    to.pending.push_back({enter,who.id,enter?who.nickname:std::string()});
}
void Relay::flush(Player& p){
    if(p.link||p.pending.empty())return;
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
    Player gone=std::move(it->second);players_.erase(it);
    if(gone.link){--remotes_;auto& l=links_[gone.link-1];l.remote.erase(gone.theirs);++l.stats.left;}
    else{byAddress_.erase(gone.address);for(auto& l:links_)if(l.exported.erase(id))l.presentDue=true;}
    for(auto other:gone.nearby){
        Player& q=players_.find(other)->second;q.nearby.erase(id);q.feeds.erase(id);
#ifndef MUTANT_RELAY_NO_LEAVE
        event(q,false,gone);flush(q);
#endif
    }
    note("relay: player "+hex(id)+(gone.link?" ("+gone.nickname+")":std::string())+" "+why+", "+std::to_string(players())+" players"+(remotes_?" and "+std::to_string(remotes_)+" of linked servers":""));
}
void Relay::tick(uint64_t now){
#ifndef MUTANT_RELAY_NO_TIMEOUT
    std::vector<uint64_t> silent;
    for(auto& entry:players_)if(!entry.second.link&&now>entry.second.lastSeen&&now-entry.second.lastSeen>config_.timeoutMs)silent.push_back(entry.first);
    for(auto id:silent){++stats_.timedOut;remove(id,"timed out");}
#else
    (void)now;
#endif
    for(size_t i=0;i<links_.size();++i)linkTick(links_[i],i,now);
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
        if(sender&&sender->id==m.bye.id&&rightToken(m.bye.token))remove(sender->id,"left");
        return;
    default:++stats_.malformed;return;   // the server's own message types are not for the server
    }
}
bool Relay::publish(Player& s,const bc::Update& u,uint64_t now){
#ifndef MUTANT_RELAY_BCAST_ORDER
    if(s.havePublish&&(!bc::later(u.sequence,s.publishSequence)||u.stamp<=s.publishStamp)){++stats_.staleUpdates;return false;}
#endif
#ifndef MUTANT_RELAY_BCAST_STAMP
    // The stamp is on this clock (the player learnt it from WELCOME): a sample from the future or long ago is no sample.
    if(u.stamp>now+BroadcastStampAheadMs||u.stamp+BroadcastStampBehindMs<now){++stats_.badStamps;return false;}
#endif
    s.havePublish=true;s.publishSequence=u.sequence;s.publishStamp=u.stamp;
    bool teleport=false;
    if(u.parts&bc::HasPresence){
        auto at=bc::presencePosition(u.presence);
        if(positionValid(at)){s.at=at;s.haveAt=s.have;}
        teleport=(bc::presenceFlags(u.presence)&bc::TeleportFlag)!=0;}
    ++stats_.published;
    Reach reach{config_.fullMetres*config_.fullMetres,config_.animationMetres*config_.animationMetres,config_.intervalMs};
    Buffer merged;
    for(auto id:s.nearby){
        auto it=players_.find(id);if(it==players_.end())continue;
        Player& r=it->second;if(r.mode!=bc::Version||r.link)continue;
        bc::Update out;
        if(!interest(r.feeds[s.id],u,distance2(where(s),where(r)),reach,teleport,now,out,merged,stats_.thinned))continue;
        out.player=s.id;out.generation=s.generation;
        enqueue(r,out,now);
    }
    // To the linked servers this player is sent to (only a player of this server: none is sent on, so no loops).
#ifndef MUTANT_LINK_PRESENCE_REEXPORT
    if(!s.link)
#endif
        for(auto& l:links_){auto e=l.exported.find(s.id);if(e!=l.exported.end())linkUpdate(l,e->second,s,u,teleport,now);}
    return true;
}
// What one receiver d2 (squared metres) away is sent of an update: false for nothing (left out by interest, its
// one-frame pulses kept in the feed for the next one sent). out may point into `merged`.
bool Relay::interest(Player::Feed& f,const bc::Update& u,double d,const Reach& reach,bool teleport,uint64_t now,bc::Update& out,Buffer& merged,uint64_t& thinned){
#ifndef MUTANT_RELAY_BCAST_ANIMATION_FAR
    bool withAnimation=(u.parts&bc::HasAnimation)&&d<=reach.animation2;
#else
    bool withAnimation=(u.parts&bc::HasAnimation)!=0;
#endif
#ifndef MUTANT_RELAY_BCAST_TELEPORT
    bool always=teleport;
#else
    bool always=false;(void)teleport;
#endif
#ifndef MUTANT_RELAY_BCAST_INTEREST
    bool due=d<=reach.full2||always||!f.sent||now>=f.lastSent+reach.intervalMs;
#else
    bool due=true;(void)always;
#endif
    if(!due){
#ifndef MUTANT_RELAY_BCAST_ROSE
        // Left out: its one-frame pulses go with the next sample this receiver is sent.
        if(withAnimation){size_t n=u.animation[0];for(size_t k=0;k<n;++k)f.rose[k]|=u.animation[1+n+k];f.roseBytes=uint8_t(std::max<size_t>(f.roseBytes,n));}
#endif
        ++thinned;return false;}
    out=u;
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
    if(!out.parts)return false;   // an animation sample alone for a receiver beyond the animation distance
    f.sent=true;f.lastSent=now;
    return true;
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
    for(auto& l:links_)if(!l.batch.empty()&&now>=l.batchSince+config_.flushMs)linkBatch(l,now);
}
uint64_t Relay::flushDue() const {
    uint64_t due=std::numeric_limits<uint64_t>::max();
    for(auto& [id,p]:players_)if(!p.batch.empty())due=std::min(due,p.batchSince+config_.flushMs);
    for(auto& l:links_)if(!l.batch.empty())due=std::min(due,l.batchSince+config_.flushMs);
    return due;
}
void Relay::state(Player& s,const bc::StatePost& st){
    size_t k=size_t(st.kind)-1;auto& blob=s.states[k];
#ifndef MUTANT_RELAY_BCAST_STATE_OLD
    if(blob.version&&!bc::later(st.version,blob.version)){++stats_.staleStates;return;}   // a repeat, or one that arrived late
#endif
    ++stats_.states;takeState(s,k,st.version,st.payload,st.size);
}
// New bytes of one kind (version 0: none any more): kept, counted in the generation, and handed to everyone near and
// to the linked servers he is sent to. Nothing goes out for version 0: the receivers ask, seeing the generation.
bool Relay::takeState(Player& s,size_t k,uint32_t version,const uint8_t* bytes,size_t size){
    auto& blob=s.states[k];
    if(!version){if(!blob.version)return false;blob={};++s.generation;return true;}
    blob.version=version;blob.bytes.assign(bytes,bytes+size);++s.generation;
    for(auto id:s.nearby){auto it=players_.find(id);if(it!=players_.end()&&it->second.mode==bc::Version)share(it->second,s,k);}
    if(!s.link)for(auto& l:links_)if(l.exported.count(s.id))linkSendShare(l,s,k);
    return true;
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
    if(to.link)return;
    bc::Share h;h.source=from.id;h.kind=bc::StateKind(k+1);h.version=from.states[k].version;h.generation=from.generation;
    for(size_t i=0;i<bc::StateKinds;++i)h.versions[i]=from.states[i].version;
    h.payload=from.states[k].bytes.data();h.size=h.version?from.states[k].bytes.size():0;
    auto b=bc::encode(h);send_(to.address,b.data(),b.size());++stats_.shares;
}

// ---- Linked servers' presence (README.md, "Presence across linked servers"; presence.h) --------------------------------
bool Relay::linkUp(size_t i) const {return i<links_.size()&&links_[i].up;}
size_t Relay::exported(size_t i) const {return i<links_.size()?links_[i].exported.size():0;}
bool Relay::exports(size_t i,uint64_t id) const {return i<links_.size()&&links_[i].exported.count(id)!=0;}
uint64_t Relay::remoteId(size_t i,uint64_t theirs) const {
    if(i>=links_.size())return 0;
    auto it=links_[i].remote.find(theirs);return it==links_[i].remote.end()?0:it->second;}
const RelayLinkStats& Relay::linkStats(size_t i) const {return links_.at(i).stats;}
// receiver: the other side's session this datagram names (default: the one held, or the one being answered).
void Relay::linkSend(Link& l,presence::Type type,const uint8_t* body,size_t size,uint64_t receiver){
    if(!l.address.ip&&!l.address.port)return;   // no address yet: it has to say HELLO first
    presence::Header h;h.type=type;h.sender=l.mine;h.receiver=receiver?receiver:l.up?l.peer:l.candidate;h.sequence=++l.sequence;
    auto b=presence::seal(h,body,size,l.key);send_(l.address,b.data(),b.size());
    ++l.stats.sent;l.stats.sentBytes+=b.size();l.tokens-=double(b.size());
}
// This server's id for a linked server's player: never one that is taken here, and not his id there (two servers'
// players can have the same id).
uint64_t Relay::localId(Link& l,uint64_t theirs) const {
#ifndef MUTANT_LINK_PRESENCE_REMAP
    uint64_t x=l.salt^theirs;
    while(true){x=mix(x);uint64_t id=x&((1ull<<53)-1);if(id&&!players_.count(id))return id;}
#else
    (void)l;return theirs;
#endif
}
// The name a linked server's player shows here: his own (or, when his state is no name, his nickname), " · ", and the
// name this server gave the link. The other server cannot change or leave out that last part.
std::string Relay::linkName(const Link& l,const uint8_t* blob,size_t size,const std::string& nickname) const {
    auto name=presence::nameOf(blob,size);
#ifndef MUTANT_LINK_PRESENCE_NAME
    return presence::shownName(name?*name:nickname,l.config.name);
#else
    return name?*name:nickname;
#endif
}
void Relay::receiveLink(const RelayAddress& from,const uint8_t* data,size_t size,uint64_t now){
    presence::Header h;const uint8_t* body=nullptr;size_t n=0;
    // The link whose key made the tag (the one whose session the datagram names first).
    Link* found=nullptr;size_t index=0;
    uint64_t names=size>=presence::HeaderSize?ds::relay::detail::get(data+16,8):0;
    for(int pass=0;pass<2&&!found;++pass)
        for(size_t i=0;i<links_.size();++i){
            if((pass==0)!=(names==links_[i].mine))continue;
            if(presence::open(data,size,links_[i].key,h,body,n)){found=&links_[i];index=i;break;}}
    if(!found){
        bool attributed=false;
        for(auto& l:links_)if(names&&names==l.mine){++l.stats.forged;attributed=true;}
        if(!attributed)++stats_.malformed;
        return;}
    Link& l=*found;auto& s=l.stats;
    // What one linked server can make this one do is bounded, the right tag or not.
    if(now>l.rateAt)l.rateTokens=std::min<double>(LinkReceiveRate,l.rateTokens+double(now-l.rateAt)*LinkReceiveRate/1000.);
    l.rateAt=std::max(l.rateAt,now);
    if(l.rateTokens<1){++s.rateDropped;return;}
    l.rateTokens-=1;++s.received;s.receivedBytes+=size;
    if(h.type==presence::Type::Hello){linkHello(l,index,from,h,body,n,now);return;}
    if(!l.up||h.sender!=l.peer||h.receiver!=l.mine||!(from==l.address)){++s.stale;return;}
    if(!l.window.take(h.sequence)){++s.replayed;return;}
    l.lastHeard=now;
    switch(h.type){
    case presence::Type::Present:linkPresent(l,index,body,n,now);return;
    case presence::Type::Interest:{
        presence::List list;std::vector<presence::Cell> cells;
        if(!presence::parse(body,n,list,cells)){++s.malformed;return;}
        if(l.cellsIn.take(list,cells)){l.cells=std::move(l.cellsIn.items);l.cellsIn.items.clear();}
        return;}
    case presence::Type::Frame:linkFrame(l,body,n,now);return;
    case presence::Type::Share:linkShare(l,body,n,now);return;
    case presence::Type::Fetch:linkFetch(l,body,n,now);return;
    default:++s.malformed;return;
    }
}
// HELLO: the other side's session, its clock and distances. A session is taken only from a HELLO that names this side's
// own session (so it was made after this side started, not recorded before), and never one given up before.
void Relay::linkHello(Link& l,size_t,const RelayAddress& from,const presence::Header& h,const uint8_t* body,size_t n,uint64_t now){
    auto& s=l.stats;presence::Hello hello;
    if(!presence::parse(body,n,hello)||hello.server==config_.serverId){++s.malformed;return;}   // the same world twice is no link
    if(l.configured&&!(from==l.address)){++s.stale;return;}
    for(auto r:l.retired)if(r==h.sender){++s.stale;return;}
    if(h.receiver!=l.mine){
        // Not yet a proof that it is new: answered (it learns this side's session), nothing more.
        if(h.sender==l.peer)return;
        if(!l.up){
            if(l.candidate!=h.sender)l.nextHello=now;   // answered at once
            l.candidate=h.sender;if(!l.configured)l.address=from;}
        else if(now>=l.probeAt){
            // The other side started again while this side still holds its old session: one HELLO naming the new one.
            l.probeAt=now+LinkHelloMs;
            presence::Hello mine;mine.server=config_.serverId;mine.t0=std::min<uint64_t>(now,1ull<<53);
            mine.enter=uint16_t(config_.enterMetres);mine.leave=uint16_t(config_.leaveMetres);mine.full=uint16_t(config_.fullMetres);
            mine.animation=uint16_t(config_.animationMetres);mine.interval=uint16_t(config_.intervalMs);
            auto b=presence::encode(mine);linkSend(l,presence::Type::Hello,b.data(),b.size(),h.sender);}
        return;}
    if(h.sender!=l.peer){
        if(l.peer){l.retired.push_back(l.peer);if(l.retired.size()>16)l.retired.erase(l.retired.begin());linkDown(l,"its server started again");}
        l.peer=h.sender;l.window={};l.up=true;l.lastHeard=now;l.nextLists=now;++s.sessions;
        note("link "+l.config.name+": presence with server "+hello.server+" at "+text(from));}
    if(!l.window.take(h.sequence)){++s.replayed;return;}
    if(!l.configured)l.address=from;
    l.lastHeard=now;l.peerServer=hello.server;l.reach=hello;
    // The clock: a round trip of one of this side's HELLOs, less the time the other side held it.
    if(hello.echo&&std::find(l.sentT0.begin(),l.sentT0.end(),hello.echo)!=l.sentT0.end()&&now>=hello.echo+hello.held){
        double rtt=double(now-hello.echo-hello.held);
        if(rtt<=5000){
            double offset=double(hello.t0)-double(hello.held)-double(hello.echo)-rtt/2;   // theirs minus ours
#ifdef MUTANT_LINK_PRESENCE_CLOCK
            offset=0;
#endif
            l.samples.push_back({rtt,offset});if(l.samples.size()>16)l.samples.pop_front();
            auto best=*std::min_element(l.samples.begin(),l.samples.end());l.offset=best.second;l.clock=true;}}
    l.echo=hello.t0;l.echoAt=now;
}
void Relay::linkDown(Link& l,const char* why){
    std::vector<uint64_t> ids;for(auto& entry:l.remote)ids.push_back(entry.second);
    for(auto id:ids)remove(id,"left with his server's link");
    if(l.up)note("link "+l.config.name+": presence lost ("+why+"), "+std::to_string(ids.size())+" of its players gone");
    l.up=false;l.peer=0;l.candidate=0;l.window={};l.cells.clear();l.cellsIn={};l.presentIn={};l.exported.clear();l.batch.clear();
    l.clock=false;l.samples.clear();l.sentT0.clear();l.echo=0;l.presentDue=false;
}
void Relay::linkTick(Link& l,size_t,uint64_t now){
    if(now>l.bucketAt)l.tokens=std::min<double>(l.config.budget,l.tokens+double(now-l.bucketAt)*l.config.budget/1000.);
    l.bucketAt=std::max(l.bucketAt,now);
#ifndef MUTANT_LINK_PRESENCE_TIMEOUT
    if(l.up&&now>l.lastHeard&&now-l.lastHeard>LinkSilentMs)linkDown(l,"silent");
#endif
    if(now>=l.nextHello){
        presence::Hello h;h.server=config_.serverId;h.t0=std::min<uint64_t>(now,1ull<<53);
        if(l.up&&l.echo){h.echo=l.echo;h.held=uint32_t(std::min<uint64_t>(now-l.echoAt,0xffffffffu));}
        h.enter=uint16_t(config_.enterMetres);h.leave=uint16_t(config_.leaveMetres);h.full=uint16_t(config_.fullMetres);
        h.animation=uint16_t(config_.animationMetres);h.interval=uint16_t(config_.intervalMs);
        auto b=presence::encode(h);linkSend(l,presence::Type::Hello,b.data(),b.size());
        l.sentT0.push_back(h.t0);if(l.sentT0.size()>16)l.sentT0.pop_front();
        l.nextHello=now+LinkHelloMs;}
    if(!l.up)return;
    // Which of this server's players it is sent, and what it is told: the list when it changed (before the state of a
    // player who starts to be sent) and every LinkListsMs, with where this server's players are.
    std::vector<uint64_t> fresh;linkExports(l,now);
    for(auto& [id,e]:l.exported)if(!e.listed){fresh.push_back(id);e.listed=true;}
    bool lists=now>=l.nextLists;
    if(lists||l.presentDue){
        std::vector<presence::Present> list;
        for(auto& entry:l.exported){
            auto& p=players_.at(entry.first);presence::Present x;
            x.id=p.id;x.area=p.area;x.generation=p.generation;x.position=where(p);x.game=p.game;x.build=p.build;x.nickname=p.nickname;list.push_back(std::move(x));}
        for(auto& b:presence::encodePresent(++l.lists,list))linkSend(l,presence::Type::Present,b.data(),b.size());
        l.presentDue=false;}
    for(auto id:fresh){auto& p=players_.at(id);for(size_t k=0;k<bc::StateKinds;++k)if(p.states[k].version)linkSendShare(l,p,k);}
    if(lists){
        std::set<presence::Cell> cells;
        for(auto& [id,p]:players_)if(!p.link&&p.mode==bc::Version&&p.have)cells.insert(presence::cellOf(p.area,where(p)));
        for(auto& b:presence::encodeCells(++l.lists,{cells.begin(),cells.end()}))linkSend(l,presence::Type::Interest,b.data(),b.size());
        l.nextLists=now+LinkListsMs;}
    if(now>=l.reportAt){
        if(l.reportAt){
            auto& a=l.stats;auto& b=l.reported;double seconds=LinkReportMs/1000.;
            note("link "+l.config.name+": presence, "+std::to_string(int(seconds))+" s: sent "+std::to_string(a.sent-b.sent)+" datagrams, "+std::to_string(a.sentBytes-b.sentBytes)+" bytes ("
                 +std::to_string(uint64_t(double(a.sentBytes-b.sentBytes)/seconds))+" a second, budget "+std::to_string(l.config.budget)+"), "+std::to_string(a.entries-b.entries)+" updates, "
                 +std::to_string(a.budgetDropped-b.budgetDropped)+" left out and "+std::to_string(a.stripped-b.stripped)+" without animation for the budget; "
                 +std::to_string(l.exported.size())+" players sent, "+std::to_string(l.remote.size())+" held from it");}
        l.reported=l.stats;l.reportAt=now+LinkReportMs;}
}
// This server's players sent to the link: those near one of its players' cells (from the leave distance it gave and a
// margin; kept until a margin more), nearest first, at most its number of players.
void Relay::linkExports(Link& l,uint64_t){
    double start=double(l.reach.leave)+LinkMarginMetres,stay=start+LinkMarginMetres;
    std::vector<std::pair<double,uint64_t>> want;
    for(auto& [id,p]:players_){
#ifndef MUTANT_LINK_PRESENCE_REEXPORT
        if(p.link)continue;   // a linked server's player is sent by his own server only: no loops
#endif
        if(p.mode!=bc::Version||!p.have)continue;
        auto at=where(p);double d=std::numeric_limits<double>::infinity();
        for(auto& c:l.cells)if(c.area==p.area)d=std::min(d,presence::distanceTo(c,at));
#ifdef MUTANT_LINK_PRESENCE_INTEREST
        d=0;
#endif
        if(d<=(l.exported.count(id)?stay:start))want.push_back({d,id});}
    std::sort(want.begin(),want.end());if(want.size()>l.config.players)want.resize(l.config.players);
    std::map<uint64_t,Link::Export> next;
    for(auto& [d,id]:want){
        auto it=l.exported.find(id);
        Link::Export e;if(it!=l.exported.end())e=std::move(it->second);else l.presentDue=true;
        e.d=d;next.emplace(id,std::move(e));}
    for(auto& entry:l.exported)if(!next.count(entry.first))l.presentDue=true;
    l.exported=std::move(next);
}
// One update of a player sent to the link: thinned by the other side's distances (with the margin, as if a receiver
// stood at the nearest of its cells), then within the byte budget: an update beyond its full distance only while a
// quarter of a second's budget is left (less the longer that player has waited, none after a second, so every player
// sent gets about one a second), its animation dropped before its presence (its pulses go with the next one).
void Relay::linkUpdate(Link& l,Link::Export& e,const Player& s,const bc::Update& u,bool teleport,uint64_t now){
    double m=LinkMarginMetres,full=l.reach.full+m,animation=l.reach.animation+m;
    Reach reach{full*full,animation*animation,uint32_t(l.reach.interval/2)};
    auto saved=e.feed;bc::Update out;Buffer merged;uint64_t thinned=0;
    if(!interest(e.feed,u,e.d*e.d,reach,teleport,now,out,merged,thinned))return;
    out.player=s.id;out.generation=s.generation;
    auto keep=[&](const bc::Update& x){
        if(!(x.parts&bc::HasAnimation))return;
        size_t k=x.animation[0];for(size_t i=0;i<k;++i)e.feed.rose[i]|=x.animation[1+k+i];
        e.feed.roseBytes=uint8_t(std::max<size_t>(e.feed.roseBytes,k));};
    double available=l.tokens-double(l.batch.empty()?0:l.batch.size()+presence::HeaderSize+presence::TagBytes);
    double waited=saved.sent?double(now-saved.lastSent):1000.;
    double reserve=(e.d*e.d<=reach.full2||teleport)?0:double(l.config.budget)/4*std::max(0.,1.-waited/1000.);
    auto fits=[&](const bc::Update& x){
        size_t size=bc::entrySize(x);if(size>presence::BodyMax-bc::FrameHeader)return false;
#ifndef MUTANT_LINK_PRESENCE_BUDGET
        if(available<double(size)+reserve)return false;
#endif
        return true;};
    if(!fits(out)){
        bc::Update bare=out;bare.parts=uint8_t(bare.parts&bc::HasPresence);bare.animation=nullptr;bare.animationSize=0;
        if(bare.parts&&(out.parts&bc::HasAnimation)&&fits(bare)){keep(out);out=bare;++l.stats.stripped;}
        else{e.feed=saved;keep(u);++l.stats.budgetDropped;return;}}
    if(!l.batch.empty()&&l.batch.size()+bc::entrySize(out)>presence::BodyMax)linkBatch(l,now);
    if(l.batch.empty()){bc::beginFrame(l.batch);l.batchSince=now;}
    bc::appendEntry(l.batch,out);++l.stats.entries;
    if(config_.flushMs==0||l.batch[16]==255)linkBatch(l,now);
}
void Relay::linkBatch(Link& l,uint64_t now){
    if(l.batch.empty())return;
    bc::finishFrame(l.batch,std::min<uint64_t>(now,1ull<<53));
    auto b=std::move(l.batch);l.batch.clear();linkSend(l,presence::Type::Frame,b.data(),b.size());
}
void Relay::linkSendShare(Link& l,const Player& p,size_t k){
    bc::Share h;h.source=p.id;h.kind=bc::StateKind(k+1);h.version=p.states[k].version;h.generation=p.generation;
    for(size_t i=0;i<bc::StateKinds;++i)h.versions[i]=p.states[i].version;
    h.payload=p.states[k].bytes.data();h.size=h.version?p.states[k].bytes.size():0;
    auto b=bc::encode(h);linkSend(l,presence::Type::Share,b.data(),b.size());++l.stats.shares;
}
void Relay::linkFetchFrom(Link& l,Player& r,uint64_t now){
    if(now<r.fetchAt)return;
    r.fetchAt=now+2*BroadcastFetchEveryMs;
    bc::Fetch f;f.player=r.id;f.source=r.theirs;auto b=bc::encode(f);linkSend(l,presence::Type::Fetch,b.data(),b.size());++l.stats.fetches;
}
// FETCH from the linked server: every kind of a player of this server it is sent (at most once in 250 ms).
void Relay::linkFetch(Link& l,const uint8_t* body,size_t n,uint64_t now){
    bc::Message m;if(bc::parse(body,n,m)!=Error::None||m.type!=bc::Type::Fetch){++l.stats.malformed;return;}
    auto e=l.exported.find(m.fetch.source);if(e==l.exported.end()){++l.stats.unknown;return;}
    if(e->second.fetchAt&&now<e->second.fetchAt)return;
    e->second.fetchAt=now+BroadcastFetchEveryMs;
    auto& p=players_.at(e->first);for(size_t k=0;k<bc::StateKinds;++k)linkSendShare(l,p,k);
}
// The linked server's list of the players it sends: new ones are players here from now on (under an id of this
// server's), the listed ones are updated, and those no longer listed leave.
void Relay::linkPresent(Link& l,size_t index,const uint8_t* body,size_t n,uint64_t now){
    presence::List list;std::vector<presence::Present> part;
    if(!presence::parse(body,n,list,part)){++l.stats.malformed;return;}
    if(!l.presentIn.take(list,part))return;
    auto all=std::move(l.presentIn.items);l.presentIn.items.clear();
    std::set<uint64_t> listed;
    for(auto& e:all){
        if(!listed.insert(e.id).second)continue;
        if(config_.pinBuild&&(e.game!=config_.game||e.build!=config_.build)){++l.stats.otherBuild;listed.erase(e.id);continue;}
        Player* r=nullptr;
        if(auto it=l.remote.find(e.id);it!=l.remote.end()){
            r=&players_.at(it->second);
            if(r->game!=e.game||r->build!=e.build){remove(r->id,"changed build");r=nullptr;}}
        if(!r){
            if(l.remote.size()>=l.config.players){++l.stats.overCap;listed.erase(e.id);continue;}
            uint64_t id=localId(l,e.id);
            Player p;p.id=id;p.link=index+1;p.theirs=e.id;p.mode=bc::Version;p.game=e.game;p.build=e.build;p.fetchAt=now+300;
#ifndef MUTANT_LINK_PRESENCE_REMAP
            players_.emplace(id,std::move(p));
#else
            players_[id]=std::move(p);
#endif
            r=&players_.at(id);l.remote[e.id]=id;++remotes_;++l.stats.joined;
            note("relay: player "+hex(id)+" ("+linkName(l,nullptr,0,e.nickname)+") of link "+l.config.name+" joined, "+std::to_string(players())+" players and "+std::to_string(remotes_)+" of linked servers");}
        bool named=r->name!=e.nickname||r->nickname.empty();
        r->area=e.area;r->name=e.nickname;r->lastSeen=now;
        if(named)r->nickname=linkName(l,nullptr,0,e.nickname);
        // Where he is: from his updates while they come, else from the list (a player near nobody sends none).
        if(!r->framedAt||now>r->framedAt+1000){r->position=e.position;r->have=true;r->haveAt=false;r->placedAt=now;entered_.clear();place(*r);handOut();}
        if(!r->haveTheirGeneration&&e.generation==0)r->haveTheirGeneration=true;
        else if(!r->haveTheirGeneration||r->theirGeneration!=e.generation)linkFetchFrom(l,*r,now);
    }
#ifndef MUTANT_LINK_PRESENCE_GONE
    std::vector<uint64_t> gone;for(auto& [theirs,id]:l.remote)if(!listed.count(theirs))gone.push_back(id);
    for(auto id:gone)remove(id,"is no longer sent by his server");
#endif
}
// Updates of the linked server's players: on its clock, taken here on this one, then as any player's.
void Relay::linkFrame(Link& l,const uint8_t* body,size_t n,uint64_t now){
    bc::Message m;if(bc::parse(body,n,m)!=Error::None||m.type!=bc::Type::Frame){++l.stats.malformed;return;}
    if(!l.clock){++l.stats.clockless;return;}
    for(auto& u:m.frame.entries){
        auto it=l.remote.find(u.player);if(it==l.remote.end()){++l.stats.unknown;continue;}
        Player& r=players_.at(it->second);
        double stamp=std::round(double(u.stamp)-l.offset);
        if(!(stamp>=1&&stamp<=double(1ull<<53))){++l.stats.clockless;continue;}
        bc::Update v=u;v.player=r.id;v.stamp=uint64_t(stamp);
        if(!publish(r,v,now))continue;
        if((v.parts&bc::HasPresence)&&positionValid(bc::presencePosition(v.presence))){
            r.have=r.haveAt=true;r.position=r.at;r.lastSeen=now;r.framedAt=now;
            if(!r.placedAt||now>=r.placedAt+250){r.placedAt=now;entered_.clear();place(r);handOut();}}
    }
}
// One kind of a linked server's player's state. The name gets this server's name for the link.
void Relay::linkShare(Link& l,const uint8_t* body,size_t n,uint64_t now){
    bc::Message m;if(bc::parse(body,n,m)!=Error::None||m.type!=bc::Type::Share){++l.stats.malformed;return;}
    auto& sh=m.share;
#ifndef MUTANT_LINK_PRESENCE_STATE_ANY
    auto it=l.remote.find(sh.source);if(it==l.remote.end()){++l.stats.unknown;return;}
    Player& r=players_.at(it->second);
#else
    auto found=players_.find(sh.source);
    if(found==players_.end()){auto it=l.remote.find(sh.source);if(it==l.remote.end()){++l.stats.unknown;return;}found=players_.find(it->second);}
    Player& r=found->second;
#endif
    if(r.haveTheirGeneration&&bc::later16(r.theirGeneration,sh.generation))return;   // older than what was taken
    r.theirGeneration=sh.generation;r.haveTheirGeneration=true;
    size_t k=size_t(sh.kind)-1;Buffer bytes;
    if(sh.version&&k==0)bytes=presence::nameBlob(linkName(l,sh.payload,sh.size,r.name));
    else if(sh.version)bytes.assign(sh.payload,sh.payload+sh.size);
    if(sh.version!=r.states[k].version||bytes!=r.states[k].bytes)takeState(r,k,sh.version,bytes.data(),bytes.size());
    for(size_t i=0;i<bc::StateKinds;++i)if(sh.versions[i]!=r.states[i].version){linkFetchFrom(l,r,now);break;}
}
}
