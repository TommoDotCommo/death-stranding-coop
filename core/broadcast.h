#pragma once
// The relay's broadcast mode: envelope version 2 (CORE.md, "Broadcast envelope"). A game publishes its presence and its
// animation sample once per tick to the players' own server (world/relay.h), which copies them to every player near
// it, several players' updates to one datagram, stamped on the server's clock. What a player only shows of himself (his
// name, gear, what he carries and where each case sits) is versioned state the server keeps and hands out: when a player
// comes near, when it changes, and when a receiver asks because it saw it had missed a version. Game-free and
// header-only, used by the server, the mod and the test tools; the player's side is broadcast_client.h.
//
// Version 1 (relay.h: one presence-core session per pair, the server passing each core datagram on) is unchanged. A
// player says in its HELLO which version it speaks; the server speaks both and puts a player near only players of the
// same version (and, in broadcast, of the same game and mod build, which the HELLO carries once).
//
// Every datagram starts with the same 8 bytes as version 1, with 2 in the version field. Integers are little endian,
// reserved bytes are zero, every datagram is at most 1200 bytes (a 1280-byte path with IPv4 and UDP headers to spare).
//
//   0  4  magic "DSR1"      4  2  version (2)      6  1  type      7  1  reserved
//
//   HELLO    1  player -> server, 140 bytes; every half second: keep-alive, position and the clock's ping
//            8..111 as version 1's HELLO | 112 u32 game build | 116 mod build[16] | 132 u64 t0: the player's clock (ms)
//   WELCOME  2  server -> player, 76 bytes; the answer to every accepted HELLO, and the clock's pong
//            8..31 as version 1's WELCOME | 32 u64 t0 echoed | 40 u64 server time (ms) | 48 u32 x4 the versions of this
//            player's state the server holds (0: none) | 64 u16 full metres | 66 u16 animation metres | 68 u16 interval
//            ms beyond full | 70 u16 players near with another build | 72 u16 datagram budget | 74 reserved[2]
//   REFUSED  3  server -> player, 24 bytes: as version 1, reason 1 server full, 2 id in use, 3 the server wants another build
//   ROSTER   4  server -> player: as version 1
//   PUBLISH  5  player -> server: 8 u64 player id | 16 u32 sequence | 20 u8 parts (1 presence, 2 animation) | 21 reserved
//            | 22 u16 animation bytes | 24 u64 stamp: the sample's time on the server's clock (ms) | 32 presence[52]
//            when part 1 | then the animation payload when part 2 (the core's ANIMATION payload, 1..1100 bytes)
//   FRAME    6  server -> player: 8 u64 server time | 16 u8 count (1..) | 17 reserved[3] | 20 count x entry:
//            u64 source | u32 sequence | u64 stamp | u8 parts | u8 reserved | u16 animation bytes | u16 state generation
//            | u16 reserved | presence[52] when part 1 | animation when part 2
//   STATE    7  player -> server: 8 u64 player id | 16 u8 kind (1..4) | 17 reserved | 18 u16 bytes (1..1100)
//            | 20 u32 version (not 0) | 24 payload
//   SHARE    8  server -> player: 8 u64 source | 16 u8 kind | 17 reserved | 18 u16 bytes | 20 u32 version | 24 u16 state
//            generation | 26 reserved[2] | 28 u32 x4 the source's versions of every kind now | 44 payload
//            (version 0 with no payload: the source holds nothing of that kind; only in the answer to a FETCH)
//   FETCH    9  player -> server, 24 bytes: 8 u64 player id | 16 u64 source (send me that player's state again)
//   BYE     10  player -> server, 48 bytes: as version 1
//
// presence[52] is the core's PRESENCE frame (core.cpp) bytes 60..63 (flags, action, phase) and 72..119 (position,
// velocity, yaw, health, stamina, the BYE reason and reserved byte, the vehicle block): the receiver rebuilds the
// 128-byte frame around it and the core's own decoder checks every value. The animation payload is the core's
// (encodeAnimationPayload). State payloads are the adapter's; the mod sends its CARGO events of that kind (crowd.h).
//
// No identifier here is called `near` or `far`: the Windows headers define both as macros.
#include "relay.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace ds::broadcast {
using relay::Buffer;using relay::Error;
constexpr uint16_t Version=2;
constexpr size_t MaxDatagram=1200;
constexpr size_t HelloSize=140,WelcomeSize=76,RefusedSize=24,FetchSize=24,ByeSize=48;
constexpr size_t PublishHeader=32,FrameHeader=20,EntryHeader=28,PresenceBytes=52,StateHeader=24,ShareHeader=44;
constexpr size_t AnimationMax=1100;    // the controls others are sent (held, rose, floats) are at most 1029 bytes
constexpr size_t StateMax=1100;
constexpr size_t StateKinds=4;
enum class Type : uint8_t { Hello=1, Welcome, Refused, Roster, Publish, Frame, State, Share, Fetch, Bye };
enum class Refusal : uint8_t { None=0, Full=1, IdInUse=2, Build=3 };
enum class StateKind : uint8_t { Name=1, Gear=2, Carry=3, Places=4 };   // state kinds
enum Part : uint8_t { HasPresence=1, HasAnimation=2 };
using Presence=std::array<uint8_t,PresenceBytes>;
using Versions=std::array<uint32_t,StateKinds>;
using BuildId=std::array<uint8_t,16>;
struct Hello { relay::Hello base; uint32_t game=0; BuildId build{}; uint64_t t0=0; };
struct Welcome { relay::Welcome base; uint64_t t0=0,serverTime=0; Versions versions{}; uint16_t fullMetres=0,animationMetres=0,intervalMs=0,otherBuild=0,budget=0; };
struct Refused { uint64_t id=0; uint32_t sequence=0; Refusal reason=Refusal::None; };
// An update: what PUBLISH carries up and what one FRAME entry carries down. payload pointers point into the datagram.
struct Update {
    uint64_t player=0;                 // PUBLISH: the sender's own id (checked against its address); FRAME: the source
    uint32_t sequence=0; uint64_t stamp=0; uint8_t parts=0; uint16_t generation=0;
    const uint8_t* presence=nullptr;   // PresenceBytes, when parts has HasPresence
    const uint8_t* animation=nullptr; size_t animationSize=0;
};
struct Frame { uint64_t serverTime=0; std::vector<Update> entries; };
struct StatePost { uint64_t player=0; StateKind kind=StateKind::Name; uint32_t version=0; const uint8_t* payload=nullptr; size_t size=0; };
struct Share { uint64_t source=0; StateKind kind=StateKind::Name; uint32_t version=0; uint16_t generation=0; Versions versions{}; const uint8_t* payload=nullptr; size_t size=0; };
struct Fetch { uint64_t player=0,source=0; };
struct Message {
    Type type=Type::Hello; Hello hello; Welcome welcome; Refused refused; relay::Roster roster; Update publish; Frame frame;
    StatePost state; Share share; Fetch fetch; relay::Bye bye;
};
// The version of a datagram's envelope (0 when it is no envelope at all): the server's dispatch.
inline uint16_t versionOf(const uint8_t* b,size_t n){
    if(!b||n<relay::HeaderSize||relay::detail::get(b,4)!=relay::Magic)return 0;
    return uint16_t(relay::detail::get(b+4,2));}
