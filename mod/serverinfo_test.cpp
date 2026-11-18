#include "serverinfo.h"
#include <cstdio>
using namespace mod;using namespace mod::serverinfo;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"server info oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
bool has(const std::u16string& t,const char16_t* p){return t.find(p)!=std::u16string::npos;}
const char* Doc=R"({"format":1,"server":"s","name":"Bridges é","preset":"friends","players":{"count":5,"limit":16},
 "online":{"count":2,"names":["Ann","Bo\nb"]},"settings":[{"text":"r1"},{"text":"r2"},{"text":"r3"},{"text":"r4"},{"text":"r5"},{"text":"r6"}]})";
struct Game {
    bool free=true,up=false;unsigned answer=dialog::NoAnswer,opens=0;dialog::Request last;
    dialog::Api api(){dialog::Api a;a.free=[this]{return free;};a.showing=[this]{return up;};
        a.open=[this](const dialog::Request& r){++opens;last=r;up=true;return true;};
        a.take=[this]{auto v=answer;answer=dialog::NoAnswer;if(v!=dialog::NoAnswer)up=false;return v;};return a;}
};
struct Net {
    unsigned starts=0;std::optional<std::string> ready;
    Fetch fetch(){Fetch f;f.start=[this]{++starts;};f.poll=[this]{auto v=ready;ready.reset();return v;};return f;}
};
int main(){
    auto none=[](const std::string&){};Facts net;net.group=true;net.server=true;net.nearby={"Cid"};
    {   auto p=parse(Doc);CHECK(p.read&&p.name=="Bridges \xc3\xa9"&&p.preset=="friends"&&p.known==5&&p.limit==16&&p.onlineKnown&&p.online==2&&p.names.size()==2&&p.rules.size()==6);
        for(auto bad:{"","nonsense","[1]","{\"format\":2}","{\"format\":\"1\"}"})CHECK(!parse(bad).read);
        auto old=parse(R"({"format":1,"name":"","preset":"p","players":{"count":1,"limit":2},"settings":[]})");CHECK(old.read&&!old.onlineKnown);
        auto v=pages(net,p);CHECK(v.size()==3);
        CHECK(has(v[0],u"Bridges é (friends rules)")&&has(v[0],u"\nOnline now: 2\nAnn, Bob\n")&&has(v[0],u"known here: 5 of 16")&&has(v[0],u"Near you: Cid"));
        CHECK(has(v[1],u"Rules 1 of 2\nr1\nr2\nr3\nr4\nr5")&&!has(v[1],u"r6")&&has(v[2],u"Rules 2 of 2\nr6"));
        auto w=pages(net,old);CHECK(w.size()==1&&has(w[0],u"This server")&&!has(w[0],u"Online now"));
        auto x=pages(net,Policy{});CHECK(x.size()==1&&has(x[0],u"did not answer")&&has(x[0],u"Near you: Cid"));
        Facts duo;duo.partner="Ann";duo.partnerHere=true;auto y=pages(duo,Policy{});CHECK(y.size()==1&&has(y[0],u"Two-player game\nAnn: connected\nNear you: nobody"));
        duo.partner.clear();duo.partnerHere=false;CHECK(has(pages(duo,Policy{})[0],u"Partner: not connected"));
        Policy many=p;many.names.assign(15,"N");CHECK(has(pages(net,many)[0],u" and 3 more"));
        Policy bad=p;bad.name=std::string("A\xff\xc3");CHECK(pages(net,bad)[0].rfind(u"A (friends",0)==0);}
    {   // The key reads the server first; the page opens with the answer; Next page turns and wraps; Close ends it.
        Game g;Net n;Screen s(g.api(),n.fetch(),none);s.tick(false,true,net,0);CHECK(!n.starts&&!g.opens);
        s.tick(true,true,net,10);CHECK(n.starts==1&&!g.opens);s.tick(true,true,net,20);CHECK(n.starts==1);
        n.ready=Doc;s.tick(false,true,net,30);CHECK(g.opens==1&&s.open()&&s.answered()&&s.pageCount()==3&&g.last.rows.size()==2&&has(g.last.body,u"Online now"));
        for(int turn=1;turn<=3;++turn){g.answer=0;s.tick(false,true,net,40);CHECK(!s.open());s.tick(false,true,net,50);CHECK(s.open()&&s.page()==size_t(turn%3));}
        CHECK(has(g.last.body,u"Online now")&&n.starts==1);
        g.answer=1;s.tick(false,true,net,60);s.tick(false,true,net,70);CHECK(!s.open()&&g.opens==4);
        s.tick(true,true,net,80);CHECK(n.starts==2);n.ready="";s.tick(false,true,net,90);CHECK(g.opens==5&&!s.answered()&&g.last.rows.size()==1&&has(g.last.body,u"did not answer"));
        g.answer=0;s.tick(false,true,net,100);CHECK(!s.open());}
    {   // A server that says nothing: the page opens after the wait, not before.
        Game g;Net n;Screen s(g.api(),n.fetch(),none);s.tick(true,true,net,0);s.tick(false,true,net,WaitMs-1);CHECK(!g.opens);s.tick(false,true,net,WaitMs);CHECK(g.opens==1&&has(g.last.body,u"did not answer"));}
    {   // No server: at once, nothing read. Not in an event, not over another menu; and an answer that comes too late for the moment shows nothing.
        Facts duo;Game g;Net n;Screen s(g.api(),n.fetch(),none);s.tick(true,false,duo,0);CHECK(!g.opens&&!n.starts);
        g.free=false;s.tick(true,true,duo,10);CHECK(!g.opens);g.free=true;s.tick(true,true,duo,20);CHECK(g.opens==1&&!n.starts);
        Game h;Net m;Screen t(h.api(),m.fetch(),none);t.tick(true,true,net,0);m.ready=Doc;t.tick(false,false,net,10);CHECK(!h.opens&&!t.open());t.tick(false,true,net,20);CHECK(!h.opens);}
    {   // The game takes it away, or stays busy when a page should turn.
        Game g;Net n;Screen s(g.api(),n.fetch(),none);s.tick(true,true,net,0);n.ready=Doc;s.tick(false,true,net,10);g.up=false;s.tick(false,true,net,20);CHECK(s.open());
        g.up=true;s.tick(false,true,net,30);g.up=false;s.tick(false,true,net,40);CHECK(!s.open());
        s.tick(true,true,net,50);n.ready=Doc;s.tick(false,true,net,60);g.answer=0;g.free=false;s.tick(false,true,net,70);s.tick(false,true,net,70+ReopenMs);g.free=true;s.tick(false,true,net,71+ReopenMs);CHECK(g.opens==2&&!s.open());}
    if(!failures)std::puts("server info checks passed");
    return failures?1:0;
}
