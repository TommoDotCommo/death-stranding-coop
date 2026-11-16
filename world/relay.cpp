#include "relay.h"
#include <algorithm>
#include <stdexcept>
namespace world {
using namespace ds::relay;
namespace {
std::string hex(uint64_t v){static const char* d="0123456789abcdef";std::string s(16,'0');for(int i=15;i>=0;--i,v>>=4)s[size_t(i)]=d[v&15];return s;}
std::string text(const RelayAddress& a){
    return std::to_string(a.ip>>24)+"."+std::to_string((a.ip>>16)&255)+"."+std::to_string((a.ip>>8)&255)+"."+std::to_string(a.ip&255)+":"+std::to_string(a.port);}
}
Relay::Relay(RelayConfig c,Send send):config_(std::move(c)),send_(std::move(send)){
    if(!tokenText(config_.token))throw std::invalid_argument("relay token: at most 32 letters, digits, - and _");
    if(!(config_.enterMetres>=1&&config_.leaveMetres>=config_.enterMetres&&config_.leaveMetres<=65535))throw std::invalid_argument("relay distances: 1 <= enter <= leave <= 65535 metres");
    if(config_.maxPlayers<1||config_.maxPlayers>RelayPlayersMax)throw std::invalid_argument("relay players: 1..1024");
    if(config_.timeoutMs<1000||config_.timeoutMs>600000)throw std::invalid_argument("relay timeout: 1..600 seconds");
    if(config_.ratePerPeer<10||config_.ratePerPeer>100000)throw std::invalid_argument("relay rate: 10..100000 datagrams a second");
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
    double rate=double(config_.ratePerPeer)*double(std::max<size_t>(1,p.nearby.size()));
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
void Relay::refuse(const RelayAddress& to,const Hello& h,Refusal why){
    Refused r;r.id=h.id;r.sequence=h.sequence;r.reason=why;auto b=encode(r);send_(to,b.data(),b.size());++stats_.refused;}
void Relay::hello(const RelayAddress& from,const Hello& h,uint64_t now){
    if(!rightToken(h.token)){++stats_.badToken;return;}
    auto it=players_.find(h.id);
    if(it!=players_.end()&&!(it->second.address==from)){
#ifndef MUTANT_RELAY_REBIND
        refuse(from,h,Refusal::IdInUse);return;
#else
        if(auto other=byAddress_.find(from);other!=byAddress_.end())remove(other->second,"replaced");
        it=players_.find(h.id);byAddress_.erase(it->second.address);it->second.address=from;byAddress_[from]=h.id;
#endif
    }
    bool fresh=false;
    if(it==players_.end()){
        // The same address under a new id is the same sender starting again.
        if(auto old=byAddress_.find(from);old!=byAddress_.end())remove(old->second,"replaced");
#ifndef MUTANT_RELAY_PLAYER_CAP
        if(players_.size()>=config_.maxPlayers){refuse(from,h,Refusal::Full);return;}
#endif
        Player p;p.id=h.id;p.address=from;p.tokens=double(config_.ratePerPeer);p.bucketAt=now;
        it=players_.emplace(h.id,std::move(p)).first;byAddress_[from]=h.id;fresh=true;restartStream(it->second);
        note("relay: player "+hex(h.id)+" ("+h.nickname+") joined from "+text(from)+", "+std::to_string(players_.size())+" players");
    }
    Player& p=it->second;
#ifndef MUTANT_RELAY_HELLO_STALE
    // A HELLO that arrives after a later one says where the player was, not where he is.
    if(!fresh&&h.stream!=0&&int32_t(h.sequence-p.lastHello)<=0)return;
#endif
    p.lastHello=h.sequence;p.lastSeen=now;p.nickname=h.nickname;p.area=h.area;p.have=h.havePosition;p.position=h.position;
    if(!fresh){
        if(h.stream==0)restartStream(p);   // the player knows nobody: tell him everyone again
        else if(h.stream==p.stream&&uint32_t(h.ack-p.acked)<=p.pending.size()){
            p.pending.erase(p.pending.begin(),p.pending.begin()+long(h.ack-p.acked));p.acked=h.ack;}
    }
    place(p);
    Welcome w;w.id=p.id;w.sequence=h.sequence;w.stream=p.stream;w.enterMetres=uint16_t(config_.enterMetres);w.leaveMetres=uint16_t(config_.leaveMetres);
    w.timeoutSeconds=uint16_t((config_.timeoutMs+999)/1000);w.nearby=uint16_t(p.nearby.size());
    auto b=encode(w);send_(p.address,b.data(),b.size());
    flush(p);
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
    auto b=encode(r);send_(p.address,b.data(),b.size());
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
        double d=0;for(unsigned i=0;i<3;++i)d+=(p.position[i]-q.position[i])*(p.position[i]-q.position[i]);
#ifndef MUTANT_RELAY_NO_HYSTERESIS
        bool now=can&&d<=(was?leave:enter);
#else
        (void)leave;bool now=can&&d<=enter;
#endif
        if(now==was)continue;
        if(now){p.nearby.insert(q.id);q.nearby.insert(p.id);}else{p.nearby.erase(q.id);q.nearby.erase(p.id);}
        event(p,now,q);event(q,now,p);flush(q);
    }
}
void Relay::remove(uint64_t id,const char* why){
    auto it=players_.find(id);if(it==players_.end())return;
    Player gone=std::move(it->second);byAddress_.erase(gone.address);players_.erase(it);
    for(auto other:gone.nearby){
        Player& q=players_.find(other)->second;q.nearby.erase(id);
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
}
}
