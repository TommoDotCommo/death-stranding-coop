// Fights with visitors (fights.h), game-free: every player a Fights over a broadcast client and a Sandbox (a game
// without the game: a catcher manager, gazers, a porter), through the players' own server (world/relay.h) in memory,
// each on a clock of its own. One owner and up to nine visitors (ten Sams), a tenth refused, a player with join_fights
// off, a player beyond the fight; damage both ways as one health bar on the owner's terms, the last hit, the owner
// leaving, a visitor whose own fight comes first, a visitor that also hosts. Every protection has a mutant
// (MUTANT_FIGHT_*; mod/Makefile) that this test must fail.
#include "fights.h"
#include "../world/relay.h"
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
using namespace mod;namespace bc=ds::broadcast;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"fights oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
const std::array<uint8_t,16> Build{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
struct Player {
    uint64_t id=0;world::RelayAddress address;uint64_t offset=0;std::array<double,3> at{};bool gone=false,postsLost=false;   // postsLost: its POSTs never arrive
    std::unique_ptr<bc::Client> client;fights::Sandbox game;std::unique_ptr<fights::Fights> fights;
    std::vector<std::vector<float>> livesSeen;   // this game's catchers at every look while a fight runs
    bool rose=false;                              // a catcher's life here ever went up during one fight
};
struct Sim {
    struct Datagram {bool up=false;world::RelayAddress address;std::vector<uint8_t> bytes;};
    uint64_t now=100000;std::deque<Datagram> queue;std::vector<std::unique_ptr<Player>> players;
    static world::RelayConfig relayConfig(){world::RelayConfig c;c.token="k";c.seed=7;c.maxPlayers=64;return c;}
    world::Relay relay{relayConfig(),[this](const world::RelayAddress& to,const uint8_t* d,size_t n){queue.push_back({false,to,{d,d+n}});}};
    uint64_t clock(const Player& p) const {return now+p.offset;}
    Player& add(uint64_t id,std::array<double,3> at,bool host,bool join){
        auto p=std::make_unique<Player>();p->id=id;p->at=at;p->offset=id*7919;p->address={0x7f000001,uint16_t(40000+players.size())};
        auto address=p->address;
        bc::ClientConfig c;c.token="k";c.nickname="Porter "+std::to_string(id);c.id=id;c.build=Build;
        p->client=std::make_unique<bc::Client>(c,[this,address](const uint8_t* d,size_t n){queue.push_back({true,address,{d,d+n}});});
        fights::Settings s;s.host=host;s.join=join;
        p->fights=std::make_unique<fights::Fights>(s,p->game.api(),[](const std::string&){});
        players.push_back(std::move(p));return *players.back();}
    Player& at(uint64_t id){for(auto& p:players)if(p->id==id)return *p;std::abort();}
    void step(){
        now+=20;
        for(auto& p:players){
            if(p->gone)continue;
            auto t=clock(*p);p->client->setPosition(p->at);p->client->tick(t);p->client->takeEvents();
            auto before=p->game.lives();bool was=p->game.active();
            p->fights->tick(p->game.local(p->at),*p->client,t);
            if(p->game.active()){
                if(was&&before.size()==p->game.lives().size())for(size_t i=0;i<before.size();++i)if(p->game.lives()[i]>before[i]+0.001f)p->rose=true;
                p->livesSeen.push_back(p->game.lives());}
        }
        size_t due=queue.size();
        for(size_t i=0;i<due;++i){
            auto d=std::move(queue.front());queue.pop_front();
            if(d.up){
                bool lost=false;for(auto& p:players)lost=lost||(p->address==d.address&&p->postsLost&&d.bytes.size()>6&&d.bytes[6]==uint8_t(bc::Type::Post));
                if(!lost)relay.receive(d.address,d.bytes.data(),d.bytes.size(),now);
                continue;}
            for(auto& p:players)if(!p->gone&&p->address==d.address)p->client->receive(d.bytes.data(),d.bytes.size(),clock(*p));}
        relay.tick(now);
    }
    void run(uint64_t ms){for(uint64_t t=0;t<ms;t+=20)step();}
    // Hits land between looks, as the game's do.
    void hit(Player& p,float percent){p.game.hit(0,percent);}
};
float life(const Player& p){return p.game.lives().empty()?-1.f:p.game.lives()[0];}
bool near(float a,float b){return std::fabs(a-b)<=0.05f;}
}

