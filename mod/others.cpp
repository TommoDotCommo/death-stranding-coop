#include "others.h"
#include "../core/relay.h"
#include "controls.h"
#include "providers.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mod::others {
namespace {
// The core's checksum (FNV-1a over the datagram with bytes 120..123 as zero), redone after the identity is rewritten.
void checksum(uint8_t* b,size_t n,uint8_t out[4]){
    uint32_t h=2166136261u;for(size_t i=0;i<n;++i){h^=i>=120&&i<124?0:b[i];h*=16777619u;}
    for(unsigned i=0;i<4;++i)out[i]=uint8_t(h>>(8*i));
}
puppet::DoubleApi drivesOnly(puppet::DoubleApi api,const uint64_t* entity){api.find=[entity](uint64_t){return *entity;};return api;}
}

// ---- Bodies ---------------------------------------------------------------------------------------------------------
Body::Body(BodyParts parts,std::function<void(const std::string&)> log)
    :settle_(std::move(parts.settle)),own_(std::move(parts.own),std::move(log)),drive_(drivesOnly(std::move(parts.drive),&entity_),[](const std::string&){}){
    drive_.walk(true);drive_.enable();   // the porter's animation network, so the body walks (double.h)
}
uint64_t Body::tick(const puppet::Anchor& porter,const puppet::Transform* goal,const ds::Animation* sample,bool fresh,double speed,uint32_t actions,uint64_t now,bool mayMake){
    // A body that has to be made (none yet, or the porter's world is a new one) waits for its turn.
    bool toMake=!own_.body()||porter.entity!=porter_;
    if(own_.exhausted()||(toMake&&!mayMake))entity_=0;
    else{
        uint64_t before=toMake?0:own_.body();
        entity_=own_.tick(porter);if(porter.entity&&porter.pose.available)porter_=porter.entity;
        if(entity_&&entity_!=before)++creations_;
    }
    if(!entity_){drive_.mirror(nullptr,false);drive_.tick(true,nullptr);return 0;}
    if(goal){drive_.mirror(sample,fresh);drive_.tick(true,goal,speed,actions,false);}
    else{
#ifndef MUTANT_BODIES_HIDE
        // Hidden, not handed back: every new acquire builds an animation-network instance the game never frees.
        drive_.mirror(nullptr,false);drive_.tick(true,nullptr,0,0,true);
#endif
    }
    drive_.mirror(nullptr,false);   // the sample lives only for this tick
    if(settle_)settle_(entity_,porter.entity,now);
    return entity_;
}
void Body::remove(){drive_.mirror(nullptr,false);drive_.tick(true,nullptr);own_.remove();entity_=0;}

void Bodies::drive(const std::vector<Want>& wants,const puppet::Anchor& porter,uint64_t now){
#ifndef MUTANT_BODIES_REMOVE
    for(auto it=bodies_.begin();it!=bodies_.end();){
        bool wanted=std::any_of(wants.begin(),wants.end(),[&](const Want& w){return w.key==it->first;});
        if(wanted){++it;continue;}
        it->second->remove();++removes_;it=bodies_.erase(it);
    }
#endif
    made_=driving_=givenUp_=0;
    if(!make_)return;
    bool newThisCall=false;
    for(const auto& w:wants){
        auto it=bodies_.find(w.key);
#ifdef MUTANT_BODIES_ONCE
        it=bodies_.end();
#endif
        if(it==bodies_.end()){
            // One new body object per call: building one looks the game's routines up again, and thirty players
            // arriving in one tick held the game thread for over 5 s (live 2026-11-16).
#ifndef MUTANT_BODIES_ONE_NEW
            if(newThisCall)continue;
#endif
            newThisCall=true;
            auto body=make_(w.key);if(!body)continue;
            bodies_[w.key]=std::move(body);it=bodies_.find(w.key);
        }
        auto& body=*it->second;
        bool mayMake=!madeOnce_||now-madeAt_>=MakeEveryMs;
#ifdef MUTANT_BODIES_RATE
        mayMake=true;
#endif
        uint64_t before=body.creations();
#ifndef MUTANT_BODIES_MIX
        const Want& use=w;
#else
        const Want& use=wants.front();
#endif
        if(body.tick(porter,use.goal,use.sample,use.fresh,use.speed,use.actions,now,mayMake))++made_;
        if(body.creations()!=before){madeAt_=now;madeOnce_=true;++makes_;}
        if(body.givenUp())++givenUp_;
        if(body.driving())++driving_;
    }
}

