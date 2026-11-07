#include "carry_visual.h"
#include <algorithm>
#include <map>
#include <sstream>
namespace mod::carry {
namespace {
template<class T> bool load(const discovery::Sampler& s,uint64_t a,T& v){return a&&s.read(a,&v,sizeof v);}
bool pointers(const discovery::Sampler& s,uint64_t array,int32_t count,size_t cap,std::vector<uint64_t>& out){
    if(count<0||size_t(count)>cap)return false;
    out.assign(size_t(count),0);if(!count)return true;
    return array&&s.read(array,out.data(),out.size()*8);
}
// Ref layout of a type-6 event (below 2^53).
constexpr unsigned IndexAt=0,TotalAt=4,SlotAt=9,ColumnAt=16,LevelAt=20,CountAt=26,GenerationAt=34;
uint64_t field(uint64_t ref,unsigned at,unsigned bits){return (ref>>at)&((uint64_t(1)<<bits)-1);}
}
const char* slotName(uint8_t s){
    static const char* names[]={"Default","BackPack","LeftArm","RightArm","LeftWaist","RightWaist","LeftHand","RightHand","LeftCarrier","RightCarrier"};
    return s<10?names[s]:"Other";
}
std::optional<std::vector<Item>> readCarried(const discovery::Sampler& s,uint64_t entity,uint64_t vtable,uint64_t manager){
    uint64_t component=0,v=0,carrier=0;
    if(!entity||!load(s,entity+PorterCarrier,component)||!component)return std::nullopt;
    if(!load(s,component,v)||v!=vtable)return std::nullopt;
    if(!load(s,component+CarrierObject,carrier)||!carrier)return std::nullopt;
    int32_t slotCount=0;uint64_t slotArray=0;std::vector<uint64_t> slots;
    if(!load(s,carrier+Slots,slotCount)||!load(s,carrier+SlotArray,slotArray)||!pointers(s,slotArray,slotCount,MaxSlots,slots))return std::nullopt;
    std::vector<Item> out;
    for(auto slot:slots){
        uint8_t type=0;int32_t stackCount=0;uint64_t stackArray=0;std::vector<uint64_t> stacks;
        if(!slot||!load(s,slot+SlotType,type))return std::nullopt;
        if(!cargoSlot(type))continue;
        if(!load(s,slot+Stacks,stackCount)||!load(s,slot+StackArray,stackArray)||!pointers(s,stackArray,stackCount,MaxStacks,stacks))return std::nullopt;
        for(size_t c=0;c<stacks.size();++c){
            int32_t n=0;uint64_t itemArray=0;std::vector<uint64_t> records;
            if(!stacks[c]||!load(s,stacks[c]+Items,n)||!load(s,stacks[c]+ItemArray,itemArray)||!pointers(s,itemArray,n,MaxLevels,records))return std::nullopt;
            for(size_t l=0;l<records.size();++l){
                uint64_t r=records[l];
                // Only the manager's own records (inline array), and only those that name this carrier as their holder.
                if(r<manager+RecordBase||(r-manager-RecordBase)%RecordStride||(r-manager-RecordBase)/RecordStride>=RecordCount)continue;
                uint64_t holder=0;Item it;uint32_t kind=0;
                if(!load(s,r+RecordHolder,holder)||!load(s,r+RecordHash,it.hash)||!load(s,r+RecordKind,kind))return std::nullopt;
#ifndef MUTANT_CARRY_HOLDER
                if(holder!=carrier)continue;   // a stale pointer, or an item already handed to another carrier
#endif
                it.index=uint32_t((r-manager-RecordBase)/RecordStride);it.slot=type;it.column=uint8_t(c);it.level=uint8_t(l);it.kind=uint8_t(kind);
#ifndef MUTANT_CARRY_CAP
                if(out.size()>=MaxItems)return out;
#endif
                out.push_back(it);
            }
        }
    }
    return out;
}
Summary summarise(const std::vector<Item>& items){
    Summary out;
    for(auto& it:items){
        if(!cargoSlot(it.slot))continue;
        if(!out.empty()){auto& e=out.back();
            if(e.hash==it.hash&&e.slot==it.slot&&e.column==it.column&&unsigned(e.level)+e.count==it.level&&e.count<255){++e.count;continue;}}
#ifndef MUTANT_CARRY_CAP
        if(out.size()>=MaxEntries)break;
#endif
        out.push_back({it.hash,it.slot,uint8_t(std::min<unsigned>(it.column,15)),uint8_t(std::min<unsigned>(it.level,63)),1});
    }
    return out;
}
std::string describe(const Summary& s){
    std::ostringstream o;o<<s.size()<<(s.size()==1?" entry":" entries");
    for(auto& e:s)o<<"; "<<e.hash<<" "<<slotName(e.slot)<<" stack "<<unsigned(e.column)<<" level "<<unsigned(e.level)<<" x"<<unsigned(e.count);
    return o.str();
}
std::vector<ds::CargoEvent> encode(const Summary& s,uint16_t generation){
    std::vector<ds::CargoEvent> out;size_t total=std::min(s.size(),MaxEntries);
    auto base=[&](size_t index){return uint64_t(index)<<IndexAt|uint64_t(total)<<TotalAt|uint64_t(generation)<<GenerationAt;};
    if(!total){ds::CargoEvent e;e.type=WireType;e.ref=base(0);out.push_back(e);return out;}
    for(size_t i=0;i<total;++i){auto& x=s[i];ds::CargoEvent e;e.type=WireType;e.item=x.hash;
        e.ref=base(i)|uint64_t(x.slot&0x7f)<<SlotAt|uint64_t(x.column&0xf)<<ColumnAt|uint64_t(x.level&0x3f)<<LevelAt|uint64_t(x.count)<<CountAt;
        out.push_back(e);}
    return out;
}
std::optional<Summary> Receiver::feed(const ds::CargoEvent& e){
    if(e.type!=WireType||e.ref>>50){reset();return std::nullopt;}
    auto index=uint8_t(field(e.ref,IndexAt,4)),total=uint8_t(field(e.ref,TotalAt,5));auto generation=uint16_t(field(e.ref,GenerationAt,16));
    if(total>MaxEntries||(total&&index>=total)){reset();return std::nullopt;}
    if(!total){
        reset();
#ifndef MUTANT_CARRY_DROP
        return Summary{};   // carries nothing now
#else
        return std::nullopt;
#endif
    }
    Entry x;x.hash=e.item;x.slot=uint8_t(field(e.ref,SlotAt,7));x.column=uint8_t(field(e.ref,ColumnAt,4));x.level=uint8_t(field(e.ref,LevelAt,6));
    x.count=uint8_t(field(e.ref,CountAt,8));
    if(!x.count||!cargoSlot(x.slot)){reset();return std::nullopt;}
    if(index==0){pending_.clear();active_=true;generation_=generation;total_=total;}
    else if(!active_||generation!=generation_||total!=total_||index!=pending_.size()){reset();return std::nullopt;}
    pending_.push_back(x);
    if(pending_.size()<total_)return std::nullopt;
    Summary done;done.swap(pending_);active_=false;return done;
}
std::vector<ds::CargoEvent> Sender::observe(const std::optional<Summary>& read,bool connected,uint64_t now){
    if(read&&(!haveCandidate_||*read!=candidate_)){candidate_=*read;since_=now;haveCandidate_=true;}
    if(!connected){wasConnected_=false;return {};}
    // A new connection: what is carried now, even if unchanged (the partner may have restarted and forgotten it).
    if(!wasConnected_)resend_=true;
    wasConnected_=true;
    if(!haveCandidate_||now-since_<Stable)return {};
    bool changed=!haveSent_||candidate_!=sent_;
    if(!resend_&&(!changed||(haveSent_&&now-lastSend_<MinGap)))return {};
    resend_=false;sent_=candidate_;haveSent_=true;lastSend_=now;
    return encode(sent_,++generation_);
}
void View::partner(const Summary& s,Sink& sink){
#ifndef MUTANT_CARRY_DUPLICATE
    shown_=s;
#else
    shown_.insert(shown_.end(),s.begin(),s.end());
#endif
    if(shown_.empty()){if(attached_)sink.detach();attached_=false;return;}
#ifndef MUTANT_CARRY_REAL
    sink.attach(shown_);
#else
    for(auto& e:shown_)for(unsigned i=0;i<e.count;++i)sink.hold(e.hash,e.slot);
#endif
    attached_=true;
}
void View::lost(Sink& sink){
#ifndef MUTANT_CARRY_LOST
    if(attached_)sink.detach();
    attached_=false;shown_.clear();
#else
    (void)sink;
#endif
}
bool Exchange::incoming(const ds::CargoEvent& e,Sink& sink){
    if(e.type!=WireType)return false;
    if(auto s=receiver_.feed(e))view_.partner(*s,sink);
    return true;
}
std::vector<ds::CargoEvent> Exchange::tick(const std::optional<Summary>& read,bool connected,uint64_t now,Sink& sink){
    if(connected_&&!connected){view_.lost(sink);receiver_.reset();}
    connected_=connected;
    return sender_.observe(read,connected,now);
}
std::optional<Summary> parseSpec(const std::string& text){
    if(text=="none")return Summary{};
    if(text.empty()||text.back()==',')return std::nullopt;
    Summary out;std::map<uint8_t,unsigned> height;std::stringstream all(text);std::string part;
    while(std::getline(all,part,',')){
        std::vector<std::string> f;std::stringstream ps(part);std::string x;while(std::getline(ps,x,':'))f.push_back(x);
        if(f.empty()||f.size()>3)return std::nullopt;
        unsigned long long v[3]={0,1,1};
        for(size_t i=0;i<f.size();++i){if(f[i].empty()||f[i].size()>10||f[i].find_first_not_of("0123456789")!=std::string::npos)return std::nullopt;v[i]=std::stoull(f[i]);}
        if(v[0]>0xffffffffull||!v[0]||v[1]>127||!cargoSlot(uint8_t(v[1]))||!v[2]||v[2]>255)return std::nullopt;
        auto& h=height[uint8_t(v[1])];if(h+v[2]>63||out.size()>=MaxEntries)return std::nullopt;
        out.push_back({uint32_t(v[0]),uint8_t(v[1]),0,uint8_t(h),uint8_t(v[2])});h+=unsigned(v[2]);
    }
    if(out.empty())return std::nullopt;
    return out;
}
}
