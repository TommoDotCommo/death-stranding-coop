// Collections on the server (../docs/COLLECTIONS.md): a player's own objects made into a DSK string, and a DSK string
// made into objects of the player who imports it, as his game's uploads would be, under the policy.
#include "world.h"
#include "collection.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace world {
namespace {
using json::Value;
constexpr double Pi=3.14159265358979323846;
constexpr int64_t MaxGameCoordinate=2147483647;   // the game's px, py, pz are 32-bit: metres x 10^5
[[noreturn]] void refuse(int status,const std::string& why){throw Refused(status,why);}
uint64_t fnv(const std::string& s,uint64_t seed){uint64_t h=1469598103934665603ull^seed;for(unsigned char c:s){h^=c;h*=1099511628211ull;}return h;}
// An object's place in centimetres (the game's reading: l / 10^exp, an exp of zero or less counts as 2).
bool placeOf(const Value& p,int64_t& x,int64_t& y,int64_t& z){
    if(!p.get("px").isInt()||!p.get("py").isInt()||!p.get("pz").isInt())return false;
    int64_t e=p.get("exp").integer();if(e<=0)e=2;if(e>18)e=18;const double scale=std::pow(10.0,double(e-2));
    x=std::llround(double(p.get("px").integer())/scale);y=std::llround(double(p.get("py").integer())/scale);z=std::llround(double(p.get("pz").integer())/scale);
    return true;}
int64_t centidegrees(double radians){
    int64_t h=std::llround(radians*18000.0/Pi);h%=36000;if(h>18000)h-=36000;if(h<=-18000)h+=36000;return h;}
// Whole UTF-8 characters, at most `bytes` of them.
std::string cut(const std::string& s,size_t bytes){
    if(s.size()<=bytes)return s;
    size_t n=bytes;while(n>0&&(static_cast<unsigned char>(s[n])>>6)==2)--n;return s.substr(0,n);}
// One stored object as a structure of a collection, relative to the anchor; false with `why` when it cannot be one.
bool itemOf(const Value& p,const collection::Collection& c,collection::Item& i,std::string& why){
    const std::string t=p.get("t").string();
    if(t.size()!=1||t[0]<'a'||t[0]>'z'){why="unknown kind";return false;}
    int64_t x=0,y=0,z=0;if(!placeOf(p,x,y,z)){why="no place";return false;}
    i.type=t[0];i.dx=x-c.x;i.dy=y-c.y;i.dz=z-c.z;
    if(std::max({std::llabs(i.dx),std::llabs(i.dy),std::llabs(i.dz)})>collection::MaxOffsetCm){why="more than 2 km from the anchor";return false;}
    i.rx=p.get("rx").integer();i.ry=p.get("ry").integer();i.rz=p.get("rz").integer();
    for(auto a:{i.rx,i.ry,i.rz})if(a<-collection::MaxAngle||a>collection::MaxAngle){why="an angle out of range";return false;}
    const std::string st=p.get("st").string();
    if(!collection::levelText(st)){why="a level that is not text";return false;}
    i.level=st;
    const int64_t q=p.get("q").integer();
    if(q<0||q>collection::MaxArea){why="an area out of range";return false;}
    if(q!=c.area){i.ownArea=true;i.area=q;}
    if(p.get("x").isInt()&&p.get("y").isInt()&&p.get("m").isInt()){
        i.cellX=p.get("x").integer();i.cellY=p.get("y").integer();i.map=p.get("m").integer();
        i.cell=std::llabs(i.cellX)<=collection::MaxCell&&std::llabs(i.cellY)<=collection::MaxCell&&i.map>=0&&i.map<=collection::MaxCell;}
    i.p=p.get("p").integer()==1;
    // The builder's materials: what every contributor delivered, summed (the accounts are not carried).
    if(p.get("mt").isArray())for(auto& e:p.get("mt").items())if(e.get("mat").isArray()){
        size_t k=0;for(auto& n:e.get("mat").items()){if(k<6&&n.isInt()&&n.integer()>0){i.materials=true;i.material[k]=std::min(collection::MaxMaterial,i.material[k]+n.integer());}++k;}}
    // A sign's comment: its phrase numbers and text.
    if(p.get("c").isObject()){
        const Value& cm=p.get("c");
        for(auto& n:cm.get("p").items())if(n.isInt()&&n.integer()>=0&&n.integer()<=65535&&i.phrases.size()<collection::MaxPhrases)i.phrases.push_back(n.integer());
        std::string text=cut(cm.get("r").string(),collection::MaxCommentBytes);if(!collection::utf8(text,true))text.clear();
        i.text=text;i.comment=!i.phrases.empty()||!i.text.empty();}
    // The info blocks: their integer fields, by name, in order.
    for(uint8_t id=0;id<collection::MaxBlocks;++id){
        const Value& b=p.get(collection::BlockNames[id]);if(!b.isObject())continue;
        std::map<std::string,int64_t> fields;
        for(auto& m:b.members()){
            bool name=!m.first.empty()&&m.first.size()<=collection::MaxFieldName;for(char ch:m.first)name=name&&ch>='a'&&ch<='z';
            if(name&&m.second.isInt()&&std::llabs(m.second.integer())<=collection::MaxBlockValue&&fields.size()<collection::MaxBlockFields)fields[m.first]=m.second.integer();}
        if(fields.empty())continue;
        collection::Block k;k.id=id;for(auto& f:fields)k.fields.emplace_back(f.first,f.second);i.blocks.push_back(k);}
    return true;}
}
std::string World::freshId(const std::string& type,const std::string& creator){
    const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    while(id.empty()||objects_.count(id)){
        id=type;uint64_t h=fnv(creator,nextObject_++*0x9e3779b97f4a7c15ull);
        for(int i=0;i<12;++i){id+=digits[h%62];h/=62;if(i==5)h^=fnv(id,nextObject_);}}
    return id;}
