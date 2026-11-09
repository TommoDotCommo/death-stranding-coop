// A name over the partner (nametag.h): the label is made when allowed, follows its point every tick, goes when not
// allowed, is replaced on a new name, is forgotten (never touched) with the world, and gives up after refusals; names
// are decoded from UTF-8 without markup or control characters and capped.
#include "nametag.h"
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
    t.tick(true,u"Sam",&p);auto before=h.removes;t.forget("load");CHECK(h.removes==before&&!t.record());
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
    if(failures)return 1;
    std::puts("nametag tests passed");return 0;
}