// ---- Sessions -------------------------------------------------------------------------------------------------------
uint64_t pairSession(uint64_t session,uint64_t a,uint64_t b){
#ifndef MUTANT_OTHERS_PAIR
    return ds::relay::pairSession(session,a,b);   // one rule for the mod, the stand-ins and anything else on the relay
#else
    (void)a;(void)b;return session?session:1;
#endif
}
Drive drive(const Shown& shown,bool poses){
    Drive d;
#ifndef MUTANT_OTHERS_STALE
    if(!shown.visible)return d;
#else
    if(!shown.connected)return d;
#endif
    const auto& s=shown.render.state;
    d.shown=true;d.goal=puppet::fromState(s.position,s.yaw);d.speed=std::hypot(s.velocity[0],s.velocity[2]);
    d.actions=uint32_t(s.flags)|(uint32_t(s.action&31)<<16);
    // Riding: the game's riding pose with the vehicle's pitch and roll. No vehicle is drawn for others.
    if(s.vehicle){d.goal.rotation=mod::rotationFromAngles(s.vehicleAngles);d.actions|=uint32_t(s.vehicle)<<puppet::VehicleShift;return d;}
    bool down=(s.flags&ds::Down)!=0,caught=(s.flags&ds::Caught)!=0;
    d.mirror=shown.animation.has_value()&&!down&&!caught;
    if(down){d.speed=0;d.goal=puppet::laidDown(d.goal);d.actions=poses?puppet::DeadPoseBit:(d.actions&puppet::FlagCrouchBit);}
    else if(caught&&poses){d.speed=0;d.actions=puppet::DragPoseBit;}
    else if(poses&&(s.flags&ds::Rest)&&!d.mirror&&d.speed<0.3){d.speed=0;d.actions=puppet::RestPoseBit;}
    return d;
}
void plan(const std::vector<Shown>& shown,bool show,bool poses,bool animation,Plan& out){
    out.drives.clear();out.samples.clear();out.wants.clear();
    out.drives.reserve(shown.size());out.samples.resize(shown.size());out.wants.reserve(shown.size());
    for(size_t i=0;i<shown.size();++i){
        auto d=drive(shown[i],poses);
        if(!show)d.shown=false;
        if(d.shown&&d.mirror&&animation)out.samples[i]=controls::blend(*shown[i].animation);
        out.drives.push_back(d);
    }
    for(size_t i=0;i<shown.size();++i){
        Want w;w.key=shown[i].id;const auto& d=out.drives[i];
        if(d.shown){w.goal=&d.goal;w.speed=d.speed;w.actions=d.actions;
            if(out.samples[i]){w.sample=&*out.samples[i];w.fresh=shown[i].animation->fresh;}}
        out.wants.push_back(w);
    }
}
std::vector<nametag::Named> labels(const std::vector<Shown>& shown,const Bodies& bodies){
    std::vector<nametag::Named> out;out.reserve(shown.size());
    for(const auto& v:shown){
        nametag::Named n;n.key=v.id;n.body=bodies.entity(v.id);
#ifndef MUTANT_OTHERS_LABEL_BODY
        if(!n.body)continue;   // no body yet (one is made every MakeEveryMs), or the game refused one
#endif
#ifndef MUTANT_OTHERS_LABEL_NAME
        n.name=nametag::fromUtf8(v.name);
#else
        n.name=nametag::fromUtf8(shown.front().name);
#endif
        if(n.name.empty())n.name=u"Porter";
        out.push_back(std::move(n));
    }
    return out;
}

