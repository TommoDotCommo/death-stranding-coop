#include "help.h"
#include <cstdio>
#include <vector>
using namespace mod::help;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"help oracle: %s\n",#x);++failures;}}while(0)
struct Hud {
    unsigned shows=0,hides=0;bool there=true;std::u16string title,text;Place at;std::vector<std::string> log;
    Api api(){Api a;a.show=[this](const std::u16string& t,const std::u16string& b,const Place& p){if(!there)return false;++shows;title=t;text=b;at=p;return true;};
        a.hide=[this]{++hides;};return a;}
    Page page(){return Page(api(),[this](const std::string& r){log.push_back(r);});}
};
bool has(const std::u16string& text,const char16_t* part){return text.find(part)!=std::u16string::npos;}
int main(){
    CHECK(keyName('K')==u"K"&&keyName(0x70)==u"F1"&&keyName(0x78)==u"F9"&&keyName(0x71)==u"F2"&&keyName(0x10)==u"?");
    {   // Always the two keys of the mod itself; the others only when they do something in this session.
        auto t=body({});CHECK(has(t,u"F1 (hold)")&&has(t,u"F2  Mark a bug")&&!has(t,u"Ping")&&!has(t,u"Like")&&!has(t,u"Ride")&&!has(t,u"route")&&!has(t,u"materials")&&!has(t,u"story"));
        CHECK(!has(t,u"switches"));
        Facts f;f.ping=0x42;f.like=true;f.ride=0x4a;f.route=0x4b;f.materials=0x55;f.story=0x78;f.debug=0x72;t=body(f);CHECK(has(t,u"\nF3  Test mode switches"));
        CHECK(has(t,u"\nB  Ping")&&has(t,u"\nL  Like")&&has(t,u"\nJ  Ride")&&has(t,u"\nK  Record")&&has(t,u"\nU  Drop")&&has(t,u"\nF9  Catch up"));}
    {   // Shown once for a hold, gone on release, and again for the next hold.
        Hud h;auto p=h.page();p.place={2,.5f,.25f};
        p.tick(false,true,{},0);CHECK(!h.shows&&!h.hides&&!p.shown());
        p.tick(true,true,{},10);CHECK(h.shows==1&&p.shown()&&h.title==Title&&has(h.text,u"F2")&&h.at.pivot==2&&h.at.x==.5f&&h.at.y==.25f);
        for(uint64_t t=20;t<2000;t+=50)p.tick(true,true,{},t);CHECK(h.shows==1&&!h.hides);
        p.tick(false,true,{},2000);CHECK(h.hides==1&&!p.shown());p.tick(false,true,{},2050);CHECK(h.hides==1);
        p.tick(true,true,{},2100);CHECK(h.shows==2);}
    {   // A game event or a load ends it, with the key still held; it comes back after.
        Hud h;auto p=h.page();p.tick(true,false,{},0);CHECK(!h.shows);
        p.tick(true,true,{},10);CHECK(h.shows==1);p.tick(true,false,{},20);CHECK(h.hides==1&&!p.shown());
        p.tick(true,true,{},30);CHECK(h.shows==2);}
    {   // No HUD yet: asked again only after the wait, and nothing to hide.
        Hud h;h.there=false;auto p=h.page();p.tick(true,true,{},100);CHECK(!p.shown());h.there=true;
        p.tick(true,true,{},100+RetryMs-1);CHECK(!h.shows);p.tick(true,true,{},100+RetryMs);CHECK(h.shows==1);}
    {   // close(): once.
        Hud h;auto p=h.page();p.close();CHECK(!h.hides);p.tick(true,true,{},0);p.close();p.close();CHECK(h.hides==1);}
    if(!failures)std::puts("help page checks passed");
    return failures?1:0;
}
