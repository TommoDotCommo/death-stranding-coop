#include "wheels.h"
#include <cmath>
#include <cstdio>
using namespace mod::wheels;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"wheels oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    // A truck-like skeleton: root 0, body 1, per wheel an arm chain 1 -> a -> b -> wheel.
    std::vector<int16_t> parents={-1,0,1,2,3,1,5,6,1,8,9,10};
    auto c=chain({4,7,11,-1,99},parents);   // three wheels; -1 and 99 are out of range
    CHECK((c==std::vector<uint16_t>{1,2,3,4,5,6,7,8,9,10,11}));                 // wheel and up to three parents, never the root
    CHECK((chain({2},parents)==std::vector<uint16_t>{1,2}));
    CHECK(chain({0},parents).empty());
    std::vector<int16_t> deep(40,-1);for(int16_t j=1;j<40;++j)deep[size_t(j)]=int16_t(j-1);
    std::vector<int32_t> many;for(int j=39;j>0;j-=1)many.push_back(j);
    CHECK(chain(many,deep).size()==ds::AnimationJoints);
    // Wire: quaternion to 1/32767, millimetres; normalised back.
    Joint j;j.index=7;j.rotation={0.6f,0,0,0.8f};j.position={1.2345f,-0.5f,0.0004f};
    auto w=toWire(j);CHECK(w.index==7&&w.rotation[0]==19660&&w.rotation[3]==26214&&w.position[0]==1235&&w.position[1]==-500&&w.position[2]==0);
    auto b=fromWire(w);double n=0;for(auto x:b.rotation)n+=double(x)*x;CHECK(std::fabs(n-1)<1e-6&&std::fabs(b.rotation[0]-0.6f)<1e-4&&std::fabs(b.position[0]-1.235f)<1e-6);
    // Blend: halfway between two spin angles, the short way round (q and -q are the same rotation).
    Joint a;a.index=3;a.rotation={0,0,0,1};Joint z;z.index=3;z.rotation={0,0,-std::sqrt(.5f),-std::sqrt(.5f)};z.position={0,0.1f,0};
    auto m=blend({a},{z},.5f);   // z is 90 degrees about -z written with w < 0: halfway is 45 degrees, not 135
    CHECK(m.size()==1&&std::fabs(m[0].rotation[3]-std::cos(0.3927f))<1e-3&&std::fabs(m[0].position[1]-0.05f)<1e-6);
    Joint other;other.index=9;CHECK(blend({other},{z},.5f)[0].rotation==other.rotation);   // not in the next sample: kept
    if(!failures)std::puts("wheels chain, wire and blend checks passed");
    return failures?1:0;
}
