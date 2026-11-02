// Fake mapped PE + process memory for host tests: three pointer layers (manager -> selection -> player -> entity).
#pragma once
#include "discovery.h"
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
namespace mod::discovery {
#include "discovery_signatures.h"
}
namespace fakemem {
using namespace mod::discovery;
struct Fake {
    uint64_t base=0x100000;
    std::vector<unsigned char> data=std::vector<unsigned char>(0x10000);
    bool deny=false,torn=false;unsigned calls=0;
    uint64_t blocked=0;
    template<class T> void put(size_t o,T v) {memcpy(data.data()+o,&v,sizeof v);}
    bool read(uint64_t a,void* p,size_t n) {
        ++calls;
        if(deny||a<base||a-base>=data.size()||n>data.size()-(a-base)||(blocked&&a<=blocked&&blocked<a+n))return false;
        memcpy(p,data.data()+a-base,n);if(torn&&calls%2)static_cast<unsigned char*>(p)[0]^=1;return true;
    }
    Fake(){
        put<uint16_t>(0,0x5a4d);put<uint32_t>(0x3c,0x80);put<uint32_t>(0x80,0x4550);put<uint16_t>(0x84,0x8664);put<uint16_t>(0x86,2);put<uint16_t>(0x94,0xf0);put<uint16_t>(0x98,0x20b);put<uint32_t>(0x98+56,data.size());
        size_t h=0x188;put<uint32_t>(h+8,0x1000);put<uint32_t>(h+12,0x1000);put<uint32_t>(h+16,0x1000);put<uint32_t>(h+36,0x20000000);
        h+=40;put<uint32_t>(h+8,0xc000);put<uint32_t>(h+12,0x3000);put<uint32_t>(h+16,0xc000);
        unsigned k=0;
        for(auto c:candidates()) {size_t at=0x1100+k*0x100;std::istringstream stream(c.pattern);std::string t;size_t j=0;while(stream>>t)data[at+j++]=t=="??"?0:uint8_t(std::stoi(t,nullptr,16));
            for(auto r:c.refs){size_t target=0x3000+k*0x100;if(r.name=="player_manager_global")target=0x3000;put<int32_t>(at+r.instruction+r.length-4,int32_t(target-(at+r.instruction+r.length)));}++k;
        }
        put<uint64_t>(0x3000,base+0x4000);put<uint64_t>(0x3300,base+0x4200);put<uint64_t>(0x3400,base+0x4600);put<uint64_t>(0x3500,base+0x4800);
        put<uint64_t>(0x3800,base+0x5000);put<uint64_t>(0x5070,base+0x6000);data[0x5078]=1;
        for(unsigned i=0;i<6;++i)put<float>(0x60c8+i*4,float(i+1));
    }
    // Selection record of signature 1 (fake offset 0x3100, record starts 4 bytes earlier) and the player slot it picks.
    void select(int32_t index,int32_t mode=1,int32_t kind=2,uint64_t other=0) {
        put<int32_t>(0x30fc,index);put<int32_t>(0x3100,mode);put<int32_t>(0x3104,kind);put<uint64_t>(0x310c,other);
        if(index>=0&&index<64)put<uint64_t>(0x4000+0x48+8*size_t(index),base+0x5000);
    }
    // Live layout: position = 3 x f64 at entity+0xc8, rotation = 3x3 f32 row-major at entity+0xe0, entity = *(player+0x70).
    void pose(double x,double y,double z,const float* rotation) {
        put<double>(0x60c8,x);put<double>(0x60d0,y);put<double>(0x60d8,z);
        for(unsigned i=0;i<9;++i)put<float>(0x60e0+i*4,rotation[i]);
    }
    void menu() {put<uint64_t>(0x3000,0);}
    void game() {put<uint64_t>(0x3000,base+0x4000);}
    std::mutex lock;   // only the threaded fake-network build uses these two
    bool readLocked(uint64_t a,void* p,size_t n) {std::lock_guard<std::mutex> g(lock);return read(a,p,n);}
    template<class F> void mutate(F f) {std::lock_guard<std::mutex> g(lock);f();}
    Sampler sampler(){return Sampler([this](uint64_t a,void* p,size_t n){return read(a,p,n);},base);}
};
}