std::vector<gear::Wearer> wearers(const std::vector<Shown>& shown,const Bodies& bodies){
    std::vector<gear::Wearer> out;out.reserve(shown.size());
    for(const auto& v:shown){
        gear::Wearer w;w.key=v.id;w.body=bodies.entity(v.id);
#ifndef MUTANT_OTHERS_WEAR_BODY
        if(!w.body)continue;   // no body yet, or the game refused one
#else
        if(!w.body)w.body=bodies.entity(shown.front().id);
#endif
#ifndef MUTANT_OTHERS_WEAR_MIX
        w.worn=v.worn;
#else
        w.worn=shown.front().worn;
#endif
        out.push_back(std::move(w));
    }
    return out;
}
std::vector<uint64_t> undress(const std::vector<Shown>& shown,const std::vector<uint64_t>& dressed,Plan& plan){
    std::vector<uint64_t> gone;
    for(auto key:dressed){
        if(!key||std::any_of(shown.begin(),shown.end(),[&](const Shown& v){return v.id==key;}))continue;
        gone.push_back(key);
#ifndef MUTANT_OTHERS_UNDRESS
        Want w;w.key=key;plan.wants.push_back(w);
#endif
    }
    return gone;
}
std::vector<bool> Nearest::choose(const std::vector<std::pair<uint64_t,double>>& candidates,size_t limit){
    std::vector<std::pair<double,size_t>> order;order.reserve(candidates.size());
    for(size_t i=0;i<candidates.size();++i){
        double d=candidates[i].second;if(!std::isfinite(d))d=1e300;
#ifndef MUTANT_OTHERS_CARRY_FLAP
        if(held_.count(candidates[i].first))d-=nametag::KeepMetres;
#endif
        order.push_back({d,i});
    }
    std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<bool> chosen(candidates.size(),false);held_.clear();
#ifdef MUTANT_OTHERS_CARRY_LIMIT
    limit=candidates.size();
#endif
    for(size_t k=0;k<order.size()&&k<limit;++k){chosen[order[k].second]=true;held_.insert(candidates[order[k].second].first);}
    return chosen;
}
std::vector<Carrier> carriers(const std::vector<Shown>& shown,const Bodies& bodies,const std::function<uint64_t(uint64_t)>& pack,bool show,
                              const std::array<double,3>* viewer,size_t limit,Nearest& nearest){
    std::vector<Carrier> all;std::vector<std::pair<uint64_t,double>> candidates;
    for(const auto& v:shown){
#ifndef MUTANT_OTHERS_CARRY_HIDDEN
        if(!show||!v.visible)continue;
#else
        (void)show;
#endif
        Carrier c;c.key=v.id;c.body=bodies.entity(v.id);if(!c.body)continue;
        c.pack=pack?pack(v.id):0;
#ifndef MUTANT_OTHERS_CARRY_PACK
        if(!c.pack)continue;
#endif
#ifndef MUTANT_OTHERS_CARRY_MIX
        const Shown& from=v;
#else
        const Shown& from=shown.front();
#endif
        if(from.places){
            c.placed.emplace();
            for(auto& p:*from.places){c.items.push_back({p.item,p.variant});c.placed->push_back(p.rel);}
        }else if(from.carried){
            for(auto& e:*from.carried)if(e.slot==1)for(unsigned i=0;i<e.count&&c.items.size()<stack::Stack::MaxCases;++i)c.items.push_back({e.hash,e.variant});   // 1: BackPack
        }
        if(c.items.empty())continue;
        double d=double(candidates.size());   // without a viewer: as listed
#ifndef MUTANT_OTHERS_CARRY_NEAREST
        if(viewer){const auto& at=v.render.state.position;d=std::sqrt((at[0]-(*viewer)[0])*(at[0]-(*viewer)[0])+(at[1]-(*viewer)[1])*(at[1]-(*viewer)[1])+(at[2]-(*viewer)[2])*(at[2]-(*viewer)[2]));}
#else
        (void)viewer;
#endif
        candidates.push_back({c.key,d});all.push_back(std::move(c));
    }
    auto chosen=nearest.choose(candidates,std::min(limit,MaxCarriers));
    std::vector<Carrier> out;for(size_t i=0;i<all.size();++i)if(chosen[i])out.push_back(std::move(all[i]));
    return out;
}

