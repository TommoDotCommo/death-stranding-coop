#include "livebuild.h"
#include "../world/json.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mod::livebuild {
uint8_t netType(char letter,const std::string& st){
    // The Net variant of each kind (own_strand.cpp letter, read the other way): safe house 4, post 7, watchtower 10,
    // catapult 13, charger 16, rain shelter 19, zipline 23, ladder 26, rope 29, bridge 30 m 32 / 45 m 35, chiral bridge 42,
    // jump stand 45.
    switch(letter){
        case 's':return 4;case 'p':return 7;case 'w':return 10;case 'a':return 13;case 'c':return 16;case 'n':return 19;
        case 'z':return 23;case 'l':return 26;case 'r':return 29;case 'g':return 42;case 'j':return 45;
#ifndef MUTANT_LIVEBUILD_BRIDGE
        case 'b':return st=="30"?32:35;
#else
        case 'b':return 35;
#endif
        default:return 0;
    }
}
std::array<float,9> rotationOf(double rxDegrees,double ryDegrees,double rzDegrees){
    // The game reads ry = asin(-m02), rx = atan2(m12, m22), rz = atan2(m01, m00) from the rows m: so m is this.
    const double k=3.14159265358979323846/180;
    const double x=rxDegrees*k,y=ryDegrees*k,z=rzDegrees*k,cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
#ifndef MUTANT_LIVEBUILD_ROTATION
    return {float(cy*cz),float(cy*sz),float(-sy),
            float(sx*sy*cz-cx*sz),float(sx*sy*sz+cx*cz),float(sx*cy),
            float(cx*sy*cz+sx*sz),float(cx*sy*sz-sx*cz),float(cx*cy)};
#else
    return {float(cz),float(sz),0,float(-sz),float(cz),0,0,0,1};
#endif
}
namespace {
bool number(const json::Value& v,double& out){if(v.isInt()){out=double(v.integer());return true;}return false;}
}
Answer parse(const std::string& body){
    Answer a;
    try{
        auto d=json::parse(body);
        if(d.get("format").integer()!=1||!d.get("version").isString())return a;
        a.version=d.get("version").string();a.full=d.get("full").boolean();a.more=d.get("more").boolean();
        a.waitMs=uint64_t(std::clamp<int64_t>(d.get("wait_ms").integer(),int64_t(WaitMinMs),int64_t(WaitMaxMs)));
        for(auto& o:d.get("objects").items()){
            Piece p;p.id=o.get("id").string();const std::string t=o.get("t").string();
            p.type=t.size()==1?netType(t[0],o.get("st").string()):0;
            auto& l=o.get("l").items();auto& r=o.get("r").items();double pos[3],rot[3]={0,0,0};
            bool good=!p.id.empty()&&p.id.size()<=64&&p.type&&l.size()==3;
            for(int i=0;good&&i<3;++i)good=number(l[i],pos[i]);
            for(int i=0;good&&i<3&&r.size()==3;++i)good=number(r[i],rot[i]);
            if(!good){++a.skipped;continue;}
            // The game's own reading: l / 10^exp, an exp of zero or less counts as 2; angles are degrees x 10^5.
            int64_t e=o.get("exp").integer();if(e<=0)e=2;if(e>18)e=18;const double scale=std::pow(10.0,double(e));
            for(int i=0;i<3;++i)p.at.position[i]=pos[i]/scale;
            p.at.rotation=rotationOf(rot[0]/1e5,rot[1]/1e5,rot[2]/1e5);
            a.pieces.push_back(p);}
        for(auto& v:d.get("removed").items())if(v.isString()&&!v.string().empty()&&v.string().size()<=64)a.removed.push_back(v.string());
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
uint64_t delayAfter(const Answer& a,unsigned& failures){
    if(a.read){failures=0;return a.waitMs;}
    failures=std::min(failures+1,6u);return std::min<uint64_t>(FailWaitMs<<(failures-1),WaitMaxMs);
}
Reader::Reader(Get get,std::function<void(const std::string&)> log):get_(std::move(get)),log_(std::move(log)){thread_=std::thread([this]{run();});}
Reader::~Reader(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
std::vector<Answer> Reader::take(){std::lock_guard<std::mutex> g(lock_);std::vector<Answer> out;out.swap(answers_);return out;}
void Reader::run(){
    unsigned failures=0;bool told=false;std::string since;
    std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        g.unlock();
        Result r;try{r=get_(since);}catch(const std::exception&){r=Result{};}
        Answer a;if(r.status==200)a=parse(r.body);
        if(a.read&&!told){told=true;log_("LIVEBUILD the server lists "+std::to_string(a.pieces.size())+" structures of other players\n");}
        if(!a.read&&r.status&&r.status!=200&&failures==0)log_("LIVEBUILD read refused: status "+std::to_string(r.status)+"\n");
        reads_.fetch_add(1,std::memory_order_relaxed);
        const uint64_t wait=delayAfter(a,failures);
        g.lock();
        if(a.read){
            if(answers_.size()>=AnswersKept){answers_.clear();since.clear();}   // not taken: read everything again next time
            else{since=a.version;answers_.push_back(std::move(a));}}
        wake_.wait_for(g,std::chrono::milliseconds(wait),[&]{return stop_;});
    }
}
void Plan::take(const Answer& a){
    if(!a.read)return;
    version_=a.version;
    if(a.full){
        std::map<std::string,Piece> next;for(auto& p:a.pieces)next[p.id]=p;
#ifndef MUTANT_LIVEBUILD_FULL_KEEPS
        for(auto& w:wanted_)if(!next.count(w.first))drop_.insert(w.first);   // made from an older read, gone since
#endif
        wanted_=std::move(next);}
    else for(auto& p:a.pieces){wanted_[p.id]=p;drop_.erase(p.id);givenUp_.erase(p.id);tries_.erase(p.id);askedAt_.erase(p.id);}
    for(auto& id:a.removed){
#ifndef MUTANT_LIVEBUILD_REMOVED_KEPT
        wanted_.erase(id);drop_.insert(id);
#endif
    }
}
Step Plan::step(const std::vector<Seen>& records,const std::array<double,3>& porter,bool porterKnown,uint64_t now){
    Step s;std::map<std::string,std::vector<const Seen*>> held;
    for(auto& r:records)if(!r.online.empty())held[r.online].push_back(&r);
    for(auto& h:held)if(h.second.size()>1)++s.twice;
    // Taken down: only the Net copies of what was removed (never the player's own, never the game's).
    for(auto it=drop_.begin();it!=drop_.end();){
        auto h=held.find(*it);
        // Asked of the game a moment ago and not there yet: it is taken down when it appears.
        if(h==held.end())if(auto a=askedAt_.find(*it);a!=askedAt_.end()&&now<a->second+RetryMs){++it;continue;}
        if(h!=held.end())for(auto* r:h->second)
#ifndef MUTANT_LIVEBUILD_REMOVE_OWN
            if(r->owner==2)
#endif
                s.remove.push_back(*r);
        tries_.erase(*it);askedAt_.erase(*it);givenUp_.erase(*it);it=drop_.erase(it);}
    // Built: what is wanted and not held, nearest first, within reach of the porter.
    std::vector<std::pair<double,const Piece*>> order;
    for(auto& w:wanted_){
        if(held.count(w.first)){tries_.erase(w.first);askedAt_.erase(w.first);continue;}   // it stands (ours, or the game's own)
        double d=0;
        if(porterKnown){double dx=w.second.at.position[0]-porter[0],dy=w.second.at.position[1]-porter[1];d=std::sqrt(dx*dx+dy*dy);
#ifndef MUTANT_LIVEBUILD_FAR
            if(d>NearMetres)continue;
#endif
        }
        order.push_back({d,&w.second});}
    std::sort(order.begin(),order.end(),[](auto& a,auto& b){return a.first!=b.first?a.first<b.first:a.second->id<b.second->id;});
    for(auto& o:order){
        const std::string& id=o.second->id;
        if(givenUp_.count(id))continue;
#ifndef MUTANT_LIVEBUILD_RETRY_AT_ONCE
        if(auto a=askedAt_.find(id);a!=askedAt_.end()&&now<a->second+RetryMs)continue;   // asked: give the game time
#endif
        if(tries_[id]>=TriesPerPiece){givenUp_.insert(id);s.givenUp.push_back(id);continue;}
        if(s.build.size()>=BuildsPerStep)break;
        ++tries_[id];askedAt_[id]=now;s.build.push_back(*o.second);}
    return s;
}
}
