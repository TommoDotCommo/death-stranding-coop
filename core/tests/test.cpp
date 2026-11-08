#include "../core.h"
#include "coopfuzz/net.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ds;
namespace {
Protection protection;
int failures=0;
void check(bool ok,const char* name) { if(!ok) { std::fprintf(stderr,"FAIL %s\n",name); ++failures; } }
void patch(Bytes& b,size_t offset,uint64_t value,size_t n) { for(size_t i=0;i<n;++i) b[offset+i]=uint8_t(value>>(i*8)); uint32_t h=2166136261u; for(size_t i=0;i<b.size();++i) { h^=(i>=120&&i<124)?0:b[i]; h*=16777619u; } for(int i=0;i<4;++i) b[120+i]=uint8_t(h>>(8*i)); }
struct Fake : StateProvider {
    bool ready=true;
    bool readyForSession() const override {return ready;}
    Progress progress{};
    Progress readProgress() const override {return progress;}
    State local; Render remote; bool blocked=false, readable=true; unsigned applies=0;
    bool readLocal(State& s) override { s=local; return readable; }
    bool isInCutsceneOrLoading() const override { return blocked; }
    void applyRemote(const Render& r) override { remote=r; ++applies; }
};
struct Queue : Transport { std::vector<Bytes> packets; void send(const Bytes& b) override { packets.push_back(b); } };
struct Fixture {
    Fake provider; Queue transport; Core core;
    Fixture():core({Role::Host,42,100,120},provider,transport) {
        core.setProtections(protection); core.tick(0);
        Packet p; p.role=Role::Client; p.kind=Kind::Ack; p.session=42; p.nonce=200; p.echo=100;
        auto b=encode(p); check(core.receive(b.data(),b.size(),0)==Error::None,"fixture_handshake"); transport.packets.clear();
    }
    Packet presence(uint32_t seq,uint64_t time,double x=0) { Packet p; p.role=Role::Client; p.kind=Kind::Presence; p.session=42; p.nonce=200; p.echo=100; p.sequence=seq; p.stamp=time; p.state.position[0]=x; return p; }
    Error receive(Packet p,uint64_t now) { auto b=encode(p); return core.receive(b.data(),b.size(),now); }
};
void parserTests() {
    Packet p, q; p.kind=Kind::Presence; p.state.position={-100000,100000,1.23}; p.state.velocity={-50,0,50}; p.state.yaw=-3.141592653589793; p.state.phase=1; p.state.action=31; p.state.flags=63;
    auto b=encode(p); auto parse=[&](const Bytes& wire){return decodeForTest(wire.data(),wire.size(),q,protection);};
    check(parse(b)==Error::None&&q.state.position==p.state.position&&q.state.phase==1&&q.state.action==31,"roundtrip_boundaries");
    check(b[0]=='D'&&b[1]=='S'&&b[2]=='P'&&b[3]=='1'&&b[8]==128,"little_endian_golden");
    for(size_t i=0;i<WireSize;++i) check(decodeForTest(b.data(),i,q,protection)==Error::Length,"length");
    check(decode(nullptr,WireSize,q)==Error::Length,"null_buffer");
    check(decode(b.data(),129,q)==Error::Length,"oversized_length");
    auto bad=b; patch(bad,0,0,4); check(parse(bad)==Error::Magic,"magic");
    bad=b; patch(bad,4,2,2); check(parse(bad)==Error::Version,"version");
    bad=b; patch(bad,8,127,2); check(parse(bad)==Error::Length,"length");
    bad=b; bad[80]^=4; check(parse(bad)==Error::Checksum,"checksum");
    for(size_t at:{size_t(10),size_t(95),size_t(124)}) { bad=b; patch(bad,at,1,1); check(parse(bad)==Error::Reserved,"reserved"); }
    for(int kind=1;kind<=8;++kind) { bad=b; patch(bad,6,kind,1); check(parse(bad)==Error::None,"every_kind"); }
    for(int kind:{0,10,255}) { bad=b; patch(bad,6,kind,1); check(parse(bad)==Error::Kind,"kind"); }
    bad=b; patch(bad,7,0,1); check(parse(bad)==Error::Role,"role");
    for(size_t at:{size_t(12),size_t(16)}) { bad=b; patch(bad,at,0,4); check(parse(bad)==Error::Build,"build"); }
    bad=b; patch(bad,32,0,8); check(parse(bad)==Error::Nonce,"zero_session");
    bad=b; patch(bad,40,0,8); check(parse(bad)==Error::Nonce,"zero_nonce");
    for(size_t at:{size_t(64),size_t(96),size_t(104),size_t(112)}) { bad=b; patch(bad,at,UINT64_MAX,8); check(parse(bad)==Error::Time,"clock_range"); }
    for(size_t at:{size_t(72),size_t(76),size_t(80)}) { bad=b; patch(bad,at,10000001,4); check(parse(bad)==Error::Range,"range"); }
    for(size_t at:{size_t(84),size_t(86),size_t(88)}) { bad=b; patch(bad,at,32767,2); check(parse(bad)==Error::Range,"range"); }
    for(auto field:std::vector<std::pair<size_t,uint64_t>>{{90,32768},{60,128},{62,32},{94,4}}) { bad=b; patch(bad,field.first,field.second,field.first==90||field.first==60?2:1); check(parse(bad)==Error::Range,"range"); }
    State s; s.position={-1e20,1e20,0}; s.velocity={1e20,-1e20,1e20}; s.yaw=1000; s.phase=10; s.flags=65535; s.action=255;
    check(sanitiseForTest(s,protection)&&s.position[0]==-100000&&s.position[1]==100000&&s.phase==1&&s.flags==127&&s.action==31,"clamp");
    check(std::sqrt(s.velocity[0]*s.velocity[0]+s.velocity[1]*s.velocity[1]+s.velocity[2]*s.velocity[2])<=100.00001,"clamp");
    s.phase=-1; sanitiseForTest(s,protection); check(s.phase==0,"clamp");
    for(double value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
        for(int field=0;field<8;++field) { State x; if(field<3) x.position[field]=value; else if(field<6) x.velocity[field-3]=value; else if(field==6) x.yaw=value; else x.phase=value; check(!sanitiseForTest(x,protection),"finite"); }
    }
    Packet nan; nan.state.yaw=std::nan(""); bool threw=false; try { encode(nan); } catch(const std::invalid_argument&) { threw=true; } check(threw,"nonfinite_encoder");
    for(unsigned i=0;i<16;++i) check(portForSlot(i)==49000+i,"ports");
    bool threwSlot=false; try { portForSlot(16); } catch(const std::out_of_range&) { threwSlot=true; } check(threwSlot,"port_range");
}
void cargoTests() {
    // Wire: an event survives encode/decode with its item hash (t0), type (action), position and yaw.
    { Fixture f; CargoEvent e; e.item=0x0782598d; e.position={1694.7,135.7,1056.4}; e.yaw=1.5; e.ref=0x1234567; f.core.queueCargo(e); f.core.tick(10);
      Packet q; bool sent=false; for(auto& b:f.transport.packets) if(b[6]==uint8_t(Kind::Cargo)) { sent=decode(b.data(),b.size(),q)==Error::None; }
      check(sent&&q.sequence==1&&q.t0==0x0782598d&&q.state.action==1&&q.t2==0x1234567&&std::abs(q.state.position[0]-1694.7)<.01&&std::abs(q.state.yaw-1.5)<.001,"cargo_wire");
      // Repeated until acknowledged, then never again.
      f.transport.packets.clear(); f.core.tick(100); f.core.tick(220); unsigned repeats=0; for(auto& b:f.transport.packets) repeats+=b[6]==uint8_t(Kind::Cargo);
      check(repeats==1,"cargo_repeat");
      Packet ack=f.presence(1,0); ack.kind=Kind::CargoAck; ack.sequence=2; f.receive(ack,230); check(f.core.cargoPending()==1,"cargoAck");   // wrong sequence
      ack.sequence=1; f.receive(ack,240); check(f.core.cargoPending()==0,"cargo_acked");
      f.transport.packets.clear(); f.core.tick(500); for(auto& b:f.transport.packets) check(b[6]!=uint8_t(Kind::Cargo),"cargo_acked"); }
    // Receiver: only the next sequence is taken, exactly once; repeats are acknowledged again but not delivered.
    { Fixture f; auto cargo=[&](uint32_t seq,uint32_t item){ Packet p=f.presence(seq,0); p.kind=Kind::Cargo; p.state.action=1; p.t0=item; p.t1=1000+item; p.t2=5000+item; return p; };
      f.receive(cargo(2,22),100); check(f.core.takeCargo().empty(),"cargoOrder");                       // gap: not delivered
      f.receive(cargo(1,11),110); f.receive(cargo(1,11),120); f.receive(cargo(2,22),130);
      auto got=f.core.takeCargo(); check(got.size()==2&&got[0].item==11&&got[1].item==22&&got[0].ref==5011,"cargoOrder");
      unsigned acks=0; for(auto& b:f.transport.packets) acks+=b[6]==uint8_t(Kind::CargoAck); check(acks==3,"cargo_ack_repeats");
      auto bad=cargo(3,33); bad.state.action=0; check(f.receive(bad,140)==Error::Range,"cargo_type_range"); bad.state.action=8; check(f.receive(bad,140)==Error::Range,"cargo_type_range");
      check(f.core.takeCargo().empty(),"cargo_taken_once");
      auto noid=cargo(3,33); noid.t1=0; check(f.receive(noid,150)==Error::Range,"cargo_id_required");
      auto carry=cargo(3,33); carry.state.action=6; check(f.receive(carry,160)==Error::None,"cargo_carry_type");   // what the porter carries (optional)
      auto gear=cargo(4,34); gear.state.action=7; check(f.receive(gear,170)==Error::None,"cargo_gear_type");       // the gear the porter wears (optional)
      auto carried=f.core.takeCargo(); check(carried.size()==2&&carried[0].type==6&&carried[1].type==7&&carried[1].item==34,"cargo_carry_type"); }
    // A ping (type 5) travels like any cargo event: delivered once, with its place and ref.
    { Fixture f; Packet p=f.presence(1,0); p.kind=Kind::Cargo; p.state.action=5; p.t0=0; p.t1=91; p.t2=4242; p.state.position={1,2,3};
      check(f.receive(p,100)==Error::None,"cargo_ping"); f.receive(p,110);
      auto got=f.core.takeCargo(); check(got.size()==1&&got[0].type==5&&got[0].ref==4242&&got[0].position[1]==2,"cargo_ping"); }
    // Acknowledgement lost, then a reconnection: the sender renumbers and repeats the event; it is not delivered twice.
    { Fixture f; Packet p=f.presence(1,0); p.kind=Kind::Cargo; p.state.action=1; p.t0=7; p.t1=77; f.receive(p,100);
      check(f.core.takeCargo().size()==1,"cargo_first_delivery");
      f.core.tick(3200); Packet a; a.kind=Kind::Ack; a.role=Role::Client; a.session=42; a.nonce=201; a.echo=101; f.receive(a,3300);   // new connection
      p.nonce=201; p.echo=101; p.sequence=1; check(f.receive(p,3400)==Error::None,"cargo_after_reconnect");
      check(f.core.takeCargo().empty(),"cargoOnce"); }
    // A real (random 64-bit) nonce: the sender's ids stay valid t-fields (below 2^53), distinct and nonzero.
    for(uint64_t nonce:{0xdeadbeefcafef00dull,0xffffffffffffffffull,0x8000000000000001ull,1ull}) {
      Fake provider; Queue transport; Core c({Role::Host,42,nonce,120},provider,transport); c.setProtections(protection);
      CargoEvent e; e.item=9; c.queueCargo(e); c.queueCargo(e);
      Packet ack; ack.role=Role::Client; ack.kind=Kind::Ack; ack.session=42; ack.nonce=200; ack.echo=nonce;
      c.tick(0); auto b=encode(ack); c.receive(b.data(),b.size(),0); transport.packets.clear(); c.tick(10);
      Packet q; bool ok=false; for(auto& w:transport.packets) if(w[6]==uint8_t(Kind::Cargo)) ok=decode(w.data(),w.size(),q)==Error::None&&q.t1&&q.t1<(1ull<<53);
      check(ok,"cargo_id_fits_random_nonce"); }
    // A new connection renumbers undelivered events from 1.
    { Fixture f; CargoEvent e; e.item=5; f.core.queueCargo(e); f.core.queueCargo(e); f.core.tick(3001); check(!f.core.connected()&&f.core.cargoPending()==2,"cargo_kept_on_reset"); }
}
void progressTests() {
    auto send=[](Core& core,Packet p,uint64_t now=0){auto b=encode(p);return core.receive(b.data(),b.size(),now);};
    {Fake f;f.ready=false;Queue q;Core c({Role::Host,42,100,120},f,q);c.setProtections(protection);c.tick(0);
        check(q.packets.empty(),"progressReady");Packet p;p.kind=Kind::Hello;p.role=Role::Client;p.session=42;
        check(send(c,p)==Error::NotConnected,"progressReady");f.ready=true;c.tick(100);check(!q.packets.empty(),"progress_ready_recovery");}
    Packet hello;hello.role=Role::Client;hello.session=42;hello.nonce=200;hello.progress={2,11};
    for(auto kind:{Kind::Hello,Kind::Ack}) {
        auto p=hello;p.kind=kind;p.progressPolicy=ProgressPolicy::Refuse;
        auto wire=encode(p);Packet q;
        check(decode(wire.data(),wire.size(),q)==Error::None&&q.progress.episode==2&&q.progress.id==11&&q.progressPolicy==ProgressPolicy::Refuse,"progress_roundtrip");
        patch(wire,104,2,1);check(decode(wire.data(),wire.size(),q)==Error::Range,"progress_policy_range");
        wire=encode(p);patch(wire,105,1,1);check(decode(wire.data(),wire.size(),q)==Error::Reserved,"progress_reserved");
    }
    check(compareProgress({2,11},{2,11})==ProgressStatus::Equal,"progress_equal");
    check(compareProgress({2,UnknownProgress},{3,UnknownProgress})==ProgressStatus::Mismatch,"progress_partial_mismatch");
    check(compareProgress({2,UnknownProgress},{2,11})==ProgressStatus::Unknown,"progress_partial_unknown");
    for(bool peerRefuses:{false,true})for(auto remote:{Progress{3,11},Progress{2,12}}) {
        Fake f;f.progress={2,11};Queue q;Config cfg{Role::Host,42,100,120};
        cfg.progressPolicy=peerRefuses?ProgressPolicy::Warn:ProgressPolicy::Refuse;
        Core c(cfg,f,q);c.setProtections(protection);c.tick(0);
        auto p=hello;p.progress=remote;p.progressPolicy=peerRefuses?ProgressPolicy::Refuse:ProgressPolicy::Warn;
        auto before=q.packets.size();
        check(send(c,p)==Error::Progress&&q.packets.size()==before,"progress");
        if(peerRefuses)check(send(c,p)==Error::Progress,"progressPolicy");
        p.kind=Kind::Ack;p.echo=100;
        check(send(c,p)==Error::Progress&&!c.connected(),"progress");
        if(peerRefuses)check(!c.connected(),"progressPolicy");
    }
    for(auto local:{Progress{},Progress{2,11}})for(auto remote:{Progress{},Progress{2,UnknownProgress}}) {
        Fake f;f.progress=local;Queue q;Core c({Role::Host,42,100,120,ProgressPolicy::Refuse},f,q);c.setProtections(protection);c.tick(0);
        auto p=hello;p.progress=remote;p.kind=Kind::Ack;p.echo=100;
        check(send(c,p)==Error::None&&c.connected()&&c.progressStatus()==ProgressStatus::Unknown,"progressUnknown");
    }
    Fake f;f.progress={2,11};Queue q;Core c({Role::Host,42,100,120},f,q);c.setProtections(protection);c.tick(0);
    auto p=hello;p.kind=Kind::Ack;p.echo=100;p.progress={3,11};
    check(send(c,p)==Error::None&&c.connected()&&c.progressStatus()==ProgressStatus::Mismatch,"progress_warn");
    check(c.peerProgress().episode==3&&c.localProgress().id==11&&c.connectionSerial()==1,"progress_metadata");
    send(c,p);check(c.connectionSerial()==1,"progress_duplicate_ack");
    q.packets.clear();c.tick(100);bool flagged=false;
    for(auto& b:q.packets){Packet out;decode(b.data(),b.size(),out);if(out.kind==Kind::Presence)flagged=(out.state.flags&ProgressMismatch)!=0;}
    check(flagged,"progressFlag");
    p.kind=Kind::Presence;p.stamp=100;p.sequence=1;
    check(send(c,p,100)==Error::None&&c.render(100).visible&&(c.render(100).state.flags&ProgressMismatch),"progressFlag");
    c.disconnect(200);check(c.progressStatus()==ProgressStatus::Unknown,"progress_reset");
}
void sessionTests() {
    { Fixture f;f.core.tick(10);f.core.tick(110);
      auto p=f.presence(0,30);p.kind=Kind::Pong;p.t0=10;p.t1=p.t2=30;
      check(f.receive(p,130)==Error::None&&f.core.rttMs()==120,"pingHistory");
      check(f.receive(p,140)==Error::Time,"ping"); }

    { Fixture f; auto p=f.presence(1,100); ++p.session; check(f.receive(p,100)==Error::Session,"session"); }
    { Fixture f; auto p=f.presence(1,100); ++p.nonce; check(f.receive(p,100)==Error::Nonce,"nonce"); p.nonce=200; ++p.echo; check(f.receive(p,100)==Error::Nonce,"nonce"); }
    { Fixture f; check(f.receive(f.presence(1,100),100)==Error::None,"first_presence"); check(f.receive(f.presence(1,200),200)==Error::Duplicate,"duplicate"); check(f.receive(f.presence(0,250),250)==Error::Duplicate,"duplicate"); }
    { Fixture f; f.receive(f.presence(UINT32_MAX,100),100); check(f.receive(f.presence(0,150),150)==Error::None,"sequence_wrap"); }
    { Fixture f; f.receive(f.presence(1,100),100); check(f.receive(f.presence(2,100),200)==Error::Time,"time"); }
    { Fixture f; f.receive(f.presence(1,100),100); check(f.receive(f.presence(2,150,1000),150)==Error::Speed,"speed"); }
    { Fixture f; auto p=f.presence(1,100,1000); p.state.flags=Teleport; check(f.receive(p,100)==Error::None,"first_teleport"); p.sequence=2; p.stamp=200; p.state.position[0]=2000; check(f.receive(p,200)==Error::Teleport,"teleport"); p.sequence=3; p.stamp=5100; check(f.receive(p,5100)==Error::None,"teleport_recovery"); }
    { Fixture f; f.receive(f.presence(1,100,0),100); f.receive(f.presence(2,200,1),200); check(std::abs(f.core.render(270).state.position[0]-.5)<.001,"delay"); }
    // Ledge or fall: 2 m in one 100 ms interval holds the old height, then snaps at the new sample time.
    { Fixture f; f.receive(f.presence(1,100,0),100); auto p=f.presence(2,200,1); p.state.position[1]=2.0; f.receive(p,200); check(f.core.render(270).state.position[1]==0,"floor"); check(f.core.render(320).state.position[1]==2.0,"floor_boundary"); }
    // Lift or jump pad: 0.9 m in 100 ms (9 m/s vertical) also snaps.
    { Fixture f; f.receive(f.presence(1,100,0),100); auto p=f.presence(2,200,1); p.state.position[1]=0.9; f.receive(p,200); auto r=f.core.render(270); check(r.state.position[1]==0&&r.snapped,"floor"); }
    // Slope: 0.25 m in 100 ms (2.5 m/s) blends; halfway through the interval the height is halfway and nothing snapped.
    { Fixture f; f.receive(f.presence(1,100,0),100); auto p=f.presence(2,200,1); p.state.position[1]=0.25; f.receive(p,200); auto r=f.core.render(270); check(std::abs(r.state.position[1]-0.125)<0.01&&!r.snapped,"floor_slope"); }
    { Fixture f; auto p=f.presence(1,100); p.state.velocity[0]=10; f.receive(p,100); check(std::abs(f.core.render(1120).state.position[0]-1.5)<.001,"extrapolate"); check(f.core.render(2000).state.position[0]==f.core.render(3000).state.position[0],"freeze"); check(!f.core.render(2000).visible&&f.core.render(800).stale,"stale"); }
    { Fixture f; for(unsigned i=1;i<=100;++i) f.receive(f.presence(i,i*50),i*50); check(f.core.buffered()<=64,"buffer"); }
    { Fixture f; f.provider.blocked=true; f.core.tick(100); unsigned count=0; for(auto& b:f.transport.packets) count+=b[6]==uint8_t(Kind::Presence); check(count==0,"gate"); f.core.frame(100); check(!f.provider.remote.visible,"apply_gate"); }
    { Fixture f; for(unsigned t=0;t<500;++t) { f.provider.local.position[0]=t*.001; f.core.tick(t); } unsigned count=0; for(auto& b:f.transport.packets) count+=b[6]==uint8_t(Kind::Presence); check(count<=10&&count>=2,"rate"); }
    { Fixture f; f.core.tick(3001); check(!f.core.connected(),"timeout"); Packet p; p.kind=Kind::Ack; p.role=Role::Client; p.session=42; p.nonce=201; p.echo=101; check(f.receive(p,3100)==Error::None&&f.core.connected(),"reconnect"); check(f.receive(f.presence(1,3200),3200)==Error::Nonce,"old_connection_replay"); }
    { Fixture f; auto p=f.presence(1,100); p.kind=Kind::Bye; f.receive(p,100); check(!f.core.connected()&&f.core.buffered()==0,"bye"); }
    { Fixture f; f.core.tick(100); Packet p; p.kind=Kind::Pong; p.role=Role::Client; p.session=42; p.nonce=200; p.echo=100; p.t0=100; p.t1=160; p.t2=170;
      check(f.receive(p,130)==Error::None&&f.core.rttMs()==20&&f.core.clockOffsetMs()==50,"ping_clock"); check(f.receive(p,140)==Error::Time,"ping"); }
    { Fixture f; Packet p=f.presence(1,100); p.kind=Kind::Hello; p.nonce=300; check(f.receive(p,100)==Error::Nonce,"hello_cannot_replace_peer"); }
    { Fake fake; Queue queue; Core c({Role::Host,1,10,120},fake,queue); c.setProtections(protection); Packet p; p.kind=Kind::Presence; p.role=Role::Client; p.nonce=20; p.echo=10; auto b=encode(p); check(c.receive(b.data(),b.size(),0)==Error::NotConnected,"pre_handshake_presence"); p.kind=Kind::Ack; b=encode(p); check(c.receive(b.data(),b.size(),0)==Error::NotConnected,"handshake"); }
    // Public pure function: teleports are held then snapped, no blend or post-teleport extrapolation.
    std::deque<Snapshot> b{{State{},100,1},{State{},200,2}}; b[1].state.position[0]=1000; b[1].state.flags=Teleport; b[1].state.velocity[0]=50;
    check(sample(b,270).state.position[0]==0&&sample(b,320).state.position[0]==1000&&sample(b,800).state.position[0]==1000,"teleport_sampling");
    b[1].state.flags=PrivateRoom; check(!sample(b,300).visible,"private_room");
    check(!sample({},10).visible,"empty_buffer");
}
struct NetworkTransport : Transport { coopfuzz::LossyLink& link; int from; NetworkTransport(coopfuzz::LossyLink& l,int f):link(l),from(f){} void send(const Bytes& b) override { link.send(from,1-from,{b.begin(),b.end()}); } };
State trajectory(uint64_t time,int side,uint64_t seed) {
    State s; double t=time/1000., phase=double(seed%97)*.03+side;
    // Acceleration < 5 m/s^2, horizontal motion <= 7 m/s, same floor. Stop after 12 seconds.
    double u=std::min(t,12.); s.position={20*std::sin(.3*u+phase),0,10*std::cos(.4*u+phase)};
    if(t<12) s.velocity={6*std::cos(.3*u+phase),0,-4*std::sin(.4*u+phase)};
    s.yaw=std::sin(u); s.phase=std::fmod(u,1.); return s;
}
void fuzz() {
    std::vector<double> errors; double finalMax=0; uint64_t accepted=0, drops=0, duplicates=0;
    for(uint64_t seed=1;seed<=500;++seed) {
        coopfuzz::LossyLink link(2,seed); link.setLink({20,150,.10,.05,0});
        Fake a,b; a.progress={uint32_t(seed%15),uint32_t(seed)};
        b.progress=seed%3==0?Progress{}:Progress{uint32_t(seed%15+(seed%3==1)),uint32_t(seed)};
        auto wantProgress=compareProgress(a.progress,b.progress);
        NetworkTransport at(link,0),bt(link,1); Core ac({Role::Host,seed+100,seed*10+1,120},a,at),bc({Role::Client,seed+100,seed*10+2,120},b,bt);
        std::vector<uint32_t> delivered;
        const uint64_t clocks[2]={10000,20000+seed*7};
        for(uint64_t now=0;now<=16000;now+=10) {
            // Seeded outage followed by healing exercises timeout/reconnect; no metrics collected in outage/warmup.
            if(now==6000) link.setConnected(1,false);
            if(now==9500) link.setConnected(1,true);
            link.advance(now); int from; coopfuzz::Bytes wire;
            for(int side=0;side<2;++side) while(link.poll(side,from,wire)) (side?bc:ac).receive(wire.data(),wire.size(),now+clocks[side]);
            a.local=trajectory(now,0,seed); b.local=trajectory(now,1,seed);
            if(now%500==0&&now>=1000&&now<5000) { CargoEvent e; e.item=uint32_t(now); ac.queueCargo(e); }
            ac.tick(now+clocks[0]); bc.tick(now+clocks[1]); ac.frame(now+clocks[0]); bc.frame(now+clocks[1]);
            for(auto& e:bc.takeCargo()) delivered.push_back(e.item);
            if((now>=2500&&now<5900)||(now>=11500&&now<12000)) {
                for(int side=0;side<2;++side) {
                    Render r=(side?bc:ac).render(now+clocks[side]); State truth=trajectory(now>120?now-120:0,1-side,seed);
                    check(r.visible,"fuzz_visible"); double error=0; for(int k=0;k<3;++k) error+=std::pow(r.state.position[k]-truth.position[k],2); errors.push_back(std::sqrt(error));
                    check(error<4.,"fuzz_convergence_bound_2m");
                }
            }
        }
        check(ac.connected()&&bc.connected(),"fuzz_reconnected");
        check(ac.progressStatus()==wantProgress&&bc.progressStatus()==wantProgress,"fuzz_progress_handshake");
        check(bool(ac.render(16000+clocks[0]).state.flags&ProgressMismatch)==(wantProgress==ProgressStatus::Mismatch),"fuzz_progress_flag");
        // Eight events queued before the outage: all delivered, in order, none twice (the outage may cut them off
        // only if they were still pending; then they are renumbered and delivered after reconnection).
        { std::vector<uint32_t> want; for(uint32_t t=1000;t<5000;t+=500) want.push_back(t); check(delivered==want,"fuzz_cargo_exactly_once"); }
        check(ac.rttMs()>=0&&bc.rttMs()>=0,"fuzz_rtt");
        check(std::abs(ac.clockOffsetMs()-double(clocks[1]-clocks[0]))<151,"fuzz_clock_offset");
        for(int side=0;side<2;++side) { auto r=(side?bc:ac).render(16000+clocks[side]); auto truth=trajectory(16000,1-side,seed); double error=0; for(int k=0;k<3;++k) error+=std::pow(r.state.position[k]-truth.position[k],2); finalMax=std::max(finalMax,std::sqrt(error)); check(error<.001,"fuzz_final_convergence"); }
        accepted+=ac.accepted()+bc.accepted(); drops+=link.stats().lost; duplicates+=link.stats().duplicated;
        // Mutate arbitrary length and bytes under ASan/UBSan, preserving output on error.
        coopfuzz::Rng rng(seed*991); for(int i=0;i<300;++i) { Bytes random; for(auto& x:random) x=uint8_t(rng.u32()); Packet p; decode(random.data(),rng.range(0,128),p);
            auto structured=encode(Packet{}); patch(structured,rng.range(0,119),rng.next(),1); decode(structured.data(),structured.size(),p); }
    }
    std::sort(errors.begin(),errors.end()); auto pct=[&](double p){ return errors[size_t(p*(errors.size()-1))]; };
    std::printf("FUZZ seeds=1..500 sessions=500 samples=%zu loss=10%% duplicate=5%% latency=20..150ms delay=120ms\n",errors.size());
    std::printf("ERROR metres delayed_truth p50=%.6f p95=%.6f p99=%.6f max=%.6f final_max=%.6f accepted=%llu lost=%llu duplicated=%llu\n",pct(.5),pct(.95),pct(.99),errors.back(),finalMax,(unsigned long long)accepted,(unsigned long long)drops,(unsigned long long)duplicates);
    check(pct(.99)<.8,"fuzz_p99_bound_0_8m");
}
}
int main(int argc,char** argv) {
    auto start=std::chrono::steady_clock::now(); std::string mutant=argc>1?argv[1]:"";
#define MUTATE(name) if(mutant==#name) protection.name=false
    MUTATE(version); MUTATE(range); MUTATE(teleport); MUTATE(duplicate); MUTATE(delay); MUTATE(session); MUTATE(build);
    MUTATE(magic); MUTATE(checksum); MUTATE(reserved); MUTATE(nonce); MUTATE(time); MUTATE(speed); MUTATE(floor); MUTATE(extrapolate); MUTATE(stale); MUTATE(gate); MUTATE(timeout); MUTATE(buffer); MUTATE(rate); MUTATE(length); MUTATE(kind); MUTATE(role); MUTATE(handshake); MUTATE(ping); MUTATE(pingHistory); MUTATE(clamp); MUTATE(finite); MUTATE(cargoOrder); MUTATE(cargoAck); MUTATE(cargoOnce); MUTATE(progressReady); MUTATE(progress); MUTATE(progressUnknown); MUTATE(progressFlag); MUTATE(progressPolicy);
#undef MUTATE
    parserTests(); cargoTests(); progressTests(); sessionTests(); if(mutant.empty()&&failures==0) fuzz();
    std::printf("%s failures=%d runtime=%.3fs\n",failures?"FAILED":"PASSED",failures,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    return failures?1:0;
}