// Ten Sams: one owner, nine visitors; a tenth player refused for the cap, one with join_fights off, one beyond the fight.
static void ten(){
    Sim s;auto& o=s.add(1,{0,0,0},true,false);
    std::vector<Player*> v;for(uint64_t i=0;i<10;++i)v.push_back(&s.add(10+i,{double(i)*2,0,5},false,true));
    auto& off=s.add(30,{3,0,3},false,false);auto& far=s.add(31,{320,0,0},false,true);
    // Gazers: the same in every world (their locators' UUIDs are).
    for(auto& p:s.players)for(uint64_t k:{0x501ull,0x502ull})p->game.gazer(k,300);
    s.run(1500);
    // A fight starts in the owner's game: announced, offered, joined by nine.
    o.game.start(1);s.run(1500);
    CHECK(o.fights->role()==fights::Role::Owner&&s.relay.fightOf(1)!=0);
    size_t joined=0,refused=0;
    for(auto* p:v){if(p->fights->joined())++joined;else if(p->fights->role()==fights::Role::None)++refused;}
    CHECK(joined==9&&refused==1&&s.relay.visitorsOf(1)==9);
    // Each visitor shows it: its own catcher fight started by the game's command, at the owner's life.
    for(auto* p:v){
        if(!p->fights->joined()){CHECK(p->game.commands.empty()&&!p->game.active());continue;}
        CHECK(p->game.commands==std::vector<uint8_t>{0}&&p->game.active()&&near(life(*p),100));}
    // Nobody else is asked anything: the player with join_fights off, the one beyond the fight, the owner.
    CHECK(off.game.commands.empty()&&off.fights->counters().statesTaken==0&&far.game.commands.empty()&&far.fights->role()==fights::Role::None);
    CHECK(s.relay.visiting(30)==0&&s.relay.visiting(31)==0&&o.game.commands.empty());
    // One health bar on the owner's terms: a visitor's hit is taken off the owner's catcher, and every visitor follows;
    // the owner's own hit too. Nothing is counted twice: the owner's life is 100 less the real hits.
    Player* first=nullptr;for(auto* p:v)if(p->fights->joined()){first=p;break;}
    s.hit(*first,10);s.run(1500);
    CHECK(near(life(o),90));for(auto* p:v)if(p->fights->joined())CHECK(near(life(*p),90));
    s.hit(o,20);s.run(1500);
    CHECK(near(life(o),70));for(auto* p:v)if(p->fights->joined())CHECK(near(life(*p),70));
    // Three visitors hit at once.
    int hitters=0;for(auto* p:v)if(p->fights->joined()&&p!=first&&hitters<3){s.hit(*p,5);++hitters;}
    s.run(2000);CHECK(near(life(o),55));for(auto* p:v)if(p->fights->joined())CHECK(near(life(*p),55));
    // A visitor's reports lost for a while: the owner's states, higher than his copy, never heal it; his damage arrives
    // with the next report that does (they are counted from the start of his visit).
    {Player* lossy=nullptr;for(auto* p:v)if(p->fights->joined()&&p!=first&&!lossy)lossy=p;
     CHECK(lossy);if(!lossy)return;
     lossy->postsLost=true;s.hit(*lossy,4);s.run(2000);CHECK(near(life(*lossy),51)&&near(life(o),55));
     lossy->postsLost=false;s.run(2000);CHECK(near(life(o),51));for(auto* p:v)if(p->fights->joined())CHECK(near(life(*p),51));}
    // A visitor's life never went up (the owner's older state never heals it), and nothing was written as zero.
    for(auto* p:v)CHECK(!p->rose&&p->game.unkillable==0);
    // A gazer destroyed in a visitor's world is destroyed in the owner's; in no other visitor's.
    first->game.kill(0x501);s.run(1500);
    CHECK(o.game.life(0x501)==0&&o.game.destroyed==std::vector<uint64_t>{0x501});
    for(auto* p:v)if(p!=first)CHECK(p->game.life(0x501)==300&&p->game.destroyed.empty());
    // A visitor's report reaches the owner only: no visitor took any.
    for(auto* p:v)CHECK(p->fights->counters().reportsTaken==0);
    CHECK(o.fights->counters().reportsTaken>0&&o.fights->counters().statesTaken==0);
    // A kill claim while the owner's catcher is far from the floor: refused, the fight goes on.
    {fights::Report r;r.killed=true;   // a kill, and no damage
     Player* liar=nullptr;for(auto* p:v)if(p->fights->joined()&&p!=first&&!liar)liar=p;
     CHECK(liar);if(!liar)return;
     s.run(100);liar->client->postFight(fights::encode(r));s.run(500);
     CHECK(o.game.active()&&o.fights->counters().killsRefused==1&&o.fights->counters().killsTaken==0&&near(life(o),51));}
    // The last hit: visitors take the owner's catcher to the floor; one visitor's own hit kills his copy; the owner takes
    // the kill (its catcher is at the floor) and ends its fight with the area cleared; every visitor's copy ends the same
    // way; the visits end; the owner closes.
    s.hit(*first,50.9f);s.run(1500);
    CHECK(near(life(o),bts::HealthSync::Floor)&&o.game.active());
    for(auto* p:v)if(p->fights->joined())CHECK(near(life(*p),bts::HealthSync::Floor));
    Player* last=nullptr;for(auto* p:v)if(p->fights->joined()&&p!=first)last=p;
    CHECK(last);if(!last)return;
    s.hit(*last,1);CHECK(!last->game.active()&&last->game.cleared()==1);
    s.run(3000);
    CHECK(!o.game.active()&&o.game.cleared()==1&&o.game.commands==std::vector<uint8_t>{1}&&o.fights->counters().killsTaken==1);
    for(auto* p:v){
        if(p==last){CHECK(p->game.commands==std::vector<uint8_t>{0}&&p->game.cleared()==1);continue;}
        if(p->game.commands.empty())continue;   // the one refused for the cap
        CHECK(p->game.commands==(std::vector<uint8_t>{0,1})&&!p->game.active()&&p->game.cleared()==1);}
    for(auto* p:v)CHECK(p->fights->role()==fights::Role::None&&s.relay.visiting(p->id)==0);
    CHECK(o.fights->role()==fights::Role::None&&s.relay.fightOf(1)==0);
    // The worlds that took no part are as they were.
    CHECK(off.game.writes==0&&off.game.cleared()==0&&far.game.writes==0&&far.game.cleared()==0);
}

