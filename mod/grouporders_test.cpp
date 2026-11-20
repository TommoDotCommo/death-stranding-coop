// Orders taken together in group play (grouporders.h), without a game or a server: who leads, who joins and from
// where, what is said, and what is applied here, only to an order of the same party under way here.
#include "grouporders.h"
#include <cstdio>
using namespace mod::grouporders;
using mod::groupevents::Event;
namespace orders=mod::orders;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"grouporders oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
constexpr uint64_t M=0x36330,N=0x372d7;
static orders::Order order(uint64_t id,uint16_t state,uint8_t detail=2){orders::Order o;o.id=id;o.state=state;o.detail=detail;
    o.result=state==16?orders::Completed:state==orders::Progress?2:1;return o;}
static orders::Unit unit(uint64_t mission,uint8_t n,uint16_t state,uint32_t packed=0){orders::Unit u;u.mission=mission;u.unit=n;u.state=state;u.packed=packed;u.life[0]=0.5f;u.life[1]=1.f;return u;}
static Event start(int64_t party,uint64_t id,Place at,int64_t age=0){Event e{"order",int64_t(id),orders::Start,party,{at[0],at[1],at[2]},{}};e.age=age;return e;}
static Event end(int64_t party,uint64_t id,int64_t what){return Event{"order",int64_t(id),what,party,{},{}};}
static Event handed(int64_t party,uint64_t id,uint8_t n,std::vector<double> data){return Event{"unit",int64_t(orders::UnitSync::key(id,n)),1,party,{},std::move(data)};}
static size_t count(const Step& s,Request::What w){size_t n=0;for(auto& r:s.apply)if(r.what==w)++n;return n;}
static const Place Terminal{1000,2000,30};
// A porter at the terminal, the order available: he joins party 77 when he hears its start.
static Together joined(uint64_t& now){
    Together t(5);now=1000;
    t.step(now,{order(M,orders::Available)},{},Terminal);
    t.hear({start(77,M,{1005,2000,31},1)},now);
    auto s=t.step(now+=1000,{order(M,orders::Available)},{},Terminal);
    CHECK(s.apply.size()==1&&s.apply[0].what==Request::Start&&s.apply[0].mission==M&&s.apply[0].party==77&&s.say.empty());
    if(!s.apply.empty())t.done(s.apply[0],true);
    s=t.step(now+=1000,{order(M,orders::Progress)},{unit(M,0,orders::NotDelivered),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(s.say.empty()&&s.lines.size()==1&&s.lines[0]==Line::Joined&&t.member(M,77));   // a join is not a party of its own
    return t;
}
static void leader(){
    Together t(42);uint64_t now=1000;
    // An order under way at the first look starts no party; nor do orders that are not standard.
    auto s=t.step(now,{order(M,orders::Available),order(N,orders::Progress),order(7,orders::Available,0)},{},Terminal);
    CHECK(s.say.empty()&&t.parties()==0);
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress),order(7,orders::Progress,0)},{unit(M,0,orders::NotDelivered),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(s.say.size()==1&&s.apply.empty());
    int64_t party=0;
    if(s.say.size()==1){auto& e=s.say[0];party=e.party;
        CHECK(e.kind=="order"&&e.key==int64_t(M)&&e.value==orders::Start&&e.party>0&&e.party<=PartyMax&&e.place==std::vector<double>({Terminal[0],Terminal[1],Terminal[2]}));}
    // A unit he hands in is said to his party with what the hand-in left in the row.
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress)},{unit(M,0,orders::Delivered,0x10005),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(s.say.size()==1&&s.say[0]==(Event{"unit",int64_t(orders::UnitSync::key(M,0)),1,party,{},{double(0x10005),0.5,1.0}}));
    // A member hands in unit 1: marked here, not said back; a member's success completes it here, not said back either.
    t.hear({handed(party,M,1,{9,0.25,0.75})},now);
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress)},{unit(M,0,orders::Delivered),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(count(s,Request::Unit)==1&&s.say.empty()&&s.lines.size()==1&&s.lines[0]==Line::UnitByOther);
    if(count(s,Request::Unit)==1){auto& r=s.apply[0];CHECK(r.mission==M&&r.unit==1&&r.packed==9&&r.life[0]==0.25f&&r.life[1]==0.75f);}
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress)},{unit(M,0,orders::Delivered),unit(M,1,orders::Delivered)},Terminal);
    CHECK(s.say.empty()&&s.apply.empty());
    t.hear({end(party,M,orders::Success)},now);
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress)},{},Terminal);
    CHECK(count(s,Request::Success)==1&&s.apply[0].mission==M&&s.lines.size()==1&&s.lines[0]==Line::DoneByOther);
    s=t.step(now+=1000,{order(M,16),order(N,orders::Progress)},{},Terminal);
    CHECK(s.say.empty()&&t.parties()==0);
    // Taken again later: a new party, and its own success is said.
    s=t.step(now+=1000,{order(M,orders::Progress),order(N,orders::Progress)},{},std::nullopt);
    CHECK(s.say.size()==1&&s.say[0].party!=party&&s.say[0].place.empty());   // where unknown: nobody can join it
    const int64_t again=s.say.empty()?0:s.say[0].party;
    s=t.step(now+=1000,{order(M,16),order(N,orders::Progress)},{},Terminal);
    CHECK(s.say.size()==1&&s.say[0]==end(again,M,orders::Success));
    // The leader gives it up: said, so his party gives it up too.
    s=t.step(now+=1000,{order(M,orders::Progress)},{},Terminal);
    const int64_t third=s.say.empty()?0:s.say[0].party;
    s=t.step(now+=1000,{order(M,8)},{},Terminal);
    CHECK(s.say.size()==1&&s.say[0]==end(third,M,orders::Fail));
}
static void member(){
    uint64_t now=0;Together t=joined(now);
    // Unit 0 handed in by someone of party 77, unit 1 by someone of another party: only the first is his.
    t.hear({handed(77,M,0,{5,0.25,0.75}),handed(99,M,1,{5,0.25,0.75})},now);
    auto s=t.step(now+=1000,{order(M,orders::Progress)},{unit(M,0,orders::NotDelivered),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(count(s,Request::Unit)==1&&s.apply.size()==1&&s.apply[0].unit==0&&s.apply[0].packed==5&&s.apply[0].party==77);
    s=t.step(now+=1000,{order(M,orders::Progress)},{unit(M,0,orders::Delivered),unit(M,1,orders::NotDelivered)},Terminal);
    CHECK(s.say.empty());                                                                  // not said back
    // His own hand-in of unit 1 is said; another party's success does nothing; his party's does.
    s=t.step(now+=1000,{order(M,orders::Progress)},{unit(M,0,orders::Delivered),unit(M,1,orders::Delivered,3)},Terminal);
    CHECK(s.say.size()==1&&s.say[0].kind=="unit"&&s.say[0].party==77&&s.say[0].key==int64_t(orders::UnitSync::key(M,1)));
    t.hear({end(99,M,orders::Success),end(99,M,orders::Fail)},now);
    s=t.step(now+=1000,{order(M,orders::Progress)},{},Terminal);
    CHECK(s.apply.empty());
    t.hear({end(77,M,orders::Success)},now);
    s=t.step(now+=1000,{order(M,orders::Progress)},{},Terminal);
    CHECK(count(s,Request::Success)==1&&s.apply.size()==1);
    s=t.step(now+=1000,{order(M,16)},{},Terminal);
    CHECK(s.say.empty()&&t.parties()==0);
    // The same party's start heard again (a full read): not joined again.
    t.hear({start(77,M,{1005,2000,31},3)},now);
    s=t.step(now+=1000,{order(M,orders::Available)},{},Terminal);
    CHECK(s.apply.empty());
}
static void memberLeaves(){
    uint64_t now=0;Together t=joined(now);
    // He gives up: he leaves alone, nothing said; the start heard again does not take him back.
    auto s=t.step(now+=1000,{order(M,8)},{},Terminal);
    CHECK(s.say.empty()&&t.parties()==0);
    t.hear({start(77,M,{1005,2000,31},5)},now);
    s=t.step(now+=1000,{order(M,orders::Available)},{},Terminal);
    CHECK(s.apply.empty());
    // The leader's give-up heard: given up here only while he is in that party.
    Together u=joined(now);
    u.hear({end(77,M,orders::Fail)},now);
    s=u.step(now+=1000,{order(M,orders::Progress)},{},Terminal);
    CHECK(count(s,Request::Fail)==1&&s.lines.size()==1&&s.lines[0]==Line::GivenUpByLeader);
    s=u.step(now+=1000,{order(M,8)},{},Terminal);
    CHECK(s.say.empty());                                                                  // not said back
}
static void whoJoins(){
    auto tries=[](Place porter,int64_t age,uint16_t state,uint8_t detail=2,bool placed=true,bool known=true){
        Together t(9);uint64_t now=500000;
        t.step(now,{order(M,state,detail)},{},porter);
        Event e=start(77,M,Terminal,age);if(!placed)e.place.clear();
        t.hear({e},now);
        auto s=t.step(now+=1000,{order(M,state,detail)},{},known?std::optional<Place>(porter):std::nullopt);
        return count(s,Request::Start);};
    CHECK(tries({1000,2029,30},0,orders::Available)==1);                    // 29 m away
    CHECK(tries({1000,2031,30},0,orders::Available)==0);                    // 31 m away: not at that terminal
    CHECK(tries({1000,2000,61},0,orders::Available)==0);                    // 31 m above it
    CHECK(tries(Terminal,119,orders::Available)==1&&tries(Terminal,122,orders::Available)==0);   // two minutes
    CHECK(tries(Terminal,0,orders::Progress)==0&&tries(Terminal,0,16)==0&&tries(Terminal,0,64)==0);   // not available here
    CHECK(tries(Terminal,0,orders::Available,0)==0&&tries(Terminal,0,orders::Available,1)==0);       // the story: never
    CHECK(tries(Terminal,0,orders::Available,2,false)==0&&tries(Terminal,0,orders::Available,2,true,false)==0);   // where unknown
    // He walks up to the terminal a minute later: joined then.
    Together t(9);uint64_t now=1000;
    t.step(now,{order(M,orders::Available)},{},Place{0,0,0});
    t.hear({start(77,M,Terminal,0)},now);
    CHECK(t.step(now+=60000,{order(M,orders::Available)},{},Place{0,0,0}).apply.empty());
    CHECK(count(t.step(now+=1000,{order(M,orders::Available)},{},Terminal),Request::Start)==1);
    // An order already ended for that party is not joined.
    Together e(9);now=1000;
    e.step(now,{order(M,orders::Available)},{},Terminal);
    e.hear({start(77,M,Terminal,0),end(77,M,orders::Success)},now);
    CHECK(e.step(now+=1000,{order(M,orders::Available)},{},Terminal).apply.empty());
}
static void refusedAndStuck(){
    // The game refuses the start: not asked again.
    Together t(3);uint64_t now=1000;
    t.step(now,{order(M,orders::Available)},{},Terminal);
    t.hear({start(77,M,Terminal,0)},now);
    auto s=t.step(now+=1000,{order(M,orders::Available)},{},Terminal);
    CHECK(s.apply.size()==1);if(!s.apply.empty())t.done(s.apply[0],false);
    t.hear({start(77,M,Terminal,1)},now);
    CHECK(t.step(now+=1000,{order(M,orders::Available)},{},Terminal).apply.empty()&&t.parties()==0);
    // A start that never shows: the party given up after JoinSettleMs; its end later does nothing here.
    Together u(3);now=1000;
    u.step(now,{order(M,orders::Available)},{},Terminal);
    u.hear({start(77,M,Terminal,0)},now);
    s=u.step(now+=1000,{order(M,orders::Available)},{},Terminal);if(!s.apply.empty())u.done(s.apply[0],true);
    u.step(now+=JoinSettleMs,{order(M,orders::Available)},{},Terminal);
    CHECK(u.parties()==0);
    u.hear({end(77,M,orders::Success)},now);
    CHECK(u.step(now+=1000,{order(M,orders::Progress)},{},Terminal).apply.empty());
    // His own order under way with no party: another party's ends and units are not his.
    Together v(3);now=1000;
    v.step(now,{order(M,orders::Progress)},{unit(M,0,orders::NotDelivered)},Terminal);
    v.hear({end(77,M,orders::Success),handed(77,M,0,{1,1,1})},now);
    CHECK(v.step(now+=1000,{order(M,orders::Progress)},{unit(M,0,orders::NotDelivered)},Terminal).apply.empty());
}
static void load(){
    // A load: the parties are left, and an order under way after it starts none.
    uint64_t now=0;Together t=joined(now);
    t.reset();
    auto s=t.step(now+=1000,{order(M,orders::Progress)},{},Terminal);
    CHECK(s.say.empty()&&s.apply.empty()&&t.parties()==0);
    t.hear({end(77,M,orders::Success)},now);
    CHECK(t.step(now+=1000,{order(M,orders::Progress)},{},Terminal).apply.empty());
}
int main(){
    leader();member();memberLeaves();whoJoins();refusedAndStuck();load();
    if(failures){std::fprintf(stderr,"%u grouporders oracle failures\n",failures);return 1;}
    std::puts("grouporders oracles passed");return 0;
}
