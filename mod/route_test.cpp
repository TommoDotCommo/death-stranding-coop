// Route challenges (route.h, docs/ROUTES.md): geometry, the checker, recording, the DSR1 string with a fuzzed parser,
// and the DST1 track the server verifies. Every check has a MUTANT_ROUTE_* build that must fail here.
#include "route.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
using namespace mod::route;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

static Route straight(unsigned width=10,double to=300){
    Route r;r.name="Straight";r.author="Ann";r.rules.width=width;r.points={{0,0,0},{to/2,0,0},{to,0,0}};return r;}
struct Walk {
    Checker c;std::vector<Event> events;uint64_t ms=1000;
    explicit Walk(const Route& r):c(r){}
    void at(double x,double z,double y=0,bool vehicle=false,std::optional<unsigned> cases=std::nullopt,bool teleport=false){
        Sample s;s.ms=ms;s.at={x,y,z};s.vehicle=vehicle;s.cases=cases;s.teleport=teleport;
        for(auto& e:c.feed(s))events.push_back(e);}
    // Walk in a straight line at `speed` m/s, a sample every 100 ms (the end included).
    void go(double x0,double z0,double x1,double z1,double speed,bool vehicle=false,std::optional<unsigned> cases=std::nullopt){
        double d=std::hypot(x1-x0,z1-z0);int n=std::max(1,int(std::ceil(d/(speed*0.1))));
        for(int i=1;i<=n;++i){ms+=100;at(x0+(x1-x0)*i/n,z0+(z1-z0)*i/n,0,vehicle,cases);}}
    void wait(double x,double z,uint64_t forMs,std::optional<unsigned> cases=std::nullopt){for(uint64_t t=0;t<forMs;t+=100){ms+=100;at(x,z,0,false,cases);}}
    bool has(Kind k) const {for(auto& e:events)if(e.kind==k)return true;return false;}
};

static void geometry(){
    // Slopes: the corridor is flat. A route climbing 150 m and down again, the porter 300 m up and 2 m aside: inside.
    Route r;r.name="Mountain";r.rules.width=5;r.points={{0,0,0},{100,150,0},{200,0,0}};
    CHECK(inCorridor(r,{100,300,2}));
    CHECK(inCorridor(r,{50,0,2.4}));
    CHECK(!inCorridor(r,{100,150,2.6}));
    // Corners: the outside of a corner is rounded, the inside covered by both legs.
    Route c;c.name="Corner";c.rules.width=6;c.rules.startRadius=2;c.rules.finishRadius=2;c.points={{0,0,0},{100,0,0},{100,0,100}};
    CHECK(inCorridor(c,{102,0,-2}));        // 2.83 m from the corner
    CHECK(!inCorridor(c,{103,0,-3}));       // 4.24 m
    CHECK(inCorridor(c,{97,0,3}));
    CHECK(inCorridor(c,{50,0,2.9}));
    CHECK(!inCorridor(c,{50,0,3.1}));
    CHECK(inCorridor(c,{103,0,50}));
    CHECK(!inCorridor(c,{103.2,0,50}));
    // The zones count as inside, even where the line does not reach.
    Route z=straight(2);z.rules.startRadius=20;z.rules.finishRadius=5;
    CHECK(inCorridor(z,{-15,0,0}));
    CHECK(inCorridor(z,{304,0,0}));
    CHECK(!inCorridor(z,{306,0,0}));
    // Nearest within an arc window.
    auto n=nearest(c.points,{100,0,100},0,60);CHECK(std::fabs(n.along-60)<1e-9&&std::fabs(n.distance-std::hypot(40.0,100.0))<1e-6);
    CHECK(std::fabs(length(c.points)-200)<1e-9);
}

