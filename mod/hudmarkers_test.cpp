// Far players as HUD markers (hudmarkers.h), without a game: which of the map's icons become marks (not drawn here,
// this area, nearest first, at most the limit), their ids, and the board.
#include "hudmarkers.h"
#include <cstdio>
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
    if(failures){std::fprintf(stderr,"%u hudmarkers oracle failures\n",failures);return 1;}
    std::puts("hudmarkers oracles passed");return 0;
}
