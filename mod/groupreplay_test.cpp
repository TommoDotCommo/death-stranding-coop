// Boss replays played together (groupreplay.h), without a game: three players and a server that keeps one entry per
// kind and key and never hands a player his own. Who is started where, what is said, and the shared boss life.
#include "groupreplay.h"
#include <cstdio>
using namespace mod::groupreplay;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"groupreplay oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
namespace {
constexpr int64_t Whale=20130,Higgs=25130;
struct Player {
    explicit Player(bool join,uint16_t fight):g(Settings{join},[fight]{return fight;},nullptr){l.available=true;}
    Group g;Local l;Actions last;std::vector<Event> said;
};
// One tick for each player, then what each said goes to the others (as the server's read hands it).
void round(std::vector<Player*> ps,uint64_t now){
    for(auto* p:ps){p->last=p->g.tick(p->l,now);for(auto& e:p->last.say)p->said.push_back(e);}
    for(auto* from:ps)for(auto& e:from->last.say)for(auto* to:ps)if(to!=from)to->g.heard(e,now);
}
size_t count(const std::vector<Event>& v,const std::string& kind){size_t n=0;for(auto& e:v)n+=e.kind==kind;return n;}
}
int main(){
    // Keys and the replay missions.
    CHECK(replayMission(320)&&replayMission(25640)&&replayMission(Whale)&&!replayMission(321)&&!replayMission(-1));
    CHECK(startKey(Whale,7)>>16==Whale&&(startKey(Whale,7)&0xffff)==7&&lifeKey(25640,65535,7)<(int64_t(1)<<40)&&(lifeKey(Whale,9,3)&0xff)==3);
    CHECK(lifeKey(Whale,9,3)!=lifeKey(Whale,10,3)&&lifeKey(Whale,9,3)!=lifeKey(Whale,9,2));
    // Ann starts the whale's replay herself: said once (her fight number), with the menu code.
    Player ann(true,41),bob(true,52),cid(false,63);uint64_t t=1000;
    round({&ann,&bob,&cid},t);
    ann.l.inRoom=true;bob.l.inRoom=false;cid.l.inRoom=true;
    ann.l.running=true;ann.l.boss=true;ann.l.mission=Whale;ann.l.code=0x1f;ann.l.lives={100.f};t+=500;
    round({&ann,&bob,&cid},t);
    CHECK(count(ann.last.say,"replay")==1&&ann.last.say[0].key==startKey(Whale,41)&&ann.last.say[0].value==0x1f&&ann.g.fight()==41);
    // Bob is out of his private room: told, not started. Cid has not opted in: told only.
    t+=500;round({&ann,&bob,&cid},t);
    CHECK(!bob.last.start&&bob.last.notices.size()==1&&!cid.last.start&&cid.last.notices.size()==1);
    t+=500;round({&ann,&bob,&cid},t);
    CHECK(count(ann.last.say,"replay")==0&&bob.last.notices.empty()&&!bob.last.start);
    // Bob walks into his room: started on Ann's code, once.
    bob.l.inRoom=true;t+=1000;round({&ann,&bob,&cid},t);
    CHECK(bob.last.start&&*bob.last.start==0x1f&&!cid.last.start);
    t+=1000;round({&ann,&bob,&cid},t);CHECK(!bob.last.start);
    // His replay begins: the same mission, so the fight is Ann's; his start is never said.
    bob.l.running=true;bob.l.boss=true;bob.l.inRoom=false;bob.l.mission=Whale;bob.l.code=0x1f;bob.l.lives={100.f};
    ann.l.lives={80.f};t+=1000;round({&ann,&bob,&cid},t);
    CHECK(bob.g.fight()==41&&bob.g.mission()==Whale&&count(bob.said,"replay")==0&&bob.g.counters().joined==1);
    // The life: Ann's 80 % reaches Bob (taken down), Bob's hit to 60 % reaches Ann; never up.
    t+=1000;round({&ann,&bob,&cid},t);
    CHECK(bob.last.hurt.size()==1&&bob.last.hurt[0].part==0&&bob.last.hurt[0].percent==80.f);
    bob.l.lives={60.f};ann.l.lives={80.f};t+=1000;round({&ann,&bob,&cid},t);
    t+=1000;round({&ann,&bob,&cid},t);
    CHECK(ann.last.hurt.size()==1&&ann.last.hurt[0].percent==60.f);
    ann.l.lives={50.f};bob.l.lives={60.f};t+=1000;round({&ann,&bob,&cid},t);
    for(int i=0;i<6;++i){t+=1000;round({&ann,&bob,&cid},t);CHECK(ann.last.hurt.empty());}   // Bob's 60 never raises Ann's 50
    CHECK(bob.last.hurt.empty()||bob.last.hurt[0].percent==50.f);
    // Said again every RepeatMs while it runs, a change at most every SayEveryMs.
    {Player p(false,9);p.l.available=true;p.g.tick(p.l,0);p.l.running=true;p.l.mission=Whale;p.l.code=0x20;p.l.lives={90.f};
     auto a=p.g.tick(p.l,100);CHECK(count(a.say,"bosslife")==1);
     p.l.lives={89.f};CHECK(count(p.g.tick(p.l,200).say,"bosslife")==0&&count(p.g.tick(p.l,1100).say,"bosslife")==1);
     CHECK(count(p.g.tick(p.l,2000).say,"bosslife")==0&&count(p.g.tick(p.l,1100+RepeatMs).say,"bosslife")==1);}
    // Never zero: the lowest heard is 0.1 %, the write keeps the floor or more.
    {Player p(false,9);p.l.available=true;p.g.tick(p.l,0);p.l.running=true;p.l.mission=Whale;p.l.code=0x20;p.l.lives={40.f};p.g.tick(p.l,10);
     p.g.heard({"bosslife",lifeKey(Whale,9,0),1},20);auto a=p.g.tick(p.l,30);
     CHECK(a.hurt.size()==1&&a.hurt[0].percent>=Floor&&a.hurt[0].percent>0.f);}
    // Another fight of the same boss (Cid's own replay, its own number): its life is not taken.
    {Player p(false,9);p.l.available=true;p.g.tick(p.l,0);p.l.running=true;p.l.mission=Whale;p.l.code=0x20;p.l.lives={90.f};p.g.tick(p.l,10);
     p.g.heard({"bosslife",lifeKey(Whale,10,0),100},20);CHECK(p.g.tick(p.l,30).hurt.empty());}
    // The fights end; both back in their rooms within KeepMs: Ann's start is never joined again, Bob's never said.
    ann.l={};ann.l.available=true;ann.l.inRoom=true;bob.l={};bob.l.available=true;bob.l.inRoom=true;
    t+=1000;round({&ann,&bob,&cid},t);
    for(int i=0;i<5;++i){t+=AfterFightMs;round({&ann,&bob,&cid},t);CHECK(!ann.last.start&&!bob.last.start);}
    // Dan joins late: in his room after KeepMs, nothing is started.
    {Player dan(true,77),eve(true,88);dan.l.inRoom=false;eve.l.inRoom=true;uint64_t u=0;round({&dan,&eve},u);
     eve.l.running=true;eve.l.mission=Higgs;eve.l.code=0x22;u+=100;round({&dan,&eve},u);
     u+=KeepMs+1;dan.l.inRoom=true;round({&dan,&eve},u);CHECK(!dan.last.start);}
    // A player in a fight, or in a boss fight, is not started.
    {Player dan(true,77),eve(true,88);dan.l.inRoom=true;dan.l.boss=true;eve.l.inRoom=true;uint64_t u=0;round({&dan,&eve},u);
     eve.l.running=true;eve.l.mission=Higgs;eve.l.code=0x22;u+=100;round({&dan,&eve},u);u+=100;round({&dan,&eve},u);CHECK(!dan.last.start);
     dan.l.boss=false;dan.l.running=true;u+=100;round({&dan,&eve},u);CHECK(!dan.last.start);
     dan.l.running=false;u+=100;round({&dan,&eve},u);CHECK(!dan.last.start);   // just after his own fight
     u+=AfterFightMs;round({&dan,&eve},u);CHECK(dan.last.start&&*dan.last.start==0x22);}
    // Started, but the game began another mission (its menu rows differ): fought alone, nothing shared.
    {Player dan(true,77),eve(true,88);dan.l.inRoom=true;eve.l.inRoom=true;uint64_t u=0;round({&dan,&eve},u);
     eve.l.running=true;eve.l.mission=Higgs;eve.l.code=0x22;eve.l.lives={70.f};u+=100;round({&dan,&eve},u);u+=100;round({&dan,&eve},u);
     CHECK(dan.last.start.has_value());
     dan.l.running=true;dan.l.mission=Whale;dan.l.lives={100.f};u+=100;round({&dan,&eve},u);
     CHECK(dan.g.fight()==0&&dan.g.counters().mismatched==1);
     for(int i=0;i<6;++i){u+=1000;round({&dan,&eve},u);CHECK(dan.last.hurt.empty()&&count(dan.last.say,"bosslife")==0);}}
    // A start that never begins is given up after StartWaitMs (and not tried again).
    {Player dan(true,77),eve(true,88);dan.l.inRoom=true;eve.l.inRoom=true;uint64_t u=0;round({&dan,&eve},u);
     eve.l.running=true;eve.l.mission=Higgs;eve.l.code=0x22;u+=100;round({&dan,&eve},u);u+=100;round({&dan,&eve},u);CHECK(dan.last.start.has_value());
     u+=StartWaitMs+1;round({&dan,&eve},u);CHECK(dan.g.counters().givenUp==1&&!dan.last.start);
     u+=1000;round({&dan,&eve},u);CHECK(!dan.last.start);}
    // Malformed news is ignored: not a replay mission, no fight number, a code outside the menu's.
    {Player p(true,1);p.l.inRoom=true;p.g.tick(p.l,0);
     p.g.heard({"replay",startKey(321,5),0x1f},1);p.g.heard({"replay",startKey(Whale,0),0x1f},1);p.g.heard({"replay",startKey(Whale,5),0x63},1);p.g.heard({"replay",-5,0x1f},1);
     auto a=p.g.tick(p.l,2);CHECK(!a.start&&a.notices.empty());}
    if(failures){std::fprintf(stderr,"%u groupreplay oracle failures\n",failures);return 1;}
    std::puts("groupreplay oracles passed");return 0;
}
