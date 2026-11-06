// Two simulated worlds with the real structures::Sync and NetCopies, connected the way mod.cpp connects them: a
// built structure travels as (type, transform, ref); the receiver builds the Net variant unless one already stands
// there, and the game builds it after a random delay; a dismantle travels as "taken" and removes the copy once it
// stands. Porters build (built after a delay) and dismantle at random; events arrive after random delays, in order.
// After each quiet period, every structure a porter built that stands in one world has exactly one built Net copy at
// its place in the other, and no Net copy stands without its original. Seeded; part of make test.
#include "structures.h"
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
using namespace mod::structures;
struct Event{int type;uint32_t item;std::array<double,3> at;uint64_t ref;uint64_t due;};
struct Pending{Record r;uint64_t when;};
struct World{
    Sync sync;NetCopies copies;std::map<uint32_t,Record> table;uint32_t nextPlayer=10001,nextNet=100001;
    std::vector<Pending> building;std::deque<Event> out;
    std::vector<Record> list() const {std::vector<Record> v;for(auto& [k,r]:table){(void)k;v.push_back(r);}return v;}
};
static double dist(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[1]-b[1]);}
int main(){
    for(unsigned seed=1;seed<=200;++seed){
        std::mt19937 rng(seed);World w[2];uint64_t now=0;const uint64_t base[2]={1ull<<40,2ull<<40};unsigned spot=0;
        auto tick=[&](){now+=500;
            for(int s=0;s<2;++s){auto& me=w[s];auto& other=w[1-s];
                for(auto it=me.building.begin();it!=me.building.end();)if(it->when<=now){me.table[it->r.id]=it->r;it=me.building.erase(it);}else ++it;
                while(!me.out.empty()&&me.out.front().due<=now){auto e=me.out.front();me.out.pop_front();
                    if(e.type==3){auto b=unpack(e.item,e.at,0);auto net=netVariant(b.type);if(!net)continue;
                        other.copies.built(e.ref,*net,b.position);
                        if(standing(other.list(),*net,b.position))continue;
                        Record r;r.id=other.nextNet++;r.type=*net;r.state=2;r.at.position=b.position;r.at.rotation=rotation(0,0,0);
                        other.building.push_back({r,now+500+rng()%3000});}
                    else if(other.copies.known(e.ref))other.copies.dismantled(e.ref,now);}
            }
            for(int s=0;s<2;++s){auto& me=w[s];auto recs=me.list();
                for(auto& b:me.sync.observe(recs))me.out.push_back({3,pack(b),b.position,base[s]+b.id,now+500+rng()%2000});
                for(auto at:me.copies.due(recs,now))for(auto it=me.table.begin();it!=me.table.end();)
                    if(it->second.id>=NetFirst&&dist(it->second.at.position,at)<=1.5)it=me.table.erase(it);else ++it;
                for(auto id:me.sync.takeRemoved())me.out.push_back({2,0,{},base[s]+id,now+500+rng()%2000});}
        };
        tick();
        for(int step=0;step<150;++step){
            int s=int(rng()%2);auto& me=w[s];int a=int(rng()%3);
            if(a==0){Record r;r.id=me.nextPlayer++;r.type=uint8_t(rng()%2?25:28);r.state=2;   // build at a fresh spot
                ++spot;r.at.position={double(spot%50)*4,double(s*400+(spot/50)*4),100};r.at.rotation=rotation(0,0,0);me.building.push_back({r,now+500+rng()%2000});}
            else if(a==1){for(auto it=me.table.begin();it!=me.table.end();++it)if(it->second.id>=PlayerFirst&&it->second.id<NetFirst&&rng()%2){me.table.erase(it);break;}}   // dismantle own
            int quiet=1+int(rng()%3);for(int i=0;i<quiet;++i)tick();
            if(step%25==24){
                for(int i=0;i<80;++i)tick();   // quiet: everything delivered, built and cleared
                for(int s=0;s<2;++s){
                    for(auto& [k,r]:w[s].table){
                        if(r.id>=PlayerFirst&&r.id<NetFirst&&r.state==2){unsigned n=0;
                            for(auto& [k2,q]:w[1-s].table)if(q.id>=NetFirst&&q.state==2&&dist(q.at.position,r.at.position)<0.01)++n;
                            if(n!=1){std::fprintf(stderr,"structures oracle: original %u in world %d has %u copies (seed %u step %d)\n",r.id,s,n,seed,step);return 1;}}
                        if(r.id>=NetFirst){bool orig=false;for(auto& [k2,q]:w[1-s].table)if(q.id>=PlayerFirst&&q.id<NetFirst&&dist(q.at.position,r.at.position)<0.01)orig=true;
                            if(!orig){std::fprintf(stderr,"structures oracle: Net copy %u in world %d has no original (seed %u step %d)\n",r.id,s,seed,step);return 1;}}
                        (void)k;}
                }
            }
        }
    }
    std::puts("two-world structure fuzz: 200 seeds x 150 steps, copies match originals after every quiet period");
    return 0;
}
