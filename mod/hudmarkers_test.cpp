// Far players as HUD markers (hudmarkers.h), without a game: which of the map's icons become marks (not drawn here,
// this area, nearest first, at most the limit), their ids, and the board.
#include "hudmarkers.h"
#include <cstdio>
#include <map>
using namespace mod::hudmarkers;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"hudmarkers oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
int main(){
    mod::mapicons::Answer a;a.read=a.on=true;
    for(int i=0;i<5;++i){mod::mapicons::Player p;p.id=std::to_string(5001+i);p.x=100.0*i;p.y=0;p.z=0;p.area=i==4?7:3;a.players.push_back(p);}
    std::swap(a.players[0],a.players[3]);   // listed out of order: 5004 first
    mod::mapicons::Board map;map.publish(mod::mapicons::icons(a,"1"));mod::mapicons::Board::Picture pic;map.read(pic);
    CHECK(pic.count==5&&iconId(5001)==pic.icons[3].id);   // the map's own ids
    const std::array<double,3> here{0,0,0};
    // 5002 is drawn as a body; 5005 is in another area: left out. Nearest first.
    auto m=marks(pic,3,{iconId(5002)},&here,10);
    CHECK(m.size()==3&&m[0].x==0&&m[1].x==200&&m[2].x==300);
    CHECK((m[0].id>>48)==0x4855&&(m[0].id&0xffffffffffffull)==(iconId(5001)&0xffffffffffffull));
    CHECK(marks(pic,3,{},&here,2).size()==2&&marks(pic,0,{},&here,99).size()==5);   // the limit; area not known: all
    Board b;b.publish(m);Board::Picture got;b.read(got);CHECK(got.count==3&&got.marks[2].x==300);
    b.publish(std::vector<Mark>(MaxMarks+5));b.read(got);CHECK(got.count==MaxMarks);
    CHECK(DefaultKind==0x98&&safeKind(0x98)&&safeKind(0xbc)&&safeKind(0x79)&&!safeKind(0x9e)&&!safeKind(0xb1)&&!safeKind(0xaf)&&!safeKind(0xc2));
    // Icon sources: any kind with an item but the user marker's (its slots hold a marker the assignment reads), the
    // marks' own kind, none and beyond the table. The scale's second item for kinds 9..0x2d.
    CHECK(safeSource(0x01)&&safeSource(0xbc)&&safeSource(0xb1)&&safeSource(0x2b)&&safeSource(LastKind));
    CHECK(!safeSource(0xaf)&&!safeSource(DefaultKind)&&!safeSource(NoIcon)&&!safeSource(LastKind+1)&&!safeSource(0xff));
    CHECK(scaleAlias(0x2b)==0x64&&scaleAlias(0x09)==0x33&&scaleAlias(0x1a)==0x1a&&scaleAlias(0x2d)==0x67&&scaleAlias(0x98)==0x98&&scaleAlias(0x08)==0x08);
    CHECK(ownId(0x4855000000000001ull)&&ownId(0x5047000000000001ull)&&!ownId(0x4d50000000000001ull)&&!ownId(0x1234));   // far players and pings; not the map's ids, not the game's
    CHECK(iconSource()==NoIcon&&DefaultIconSource==NoIcon);   // no change unless asked
    CHECK(!setIconSource(0xaf)&&iconSource()==NoIcon&&setIconSource(0x01)&&iconSource()==0x01&&setIconSource(NoIcon)&&iconSource()==NoIcon);
    // Which slots are restyled. The mod's slot (kind 0x98, an id of the mod's) once after the game assigned it; never a
    // game slot of the same kind, nor one of another kind, nor one not in state 2; again after each assignment by the
    // game (another id or index, or a lapse out of state 2); a texture that is not the wanted one only every
    // RetryFrames; back to the kind's own look when the source goes, only where the mod's look still is.
    {
        Restyle r;const uint8_t mark=0x98,src=0x01;uint64_t frame=100;
        SlotState mine;mine.kind=mark;mine.state=SlotAssigned;mine.id=0x4855000000000007ull;mine.index=3;
        SlotState game=mine;game.id=0x1234;SlotState other=mine;other.kind=0xbc;SlotState fading=mine;fading.state=3;
        CHECK(!r.due(1,game,mark,src,frame)&&!r.due(2,other,mark,src,frame)&&!r.due(0,fading,mark,src,frame));
        CHECK(r.due(0,mine,mark,src,frame));r.done(0,mine,mark,src,frame);
        CHECK(!r.due(0,mine,mark,src,++frame));                            // done: not every frame
        SlotState moved=mine;moved.index=4;CHECK(r.due(0,moved,mark,src,++frame));r.done(0,moved,mark,src,frame);   // the game reassigned it (new index)
        CHECK(!r.due(0,fading,mark,src,++frame)&&r.due(0,moved,mark,src,++frame));r.done(0,moved,mark,src,frame);    // out of state 2 and back
        SlotState shows=moved;shows.other=true;CHECK(!r.due(0,shows,mark,src,frame+1));CHECK(r.due(0,shows,mark,src,frame+Restyle::RetryFrames));
        SlotState newId=moved;newId.id=0x4855000000000008ull;CHECK(r.due(0,newId,mark,src,frame+1));r.done(0,newId,mark,src,frame+1);
        CHECK(r.due(0,newId,mark,0xbc,frame+2));r.done(0,newId,mark,0xbc,frame+2);   // another source: again
        // Off: back to the kind's own look, once; a slot the game has reassigned since is not touched.
        CHECK(r.due(0,newId,mark,mark,frame+3));r.done(0,newId,mark,mark,frame+3);CHECK(!r.due(0,newId,mark,mark,frame+4));
        CHECK(r.due(5,mine,mark,src,frame));r.done(5,mine,mark,src,frame);
        CHECK(!r.due(5,moved,mark,mark,frame+1)&&!r.due(5,mine,mark,mark,frame+2));   // reassigned by the game: its own look already
        CHECK(!r.due(6,mine,mark,mark,frame));   // never restyled: nothing to undo
        SlotState free;CHECK(!r.due(0,free,mark,src,frame)&&!r.due(HudSlots,mine,mark,src,frame));
    }
    // The icon table, read from memory: one row per kind with an item (its textures' size and name), one of the rest.
    {
        std::map<uint64_t,uint8_t> mem;
        auto put=[&](uint64_t a,const void* v,size_t n){for(size_t i=0;i<n;++i)mem[a+i]=static_cast<const uint8_t*>(v)[i];};
        auto q=[&](uint64_t a,uint64_t v){put(a,&v,8);};
        const uint64_t icons=0x10000,item=0x20000,icon=0x30000,data=0x40000,tex=0x50000,name=0x60000;
        for(unsigned k=0;k<=LastKind;++k)q(icons+0x20+8*k,0);
        q(icons+0x20+8*0x98,item);q(item+0x20,icon);int32_t two=2;put(icon+0x20,&two,4);q(icon+0x28,data);float h=1.5f,s=0.75f;put(icon+0x38,&h,4);put(icon+0x40,&s,4);
        q(data,tex);q(data+8,0);int32_t size[2]={64,32};put(tex+0x48,size,8);q(tex+0x40,name);const char n[]="ui_icon_mule_lastknown\n";put(name,n,sizeof n);
        q(icons+0x20+8*0x01,item);
        Read read=[&](uint64_t a,void* out,size_t len){for(size_t i=0;i<len;++i){auto it=mem.find(a+i);if(it==mem.end())return false;static_cast<uint8_t*>(out)[i]=it->second;}return true;};
        auto rows=iconTable(read,icons);
        CHECK(rows.size()==3&&rows[0].find("HUD icon 0x01 ")==0&&rows[1].find("HUD icon 0x98 item 0x20000 icon 0x30000 height 1.50 scale 0.75 textures 2 | 0x50000 64x32 'ui_icon_mule_lastknown?' | 0x0 0x0 ''")==0);
        CHECK(rows[2].find("HUD icons: 2 kinds with an item; none: 0x00 0x02")==0&&rows[2].find(" 0xc2\n")!=std::string::npos&&rows[2].find("0x98")==std::string::npos);
        mem.erase(icons+0x20+8*0x50);rows=iconTable(read,icons);CHECK(rows.back()=="HUD icon table unreadable at kind 0x50\n");
    }
    // Names over the far players: the nearest marks with a name, a label height above them, keyed by the mark's id,
    // without the UI's markup characters; at most MaxFarNames.
    {
        mod::mapicons::Answer n;n.read=n.on=true;
        const char* names[]={"Sam","<b>Fragile</b>","","Higgs","Deadman"};
        for(int i=0;i<5;++i){mod::mapicons::Player p;p.id=std::to_string(6001+i);p.name=names[i];p.x=50.0*(i+1);p.y=0;p.z=10;p.area=3;n.players.push_back(p);}
        mod::mapicons::Board nm;nm.publish(mod::mapicons::icons(n,"1"));mod::mapicons::Board::Picture np;nm.read(np);
        auto far=marks(np,3,{},&here,10);auto f=farNames(np,far,3);
        CHECK(f.size()==3&&f[0].name==u"Sam"&&f[1].name==u"bFragile/b"&&f[2].name==u"Higgs");   // the one without a name is left out
        CHECK(f[0].key==far[0].id&&f[0].at[0]==50&&f[0].at[2]==10+NameHeight&&f[2].at[0]==200);
        CHECK(farNames(np,far,1).size()==1&&farNames(np,far,99).size()==MaxFarNames&&farNames(np,far,0).empty());
    }
    if(failures){std::fprintf(stderr,"%u hudmarkers oracle failures\n",failures);return 1;}
    std::puts("hudmarkers oracles passed");return 0;
}
