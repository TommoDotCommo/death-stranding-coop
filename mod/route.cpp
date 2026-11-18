#include "route.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace mod::route {
namespace {
const char* Alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const char* Damaged="This challenge is incomplete. Ask for the whole string again.";
constexpr double Pi=3.14159265358979323846;
struct Writer {
    std::vector<uint8_t> b;
    void u8(unsigned v){b.push_back(uint8_t(v));}
    void u16(unsigned v){u8(v&0xff);u8((v>>8)&0xff);}
    void u32(uint32_t v){for(int i=0;i<4;++i)u8((v>>(8*i))&0xff);}
    void text(const std::string& s){u8(unsigned(s.size()));b.insert(b.end(),s.begin(),s.end());}
    void zig(int64_t v){uint64_t z=v<0?(uint64_t(-(v+1))<<1)|1:uint64_t(v)<<1;var(z);}
    void var(uint64_t z){do{uint8_t c=z&0x7f;z>>=7;if(z)c|=0x80;b.push_back(c);}while(z);}
};
struct Reader {
    const std::vector<uint8_t>& b;size_t at=0;bool bad=false;
    explicit Reader(const std::vector<uint8_t>& v):b(v){}
    unsigned u8(){if(at>=b.size()){bad=true;return 0;}return b[at++];}
    unsigned u16(){unsigned lo=u8();return lo|(u8()<<8);}
    uint32_t u32(){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(u8())<<(8*i);return v;}
    std::string text(){unsigned n=u8();if(at+n>b.size()){bad=true;return {};}std::string s(b.begin()+long(at),b.begin()+long(at+n));at+=n;return s;}
    // At most four bytes (28 bits); the shortest form only (a last byte of 0 after the first is refused).
    uint64_t var(){
        uint64_t z=0;
        for(int i=0;i<4;++i){
            unsigned c=u8();if(bad)return 0;
            z|=uint64_t(c&0x7f)<<(7*i);
            if(!(c&0x80)){
#ifndef MUTANT_ROUTE_VARINT
                if(i&&!c)bad=true;
#endif
                return z;}}
        bad=true;return 0;}
    int64_t zig(){uint64_t z=var();return (z&1)?-int64_t(z>>1)-1:int64_t(z>>1);}
};
int64_t q(double v,double step){return int64_t(std::llround(v/step));}
}

bool nameText(const std::string& s,size_t most,bool empty){
#ifdef MUTANT_ROUTE_NAME
    return s.size()<=255;(void)most;(void)empty;
#endif
    if(s.empty())return empty;
    if(s.size()>most||s.front()==' '||s.back()==' ')return false;
    for(unsigned char c:s)if(c<0x20||c>0x7e)return false;
    return true;
}
std::string invalid(const Route& r){
    const Rules& u=r.rules;
    if(!nameText(r.name,NameMax,false))return "the name is 1 to 32 printable characters (letters, digits, punctuation), no space at either end";
    if(!nameText(r.author,AuthorMax,true))return "the author's name is at most 24 printable characters, no space at either end";
#ifndef MUTANT_ROUTE_RANGE
    if(u.width<WidthMin||u.width>WidthMax)return "the corridor is 2 to 100 m wide";
    if(u.startRadius<RadiusMin||u.startRadius>RadiusMax||u.finishRadius<RadiusMin||u.finishRadius>RadiusMax)return "the start and finish zones have a radius of 2 to 50 m";
    if(u.grace>GraceMax)return "the grace is at most 60 s";
    if(u.minCases>CasesMax)return "at most 40 cases";
    if(u.timeLimit>TimeLimitMax)return "the time limit is at most 12 hours";
#endif
#ifndef MUTANT_ROUTE_POINTS
    if(r.points.size()<2||r.points.size()>PointsMax)return "a route has 2 to 300 points";
#endif
    for(auto& p:r.points){
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return "a point is not a number";
#ifndef MUTANT_ROUTE_REACH
        if(std::fabs(p.x-r.points[0].x)>ReachMax||std::fabs(p.y-r.points[0].y)>ReachMax||std::fabs(p.z-r.points[0].z)>ReachMax)return "a point is more than 100 km from the start";
#endif
        if(std::fabs(p.x)>2e8||std::fabs(p.y)>2e8||std::fabs(p.z)>2e8)return "a point is out of the world";}
    return {};
}