// One other player: the core's view of this porter (StateProvider) and its way out (Transport).
struct Others::Session final:ds::StateProvider,ds::Transport {
    Others& owner;uint64_t id;
    ds::Render remote;bool jump=false;
    nametag::Sender nameTx;nametag::Receiver nameRx;std::string name;
    // What that player wears and carries (received), and this porter's to that player (sent).
    gear::WornReceiver wornRx;gear::WornSender wornTx;std::optional<gear::WornSet> worn;
    carry::Receiver carryRx;carry::Sender carryTx;std::optional<carry::Summary> carried;
    carryplace::Receiver placeRx;carryplace::Sender placeTx;std::optional<std::vector<carryplace::Placed>> places;
    bool wasUp=false;
    std::array<uint8_t,ds::AnimationBoolBytes> rose{};
    std::unique_ptr<ds::Core> core;   // last: it refers to this object
    Session(Others& o,uint64_t i):owner(o),id(i){}
    bool readLocal(ds::State& s) override {
        s=owner.local_.state;s.flags=uint16_t(jump?s.flags|ds::Teleport:s.flags&~ds::Teleport);return owner.local_.available;}
    uint64_t localSampleTime(uint64_t) const override {return owner.local_.sampledAt;}
    bool isInCutsceneOrLoading() const override {return owner.local_.gated;}
    void applyRemote(const ds::Render& r) override {remote=r;}
    // Story progress is not compared between others (each plays an own save): unknown never warns and never refuses.
    void send(const ds::Bytes& b) override {
#ifndef MUTANT_OTHERS_STICKY
        if(b[6]==uint8_t(ds::Kind::Presence)&&(b[60]&ds::Teleport))jump=false;   // the jump has been sent
#endif
        owner.out(id,{b.begin(),b.end()});}
    void sendLong(const std::vector<uint8_t>& b) override {if(b.size()>=ds::WireSize)owner.out(id,b);}
};
Others::Others(Settings s,Link link,std::function<uint64_t()> nonce,std::function<void(const std::string&)> log)
    :settings_(std::move(s)),link_(std::move(link)),nonce_(std::move(nonce)),log_(std::move(log)){
    settings_.cap=std::min(settings_.cap,MaxOthers);
    if(settings_.cap&&(!settings_.self||!settings_.session||!nonce_||settings_.delayMs>1000))throw std::invalid_argument("others: an id, a session and a nonce source are required");
    if(!log_)log_=[](const std::string&){};
}
Others::~Others()=default;
void Others::out(uint64_t to,std::vector<uint8_t> d){
    std::copy(settings_.build.begin(),settings_.build.end(),d.begin()+16);checksum(d.data(),d.size(),d.data()+120);
    ++counters_.sent;if(link_.send)link_.send(to,d.data(),d.size());
}
bool Others::entered(uint64_t id,uint64_t nowMs){
    (void)nowMs;
    if(!settings_.cap||!id||sessions_.count(id))return false;
#ifndef MUTANT_OTHERS_SELF
    if(id==settings_.self)return false;
#endif
#ifndef MUTANT_OTHERS_CAP
    if(sessions_.size()>=settings_.cap){
        ++counters_.full;
        // Remembered (bounded), so the player is taken when somebody leaves.
        if(std::find(waiting_.begin(),waiting_.end(),id)==waiting_.end()&&waiting_.size()<4*MaxOthers)waiting_.push_back(id);
        return false;}
#endif
    auto s=std::make_unique<Session>(*this,id);
    ds::Config c;c.session=pairSession(settings_.session,settings_.self,id);c.nonce=nonce_();c.delayMs=settings_.delayMs;
#ifndef MUTANT_OTHERS_ROLE
    c.role=pairRole(settings_.self,id);
#else
    c.role=ds::Role::Host;
#endif
    s->core=std::make_unique<ds::Core>(c,*s,*s);
    sessions_[id]=std::move(s);
    log_("OTHERS player "+std::to_string(id)+" is near ("+std::to_string(sessions_.size())+" now)\n");
    return true;
}
void Others::left(uint64_t id,uint64_t nowMs){
    waiting_.erase(std::remove(waiting_.begin(),waiting_.end(),id),waiting_.end());
    auto it=sessions_.find(id);if(it==sessions_.end())return;
#ifndef MUTANT_OTHERS_LEFT
    it->second->core->disconnect(nowMs);   // one BYE, so their side lets go at once
    sessions_.erase(it);
    log_("OTHERS player "+std::to_string(id)+" left ("+std::to_string(sessions_.size())+" now)\n");
    while(!waiting_.empty()&&sessions_.size()<settings_.cap){auto next=waiting_.front();waiting_.pop_front();entered(next,nowMs);}
#endif
}
void Others::clear(uint64_t nowMs){
    waiting_.clear();
    std::vector<uint64_t> ids;for(auto& [id,s]:sessions_)ids.push_back(id);
    for(auto id:ids)left(id,nowMs);
}
ds::Error Others::received(uint64_t from,const uint8_t* data,size_t size,uint64_t nowMs){
    ++counters_.received;
    auto it=sessions_.find(from);
#ifdef MUTANT_OTHERS_UNKNOWN
    if(it==sessions_.end())it=sessions_.begin();
#endif
    if(it==sessions_.end()){++counters_.unknown;return ds::Error::NotConnected;}
    // Only the presence core's datagrams: the fixed frame, or the one longer kind others carry (animation).
    if(!data||size<ds::WireSize||size>ds::AnimationWireMax||(size>ds::WireSize&&data[6]!=uint8_t(ds::Kind::Animation))){++counters_.malformed;return ds::Error::Length;}
#ifndef MUTANT_OTHERS_BUILD
    if(!std::equal(settings_.build.begin(),settings_.build.end(),data+16)){++counters_.foreignBuild;return ds::Error::Build;}
#endif
#ifndef MUTANT_OTHERS_KINDS
    // Presence only: the host's time of day, rain and enemies are the two-player layer's.
    if(data[6]==uint8_t(ds::Kind::Environment)||data[6]==uint8_t(ds::Kind::Enemies)){++counters_.kind;return ds::Error::Kind;}
#endif
    // The adapter identity was checked above; the core is given its own pin. A frame whose checksum was already wrong
    // stays wrong.
    std::vector<uint8_t> b(data,data+size);uint8_t sum[4];checksum(b.data(),b.size(),sum);
    if(std::equal(sum,sum+4,b.begin()+120)){std::copy(ds::ModBuild.begin(),ds::ModBuild.end(),b.begin()+16);checksum(b.data(),b.size(),b.data()+120);}
    auto e=it->second->core->receive(b.data(),b.size(),nowMs);
    if(e==ds::Error::None)++counters_.accepted;else ++counters_.refused;
    return e;
}
void Others::local(const Local& l){
    local_=l;local_.state.flags&=uint16_t(~ds::Teleport);
    if(l.available&&l.jumped)for(auto& [id,s]:sessions_)s->jump=true;
}
void Others::tick(uint64_t nowMs){
    for(auto& [id,s]:sessions_){
        bool up=s->core->connected();
        for(auto& e:s->nameTx.tick(settings_.nickname,up))s->core->queueCargo(e);
        s->core->tick(nowMs);
#ifndef MUTANT_OTHERS_CARRY_LOST
        // The session dropped: what that player carried is not known any more (they send it again on connecting).
        if(s->wasUp&&!s->core->connected()){s->carried.reset();s->places.reset();s->carryRx.reset();s->placeRx.reset();s->wornRx.reset();}
#endif
        s->wasUp=s->core->connected();
        // The core's reliable events carry for others the name and, when the settings say so, what is only shown: the
        // gear a player wears (7), what a player carries (6) and where each case sits (16). Anything else (cargo put
        // down or taken, structures, enemies, orders, ...) was acknowledged, so their stream goes on, and is dropped
        // here: nothing of another player's changes this world.
        for(auto& e:s->core->takeCargo()){
#ifndef MUTANT_OTHERS_EVENTS_OFF
            bool gear=settings_.gear,carry=settings_.carry;
#else
            bool gear=true,carry=true;
#endif
#ifndef MUTANT_OTHERS_EVENTS_MIX
            Session& to=*s;
#else
            Session& to=*sessions_.begin()->second;
#endif
            if(e.type==nametag::WireType){
                if(auto n=s->nameRx.feed(e)){if(*n!=s->name)log_("OTHERS player "+std::to_string(id)+" has a name of "+std::to_string(n->size())+" bytes\n");s->name=*n;}
            }else if(gear&&e.type==gear::GearWireType){
                if(auto w=s->wornRx.feed(e)){++counters_.gear;
                    if(!to.worn||*to.worn!=*w)log_("OTHERS player "+std::to_string(id)+" shows "+std::to_string(w->size())+" pieces of gear\n");
                    to.worn=std::move(*w);}
            }else if(carry&&e.type==carry::WireType){
                if(auto c=s->carryRx.feed(e)){++counters_.carried;
                    if(!to.carried||*to.carried!=*c)log_("OTHERS player "+std::to_string(id)+" carries "+std::to_string(c->size())+" entries\n");
                    to.carried=std::move(*c);}
            }else if(carry&&e.type==carryplace::WireType){
                if(auto p=s->placeRx.feed(e)){++counters_.carried;to.places=std::move(*p);}
            }else ++counters_.events;
        }
    }
}
bool Others::animation(const ds::Animation& a,uint64_t nowMs){
    bool any=false;
    for(auto& [id,s]:sessions_){
        if(!s->core->connected()){s->rose={};continue;}
        for(size_t k=0;k<s->rose.size();++k)s->rose[k]|=a.rose[k];
        // The controls only: what he holds, his vehicle's wheels and his carts are not shown for others.
        ds::Animation x;x.held=a.held;x.floats=a.floats;x.rose=s->rose;
        try{if(s->core->sendAnimation(x,nowMs)){s->rose={};any=true;}}
        catch(const std::invalid_argument&){}   // a control the wire cannot carry: this sample is left out
    }
    return any;
}
size_t Others::wears(const std::optional<gear::WornSet>& read,uint64_t nowMs){
    size_t queued=0;if(!settings_.gear)return 0;
    for(auto& [id,s]:sessions_)for(auto& e:s->wornTx.observe(read,s->core->connected(),nowMs)){s->core->queueCargo(e);++queued;}
    return queued;
}
size_t Others::carries(const std::optional<carry::Summary>& summary,const std::optional<std::vector<carryplace::Placed>>& placed,uint64_t nowMs){
    size_t queued=0;if(!settings_.carry)return 0;
    for(auto& [id,s]:sessions_){
        bool up=s->core->connected();
        for(auto& e:s->carryTx.observe(summary,up,nowMs)){s->core->queueCargo(e);++queued;}
        // Places only on the passes that measured them: the sender notices a new connection only when it is called,
        // and one noticed on a pass without places would never send them to that player again.
#ifndef MUTANT_OTHERS_PLACE_FRESH
        if(!placed)continue;
#endif
        for(auto& e:s->placeTx.observe(placed,up,nowMs)){s->core->queueCargo(e);++queued;}
    }
    return queued;
}
std::vector<Shown> Others::frame(uint64_t nowMs){
    std::vector<Shown> out;out.reserve(sessions_.size());
    for(auto& [id,s]:sessions_){
        s->core->frame(nowMs);
        Shown v;v.id=id;v.connected=s->core->connected();v.render=s->remote;v.visible=v.connected&&s->remote.visible;v.name=s->name;
        if(v.connected)v.animation=s->core->takeAnimation(nowMs);
        v.worn=s->worn;v.carried=s->carried;v.places=s->places;
        out.push_back(std::move(v));
    }
    return out;
}
size_t Others::connected() const {size_t n=0;for(auto& [id,s]:sessions_)if(s->core->connected())++n;return n;}

