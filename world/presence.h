#pragma once
// Presence across linked servers (README.md, "Presence across linked servers"): envelope version 3 on the relay's UDP
// port, spoken between the relays of two linked coop-world servers and never by a game. Header-only and game-free; the
// relay's side is relay.cpp ("Linked servers' presence").
//
// Every datagram has the same 32-byte header and ends in an 8-byte tag; integers are little endian, reserved bytes are
// zero, a datagram is at most 1200 bytes:
//
//   0  4  magic "DSR1"   4  2  version (3)   6  1  type   7  1  reserved
//   8  u64 sender's session   16 u64 receiver's session as the sender knows it (0: not yet)   24 u32 sequence   28 reserved[4]
//   32 body                   n-8 u64 tag: SipHash-2-4 of bytes 0..n-9 under a key made from the link key
//
//   HELLO     1  every half second: 0 server id[32] (zero padded) | 32 u64 t0, the sender's clock (ms) | 40 u64 the last t0
//                received from the other side (0: none) | 48 u32 ms since it arrived | 52 u16 enter metres | 54 u16 leave
//                metres | 56 u16 full metres | 58 u16 animation metres | 60 u16 interval ms | 62 reserved[2]
//   PRESENT   2  the sender's players it sends now, a complete list in parts: 0 u32 list | 4 u8 part | 5 u8 parts | 6 u16
//                count | count x ( u64 id | u32 area | u16 state generation | u8 reserved | u8 n (0..32) | i32 x3 position,
//                centimetres | u32 game build | mod build[16] | nickname[n] )
//   INTEREST  3  where the sender's players are, 8 m cells, a complete list in parts: 0 u32 list | 4 u8 part | 5 u8 parts
//                | 6 u16 count | count x ( u32 area | i16 x3 cell )
//   FRAME     4  a version 2 FRAME (core/broadcast.h): the updates of the sender's players, on the sender's clock
//   SHARE     5  a version 2 SHARE: one kind of a sender's player's shown state, with his versions of every kind
//   FETCH     6  a version 2 FETCH: send that player's state again (every kind)
//
// The key never travels. A receiver takes a datagram only with the right tag; anything but a HELLO only from the
// session it holds for that link, naming its own session, with a sequence it has not seen (a 64-datagram window).
#include "../core/broadcast.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace world::presence {
using ds::relay::Buffer;
constexpr uint16_t Version=3;
constexpr size_t HeaderSize=32,TagBytes=8,MaxDatagram=1200,BodyMax=MaxDatagram-HeaderSize-TagBytes;   // 1160
constexpr size_t HelloBody=64,ListHeader=8,CellBytes=10,EntryFixed=48,ServerIdBytes=32;
constexpr double CellMetres=8;
enum class Type : uint8_t { Hello=1, Present, Interest, Frame, Share, Fetch };
namespace detail {
using ds::relay::detail::put;using ds::relay::detail::get;
inline uint64_t rotl(uint64_t x,int b){return (x<<b)|(x>>(64-b));}
}
// SipHash-2-4 (Aumasson and Bernstein), a keyed 64-bit tag.
inline uint64_t siphash(uint64_t k0,uint64_t k1,const uint8_t* m,size_t n){
    using detail::rotl;
    uint64_t v0=0x736f6d6570736575ull^k0,v1=0x646f72616e646f6dull^k1,v2=0x6c7967656e657261ull^k0,v3=0x7465646279746573ull^k1;
    auto round=[&]{v0+=v1;v1=rotl(v1,13);v1^=v0;v0=rotl(v0,32);v2+=v3;v3=rotl(v3,16);v3^=v2;v0+=v3;v3=rotl(v3,21);v3^=v0;v2+=v1;v1=rotl(v1,17);v1^=v2;v2=rotl(v2,32);};
    size_t whole=n-n%8;
    for(size_t i=0;i<whole;i+=8){uint64_t w=detail::get(m+i,8);v3^=w;round();round();v0^=w;}
    uint64_t last=uint64_t(n)<<56;for(size_t i=0;i<n%8;++i)last|=uint64_t(m[whole+i])<<(8*i);
    v3^=last;round();round();v0^=last;v2^=0xff;round();round();round();round();
    return v0^v1^v2^v3;}
struct Key {uint64_t k0=0,k1=0;};
// The tag's key, from the link key (both servers know it; it is never sent).
inline Key keyOf(const std::string& linkKey){
    auto p=reinterpret_cast<const uint8_t*>(linkKey.data());
    return {siphash(0x636f6f702d776f72ull,0x6c64206c696e6b30ull,p,linkKey.size()),siphash(0x636f6f702d776f72ull,0x6c64206c696e6b31ull,p,linkKey.size())};}