// ---- Geometry ----------------------------------------------------------------------------------------------------
double ground(const Point& a,const Point& b){
#ifdef MUTANT_ROUTE_FLAT
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
#endif
    return std::hypot(a.x-b.x,a.z-b.z);}
double length(const std::vector<Point>& l){double s=0;for(size_t i=1;i<l.size();++i)s+=ground(l[i-1],l[i]);return s;}
Nearest nearest(const std::vector<Point>& l,const Point& p,double from,double to){
    Nearest best;
    if(l.empty())return best;
    if(l.size()==1){best.distance=ground(l[0],p);best.at=l[0];return best;}
    double arc=0;
    for(size_t i=0;i+1<l.size();++i){
        const Point& a=l[i];const Point& b=l[i+1];
        double dx=b.x-a.x,dz=b.z-a.z,len=std::hypot(dx,dz);
        double next=arc+len;
        if(next<from||arc>to){arc=next;continue;}
        double lo=0,hi=1;
        if(len>0){lo=std::max(0.0,(from-arc)/len);hi=std::min(1.0,(to-arc)/len);}
        double t=len>0?((p.x-a.x)*dx+(p.z-a.z)*dz)/(len*len):0;
#ifndef MUTANT_ROUTE_CORNER
        t=std::clamp(t,lo,hi);
#else
        t=t<0.5?lo:hi;
#endif
        Point at{a.x+t*dx,a.y+t*(b.y-a.y),a.z+t*dz};
        double d=ground(at,p);
        if(d<best.distance){best.distance=d;best.along=arc+t*len;best.segment=i;best.at=at;}
        arc=next;}
    return best;
}
bool inCorridor(const Route& r,const Point& p){
    if(r.points.empty())return false;
#ifndef MUTANT_ROUTE_ZONES
    if(ground(p,r.points.front())<=r.rules.startRadius||ground(p,r.points.back())<=r.rules.finishRadius)return true;
#endif
    return nearest(r.points,p).distance<=r.rules.width/2.0;
}
namespace {
double offLine(const Point& p,const Point& a,const Point& b){
    double dx=b.x-a.x,dz=b.z-a.z,len2=dx*dx+dz*dz;
    if(len2<=0)return std::hypot(p.x-a.x,p.z-a.z);
    double t=std::clamp(((p.x-a.x)*dx+(p.z-a.z)*dz)/len2,0.0,1.0);
    return std::hypot(p.x-(a.x+t*dx),p.z-(a.z+t*dz));}
}
std::vector<Point> simplify(const std::vector<Point>& in,double tolerance){
    if(in.size()<3)return in;
    std::vector<bool> keep(in.size(),false);keep.front()=keep.back()=true;
    std::vector<std::pair<size_t,size_t>> todo{{0,in.size()-1}};
    while(!todo.empty()){
        auto [a,b]=todo.back();todo.pop_back();
        double worst=-1;size_t at=a;
        for(size_t i=a+1;i<b;++i){double d=offLine(in[i],in[a],in[b]);if(d>worst){worst=d;at=i;}}
#ifndef MUTANT_ROUTE_SIMPLIFY
        if(worst>tolerance){keep[at]=true;todo.push_back({a,at});todo.push_back({at,b});}
#else
        if(worst>tolerance*4){keep[at]=true;todo.push_back({a,at});todo.push_back({at,b});}
#endif
    }
    std::vector<Point> out;for(size_t i=0;i<in.size();++i)if(keep[i])out.push_back(in[i]);
    return out;
}
std::vector<Point> fit(const std::vector<Point>& in,size_t most,double tolerance){
    std::vector<Point> distinct;
    for(auto& p:in)if(distinct.empty()||ground(distinct.back(),p)>=0.25||std::fabs(distinct.back().y-p.y)>=0.5)distinct.push_back(p);
    if(distinct.size()<2)return distinct;
    auto out=simplify(distinct,tolerance);
    while(out.size()>most){tolerance*=2;out=simplify(distinct,tolerance);}
    return out;
}

