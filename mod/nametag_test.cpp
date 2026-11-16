// A name over the partner (nametag.h): the label is made when allowed, follows its point every tick, goes when not
// allowed, is replaced on a new name, is forgotten (never touched) with the world, and gives up after refusals; names
// are decoded from UTF-8 without markup or control characters and capped.
#include "nametag.h"
#include <algorithm>
#include <cstdio>
#include <map>
using namespace mod::nametag;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"nametag oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
namespace {
struct Hud {
    std::map<uint64_t,std::pair<std::u16string,std::array<double,3>>> live;uint64_t next=0x100;unsigned creates=0,removes=0,refuse=0;
    Api api(){Api a;
        a.create=[this](const std::u16string& n,const std::array<double,3>& at)->uint64_t{++creates;if(refuse){--refuse;return 0;}live[next+=0x40]={n,at};return next;};
        a.move=[this](uint64_t r,const std::array<double,3>& at){live.at(r).second=at;};
        a.remove=[this](uint64_t r){++removes;live.erase(r);};
        return a;}
};
}
int main(){
    CHECK(fromUtf8("Sam")==u"Sam");
    CHECK(fromUtf8("Ren\xc3\xa9")==u"René");
    CHECK(fromUtf8("<b>x</b>{y}")==u"bx/by");
    CHECK(fromUtf8("a\x01\x7f\xff" "b")==u"ab");
    CHECK(fromUtf8(std::string(40,'x')).size()==Tag::MaxName);
    Hud h;std::vector<std::string> log;Tag t(h.api(),[&](const std::string& m){log.push_back(m);});
    std::array<double,3> p{1,2,3},q{4,5,6};
    t.tick(false,u"Sam",&p);CHECK(h.live.empty());
    t.tick(true,u"Sam",&p);CHECK(h.live.size()==1&&h.live.begin()->second.first==u"Sam");
    t.tick(true,u"Sam",&q);CHECK(h.live.size()==1&&h.live.begin()->second.second==q&&h.creates==1);
    t.tick(true,u"Fragile",&q);CHECK(h.live.size()==1&&h.live.begin()->second.first==u"Fragile"&&h.removes==1);
    t.tick(true,u"Fragile",nullptr);CHECK(h.live.empty());
    t.tick(true,u"",&p);CHECK(h.live.empty());
    t.tick(true,u"Sam",&p);auto before=h.removes;t.forget("load");CHECK(h.removes==before+1&&!t.record()&&h.live.empty());
    t.tick(true,u"Sam",&p);CHECK(h.creates==4);
    std::array<double,3> bad{0,0,1.0/0.0};auto r=t.record();t.tick(true,u"Sam",&bad);CHECK(h.live.at(r).second==p);
    Hud n;n.refuse=99;Tag g(n.api(),[](const std::string&){});
    for(int i=0;i<10;++i)g.tick(true,u"Sam",&p);
    CHECK(n.creates==Tag::MaxFailures&&!g.record());
    // Wire: a name in 9-byte chunks round-trips (multibyte characters across chunk edges); long names are cut on a
    // character boundary; an empty name is one event; chunks out of order or a broken run are refused.
    {
        auto roundtrip=[](const std::string& n){Receiver r;std::optional<std::string> got;for(auto& e:encode(n))got=r.feed(e);return got;};
        CHECK(roundtrip("Sam")==std::string("Sam"));
        std::string accents="Ren\xc3\xa9" "e Porter \xc3\xa9\xc3\xa9\xc3\xa9";CHECK(roundtrip(accents)==accents);
        CHECK(roundtrip("")==std::string());
        std::string longName(30,'x');longName+="\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";auto cut=roundtrip(longName);
        CHECK(cut&&cut->size()<=MaxBytes&&fromUtf8(*cut).size()>=Tag::MaxName-1);
        auto ev=encode("Higgs Monaghan");CHECK(ev.size()==2);
        for(auto& e:ev)CHECK(e.type==WireType&&!(e.ref>>48));
        Receiver r;CHECK(!r.feed(ev[1]));CHECK(!r.feed(ev[0]));CHECK(!r.feed(ev[0]));CHECK(r.feed(ev[1])==std::string("Higgs Monaghan"));
        auto three=encode("ABCDEFGHIJKLMNOPQRS");CHECK(three.size()==3);
        Receiver o;CHECK(!o.feed(three[0]));CHECK(!o.feed(three[2]));CHECK(!o.feed(three[1]));   // a skipped chunk breaks the run
        Sender s;CHECK(s.tick("Sam",false).empty());CHECK(s.tick("Sam",true).size()==1);CHECK(s.tick("Sam",true).empty());
        CHECK(s.tick("Fragile",true).size()==1);s.tick("Fragile",false);CHECK(s.tick("Fragile",true).size()==1);
    }
    // Several labels (Tags): one per body by key, each at its own body's point with its own name; none before the body
    // exists or while it is hidden; all gone while not permitted; a key no longer listed loses its label; a new world
    // hands every record back.
    {
        Hud hud;std::vector<std::string> rows;
        std::map<uint64_t,std::array<double,3>> bodies;   // the bodies that are shown, and where their heads are
        Tags tags(hud.api(),[&](uint64_t body)->std::optional<std::array<double,3>>{auto it=bodies.find(body);if(it==bodies.end())return std::nullopt;return it->second;},
                  [&](const std::string& m){rows.push_back(m);});
        auto at=[&](uint64_t key){return hud.live.at(tags.record(key));};
        bodies[0xa0]={1,1,3};bodies[0xb0]={2,2,3};
        std::vector<Named> named{{0,0xa0,u"Fragile"},{7,0xb0,u"Sam 7"},{9,0,u"Sam 9"}};   // player 9 has no body yet
        tags.tick(true,named);
        CHECK(hud.live.size()==2&&tags.size()==3&&tags.shown()==2&&!tags.record(9));
        CHECK(at(0).first==u"Fragile"&&at(0).second==bodies[0xa0]&&at(7).first==u"Sam 7"&&at(7).second==bodies[0xb0]);
        CHECK(rows.size()==2&&rows[0]=="TAG shown\n"&&rows[1]=="TAG player 7 shown\n");
        // The body of player 9 is made: its label comes, over that body. Everybody moves: each label follows its own.
        named[2].body=0xc0;bodies[0xc0]={5,5,3};bodies[0xa0]={1,4,3};bodies[0xb0]={2,6,3};tags.tick(true,named);
        CHECK(hud.live.size()==3&&at(9).first==u"Sam 9"&&at(9).second==bodies[0xc0]&&at(0).second==bodies[0xa0]&&at(7).second==bodies[0xb0]&&hud.creates==3);
        // One body is hidden (its player is stale): that label goes, the others stay; shown again: a label again.
        auto hidden=bodies[0xb0];bodies.erase(0xb0);tags.tick(true,named);
        CHECK(hud.live.size()==2&&!tags.record(7)&&tags.record(0)&&tags.record(9)&&tags.size()==3);
        bodies[0xb0]=hidden;tags.tick(true,named);CHECK(hud.live.size()==3&&at(7).first==u"Sam 7");
        // A game event: no labels at all; over: all of them again.
        tags.tick(false,named);CHECK(hud.live.empty()&&tags.shown()==0);
        tags.tick(true,named);CHECK(hud.live.size()==3);
        // Player 7 leaves: only that label goes, and nothing of it is kept.
        named.erase(named.begin()+1);auto removes=hud.removes;tags.tick(true,named);
        CHECK(hud.live.size()==2&&tags.size()==2&&hud.removes==removes+1&&at(0).first==u"Fragile"&&at(9).first==u"Sam 9");
        // A new name for one player replaces that label only.
        named[1].name=u"Higgs";auto creates=hud.creates;tags.tick(true,named);CHECK(at(9).first==u"Higgs"&&at(0).first==u"Fragile"&&hud.creates==creates+1);
        // A new world: every record is handed back once, and the labels are made again over the new bodies.
        removes=hud.removes;tags.forget("world changed");CHECK(hud.removes==removes+2&&tags.shown()==0&&hud.live.empty());
        tags.tick(true,named);CHECK(hud.live.size()==2);
        // Nobody listed: nothing is left.
        tags.tick(true,{});CHECK(hud.live.empty()&&tags.size()==0);
    }
    // A limit on the records at once: the partner (key 0) first, then the bodies nearest the viewer; a body that has its
    // label keeps it until another is clearly nearer; nobody over the limit is registered, and none is while another
    // still holds its place.
    {
        Hud hud;unsigned most=0;
        std::map<uint64_t,std::array<double,3>> bodies;
        auto api=hud.api();auto create=api.create;
        api.create=[&,create](const std::u16string& n,const std::array<double,3>& at){auto r=create(n,at);most=std::max<unsigned>(most,unsigned(hud.live.size()));return r;};
        Tags tags(api,[&](uint64_t body)->std::optional<std::array<double,3>>{auto it=bodies.find(body);if(it==bodies.end())return std::nullopt;return it->second;},{},3);
        std::array<double,3> viewer{0,0,0};
        std::vector<Named> named{{5,0x50,u"five"},{6,0x60,u"six"},{7,0x70,u"seven"},{8,0x80,u"eight"},{0,0x10,u"partner"}};
        bodies[0x50]={10,0,0};bodies[0x60]={20,0,0};bodies[0x70]={30,0,0};bodies[0x80]={40,0,0};bodies[0x10]={500,0,0};
        auto has=[&](uint64_t key){return tags.record(key)!=0;};
        tags.tick(true,named,&viewer);
        CHECK(hud.live.size()==3&&has(0)&&has(5)&&has(6)&&!has(7)&&!has(8));        // the partner, far as he is, and the two nearest
        // The porter walks towards the far ones: the labels change over to the nearest, never more than three at once.
        viewer={38,0,0};tags.tick(true,named,&viewer);
        CHECK(hud.live.size()==3&&has(0)&&has(7)&&has(8)&&!has(5)&&!has(6)&&most==3);
        // Two at about the same distance: the one that has the label keeps it; clearly nearer: it changes over.
        viewer={0,0,0};tags.tick(true,named,&viewer);CHECK(has(5)&&has(6)&&!has(7));
        bodies[0x70]={19.5,0,0};tags.tick(true,named,&viewer);CHECK(has(6)&&!has(7));
        bodies[0x70]={17,0,0};tags.tick(true,named,&viewer);CHECK(has(7)&&!has(6)&&hud.live.size()==3&&most==3);
        // A hidden body takes no place: the next nearest gets it.
        bodies.erase(0x50);tags.tick(true,named,&viewer);CHECK(!has(5)&&has(7)&&has(6)&&has(0)&&hud.live.size()==3);
        // Fewer bodies than the limit: everybody. No limit: everybody.
        bodies.erase(0x80);bodies.erase(0x10);tags.tick(true,named,&viewer);CHECK(hud.live.size()==2);
        bodies[0x50]={10,0,0};bodies[0x80]={40,0,0};bodies[0x10]={500,0,0};tags.limit(0);tags.tick(true,named,&viewer);CHECK(hud.live.size()==5);
        // Without a viewer the order listed decides (the partner still first).
        tags.limit(2);tags.tick(true,named,nullptr);CHECK(hud.live.size()==2&&has(0)&&has(5));
        Tags usual(hud.api(),{},{});(void)usual;CHECK(LabelsAtOnce==4);
    }
    if(failures)return 1;
    std::puts("nametag tests passed");return 0;
}
