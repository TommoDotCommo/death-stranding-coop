#include "vehicle.h"
#include "fake_memory.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
using namespace mod::vehicle;
namespace {
void check(bool ok,const char* text) {if(!ok)throw std::runtime_error(std::string("vehicle oracle: ")+text);}
#define CHECK(x) check(bool(x),#x)
struct World {
    fakemem::Fake f;
    std::map<uint64_t,unsigned char> image;
    std::function<void(uint64_t,size_t)> after;
    unsigned reads=0,vehicleReads=0;
    static constexpr size_t Porter=0x6000, Vehicle=0x8000, Other=0xa000, Copy=0x9000;
    static constexpr uint64_t BikeTable=0x3e25db8,TruckTable=0x3e26790,CarTable=0x3e274d8,PorterTable=0x3d7d0d8;
    mod::discovery::Sampler sampler;
    uint64_t address(size_t offset) const {return f.base+offset;}
    template<class T> void put(size_t offset,T value) {f.put<T>(offset,value);}
    template<class T> void code(uint64_t rva,T value) {
        auto* bytes=reinterpret_cast<unsigned char*>(&value);
        for(size_t i=0;i<sizeof value;++i)image[f.base+rva+i]=bytes[i];
    }
    void type(uint64_t table,uint64_t getter,uint64_t metadata) {
        code<uint64_t>(table,f.base+getter);
        unsigned char bytes[8]={0x48,0x8d,0x05,0,0,0,0,0xc3};int32_t d=int32_t(metadata-getter-7);
        std::memcpy(bytes+3,&d,4);for(size_t i=0;i<8;++i)code<uint8_t>(getter+i,bytes[i]);
    }
    bool read(uint64_t a,void* out,size_t n) {
        ++reads;if(a>=address(Vehicle)&&a<address(Vehicle+0x1120))++vehicleReads;
        bool ok=true;
        if(a>=f.base&&a-f.base<f.data.size())ok=f.read(a,out,n);
        else for(size_t i=0;i<n;++i) {auto it=image.find(a+i);if(it==image.end()){ok=false;break;}static_cast<unsigned char*>(out)[i]=it->second;}
        if(after)after(a,n);
        return ok;
    }
    // Live layout (2026-11-08): seated porter's parent (+0x70) is the vehicle; the vehicle's first child (+0x78) ->
    // next sibling (+0x80) chain contains the porter.
    World():sampler([this](uint64_t a,void* out,size_t n){return read(a,out,n);},f.base) {
        f.select(2);sampler.scan(mod::discovery::PinnedHash,mod::discovery::PinnedHash);
        type(PorterTable,0x2711b40,0x4bc6e00);
        type(BikeTable,0x2f6bb60,0x4ca65a0);type(TruckTable,0x2f6bcb0,0x4ca6d60);type(CarTable,0x2f6bc10,0x4ca7660);
        put<uint64_t>(Porter,address(PorterTable));put<uint64_t>(Porter+0x70,address(Vehicle));
        put<uint64_t>(Vehicle,address(BikeTable));put<uint32_t>(Vehicle+0x4c4,3);put<uint64_t>(Vehicle+0x78,address(Porter));
        position(100,200,30);const float rotation[9]={0,1,0,-1,0,0,0,0,1};
        for(unsigned i=0;i<9;++i)put<float>(Vehicle+0xe0+4*i,rotation[i]);
    }
    void position(double x,double y,double z) {put<double>(Vehicle+0xc8,x);put<double>(Vehicle+0xd0,y);put<double>(Vehicle+0xd8,z);}
    void foot() {put<uint64_t>(Porter+0x70,0);put<uint64_t>(Vehicle+0x78,0);}
    void ride(size_t vehicle=Vehicle) {put<uint64_t>(Porter+0x70,address(vehicle));put<uint64_t>(vehicle+0x78,address(Porter));}
    void refuse(const char* reason=nullptr) {Reader r(sampler);auto o=r.read(1000000);CHECK(!o.available&&!o.coherent&&!o.riding&&!o.speedAvailable&&o.vehicle==0);CHECK(!o.reason.empty());if(reason)CHECK(o.reason==reason);}
    void changeDuringPose(std::function<void()> change) {
        after=[this,change](uint64_t a,size_t){if(a==address(Vehicle+0xc8)){auto saved=change;after=nullptr;saved();}};
    }
};
using Test=std::pair<const char*,std::function<void()>>;
std::vector<Test> tests={
    {"bike",[]{World w;Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&o.coherent&&o.riding&&o.kind==Kind::Motorbike);CHECK(o.seat==Seat::Driver&&o.seatIndex==0);CHECK(o.vehicle==w.address(World::Vehicle)&&o.parent==o.vehicle);CHECK(o.position[0]==100&&o.position[1]==200&&o.position[2]==30);CHECK(o.rotation[1]==1&&o.rotation[3]==-1);CHECK(!o.speedAvailable);}},
    {"truck",[]{World w;w.put<uint64_t>(World::Vehicle,w.address(World::TruckTable));w.put<uint32_t>(World::Vehicle+0x4c4,2);Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&o.riding&&o.kind==Kind::Truck&&o.seat==Seat::Driver);}},
    {"car",[]{World w;w.put<uint64_t>(World::Vehicle,w.address(World::CarTable));w.put<uint32_t>(World::Vehicle+0x4c4,1);Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&o.riding&&o.kind==Kind::Car);}},
    {"foot",[]{World w;w.foot();w.vehicleReads=0;Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&o.coherent&&!o.riding&&o.kind==Kind::None&&o.seat==Seat::None&&!o.speedAvailable&&o.vehicle==0);CHECK(w.vehicleReads==0&&o.reason.empty());}},
    {"other_parent",[]{World w;w.put<uint64_t>(World::Other,w.address(World::PorterTable));w.put<uint64_t>(World::Porter+0x70,w.address(World::Other));Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&!o.riding&&o.kind==Kind::None&&o.vehicle==0&&o.reason=="parent is not a vehicle");}},
    {"sibling",[]{World w;w.put<uint64_t>(World::Vehicle+0x78,w.address(World::Other));w.put<uint64_t>(World::Other+0x80,w.address(World::Porter));Reader r(w.sampler);auto o=r.read(1000000);CHECK(o.available&&o.riding);}},
    {"not_child",[]{World w;w.put<uint64_t>(World::Vehicle+0x78,0);w.refuse("porter not among the vehicle's children");}},
    {"child_cycle",[]{World w;w.put<uint64_t>(World::Vehicle+0x78,w.address(World::Other));w.put<uint64_t>(World::Other+0x80,w.address(World::Other));auto before=w.reads;w.after=[&](uint64_t,size_t){if(w.reads-before>8*Reader::MaxChildren)throw std::runtime_error("runaway child walk");};w.refuse("porter not among the vehicle's children");}},
    {"child_pointer",[]{World w;w.put<uint64_t>(World::Vehicle+0x78,w.address(World::Other)+3);w.refuse("child pointer invalid");}},
    {"parent_pointer",[]{World w;w.put<uint64_t>(World::Porter+0x70,w.address(World::Vehicle)+4);w.refuse("parent pointer invalid");}},
    {"parent_alias",[]{World w;w.put<uint64_t>(World::Porter+0x70,w.address(World::Porter));w.refuse("parent aliases porter");}},
    {"sampler",[]{World w;w.sampler.signatures.pop_back();auto before=w.reads;w.refuse("sampler not validated");CHECK(w.reads==before);}},
    {"menu",[]{World w;w.f.menu();w.refuse();}},
    {"selection",[]{World w;w.f.select(2,0);w.refuse();}},
    {"human",[]{World w;w.f.data[0x5079]=1;w.refuse();}},
    {"porter_type",[]{World w;w.put<uint64_t>(World::Porter,w.address(World::BikeTable));w.refuse();}},
    {"table_identity",[]{World w;w.code<uint64_t>(World::BikeTable,w.address(0x2711b40));w.refuse("vehicle type unverified");}},
    {"getter_identity",[]{World w;w.code<uint8_t>(0x2f6bb60+7,0x90);w.refuse("vehicle type unverified");}},
    {"kind",[]{World w;w.put<uint32_t>(World::Vehicle+0x4c4,2);w.refuse("vehicle kind differs");}},
    {"position_finite",[]{World w;w.position(0,std::numeric_limits<double>::infinity(),0);w.refuse();}},
    {"rotation_finite",[]{World w;w.put<float>(World::Vehicle+0xe0,std::numeric_limits<float>::quiet_NaN());w.refuse();}},
    {"rotation_orthogonal",[]{World w;w.put<float>(World::Vehicle+0xe0,1);w.refuse();}},
    {"rotation_proper",[]{World w;const float reflection[9]={1,0,0,0,1,0,0,0,-1};for(unsigned i=0;i<9;++i)w.put<float>(World::Vehicle+0xe0+4*i,reflection[i]);w.refuse();}},
    {"unreadable",[]{World w;w.f.blocked=w.address(World::Vehicle+0xe0);w.refuse("memory unreadable or torn");}},
    {"torn",[]{World w;w.after=[&](uint64_t a,size_t){if(a==w.address(World::Vehicle+0xc8))w.f.data[World::Vehicle+0xc8]^=1;};w.refuse();}},
    {"manager_changed",[]{World w;w.changeDuringPose([&]{w.f.menu();});w.refuse();}},
    {"player_changed",[]{World w;w.changeDuringPose([&]{w.put<uint64_t>(0x4058,w.address(0x5100));});w.refuse();}},
    {"selection_changed",[]{World w;w.changeDuringPose([&]{w.f.select(3);});w.refuse();}},
    {"porter_changed",[]{World w;w.changeDuringPose([&]{w.put<uint64_t>(0x5070,w.address(World::Vehicle));});w.refuse();}},
    {"parent_changed",[]{World w;w.changeDuringPose([&]{w.put<uint64_t>(World::Porter+0x70,0);});w.refuse("observation changed");}},
    {"child_changed",[]{World w;w.changeDuringPose([&]{w.put<uint64_t>(World::Vehicle+0x78,0);});w.refuse("observation changed");}},
    {"speed",[]{World w;Reader r(w.sampler);CHECK(!r.read(1000000).speedAvailable);w.position(103,204,30);auto o=r.read(1250000);CHECK(o.available&&o.speedAvailable&&std::abs(o.speed-20)<1e-9);w.position(103,204,35);o=r.read(1500000);CHECK(o.speedAvailable&&std::abs(o.speed-20)<1e-9);o=r.read(1750000);CHECK(o.speedAvailable&&o.speed==0);}},
    {"speed_gap",[]{World w;Reader r(w.sampler);r.read(1000000);w.position(101,200,30);CHECK(!r.read(2000000).speedAvailable);CHECK(!r.read(2000000).speedAvailable);CHECK(!r.read(1000000).speedAvailable);CHECK(!r.read(1000001).speedAvailable);}},
    {"speed_jump",[]{World w;Reader r(w.sampler);r.read(1000000);w.position(1000,200,30);auto o=r.read(1250000);CHECK(o.available&&!o.speedAvailable);CHECK(r.read(1500000).speedAvailable);}},
    {"speed_reset",[]{World w;Reader r(w.sampler);r.read(1000000);w.f.menu();CHECK(!r.read(1250000).available);w.f.game();CHECK(!r.read(1500000).speedAvailable);r.reset();CHECK(!r.read(1750000).speedAvailable);w.foot();CHECK(!r.read(2000000).riding);w.ride();CHECK(!r.read(2250000).speedAvailable);}},
    {"speed_vehicle",[]{World w;Reader r(w.sampler);r.read(1000000);for(size_t i=0;i<0x600;++i)w.f.data[World::Copy+i]=w.f.data[World::Vehicle+i];w.ride(World::Copy);auto o=r.read(1250000);CHECK(o.available&&o.riding&&o.vehicle==w.address(World::Copy)&&!o.speedAvailable);}},
    {"stamp",[]{World w;Monitor m(w.sampler);auto before=w.reads;CHECK(!m.command("watch wrong\n","current",1000000));CHECK(m.tick(1000000).empty()&&w.reads==before);CHECK(!m.command("watch current","current",1000000));CHECK(m.command("read current\n","current",1000000));CHECK(!m.tick(1000000).empty());CHECK(m.tick(1250000).empty());}},
    {"monitor_rate",[]{World w;Monitor m(w.sampler);CHECK(m.command("watch current\n","current",1000000));CHECK(!m.tick(1000000).empty());auto before=w.reads;CHECK(m.tick(1249999).empty()&&w.reads==before);CHECK(!m.tick(1250000).empty());CHECK(m.tick(1250000).empty());CHECK(!m.tick(2000000).empty());CHECK(m.tick(2000000).empty());CHECK(!m.command("off wrong\n","current",2000000));CHECK(!m.tick(2250000).empty());CHECK(m.command("off current\n","current",2250000));CHECK(m.tick(2500000).empty());}},
    {"monitor_cap",[]{World w;Monitor m(w.sampler);m.command("watch current\n","current",0);unsigned rows=0;for(uint64_t us=0;us<=Monitor::DurationUs+Monitor::PeriodUs;us+=Monitor::PeriodUs)if(!m.tick(us).empty())++rows;CHECK(rows==Monitor::MaxSamples);CHECK(m.tick(Monitor::DurationUs*2).empty());m.command("watch current\n","current",0);CHECK(!m.tick(0).empty());CHECK(m.tick(Monitor::DurationUs).empty());m.command("watch current\n","current",100);CHECK(m.tick(99).empty());CHECK(m.tick(200000).empty());}},
    {"log",[]{World w;Reader r(w.sampler);auto text=row(r.read(1000000));CHECK(text.find("\"position\":[100,200,30]")!=std::string::npos);CHECK(text.find("\"speed\":null")!=std::string::npos);CHECK(text.find("\"riding\":true")!=std::string::npos);w.f.menu();text=row(r.read(1250000));CHECK(text.find("\"available\":false,\"riding\":null")!=std::string::npos);CHECK(text.find("\"position\":null")!=std::string::npos);}}
};
}
int main(int argc,char** argv) {
    try {unsigned ran=0;for(const auto& test:tests)if(argc==1||test.first==std::string(argv[1])) {test.second();++ran;}
        CHECK(ran>0);std::printf("vehicle: %u host oracles passed\n",ran);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