static void runs(){
    {   // Inside all the way: ready, started, finished; the clock runs from leaving the start zone.
        Walk w(straight());w.wait(0,0,500);w.go(0,0,300,0,5);
        CHECK(w.has(Kind::Ready)&&w.has(Kind::Started)&&w.has(Kind::Finished));
        CHECK(w.c.phase()==Phase::Finished);
        CHECK(std::llabs(int64_t(w.c.result())-55900)<=100);
        CHECK(!w.events.empty()&&w.events.back().text=="Straight finished in 0:55.9.");   // out at x=10.5, in at x=290
    }
    {   // A short route whose zones nearly meet (live 2026-11-18: 26 m with a corner): leaving the start zone and walking
        // into the finish zone off the route is no finish; following it is.
        Route r;r.name="Corner";r.author="Ann";r.rules.width=10;r.points={{0,0,0},{13,0,0},{13,0,13}};
        Walk w(r);w.wait(0,0,300);w.go(0,0,10.5,0,3);w.go(10.5,0,10.5,-6,3);w.go(10.5,-6,8,4,3);
        CHECK(!w.has(Kind::Finished));
        Walk v(r);v.wait(0,0,300);v.go(0,0,13,0,3);v.go(13,0,13,13,3);
        CHECK(v.has(Kind::Finished));
    }
    {   // Before the start: told where it is; a sample outside the zone does not start it.
        Walk w(straight());w.at(-200,0);
        CHECK(w.c.phase()==Phase::Away&&w.has(Kind::Away)&&w.events[0].text=="Challenge Straight: go to the start (200 m).");
        w.go(-200,0,50,0,5);CHECK(w.c.phase()==Phase::Running);
    }
    {   // Out for 3 s twice, within a 5 s grace each time: back in, finished.
        Walk w(straight());w.wait(0,0,200);w.go(0,0,100,0,5);
        w.go(100,0,102,8,5);w.wait(102,8,2500);w.go(102,8,104,0,5);
        CHECK(w.has(Kind::Leaving)&&w.has(Kind::Back));
        w.go(104,0,200,0,5);
        w.go(200,0,202,8,5);w.wait(202,8,2500);w.go(202,8,204,0,5);
        w.go(204,0,300,0,5);
        if(w.c.phase()!=Phase::Finished)std::fprintf(stderr,"grace: %s %s\n",phaseName(w.c.phase()),w.c.reason().c_str());
        CHECK(w.c.phase()==Phase::Finished);
    }
    {   // Out for longer than the grace: failed.
        Walk w(straight());w.wait(0,0,200);w.go(0,0,100,0,5);w.go(100,0,100,20,5);w.wait(100,20,6000);
        CHECK(w.c.phase()==Phase::Failed&&w.c.reason()=="left the corridor");
        CHECK(w.events.back().text=="Straight failed: left the corridor.");
        // The leaving notice says where the corridor is: the porter faces +x (yaw pi/2), the corridor is at -z: right.
        bool told=false;for(auto& e:w.events)if(e.kind==Kind::Leaving)told=e.text.find("Leaving the corridor: 5 s. Corridor: ")==0;
        CHECK(told);
    }
    {   Route r=straight();r.rules.grace=0;Walk w(r);w.wait(0,0,200);w.go(0,0,100,0,5);w.go(100,0,100,8,5);
        CHECK(w.c.phase()==Phase::Failed&&w.c.reason()=="left the corridor");}
    {   // Left and right from the porter's facing (x east, z north; yaw from the forward vector's x over z).
        Route r=straight();r.rules.grace=60;Checker c(r);
        Sample s;s.ms=1;s.at={0,0,0};c.feed(s);s.ms=2000;s.at={20,0,0};c.feed(s);
        s.ms=4000;s.at={40,0,20};s.yaw=std::atan2(1.0,0.0);auto e=c.feed(s);   // facing +x, the corridor at -z: to the right
        CHECK(e.size()==1&&e[0].text=="Leaving the corridor: 60 s. Corridor: 15 m to your right.");
        s.ms=6000;s.at={40,0,0};c.feed(s);s.ms=8000;s.at={40,0,-20};s.yaw=std::atan2(1.0,0.0);e=c.feed(s);
        CHECK(e.size()==1&&e[0].text.find("to your left.")!=std::string::npos);
        s.ms=10000;s.at={40,0,0};c.feed(s);s.ms=12000;s.at={40,0,-20};s.yaw=std::atan2(0.0,1.0);e=c.feed(s);   // facing +z: ahead
        CHECK(e.size()==1&&e[0].text.find("ahead of you.")!=std::string::npos);
    }
    {   // Teleports: a 40 m jump in 0.1 s; a steady 70 m/s; the provider's flag.
        Walk w(straight());w.wait(0,0,200);w.go(0,0,100,0,5);w.ms+=100;w.at(140,0);
        CHECK(w.c.phase()==Phase::Failed&&w.c.reason()=="teleported");
        Walk v(straight());v.wait(0,0,200);v.go(0,0,300,0,70);
        CHECK(v.c.phase()==Phase::Failed&&v.c.reason()=="teleported");
        Walk f(straight());f.wait(0,0,200);f.go(0,0,100,0,5);f.ms+=100;f.at(100.5,0,0,false,std::nullopt,true);
        CHECK(f.c.phase()==Phase::Failed&&f.c.reason()=="teleported");
        // Fast travel to the start is fine: no run yet.
        Walk t(straight());t.at(-5000,0);t.ms+=100;t.at(0,0);t.wait(0,0,200);t.go(0,0,300,0,5);
        CHECK(t.c.phase()==Phase::Finished);
    }
    {   // No vehicles: riding fails; 20 m/s on foot fails; both are fine without the rule.
        Route r=straight();r.rules.noVehicles=true;
        Walk a(r);a.wait(0,0,200);a.go(0,0,300,0,5,true);CHECK(a.c.phase()==Phase::Failed&&a.c.reason()=="rode a vehicle");
        Walk b(r);b.wait(0,0,200);b.go(0,0,300,0,20);CHECK(b.c.phase()==Phase::Failed&&b.c.reason()=="too fast on foot");
        Walk c(r);c.wait(0,0,200);c.go(0,0,300,0,7);CHECK(c.c.phase()==Phase::Finished);
        Walk d(straight());d.wait(0,0,200);d.go(0,0,300,0,20,true);CHECK(d.c.phase()==Phase::Finished);
    }
    {   // A time limit.
        Route r=straight();r.rules.timeLimit=30;
        Walk a(r);a.wait(0,0,200);a.go(0,0,300,0,5);CHECK(a.c.phase()==Phase::Failed&&a.c.reason()=="out of time");
        Walk b(r);b.wait(0,0,200);b.go(0,0,300,0,10);CHECK(b.c.phase()==Phase::Finished);
    }
    {   // Cases: the start waits for them; putting them down for longer than the grace fails; unreadable counts as neither.
        Route r=straight();r.rules.minCases=2;
        Walk a(r);a.wait(0,0,500,1u);CHECK(a.c.phase()==Phase::Away&&a.has(Kind::Wait)&&!a.has(Kind::Ready));
        a.wait(0,0,200,2u);CHECK(a.c.phase()==Phase::Ready);
        a.go(0,0,100,0,5,false,2u);a.wait(100,0,6000,0u);CHECK(a.c.phase()==Phase::Failed&&a.c.reason()=="put the cargo down");
        Walk b(r);b.wait(0,0,200,3u);b.go(0,0,100,0,5,false,3u);b.wait(100,0,3000,1u);b.go(100,0,300,0,5,false,2u);
        CHECK(b.c.phase()==Phase::Finished&&b.c.casesVerified());
        Walk c(r);c.wait(0,0,200);c.go(0,0,300,0,5);CHECK(c.c.phase()==Phase::Finished&&!c.c.casesVerified());
    }
    {   // Progress: a loop that ends 8 m from its start cannot be finished by stepping across.
        Route r;r.name="Loop";r.rules.width=10;r.rules.startRadius=5;r.rules.finishRadius=5;
        r.points={{0,0,0},{100,0,0},{100,0,50},{0,0,50},{0,0,8}};
        Walk a(r);a.wait(0,0,200);a.go(0,0,6,0,3);a.go(6,0,1,8,3);a.wait(1,8,500);
        CHECK(a.c.phase()==Phase::Running);
        a.go(1,8,6,0,3);a.go(6,0,100,0,5);a.go(100,0,100,50,5);a.go(100,50,0,50,5);a.go(0,50,0,8,5);
        CHECK(a.c.phase()==Phase::Finished);
    }
    {   // Progress is searched ahead of what was reached: where a later leg runs beside an earlier one, the porter cannot
        // jump to it. Out 200 m, back beside it 6 m over, then north to the finish: going north at once does not finish.
        Route r;r.name="Hairpin";r.rules.width=10;r.rules.finishRadius=10;
        r.points={{0,0,0},{200,0,0},{200,0,6},{0,0,6},{0,0,100}};
        Walk a(r);a.wait(0,0,200);a.go(0,0,0,95,5);
        CHECK(a.c.phase()!=Phase::Finished);
        Walk b(r);b.wait(0,0,200);b.go(0,0,200,0,5);b.go(200,0,200,6,5);b.go(200,6,0,6,5);b.go(0,6,0,95,5);
        CHECK(b.c.phase()==Phase::Finished);
    }
}