// Serial-number order for 32-bit counters: a is later than b.
inline bool later(uint32_t a,uint32_t b){return a!=b&&uint32_t(a-b)<0x80000000u;}
inline bool later16(uint16_t a,uint16_t b){return a!=b&&uint16_t(a-b)<0x8000u;}
namespace detail {
using relay::detail::put;using relay::detail::get;using relay::detail::need;
inline Buffer start(Type t,size_t size){Buffer b;b.reserve(size);put(b,relay::Magic,4);put(b,Version,2);b.push_back(uint8_t(t));b.push_back(0);return b;}
// Version 1's encoders and parser for the parts the two versions share, with the version field swapped.
inline Buffer asVersion(Buffer b,uint16_t v){b[4]=uint8_t(v);b[5]=uint8_t(v>>8);return b;}
inline Error parseShared(const uint8_t* b,size_t size,relay::Type type,relay::Message& m){
    Buffer copy(b,b+size);copy[4]=1;copy[5]=0;copy[6]=uint8_t(type);return relay::parse(copy.data(),copy.size(),m);}
inline void putVersions(Buffer& b,const Versions& v){for(auto x:v)put(b,x,4);}
inline Versions getVersions(const uint8_t* p){Versions v{};for(size_t i=0;i<StateKinds;++i)v[i]=uint32_t(get(p+4*i,4));return v;}
inline bool kindValid(uint8_t k){return k>=1&&k<=StateKinds;}
// An animation payload's shape as far as the server needs it: [u8 n <= 32][held n][rose n]...
inline bool animationShape(const uint8_t* p,size_t n){return n>=1&&n<=AnimationMax&&p[0]<=32&&size_t(1)+2*size_t(p[0])<=n;}
}
// Encoders throw invalid_argument for a message the wire cannot carry.
inline Buffer encode(const Hello& h){
    using namespace detail;need(h.t0<=(1ull<<53),"broadcast: t0");
    auto b=asVersion(relay::encode(h.base),Version);b.reserve(HelloSize);
    put(b,h.game,4);b.insert(b.end(),h.build.begin(),h.build.end());put(b,h.t0,8);return b;}