void World::noteSpot(uint64_t relayId,const Spot& s){
    std::lock_guard<std::mutex> g(lock_);
    if(!relayId)return;
    if(!spots_.count(relayId)&&spots_.size()>=MaxSpots){
        auto oldest=spots_.begin();for(auto it=spots_.begin();it!=spots_.end();++it)if(it->second.at<oldest->second.at)oldest=it;
        spots_.erase(oldest);}
    spots_[relayId]=s;}
bool World::spotOf(uint64_t relayId,Spot& out) const {
    std::lock_guard<std::mutex> g(lock_);
    auto it=spots_.find(relayId);if(it==spots_.end())return false;out=it->second;return true;}
json::Value World::apiCollection(const std::string& key,const std::vector<std::string>& ids,int64_t nearMetres,const std::string& name,uint64_t relayId,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(ids.empty()==(nearMetres<=0))refuse(400,"name the objects, or near and a distance in metres");
    if(nearMetres>MaxNearMetres)refuse(400,"near is 1 to "+std::to_string(MaxNearMetres)+" metres");
    if(ids.size()>collection::MaxItems)refuse(400,"a collection holds at most "+std::to_string(collection::MaxItems)+" structures");
    if(!name.empty()&&!collection::nameText(name))refuse(400,"name: 1 to 32 bytes of text, no control characters, no space at either end");
    auto found=players_.find(key);
    if(found==players_.end())refuse(403,"you have built nothing on this server");
    if(found->second.banned)refuse(403,"this player is banned from this server");
    const std::string me=found->second.id;
    Value left=Value::array();
    auto leave=[&](const std::string& id,const std::string& why){Value v=Value::object();v.set("id",id);v.set("why",why);left.push(v);};
    // His own objects built here: in any state but a remnant; never another's, never an import.
    auto mine=[&](const Object& o){
#ifndef MUTANT_COLLECTION_OTHERS
        return o.creator==me&&o.originServer.empty()&&!o.remnant;
#else
        (void)o;return true;
#endif
    };
    std::vector<const Object*> chosen;collection::Collection c;
    if(!ids.empty()){
        std::set<std::string> seen;
        for(auto& id:ids){
            if(!seen.insert(id).second)continue;
            auto o=objects_.find(id);
            if(o==objects_.end()){leave(id,"no such object");continue;}
            if(!mine(o->second)){leave(id,o->second.remnant&&o->second.creator==me?"withdrawn":"not yours");continue;}
            int64_t x,y,z;if(!placeOf(o->second.post,x,y,z)){leave(id,"no place");continue;}
            chosen.push_back(&o->second);}
        if(chosen.empty())refuse(403,"none of these is a structure of yours with a place");
        // The anchor: the middle of them, in the area most of them have; no heading.
        double sx=0,sy=0,sz=0;std::map<int64_t,size_t> areas;
        for(auto* o:chosen){int64_t x,y,z;placeOf(o->post,x,y,z);sx+=double(x);sy+=double(y);sz+=double(z);++areas[o->post.get("q").integer()];}
        const double n=double(chosen.size());c.x=std::llround(sx/n);c.y=std::llround(sy/n);c.z=std::llround(sz/n);
        size_t most=0;for(auto& a:areas)if(a.second>most){most=a.second;c.area=a.first;}}
    else{
        // Everything of his within nearMetres of his spot; the anchor is the spot, facing his way.
        auto spot=spots_.find(relayId);
        if(!relayId||spot==spots_.end()||now-spot->second.at>SpotFreshSeconds)refuse(409,"the server has not heard where your game is in the last 10 minutes: start the game and move a little");
        const Spot& s=spot->second;
        c.x=std::llround(s.x*100);c.y=std::llround(s.y*100);c.z=std::llround(s.z*100);
        if(s.haveYaw){c.headingKnown=true;c.heading=centidegrees(s.yaw);}
        std::vector<std::pair<double,const Object*>> near;std::map<int64_t,size_t> areas;
        for(auto& o:objects_){
            if(!mine(o.second))continue;
            int64_t x,y,z;if(!placeOf(o.second.post,x,y,z))continue;
            const double dx=double(x-c.x)/100,dy=double(y-c.y)/100,dz=double(z-c.z)/100,d=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(d<=double(nearMetres))near.emplace_back(d,&o.second);}
        std::stable_sort(near.begin(),near.end(),[](auto& a,auto& b){return a.first<b.first;});
        for(auto& n:near){if(chosen.size()>=collection::MaxItems){leave(n.second->id,"more than 200: the nearest are taken");continue;}chosen.push_back(n.second);++areas[n.second->post.get("q").integer()];}
        if(chosen.empty())refuse(404,"nothing you built here is within "+std::to_string(nearMetres)+" m of where your game is");
        size_t most=0;for(auto& a:areas)if(a.second>most){most=a.second;c.area=a.first;}}
    if(std::max({std::llabs(c.x),std::llabs(c.y),std::llabs(c.z)})>collection::MaxAnchorCm)refuse(400,"the place is too far from the map's origin");
    c.name=name;
    for(auto* o:chosen){collection::Item i;std::string why;if(itemOf(o->post,c,i,why))c.items.push_back(i);else leave(o->id,why);}
    if(c.items.empty())refuse(403,"none of these can be in a collection");
    std::string why,text=collection::write(c,why);
    if(text.empty())refuse(400,why);
    note("collection of "+std::to_string(c.items.size())+" structures made by "+me);
    Value out=Value::object();out.set("format",1);out.set("collection",text);out.set("count",int64_t(c.items.size()));out.set("left_out",left);
    out.set("text",collection::describe(c));
    Value a=Value::object();a.set("area",c.area);a.set("x",double(c.x)/100);a.set("y",double(c.y)/100);a.set("z",double(c.z)/100);
    if(c.headingKnown){a.set("heading",double(c.heading)/100);}
    out.set("anchor",a);
    return out;}