// The bytes of a challenge, field by field, with a right CRC: for the checks the CRC cannot make.
struct Raw {
    std::vector<uint8_t> b;
    void u8(unsigned v){b.push_back(uint8_t(v));}
    void u16(unsigned v){u8(v&0xff);u8(v>>8);}
    void u32(uint32_t v){for(int i=0;i<4;++i)u8((v>>(8*i))&0xff);}
    void text(const std::string& s){u8(unsigned(s.size()));for(char c:s)u8(uint8_t(c));}
    void var(uint64_t z){do{uint8_t c=z&0x7f;z>>=7;if(z)c|=0x80;b.push_back(c);}while(z);}
    void zig(int64_t v){var(v<0?(uint64_t(-(v+1))<<1)|1:uint64_t(v)<<1);}
    std::string done(){u32(crc32(b.data(),b.size()));return "DSR1-"+base32(b);}
};
static Raw header(unsigned width=10,const std::string& name="Raw",unsigned flags=0,unsigned version=1){
    Raw r;r.u8(version);r.u8(flags);r.u8(width);r.u8(10);r.u8(10);r.u8(5);r.u8(0);r.u16(0);r.text(name);r.text("");r.u32(100);r.u32(0);r.u32(uint32_t(-50));return r;}

static void format(){
    Route r;r.name="Over the mountain";r.author="Ann";r.rules.width=5;r.rules.grace=3;r.rules.noVehicles=true;r.rules.minCases=2;r.rules.timeLimit=1200;
    r.points={{1234.56,87.3,-4321.09},{1300.1,120.2,-4300},{1400.77,240.9,-4100.3},{1500,10,-4000}};
    std::string s=encode(r);
    CHECK(s.compare(0,5,"DSR1-")==0);
    std::string why;auto d=decode(s,&why);
    CHECK(d&&d->name==r.name&&d->author==r.author&&d->rules==r.rules&&d->points.size()==4);
    if(d)for(size_t i=0;i<4;++i)CHECK(std::fabs(d->points[i].x-r.points[i].x)<=0.2&&std::fabs(d->points[i].z-r.points[i].z)<=0.2&&std::fabs(d->points[i].y-r.points[i].y)<=0.35);
    if(d)CHECK(encode(*d)==s);                        // a decoded route encodes to the same string
    CHECK(id(s)!=0&&d&&id(encode(*d))==id(s));
    // Lower case, I/L for 1, O for 0, spaces and missing dashes read the same.
    std::string lower;for(char c:s)lower+=char(c>='A'&&c<='Z'?c+32:c);CHECK(decode(lower).has_value());
    std::string alias=s;for(size_t i=5;i<alias.size();++i){if(alias[i]=='1')alias[i]='L';else if(alias[i]=='0')alias[i]='O';}CHECK(decode(alias).has_value());
    std::string squeezed;for(char c:s)if(c!='-'||squeezed.size()<5)squeezed+=c;CHECK(decode(squeezed).has_value());
    CHECK(decode("  "+s+"\n").has_value());
    // Refusals with a reason.
    CHECK(!decode("DSG-ABCDE",&why)&&why.find("not a challenge")!=std::string::npos);
    CHECK(!decode("DST1-ABCDE",&why)&&why.find("track")!=std::string::npos);
    CHECK(!decode(s.substr(0,s.size()-3),&why)&&why.find("incomplete")!=std::string::npos);
    // Every truncation, and every change of one character to another value, is refused.
    for(size_t n=5;n<s.size();++n)CHECK(!decode(s.substr(0,n)));
    const std::string alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    size_t refused=0,tried=0;
    for(size_t i=5;i<s.size();++i){if(s[i]=='-')continue;
        for(char c:alphabet){if(c==s[i])continue;std::string t=s;t[i]=c;++tried;if(!decode(t))++refused;}}
    CHECK(refused==tried);
    // Encode refuses what decode would.
    auto refusedEncode=[](Route x){try{encode(x);return false;}catch(const std::invalid_argument&){return true;}};
    Route bad=r;bad.rules.width=1;CHECK(refusedEncode(bad));
    bad=r;bad.rules.width=101;CHECK(refusedEncode(bad));
    bad=r;bad.name="";CHECK(refusedEncode(bad));
    bad=r;bad.name=" padded";CHECK(refusedEncode(bad));
    bad=r;bad.name=std::string(33,'a');CHECK(refusedEncode(bad));
    bad=r;bad.author="tab\there";CHECK(refusedEncode(bad));
    bad=r;bad.points.resize(1);CHECK(refusedEncode(bad));
    bad=r;bad.points={{0,0,0},{0.05,0,0}};CHECK(refusedEncode(bad));   // the same point once quantised
    bad=r;bad.points={{0,0,0},{150000,0,0}};CHECK(refusedEncode(bad));
    bad=r;bad.rules.grace=61;CHECK(refusedEncode(bad));
    bad=r;bad.rules.minCases=41;CHECK(refusedEncode(bad));
    bad=r;bad.rules.timeLimit=43201;CHECK(refusedEncode(bad));
    bad=r;bad.rules.startRadius=1;CHECK(refusedEncode(bad));
    bad=r;bad.rules.finishRadius=51;CHECK(refusedEncode(bad));
    // 300 points fit in the cap; 301 do not encode.
    Route many;many.name="Many";for(int i=0;i<300;++i)many.points.push_back({i*37.0,double(i%7),double((i*i)%113)});
    std::string m=encode(many);CHECK(m.size()<4000&&decode(m).has_value());
    many.points.push_back({1,1,1});CHECK(refusedEncode(many));
    // Raw bytes with a right CRC.
    {Raw x=header();x.u16(2);x.zig(40);x.zig(0);x.zig(0);CHECK(decode(x.done()).has_value());}
    {Raw x=header(0);x.u16(2);x.zig(40);x.zig(0);x.zig(0);CHECK(!decode(x.done()));}
    {Raw x=header(10,"bell\x01");x.u16(2);x.zig(40);x.zig(0);x.zig(0);CHECK(!decode(x.done()));}
    {Raw x=header(10,"Raw",2);x.u16(2);x.zig(40);x.zig(0);x.zig(0);CHECK(!decode(x.done(),&why)&&why.find("rule")!=std::string::npos);}
    {Raw x=header(10,"Raw",0,2);x.u16(2);x.zig(40);x.zig(0);x.zig(0);CHECK(!decode(x.done(),&why)&&why.find("newer")!=std::string::npos);}
    {Raw x=header();x.u16(2);x.zig(40);x.zig(0);x.zig(0);x.u8(0);CHECK(!decode(x.done()));}                 // trailing
    {Raw x=header();x.u16(2);x.zig(0);x.zig(0);x.zig(0);CHECK(!decode(x.done(),&why)&&why.find("same")!=std::string::npos);}
    {Raw x=header();x.u16(2);x.zig(800000);x.zig(0);x.zig(0);CHECK(!decode(x.done(),&why)&&why.find("100 km")!=std::string::npos);}
    {Raw x=header();x.u16(2);x.u8(0x80|80);x.u8(0);x.zig(0);x.zig(0);CHECK(!decode(x.done()));}               // 40 in two bytes
    {Raw x=header();x.u16(301);for(int i=1;i<301;++i){x.zig(4);x.zig(0);x.zig(0);}CHECK(!decode(x.done()));}
    {Raw x=header();x.u16(2);x.u8(0xff);x.u8(0xff);x.u8(0xff);x.u8(0x7f);x.zig(0);x.zig(0);CHECK(!decode(x.done()));}   // 28 bits: too far
    {Raw x=header();x.u16(2);x.u8(0xff);x.u8(0xff);x.u8(0xff);x.u8(0xff);x.u8(0x01);x.zig(0);x.zig(0);CHECK(!decode(x.done()));}   // 5 bytes
    // Padding bits that are not zero would give a second string for one challenge.
    {auto b=*unbase32(s.substr(5));std::string t=s;char& last=t.back();
     size_t bits=b.size()*8%5;   // bits of the last character that carry data
     if(bits){std::string a="0123456789ABCDEFGHJKMNPQRSTVWXYZ";size_t v=a.find(last);last=a[v|1];if(a[v|1]!=s.back())CHECK(!decode(t));}}
    CHECK(base32({})==""&&unbase32("")&&unbase32("")->empty());
    CHECK(!unbase32("U")&&!unbase32("*"));
    // Fuzz: random text never crashes and is refused (a random string with a right CRC is one in 2^32).
    std::mt19937 rng(7);
    for(int i=0;i<20000;++i){
        std::string t="DSR1-";int n=rng()%120;for(int k=0;k<n;++k)t+=alphabet[rng()%32];
        if(rng()%4==0)t+="-";
        CHECK(!decode(t));}
    for(int i=0;i<5000;++i){std::string t;int n=rng()%60;for(int k=0;k<n;++k)t+=char(rng()%256);decode(t);decode("DSR1-"+t);}
    // Random byte changes of good payloads with the CRC made right again: refused or valid, never a crash.
    auto good=*unbase32(s.substr(5));
    for(int i=0;i<20000;++i){
        auto b=good;b.resize(b.size()-4);int k=1+rng()%3;
        for(int j=0;j<k;++j)b[rng()%b.size()]^=uint8_t(1+rng()%255);
        if(rng()%5==0)b.resize(rng()%b.size());
        uint32_t c=crc32(b.data(),b.size());for(int q=0;q<4;++q)b.push_back(uint8_t(c>>(8*q)));
        auto x=decode("DSR1-"+base32(b));
        if(x)CHECK(invalid(*x).empty()&&x->points.size()>=2&&x->points.size()<=PointsMax);}
    CHECK(clock(754500)=="12:34.5"&&clock(3723400)=="1:02:03.4"&&clock(0)=="0:00.0");
    CHECK(rulesText(r.rules)=="5 m wide, 3 s grace, no vehicles, carry at least 2 cases, within 20:00");
}