struct Header {Type type=Type::Hello;uint64_t sender=0,receiver=0;uint32_t sequence=0;};
inline Buffer seal(const Header& h,const uint8_t* body,size_t size,const Key& k){
    using namespace detail;ds::relay::detail::need(size<=BodyMax&&(size==0||body),"presence: body size");
    Buffer b;b.reserve(HeaderSize+size+TagBytes);put(b,ds::relay::Magic,4);put(b,Version,2);b.push_back(uint8_t(h.type));b.push_back(0);
    put(b,h.sender,8);put(b,h.receiver,8);put(b,h.sequence,4);put(b,0,4);if(size)b.insert(b.end(),body,body+size);
    put(b,siphash(k.k0,k.k1,b.data(),b.size()),8);return b;}
// false: not a datagram of this envelope, or not made with this key. body points into the datagram.
inline bool open(const uint8_t* b,size_t n,const Key& k,Header& h,const uint8_t*& body,size_t& size){
    using namespace detail;
    if(!b||n<HeaderSize+TagBytes||n>MaxDatagram||get(b,4)!=ds::relay::Magic||get(b+4,2)!=Version||b[6]<1||b[6]>6||b[7]||get(b+28,4))return false;
#ifndef MUTANT_LINK_PRESENCE_MAC
    uint64_t want=siphash(k.k0,k.k1,b,n-TagBytes),got=get(b+n-TagBytes,8),diff=want^got;
    if(diff)return false;
#else
    (void)k;
#endif
    h.type=Type(b[6]);h.sender=get(b+8,8);h.receiver=get(b+16,8);h.sequence=uint32_t(get(b+24,4));
    if(!h.sender||!h.sequence)return false;
    body=b+HeaderSize;size=n-HeaderSize-TagBytes;return true;}
// The sequences of one session already taken: the highest and the 63 before it.
struct Window {
    uint32_t top=0;uint64_t seen=0;
    bool take(uint32_t s){
#ifndef MUTANT_LINK_PRESENCE_REPLAY
        if(s>top){uint32_t shift=s-top;seen=shift>=64?0:seen<<shift;seen|=1;top=s;return true;}
        uint32_t back=top-s;if(back>=64||(seen>>back&1))return false;
        seen|=uint64_t(1)<<back;return true;
#else
        if(s>top)top=s;return true;
#endif
    }
};

struct Hello {std::string server;uint64_t t0=0,echo=0;uint32_t held=0;uint16_t enter=0,leave=0,full=0,animation=0,interval=0;};
inline Buffer encode(const Hello& h){
    using namespace detail;ds::relay::detail::need(h.server.size()<=ServerIdBytes&&h.t0<=(1ull<<53)&&h.echo<=(1ull<<53),"presence: hello");
    Buffer b;b.insert(b.end(),h.server.begin(),h.server.end());b.insert(b.end(),ServerIdBytes-h.server.size(),0);
    put(b,h.t0,8);put(b,h.echo,8);put(b,h.held,4);put(b,h.enter,2);put(b,h.leave,2);put(b,h.full,2);put(b,h.animation,2);put(b,h.interval,2);put(b,0,2);return b;}
inline bool parse(const uint8_t* b,size_t n,Hello& h){
    using namespace detail;
    if(n!=HelloBody||get(b+62,2))return false;
    if(!ds::relay::detail::getPadded(b,ServerIdBytes,h.server)||h.server.size()<4)return false;
    for(char c:h.server)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
    h.t0=get(b+32,8);h.echo=get(b+40,8);h.held=uint32_t(get(b+48,4));
    h.enter=uint16_t(get(b+52,2));h.leave=uint16_t(get(b+54,2));h.full=uint16_t(get(b+56,2));h.animation=uint16_t(get(b+58,2));h.interval=uint16_t(get(b+60,2));
    return h.t0<=(1ull<<53)&&h.echo<=(1ull<<53)&&h.enter>=1&&h.leave>=h.enter;}

struct Cell {
    uint32_t area=0;int16_t x=0,y=0,z=0;
    bool operator<(const Cell& o) const {return area!=o.area?area<o.area:x!=o.x?x<o.x:y!=o.y?y<o.y:z<o.z;}
    bool operator==(const Cell& o) const {return area==o.area&&x==o.x&&y==o.y&&z==o.z;}
};
// The cell of a place (metres, the core's axes; within 100 km, so every index fits 16 bits).
inline Cell cellOf(uint32_t area,const std::array<double,3>& p){
    auto i=[](double x){return int16_t(std::floor(x/CellMetres));};return {area,i(p[0]),i(p[1]),i(p[2])};}