inline Buffer encode(const Welcome& w){
    using namespace detail;need(w.t0<=(1ull<<53)&&w.serverTime<=(1ull<<53),"broadcast: time");
    auto b=asVersion(relay::encode(w.base),Version);b.reserve(WelcomeSize);
    put(b,w.t0,8);put(b,w.serverTime,8);putVersions(b,w.versions);
    put(b,w.fullMetres,2);put(b,w.animationMetres,2);put(b,w.intervalMs,2);put(b,w.otherBuild,2);put(b,w.budget,2);put(b,0,2);return b;}
inline Buffer encode(const Refused& r){
    using namespace detail;need(r.id!=0,"broadcast: player id 0");need(r.reason>=Refusal::Full&&r.reason<=Refusal::Build,"broadcast: reason");
    auto b=start(Type::Refused,RefusedSize);put(b,r.id,8);put(b,r.sequence,4);b.push_back(uint8_t(r.reason));put(b,0,3);return b;}
inline Buffer encodeRoster(const relay::Roster& r){return detail::asVersion(relay::encode(r),Version);}
inline Buffer encodeBye(const relay::Bye& y){auto b=detail::asVersion(relay::encode(y),Version);b[6]=uint8_t(Type::Bye);return b;}
inline size_t updateSize(const Update& u){return (u.parts&HasPresence?PresenceBytes:0)+(u.parts&HasAnimation?u.animationSize:0);}
inline void checkUpdate(const Update& u){
    using namespace detail;
    need(u.player!=0,"broadcast: player id 0");need(u.parts&&!(u.parts&~3),"broadcast: parts");need(u.stamp<=(1ull<<53),"broadcast: stamp");
    need(!(u.parts&HasPresence)||u.presence,"broadcast: presence");
    need(u.parts&HasAnimation?(u.animation&&animationShape(u.animation,u.animationSize)):u.animationSize==0,"broadcast: animation");}
inline Buffer encodePublish(const Update& u){
    using namespace detail;checkUpdate(u);
    auto b=start(Type::Publish,PublishHeader+updateSize(u));put(b,u.player,8);put(b,u.sequence,4);b.push_back(u.parts);b.push_back(0);
    put(b,u.animationSize,2);put(b,u.stamp,8);
    if(u.parts&HasPresence)b.insert(b.end(),u.presence,u.presence+PresenceBytes);
    if(u.parts&HasAnimation)b.insert(b.end(),u.animation,u.animation+u.animationSize);
    return b;}
