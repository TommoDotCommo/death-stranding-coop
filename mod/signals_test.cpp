// Shout, scan and vitals for the partner (signals.h): the one-shot events (one per shout, one per scan, never an old
// one, bounded on both sides), the read-only scan counter and status block (refused unless they are this porter's),
// the vitals bytes (sent only when what is shown changes, at a low rate) and the name tag's text.
#include "signals.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace mod::signals;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"signals oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    {   // The event wire.
        auto e=encodeEvent(Kind::Shout,7);CHECK(e.type==EventWireType&&decodeEvent(e)&&*decodeEvent(e)==Kind::Shout);
        CHECK(*decodeEvent(encodeEvent(Kind::Scan,8))==Kind::Scan);
        auto bad=e;bad.item=0;CHECK(!decodeEvent(bad));bad.item=3;CHECK(!decodeEvent(bad));bad=e;bad.type=VitalsWireType;CHECK(!decodeEvent(bad));
        bad=e;bad.ref=1ull<<40;CHECK(!decodeEvent(bad));
    }
    {   // A shout: one event when the call starts, none while it is held, none while disconnected.
        EventSender s;unsigned sent=0;
        CHECK(!s.shout(1000,true,false));
        for(uint64_t t=2000;t<4000;t+=50)sent+=bool(s.shout(t,true,true));
        CHECK(sent==1);
        CHECK(!s.shout(4000,true,false));auto again=s.shout(4050,true,true);CHECK(again&&*decodeEvent(*again)==Kind::Shout);
        CHECK(!s.shout(4100,true,false));CHECK(!s.shout(4150,true,true));                    // hammered: inside the gap
        CHECK(!s.shout(9000,false,false));CHECK(!s.shout(9050,false,true));CHECK(!s.shout(9100,true,true));   // began while disconnected
    }
    {   // A scan: the counter going up, once however far; the first read and a new connection only learn.
        EventSender s;
        CHECK(!s.scan(1000,true,5u));                                // learned: five old scans are not news
        CHECK(!s.scan(1050,true,5u));
        auto e=s.scan(1100,true,6u);CHECK(e&&*decodeEvent(*e)==Kind::Scan);
        CHECK(!s.scan(1150,true,6u));
        CHECK(!s.scan(1200,true,7u));                                // inside the gap
        auto f=s.scan(1100+EventSender::MinGapMs,true,9u);CHECK(f&&f->ref!=e->ref);
        CHECK(!s.scan(5000,true,std::nullopt));CHECK(!s.scan(5050,true,12u));   // unreadable, then a new world: learned again
        CHECK(!s.scan(5100,true,3u));CHECK(s.scan(5150,true,4u));    // a lower counter is a new world too
        CHECK(!s.scan(8000,false,5u));                               // scanned while alone
        CHECK(!s.scan(8050,true,6u));                                // the partner arrives: not told what happened before
        CHECK(s.scan(8100,true,7u));
    }
    {   // The receiver bounds what is shown.
        EventReceiver r;CHECK(r.accept(Kind::Shout,5000));CHECK(!r.accept(Kind::Shout,5000+EventReceiver::MinGapMs-1));
        CHECK(r.accept(Kind::Scan,5001));                            // the kinds are bounded apart
        CHECK(r.accept(Kind::Shout,5000+EventReceiver::MinGapMs));
        EventReceiver w;unsigned shown=0;for(uint64_t t=0;t<60000;t+=100)shown+=w.accept(Kind::Scan,100000+t);
        CHECK(shown==EventReceiver::PerMinute);
    }
    const uint64_t base=0x10000,entity=base+0x1000,state=base+0x8000,manager=base+0x100,list=base+0x200,player=base+0x10000,other=base+0x400;
    std::vector<uint8_t> mem(0x12000);bool deny=false;
    mod::discovery::Sampler s{[&](uint64_t a,void* p,size_t n){if(deny||a<base||a-base>mem.size()||n>mem.size()-(a-base))return false;std::memcpy(p,mem.data()+(a-base),n);return true;},base};
    auto put=[&](uint64_t at,const void* v,size_t n){std::memcpy(mem.data()+(at-base),v,n);};
    auto put64=[&](uint64_t at,uint64_t v){put(at,&v,8);};auto put32=[&](uint64_t at,uint32_t v){put(at,&v,4);};auto putf=[&](uint64_t at,float v){put(at,&v,4);};
    {   // The scan counter: only from a state object that points back at this porter and has a plausible scan state.
        CHECK(!readScanCount(s,entity));
        put64(entity+PlayerState,state);put64(state+StateOwner,entity);put32(state+ScanState,4);put32(state+ScanCount,41);
        CHECK(readScanCount(s,entity)&&*readScanCount(s,entity)==41);
        put64(state+StateOwner,other);CHECK(!readScanCount(s,entity));put64(state+StateOwner,entity);
        put32(state+ScanState,900);CHECK(!readScanCount(s,entity));put32(state+ScanState,0);
        CHECK(!readScanCount(s,0));deny=true;CHECK(!readScanCount(s,entity));deny=false;
    }
    {   // The status block: player number zero, accepted only for this porter; a pair is read only when it makes sense.
        const uint64_t slot=base+0x50;   // the test's manager slot stands at image + PlayerManagerSlot
        uint64_t image=slot-PlayerManagerSlot;
        CHECK(!readInfo(s,image,entity).first);
        put64(slot,manager);put32(manager+0x38,2);put64(manager+0x40,list);put64(list,other);put64(list+8,player);
        put32(other+0x88,1);put32(player+0x88,0);put64(player+0x70,entity);
        putf(player+PlayerInfo+0x28,450);putf(player+PlayerInfo+0x2c,900);putf(player+PlayerInfo+0x30,30);putf(player+PlayerInfo+0x38,120);
        auto i=readInfo(s,image,entity);CHECK(i.first&&*i.first==0.5f&&i.second&&*i.second==0.25f);
        put64(player+0x70,other);CHECK(!readInfo(s,image,entity).first&&!readInfo(s,image,entity).second);put64(player+0x70,entity);
        putf(player+PlayerInfo+0x28,5000);i=readInfo(s,image,entity);CHECK(!i.first&&i.second);         // over its maximum: not a value
        putf(player+PlayerInfo+0x28,-1);CHECK(!readInfo(s,image,entity).first);putf(player+PlayerInfo+0x28,450);
        putf(player+PlayerInfo+0x38,0);CHECK(!readInfo(s,image,entity).second);putf(player+PlayerInfo+0x38,120);
        put32(manager+0x38,5000);CHECK(!readInfo(s,image,entity).first);put32(manager+0x38,2);
        deny=true;CHECK(!readInfo(s,image,entity).first);deny=false;
    }
    {   // The vitals wire.
        Vitals v;v.blood=90;v.stamina=5;v.unsteady=true;auto e=encodeVitals(v,3);CHECK(e.type==VitalsWireType&&decodeVitals(e)&&*decodeVitals(e)==v);
        Vitals u;CHECK(*decodeVitals(encodeVitals(u,4))==u);            // unknown stays unknown
        auto bad=e;bad.item=101;CHECK(!decodeVitals(bad));bad.item=50|200u<<8;CHECK(!decodeVitals(bad));bad.item=50|50u<<8|4u<<16;CHECK(!decodeVitals(bad));
        bad=e;bad.type=EventWireType;CHECK(!decodeVitals(bad));bad=e;bad.ref=1ull<<33;CHECK(!decodeVitals(bad));
    }
    {   // The sender: once per connection, then only a change of what is shown, never more often than the gap.
        VitalsSender tx;Raw r;r.blood=1.f;r.stamina=0.8f;unsigned sent=0;uint64_t t=1000;
        CHECK(!tx.tick(t,false,r));
        auto first=tx.tick(t+=50,true,r);CHECK(first&&decodeVitals(*first)->blood==100&&decodeVitals(*first)->stamina==80);
        for(int i=0;i<600;++i)sent+=bool(tx.tick(t+=50,true,r));     // half a minute of nothing new
        CHECK(sent==0);
        // Stamina trembling around a boundary (81..84 %) and blood a little under full: nothing to send.
        for(int i=0;i<600;++i){r.stamina=0.81f+0.01f*float(i%4);r.blood=0.97f;sent+=bool(tx.tick(t+=50,true,r));}
        CHECK(sent==0);
        // Stamina drains two percent a tick: the partner is told at most once every MinGapMs, and the last value arrives.
        sent=0;uint64_t began=t;std::optional<Vitals> last;
        for(int i=0;i<40;++i){r.stamina=0.8f-0.02f*float(i);if(auto e=tx.tick(t+=50,true,r)){++sent;last=decodeVitals(*e);}}
        CHECK(sent>=1&&sent<=1+(t-began)/VitalsSender::MinGapMs);
        r.stamina=0.f;for(int i=0;i<100;++i)if(auto e=tx.tick(t+=50,true,r)){++sent;last=decodeVitals(*e);}
        CHECK(last&&last->stamina==0&&last->blood==100);
        // A state must last: one off-balance frame is not told, a held one is, once.
        sent=0;r.unsteady=true;CHECK(!tx.tick(t+=50,true,r));r.unsteady=false;for(int i=0;i<100;++i)sent+=bool(tx.tick(t+=50,true,r));
        CHECK(sent==0);
        r.unsteady=true;r.overloaded=true;last.reset();for(int i=0;i<100;++i)if(auto e=tx.tick(t+=50,true,r)){++sent;last=decodeVitals(*e);}
        CHECK(sent==1&&last&&last->unsteady&&last->overloaded);
        // Unreadable values go out as unknown; a new connection gets the set again.
        r.blood.reset();sent=0;last.reset();for(int i=0;i<100;++i)if(auto e=tx.tick(t+=50,true,r)){++sent;last=decodeVitals(*e);}
        CHECK(sent==1&&last&&last->blood==Unknown);
        CHECK(!tx.tick(t+=50,false,r));CHECK(tx.tick(t+=50,true,r));CHECK(!tx.tick(t+=50,true,r));
    }
    {   // The receiver and the name tag's text.
        VitalsReceiver rx;CHECK(!rx.current(true));
        Vitals v;v.blood=90;v.stamina=60;CHECK(rx.feed(encodeVitals(v,1)));CHECK(rx.current(true)&&*rx.current(true)==v);
        auto bad=encodeVitals(v,2);bad.item=250;CHECK(!rx.feed(bad));CHECK(*rx.current(true)==v);
        CHECK(!rx.current(false));CHECK(!rx.current(true));             // a session that ended forgets him
        CHECK(label(u"Higgs",nullptr)==u"Higgs");CHECK(label(u"Higgs",&v)==u"Higgs  B90 S60");
        v.overloaded=true;v.unsteady=true;CHECK(label(u"Higgs",&v)==u"Higgs  B90 S60 heavy unsteady");
        Vitals part;part.stamina=5;CHECK(label(u"Higgs",&part)==u"Higgs  S5");
        Vitals none;CHECK(label(u"Higgs",&none)==u"Higgs");none.unsteady=true;CHECK(label(u"Higgs",&none)==u"Higgs unsteady");
        // The tag's text changes at most once every GapMs, whatever arrives; a new name at once.
        LabelPacer p;Vitals a;a.blood=100;a.stamina=100;unsigned changes=0;std::u16string was;uint64_t t=1000;
        for(int i=0;i<200;++i){a.stamina=uint8_t(100-i%100);auto now=p.text(t+=50,u"Higgs",&a);if(now!=was){++changes;was=now;}}
        CHECK(changes>=2&&changes<=1+200*50/LabelPacer::GapMs);
        a.stamina=40;CHECK(p.text(t+=LabelPacer::GapMs,u"Higgs",&a)==u"Higgs  B100 S40");
        CHECK(p.text(t+=50,u"Sam",&a)==u"Sam  B100 S40");
    }
    if(failures)return 1;
    std::puts("signals tests passed");return 0;
}
