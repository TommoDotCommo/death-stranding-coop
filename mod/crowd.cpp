#include "crowd.h"
#include <algorithm>
#include <cmath>
namespace mod::crowd {
namespace bc=ds::broadcast;
namespace {
// One complete set from a fresh receiver of the kind: it must come exactly with the last event (one blob is one
// sender output; a set that ends early is a blob of something else).
template<class Receiver> auto whole(Receiver receiver,const std::vector<ds::CargoEvent>& events)->decltype(receiver.feed(events.front())){
    decltype(receiver.feed(events.front())) out;
    for(size_t i=0;i<events.size();++i){
        auto r=receiver.feed(events[i]);if(!r)continue;
#ifndef MUTANT_CROWD_WHOLE
        if(i+1!=events.size())return {};
#endif
        out=std::move(r);
    }
    return out;
}
ds::broadcast::ClientConfig configured(ds::broadcast::ClientConfig c,const others::Settings& s){c.id=s.self;c.build=s.build;c.delayMs=s.delayMs;return c;}
}
Crowd::Crowd(others::Settings s,bc::ClientConfig c,bc::Client::Send send,std::function<void(const std::string&)> log)
    :settings_(std::move(s)),log_(log?std::move(log):[](const std::string&){}),client_(configured(std::move(c),settings_),std::move(send)){
    settings_.cap=std::min(settings_.cap,others::MaxOthers);
    // The name goes as soon as the server is reached and stays there: whoever comes near is handed it.
    post(bc::StateKind::Name,nametag::encode(settings_.nickname));
}
bool Crowd::post(bc::StateKind kind,const std::vector<ds::CargoEvent>& events){
    auto blob=pack(events);
    if(blob.empty()||!client_.setState(kind,blob)){log_("OTHERS a set of "+std::to_string(events.size())+" events of kind "+std::to_string(int(kind))+" does not fit the wire\n");return false;}
    return true;
}
void Crowd::receive(const uint8_t* data,size_t size,uint64_t nowMs){client_.receive(data,size,nowMs);}
void Crowd::bye(){client_.bye();}
void Crowd::local(const others::Local& l){
    local_=l;local_.state.flags&=uint16_t(~ds::Teleport);
    client_.setLocal(l.state,l.available,l.gated,l.sampledAt,l.jumped);
    // Where this porter is: the server decides from it who is near. No place while there is no pose (loading, cutscene).
    if(l.available&&!l.gated)client_.setPosition(l.state.position,l.area);else client_.clearPosition();
}
void Crowd::admit(uint64_t id){
    if(!settings_.cap||!id||id==settings_.self||members_.count(id))return;
#ifndef MUTANT_CROWD_CAP
    if(members_.size()>=settings_.cap){
        ++full_;
        // Remembered (bounded), so the player is taken when somebody leaves.
        if(std::find(waiting_.begin(),waiting_.end(),id)==waiting_.end()&&waiting_.size()<4*others::MaxOthers)waiting_.push_back(id);
        return;}
#endif
    members_[id];
    log_("OTHERS player "+std::to_string(id)+" is near ("+std::to_string(members_.size())+" now)\n");
}
void Crowd::tick(uint64_t nowMs){
    client_.tick(nowMs);
    for(auto& e:client_.takeEvents()){
        if(e.enter){admit(e.id);continue;}
        waiting_.erase(std::remove(waiting_.begin(),waiting_.end(),e.id),waiting_.end());
#ifndef MUTANT_CROWD_LEFT
        if(!members_.erase(e.id))continue;
        log_("OTHERS player "+std::to_string(e.id)+" left ("+std::to_string(members_.size())+" now)\n");
        while(!waiting_.empty()&&members_.size()<settings_.cap){auto next=waiting_.front();waiting_.pop_front();if(client_.nearby().count(next))admit(next);}
#endif
    }
    for(auto& [id,m]:members_){
        bool live=client_.online()&&client_.live(id,nowMs);
#ifndef MUTANT_CROWD_CARRY_LOST
        // Gone quiet: what that player carried is not known any more. Heard again: the server's copy is taken again.
        if(m.live&&!live){
            m.carried.reset();m.places.reset();
#ifndef MUTANT_CROWD_CARRY_BACK
            m.revision[size_t(bc::StateKind::Carry)-1]=m.revision[size_t(bc::StateKind::Places)-1]=~uint64_t(0);
#endif
        }
#endif
        m.live=live;take(id,m,nowMs);
    }
}
// The state blobs of one player that changed since the last look, by the pairs mode's rules: the name always, gear
// with Settings::gear, what is carried and where with Settings::carry (and only while that player is heard).
void Crowd::take(uint64_t id,Member& m,uint64_t nowMs){
    (void)nowMs;
    for(size_t k=0;k<bc::StateKinds;++k){
        auto kind=bc::StateKind(k+1);bool carried=kind==bc::StateKind::Carry||kind==bc::StateKind::Places;
        if(carried&&!m.live)continue;
        uint64_t revision=0;auto* blob=client_.state(id,kind,&revision);
        if(revision==m.revision[k])continue;
        m.revision[k]=revision;
#ifndef MUTANT_CROWD_MIX
        Member& to=m;
#else
        Member& to=members_.begin()->second;
#endif
        // The player holds nothing of this kind now (a game that started again): nothing carried; worn and name stay.
        if(!blob){if(kind==bc::StateKind::Carry)to.carried.reset();if(kind==bc::StateKind::Places)to.places.reset();continue;}
#ifndef MUTANT_CROWD_EVENTS_OFF
        bool on=kind==bc::StateKind::Name||(kind==bc::StateKind::Gear&&settings_.gear)||(carried&&settings_.carry);
#else
        bool on=true;
#endif
        auto events=unpack(*blob,typeOf(kind));
        if(!events){++malformed_;continue;}
        if(!on){dropped_+=events->size();continue;}
        switch(kind){
        case bc::StateKind::Name:
            if(auto n=whole(nametag::Receiver{},*events)){if(*n!=to.name)log_("OTHERS player "+std::to_string(id)+" has a name of "+std::to_string(n->size())+" bytes\n");to.name=*n;}
            else ++malformed_;
            break;
        case bc::StateKind::Gear:
            if(auto w=whole(gear::WornReceiver{},*events)){++taken_[0];
                if(!to.worn||*to.worn!=*w)log_("OTHERS player "+std::to_string(id)+" shows "+std::to_string(w->size())+" pieces of gear\n");
                to.worn=std::move(*w);}
            else ++malformed_;
            break;
        case bc::StateKind::Carry:
            if(auto c=whole(carry::Receiver{},*events)){++taken_[1];
                if(!to.carried||*to.carried!=*c)log_("OTHERS player "+std::to_string(id)+" carries "+std::to_string(c->size())+" entries\n");
                to.carried=std::move(*c);}
            else ++malformed_;
            break;
        case bc::StateKind::Places:
            if(auto p=whole(carryplace::Receiver{},*events)){++taken_[1];to.places=std::move(*p);}
            else ++malformed_;
            break;
        }
    }
}
bool Crowd::animation(const ds::Animation& a,uint64_t nowMs){
    if(!client_.online()){rose_={};return false;}
    for(size_t k=0;k<rose_.size();++k)rose_[k]|=a.rose[k];
    // The controls only: what he holds, his vehicle's wheels and his carts are not shown for others.
    ds::Animation x;x.held=a.held;x.floats=a.floats;x.rose=rose_;
    bool taken=client_.offerAnimation(x,nowMs);
#ifndef MUTANT_CROWD_ROSE
    if(taken)rose_={};   // pulses of a sample not taken go with the next
#else
    rose_={};
#endif
    return taken;
}
size_t Crowd::wears(const std::optional<gear::WornSet>& read,uint64_t nowMs){
#ifndef MUTANT_CROWD_SEND_OFF
    if(!settings_.gear)return 0;
#endif
    auto events=wornTx_.observe(read,client_.online(),nowMs);
    if(events.empty()||!post(bc::StateKind::Gear,events))return 0;
    return events.size();
}
size_t Crowd::carries(const std::optional<carry::Summary>& summary,const std::optional<std::vector<carryplace::Placed>>& placed,uint64_t nowMs){
#ifndef MUTANT_CROWD_SEND_OFF
    if(!settings_.carry)return 0;
#endif
    size_t queued=0;bool up=client_.online();
    auto events=carryTx_.observe(summary,up,nowMs);
    if(!events.empty()&&post(bc::StateKind::Carry,events))queued+=events.size();
    events=placeTx_.observe(placed,up,nowMs);   // nothing on a pass that did not measure the places
    if(!events.empty()&&post(bc::StateKind::Places,events))queued+=events.size();
    return queued;
}
std::vector<others::Shown> Crowd::frame(uint64_t nowMs){
    std::vector<others::Shown> out;out.reserve(members_.size());
    for(auto& [id,m]:members_){
        others::Shown v;v.id=id;v.connected=client_.online()&&client_.live(id,nowMs);v.render=client_.render(id,nowMs);
#ifndef MUTANT_CROWD_GATED
        v.render.visible=v.render.visible&&!local_.gated;   // this game in a cutscene or loading: nobody is shown
#endif
        v.visible=v.connected&&v.render.visible;v.name=m.name;
        if(v.connected)v.animation=client_.takeAnimation(id,nowMs);
        v.worn=m.worn;v.carried=m.carried;v.places=m.places;
        out.push_back(std::move(v));
    }
    return out;
}
void Crowd::clear(uint64_t nowMs){
    (void)nowMs;waiting_.clear();
    for(auto& [id,m]:members_)log_("OTHERS player "+std::to_string(id)+" left (the relay went away)\n");
    members_.clear();
}
size_t Crowd::connected() const {size_t n=0;for(auto& [id,m]:members_)n+=m.live;return n;}
const others::Counters& Crowd::counters() const {
    auto& k=client_.counters();
    counters_.sent=k.sent;counters_.received=k.received;counters_.accepted=k.accepted;counters_.unknown=k.unknown;
    counters_.malformed=k.malformed+malformed_;counters_.refused=k.refused;counters_.full=full_;counters_.events=dropped_;
    counters_.gear=taken_[0];counters_.carried=taken_[1];
    return counters_;
}
}