// ---- A link in memory -----------------------------------------------------------------------------------------------
Link MemoryHub::link(uint64_t self){
    return {[this,self](uint64_t to,const uint8_t* data,size_t size){
        Datagram d{self,to,now_+delayMs,std::vector<uint8_t>(data,data+size)};
        if(record)sentLog.push_back(d);
        if(drop&&drop(self,to,d.bytes))return;
        queue_.push_back(std::move(d));}};
}
void MemoryHub::join(uint64_t id,Others& player,uint64_t nowMs,uint64_t clock){
    now_=nowMs;
    for(auto& [other,p]:players_){p.others->entered(id,nowMs+p.clock);player.entered(other,nowMs+clock);}
    players_[id]={&player,clock};
}
void MemoryHub::leave(uint64_t id,uint64_t nowMs){
    now_=nowMs;auto it=players_.find(id);if(it==players_.end())return;
    auto gone=it->second;players_.erase(it);
    for(auto& [other,p]:players_){p.others->left(id,nowMs+p.clock);gone.others->left(other,nowMs+gone.clock);}
}
void MemoryHub::deliver(uint64_t nowMs){
    now_=nowMs;
    // What is due now, in the order sent; an answer sent while delivering waits for the next call.
    std::deque<Datagram> due;
    for(auto it=queue_.begin();it!=queue_.end();)if(it->due<=nowMs){due.push_back(std::move(*it));it=queue_.erase(it);}else ++it;
    for(auto& d:due){auto it=players_.find(d.to);if(it!=players_.end())it->second.others->received(d.from,d.bytes.data(),d.bytes.size(),nowMs+it->second.clock);}
}
}
