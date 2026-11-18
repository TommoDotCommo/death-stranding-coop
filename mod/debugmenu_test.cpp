#include "debugmenu.h"
#include <cstdio>
using namespace mod;using namespace mod::debugmenu;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"debug menu oracle: %s\n",#x);++failures;}}while(0)
struct Game {
    bool free=true,up=false,refuse=false;unsigned answer=dialog::NoAnswer,opens=0;dialog::Request last;
    dialog::Api api(){dialog::Api a;a.free=[this]{return free;};a.showing=[this]{return up;};
        a.open=[this](const dialog::Request& r){if(refuse)return false;++opens;last=r;up=true;return true;};
        a.take=[this]{auto v=answer;answer=dialog::NoAnswer;if(v!=dialog::NoAnswer)up=false;return v;};return a;}
};
bool has(const std::u16string& t,const char16_t* p){return t.find(p)!=std::u16string::npos;}
int main(){
    CHECK(dialog::setting(1)==0&&dialog::setting(2)==3&&dialog::setting(3)==4&&dialog::setting(0)==0xff&&dialog::setting(4)==0xff);
    CHECK(dialog::rowOf(1,1)==0&&dialog::rowOf(1,4)==dialog::Dismissed&&dialog::rowOf(2,2)==0&&dialog::rowOf(2,3)==1&&dialog::rowOf(2,4)==dialog::Dismissed);
    CHECK(dialog::rowOf(3,2)==0&&dialog::rowOf(3,3)==1&&dialog::rowOf(3,4)==2&&dialog::rowOf(3,1)==dialog::Dismissed&&dialog::rowOf(3,9)==dialog::Dismissed);
    bool a=true,b=false;std::vector<Item> items={{u"Stamina",&a},{u"Blood",&b}};
    {   auto t=body(items,1);CHECK(has(t,u"   Stamina: ON")&&has(t,u"\n> Blood: OFF"));}
    {   // Opens on the key, once, with the three rows; nothing changes until an answer.
        Game g;Menu m(g.api(),items,[](const std::string&){});m.tick(false,true,0);CHECK(!g.opens);
        m.tick(true,true,10);CHECK(g.opens==1&&m.open()&&g.last.rows.size()==3&&g.last.start==RowSwitch&&has(g.last.body,u"> Stamina: ON"));
        m.tick(true,true,20);m.tick(false,true,30);CHECK(g.opens==1&&a&&!b);
        // Switch: applied, and the dialogue comes back with the new state, on the same row.
        g.answer=RowSwitch;m.tick(false,true,40);CHECK(!a&&!m.open()&&m.switches()==1);
        m.tick(false,true,50);CHECK(g.opens==2&&m.open()&&has(g.last.body,u"> Stamina: OFF")&&g.last.start==RowSwitch);
        // Next: the other switch is selected, the cursor stays on Next; it wraps round.
        g.answer=RowNext;m.tick(false,true,60);m.tick(false,true,70);CHECK(m.selected()==1&&g.opens==3&&g.last.start==RowNext&&has(g.last.body,u"> Blood: OFF"));
        g.answer=RowSwitch;m.tick(false,true,80);m.tick(false,true,90);CHECK(b&&!a&&has(g.last.body,u"> Blood: ON"));
        g.answer=RowNext;m.tick(false,true,100);m.tick(false,true,110);CHECK(m.selected()==0);
        // Close, and backing out: gone, nothing changed, and it stays gone until the key.
        g.answer=RowClose;m.tick(false,true,120);m.tick(false,true,130);CHECK(!m.open()&&g.opens==5&&b&&!a);
        m.tick(true,true,140);CHECK(g.opens==6);g.answer=dialog::Dismissed;m.tick(false,true,150);m.tick(false,true,160);CHECK(!m.open()&&g.opens==6&&b&&!a);a=true;b=false;}
    {   // Not in a game event, not over another menu, and a refusal leaves it closed.
        Game g;Menu m(g.api(),items,[](const std::string&){});m.tick(true,false,0);CHECK(!g.opens);
        g.free=false;m.tick(true,true,10);CHECK(!g.opens);g.free=true;g.refuse=true;m.tick(true,true,20);CHECK(!m.open());g.refuse=false;m.tick(true,true,30);CHECK(g.opens==1);}
    {   // Reopening waits for the game, and gives up rather than popping up later.
        Game g;Menu m(g.api(),items,[](const std::string&){});m.tick(true,true,0);g.answer=RowNext;g.free=false;m.tick(false,true,10);
        m.tick(false,true,10+ReopenMs-1);CHECK(g.opens==1);m.tick(false,true,10+ReopenMs);g.free=true;m.tick(false,true,10+ReopenMs+1);CHECK(g.opens==1&&!m.open());}
    {   // The game takes the dialogue away without an answer: the menu is closed, and only after it was seen up.
        Game g;Menu m(g.api(),items,[](const std::string&){});m.tick(true,true,0);g.up=false;m.tick(false,true,10);CHECK(m.open());
        g.up=true;m.tick(false,true,20);g.up=false;m.tick(false,true,30);CHECK(!m.open());m.tick(true,true,40);CHECK(g.opens==2);}
    if(!failures)std::puts("debug menu checks passed");
    return failures?1:0;
}