json::Value World::apiImport(const std::string& key,const std::string& text,const std::string& place,uint64_t relayId,bool owner,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(place!="here"&&place!="original")refuse(400,"place is here or original");
    // Who may import (the policy's `import`).
    auto found=players_.find(key);
    const bool member=found!=players_.end()&&found->second.member;
#ifndef MUTANT_COLLECTION_IMPORT_ANYONE
    if(policy_.importWho=="nobody")refuse(403,"this server takes no collections");
    if(policy_.importWho=="members"&&!member&&!owner)refuse(403,"only members import collections here");
    if(policy_.importWho=="owner"&&!owner)refuse(403,"only the owner imports collections here");
#else
    (void)member;
#endif
    collection::Collection c;std::string why;
    if(!collection::parse(text,c,why))refuse(400,"not a collection: "+why);
    Player& me=[&]()->Player&{try{return playerFor(key,"import");}catch(const std::exception& e){refuse(403,e.what());}}();
    // Where they go: the anchor, the turn, and (here) the area, cell and map of the nearest object with a place.
    int64_t ax=c.x,ay=c.y,az=c.z,area=c.area,turnBy=0;bool cellHere=false;int64_t hx=0,hy=0,hm=0;
    if(place=="here"){
        auto spot=spots_.find(relayId);
        if(!relayId||spot==spots_.end()||now-spot->second.at>SpotFreshSeconds)refuse(409,"the server has not heard where your game is in the last 10 minutes: start the game and move a little, or import at the original place");
        const Spot& s=spot->second;ax=std::llround(s.x*100);ay=std::llround(s.y*100);az=std::llround(s.z*100);
#ifndef MUTANT_COLLECTION_NO_TURN
        if(c.headingKnown&&s.haveYaw){turnBy=centidegrees(s.yaw)-c.heading;if(turnBy>18000)turnBy-=36000;if(turnBy<=-18000)turnBy+=36000;}
#endif
        double best=double(AreaSearchMetres)*100;const Object* nearest=nullptr;
        for(auto& o:objects_){int64_t x,y,z;if(!placeOf(o.second.post,x,y,z))continue;
            const double d=std::sqrt(double(x-ax)*double(x-ax)+double(y-ay)*double(y-ay)+double(z-az)*double(z-az));
            if(d<=best){best=d;nearest=&o.second;}}
        if(!nearest)refuse(409,"the server knows no structure within 1.5 km of where you are, so it cannot tell the game's area there: import at the original place");
        area=nearest->post.get("q").integer();
        if(nearest->post.get("x").isInt()&&nearest->post.get("y").isInt()&&nearest->post.get("m").isInt()){cellHere=true;hx=nearest->post.get("x").integer();hy=nearest->post.get("y").integer();hm=nearest->post.get("m").integer();}}
    // His count against the hard cap: the whole import or nothing.
    size_t his=0;for(auto& o:objects_)if(o.second.creator==me.id&&o.second.originServer.empty()&&!o.second.remnant)++his;
#ifndef MUTANT_COLLECTION_CAP
    if(his+c.items.size()>MaxObjectsPerPlayer)refuse(403,"you would have more than "+std::to_string(MaxObjectsPerPlayer)+" objects here");
#endif
    struct Placed { const collection::Item* item;int64_t x,y,z,rz,area; };
    std::vector<Placed> made;Value left=Value::array();
    auto leave=[&](size_t index,const std::string& why2){Value v=Value::object();v.set("index",int64_t(index));v.set("why",why2);left.push(v);};
    const double spread2=double(policy_.spread)*double(policy_.spread)*10000;   // square centimetres
    for(size_t n=0;n<c.items.size();++n){
        const auto& i=c.items[n];int64_t dx=i.dx,dy=i.dy,rz=i.rz;
        if(turnBy)collection::turn(dx,dy,rz,turnBy);
        Placed p{&i,ax+dx,ay+dy,az+i.dz,rz,place=="here"?area:(i.ownArea?i.area:c.area)};
        if(std::max({std::llabs(p.x),std::llabs(p.y),std::llabs(p.z)})*1000>MaxGameCoordinate){leave(n,"outside");continue;}
        // Nothing the game would fail: not closer than the spread to one of its kind in its area.
        bool close=false;
#ifndef MUTANT_COLLECTION_SPREAD
        if(policy_.spread>0){
            const std::string type(1,i.type);
            auto tooClose=[&](int64_t x,int64_t y,int64_t z){const double ex=double(x-p.x),ey=double(y-p.y),ez=double(z-p.z);return ex*ex+ey*ey+ez*ez<spread2;};
            for(auto& m:made)if(!close&&m.item->type==i.type&&m.area==p.area&&tooClose(m.x,m.y,m.z))close=true;
            for(auto& o:objects_){
                if(close)break;
                const Object& ob=o.second;
                if(ob.post.get("t").string()!=type||ob.post.get("q").integer()!=p.area||ob.remnant)continue;
                if(!(ob.share.empty()||ob.creator==me.id))continue;   // what everyone is handed, or his own
                int64_t x,y,z;if(placeOf(ob.post,x,y,z)&&tooClose(x,y,z))close=true;}}
#endif
        if(close){leave(n,"too-close");continue;}
        made.push_back(p);}
    // Each one an object of his, as his game's upload would be: shared, held, kept or waiting.
    const std::string source=collection::tag(c,text);
    const std::string& choice=me.offerBuilds.empty()?policy_.offerBuilds:me.offerBuilds;
    Value ids=Value::array();std::map<std::string,int64_t> states{{"shared",0},{"held",0},{"kept",0},{"waiting",0}};std::array<int64_t,6> carried{};
    for(auto& p:made){
        const auto& i=*p.item;Value post=Value::object();
        post.set("t",std::string(1,i.type));
        const bool cell=place=="here"?cellHere:i.cell;
        if(cell){post.set("x",place=="here"?hx:i.cellX);post.set("y",place=="here"?hy:i.cellY);post.set("m",place=="here"?hm:i.map);}
        post.set("px",p.x*1000);post.set("py",p.y*1000);post.set("pz",p.z*1000);
        post.set("rx",i.rx);post.set("ry",i.ry);post.set("rz",p.rz);post.set("exp",5);post.set("q",p.area);
        post.set("st",i.level);post.set("p",int64_t(i.p?1:0));post.set("lp",0);
        if(i.comment){Value cm=Value::object();Value ph=Value::array();for(auto n:i.phrases)ph.push(n);cm.set("p",ph);cm.set("r",i.text);cm.set("pi",-1);cm.set("d",false);cm.set("wr","");post.set("c",cm);}
        for(auto& b:i.blocks){Value v=Value::object();for(auto& f:b.fields)v.set(f.first,f.second);post.set(collection::BlockNames[b.id],v);}
        if(i.materials){Value mt=Value::array(),e=Value::object(),mat=Value::array(),rmat=Value::array();for(auto n:i.material){mat.push(n);rmat.push(0);}
            e.set("c","");e.set("t",0);e.set("mat",mat);e.set("rmat",rmat);mt.push(e);post.set("mt",mt);
            for(size_t k=0;k<6;++k)carried[k]+=i.material[k];}
        // His object on the server (his limit, his withdrawal, his likes); the games are told its collection builder
        // (World::shownCreator), so his own game builds it as another player's.
        Object o;o.id=freshId(post.get("t").string(),me.id);o.post=post;o.updated=now;o.name=me.name;o.collection=source;
#ifndef MUTANT_COLLECTION_OWNER_LOST
        o.creator=me.id;
#else
        o.creator=collectionBuilderId(me.id);
#endif
        if(policy_.importCost=="materials"){o.costed=true;o.cost=i.material;}
        const std::string held=choice=="no"?std::string():blocked(me,false);
        o.share=choice=="no"?"kept":!held.empty()?"held":choice=="ask"?"ask":shareAfterOffer();
        if(o.share=="held"){o.held=held;o.heldAsk=choice=="ask";}
        if(o.share=="kept")o.kept=choice=="no"?"choice":"server";
        ++states[o.share.empty()?"shared":o.share=="held"?"held":o.share=="kept"?"kept":"waiting"];
        touch(o);ids.push(o.id);objects_[o.id]=o;}
    if(!made.empty()){dirty_=true;++changes_;}
    note("collection "+source+" imported by "+me.name+" ("+place+"): "+std::to_string(made.size())+" of "+std::to_string(c.items.size())+" structures made");
    Value out=Value::object();out.set("format",1);out.set("imported",int64_t(made.size()));out.set("ids",ids);
    for(auto& s:states)out.set(s.first,s.second);
    out.set("left_out",left);out.set("source",source);out.set("text",collection::describe(c));
    Value a=Value::object();a.set("area",area);a.set("x",double(ax)/100);a.set("y",double(ay)/100);a.set("z",double(az)/100);a.set("turned",double(turnBy)/100);out.set("anchor",a);
    Value m=Value::array();for(auto n:carried)m.push(n);out.set("materials",m);out.set("cost",policy_.importCost);
    return out;}
}