// The owner leaves in the middle of a fight: every visitor is released and nothing more is written in his world; his
// copy is his game's fight now, which he leaves as in the game; his own world keeps everything it had.
static void ownerLeaves(){
    Sim s;auto& o=s.add(1,{0,0,0},true,false);auto& a=s.add(2,{5,0,0},false,true);auto& b=s.add(3,{8,0,0},true,true);
    for(auto& p:s.players)p->game.gazer(0x700,300);
    s.run(1500);o.game.start(1);s.run(1500);
    CHECK(a.fights->joined()&&b.fights->joined()&&a.game.active()&&b.game.active());
    // A visitor that also hosts never announces the fight it shows.
    CHECK(s.relay.fightOf(3)==0&&b.fights->role()==fights::Role::Visitor&&b.fights->counters().hosted==0);
    s.hit(o,30);s.run(1500);CHECK(near(life(a),70)&&near(life(b),70));
    a.game.kill(0x700);s.run(1000);CHECK(o.game.life(0x700)==0);
    auto writes=a.game.writes;auto cleared=a.game.cleared();
    // The owner's game goes away (BYE).
    o.client->bye();o.gone=true;s.run(1500);
    for(auto* p:{&a,&b}){
        CHECK(p->fights->role()==fights::Role::None&&!p->fights->joined()&&s.relay.visiting(p->id)==0);
        CHECK(p->game.commands==std::vector<uint8_t>{0}&&p->game.active());   // no command to end it: the copy is his game's fight now
        CHECK(p->game.unkillable==0&&p->game.destroyed.empty());}
    CHECK(a.game.writes==writes&&a.game.cleared()==cleared&&b.game.life(0x700)==300);
    // His own hits now are his own: not reported, nothing written by the mod.
    s.hit(a,10);s.run(1000);CHECK(near(life(a),60)&&a.game.writes==writes);
    // The copy is never announced, even after the visit, by the visitor that hosts.
    s.run(1000);CHECK(s.relay.fightOf(3)==0&&b.fights->counters().hosted==0);
    // He gets away (the fight ends without a clear); the next fight of his own is his and is announced.
    b.game.end(false);s.run(500);b.game.start(1);s.run(1500);CHECK(s.relay.fightOf(3)!=0&&b.fights->role()==fights::Role::Owner);
    // And a visitor released is free to join another fight: a's game joins b's.
    a.game.end(false);s.run(3000);CHECK(a.fights->joined()&&s.relay.visiting(2)==3);
}