// FRAME, built entry by entry by the server: begin, append while it fits, finish with the count and the time.
inline size_t entrySize(const Update& u){return EntryHeader+updateSize(u);}
inline void beginFrame(Buffer& b){b.clear();b=detail::start(Type::Frame,MaxDatagram);detail::put(b,0,8);b.push_back(0);detail::put(b,0,3);}
inline void appendEntry(Buffer& b,const Update& u){
    using namespace detail;checkUpdate(u);need(b.size()+entrySize(u)<=MaxDatagram&&b[16]<255,"broadcast: frame full");
    put(b,u.player,8);put(b,u.sequence,4);put(b,u.stamp,8);b.push_back(u.parts);b.push_back(0);put(b,u.animationSize,2);put(b,u.generation,2);put(b,0,2);
    if(u.parts&HasPresence)b.insert(b.end(),u.presence,u.presence+PresenceBytes);
    if(u.parts&HasAnimation)b.insert(b.end(),u.animation,u.animation+u.animationSize);
    ++b[16];}
inline void finishFrame(Buffer& b,uint64_t serverTime){
    detail::need(b.size()>FrameHeader&&b[16]>0&&serverTime<=(1ull<<53),"broadcast: frame");
    for(unsigned i=0;i<8;++i)b[8+i]=uint8_t(serverTime>>(8*i));}
inline Buffer encode(const StatePost& s){
    using namespace detail;need(s.player!=0,"broadcast: player id 0");need(kindValid(uint8_t(s.kind)),"broadcast: kind");need(s.version!=0,"broadcast: version");
    need(s.payload&&s.size>=1&&s.size<=StateMax,"broadcast: state size");
    auto b=start(Type::State,StateHeader+s.size);put(b,s.player,8);b.push_back(uint8_t(s.kind));b.push_back(0);put(b,s.size,2);put(b,s.version,4);
    b.insert(b.end(),s.payload,s.payload+s.size);return b;}
inline Buffer encode(const Share& s){
    using namespace detail;need(s.source!=0,"broadcast: player id 0");need(kindValid(uint8_t(s.kind)),"broadcast: kind");
    need((s.version==0)==(s.size==0)&&s.size<=StateMax&&(!s.size||s.payload),"broadcast: state size");
    need(s.versions[size_t(s.kind)-1]==s.version,"broadcast: versions");
    auto b=start(Type::Share,ShareHeader+s.size);put(b,s.source,8);b.push_back(uint8_t(s.kind));b.push_back(0);put(b,s.size,2);put(b,s.version,4);
    put(b,s.generation,2);put(b,0,2);putVersions(b,s.versions);if(s.size)b.insert(b.end(),s.payload,s.payload+s.size);return b;}
inline Buffer encode(const Fetch& f){
    using namespace detail;need(f.player!=0&&f.source!=0,"broadcast: player id 0");
    auto b=start(Type::Fetch,FetchSize);put(b,f.player,8);put(b,f.source,8);return b;}

