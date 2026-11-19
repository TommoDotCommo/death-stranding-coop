#include "chat.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
using namespace mod::chat;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"chat oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static Press ch(char16_t c){return {Press::Char,c};}
struct Game {
    std::vector<std::u16string> drawn;std::string order;unsigned hides=0,takes=0,gives=0;bool refuse=false;std::vector<Press> queue;std::vector<std::string> sent;
    Hud hud(){Hud h;h.show=[this](const std::u16string& t,const std::u16string& b){CHECK(t==Title);drawn.push_back(b);order+='S';return true;};h.hide=[this]{++hides;order+='H';};return h;}
    Keyboard keys(){Keyboard k;k.take=[this]{if(refuse)return false;++takes;return true;};k.give=[this]{++gives;};k.presses=[this]{auto v=queue;queue.clear();return v;};return k;}
    Box box(){return Box(hud(),keys(),[this](const std::string& s){sent.push_back(s);},[](const std::string&){});}
};
template<class F> bool soon(F f){for(int i=0;i<2000;++i){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return f();}
int main(){
    {   Composer c;CHECK(c.type(u'a')&&!c.type(u'\n')&&!c.type(0x7f)&&c.text()==u"a");c.back();c.back();CHECK(c.text().empty());
        for(size_t i=0;i<MaxChars;++i)CHECK(c.type(u'x'));CHECK(!c.type(u'y')&&c.text().size()==MaxChars);c.clear();
        c.type(0xd83d);c.type(0xde00);c.type(u'!');CHECK(c.utf8()=="\xf0\x9f\x98\x80!");c.back();c.back();CHECK(c.text().empty());
        c.type(u' ');c.type(0xe9);c.type(u' ');CHECK(c.utf8()=="\xc3\xa9");c.clear();c.type(u' ');CHECK(c.utf8().empty());}
    {   auto a=parse(R"({"format":1,"on":true,"next":7,"lines":[{"n":6,"name":"Ann","text":"hi"}]})");CHECK(a.read&&a.on&&a.next=="7"&&a.lines.size()==1&&shown(a.lines[0])==u"Ann: hi");
        for(auto bad:{"","x","{\"format\":2}"})CHECK(!parse(bad).read);
        CHECK(shown({"A\nB","x\ty"})==u"AB: xy");CHECK(sayBody("a\"b")=="{\"text\":\"a\\\"b\"}");
        CHECK(refusal(200,"").empty()&&refusal(429,"").find(u"too many")!=std::u16string::npos&&refusal(403,"chat-off").find(u"off")!=std::u16string::npos&&refusal(0,"").find(u"did not reach")!=std::u16string::npos);
        CHECK(pollAfter(PollMs,0)==PollMs&&pollAfter(PollMs,5000)==5000&&pollAfter(PollMs,UINT64_MAX)==MaxWaitMs&&pollAfter(BackoffMs,5000)==BackoffMs);
        CHECK(parse(R"({"format":1,"on":true,"next":1,"wait_ms":4000,"lines":[]})").waitMs==4000);
        unsigned f=0;CHECK(delayAfter(false,f)==PollMs&&delayAfter(false,f)==PollMs&&delayAfter(false,f)==BackoffMs&&delayAfter(true,f)==PollMs&&f==0);}
    {   // Y opens it and takes the keyboard; the Y itself is not typed; letters draw; Backspace; Enter sends and gives the keys back.
        Game g;auto b=g.box();b.tick(false,false,true);CHECK(!b.typing()&&!g.takes);b.tick(true,true,false);CHECK(!b.typing());
        b.tick(true,true,true);CHECK(b.typing()&&g.takes==1&&g.drawn.back()==u"_");
        g.queue={ch(u'y')};b.tick(false,true,true);CHECK(g.drawn.size()==1);b.tick(false,false,true);
        g.queue={ch(u'h'),ch(u'y'),ch(u'x'),{Press::Back,0}};b.tick(false,false,true);CHECK(g.drawn.back()==u"hy_"&&g.drawn.size()==2&&g.order=="SHS");   // the old line goes at once, or the game fades it out first
        unsigned n=g.drawn.size();b.tick(false,false,true);CHECK(g.drawn.size()==n);      // nothing new, nothing drawn
        g.queue={{Press::Enter,0}};b.tick(false,false,true);CHECK(!b.typing()&&g.sent==(std::vector<std::string>{"hy"})&&g.gives==1&&g.order=="SHSH");}
    {   // Esc drops it; Enter on nothing sends nothing; a game event ends it; the keyboard not to be had.
        Game g;auto b=g.box();b.tick(true,false,true);g.queue={ch(u'a'),{Press::Escape,0}};b.tick(false,false,true);CHECK(!b.typing()&&g.sent.empty()&&g.gives==1);
        b.tick(true,false,true);g.queue={{Press::Enter,0}};b.tick(false,false,true);CHECK(g.sent.empty()&&!b.typing());
        b.tick(true,false,true);CHECK(b.typing());b.tick(false,false,false);CHECK(!b.typing()&&g.gives==3&&g.hides==3);b.close();CHECK(g.gives==3);
        Game h;h.refuse=true;auto c=h.box();c.tick(true,false,true);CHECK(!c.typing()&&h.drawn.empty());}
    {   // The link: the first read has no number, later ones the server's; lines arrive in order; a line is sent; a refusal is told.
        std::mutex m;std::vector<std::string> sinces,bodies;std::atomic<int> turn{0};
        Link::Get get=[&](const std::string& s){std::lock_guard<std::mutex> g(m);sinces.push_back(s);int t=turn++;
            if(t==0)return Result{200,R"({"format":1,"on":true,"next":5,"lines":[]})"};
            if(t==1)return Result{200,R"({"format":1,"on":true,"next":7,"lines":[{"name":"A","text":"1"},{"name":"B","text":"2"}]})"};
            return Result{200,R"({"format":1,"on":true,"next":7,"lines":[]})"};};
        std::atomic<int> status{200};Link::Post post=[&](const std::string& b){std::lock_guard<std::mutex> g(m);bodies.push_back(b);return Result{status.load(),"{}"};};
        Link l(get,post,[](bool,unsigned&){return uint64_t(1);});
        std::vector<std::u16string> got;CHECK(soon([&]{for(auto& x:l.take())got.push_back(x);return got.size()>=2;}));CHECK(got==(std::vector<std::u16string>{u"A: 1",u"B: 2"}));
        CHECK(soon([&]{return l.counters().reads>=3;}));{std::lock_guard<std::mutex> g(m);CHECK(sinces[0].empty()&&sinces[1]=="5"&&sinces[2]=="7");}
        l.say("hello");CHECK(soon([&]{return l.counters().sent==1;}));{std::lock_guard<std::mutex> g(m);CHECK(bodies.back()=="{\"text\":\"hello\"}");}
        status=429;l.say("x");CHECK(soon([&]{return l.counters().refused==1;}));std::u16string told;CHECK(soon([&]{for(auto& x:l.take())told=x;return !told.empty();})&&told.find(u"too many")!=std::u16string::npos);CHECK(l.counters().on&&l.counters().received==2);}
    if(!failures)std::puts("chat checks passed");
    return failures?1:0;
}