static void recording(){
    // Samples closer than a metre are not kept; a walk of 1 km in 1 cm steps keeps about a thousand.
    Recorder rec;rec.start();uint64_t ms=0;
    for(int i=0;i<=100000;++i){ms+=10;rec.sample({i*0.01,0,std::sin(i*0.0001)*30},ms);}
    CHECK(rec.samples()>500&&rec.samples()<1100);
    auto pts=rec.stop();CHECK(pts.size()>=2&&pts.size()<=PointsMax&&!rec.on());
    // A wandering path of 3000 points simplifies to at most 300, and every dropped point stays near the line.
    std::mt19937 rng(3);std::vector<Point> path;double x=0,z=0,a=0;
    for(int i=0;i<3000;++i){a+=(int(rng()%200)-100)/400.0;x+=std::cos(a)*3;z+=std::sin(a)*3;path.push_back({x,double(i%50),z});}
    auto s=simplify(path,0.5);
    double worst=0;for(auto& p:path)worst=std::max(worst,nearest(s,p).distance);
    CHECK(worst<=0.5+1e-9);
    auto f=fit(path);CHECK(f.size()<=PointsMax&&f.size()>=2&&f.front().x==path.front().x&&f.back().z==path.back().z);
    // Too many samples: halved, never unbounded.
    Recorder big;big.start();for(int i=0;i<70000;++i)big.sample({i*2.0,0,0},uint64_t(i)*300);
    CHECK(big.samples()<Recorder::Samples);
    Route r;r.name="Fit";r.points=f;CHECK(decode(encode(r)).has_value());
}