// The straight-line distance from a place to the nearest point of a cell.
inline double distanceTo(const Cell& c,const std::array<double,3>& p){
    double d=0;int16_t at[3]={c.x,c.y,c.z};
    for(unsigned k=0;k<3;++k){double lo=at[k]*CellMetres,hi=lo+CellMetres,e=p[k]<lo?lo-p[k]:p[k]>hi?p[k]-hi:0;d+=e*e;}
    return std::sqrt(d);}
struct Present {uint64_t id=0;uint32_t area=0;uint16_t generation=0;std::array<double,3> position{};uint32_t game=0;ds::broadcast::BuildId build{};std::string nickname;};
struct List {uint32_t list=0;uint8_t part=0,parts=0;};
inline size_t sizeOf(const Present& p){return EntryFixed+p.nickname.size();}
// A complete list as bodies of at most BodyMax bytes (at least one, empty when there is nothing).
template<class Item,class Put,class Size> std::vector<Buffer> parts(uint32_t list,const std::vector<Item>& items,Size sizeOf,Put putItem){
    using namespace detail;std::vector<std::vector<const Item*>> groups(1);size_t used=ListHeader;
    for(auto& i:items){size_t s=sizeOf(i);if(used+s>BodyMax){groups.emplace_back();used=ListHeader;}groups.back().push_back(&i);used+=s;}
    ds::relay::detail::need(groups.size()<=255,"presence: list too long");
    std::vector<Buffer> out;
    for(size_t g=0;g<groups.size();++g){
        Buffer b;put(b,list,4);b.push_back(uint8_t(g));b.push_back(uint8_t(groups.size()));put(b,groups[g].size(),2);
        for(auto* i:groups[g])putItem(b,*i);
        out.push_back(std::move(b));}
    return out;}
inline std::vector<Buffer> encodeCells(uint32_t list,const std::vector<Cell>& cells){
    return parts(list,cells,[](const Cell&){return CellBytes;},[](Buffer& b,const Cell& c){
        detail::put(b,c.area,4);detail::put(b,uint16_t(c.x),2);detail::put(b,uint16_t(c.y),2);detail::put(b,uint16_t(c.z),2);});}
inline std::vector<Buffer> encodePresent(uint32_t list,const std::vector<Present>& players){
    for(auto& p:players)ds::relay::detail::need(p.id!=0&&ds::relay::nicknameText(p.nickname)&&ds::relay::positionValid(p.position),"presence: player");
    return parts(list,players,[](const Present& p){return sizeOf(p);},[](Buffer& b,const Present& p){
        using namespace detail;put(b,p.id,8);put(b,p.area,4);put(b,p.generation,2);b.push_back(0);b.push_back(uint8_t(p.nickname.size()));
        for(double x:p.position)put(b,uint32_t(int32_t(std::llround(x*100))),4);
        put(b,p.game,4);b.insert(b.end(),p.build.begin(),p.build.end());b.insert(b.end(),p.nickname.begin(),p.nickname.end());});}
inline bool parseList(const uint8_t* b,size_t n,List& l,size_t& count){
    if(n<ListHeader)return false;
    l.list=uint32_t(detail::get(b,4));l.part=b[4];l.parts=b[5];count=size_t(detail::get(b+6,2));
    return l.parts>=1&&l.part<l.parts;}
inline bool parse(const uint8_t* b,size_t n,List& l,std::vector<Cell>& out){
    size_t count=0;if(!parseList(b,n,l,count)||n!=ListHeader+count*CellBytes)return false;
    out.clear();
    for(size_t i=0;i<count;++i){const uint8_t* p=b+ListHeader+i*CellBytes;
        out.push_back({uint32_t(detail::get(p,4)),int16_t(uint16_t(detail::get(p+4,2))),int16_t(uint16_t(detail::get(p+6,2))),int16_t(uint16_t(detail::get(p+8,2)))});}
    return true;}
inline bool parse(const uint8_t* b,size_t n,List& l,std::vector<Present>& out){
    size_t count=0;if(!parseList(b,n,l,count))return false;
    out.clear();size_t o=ListHeader;
    for(size_t i=0;i<count;++i){
        if(o+EntryFixed>n)return false;
        Present p;const uint8_t* e=b+o;p.id=detail::get(e,8);p.area=uint32_t(detail::get(e+8,4));p.generation=uint16_t(detail::get(e+12,2));
        size_t length=e[15];if(!p.id||e[14]||length>ds::relay::NicknameBytes||o+EntryFixed+length>n)return false;
        for(unsigned k=0;k<3;++k){auto cm=int32_t(uint32_t(detail::get(e+16+4*k,4)));if(cm<-10000000||cm>10000000)return false;p.position[k]=cm/100.;}
        p.game=uint32_t(detail::get(e+28,4));std::copy(e+32,e+48,p.build.begin());p.nickname.assign(reinterpret_cast<const char*>(e+48),length);
        if(!ds::relay::nicknameText(p.nickname))return false;
        o+=EntryFixed+length;out.push_back(std::move(p));}
    return o==n;}
