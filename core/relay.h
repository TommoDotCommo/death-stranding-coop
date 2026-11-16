#pragma once
// The relay envelope: how a player's game reaches the other players through the players' own server (world/relay.h)
// instead of one direct link. Game-free and header-only, used by the server, the mod and the test tools.
//
// Every datagram starts with the same 8 bytes; integers are little endian; reserved bytes are zero; every type but
// ROSTER, DATA and RELAYED has one exact size; a datagram that is cut short, has bytes left over, names another
// version or type, or carries a value outside its range is refused whole (CORE.md, "Relay envelope").
//
//   0  4  magic "DSR1"      4  2  version (1)      6  1  type      7  1  reserved
//
//   HELLO    1  player -> server, 112 bytes; also the keep-alive, sent every half second
//            8 u64 player id (not 0) | 16 token[32] | 48 u32 hello sequence | 52 u32 roster stream | 56 u32 roster ack
//            60 u32 area | 64 u8 flags (1 = position given) | 65 reserved[3] | 68 i32 x3 position, centimetres
//            80 nickname[32]
//   WELCOME  2  server -> player, 32 bytes; the answer to every accepted HELLO
//            8 u64 player id | 16 u32 hello sequence | 20 u32 roster stream | 24 u16 enter metres | 26 u16 leave metres
//            28 u16 timeout seconds | 30 u16 players near now
//   REFUSED  3  server -> player, 24 bytes; only to a HELLO with the right token
//            8 u64 player id | 16 u32 hello sequence | 20 u8 reason (1 server full, 2 id in use) | 21 reserved[3]
//   ROSTER   4  server -> player; who came near and who is no longer near, in order
//            8 u32 roster stream | 12 u32 sequence of the first event | 16 u8 count (1..24) | 17 reserved[3]
//            20 count x ( u8 event (1 enter, 2 leave) | u64 player id | u8 n (0..32) | nickname[n] )
//   DATA     5  player -> server:  8 u64 destination player id | 16 payload (1..1252 bytes, one core datagram)
//   RELAYED  6  server -> player:  8 u64 source player id      | 16 payload
//   BYE      7  player -> server, 48 bytes:  8 u64 player id | 16 token[32]
//
// token: the group's secret (letters, digits, - and _; zero padded). nickname: at most 32 bytes, no control bytes, zero
// padded. position: the axes of the core's State (metres there), within 100 km; area: an adapter-defined number for
// the map the player is in (0 when it has none); players in different areas are never near each other.
//
// The roster is a reliable ordered stream, because a lost ENTER would leave two players near each other unseen: the
// server numbers the events of a stream from 1 and repeats the unacknowledged ones; the player applies only the next
// one and acknowledges in every HELLO. A new stream number in a WELCOME means the server started again from nothing
// (the player timed out there, or the server restarted): the player forgets everyone and starts at 0. A HELLO with
// stream 0 asks the server for exactly that.
//
// No identifier here is called `near` or `far`: the Windows headers define both as macros.
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace ds::relay {
constexpr uint32_t Magic=0x31525344;   // "DSR1"
constexpr uint16_t Version=1;
constexpr size_t HeaderSize=8,TokenBytes=32,NicknameBytes=32,RosterEventsMax=24;
constexpr size_t MaxPayload=1252;                       // the core's longest datagram (AnimationWireMax)
constexpr size_t DataHeader=HeaderSize+8,MaxDatagram=DataHeader+MaxPayload;
constexpr size_t HelloSize=112,WelcomeSize=32,RefusedSize=24,ByeSize=48,RosterHeader=20;
constexpr double PositionMax=100000;                    // metres on each axis, like the core
enum class Type : uint8_t { Hello=1, Welcome, Refused, Roster, Data, Relayed, Bye };
enum class Refusal : uint8_t { None=0, Full=1, IdInUse=2 };
enum class Error { None, Length, Magic, Version, Type, Reserved, Range, Text };
struct Hello {
    uint64_t id=0; std::string token,nickname;
    uint32_t sequence=0,stream=0,ack=0,area=0;
    bool havePosition=false; std::array<double,3> position{};
};
struct Welcome { uint64_t id=0; uint32_t sequence=0,stream=0; uint16_t enterMetres=0,leaveMetres=0,timeoutSeconds=0,nearby=0; };
struct Refused { uint64_t id=0; uint32_t sequence=0; Refusal reason=Refusal::None; };
struct RosterEvent { bool enter=true; uint64_t id=0; std::string nickname; };
struct Roster { uint32_t stream=0,first=0; std::vector<RosterEvent> events; };
struct Data { uint64_t peer=0; const uint8_t* payload=nullptr; size_t size=0; };   // payload points into the parsed datagram
struct Bye { uint64_t id=0; std::string token; };
struct Message { Type type=Type::Hello; Hello hello; Welcome welcome; Refused refused; Roster roster; Data data; Bye bye; };
using Buffer=std::vector<uint8_t>;

inline bool tokenText(const std::string& s){
    if(s.size()>TokenBytes)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    return true;}
inline bool nicknameText(const std::string& s){
    if(s.size()>NicknameBytes)return false;
    for(unsigned char c:s)if(c<0x20||c==0x7f)return false;
    return true;}
inline bool positionValid(const std::array<double,3>& p){
    for(double x:p)if(!std::isfinite(x)||std::fabs(x)>PositionMax)return false;
    return true;}
namespace detail {
inline void put(Buffer& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
inline uint64_t get(const uint8_t* p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline Buffer start(Type t,size_t size){Buffer b;b.reserve(size);put(b,Magic,4);put(b,Version,2);b.push_back(uint8_t(t));b.push_back(0);return b;}
inline void putPadded(Buffer& b,const std::string& s,size_t n){b.insert(b.end(),s.begin(),s.end());b.insert(b.end(),n-s.size(),0);}
// Zero-padded text: nothing but zeros after the first zero.
inline bool getPadded(const uint8_t* p,size_t n,std::string& out){
    size_t length=0;while(length<n&&p[length])++length;
    for(size_t i=length;i<n;++i)if(p[i])return false;
    out.assign(reinterpret_cast<const char*>(p),length);return true;}
inline void need(bool ok,const char* what){if(!ok)throw std::invalid_argument(what);}
}
// Encoders throw invalid_argument for a message the wire cannot carry.
inline Buffer encode(const Hello& h){
    using namespace detail;
    need(h.id!=0,"relay: player id 0");need(tokenText(h.token),"relay: token");need(nicknameText(h.nickname),"relay: nickname");
    need(!h.havePosition||positionValid(h.position),"relay: position");
    auto b=start(Type::Hello,HelloSize);put(b,h.id,8);putPadded(b,h.token,TokenBytes);
    put(b,h.sequence,4);put(b,h.stream,4);put(b,h.ack,4);put(b,h.area,4);b.push_back(h.havePosition?1:0);put(b,0,3);
    for(double x:h.position)put(b,uint32_t(int32_t(h.havePosition?std::llround(x*100):0)),4);
    putPadded(b,h.nickname,NicknameBytes);return b;}
inline Buffer encode(const Welcome& w){
    using namespace detail;need(w.id!=0,"relay: player id 0");
    auto b=start(Type::Welcome,WelcomeSize);put(b,w.id,8);put(b,w.sequence,4);put(b,w.stream,4);
    put(b,w.enterMetres,2);put(b,w.leaveMetres,2);put(b,w.timeoutSeconds,2);put(b,w.nearby,2);return b;}
inline Buffer encode(const Refused& r){
    using namespace detail;need(r.id!=0,"relay: player id 0");need(r.reason==Refusal::Full||r.reason==Refusal::IdInUse,"relay: reason");
    auto b=start(Type::Refused,RefusedSize);put(b,r.id,8);put(b,r.sequence,4);b.push_back(uint8_t(r.reason));put(b,0,3);return b;}
inline Buffer encode(const Roster& r){
    using namespace detail;need(!r.events.empty()&&r.events.size()<=RosterEventsMax,"relay: roster count");
    auto b=start(Type::Roster,RosterHeader+r.events.size()*(10+NicknameBytes));put(b,r.stream,4);put(b,r.first,4);b.push_back(uint8_t(r.events.size()));put(b,0,3);
    for(auto& e:r.events){
        need(e.id!=0,"relay: player id 0");need(nicknameText(e.nickname),"relay: nickname");
        b.push_back(e.enter?1:2);put(b,e.id,8);b.push_back(uint8_t(e.nickname.size()));b.insert(b.end(),e.nickname.begin(),e.nickname.end());}
    return b;}
// DATA (to the server) or RELAYED (from it): `peer` is the destination or the source.
inline void encodeData(Buffer& out,Type type,uint64_t peer,const uint8_t* payload,size_t size){
    using namespace detail;need(type==Type::Data||type==Type::Relayed,"relay: type");need(peer!=0,"relay: player id 0");
    need(payload&&size>=1&&size<=MaxPayload,"relay: payload size");
    out.clear();put(out,Magic,4);put(out,Version,2);out.push_back(uint8_t(type));out.push_back(0);put(out,peer,8);out.insert(out.end(),payload,payload+size);}
inline Buffer encode(const Bye& y){
    using namespace detail;need(y.id!=0,"relay: player id 0");need(tokenText(y.token),"relay: token");
    auto b=start(Type::Bye,ByeSize);put(b,y.id,8);putPadded(b,y.token,TokenBytes);return b;}

inline Error parse(const uint8_t* b,size_t n,Message& m){
    using namespace detail;
    if(!b||n<HeaderSize||n>MaxDatagram)return Error::Length;
    if(get(b,4)!=Magic)return Error::Magic;
#ifndef MUTANT_RELAY_WIRE_VERSION
    if(get(b+4,2)!=Version)return Error::Version;
#endif
    if(b[6]<1||b[6]>7)return Error::Type;
    if(b[7])return Error::Reserved;
    m.type=Type(b[6]);
    auto exact=[&](size_t size){
#ifndef MUTANT_RELAY_WIRE_LENGTH
        return n==size;
#else
        return n>=size;
#endif
    };
    switch(m.type){
    case Type::Hello:{
        if(!exact(HelloSize))return Error::Length;
        Hello h;h.id=get(b+8,8);if(!h.id)return Error::Range;
        if(!getPadded(b+16,TokenBytes,h.token)||!tokenText(h.token))return Error::Text;
        h.sequence=uint32_t(get(b+48,4));h.stream=uint32_t(get(b+52,4));h.ack=uint32_t(get(b+56,4));h.area=uint32_t(get(b+60,4));
        if(b[64]>1||b[65]||b[66]||b[67])return Error::Reserved;
        h.havePosition=b[64]==1;
        for(unsigned i=0;i<3;++i){
            auto cm=int32_t(uint32_t(get(b+68+4*i,4)));
#ifndef MUTANT_RELAY_WIRE_RANGE
            if(cm<-10000000||cm>10000000)return Error::Range;
#endif
            if(!h.havePosition&&cm)return Error::Reserved;
            h.position[i]=cm/100.;}
#ifndef MUTANT_RELAY_WIRE_TEXT
        if(!getPadded(b+80,NicknameBytes,h.nickname)||!nicknameText(h.nickname))return Error::Text;
#else
        h.nickname.assign(reinterpret_cast<const char*>(b+80),NicknameBytes);
#endif
        m.hello=std::move(h);return Error::None;}
    case Type::Welcome:{
        if(!exact(WelcomeSize))return Error::Length;
        Welcome w;w.id=get(b+8,8);if(!w.id)return Error::Range;
        w.sequence=uint32_t(get(b+16,4));w.stream=uint32_t(get(b+20,4));
        w.enterMetres=uint16_t(get(b+24,2));w.leaveMetres=uint16_t(get(b+26,2));w.timeoutSeconds=uint16_t(get(b+28,2));w.nearby=uint16_t(get(b+30,2));
        m.welcome=w;return Error::None;}
    case Type::Refused:{
        if(!exact(RefusedSize))return Error::Length;
        Refused r;r.id=get(b+8,8);if(!r.id)return Error::Range;
        r.sequence=uint32_t(get(b+16,4));
        if(get(b+20,1)<1||get(b+20,1)>2)return Error::Range;
        if(get(b+21,3))return Error::Reserved;
        r.reason=Refusal(get(b+20,1));m.refused=r;return Error::None;}
    case Type::Roster:{
        if(n<RosterHeader)return Error::Length;
        Roster r;r.stream=uint32_t(get(b+8,4));r.first=uint32_t(get(b+12,4));
        size_t count=b[16];if(count<1||count>RosterEventsMax)return Error::Range;
        if(b[17]||b[18]||b[19])return Error::Reserved;
        size_t o=RosterHeader;
        for(size_t i=0;i<count;++i){
            if(o+10>n)return Error::Length;
            RosterEvent e;if(b[o]!=1&&b[o]!=2)return Error::Range;
            e.enter=b[o]==1;e.id=get(b+o+1,8);if(!e.id)return Error::Range;
            size_t length=b[o+9];if(length>NicknameBytes)return Error::Range;
            if(o+10+length>n)return Error::Length;
            e.nickname.assign(reinterpret_cast<const char*>(b+o+10),length);
            if(!nicknameText(e.nickname))return Error::Text;
            o+=10+length;r.events.push_back(std::move(e));}
        if(o!=n)return Error::Length;
        m.roster=std::move(r);return Error::None;}
    case Type::Data:case Type::Relayed:{
#ifndef MUTANT_RELAY_WIRE_PAYLOAD
        if(n<=DataHeader)return Error::Length;
#else
        if(n<DataHeader)return Error::Length;
#endif
        m.data.peer=get(b+8,8);if(!m.data.peer)return Error::Range;
        m.data.payload=b+DataHeader;m.data.size=n-DataHeader;return Error::None;}
    case Type::Bye:{
        if(!exact(ByeSize))return Error::Length;
        Bye y;y.id=get(b+8,8);if(!y.id)return Error::Range;
        if(!getPadded(b+16,TokenBytes,y.token)||!tokenText(y.token))return Error::Text;
        m.bye=std::move(y);return Error::None;}
    }
    return Error::Type;
}

// The player's side of the relay, without sockets: it says HELLO, keeps the roster and wraps the core's datagrams.
// One thread owns it. The adapter hands it only datagrams that came from the relay's address, and gives every player
// in the roster a core session of their own: ENTER starts one, LEAVE ends it.
struct ClientConfig {
    uint64_t id=0; std::string token,nickname;
    uint32_t helloMs=500;      // how often HELLO is sent
    uint32_t silentMs=5000;    // no WELCOME for this long: the relay is gone and so is everyone near
};
class Client {
public:
    using Send=std::function<void(const uint8_t*,size_t)>;   // one datagram to the relay
    Client(ClientConfig c,Send send):config_(std::move(c)),send_(std::move(send)){
        detail::need(config_.id!=0,"relay: player id 0");detail::need(tokenText(config_.token),"relay: token");
        detail::need(nicknameText(config_.nickname),"relay: nickname");detail::need(config_.helloMs>0,"relay: hello interval");}
    // Where the player is. false (and no position is reported) for a place the wire cannot carry.
    bool setPosition(const std::array<double,3>& p,uint32_t area=0){
        area_=area;if(!positionValid(p)){have_=false;return false;}
        position_=p;have_=true;return true;}
    // No place to report (loading, a cutscene): the player stays known to the server and is near nobody.
    void clearPosition(){have_=false;}
    void tick(uint64_t now){
#ifndef MUTANT_RELAY_CLIENT_SILENT
        if(online_&&now-lastWelcome_>config_.silentMs)offline();
#endif
        if(!sentHello_||now>=nextHello_){
            Hello h;h.id=config_.id;h.token=config_.token;h.nickname=config_.nickname;h.sequence=++helloSequence_;
            h.stream=stream_;h.ack=applied_;h.area=area_;h.havePosition=have_;if(have_)h.position=position_;
            auto b=encode(h);send_(b.data(),b.size());sentHello_=true;nextHello_=now+config_.helloMs;}
    }
    // One datagram from the relay. true when it carried a core datagram from a player in the roster: `from` is that
    // player and payload/size point into `data`.
    bool receive(const uint8_t* data,size_t size,uint64_t now,uint64_t& from,const uint8_t*& payload,size_t& payloadSize){
        Message m;if(parse(data,size,m)!=Error::None)return false;
        switch(m.type){
        case Type::Welcome:{
            auto& w=m.welcome;if(w.id!=config_.id||!answer(w.sequence))return false;
            online_=true;lastWelcome_=now;refusal_=Refusal::None;welcome_=w;
#ifndef MUTANT_RELAY_CLIENT_STREAM
            if(w.stream!=stream_){leaveAll();stream_=w.stream;applied_=0;}
#else
            stream_=w.stream;
#endif
            return false;}
        case Type::Refused:
            if(m.refused.id==config_.id&&answer(m.refused.sequence))refusal_=m.refused.reason;
            return false;
        case Type::Roster:{
            auto& r=m.roster;if(!online_||r.stream!=stream_)return false;
            for(size_t i=0;i<r.events.size();++i){
#ifndef MUTANT_RELAY_CLIENT_ORDER
                if(r.first+uint32_t(i)!=applied_+1)continue;
#endif
                ++applied_;auto& e=r.events[i];
                if(e.id==config_.id)continue;
                if(e.enter){if(near_.emplace(e.id,e.nickname).second)events_.push_back(e);}
                else if(near_.erase(e.id))events_.push_back(e);}
            nextHello_=now;   // acknowledge now, so the server stops repeating
            return false;}
        case Type::Relayed:
#ifndef MUTANT_RELAY_CLIENT_STRANGER
            if(!near_.count(m.data.peer))return false;
#endif
            from=m.data.peer;payload=m.data.payload;payloadSize=m.data.size;return true;
        default:return false;}
    }
    // One core datagram for a player in the roster. false (nothing sent) for anyone else or a size the wire refuses.
    bool sendTo(uint64_t peer,const uint8_t* payload,size_t size){
        if(!online_||!near_.count(peer)||!payload||size<1||size>MaxPayload)return false;
        encodeData(out_,Type::Data,peer,payload,size);send_(out_.data(),out_.size());return true;}
    // Leaving: tells the server once (not repeated) and forgets everyone.
    void bye(){Bye y;y.id=config_.id;y.token=config_.token;auto b=encode(y);send_(b.data(),b.size());offline();sentHello_=false;}
    // ENTER and LEAVE since the last call, in order; each player enters before he leaves.
    std::vector<RosterEvent> takeEvents(){auto e=std::move(events_);events_.clear();return e;}
    const std::map<uint64_t,std::string>& nearby() const {return near_;}   // id -> nickname
    bool online() const {return online_;}             // the relay answered within silentMs
    Refusal refusal() const {return refusal_;}        // why the last HELLO was refused, until one is accepted
    const Welcome& welcome() const {return welcome_;} // the server's distances and timeout, once online
    uint64_t id() const {return config_.id;}
private:
    // A WELCOME or REFUSED is taken once, in the order of the HELLOs they answer; one that answers no HELLO is not.
    bool answer(uint32_t sequence){
        if(int32_t(sequence-lastAnswer_)<=0||int32_t(helloSequence_-sequence)<0)return false;
        lastAnswer_=sequence;return true;}
    void leaveAll(){for(auto& p:near_)events_.push_back({false,p.first,p.second});near_.clear();}
    void offline(){leaveAll();online_=false;stream_=0;applied_=0;}
    ClientConfig config_;Send send_;Buffer out_;
    std::array<double,3> position_{};uint32_t area_=0;bool have_=false;
    bool online_=false,sentHello_=false;uint64_t nextHello_=0,lastWelcome_=0;
    uint32_t helloSequence_=0,lastAnswer_=0,stream_=0,applied_=0;
    Refusal refusal_=Refusal::None;Welcome welcome_{};
    std::map<uint64_t,std::string> near_;std::vector<RosterEvent> events_;
};
}
