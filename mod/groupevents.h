#pragma once
// What happens to the world's enemies, in group play (opt-in `group_enemies`; the server's rule `enemies`;
// ../world/README.md, "What happens to the world's enemies"). Each game runs its own MULEs; what is shared is who is
// down and which camp is alarmed, as two-player play shares it (enemies.h: the MULE's locator UUID is the same in
// every game), through the server instead of the partner: an event said once (`…/coop/events`, POST), and the others'
// read every two seconds (GET since the last version). One thread of its own talks to the server (Courier).
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace mod::groupevents {
constexpr uint64_t WaitMinMs=1000,WaitMaxMs=60000,FailWaitMs=10000;
struct Event { std::string kind; int64_t key=0, value=0; bool operator==(const Event& o) const {return kind==o.kind&&key==o.key&&value==o.value;} };
struct Answer { bool read=false,on=true,full=false; std::string version; uint64_t waitMs=2000; std::vector<Event> events; };
Answer parse(const std::string& body);
std::string body(const Event&);
uint64_t delayAfter(const Answer&,unsigned& failures);
struct Result { int status=0; std::string body; };
class Courier {
public:
    using Get=std::function<Result(const std::string& since)>;
    using Post=std::function<Result(const std::string& body)>;
    Courier(Get,Post,std::function<void(const std::string&)> log);
    ~Courier();
    void send(std::vector<Event>);
    std::vector<Answer> reads();
    uint64_t sent() const {return sent_.load(std::memory_order_relaxed);}
private:
    void run();
    Get get_;Post post_;std::function<void(const std::string&)> log_;std::deque<Event> out_;std::vector<Answer> reads_;
    std::atomic<uint64_t> sent_{0};std::mutex lock_;std::condition_variable wake_;bool stop_=false;std::thread thread_;
};
}
