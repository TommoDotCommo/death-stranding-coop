#pragma once
#include "discovery_options.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace mod::discovery {
using Reader=std::function<bool(uint64_t,void*,size_t)>;
struct Section { uint64_t address,size; bool executable; };
struct Ref { std::string name; size_t instruction,length; };
struct Candidate { std::string name,pattern; std::vector<Ref> refs; };
struct Signature { std::string name; uint64_t match; std::map<std::string,uint64_t> targets; };
// All portable paths used by production are exercised with fake memory on host.
class Sampler {
    Reader read_;
public:
    uint64_t base;
    std::vector<Section> sections;
    std::vector<Signature> signatures;
    Sampler(Reader r,uint64_t b):read_(r),base(b) {}
    bool read(uint64_t,void*,size_t) const;
    // One guarded copy, without the second read that rejects tearing: for bulk tables whose readers tolerate a record
    // changing mid-copy (cargo records; each consumer checks over time). Fails like read on unreadable memory.
    bool readOnce(uint64_t,void*,size_t) const;
    bool region(uint64_t,size_t,bool nonExecutable=false) const;
    uint64_t pointer(uint64_t) const;
    void scan(const std::string& actual,const std::string& expected);
    std::string sample(uint64_t us,const Options&) const;
    // H1 fast path, shared by the discovery sampler and the pose reader. reason is empty on success; otherwise one of
    // "manager null", "selection unreadable", "slow selection path", "bad index", "player slot unreadable", "player null".
    struct Selection { uint64_t slot=0,player=0; const char* reason=""; };
    Selection selectPlayer(uint64_t manager) const;
};
// Windows only: hash the host executable read-only, then scan the mapped main module (throws on any failure).
// Non-Windows builds throw; host tests use fake memory.
std::unique_ptr<Sampler> openHostSampler(const std::string& expectedSha256);
struct Budget {
    uint64_t limit,bytes=0,last=0;bool started=false;
    bool append(size_t n);
    bool sample(uint64_t us);
};
bool stopRequested(const std::filesystem::path&);
class Worker {
    std::atomic<bool> stop_{false},failed_{false};
    std::thread thread_;
public:
    Worker(const std::filesystem::path& config,Options);
    ~Worker();
    bool failed() const {return failed_;}
};
}
