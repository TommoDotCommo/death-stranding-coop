// Deterministic two-core adapter model: fake clock, independent scheduler wakes,
// real production trajectory, core and deadline arithmetic. No OS sleeps/sockets.
#include "providers.h"
#include "timing.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
struct Provider: ds::StateProvider {
    ds::State local;ds::Render remote;uint64_t sampled=0;bool legacy=false;
    bool readLocal(ds::State& s) override {s=local;return true;}
    uint64_t localSampleTime(uint64_t now) const override {return legacy?now:sampled;}
    bool isInCutsceneOrLoading() const override {return false;}
    void applyRemote(const ds::Render& r) override {remote=r;}
};
struct Wire: ds::Transport {
    std::vector<ds::Bytes> out;
    void send(const ds::Bytes& b) override {out.push_back(b);}
};
struct Delivery {uint64_t due;unsigned side;ds::Bytes bytes;};
int main(int argc,char** argv) {
    const bool legacy=argc>1 && std::string(argv[1])=="legacy";
    const bool slowPing=argc>1 && std::string(argv[1])=="slow-ping";
    double worst95=0,worst99=0;uint64_t worstPong=0;
    for(unsigned seed=1;seed<=100;++seed) {
        std::mt19937 rng(seed);Provider p[2];Wire w[2];
        ds::Core a({ds::Role::Host,42,1,120},p[0],w[0]),b({ds::Role::Client,42,2,120},p[1],w[1]);
        ds::Core* cores[]={&a,&b};
        if(slowPing) {ds::Protection protection;protection.pingRetry=false;a.setProtections(protection);b.setProtections(protection);}
        for(auto& x:p)x.legacy=legacy;
        uint64_t network[2]={0,700},frame[2]={200,1100},firstPong[2]={0,0};
        unsigned pings[2]={0,0};std::vector<Delivery> queue;std::vector<double> errors;
        for(uint64_t us=0;us<5000000;us+=100) {
            // Legacy GetTickCount64: 15.6ms steps, independently jittered sleeps.
            uint64_t time=10000+(legacy?(us/15600*15600):us)/1000;
            for(unsigned side=0;side<2;++side) {
                if(us>=frame[side]) {
                    p[side].local=mod::trajectory(time,side);p[side].sampled=time;
                    if(!legacy)cores[side]->frame(time);
                    if(us>1000000 && p[side].remote.visible) {
                        auto truth=mod::trajectory(10000+us/1000-cores[side]->presentationDelayMs(),1-side);double error=0;
                        for(unsigned k=0;k<3;++k)error+=std::pow(p[side].remote.state.position[k]-truth.position[k],2);
                        errors.push_back(std::sqrt(error));
                    }
                    frame[side]=legacy?us+15600+(rng()%5000):mod::nextDeadline(frame[side],us,10000)+rng()%1500;
                }
                if(us>=network[side]) {
                    for(size_t i=0;i<queue.size();) {
                        if(queue[i].side==side && queue[i].due<=us) {
                            auto d=queue[i];queue.erase(queue.begin()+i);
                            auto e=cores[side]->receive(d.bytes.data(),d.bytes.size(),time);
                            if(d.bytes[6]==uint8_t(ds::Kind::Pong) && e==ds::Error::None && !firstPong[side])firstPong[side]=us;
                        } else ++i;
                    }
                    cores[side]->tick(time);if(legacy)cores[side]->frame(time);
                    for(auto& bytes:w[side].out) {
                        // Lose first three pings: retry oracle cannot pass accidentally.
                        if(bytes[6]==uint8_t(ds::Kind::Ping) && ++pings[side]<=3)continue;
                        auto delay=bytes[6]==uint8_t(ds::Kind::Presence)?15000+rng()%50001:500+rng()%1500;
                        queue.push_back({us+delay,1-side,bytes});
                    }
                    w[side].out.clear();
                    network[side]=legacy?us+15600+rng()%5000:mod::nextDeadline(network[side],us,2000)+rng()%1500;
                }
            }
        }
        std::sort(errors.begin(),errors.end());if(errors.empty())return 2;
        worst95=std::max(worst95,errors[size_t((errors.size()-1)*.95)]);
        worst99=std::max(worst99,errors[size_t((errors.size()-1)*.99)]);
        for(auto t:firstPong) {if(!t)return 2;worstPong=std::max(worstPong,t);}
    }
    std::cout<<"fake clock seeds=1..100 worst p95="<<worst95<<" p99="<<worst99<<" first_pong_ms="<<worstPong/1000.<<'\n';
    if(worstPong>=1000000) {std::cerr<<"FAIL early_ping_retry\n";return 1;}
    if(worst95>=.30 || worst99>=.45) {std::cerr<<"FAIL timing_error_bounds\n";return 1;}
    std::cout<<"timing regression passed\n";
}
