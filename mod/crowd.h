#pragma once
// Crowd: the other players through the relay's broadcast mode (config relay_mode "broadcast"; core/broadcast.h,
// core/broadcast_client.h, world/relay.h). Where Others (others.h) runs one presence-core session for every other
// player and the relay carries each pair's datagrams, here one ds::broadcast::Client publishes this porter once a tick
// to the players' own server, which copies it to everybody near; what each player only shows of himself (name, gear,
// carried cases, where each case sits) is versioned state the server keeps and hands out. Both are Players (others.h),
// so the mod's bodies, labels, gear and cases take the same Shown from either.
//
// The state: one blob per kind, and one blob is one complete output of the partner path's own senders, its events
// written as the core's CARGO fields (the same values a CARGO datagram would deliver):
//
//   0  u8 count (1..40)   then count x 27 bytes:  u8 type | u32 item | u64 ref (at most 2^53)
//                         | i32 x3 position (centimetres, within 100 km) | i16 yaw (pi/32767 steps, -32767..32767)
//
//   kind 1 name    type 10 (nametag.h encode)          kind 2 gear    type 7 (gear.h WornSender)
//   kind 3 carry   type 6 (carry_visual.h Sender)      kind 4 places  type 16 (carry_place.h Sender)
//
// A blob is taken only whole: every event of its kind's type and the kind's receiver giving its complete set exactly
// at the last event. What is taken follows the pairs mode's rules: the name always, gear with Settings::gear, carried
// cases and their places with Settings::carry; the blobs of a kind that is off are dropped (counted as events). This
// porter's own sets go out by the same senders' rules (once they have held still; not too often), as one blob each.
// What differs from the pairs mode: a player is "connected" while his presence arrives (a sample taken within the last
// 3 s) rather than while a session is up; what he carried is forgotten when he goes quiet and taken again from the
// server's copy when he is heard again (nothing is resent: the server hands the state to whoever comes near).
//
// Nothing here opens a socket: the client's datagrams go through the send function the caller gives, and the caller
// hands over only datagrams from the relay's address (receive). Every call must come from one thread at a time (the mod
// makes them under its runtime lock).
#include "others.h"
#include "../core/broadcast_client.h"
#include <cmath>
#include <deque>
namespace mod::crowd {
constexpr size_t EventBytes=27,MaxEvents=40;   // 1 + 40 x 27 = 1081 <= ds::broadcast::StateMax
using Blob=std::vector<uint8_t>;
namespace detail {
constexpr uint64_t TimeMax=1ull<<53;   // the core's limit for every t-field
constexpr double Pi=3.14159265358979323846;
constexpr int32_t CentimetresMax=10000000;
}
// Header-only, so the stand-in tool (swarm.cpp) can post a name without the bodies' code.
// The state kind's CARGO event type (ds::broadcast::StateKind 1..4).
// pack: the events as a blob (empty when they cannot be carried: none, too many, or a value outside the wire's range).
// unpack: the events of a blob, every one of type `type` (nothing: not a blob of that type).
inline uint8_t typeOf(ds::broadcast::StateKind kind){
    switch(kind){
    case ds::broadcast::StateKind::Name:return nametag::WireType;
    case ds::broadcast::StateKind::Gear:return gear::GearWireType;
    case ds::broadcast::StateKind::Carry:return carry::WireType;
    case ds::broadcast::StateKind::Places:return carryplace::WireType;
    }
    return 0;
}
inline Blob pack(const std::vector<ds::CargoEvent>& events){
    if(events.empty()||events.size()>MaxEvents)return {};
    Blob b;b.reserve(1+EventBytes*events.size());b.push_back(uint8_t(events.size()));
    for(const auto& e:events){
        if(e.ref>detail::TimeMax)return {};
        double yaw=e.yaw*32767/detail::Pi;if(!std::isfinite(yaw)||std::fabs(std::round(yaw))>32767)return {};
        b.push_back(e.type);ds::relay::detail::put(b,e.item,4);ds::relay::detail::put(b,e.ref,8);
        for(double x:e.position){double cm=std::round(x*100);if(!std::isfinite(cm)||std::fabs(cm)>detail::CentimetresMax)return {};ds::relay::detail::put(b,uint32_t(int32_t(cm)),4);}
        ds::relay::detail::put(b,uint16_t(int16_t(std::lround(yaw))),2);
    }
    return b;
}
inline std::optional<std::vector<ds::CargoEvent>> unpack(const Blob& b,uint8_t type){
    if(b.empty())return std::nullopt;
    size_t n=b[0];
#ifndef MUTANT_CROWD_BLOB_LENGTH
    if(!n||n>MaxEvents||b.size()!=1+EventBytes*n)return std::nullopt;
#else
    if(!n||n>MaxEvents||b.size()<1+EventBytes*n)return std::nullopt;
#endif
    std::vector<ds::CargoEvent> out;out.reserve(n);
    for(size_t i=0;i<n;++i){
        const uint8_t* p=b.data()+1+EventBytes*i;ds::CargoEvent e;
#ifndef MUTANT_CROWD_BLOB_TYPE
        if(p[0]!=type)return std::nullopt;
#endif
        e.type=p[0];e.item=uint32_t(ds::relay::detail::get(p+1,4));e.ref=ds::relay::detail::get(p+5,8);
        auto steps=int16_t(uint16_t(ds::relay::detail::get(p+25,2)));
        for(unsigned k=0;k<3;++k){auto cm=int32_t(uint32_t(ds::relay::detail::get(p+13+4*k,4)));e.position[k]=cm/100.;
#ifndef MUTANT_CROWD_BLOB_RANGE
            if(cm<-detail::CentimetresMax||cm>detail::CentimetresMax)return std::nullopt;
#endif
        }
#ifndef MUTANT_CROWD_BLOB_RANGE
        // What the core's decoder refuses in a CARGO datagram: a t-field beyond 2^53, a place beyond 100 km, a yaw beyond pi.
        if(e.ref>detail::TimeMax||steps==-32768)return std::nullopt;
#endif
        e.yaw=steps*detail::Pi/32767.;e.sequence=uint32_t(i+1);e.id=i+1;
        out.push_back(e);
    }
    return out;
}


class Crowd final:public others::Players {
public:
    // Settings::self is the client's id; session is not used (the server keeps groups apart by its key).
    Crowd(others::Settings,ds::broadcast::ClientConfig,ds::broadcast::Client::Send,std::function<void(const std::string&)> log);
    // One datagram from the relay's address.
    void receive(const uint8_t*,size_t,uint64_t nowMs);
    void bye();                          // leaving: one BYE to the server; everybody near leaves
    const ds::broadcast::Client& client() const {return client_;}
    size_t waiting() const {return waiting_.size();}
    // Players.
    void local(const others::Local&) override;
    void tick(uint64_t nowMs) override;
    bool animation(const ds::Animation&,uint64_t nowMs) override;
    size_t wears(const std::optional<gear::WornSet>&,uint64_t nowMs) override;
    size_t carries(const std::optional<carry::Summary>&,const std::optional<std::vector<carryplace::Placed>>&,uint64_t nowMs) override;
    std::optional<std::array<double,3>> here() const override {if(!local_.available)return std::nullopt;return local_.state.position;}
    std::vector<others::Shown> frame(uint64_t nowMs) override;
    void clear(uint64_t nowMs) override;
    size_t size() const override {return members_.size();}
    size_t connected() const override;
    const others::Counters& counters() const override;
    const others::Settings& settings() const override {return settings_;}
private:
    struct Member {
        std::string name;std::optional<gear::WornSet> worn;std::optional<carry::Summary> carried;std::optional<std::vector<carryplace::Placed>> places;
        std::array<uint64_t,ds::broadcast::StateKinds> revision{};bool live=false;
    };
    void admit(uint64_t id);
    void take(uint64_t id,Member&,uint64_t nowMs);
    bool post(ds::broadcast::StateKind,const std::vector<ds::CargoEvent>&);
    others::Settings settings_;std::function<void(const std::string&)> log_;ds::broadcast::Client client_;
    others::Local local_;mutable others::Counters counters_;uint64_t dropped_=0,malformed_=0,taken_[2]{},full_=0;
    std::map<uint64_t,Member> members_;std::deque<uint64_t> waiting_;
    std::array<uint8_t,ds::AnimationBoolBytes> rose_{};
    gear::WornSender wornTx_;carry::Sender carryTx_;carryplace::Sender placeTx_;
};
}