// The parts of the latest list of one kind, kept until all of them have come.
template<class Item> struct Assembly {
    bool have=false;uint32_t list=0;std::vector<bool> got;std::vector<Item> items;size_t missing=0;
    // true: this part completed the list, and `items` is all of it.
    bool take(const List& l,std::vector<Item>& part){
        if(have&&l.list!=list&&!ds::broadcast::later(l.list,list))return false;   // an older list
        if(!have||l.list!=list||got.size()!=l.parts){have=true;list=l.list;got.assign(l.parts,false);items.clear();missing=l.parts;}
        if(got[l.part]||!missing)return false;
        got[l.part]=true;items.insert(items.end(),part.begin(),part.end());return --missing==0;}
};

// What a player shows as his name is the mod's state of kind 1 (mod/crowd.h): one event of 27 bytes per 9-byte chunk of
// UTF-8 (mod/nametag.h), at most 36 bytes. Read and written here so that a server can name a linked server's player.
constexpr uint8_t NameEventType=10;constexpr size_t NameMaxBytes=36,NameEventBytes=27,ShownCharacters=24,LabelMaxBytes=12;
inline std::optional<std::string> nameOf(const uint8_t* b,size_t n){
    using detail::get;
    if(!b||n<1)return std::nullopt;
    size_t events=b[0];if(events<1||events>(NameMaxBytes+8)/9||n!=1+NameEventBytes*events)return std::nullopt;
    std::string out;
    for(size_t i=0;i<events;++i){
        const uint8_t* e=b+1+NameEventBytes*i;uint64_t ref=get(e+5,8);size_t index=ref&15,total=ref>>4&15;
        if(e[0]!=NameEventType||ref>>48)return std::nullopt;
        if(total==0){if(events!=1)return std::nullopt;return std::string();}
        if(total!=events||index!=i)return std::nullopt;
        uint8_t chunk[9];for(int k=0;k<4;++k)chunk[k]=e[1+k];for(int k=0;k<5;++k)chunk[4+k]=uint8_t(ref>>(8+8*k));
        for(uint8_t c:chunk)if(c)out.push_back(char(c));}
    return out;}
inline Buffer nameBlob(const std::string& utf8){
    using detail::put;
    std::string s=utf8.substr(0,NameMaxBytes);
    if(utf8.size()>NameMaxBytes)while(!s.empty()&&(static_cast<unsigned char>(utf8[s.size()])>>6)==2)s.pop_back();   // whole characters
    size_t total=(s.size()+8)/9;Buffer b;b.push_back(uint8_t(total?total:1));
    for(size_t k=0;k<(total?total:1);++k){
        uint8_t c[9]{};for(size_t i=0;i<9&&9*k+i<s.size();++i)c[i]=uint8_t(s[9*k+i]);
        uint64_t rest=0;for(int i=8;i>=4;--i)rest=rest<<8|c[i];
        b.push_back(NameEventType);put(b,total?uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|uint32_t(c[3])<<24:0,4);
        put(b,total?uint64_t(k)|uint64_t(total)<<4|rest<<8:0,8);put(b,0,14);}
    return b;}
// A linked server's player's name as this server shows it: his name, " · ", and the name this server gave the link,
// within 32 bytes (a nickname) and 24 characters (the label over his body). The name is cut, never the link's name
// (itself cut to 12 bytes); control bytes are dropped.
inline std::string shownName(const std::string& name,const std::string& link){
    auto characters=[](const std::string& s){size_t n=0;for(unsigned char c:s)n+=(c>>6)!=2;return n;};
    std::string label=link.substr(0,LabelMaxBytes),clean;
    for(unsigned char c:name)if(c>=0x20&&c!=0x7f)clean.push_back(char(c));
    const std::string dot=" \xc2\xb7 ";
    size_t maxBytes=ds::relay::NicknameBytes-dot.size()-label.size(),maxCharacters=ShownCharacters-3-characters(label);
    std::string out;size_t count=0;
    for(size_t i=0;i<clean.size();){
        size_t n=1;while(i+n<clean.size()&&(static_cast<unsigned char>(clean[i+n])>>6)==2)++n;
        if(out.size()+n>maxBytes||count+1>maxCharacters)break;
        out.append(clean,i,n);++count;i+=n;}
    return out+dot+label;}
}
