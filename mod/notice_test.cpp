#include "notice.h"
#include <cstdio>
#include <vector>
using namespace mod::notice;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"notice oracle: %s\n",#x);++failures;}}while(0)
struct Hud {std::vector<std::u16string> shown;bool there=true;std::vector<std::string> log;
    Api api(){Api a;a.show=[this](const std::u16string& t){if(!there)return false;shown.push_back(t);return true;};return a;}
    Notifier make(){return Notifier(api(),[this](const std::string& r){log.push_back(r);});}};
int main(){
    CHECK(text(Kind::Joined,"Higgs")==u"Higgs joined your session.");CHECK(text(Kind::Left,"")==u"Your partner left the session.");
    CHECK(text(Kind::Joined,"<b>Sam{0}  ")==u"bSam0 joined your session.");   // markup characters never reach the UI
    CHECK(text(Kind::Alone,"x").find(u"firewall")!=std::u16string::npos);
    {   // A partner arrives: told once, with his name (waited for), and when he leaves.
        Hud h;auto n=h.make();uint64_t t=0;
        for(;t<5000;t+=50)n.tick(t,true,false,"");
        CHECK(h.shown.empty());
        n.tick(t,true,true,"");n.tick(t+=50,true,true,"");CHECK(h.shown.empty());              // name not here yet
        n.tick(t+=50,true,true,"Higgs");CHECK(h.shown.size()==1&&h.shown[0]==u"Higgs joined your session.");
        for(int i=0;i<200;++i)n.tick(t+=50,true,true,"Higgs");
        CHECK(h.shown.size()==1);
        n.tick(t+=50,true,false,"");CHECK(h.shown.size()==2&&h.shown[1]==u"Higgs left the session.");   // his last name is kept
        for(int i=0;i<2000;++i)n.tick(t+=50,true,false,"");
        CHECK(h.shown.size()==2);                                                               // never "alone" after a partner
        CHECK(h.log.size()==2&&h.log[0]=="NOTICE shown: joined\n"&&h.log[1]=="NOTICE shown: left\n");
    }
    {   // No name ever: told after the wait, as "Your partner".
        Hud h;auto n=h.make();uint64_t t=1000;n.tick(t,true,true,"");
        for(;t<1000+Notifier::NameMs-50;t+=50)n.tick(t,true,true,"");
        CHECK(h.shown.empty());
        n.tick(1000+Notifier::NameMs,true,true,"");CHECK(h.shown.size()==1&&h.shown[0]==u"Your partner joined your session.");
    }
    {   // Nobody arrives: one hint after a continuous stretch in the world; leaving the world starts the stretch again.
        Hud h;auto n=h.make();uint64_t t=0;
        for(;t<20000;t+=100)n.tick(t,true,false,"");
        for(;t<22000;t+=100)n.tick(t,false,false,"");                                         // a cutscene
        for(;t<22000+Notifier::AloneMs-100;t+=100)n.tick(t,true,false,"");
        CHECK(h.shown.empty());
        for(;t<60000;t+=100)n.tick(t,true,false,"");
        CHECK(h.shown.size()==1&&h.shown[0]==text(Kind::Alone,""));
        for(;t<200000;t+=100)n.tick(t,true,false,"");
        CHECK(h.shown.size()==1);                                                               // once
    }
    {   // Outside the world nothing is shown; what happened is told afterwards, in order and spaced out.
        Hud h;auto n=h.make();uint64_t t=0;
        n.tick(t,false,true,"Fragile");n.tick(t+=100,false,false,"Fragile");n.tick(t+=100,false,true,"Fragile");
        for(int i=0;i<50;++i)n.tick(t+=100,false,true,"Fragile");
        CHECK(h.shown.empty()&&n.pending()==3);
        n.tick(t+=100,true,true,"Fragile");CHECK(h.shown.size()==1&&h.shown[0]==u"Fragile joined your session.");
        n.tick(t+=100,true,true,"Fragile");CHECK(h.shown.size()==1);                            // the gap
        uint64_t first=t-100;while(h.shown.size()<2&&t<first+10000)n.tick(t+=100,true,true,"Fragile");
        CHECK(h.shown.size()==2&&h.shown[1]==u"Fragile left the session."&&t-first>=Notifier::GapMs);
        while(h.shown.size()<3&&t<first+20000)n.tick(t+=100,true,true,"Fragile");
        CHECK(h.shown.size()==3&&h.shown[2]==u"Fragile joined your session."&&n.pending()==0);
    }
    {   // The partner goes into a cutscene or private room: told once it has lasted, and again when he is back. A brief
        // transition is not told, nor is "back" without "away"; a partner who leaves while away just leaves.
        Hud h;auto n=h.make();uint64_t t=0;
        n.tick(t,true,true,"Fragile");n.tick(t+=100,true,true,"Fragile");CHECK(h.shown.size()==1);
        for(int i=0;i<60;++i)n.tick(t+=100,true,true,"Fragile",false);
        for(int i=0;i<10;++i)n.tick(t+=100,true,true,"Fragile",true);                           // 1 s: a door
        for(int i=0;i<60;++i)n.tick(t+=100,true,true,"Fragile",false);
        CHECK(h.shown.size()==1);
        for(int i=0;i<19;++i)n.tick(t+=100,true,true,"Fragile",true);
        CHECK(h.shown.size()==1);
        for(int i=0;i<40;++i)n.tick(t+=100,true,true,"Fragile",true);
        CHECK(h.shown.size()==2&&h.shown[1]==u"Fragile is away (cutscene or private room).");
        for(int i=0;i<300;++i)n.tick(t+=100,true,true,"Fragile",true);
        CHECK(h.shown.size()==2);                                                               // once per absence
        for(int i=0;i<40;++i)n.tick(t+=100,true,true,"Fragile",false);
        CHECK(h.shown.size()==3&&h.shown[2]==u"Fragile is back.");
        for(int i=0;i<60;++i)n.tick(t+=100,true,true,"Fragile",true);
        CHECK(h.shown.size()==4&&h.shown[3]==u"Fragile is away (cutscene or private room).");
        for(int i=0;i<60;++i)n.tick(t+=100,true,false,"",true);                                 // the link ends while he is away
        CHECK(h.shown.size()==5&&h.shown[4]==u"Fragile left the session.");
        for(int i=0;i<60;++i)n.tick(t+=100,true,true,"Fragile",false);
        CHECK(h.shown.size()==6&&h.shown[5]==u"Fragile joined your session.");                  // no "back" for a new session
    }
    {   // The HUD is not there: tried again, then dropped; a flapping link never grows the queue.
        Hud h;h.there=false;auto n=h.make();uint64_t t=0;
        n.tick(t,true,true,"A");
        for(;t<Notifier::GiveUpMs-100;t+=100)n.tick(t,true,true,"A");
        CHECK(n.pending()==1&&h.log.empty());
        n.tick(Notifier::GiveUpMs,true,true,"A");CHECK(n.pending()==0&&h.log.size()==1&&h.log[0].find("dropped")!=std::string::npos);
        for(int i=0;i<40;++i)n.tick(t+=100,true,i%2==0,"A");
        CHECK(n.pending()<=Notifier::MaxPending);
        h.there=true;h.log.clear();unsigned before=n.shown();for(int i=0;i<400;++i)n.tick(t+=100,true,true,"A");
        CHECK(n.shown()-before<=Notifier::MaxPending+1&&n.pending()==0);
    }
    if(!failures)std::puts("notice checks passed");
    return failures?1:0;
}
