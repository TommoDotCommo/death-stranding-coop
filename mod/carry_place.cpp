#include "carry_place.h"
#include "carry_visual.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mod::carryplace {
static double dot(const double* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Transform relative(const Transform& a,const Transform& w){
    Transform r;double d[3]={w.position[0]-a.position[0],w.position[1]-a.position[1],w.position[2]-a.position[2]};
    for(int k=0;k<3;++k)r.position[size_t(k)]=dot(d,&a.rotation[size_t(3*k)]);
    for(int i=0;i<3;++i)for(int k=0;k<3;++k){double row[3]={w.rotation[size_t(3*i)],w.rotation[size_t(3*i+1)],w.rotation[size_t(3*i+2)]};r.rotation[size_t(3*i+k)]=float(dot(row,&a.rotation[size_t(3*k)]));}
    return r;
}
Transform compose(const Transform& a,const Transform& rel){
    Transform w;
    for(int j=0;j<3;++j){
        w.position[size_t(j)]=a.position[size_t(j)];
        for(int k=0;k<3;++k)w.position[size_t(j)]+=rel.position[size_t(k)]*a.rotation[size_t(3*k+j)];
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double v=0;for(int k=0;k<3;++k)v+=double(rel.rotation[size_t(3*i+k)])*a.rotation[size_t(3*k+j)];w.rotation[size_t(3*i+j)]=float(v);}
    return w;
}
// Rows are the images of the axes: the usual rotation matrix has them as columns, m[j][i] = rows[i][j].
uint64_t packRotation(const std::array<float,9>& r){
    auto m=[&](int row,int col){return double(r[size_t(3*col+row)]);};
    double q[4];double t=m(0,0)+m(1,1)+m(2,2);
    if(t>0){double s=std::sqrt(t+1)*2;q[3]=s/4;q[0]=(m(2,1)-m(1,2))/s;q[1]=(m(0,2)-m(2,0))/s;q[2]=(m(1,0)-m(0,1))/s;}
    else if(m(0,0)>m(1,1)&&m(0,0)>m(2,2)){double s=std::sqrt(1+m(0,0)-m(1,1)-m(2,2))*2;q[3]=(m(2,1)-m(1,2))/s;q[0]=s/4;q[1]=(m(0,1)+m(1,0))/s;q[2]=(m(0,2)+m(2,0))/s;}
    else if(m(1,1)>m(2,2)){double s=std::sqrt(1+m(1,1)-m(0,0)-m(2,2))*2;q[3]=(m(0,2)-m(2,0))/s;q[0]=(m(0,1)+m(1,0))/s;q[1]=s/4;q[2]=(m(1,2)+m(2,1))/s;}
    else{double s=std::sqrt(1+m(2,2)-m(0,0)-m(1,1))*2;q[3]=(m(1,0)-m(0,1))/s;q[0]=(m(0,2)+m(2,0))/s;q[1]=(m(1,2)+m(2,1))/s;q[2]=s/4;}
    double n=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);if(!(n>1e-9)){q[0]=q[1]=q[2]=0;q[3]=1;n=1;}
    int big=0;for(int i=1;i<4;++i)if(std::fabs(q[i])>std::fabs(q[big]))big=i;
    double sign=q[big]<0?-1:1;uint64_t out=uint64_t(big);int shift=2;
    for(int i=0;i<4;++i){if(i==big)continue;
        double v=q[i]/n*sign*std::sqrt(2.);                                // in [-1, 1]
        long step=std::lround((std::clamp(v,-1.,1.)+1)*0.5*32767);out|=uint64_t(step)<<shift;shift+=15;}
    return out;
}
std::array<float,9> unpackRotation(uint64_t p){
    int big=int(p&3);double q[4];double sum=0;int shift=2;
    for(int i=0;i<4;++i){if(i==big)continue;double v=(double((p>>shift)&0x7fff)/32767*2-1)/std::sqrt(2.);q[i]=v;sum+=v*v;shift+=15;}
    q[big]=std::sqrt(std::max(0.,1-sum));
    double x=q[0],y=q[1],z=q[2],w=q[3];
    double m[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    std::array<float,9> r{};for(int row=0;row<3;++row)for(int col=0;col<3;++col)r[size_t(3*col+row)]=float(m[row][col]);
    return r;
}
template<class T> static bool load(const discovery::Sampler& s,uint64_t at,T& v){return s.read(at,&v,sizeof v);}
static bool pointers(const discovery::Sampler& s,uint64_t array,int32_t count,size_t most,std::vector<uint64_t>& out){
    out.clear();if(count<0||size_t(count)>most)return false;if(!count)return true;if(!array)return false;
    out.resize(size_t(count));return s.read(array,out.data(),out.size()*8);
}
namespace {
// Every drawn case on `slots` of `carrier` whose slot type `keep` accepts, relative to `anchor`, within `reach` m.
bool collect(const discovery::Sampler& s,uint64_t carrier,const std::vector<uint64_t>& slots,uint64_t manager,const Transform& anchor,
             bool(*keep)(uint8_t),double reach,std::vector<Placed>& out,std::vector<uint64_t>* kept){
    using namespace mod::carry;
    for(auto slot:slots){
        // The slot's header in one read (type +1, stack count +18, stack array +20, carrier +28): every read is guarded.
        uint8_t head[0x30];std::vector<uint64_t> stacks;
        if(!slot||!s.read(slot,head,sizeof head))return false;
        uint8_t type=head[SlotType];int32_t stackCount;uint64_t stackArray,slotCarrier;
        std::memcpy(&stackCount,head+Stacks,4);std::memcpy(&stackArray,head+StackArray,8);std::memcpy(&slotCarrier,head+SlotCarrier,8);
        if(!keep(type))continue;
        if(kept)kept->push_back(slot);
        if(!pointers(s,stackArray,stackCount,MaxStacks,stacks))return false;
        for(auto stack:stacks){
            int32_t n=0;uint64_t itemArray=0;std::vector<uint64_t> records;
            if(!stack||!load(s,stack+Items,n)||!load(s,stack+ItemArray,itemArray)||!pointers(s,itemArray,n,MaxLevels,records))return false;
            for(auto r:records){
                if(r<manager+RecordBase||(r-manager-RecordBase)%RecordStride||(r-manager-RecordBase)/RecordStride>=RecordCount)continue;
                // The record from its world transform (+38) to its flags (+204) in one read.
                uint64_t holder=0,mesh=0;uint32_t flags=0;Placed p;uint8_t block[RecordFlags+4-RecordWorld];
                if(!s.read(r+RecordWorld,block,sizeof block))return false;
                auto at=[&](uint64_t off){return block+(off-RecordWorld);};
                std::memcpy(&holder,at(RecordHolder),8);std::memcpy(&p.item,at(RecordHash),4);std::memcpy(&flags,at(RecordFlags),4);std::memcpy(&mesh,at(0x148),8);
                const uint8_t* raw=block;
                if(!((holder==slot&&slotCarrier==carrier)||holder==carrier)||!mesh)continue;   // not this carrier's, or not drawn
                if(!p.item){uint64_t item=0;uint32_t id=0;if(load(s,r+RecordItem,item)&&item&&load(s,item+ItemId,id))p.item=id;}
                Transform w;std::memcpy(w.position.data(),raw,24);std::memcpy(w.rotation.data(),raw+24,36);
                bool sane=true;for(auto c:w.position)sane=sane&&std::isfinite(c);for(auto c:w.rotation)sane=sane&&std::isfinite(c)&&std::fabs(c)<=1.01f;
                if(!sane)continue;
                p.variant=(flags&RecordVariant)!=0;p.slot=type;p.rel=relative(anchor,w);
                double d=std::sqrt(p.rel.position[0]*p.rel.position[0]+p.rel.position[1]*p.rel.position[1]+p.rel.position[2]*p.rel.position[2]);
                if(d>reach)continue;
                if(out.size()>=MaxPlaced)return true;
                out.push_back(p);
            }
        }
    }
    return true;
}
bool bodySlot(uint8_t t){return t>=1&&t<=5;}   // back, arms, hips
bool handSlot(uint8_t t){return t==6||t==7;}
bool anySlot(uint8_t){return true;}
}
std::optional<std::vector<Placed>> readPlaced(const discovery::Sampler& s,uint64_t entity,uint64_t vtable,uint64_t manager,const Transform& anchor,bool hands,SlotCache* cache,uint64_t now){
    using namespace mod::carry;
    uint64_t component=0,v=0,carrier=0;
    if(!entity||!load(s,entity+PorterCarrier,component)||!component||!load(s,component,v)||v!=vtable||!load(s,component+CarrierObject,carrier)||!carrier)return std::nullopt;
    int32_t slotCount=0;uint64_t slotArray=0;std::vector<uint64_t> slots;
    if(!load(s,carrier+Slots,slotCount)||!load(s,carrier+SlotArray,slotArray))return std::nullopt;
    bool cached=hands&&cache&&cache->carrier==carrier&&cache->array==slotArray&&now>=cache->at&&now-cache->at<2000;
    if(cached)slots=cache->slots;
    else if(!pointers(s,slotArray,slotCount,MaxSlots,slots))return std::nullopt;
    std::vector<uint64_t> handSlots;std::vector<Placed> out;
    if(!collect(s,carrier,slots,manager,anchor,hands?handSlot:bodySlot,3.0,out,hands?&handSlots:nullptr))return std::nullopt;
    if(hands&&cache&&!cached)*cache={carrier,slotArray,now,handSlots};
    return out;
}
std::optional<std::vector<Placed>> readCarrier(const discovery::Sampler& s,uint64_t carrier,uint64_t manager,const Transform& anchor,double reach){
    using namespace mod::carry;
    int32_t slotCount=0;uint64_t slotArray=0;std::vector<uint64_t> slots;std::vector<Placed> out;
    if(!carrier||!load(s,carrier+Slots,slotCount)||!load(s,carrier+SlotArray,slotArray)||!pointers(s,slotArray,slotCount,MaxSlots,slots))return std::nullopt;
    if(!collect(s,carrier,slots,manager,anchor,anySlot,reach,out,nullptr))return std::nullopt;
    return out;
}
ds::HeldCase toHeld(const Placed& p){
    ds::HeldCase h;h.item=p.item;h.tag=uint8_t((p.slot&0x7f)|(p.variant?0x80:0));
    for(int k=0;k<3;++k)h.position[k]=int16_t(std::clamp<long>(std::lround(p.rel.position[k]*1000),-ds::AnimationHandMaxMm,ds::AnimationHandMaxMm));
    h.rotation=packRotation(p.rel.rotation);return h;
}
Placed fromHeld(const ds::HeldCase& h){
    Placed p;p.item=h.item;p.slot=uint8_t(h.tag&0x7f);p.variant=(h.tag&0x80)!=0;
    for(int k=0;k<3;++k)p.rel.position[k]=h.position[k]/1000.;
    p.rel.rotation=unpackRotation(h.rotation&((1ull<<48)-1));return p;
}
static constexpr double YawStep=3.14159265358979323846/32767.;
std::vector<ds::CargoEvent> encode(const std::vector<Placed>& list,uint8_t generation,uint8_t type){
    std::vector<ds::CargoEvent> out;size_t total=std::min<size_t>(list.size(),MaxPlaced);
    auto head=[&](ds::CargoEvent& e){e.type=type;e.yaw=double(total|(size_t(generation)<<5))*YawStep;};
    if(!total){ds::CargoEvent e;head(e);out.push_back(e);return out;}
    for(size_t i=0;i<total;++i){
        ds::CargoEvent e;head(e);e.item=list[i].item;
        e.ref=packRotation(list[i].rel.rotation)|(uint64_t(i)<<47)|(uint64_t(list[i].variant)<<52);
        for(int k=0;k<3;++k)e.position[size_t(k)]=list[i].rel.position[size_t(k)]*100.;   // the wire keeps 1/100: 0.1 mm here
        out.push_back(e);
    }
    return out;
}
std::optional<std::vector<Placed>> Receiver::feed(const ds::CargoEvent& e){
    if(e.type!=type_)return std::nullopt;
    long word=std::lround(e.yaw/YawStep);if(word<0||word>0x1fff){reset();return std::nullopt;}
    uint8_t total=uint8_t(word&31),generation=uint8_t(word>>5),index=uint8_t((e.ref>>47)&31);
    if(total>MaxPlaced){reset();return std::nullopt;}
    if(!total){reset();return std::vector<Placed>{};}
#ifndef MUTANT_PLACE_ORDER
    if(index==0){pending_.clear();active_=true;generation_=generation;total_=total;}
    else if(!active_||generation!=generation_||total!=total_||index!=pending_.size()){reset();return std::nullopt;}
#else
    if(index==0){pending_.clear();active_=true;generation_=generation;total_=total;}
#endif
    Placed p;p.item=e.item;p.variant=(e.ref>>52)&1;p.rel.rotation=unpackRotation(e.ref&((uint64_t(1)<<47)-1));
    for(int k=0;k<3;++k)p.rel.position[size_t(k)]=e.position[size_t(k)]/100.;
    pending_.push_back(p);
    if(pending_.size()<total_)return std::nullopt;
    auto done=pending_;reset();return done;
}
bool Sender::alike(const std::vector<Placed>& a,const std::vector<Placed>& b,double limit){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){
        if(a[i].item!=b[i].item||a[i].variant!=b[i].variant)return false;
        double d=0;for(int k=0;k<3;++k){double x=a[i].rel.position[size_t(k)]-b[i].rel.position[size_t(k)];d+=x*x;}
        if(d>limit*limit)return false;
        double turn=0;for(int k=0;k<9;++k)turn=std::max(turn,double(std::fabs(a[i].rel.rotation[size_t(k)]-b[i].rel.rotation[size_t(k)])));
        if(turn>limit*5)return false;                                   // about 0.2 of a unit vector's end for Moved
    }
    return true;
}
std::vector<ds::CargoEvent> Sender::observe(const std::optional<std::vector<Placed>>& read,bool connected,uint64_t now){
    bool fresh=connected&&!wasConnected_;wasConnected_=connected;
    if(!read)return {};
    bool still=haveLast_&&alike(*read,last_,Still);
    last_=*read;haveLast_=true;
    if(!connected)return {};
    if(fresh)haveSent_=false;
#ifndef MUTANT_PLACE_RESTLESS
    if(!still)return {};
#endif
    (void)still;
    if(haveSent_&&alike(*read,sent_,Moved))return {};
    if(haveSent_&&now-sentAt_<MinGap)return {};
    sent_=*read;haveSent_=true;sentAt_=now;
    return encode(sent_,++generation_,type_);
}
}
