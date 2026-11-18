#pragma once
// P: which server this is, who is on it and its rules, on the game's common dialogue (dialog.h). The server's public
// policy answer (GET .../coop/policy, ../world/README.md) is read when the key is pressed, on a thread of its own; the
// dialogue opens with the answer, or says none came. The rules take as many pages as they need.
#include "dialog.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace mod::serverinfo {
constexpr unsigned Key=0x50;            // P: free in the supported build's recorded default bindings
constexpr uint64_t WaitMs=3000;         // the longest the page waits for the server
constexpr uint64_t ReopenMs=3000;       // how long the game may take to be free again after "Next page"
constexpr size_t RulesPerPage=5,NamesShown=12;
struct Policy {
    bool read=false;                    // a policy answer of a known format
    std::string name,preset;long known=0,limit=0;
    bool onlineKnown=false;long online=0;std::vector<std::string> names;   // an older server does not say who is on
    std::vector<std::string> rules;
};
Policy parse(const std::string& body);  // nothing read from anything else; never throws
struct Facts {
    bool group=false,server=false;      // server: this game has a server to ask
    std::string partner;bool partnerHere=false;
    std::vector<std::string> near;      // the other players shown around this porter now
};
std::vector<std::u16string> pages(const Facts&,const Policy&);
struct Fetch {
    std::function<void()> start;
    std::function<std::optional<std::string>()> poll;   // the answer's body once done ("": none)
};
class Screen {
public:
    Screen(dialog::Api api,Fetch fetch,std::function<void(const std::string&)> log):api_(std::move(api)),fetch_(std::move(fetch)),log_(std::move(log)) {}
    // One game-thread tick. pressed: the key went down; allowed: the player is in the world and no game event runs.
    void tick(bool pressed,bool allowed,const Facts&,uint64_t now);
    bool open() const {return state_==State::Open;}
    size_t page() const {return page_;}
    size_t pageCount() const {return pages_.size();}
    unsigned opens() const {return opens_;}
    bool answered() const {return policy_.read;}
private:
    enum class State {Idle,Reading,Open,Turning};
    bool show();
    dialog::Api api_;Fetch fetch_;std::function<void(const std::string&)> log_;
    State state_=State::Idle;bool seen_=false;uint64_t until_=0;size_t page_=0;unsigned opens_=0;
    Policy policy_;std::vector<std::u16string> pages_;
};
}
