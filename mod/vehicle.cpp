#include "vehicle.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mod::vehicle {
namespace {
// These are checked loaded table/getter pairs, never callable function pointers.
struct Type {uint64_t table, getter, metadata;};
constexpr Type Porter{0x3d7d0d8,0x2711b40,0x4bc6e00};
constexpr Type Bike{0x3e25db8,0x2f6bb60,0x4ca65a0};
constexpr Type Truck{0x3e26790,0x2f6bcb0,0x4ca6d60};
constexpr Type Car{0x3e274d8,0x2f6bc10,0x4ca7660};
void require(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
bool pointer(uint64_t p) {return p>=0x10000&&p<0x0000800000000000ULL&&p%8==0;}
uint64_t add(uint64_t p,uint64_t offset) {
    require(pointer(p)&&offset<0x0000800000000000ULL-p,"invalid pointer");return p+offset;
}
// Sampler copies twice. Retain bounded read dependencies and check them again
// after the observation, including the parent link and the vehicle's child list.
// Repeated equality is not an atomic snapshot or a proof against address reuse.
struct Copies {
    const discovery::Sampler& s;
    struct Copy {uint64_t address;std::vector<unsigned char> bytes;};
    std::vector<Copy> copies;
    void read(uint64_t a,void* out,size_t n) {
        require(s.read(a,out,n),"memory unreadable or torn");
        const auto* b=static_cast<unsigned char*>(out);copies.push_back({a,{b,b+n}});
    }
    template<class T> T value(uint64_t a) {T v{};read(a,&v,sizeof v);return v;}
    bool unchanged() const {
        for(const auto& c:copies) {
            std::vector<unsigned char> b(c.bytes.size());
            if(!s.read(c.address,b.data(),b.size())||b!=c.bytes)return false;
        }
        return true;
    }
    bool type(uint64_t object,Type t) {
        if(!pointer(object))return false;
        auto table=value<uint64_t>(object);
        if(table!=s.base+t.table)return false;
        if(value<uint64_t>(table)!=s.base+t.getter)return false;
        // Reviewable lea rax,[rip+metadata]; ret, including its exact disp32.
        unsigned char code[8]{},expected[8]={0x48,0x8d,0x05,0,0,0,0,0xc3};
        const int32_t displacement=int32_t(t.metadata-(t.getter+7));
        std::memcpy(expected+3,&displacement,4);read(s.base+t.getter,code,sizeof code);
        return std::memcmp(code,expected,sizeof code)==0;
    }
};
bool rotationValid(const std::array<float,9>& r) {
    for(float x:r)if(!std::isfinite(x))return false;
    // A vehicle may pitch/roll: check the full proper rotation, never upright yaw only.
    for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j) {
        double dot=0;for(unsigned k=0;k<3;++k)dot+=double(r[i*3+k])*r[j*3+k];
        if(std::abs(dot-(i==j?1.:0.))>.05)return false;
    }
    double det=double(r[0])*(double(r[4])*r[8]-double(r[5])*r[7])
              -double(r[1])*(double(r[3])*r[8]-double(r[5])*r[6])
              +double(r[2])*(double(r[3])*r[7]-double(r[4])*r[6]);
    return std::abs(det-1.)<=.05;
}
}
void Reader::reset() {
    previousPorter_=previousVehicle_=previousTime_=0;previousPosition_={};
}
unsigned Observation::expectedKindValue() const {return kind==Kind::Motorbike?3u:kind==Kind::Truck?2u:kind==Kind::Car?1u:0u;}
Observation Reader::read(uint64_t us) {
    Observation o;o.sampledAtUs=us;
    try {
        const auto& s=sampler_;
        require(s.signatures.size()==6,"sampler not validated");
        auto target=s.signatures[0].targets.find("player_manager_global");
        require(target!=s.signatures[0].targets.end(),"sampler not validated");
        Copies c{s,{}};
        auto manager=c.value<uint64_t>(target->second);
        require(pointer(manager),"manager unavailable");
        auto selected=s.selectPlayer(manager);
        require(!*selected.reason,selected.reason);
        auto player=selected.player;
        require(pointer(player),"player unavailable");
        require(c.value<uint16_t>(add(player,0x78))==1,"not local human");
        o.porter=c.value<uint64_t>(add(player,0x70));
        require(c.type(o.porter,Porter),"porter type unverified");
        // Live 2026-11-08 (Episode 3, a truck): a seated porter is a child entity of the vehicle (porter+0x70 = parent,
        // vehicle+0x78 = first child, +0x80 = next sibling); on foot the parent is null. The static ride-component path
        // was refuted there (the porter has no DSRideVehicleMovementComponent while driving).
        auto parent=c.value<uint64_t>(add(o.porter,ParentOffset));
        o.parent=parent;
        if(parent) {
            require(pointer(parent),"parent pointer invalid");
            require(parent!=o.porter,"parent aliases porter");
            auto table=c.value<uint64_t>(parent);
            unsigned expected=0;
            if(table==s.base+Bike.table) {require(c.type(parent,Bike),"vehicle type unverified");o.kind=Kind::Motorbike;expected=3;}
            else if(table==s.base+Truck.table) {require(c.type(parent,Truck),"vehicle type unverified");o.kind=Kind::Truck;expected=2;}
            else if(table==s.base+Car.table) {require(c.type(parent,Car),"vehicle type unverified");o.kind=Kind::Car;expected=1;}
        }
        if(o.kind!=Kind::Unknown) {
            o.vehicle=parent;
            require(c.value<uint32_t>(add(o.vehicle,0x4c4))==o.expectedKindValue(),"vehicle kind differs");
            // The vehicle must list the porter among its children (bounded walk).
            bool child=false;uint64_t next=c.value<uint64_t>(add(o.vehicle,FirstChildOffset));
            for(unsigned i=0;next&&i<MaxChildren;++i) {
                require(pointer(next),"child pointer invalid");
                if(next==o.porter){child=true;break;}
                next=c.value<uint64_t>(add(next,NextSiblingOffset));
            }
            require(child,"porter not among the vehicle's children");
            o.riding=true;o.seat=Seat::Driver;o.seatIndex=0; // A player always drives in this game (no passenger seats for players).
            o.variant=c.value<uint32_t>(add(o.vehicle,0x4c8));o.appearance=c.value<uint16_t>(add(o.vehicle,0x664));
            c.read(add(o.vehicle,0xc8),o.position.data(),sizeof(o.position));
            c.read(add(o.vehicle,0xe0),o.rotation.data(),sizeof(o.rotation));
            for(double x:o.position)require(std::isfinite(x),"vehicle position non-finite");
            require(rotationValid(o.rotation),"vehicle rotation invalid");
        } else {o.kind=Kind::None;o.seat=Seat::None;}
        require(c.unchanged(),"observation changed");
        auto selectedAgain=s.selectPlayer(manager);
        require(!*selectedAgain.reason&&selectedAgain.player==player&&selectedAgain.slot==selected.slot,"player selection changed");
        o.available=o.coherent=true;o.reason=parent&&!o.riding?"parent is not a vehicle":"";
        if(!o.riding) {reset();return o;}
        if(previousPorter_==o.porter&&previousVehicle_==o.vehicle
           &&us>previousTime_&&us-previousTime_>=MinGapUs&&us-previousTime_<=MaxGapUs) {
            double distance=0;for(unsigned i=0;i<3;++i)distance=std::hypot(distance,o.position[i]-previousPosition_[i]);
            double speed=distance/(double(us-previousTime_)/1000000.);
            if(std::isfinite(speed)&&speed<=MaxSpeed) {o.speed=speed;o.speedAvailable=true;}
        }
        previousPorter_=o.porter;previousVehicle_=o.vehicle;previousTime_=us;previousPosition_=o.position;
        return o;
    } catch(const std::exception& error) {
        reset();Observation unavailable;unavailable.sampledAtUs=us;unavailable.reason=error.what();return unavailable;
    }
}
std::string row(const Observation& o) {
    const char* kind=o.kind==Kind::None?"none":o.kind==Kind::Motorbike?"motorbike":o.kind==Kind::Truck?"truck":o.kind==Kind::Car?"car":"unknown";
    const char* seat=o.seat==Seat::None?"none":o.seat==Seat::Driver?"driver":o.seat==Seat::Passenger?"passenger":"unknown";
    std::ostringstream s;s<<std::setprecision(12)<<"VEHICLE {\"t_us\":"<<o.sampledAtUs<<",\"provisional\":true,\"available\":"<<(o.available?"true":"false")
        <<",\"riding\":"<<(o.available?(o.riding?"true":"false"):"null")<<",\"kind\":\""<<kind<<"\",\"seat\":\""<<seat
        <<"\",\"seat_index\":"<<o.seatIndex<<",\"porter\":\"0x"<<std::hex<<o.porter<<"\",\"parent\":\"0x"<<o.parent
        <<"\",\"vehicle\":\"0x"<<o.vehicle<<std::dec<<"\",\"position\":";
    if(o.available&&o.riding) {s<<'[';for(unsigned i=0;i<3;++i)s<<(i?",":"")<<o.position[i];s<<']';} else s<<"null";
    s<<",\"rotation\":";
    if(o.available&&o.riding) {s<<'[';for(unsigned i=0;i<9;++i)s<<(i?",":"")<<o.rotation[i];s<<']';} else s<<"null";
    s<<",\"speed\":";if(o.speedAvailable)s<<o.speed;else s<<"null";
    s<<",\"speed_source\":\"position_delta\",\"reason\":\""<<o.reason<<"\"}\n";return s.str();
}
bool Monitor::command(const std::string& content,const std::string& stamp,uint64_t us) {
    auto suffix=" "+stamp+"\n";
    if(content=="off"+suffix) {remaining_=0;reader_.reset();return true;}
    unsigned count=content=="read"+suffix?1:content=="watch"+suffix?MaxSamples:0;
    if(!count)return false;
    remaining_=count;started_=last_=us;sampled_=false;reader_.reset();return true;
}
std::string Monitor::tick(uint64_t us) {
    if(!remaining_)return {};
    if(us<started_||us-started_>=DurationUs||(sampled_&&us<last_)) {remaining_=0;reader_.reset();return {};}
    if(sampled_&&us-last_<PeriodUs)return {};
    sampled_=true;last_=us;--remaining_;return row(reader_.read(us));
}
}