// A game that runs a fight of its own takes no offer: its own comes first, and nothing is written to it.
static void ownFirst(){
    Sim s;auto& o=s.add(1,{0,0,0},true,false);auto& a=s.add(2,{5,0,0},false,true);
    s.run(1500);a.game.start(1);s.hit(a,25);s.run(500);o.game.start(1);s.run(2500);
    CHECK(o.fights->role()==fights::Role::Owner&&a.fights->role()==fights::Role::None&&s.relay.visiting(2)==0);
    CHECK(a.fights->counters().joined==0&&a.fights->counters().ownFirst>=1&&a.game.commands.empty()&&a.game.writes==0&&near(life(a),75));
    // Its own fight over, it joins.
    a.game.end(true);s.run(2500);CHECK(a.fights->joined()&&a.game.commands==std::vector<uint8_t>{0});
}

// The wire formats: whole or nothing.
static void formats(){
    fights::State st;st.phase=fights::Phase::Cleared;st.lives={100,0.05f,55.5f};
    auto b=fights::encode(st);auto d=fights::decodeState(b);
    CHECK(b.size()==10&&d&&d->phase==fights::Phase::Cleared&&d->lives.size()==3&&near(d->lives[1],0.05f)&&near(d->lives[2],55.5f));
    for(size_t n=0;n<b.size();++n)CHECK(!fights::decodeState({b.begin(),b.begin()+long(n)}));
    {auto x=b;x[1]=4;CHECK(!fights::decodeState(x));x=b;x[3]=1;CHECK(!fights::decodeState(x));x=b;x[4]=0xff;x[5]=0xff;CHECK(!fights::decodeState(x));}
    fights::Report r;r.killed=true;r.caught=2;r.damage={10.5f,0};r.gazers={0x501,(uint64_t(1)<<52)-1};
    auto c=fights::encode(r);auto e=fights::decodeReport(c);
    CHECK(c.size()==8+8+16&&e&&e->killed&&e->caught==2&&near(e->damage[0],10.5f)&&e->gazers==r.gazers);
    for(size_t n=0;n<c.size();++n)CHECK(!fights::decodeReport({c.begin(),c.begin()+long(n)}));
    {auto x=c;x[1]=2;CHECK(!fights::decodeReport(x));x=c;x[6]=1;CHECK(!fights::decodeReport(x));x=c;x.back()=0x10;CHECK(!fights::decodeReport(x));}
    r.gazers.resize(fights::MaxGazers+1);CHECK(fights::encode(r).empty());
}

// The floor: an owner's catcher left at almost nothing by its own hit is shown at the floor, never at zero (a catcher
// written to zero cannot die).
static void floor(){
    Sim s;auto& o=s.add(1,{0,0,0},true,false);auto& a=s.add(2,{5,0,0},false,true);
    s.run(1500);o.game.start(1);s.run(1500);CHECK(a.fights->joined());
    s.hit(o,99.996f);s.run(1000);CHECK(o.game.active()&&life(o)<0.01f);
    CHECK(near(life(a),bts::HealthSync::Floor)&&a.game.unkillable==0);
}

// A visitor who goes out of reach is released; back in reach (his copy over, after the quiet time) he joins the same
// fight again: what he did before is not taken again, what he does now is.
static void rejoin(){
    Sim s;auto& o=s.add(1,{0,0,0},true,false);auto& a=s.add(2,{5,0,0},false,true);
    s.run(1500);o.game.start(1);s.run(1500);CHECK(a.fights->joined());
    s.hit(a,10);s.run(1500);CHECK(near(life(o),90));
    a.at={400,0,0};s.run(1500);CHECK(a.fights->role()==fights::Role::None&&s.relay.visiting(2)==0&&a.game.active());
    a.game.end(false);a.at={5,0,0};s.run(3000);CHECK(!a.fights->joined());   // not at once
    s.run(9000);CHECK(a.fights->joined()&&a.game.commands==(std::vector<uint8_t>{0,0})&&near(life(a),90));
    s.hit(a,5);s.run(1500);CHECK(near(life(o),85)&&near(life(a),85));
}

int main(){
    formats();ten();ownerLeaves();ownFirst();floor();rejoin();
    if(failures)return 1;
    std::puts("fights ok");return 0;
}
