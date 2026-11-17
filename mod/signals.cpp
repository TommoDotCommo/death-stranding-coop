#include "signals.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>
namespace mod::signals {
ds::CargoEvent encodeEvent(Kind kind,uint32_t serial){
    ds::CargoEvent e;e.type=EventWireType;e.item=uint32_t(kind);e.ref=serial;return e;
}
std::optional<Kind> decodeEvent(const ds::CargoEvent& e){
#ifndef MUTANT_SIGNALS_KIND
    if(e.type!=EventWireType||e.ref>>32||(e.item!=uint32_t(Kind::Shout)&&e.item!=uint32_t(Kind::Scan)))return std::nullopt;
#else
    if(e.type!=EventWireType)return std::nullopt;
#endif
    return Kind(e.item);
}
std::optional<ds::CargoEvent> EventSender::shout(uint64_t now,bool connected,bool callingNow){
#ifndef MUTANT_SIGNALS_SHOUT_HELD
    bool started=callingNow&&!calling_;
#else
    bool started=callingNow;
#endif
    calling_=callingNow;
    if(!started||!connected)return std::nullopt;
    if(sentShout_&&now>=lastShout_&&now-lastShout_<MinGapMs)return std::nullopt;
    sentShout_=true;lastShout_=now;return encodeEvent(Kind::Shout,++serial_);
}
std::optional<ds::CargoEvent> EventSender::scan(uint64_t now,bool connected,const std::optional<uint32_t>& count){
    bool fresh=connected&&!wasConnected_;wasConnected_=connected;
    if(!count){haveCount_=false;return std::nullopt;}
    bool rose=haveCount_&&*count>count_;
#ifndef MUTANT_SIGNALS_SCAN_LEARN
    if(fresh)rose=false;
#else
    (void)fresh;if(!haveCount_&&*count)rose=true;
#endif
    haveCount_=true;count_=*count;
    if(!rose||!connected)return std::nullopt;
#ifndef MUTANT_SIGNALS_SCAN_GAP
    if(sentScan_&&now>=lastScan_&&now-lastScan_<MinGapMs)return std::nullopt;
#endif
    sentScan_=true;lastScan_=now;return encodeEvent(Kind::Scan,++serial_);
}
bool EventReceiver::accept(Kind kind,uint64_t now){
    auto& q=shown_[kind==Kind::Shout?0:1];
    while(!q.empty()&&(now<q.front()||now-q.front()>=60000))q.pop_front();
#ifndef MUTANT_SIGNALS_RECEIVE_GAP
    if(!q.empty()&&now>=q.back()&&now-q.back()<MinGapMs)return false;
#endif
    if(q.size()>=PerMinute)return false;
    q.push_back(now);return true;
}
namespace {
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){v=0;return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
}
std::optional<uint32_t> readScanCount(const discovery::Sampler& s,uint64_t entity){
    uint64_t state=0,owner=0;uint32_t phase=0,count=0;
    if(!entity||!q(s,entity+PlayerState,state)||!state||!q(s,state+StateOwner,owner))return std::nullopt;
#ifndef MUTANT_SIGNALS_SCAN_OWNER
    if(owner!=entity)return std::nullopt;
#endif
    if(!s.read(state+ScanState,&phase,4)||phase>8||!s.read(state+ScanCount,&count,4))return std::nullopt;
    return count;
}
ds::CargoEvent encodeVitals(const Vitals& v,uint32_t serial){
    ds::CargoEvent e;e.type=VitalsWireType;e.ref=serial;
    e.item=uint32_t(v.blood)|uint32_t(v.stamina)<<8|uint32_t(v.overloaded?1:0)<<16|uint32_t(v.unsteady?2:0)<<16;return e;
}
std::optional<Vitals> decodeVitals(const ds::CargoEvent& e){
    if(e.type!=VitalsWireType||e.ref>>32)return std::nullopt;
    Vitals v;v.blood=uint8_t(e.item);v.stamina=uint8_t(e.item>>8);uint32_t bits=e.item>>16;
#ifndef MUTANT_SIGNALS_VITALS_RANGE
    if(bits>3||(v.blood>100&&v.blood!=Unknown)||(v.stamina>100&&v.stamina!=Unknown))return std::nullopt;
#endif
    v.overloaded=bits&1;v.unsteady=bits&2;return v;
}
InfoPairs readInfo(const discovery::Sampler& s,uint64_t base,uint64_t entity){
    InfoPairs out;uint64_t manager=0,list=0,player=0;int32_t count=0;
    if(!entity||!q(s,base+PlayerManagerSlot,manager)||!manager||!s.read(manager+0x38,&count,4)||count<=0||count>16||!q(s,manager+0x40,list)||!list)return out;
    for(int32_t i=0;i<count&&!player;++i){uint64_t p=0;uint32_t number=1;if(q(s,list+8ull*i,p)&&p&&s.read(p+0x88,&number,4)&&number==0)player=p;}
    uint64_t owner=0;
    if(!player||!q(s,player+0x70,owner))return out;
#ifndef MUTANT_SIGNALS_INFO_OWNER
    if(owner!=entity)return out;
#endif
    auto rate=[&](uint64_t value,uint64_t maximum)->std::optional<float>{
        float v=0,m=0;if(!s.read(player+PlayerInfo+value,&v,4)||!s.read(player+PlayerInfo+maximum,&m,4))return std::nullopt;
#ifndef MUTANT_SIGNALS_INFO_RANGE
        if(!std::isfinite(v)||!std::isfinite(m)||!(m>0)||v<0||v>m*1.001f)return std::nullopt;
#else
        if(!(m>0))return std::nullopt;
#endif
        return std::min(1.f,v/m);};
    out.first=rate(0x28,0x2c);out.second=rate(0x30,0x38);return out;
}
bool VitalsSender::Held::step(uint64_t now,bool v){
    if(v!=seen){seen=v;since=now;}
#ifndef MUTANT_SIGNALS_STATE_HOLD
    if(v!=value&&now-since>=HoldMs)value=v;
#else
    value=v;
#endif
    return value;
}
uint8_t VitalsSender::percent(uint8_t have,const std::optional<float>& rate){
    if(!rate||!std::isfinite(*rate))return Unknown;
    int raw=int(std::lround(std::clamp(*rate,0.f,1.f)*100));
#ifndef MUTANT_SIGNALS_STEP
    if(raw==0||raw==100)return uint8_t(raw);   // empty and full are always told exactly
    if(have!=Unknown&&std::abs(raw-int(have))<Step)return have;
#else
    (void)have;return uint8_t(raw);
#endif
    return uint8_t((raw+Step/2)/Step*Step);
}
std::optional<ds::CargoEvent> VitalsSender::tick(uint64_t now,bool connected,const Raw& raw){
    want_.blood=percent(want_.blood,raw.blood);want_.stamina=percent(want_.stamina,raw.stamina);
    want_.overloaded=over_.step(now,raw.overloaded);want_.unsteady=unsteady_.step(now,raw.unsteady);
    bool fresh=connected&&!wasConnected_;wasConnected_=connected;
    if(!connected)return std::nullopt;
    if(fresh)haveSent_=false;
#ifndef MUTANT_SIGNALS_VITALS_CHANGED
    if(haveSent_&&want_==sent_)return std::nullopt;
#endif
#ifndef MUTANT_SIGNALS_VITALS_GAP
    if(haveSent_&&now>=last_&&now-last_<MinGapMs)return std::nullopt;
#endif
    haveSent_=true;sent_=want_;last_=now;return encodeVitals(want_,++serial_);
}
bool VitalsReceiver::feed(const ds::CargoEvent& e){
    auto v=decodeVitals(e);if(!v)return false;
    now_=*v;have_=true;return true;
}
std::u16string label(const std::u16string& name,const Vitals* v){
    std::u16string out=name;if(!v)return out;
    auto number=[&](char16_t letter,uint8_t p){if(p==Unknown)return;out+=out.size()==name.size()?u"  ":u" ";out+=letter;
        auto digits=std::to_string(unsigned(p));out.append(digits.begin(),digits.end());};
    number(u'B',v->blood);number(u'S',v->stamina);
    if(v->overloaded)out+=u" heavy";
    if(v->unsteady)out+=u" unsteady";
    return out;
}
const std::u16string& LabelPacer::text(uint64_t now,const std::u16string& name,const Vitals* v){
    auto want=label(name,v);
    if(have_&&want==text_)return text_;
#ifndef MUTANT_SIGNALS_LABEL_GAP
    if(have_&&name==name_&&now>=at_&&now-at_<GapMs)return text_;
#endif
    have_=true;name_=name;text_=std::move(want);at_=now;return text_;
}
#ifdef _WIN32
namespace {
struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
// Each entry's bytes occur exactly once in the executable sections, at its address.
void verify(discovery::Sampler& s,const std::vector<Entry>& entries,const char* what){
    std::vector<unsigned> counts(entries.size());
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error(std::string(what)+" code size cap");
        std::vector<uint8_t> code(section.size);if(!s.readOnce(section.address,code.data(),code.size()))throw std::runtime_error(std::string(what)+" code unreadable");
        for(size_t k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];if(section.address+size_t(at-code.begin())!=s.base+e.rva)throw std::runtime_error(std::string(what)+" function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)if(n!=1)throw std::runtime_error(std::string(what)+" function signature missing/ambiguous");
}
}
#endif
VoiceApi nativeVoiceApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native voice API requires Windows");
#else
    const uint64_t rva=0x273bb10,bodyVtable=0x3d7de48;   // DSPlayerAKukanEntity (the partner body's class)
    verify(s,{{rva,{0x4c,0x8b,0xdc,0x55,0x53,0x49,0x8d,0x6b,0xa1,0x48,0x81,0xec,0xb8,0x00,0x00,0x00,0x48,0x8b,0x05,0x09,0x55,0x82,0x02,0x48}}},"voice");
    auto play=reinterpret_cast<void(*)(uint64_t,int32_t)>(s.base+rva);auto* sp=&s;const uint64_t vtable=s.base+bodyVtable;
    VoiceApi a;
    a.play=[play,sp,vtable](uint64_t body,unsigned index){
        uint64_t v=0;if(!body||index>=VoiceContainers||!sp->read(body,&v,8)||v!=vtable)return false;
        play(body,int32_t(index));return true;};   // the game checks the container count and its sentence count itself
    return a;
#endif
}
PulseApi nativePulseApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native pulse API requires Windows");
#else
    // The named global render variables' setters: float, int, bool (name, value).
    const uint64_t setFloat=0x22af4f0,setInt=0x22af220,setBool=0x22af7b0;
    verify(s,{{setFloat,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0xc5,0xfa,0x11,0x4c,0x24,0x10}},
              {setInt,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0xef,0x1d,0xcb,0x02,0x48,0x33,0xc4,0x48,0x89,0x44,0x24}},
              {setBool,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x5f,0x18,0xcb,0x02,0x48,0x33,0xc4,0x48,0x89,0x44,0x24}}},"pulse");
    auto f=reinterpret_cast<void(*)(const char*,float)>(s.base+setFloat);auto i=reinterpret_cast<void(*)(const char*,int32_t)>(s.base+setInt);
    auto b=reinterpret_cast<void(*)(const char*,bool)>(s.base+setBool);
    PulseApi a;
    a.show=[f,i,b](unsigned slot,const std::array<double,3>& at,float dx,float dy,float angle,float distance){
        for(double x:at)if(!std::isfinite(x))return false;
        if(slot>1||!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(angle)||!(distance>0)||distance>1000)return false;
        static const char* const names[2][6]={
            {"OdradekSensorPositionX_0","OdradekSensorPositionY_0","OdradekSensorEmitDirectionX_0","OdradekSensorEmitDirectionY_0","OdradekSensorAngleRadian_0","OdradekSensorDistance_0"},
            {"OdradekSensorPositionX_1","OdradekSensorPositionY_1","OdradekSensorEmitDirectionX_1","OdradekSensorEmitDirectionY_1","OdradekSensorAngleRadian_1","OdradekSensorDistance_1"}};
        const float values[6]={float(at[0]),float(at[1]),dx,dy,angle,distance};
        for(int k=0;k<6;++k)f(names[slot][k],values[k]);   // the game's own order (0x282c659..0x282c69f)
        i("OdradekSensorLastIndex",int32_t(slot));b("OdradekSensorLastIndexValid",true);return true;};
    return a;
#endif
}
}
