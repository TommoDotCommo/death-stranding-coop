#include "thread_trace.h"
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
using namespace redirect::trace;
static unsigned failures=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"trace oracle: %s\n",#x);++failures;}}while(0)
int main() {
    bool enabled=true;
    CHECK(Observer::parse(nullptr,enabled)&&!enabled);
    CHECK(Observer::parse(L"",enabled)&&!enabled);
    CHECK(Observer::parse(L"0",enabled)&&!enabled);
    CHECK(Observer::parse(L"1",enabled)&&enabled);
    CHECK(!Observer::parse(L"yes",enabled));CHECK(!Observer::parse(L"01",enabled));
    auto p=std::make_unique<Observer>();auto& o=*p;
    auto outer=o.begin(Api::peek,10,100,0x141a19387);
    uint64_t frames[]={0x141a19387,0x141a17aec};
    o.publish(outer,Phase::enter,100,0,frames,2);
    auto inner=o.begin(Api::get,10,101,0x1234);
    o.publish(inner,Phase::enter,101,0);
    o.finish(inner,102,-1);o.finish(outer,103,0);
    CHECK(o.buffer.records[0].depth==1&&o.buffer.records[2].depth==2);
    CHECK(o.buffer.records[0].committed.load()==1&&o.buffer.records[1].committed.load()==1);
    CHECK(o.buffer.records[0].frames[1]==frames[1]);CHECK(o.buffer.records[0].frameCount==2);
    CHECK(o.buffer.records[3].result==-1&&o.buffer.records[1].result==0);
    CHECK(o.buffer.records[3].call==o.buffer.records[2].call);
    CHECK(o.buffer.calls[0]==1&&o.buffer.calls[1]==1);
    for(unsigned i=0;i<1000;++i) {auto c=o.begin(Api::peek,10,104,0);o.publish(c,Phase::enter,104,0);o.finish(c,104,1);}
    CHECK(o.buffer.reserved==10); // four warmup Peek calls and one Get, two records each
    auto later=o.begin(Api::peek,10,1104,0);CHECK(later.sampled());o.finish(later,1104,1);
    auto other=o.begin(Api::peek,11,1104,0);CHECK(other.depth==1);o.finish(other,1104,1);
    // Reservation and commit are separate: a blocking Get has no exit event yet.
    auto pending=o.begin(Api::get,10,2104,0);o.publish(pending,Phase::enter,2104,0);
    CHECK(o.buffer.records[pending.ticket].committed==1);
    CHECK(o.buffer.records[pending.ticket+1].committed==0);o.finish(pending,2105,0);
    while(o.buffer.reserved<Capacity) {auto c=o.begin(Api::get,10,10000+o.buffer.calls[1]*1000,0);o.publish(c,Phase::enter,0,0);o.finish(c,0,1);}
    for(unsigned i=0;i<10;++i) {auto c=o.begin(Api::get,10,10000000+i*1000,0);o.finish(c,0,1);}
    CHECK(o.buffer.reserved==Capacity&&o.buffer.dropped==10);
    // Stop broken single-thread mutants before exercising concurrent producers.
    // In particular, the capacity mutant must fail an oracle without racing writes.
    if(failures)return 1;
    auto concurrent=std::make_unique<Observer>();std::vector<std::thread> workers;
    for(uint32_t tid=1;tid<=16;++tid)workers.emplace_back([&,tid]{for(unsigned i=0;i<100;++i){auto c=concurrent->begin(Api::peek,tid,1000+i*1000,tid);concurrent->publish(c,Phase::enter,i,0);concurrent->finish(c,i,1);}});
    for(auto& t:workers)t.join();
    CHECK(concurrent->buffer.calls[0]==1600);CHECK(concurrent->buffer.reserved<=Capacity);
    for(unsigned i=0;i<concurrent->buffer.reserved;++i) {auto& r=concurrent->buffer.records[i];CHECK(r.committed==1&&r.tid>=1&&r.tid<=16&&r.depth==1);}
    // Fixed thread table saturation is explicit and cannot create a false event.
    auto table=std::make_unique<Observer>();
    for(uint32_t tid=1;tid<=ThreadSlots+1;++tid){auto c=table->begin(Api::peek,tid,1,0);table->finish(c,1,1);}
    CHECK(table->buffer.dropped==1&&table->buffer.calls[0]==ThreadSlots+1);
    if(!failures)std::puts("thread trace: all checks passed");
    return failures?1:0;
}
