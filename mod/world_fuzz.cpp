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
    // A put-down from the cargo menu: the record is in the world at once but keeps its carry-frame position until the game
    // writes the real place (live 2026-11-07). (slot, real place, when)
    struct Landing{uint32_t slot;std::array<double,3> at;uint64_t when;};std::vector<Landing> landing;
    unsigned story=0;
    // The cargo menu is open: the items next to the porter are staged (location 2, carry frame) and put back when it
    // closes, maybe minus one he takes; he cannot move meanwhile (live 2026-11-07).
    bool browsing=false;uint64_t browseUntil=0;std::vector<std::pair<uint32_t,std::array<double,3>>> browsed;   // story items (kind 5) made here: only these may exist here, and none in the other world
    std::vector<Record> list() const {std::vector<Record> v;for(auto& [k,r]:records){(void)k;v.push_back(r);}return v;}
    void create(uint32_t hash,const std::array<double,3>& at){Record r;r.slot=next++;r.index=r.slot;r.hash=hash;r.state=InWorld<<8|SharedKind;r.position=at;records[r.slot]=r;}
};
static bool near(const std::array<double,3>& a,const std::array<double,3>& b){return std::hypot(a[0]-b[0],a[1]-b[1])<0.01;}
static bool trace=false;
int main(){
    unsigned bad=0;
    for(int restarts=0;restarts<2&&!bad;++restarts)for(unsigned seed=1;seed<=200;++seed){trace=false;
        std::mt19937 rng(seed);World w[2];w[0].sync.setIdBase(1ull<<40);w[1].sync.setIdBase(2ull<<40);w[1].porter={4,0,100};
        uint64_t now=0;bool primed=false;
        auto settle=[&](int rounds){for(int i=0;i<rounds;++i){now+=250;
            for(auto& me:w)if(me.browsing&&now>=me.browseUntil){
                size_t keep=rng()%3==0?rng()%std::max<size_t>(1,me.browsed.size()):me.browsed.size();   // sometimes he takes one
                for(size_t j=0;j<me.browsed.size();++j){auto r=me.records.find(me.browsed[j].first);if(r==me.records.end()||j==keep)continue;
                    r->second.state=InWorld<<8|(r->second.state&0xff);me.landing.push_back({r->first,me.browsed[j].second,now+250*(rng()%6)});}
                me.browsed.clear();me.browsing=false;}
            for(auto& me:w)for(auto it=me.landing.begin();it!=me.landing.end();){
                auto r=me.records.find(it->slot);if(r==me.records.end()){it=me.landing.erase(it);continue;}
                if(it->when<=now){r->second.position=it->at;it=me.landing.erase(it);}else ++it;}
            for(int s=0;s<2;++s){auto& me=w[s];auto& other=w[1-s];
                // deliver due events to `other`
                while(!me.out.empty()&&me.out.front().due<=now){auto e=me.out.front();me.out.pop_front();
                    if(trace)std::fprintf(stderr,"t%llu deliver %d->%d type %d h%u ref %llx at %.0f,%.0f\n",(unsigned long long)now,s,1-s,e.type,e.hash,(unsigned long long)e.ref,e.at[0],e.at[1]);
                    if(e.type==1){other.sync.expect({e.hash,e.at,e.ref},now);other.create(e.hash,e.at);}
                    else{auto idx=other.sync.partnerTook(e.ref,e.hash,e.at);if(trace)std::fprintf(stderr,"   took -> %s\n",idx?"removed":"nothing");if(idx)other.records.erase(*idx);}}
            }
            for(int s=0;s<2;++s){auto& me=w[s];
                for(auto& p:me.sync.observe(me.list(),me.porter,now)){if(trace)std::fprintf(stderr,"t%llu w%d placed h%u at %.0f,%.0f id %llx\n",(unsigned long long)now,s,p.hash,p.position[0],p.position[1],(unsigned long long)p.id);me.out.push_back({1,p.hash,p.position,p.id,now+250+rng()%1000});}
                for(auto idx:me.sync.takeDoomed())me.records.erase(idx);
                for(auto tk:me.sync.takeTaken()){if(trace)std::fprintf(stderr,"t%llu w%d took id %llx h%u at %.0f,%.0f\n",(unsigned long long)now,s,(unsigned long long)tk.id,tk.hash,tk.position[0],tk.position[1]);me.out.push_back({2,tk.hash,tk.position,me.sync.wireRef(tk),now+250+rng()%1000});}}
            primed=true;}};
        settle(1);
        for(int step=0;step<120;++step){
            int s=int(rng()%2);auto& me=w[s];int a=me.browsing?-1:int(rng()%6);
            if(a==-1){}   // in the cargo menu: nothing else
            else if(a==5){   // an item on the ground slides or floats away (slope, river): only in this world
                std::vector<uint32_t> g;for(auto& [k,r]:me.records)if((r.state>>8)==InWorld&&kind(r)==SharedKind)g.push_back(k);
                if(!g.empty()){auto& r=me.records[g[rng()%g.size()]];r.position[0]+=2+double(rng()%4);if(trace)std::fprintf(stderr,"t%llu w%d slide h%u to %.0f,%.0f\n",(unsigned long long)now,s,r.hash,r.position[0],r.position[1]);}}
            else if(a==4){me.browsing=true;me.browseUntil=now+250*(1+rng()%30);
                for(auto& [k,r]:me.records)if((r.state>>8)==InWorld&&std::hypot(r.position[0]-me.porter[0],r.position[1]-me.porter[1])<3){
                    me.browsed.push_back({k,r.position});r.state=OnPorter<<8|(r.state&0xff);r.position={99.6,100.7,20};}}
            else if(a==0){std::array<double,3> at{me.porter[0]+double(rng()%3),me.porter[1]+double(rng()%3),100};uint32_t h=uint32_t(10+rng()%3);bool story=rng()%8==0;if(story)h=99;if(trace)std::fprintf(stderr,"t%llu w%d menu-drop h%u at %.0f,%.0f\n",(unsigned long long)now,s,h,at[0],at[1]);me.create(h,at);if(story){me.records[me.next-1].state=InWorld<<8|5;++me.story;}}   // put down from the menu (sometimes story cargo, kind 5)
            else if(a==1){for(auto& [k,r]:me.records)if((r.state>>8)==InWorld&&std::hypot(r.position[0]-me.porter[0],r.position[1]-me.porter[1])<3){if(trace)std::fprintf(stderr,"t%llu w%d pick slot %u h%u at %.0f,%.0f\n",(unsigned long long)now,s,k,r.hash,r.position[0],r.position[1]);r.state=OnPorter<<8|(r.state&0xff);r.position={100+double(rng()%100)/100,100,20};break;}}   // pick up (the record moves to the carry frame)
            else if(a==2){for(auto& [k,r]:me.records)if((r.state>>8)==OnPorter){r.state=InWorld<<8|(r.state&0xff);me.landing.push_back({k,{me.porter[0]+1,me.porter[1],100},now+250*(rng()%12)});if(trace)std::fprintf(stderr,"t%llu w%d putdown slot %u h%u at %.0f,%.0f\n",(unsigned long long)now,s,k,r.hash,r.position[0],r.position[1]);break;}}   // put down what it carries
            else {me.porter[0]+=double(int(rng()%5)-2);me.porter[1]+=double(int(rng()%5)-2);}
            settle(1+int(rng()%3));
            if(restarts&&!w[0].browsing&&!w[1].browsing&&rng()%40==0){for(auto& me:w)me.porter[0]+=1;/* walked on: nothing pending */settle(40);int r=int(rng()%2);if(trace)std::fprintf(stderr,"t%llu RESTART w%d\n",(unsigned long long)now,r);w[r].sync=Sync();w[r].sync.setIdBase((3ull+step)<<40);settle(1);}   // a mod restarts
            if(step%20==19){
                settle(40);   // menus closed
                for(auto& me:w)me.porter[0]+=2;   // both walk on (a pick-up is reported once the porter moves)
                settle(40);   // quiet: everything delivered and settled
                auto ground=[&](int s){std::vector<std::pair<uint32_t,std::array<double,3>>> g;for(auto& [k,r]:w[s].records)if((r.state>>8)==InWorld&&kind(r)==SharedKind)g.push_back({r.hash,r.position});return g;};
                for(int s=0;s<2;++s){unsigned n=0;for(auto& [k,r]:w[s].records)if(r.hash==99)++n;
                    if(n!=w[s].story){++bad;std::fprintf(stderr,"world oracle: story cargo crossed (seed %u step %d: world %d has %u of %u)\n",seed,step,s,n,w[s].story);}}
                if(bad)break;
                auto g0=ground(0),g1=ground(1);bool same=g0.size()==g1.size();
                for(auto& a:g0){bool found=false;for(auto& b:g1)if(a.first==b.first&&near(a.second,b.second))found=true;same=same&&found;}
                if(!same){++bad;std::fprintf(stderr,"world oracle: worlds differ (seed %u step %d restarts %d: %zu vs %zu on the ground)\n",seed,step,restarts,g0.size(),g1.size());
                    for(int s=0;s<2;++s){std::fprintf(stderr," world %d porter %.0f,%.0f:",s,w[s].porter[0],w[s].porter[1]);
                        for(auto& [k,r]:w[s].records){std::fprintf(stderr," [%u h%u %s %.0f,%.0f]",k,r.hash,(r.state>>8)==InWorld?"G":"C",r.position[0],r.position[1]);}std::fprintf(stderr,"\n");}
                    break;}
            }
        }
        (void)primed;if(bad)break;
    }
    failures=bad;
    if(!failures)std::puts("two-world cargo fuzz: 200 seeds x 120 steps, with and without restarts, worlds agree after every quiet period");
    return failures?1:0;
}
