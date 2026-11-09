// Likes for the partner (likes.h): the one-event wire, a sender that sends at most one like per gap and only while
// connected, and a receiver that gives at most one a second and PerMinute a minute whatever arrives.
#include "likes.h"
#include <cstdio>
using namespace mod::likes;
unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"likes oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
int main(){
    auto e=encode(1,7);CHECK(e.type==WireType&&decode(e)&&*decode(e)==1);
    auto bad=e;bad.item=0;CHECK(!decode(bad));bad=e;bad.item=MaxPerEvent+1;CHECK(!decode(bad));
    bad=e;bad.type=5;CHECK(!decode(bad));bad=e;bad.ref=1ull<<40;CHECK(!decode(bad));
    Sender s;CHECK(!s.press(1000,false));
    auto a=s.press(1000,true);CHECK(a&&decode(*a));
    CHECK(!s.press(1000+Sender::MinGapMs-1,true));
    auto b=s.press(1000+Sender::MinGapMs,true);CHECK(b&&b->ref!=a->ref);
    Receiver r;unsigned given=0;
    for(uint64_t t=0;t<60000;t+=100)given+=r.accept(100000+t);   // ten a second for a minute
    CHECK(given==Receiver::PerMinute);
    Receiver g;CHECK(g.accept(5000));CHECK(!g.accept(5000+Receiver::MinGapMs-1));CHECK(g.accept(5000+Receiver::MinGapMs));
    Receiver w;for(unsigned i=0;i<Receiver::PerMinute;++i)CHECK(w.accept(1000+2000*i));
    CHECK(!w.accept(1000+2000*Receiver::PerMinute));CHECK(w.accept(1000+60000+1));   // the oldest has left the minute
    if(failures)return 1;
    std::puts("likes tests passed");return 0;
}
