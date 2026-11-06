// Two simulated worlds, each with the real cargo::Sync, connected the way mod.cpp connects them (placements create
// the item on the other side with an expectation; "taken" deletes the other side's copy by record index). Players
// put cargo down near themselves and pick up cargo near them, in random order, with events delivered after a random
// delay (reliable and in order, like the core's cargo channel). After each quiet period both worlds must hold the
// same cargo on the ground (by item and position), and nothing may be on the ground in one world while carried in
// the other. Seeded; part of make test.
#include "cargo.h"
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <vector>
using namespace mod::cargo;
unsigned failures=0;
struct Event{int type;uint32_t hash;std::array<double,3> at;uint64_t ref;uint64_t due;};
struct World{
    Sync sync;std::map<uint32_t,Record> records;uint32_t next=1;std::array<double,3> porter{0,0,100};
    std::deque<Event> out;std::vector<std::pair<uint32_t,std::array<double,3>>> pendingCreate;
    std::vector<Record> list() const {std::vector<Record> v;for(auto& [k,r]:records){(void)k;v.push_back(r);}return v;}
    void create(uint32_t hash,const std::array<double,3>& at){Record r;r.slot=next++;r.index=r.slot;r.hash=hash;r.state=InWorld<<8;r.position=at;records[r.slot]=r;}
};
static bool near(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[1]-b[1])<0.01;}
static bool trace=false;
int main(){
    unsigned bad=0;
    for(unsigned seed=1;seed<=200;++seed){trace=false;
        std::mt19937 rng(seed);World w[2];w[0].sync.setIdBase(1ull<<40);w[1].sync.setIdBase(2ull<<40);w[1].porter={4,0,100};
        uint64_t now=0;bool primed=false;
        auto settle=[&](int rounds){for(int i=0;i<rounds;++i){now+=250;
            for(int s=0;s<2;++s){auto& me=w[s];auto& other=w[1-s];
                // deliver due events to `other`
                while(!me.out.empty()&&me.out.front().due<=now){auto e=me.out.front();me.out.pop_front();
                    if(trace&&(e.hash==11||e.type==2))std::fprintf(stderr,"t%llu deliver %d->%d type %d h%u ref %llx at %.0f,%.0f\n",(unsigned long long)now,s,1-s,e.type,e.hash,(unsigned long long)e.ref,e.at[0],e.at[1]);
                    if(e.type==1){other.sync.expect({e.hash,e.at,e.ref},now);other.create(e.hash,e.at);}
                    else{auto idx=other.sync.partnerTook(e.ref);if(trace)std::fprintf(stderr,"   took -> %s\n",idx?"removed":"nothing");if(idx)other.records.erase(*idx);}}
            }
            for(int s=0;s<2;++s){auto& me=w[s];
                for(auto& p:me.sync.observe(me.list(),me.porter,now))me.out.push_back({1,p.hash,p.position,p.id,now+250+rng()%1000});
                for(auto idx:me.sync.takeDoomed())me.records.erase(idx);
                for(auto id:me.sync.takeTaken()){if(trace)std::fprintf(stderr,"t%llu w%d took %llx\n",(unsigned long long)now,s,(unsigned long long)id);me.out.push_back({2,0,{},id,now+250+rng()%1000});}}
            primed=true;}};
        settle(1);
        for(int step=0;step<120;++step){
            int s=int(rng()%2);auto& me=w[s];int a=int(rng()%4);
            if(a==0){std::array<double,3> at{me.porter[0]+double(rng()%3),me.porter[1]+double(rng()%3),100};me.create(uint32_t(10+rng()%3),at);}   // put down from the menu
            else if(a==1){for(auto& [k,r]:me.records)if((r.state>>8)==InWorld&&std::hypot(r.position[0]-me.porter[0],r.position[1]-me.porter[1])<3){if(trace)std::fprintf(stderr,"t%llu w%d pick slot %u h%u at %.0f,%.0f\n",(unsigned long long)now,s,k,r.hash,r.position[0],r.position[1]);r.state=OnPorter<<8;r.position=me.porter;break;}}   // pick up
            else if(a==2){for(auto& [k,r]:me.records)if((r.state>>8)==OnPorter){r.state=InWorld<<8;r.position={me.porter[0]+1,me.porter[1],100};break;}}   // put down what it carries
            else {me.porter[0]+=double(int(rng()%5)-2);me.porter[1]+=double(int(rng()%5)-2);}
            settle(1+int(rng()%3));
            if(step%20==19){
                settle(40);   // quiet: everything delivered and settled
                auto ground=[&](int s){std::vector<std::pair<uint32_t,std::array<double,3>>> g;for(auto& [k,r]:w[s].records)if((r.state>>8)==InWorld)g.push_back({r.hash,r.position});return g;};
                auto g0=ground(0),g1=ground(1);bool same=g0.size()==g1.size();
                for(auto& a:g0){bool found=false;for(auto& b:g1)if(a.first==b.first&&near(a.second,b.second))found=true;same=same&&found;}
                if(!same){++bad;std::fprintf(stderr,"world oracle: worlds differ (seed %u step %d: %zu vs %zu on the ground)\n",seed,step,g0.size(),g1.size());
                    for(int s=0;s<2;++s){std::fprintf(stderr," world %d porter %.0f,%.0f:",s,w[s].porter[0],w[s].porter[1]);
                        for(auto& [k,r]:w[s].records)std::fprintf(stderr," [%u h%u %s %.0f,%.0f]",k,r.hash,(r.state>>8)==InWorld?"G":"C",r.position[0],r.position[1]);std::fprintf(stderr,"\n");}
                    break;}
            }
        }
        (void)primed;if(bad)break;
    }
    failures=bad;
    if(!failures)std::puts("two-world cargo fuzz: 200 seeds x 120 steps, worlds agree after every quiet period");
    return failures?1:0;
}