// ---- The string --------------------------------------------------------------------------------------------------
uint32_t crc32(const uint8_t* p,size_t n){
    static uint32_t table[256];static bool made=false;
    if(!made){for(uint32_t i=0;i<256;++i){uint32_t c=i;for(int k=0;k<8;++k)c=c&1?0xEDB88320u^(c>>1):c>>1;table[i]=c;}made=true;}
    uint32_t c=0xffffffffu;for(size_t i=0;i<n;++i)c=table[(c^p[i])&0xff]^(c>>8);return c^0xffffffffu;}
std::string base32(const std::vector<uint8_t>& b){
    std::string out;uint32_t acc=0;int bits=0;size_t n=0;
    auto put=[&](unsigned v){if(n&&n%5==0)out+='-';out+=Alphabet[v&31];++n;};
    for(uint8_t c:b){acc=(acc<<8)|c;bits+=8;while(bits>=5){put(acc>>(bits-5));bits-=5;}acc&=(1u<<bits)-1;}
    if(bits)put(acc<<(5-bits));
    return out;
}
std::optional<std::vector<uint8_t>> unbase32(const std::string& s){
    std::vector<uint8_t> out;uint32_t acc=0;int bits=0;
    for(char ch:s){
        if(ch=='-'||ch==' ')continue;
        char c=char(ch>='a'&&ch<='z'?ch-32:ch);
        if(c=='I'||c=='L')c='1';else if(c=='O')c='0';
        const char* at=c?std::strchr(Alphabet,c):nullptr;if(!at)return std::nullopt;
        acc=(acc<<5)|unsigned(at-Alphabet);bits+=5;
        if(bits>=8){out.push_back(uint8_t(acc>>(bits-8)));bits-=8;acc&=(1u<<bits)-1;}}
#ifndef MUTANT_ROUTE_PADDING
    if(bits>=5||acc)return std::nullopt;   // a whole spare character, or padding bits that are not zero: another string
#endif
    return out;
}
std::string encode(const Route& r){
    if(auto why=invalid(r);!why.empty())throw std::invalid_argument(why);
    Writer w;const Rules& u=r.rules;
    w.u8(1);w.u8(u.noVehicles?1:0);w.u8(u.width);w.u8(u.startRadius);w.u8(u.finishRadius);w.u8(u.grace);w.u8(u.minCases);w.u16(u.timeLimit);
    w.text(r.name);w.text(r.author);
    int64_t sx=q(r.points[0].x,0.1),sy=q(r.points[0].y,0.1),sz=q(r.points[0].z,0.1);
    w.u32(uint32_t(int32_t(sx)));w.u32(uint32_t(int32_t(sy)));w.u32(uint32_t(int32_t(sz)));
    const double ox=sx/10.0,oy=sy/10.0,oz=sz/10.0;
    std::vector<std::array<int64_t,3>> cells{{0,0,0}};
    for(size_t i=1;i<r.points.size();++i){
        std::array<int64_t,3> c{q(r.points[i].x-ox,0.25),q(r.points[i].z-oz,0.25),q(r.points[i].y-oy,0.5)};
        if(c!=cells.back())cells.push_back(c);}   // quantised onto the one before: the same point
    if(cells.size()<2)throw std::invalid_argument("a route has 2 to 300 points");
    w.u16(unsigned(cells.size()));
    for(size_t i=1;i<cells.size();++i)for(int k=0;k<3;++k)w.zig(cells[i][k]-cells[i-1][k]);
    w.u32(crc32(w.b.data(),w.b.size()));
    if(w.b.size()>BytesMax)throw std::invalid_argument("the route is too long to share: record it in parts");
    return "DSR1-"+base32(w.b);
}
namespace {
bool prefixed(const std::string& s,const char* p){
    size_t n=std::strlen(p);if(s.size()<n)return false;
    for(size_t i=0;i<n;++i){char c=s[i];if(c>='a'&&c<='z')c=char(c-32);if(c!=p[i])return false;}
    return true;}
std::string trimmed(const std::string& s){size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?std::string():s.substr(a,b-a+1);}
}
std::optional<Route> decode(const std::string& text,std::string* why){
    auto no=[&](const std::string& w)->std::optional<Route>{if(why)*why=w;return std::nullopt;};
    const std::string s=trimmed(text);
    if(prefixed(s,"DST1-"))return no("That is the track of a result, not a challenge.");
    if(!prefixed(s,"DSR1-"))return no("That is not a challenge string (it starts with DSR1-).");
    if(s.size()>8+BytesMax*2)return no("That challenge string is too long.");
    auto bytes=unbase32(s.substr(5));
    if(!bytes||bytes->size()<5||bytes->size()>BytesMax)return no(Damaged);
#ifndef MUTANT_ROUTE_CRC
    uint32_t sum=uint32_t((*bytes)[bytes->size()-4])|uint32_t((*bytes)[bytes->size()-3])<<8|uint32_t((*bytes)[bytes->size()-2])<<16|uint32_t((*bytes)[bytes->size()-1])<<24;
    if(crc32(bytes->data(),bytes->size()-4)!=sum)return no(Damaged);
#endif
    Reader rd(*bytes);Route r;Rules& u=r.rules;
    unsigned version=rd.u8(),flags=rd.u8();
    if(rd.bad)return no(Damaged);
    if(version!=1)return no("That challenge is from a newer version of the mod (format "+std::to_string(version)+").");
    if(flags&~1u)return no("That challenge asks for a rule this version does not know.");
    u.noVehicles=flags&1;u.width=rd.u8();u.startRadius=rd.u8();u.finishRadius=rd.u8();u.grace=rd.u8();u.minCases=rd.u8();u.timeLimit=rd.u16();
    r.name=rd.text();r.author=rd.text();
    int32_t sx=int32_t(rd.u32()),sy=int32_t(rd.u32()),sz=int32_t(rd.u32());
    unsigned count=rd.u16();
    if(rd.bad)return no(Damaged);
    if(count<2||count>PointsMax)return no("A route has 2 to 300 points.");
    const double ox=sx/10.0,oy=sy/10.0,oz=sz/10.0;
    r.points.push_back({ox,oy,oz});
    int64_t cx=0,cz=0,cy=0;
    for(unsigned i=1;i<count;++i){
        int64_t dx=rd.zig(),dz=rd.zig(),dy=rd.zig();
        if(rd.bad)return no(Damaged);
#ifndef MUTANT_ROUTE_DUPLICATE
        if(!dx&&!dz&&!dy)return no("Two points of that route are the same.");
#endif
        cx+=dx;cz+=dz;cy+=dy;r.points.push_back({ox+cx*0.25,oy+cy*0.5,oz+cz*0.25});}
#ifndef MUTANT_ROUTE_TRAILING
    if(rd.at+4!=bytes->size())return no(Damaged);
#endif
    if(auto w=invalid(r);!w.empty()){w[0]=char(std::toupper(static_cast<unsigned char>(w[0])));return no("That challenge is not valid: "+w+".");}
    return r;
}
uint32_t id(const std::string& s){
    auto r=decode(s);if(!r)return 0;
    auto b=unbase32(trimmed(s).substr(5));
    return uint32_t((*b)[b->size()-4])|uint32_t((*b)[b->size()-3])<<8|uint32_t((*b)[b->size()-2])<<16|uint32_t((*b)[b->size()-1])<<24;
}
std::string clock(uint64_t ms){
    uint64_t tenths=(ms+50)/100,s=tenths/10,h=s/3600,m=(s/60)%60;char buf[48];
    if(h)std::snprintf(buf,sizeof buf,"%llu:%02llu:%02llu.%llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)(s%60),(unsigned long long)(tenths%10));
    else std::snprintf(buf,sizeof buf,"%llu:%02llu.%llu",(unsigned long long)(s/60),(unsigned long long)(s%60),(unsigned long long)(tenths%10));
    return buf;
}
std::string rulesText(const Rules& u){
    std::string t=std::to_string(u.width)+" m wide, "+std::to_string(u.grace)+" s grace";
    if(u.noVehicles)t+=", no vehicles";
    if(u.minCases)t+=", carry at least "+std::to_string(u.minCases)+(u.minCases==1?" case":" cases");
    if(u.timeLimit)t+=", within "+clock(uint64_t(u.timeLimit)*1000).substr(0,clock(uint64_t(u.timeLimit)*1000).size()-2);
    return t;
}

// ---- Recording ---------------------------------------------------------------------------------------------------
void Recorder::sample(const Point& p,uint64_t ms){
    if(!on_)return;
    if(!points_.empty()){
        const Point& l=points_.back();
#ifndef MUTANT_ROUTE_RECORD_STEP
        if(std::sqrt((l.x-p.x)*(l.x-p.x)+(l.y-p.y)*(l.y-p.y)+(l.z-p.z)*(l.z-p.z))<Step||ms-at_<MinGapMs)return;
#endif
    }
    points_.push_back(p);at_=ms;
    if(points_.size()>=Samples){   // halve: every second sample, the newest kept
        std::vector<Point> half;for(size_t i=points_.size()%2?0:1;i<points_.size();i+=2)half.push_back(points_[i]);
        points_.swap(half);}
}
std::vector<Point> Recorder::stop(){on_=false;auto out=points_.size()<2?std::vector<Point>{}:fit(points_);points_.clear();return out;}

// ---- A run -------------------------------------------------------------------------------------------------------
const char* phaseName(Phase p){
    switch(p){case Phase::Away:return "away";case Phase::Ready:return "ready";case Phase::Running:return "running";
              case Phase::Outside:return "outside";case Phase::Failed:return "failed";case Phase::Finished:return "finished";}
    return "?";}
Checker::Checker(Route r):route_(std::move(r)){total_=length(route_.points);}
std::string Checker::where(const Sample& s) const {
    auto n=nearest(route_.points,s.at);
    double edge=std::max(0.0,n.distance-route_.rules.width/2.0);
    double dx=n.at.x-s.at.x,dz=n.at.z-s.at.z,fx=std::sin(s.yaw),fz=std::cos(s.yaw);
    double angle=std::atan2(fx*dz-fz*dx,fx*dx+fz*dz)*180/Pi;   // positive: to the left (x east, z north)
    const char* side=std::fabs(angle)<30?"ahead of you":std::fabs(angle)>150?"behind you":angle>0?"to your left":"to your right";
    return "Corridor: "+std::to_string(long(std::ceil(edge)))+" m "+side+".";
}
void Checker::keep(const Sample& s,bool force){
    if(track_.empty()&&readySample_){trackBase_=readySample_->ms;track_.push_back({0,readySample_->at,readySample_->vehicle});}
    if(force||s.ms-trackAt_>=TrackEveryMs){
        if(track_.empty()){trackBase_=s.ms;}
        if(track_.empty()||track_.back().ms!=s.ms-trackBase_)track_.push_back({s.ms-trackBase_,s.at,s.vehicle});
        trackAt_=s.ms;}
}
void Checker::fail(std::vector<Event>& out,const std::string& why,uint64_t ms){
    phase_=Phase::Failed;reason_=why;result_=ms-startMs_;
    out.push_back({Kind::Failed,route_.name+" failed: "+why+".",ms});
}
std::vector<Event> Checker::feed(const Sample& s){
    std::vector<Event> out;
    if(phase_==Phase::Failed||phase_==Phase::Finished)return out;
    const Rules& u=route_.rules;
    const bool inStart=ground(s.at,route_.points.front())<=u.startRadius;
    const bool enough=!u.minCases||!s.cases||*s.cases>=u.minCases;
    auto prev=last_s_;last_s_=s;last_=s.ms;
    if(phase_==Phase::Away||phase_==Phase::Ready){
#ifdef MUTANT_ROUTE_START_CASES
        const bool ready=inStart;
#else
        const bool ready=inStart&&enough;
#endif
        if(ready){
            if(phase_!=Phase::Ready)out.push_back({Kind::Ready,"At the start of "+route_.name+". Go when ready.",s.ms});
            phase_=Phase::Ready;readySample_=s;return out;}
        if(phase_==Phase::Ready&&!inStart){
            // Left the start zone: the clock runs from this sample.
            phase_=Phase::Running;startMs_=s.ms;speedFrom_=s;footFrom_=s;trackAt_=s.ms;
            if(u.minCases&&!s.cases)casesVerified_=false;
            out.push_back({Kind::Started,route_.name+" started.",s.ms});
            keep(s,true);
            // fall through: the same sample is checked as running
        }else{
            if(inStart&&!enough){
                if(!awayOnce_){awayOnce_=true;out.push_back({Kind::Wait,"Carry at least "+std::to_string(u.minCases)+(u.minCases==1?" case":" cases")+" to start "+route_.name+".",s.ms});}
                phase_=Phase::Away;return out;}
            awayOnce_=false;
#ifdef MUTANT_ROUTE_READY
            if(inCorridor(route_,s.at)){phase_=Phase::Running;startMs_=s.ms;speedFrom_=s;footFrom_=s;trackAt_=s.ms;out.push_back({Kind::Started,route_.name+" started.",s.ms});keep(s,true);}
            else
#endif
            {
            if(!quiet_&&(!awayTold_||s.ms-awayTold_>=60000)){
                awayTold_=s.ms;phase_=Phase::Away;
                out.push_back({Kind::Away,"Challenge "+route_.name+": go to the start ("+std::to_string(long(std::lround(ground(s.at,route_.points.front()))))+" m).",s.ms});}
            phase_=Phase::Away;return out;}
        }
    }
    // Running or outside.
#ifndef MUTANT_ROUTE_TELEPORT_FLAG
    if(s.teleport){fail(out,"teleported",s.ms);keep(s,true);return out;}
#endif
#ifndef MUTANT_ROUTE_JUMP
    if(prev&&s.ms>prev->ms&&s.ms-prev->ms<JumpMs&&ground(prev->at,s.at)>JumpMetres){fail(out,"teleported",s.ms);keep(s,true);return out;}
#endif
    if(speedFrom_&&s.ms-speedFrom_->ms>=SpeedWindowMs){
        double v=ground(speedFrom_->at,s.at)*1000.0/double(s.ms-speedFrom_->ms);speedFrom_=s;
#ifndef MUTANT_ROUTE_SPEED
        if(v>TeleportSpeed){fail(out,"teleported",s.ms);keep(s,true);return out;}
#endif
    }
    if(u.noVehicles){
#ifndef MUTANT_ROUTE_VEHICLE
        if(s.vehicle){fail(out,"rode a vehicle",s.ms);keep(s,true);return out;}
#endif
        if(footFrom_&&s.ms-footFrom_->ms>=FootWindowMs){
            double v=ground(footFrom_->at,s.at)*1000.0/double(s.ms-footFrom_->ms);footFrom_=s;
#ifndef MUTANT_ROUTE_FOOT
            if(v>FootSpeed){fail(out,"too fast on foot",s.ms);keep(s,true);return out;}
#endif
        }
    }
#ifndef MUTANT_ROUTE_TIME
    if(u.timeLimit&&s.ms-startMs_>uint64_t(u.timeLimit)*1000){fail(out,"out of time",s.ms);keep(s,true);return out;}
#endif
    if(u.minCases){
        if(!s.cases)casesVerified_=false;
        else if(*s.cases<u.minCases){
            if(!fewCases_){fewCases_=true;fewSince_=s.ms;}
#ifndef MUTANT_ROUTE_CASES
            else if(s.ms-fewSince_>uint64_t(u.grace)*1000){fail(out,"put the cargo down",s.ms);keep(s,true);return out;}
#endif
        }else fewCases_=false;
    }
    // How far along the route: the nearest point within the window ahead of what was reached.
#ifndef MUTANT_ROUTE_WINDOW
    auto ahead=nearest(route_.points,s.at,progress_,progress_+Lookahead);
#else
    auto ahead=nearest(route_.points,s.at);
#endif
    if(ahead.distance<=u.width/2.0&&ahead.along>progress_)progress_=ahead.along;
    const bool inFinish=ground(s.at,route_.points.back())<=u.finishRadius;
#ifndef MUTANT_ROUTE_PROGRESS
    // Followed: progress (along the route, inside the corridor) reached the finish zone's edge, give or take the samples'
    // spacing. Not minus the search window: with that, any route under ~70 m counted as followed from its start (live,
    // 2026-11-18: a 26 m route 'finished' 1.4 s after leaving its start zone, going the wrong way).
    const bool followed=progress_>=total_-u.finishRadius-FinishSlack;
#else
    const bool followed=true;
#endif
    if(inFinish&&followed){
        phase_=Phase::Finished;result_=s.ms-startMs_;outsideBy_=0;keep(s,true);
        out.push_back({Kind::Finished,route_.name+" finished in "+clock(result_)+".",s.ms});
        return out;}
    const bool inside=inCorridor(route_,s.at);
    outsideBy_=inside?0:std::max(0.0,nearest(route_.points,s.at).distance-u.width/2.0);
    if(inside){
        if(phase_==Phase::Outside){phase_=Phase::Running;out.push_back({Kind::Back,"Back in the corridor.",s.ms});}
    }else if(phase_!=Phase::Outside){
        phase_=Phase::Outside;
#ifndef MUTANT_ROUTE_GRACE_RESET
        outsideSince_=s.ms;     // every time out: the whole grace again
#else
        if(!outsideSince_)outsideSince_=s.ms;
#endif
#ifndef MUTANT_ROUTE_GRACE
        if(!u.grace){fail(out,"left the corridor",s.ms);keep(s,true);return out;}
#else
        fail(out,"left the corridor",s.ms);keep(s,true);return out;
#endif
        out.push_back({Kind::Leaving,"Leaving the corridor: "+std::to_string(u.grace)+" s. "+where(s),s.ms});
    }else{
        if(s.ms-outsideSince_>uint64_t(u.grace)*1000){fail(out,"left the corridor",s.ms);keep(s,true);return out;}
    }
    keep(s);
    return out;
}

// ---- The track ---------------------------------------------------------------------------------------------------
std::string encodeTrack(const Track& t,const Point& o){
    std::vector<TrackPoint> pts=t.points;
    while(pts.size()>TrackPointsMax){
        std::vector<TrackPoint> half;
        for(size_t i=0;i<pts.size();++i)if(i<2||i+1==pts.size()||i%2==0)half.push_back(pts[i]);
        pts.swap(half);}
    if(pts.empty())throw std::invalid_argument("an empty track");
    Writer w;w.u8(1);w.u32(uint32_t(std::min<uint64_t>(t.claimedMs,0xffffffffu)));w.u32(t.route);w.u16(unsigned(pts.size()));
    int64_t pm=0,px=0,pz=0,py=0;
    for(size_t i=0;i<pts.size();++i){
        int64_t m=int64_t((pts[i].ms+50)/100);if(m<pm)m=pm;
        int64_t x=q(pts[i].at.x-o.x,0.5),z=q(pts[i].at.z-o.z,0.5),y=q(pts[i].at.y-o.y,1);
        w.var(uint64_t(m-pm)<<1|(pts[i].vehicle?1:0));w.zig(x-px);w.zig(z-pz);w.zig(y-py);
        pm=m;px=x;pz=z;py=y;}
    w.u32(crc32(w.b.data(),w.b.size()));
    if(w.b.size()>TrackBytesMax)throw std::invalid_argument("the track is too long");
    return "DST1-"+base32(w.b);
}
std::optional<Track> decodeTrack(const std::string& text,const Point& o,std::string* why){
    auto no=[&](const char* w)->std::optional<Track>{if(why)*why=w;return std::nullopt;};
    const std::string s=trimmed(text);
    if(!prefixed(s,"DST1-"))return no("not a track (DST1-)");
    if(s.size()>8+TrackBytesMax*2)return no("the track is too long");
    auto bytes=unbase32(s.substr(5));
    if(!bytes||bytes->size()<15||bytes->size()>TrackBytesMax)return no("the track is damaged");
    uint32_t sum=uint32_t((*bytes)[bytes->size()-4])|uint32_t((*bytes)[bytes->size()-3])<<8|uint32_t((*bytes)[bytes->size()-2])<<16|uint32_t((*bytes)[bytes->size()-1])<<24;
    if(crc32(bytes->data(),bytes->size()-4)!=sum)return no("the track is damaged");
    Reader rd(*bytes);Track t;
    if(rd.u8()!=1)return no("the track is of another format");
    t.claimedMs=rd.u32();t.route=rd.u32();unsigned n=rd.u16();
    if(rd.bad||!n||n>TrackPointsMax)return no("the track is damaged");
    int64_t pm=0,px=0,pz=0,py=0;
    for(unsigned i=0;i<n;++i){
        uint64_t d=rd.var();int64_t dx=rd.zig(),dz=rd.zig(),dy=rd.zig();
        if(rd.bad)return no("the track is damaged");
        pm+=int64_t(d>>1);px+=dx;pz+=dz;py+=dy;
        t.points.push_back({uint64_t(pm)*100,{o.x+px*0.5,o.y+py*1.0,o.z+pz*0.5},(d&1)!=0});}
    if(rd.at+4!=bytes->size())return no("the track is damaged");
    return t;
}
std::string verify(const Route& r,const Track& t){
    std::string encoded;
    try{encoded=encode(r);}catch(const std::exception& e){return std::string("the challenge is not valid: ")+e.what();}
#ifndef MUTANT_ROUTE_TRACK_ROUTE
    if(t.route!=id(encoded))return "the track is of another challenge";
#endif
    if(t.points.size()<2)return "the track is too short";
    Checker c(r);c.quiet(true);
    const uint64_t base=1000000;   // a clock that never goes below zero
    for(auto& p:t.points){
        Sample s;s.ms=base+p.ms;s.at=p.at;s.vehicle=p.vehicle;
        c.feed(s);
        if(c.phase()==Phase::Failed)return "the run "+c.reason()+" (at "+clock(p.ms)+" of the track)";
        if(c.phase()==Phase::Finished)break;}
    if(c.phase()!=Phase::Finished)return c.phase()==Phase::Away?"the track does not begin at the start":"the track does not reach the finish";
#ifndef MUTANT_ROUTE_TRACK_CLAIM
    uint64_t own=c.result(),claimed=t.claimedMs;
    if((own>claimed?own-claimed:claimed-own)>ClaimSlackMs)return "the time "+clock(claimed)+" is not the track's ("+clock(own)+")";
#endif
    return {};
}
}