// Refused whole: another magic or version, an unknown type, a nonzero reserved byte, a size that is not the type's, a
// player id 0, a part, kind, count or length out of range, entries that do not fill a FRAME exactly.
inline Error parse(const uint8_t* b,size_t n,Message& m){
    using namespace detail;
    if(!b||n<relay::HeaderSize||n>MaxDatagram)return Error::Length;
    if(get(b,4)!=relay::Magic)return Error::Magic;
#ifndef MUTANT_BCAST_WIRE_VERSION
    if(get(b+4,2)!=Version)return Error::Version;
#endif
    if(b[6]<1||b[6]>10)return Error::Type;
    if(b[7])return Error::Reserved;
    m.type=Type(b[6]);
    auto exact=[&](size_t size){
#ifndef MUTANT_BCAST_WIRE_LENGTH
        return n==size;
#else
        return n>=size;
#endif
    };
    // An update's parts and lengths, from the bytes at p (parts) and q (animation bytes); `rest` bytes follow its header.
    auto update=[&](uint8_t parts,uint8_t reserved,size_t animation,size_t rest)->Error{
        if(!parts||(parts&~3)||reserved)return parts&~3||reserved?Error::Reserved:Error::Range;
        if(!(parts&HasAnimation)&&animation)return Error::Range;
        if((parts&HasAnimation)&&(animation<1||animation>AnimationMax))return Error::Range;
        size_t want=(parts&HasPresence?PresenceBytes:0)+animation;
        return rest<want?Error::Length:Error::None;};
    switch(m.type){
    case Type::Hello:{
        if(!exact(HelloSize))return Error::Length;
        relay::Message shared;auto e=parseShared(b,112,relay::Type::Hello,shared);if(e!=Error::None)return e;
        m.hello.base=std::move(shared.hello);m.hello.game=uint32_t(get(b+112,4));std::memcpy(m.hello.build.data(),b+116,16);m.hello.t0=get(b+132,8);
        if(m.hello.t0>(1ull<<53))return Error::Range;
        return Error::None;}
    case Type::Welcome:{
        if(!exact(WelcomeSize))return Error::Length;
        relay::Message shared;auto e=parseShared(b,32,relay::Type::Welcome,shared);if(e!=Error::None)return e;
        auto& w=m.welcome;w.base=shared.welcome;w.t0=get(b+32,8);w.serverTime=get(b+40,8);w.versions=getVersions(b+48);
        w.fullMetres=uint16_t(get(b+64,2));w.animationMetres=uint16_t(get(b+66,2));w.intervalMs=uint16_t(get(b+68,2));w.otherBuild=uint16_t(get(b+70,2));w.budget=uint16_t(get(b+72,2));
        if(get(b+74,2))return Error::Reserved;
        if(w.t0>(1ull<<53)||w.serverTime>(1ull<<53))return Error::Range;
        return Error::None;}
    case Type::Refused:{
        if(!exact(RefusedSize))return Error::Length;
        Refused r;r.id=get(b+8,8);if(!r.id)return Error::Range;r.sequence=uint32_t(get(b+16,4));
        if(b[20]<1||b[20]>3)return Error::Range;
        if(get(b+21,3))return Error::Reserved;
        r.reason=Refusal(b[20]);m.refused=r;return Error::None;}
    case Type::Roster:{
        relay::Message shared;auto e=parseShared(b,n,relay::Type::Roster,shared);if(e!=Error::None)return e;
        m.roster=std::move(shared.roster);return Error::None;}
    case Type::Publish:{
        if(n<PublishHeader)return Error::Length;
        Update u;u.player=get(b+8,8);if(!u.player)return Error::Range;
        u.sequence=uint32_t(get(b+16,4));u.parts=b[20];u.animationSize=size_t(get(b+22,2));u.stamp=get(b+24,8);
        if(u.stamp>(1ull<<53))return Error::Range;
        auto e=update(u.parts,b[21],u.animationSize,n-PublishHeader);if(e!=Error::None)return e;
        if(n!=PublishHeader+updateSize(u))return Error::Length;
        size_t o=PublishHeader;
        if(u.parts&HasPresence){u.presence=b+o;o+=PresenceBytes;}
        if(u.parts&HasAnimation){u.animation=b+o;if(!animationShape(u.animation,u.animationSize))return Error::Range;}
        m.publish=u;return Error::None;}
    case Type::Frame:{
        if(n<FrameHeader+EntryHeader)return Error::Length;
        Frame f;f.serverTime=get(b+8,8);if(f.serverTime>(1ull<<53))return Error::Range;
        size_t count=b[16];if(!count)return Error::Range;
        if(b[17]||b[18]||b[19])return Error::Reserved;
        size_t o=FrameHeader;
        for(size_t i=0;i<count;++i){
            if(o+EntryHeader>n)return Error::Length;
            Update u;u.player=get(b+o,8);if(!u.player)return Error::Range;
            u.sequence=uint32_t(get(b+o+8,4));u.stamp=get(b+o+12,8);u.parts=b[o+20];u.animationSize=size_t(get(b+o+22,2));u.generation=uint16_t(get(b+o+24,2));
            if(u.stamp>(1ull<<53))return Error::Range;
            if(get(b+o+26,2))return Error::Reserved;
            auto e=update(u.parts,b[o+21],u.animationSize,n-o-EntryHeader);if(e!=Error::None)return e;
            o+=EntryHeader;
            if(u.parts&HasPresence){u.presence=b+o;o+=PresenceBytes;}
            if(u.parts&HasAnimation){u.animation=b+o;if(!animationShape(u.animation,u.animationSize))return Error::Range;o+=u.animationSize;}
            f.entries.push_back(u);}
#ifndef MUTANT_BCAST_WIRE_FRAME
        if(o!=n)return Error::Length;
#endif
        m.frame=std::move(f);return Error::None;}
    case Type::State:{
        if(n<=StateHeader)return Error::Length;
        StatePost s;s.player=get(b+8,8);if(!s.player)return Error::Range;
#ifndef MUTANT_BCAST_WIRE_RANGE
        if(!kindValid(b[16]))return Error::Range;
#endif
        if(b[17])return Error::Reserved;
        s.kind=StateKind(b[16]);s.size=size_t(get(b+18,2));s.version=uint32_t(get(b+20,4));
        if(!s.version||s.size<1||s.size>StateMax)return Error::Range;
        if(n!=StateHeader+s.size)return Error::Length;
        s.payload=b+StateHeader;m.state=s;return Error::None;}
    case Type::Share:{
        if(n<ShareHeader)return Error::Length;
        Share s;s.source=get(b+8,8);if(!s.source)return Error::Range;
        if(!kindValid(b[16]))return Error::Range;
        if(b[17]||get(b+26,2))return Error::Reserved;
        s.kind=StateKind(b[16]);s.size=size_t(get(b+18,2));s.version=uint32_t(get(b+20,4));s.generation=uint16_t(get(b+24,2));s.versions=getVersions(b+28);
        if((s.version==0)!=(s.size==0)||s.size>StateMax||s.versions[size_t(s.kind)-1]!=s.version)return Error::Range;
        if(n!=ShareHeader+s.size)return Error::Length;
        s.payload=s.size?b+ShareHeader:nullptr;m.share=s;return Error::None;}
    case Type::Fetch:{
        if(!exact(FetchSize))return Error::Length;
        m.fetch.player=get(b+8,8);m.fetch.source=get(b+16,8);
        if(!m.fetch.player||!m.fetch.source)return Error::Range;
        return Error::None;}
    case Type::Bye:{
        relay::Message shared;auto e=parseShared(b,n,relay::Type::Bye,shared);if(e!=Error::None)return e;
        m.bye=std::move(shared.bye);return Error::None;}
    }
    return Error::Type;
}

// The presence block: the core's PRESENCE bytes 60..63 and 72..119 (see the top), and the few fields the server reads.
inline Presence presenceOf(const uint8_t* frame128){Presence p{};std::memcpy(p.data(),frame128+60,4);std::memcpy(p.data()+4,frame128+72,48);return p;}
inline void presenceInto(const uint8_t* block,uint8_t* frame128){std::memcpy(frame128+60,block,4);std::memcpy(frame128+72,block+4,48);}
inline uint16_t presenceFlags(const uint8_t* block){return uint16_t(relay::detail::get(block,2));}
inline std::array<double,3> presencePosition(const uint8_t* block){
    std::array<double,3> p{};for(unsigned i=0;i<3;++i)p[i]=int32_t(uint32_t(relay::detail::get(block+4+4*i,4)))/100.;return p;}
constexpr uint16_t TeleportFlag=32;   // ds::Teleport
}