static void track(){
    Route r=straight();r.rules.noVehicles=true;
    Walk w(r);w.wait(0,0,600);w.go(0,0,300,0,5);
    CHECK(w.c.phase()==Phase::Finished);
    auto& pts=w.c.track();
    CHECK(pts.size()>=29&&pts.size()<=32&&pts.front().ms==0);
    Track t{id(encode(r)),w.c.result(),pts};
    std::string text=encodeTrack(t,r.points[0]);
    std::string why;auto back=decodeTrack(text,r.points[0],&why);
    CHECK(back&&back->points.size()==pts.size()&&back->claimedMs==t.claimedMs&&back->route==t.route);
    if(!back){std::fprintf(stderr,"track: %s\n",why.c_str());CHECK(back);return;}
    if(auto v=verify(r,*back);!v.empty())std::fprintf(stderr,"verify: %s\n",v.c_str());
    CHECK(verify(r,*back).empty());
    // An edited time, another challenge, a teleport, a vehicle, a start outside the start zone: refused.
    Track x=*back;x.claimedMs+=5000;CHECK(verify(r,x).find("not the track's")!=std::string::npos);
    x=*back;x.route^=1;CHECK(verify(r,x)=="the track is of another challenge");
    x=*back;x.points[10].at.x+=600;CHECK(!verify(r,x).empty());
    x=*back;x.points[12].vehicle=true;CHECK(verify(r,x).find("rode a vehicle")!=std::string::npos);
    x=*back;x.points.erase(x.points.begin());CHECK(verify(r,x)=="the track does not begin at the start");
    x=*back;x.points.pop_back();CHECK(verify(r,x)=="the track does not reach the finish");
    // A track point off the corridor for longer than the grace.
    x=*back;for(int i=8;i<13;++i)x.points[size_t(i)].at.z+=20;CHECK(verify(r,x).find("left the corridor")!=std::string::npos);
    // Damaged text and the cap.
    CHECK(!decodeTrack(text.substr(0,text.size()-2),r.points[0]));
    std::string changed=text;changed[10]=changed[10]=='A'?'B':'A';CHECK(!decodeTrack(changed,r.points[0]));
    CHECK(!decodeTrack("DSR1-"+text.substr(5),r.points[0]));
    Track longOne{1,1,{}};for(int i=0;i<5000;++i)longOne.points.push_back({uint64_t(i)*2000,{double(i),0,0},false});
    auto lt=decodeTrack(encodeTrack(longOne,{0,0,0}),{0,0,0});
    CHECK(lt&&lt->points.size()<=TrackPointsMax&&lt->points.front().ms==0&&lt->points.back().ms==4999ull*2000);
    std::mt19937 rng(11);
    for(int i=0;i<5000;++i){std::string s="DST1-";int n=rng()%200;for(int k=0;k<n;++k)s+="0123456789ABCDEFGHJKMNPQRSTVWXYZ"[rng()%32];decodeTrack(s,{0,0,0});}
}

int main(){
    geometry();runs();format();recording();track();
    if(failures){std::fprintf(stderr,"route: %d checks failed\n",failures);return 1;}
    std::puts("route: geometry, runs, the string, recording and the track hold");
    return 0;
}
