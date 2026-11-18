#include "mapicons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
using namespace mod::mapicons;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"map icons oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static const char* Doc=R"({"format":1,"on":true,"players":[{"id":"7","name":"Ann","x":12.5,"y":-3,"z":4,"yaw":1,"area":200},{"id":"8","name":"Bob","x":1,"y":2,"z":3}]})";
template<class F> bool soon(F f){for(int i=0;i<2000;++i){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return f();}
static std::vector<Icon> onBoard(const Board& b){Board::Picture p;b.read(p);return std::vector<Icon>(p.icons.begin(),p.icons.begin()+p.count);}
int main(){
    {   auto a=parse(Doc);CHECK(a.read&&a.on&&a.players.size()==2&&a.players[0].name=="Ann"&&a.players[0].x==12.5&&a.players[0].y==-3&&a.players[0].area==200&&a.players[1].area==0);
        for(auto bad:{"","x","[1]","{\"format\":2}"})CHECK(!parse(bad).read);
        CHECK(!parse(R"({"format":1,"on":false,"players":[]})").on);}
    {   // His own entry is left out; off or unread: nobody; ids stable, different, never a marker's.
        auto a=parse(Doc);auto v=icons(a,"7");CHECK(v.size()==1&&v[0].x==1&&v[0].z==3);auto w=icons(a,"");CHECK(w.size()==2&&w[0].x==12.5f&&w[0].area==200&&w[0].id!=w[1].id&&w[1].id==v[0].id&&(w[0].id>>48)==0x4d50);
        auto off=a;off.on=false;CHECK(icons(off,"").empty());auto un=a;un.read=false;CHECK(icons(un,"").empty());
        auto bad=a;bad.players[0].x=NAN;bad.players[1].z=2e6;CHECK(icons(bad,"").empty());
        Answer many;many.read=many.on=true;many.players.resize(MaxIcons+9);for(size_t i=0;i<many.players.size();++i)many.players[i].id=std::to_string(i);CHECK(icons(many,"").size()==MaxIcons);}
    {   // The item the game gets.
        Icon n;n.x=1.5f;n.y=2.5f;n.z=3.5f;n.id=0x4d50000000000042ull;alignas(16) uint8_t item[ItemSize];std::memset(item,0xee,ItemSize);fillItem(item,n,0xb5);
        float f[4];std::memcpy(f,item,16);CHECK(f[0]==1.5f&&f[1]==2.5f&&f[2]==3.5f&&f[3]==0);CHECK(!std::memcmp(item,item+0x10,16)&&item[0x20]==0xb5&&item[0x21]==0&&item[0x22]==0);
        uint64_t id;std::memcpy(&id,item+0x28,8);CHECK(id==n.id);float alpha;std::memcpy(&alpha,item+0x3c,4);CHECK(alpha==1);for(size_t i=0x50;i<ItemSize;++i)CHECK(!item[i]);}
    {   // Only the area on show; not known on either side: shown. What the game refuses is not counted.
        Board b;std::vector<Icon> v(3);v[0].area=100;v[1].area=200;v[1].x=9;v[2].area=0;v[2].x=5;b.publish(v);std::vector<float> xs;auto add=[&](uint8_t* item){float x;std::memcpy(&x,item,4);xs.push_back(x);return true;};
        CHECK(addIcons(b,200,1,add)==2&&xs==(std::vector<float>{9,5}));xs.clear();CHECK(addIcons(b,0,1,add)==3);CHECK(addIcons(b,200,1,[](uint8_t*){return false;})==0);
        b.publish({});CHECK(addIcons(b,0,1,add)==0&&onBoard(b).empty());std::vector<Icon> lots(MaxIcons+5);b.publish(lots);CHECK(onBoard(b).size()==MaxIcons);}
    {   // The thread: answers reach the board; off and a server gone clear it (after a few failures); the extra icons of a test.
        std::mutex m;Result now{200,Doc};Board b;std::atomic<unsigned> asked{0};
        Link l([&]{std::lock_guard<std::mutex> g(m);++asked;return now;},b,"7",[](bool,unsigned& f){++f;f=std::min(f,5u);return uint64_t(2);});
        CHECK(soon([&]{return onBoard(b).size()==1&&l.counters().on&&l.counters().shown==1;}));
        Icon e;e.x=77;l.setExtra({e});CHECK(soon([&]{return onBoard(b).size()==2&&onBoard(b)[1].x==77;}));l.setExtra({});
        {std::lock_guard<std::mutex> g(m);now.body=R"({"format":1,"on":false,"players":[]})";}CHECK(soon([&]{return onBoard(b).empty()&&!l.counters().on;}));
        {std::lock_guard<std::mutex> g(m);now.body=Doc;}CHECK(soon([&]{return onBoard(b).size()==1;}));
        {std::lock_guard<std::mutex> g(m);now={0,""};}CHECK(soon([&]{return onBoard(b).empty();}));}
    {   unsigned f=0;CHECK(delayAfter(false,f)==PollMs&&delayAfter(false,f)==PollMs&&delayAfter(false,f)==BackoffMs&&delayAfter(true,f)==PollMs&&f==0);}
    if(!failures)std::puts("map icon checks passed");
    return failures?1:0;
}
