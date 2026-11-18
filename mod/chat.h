#pragma once
// Text chat between the players on a server (config `chat`). The lines travel through the server (GET and POST
// .../coop/chat with this player's notice token; ../world/README.md, "Text chat"), on a thread of their own, and arrive in
// the game's own info log as "Name: text". Y in the world starts typing: what is typed shows on the game's common telop;
// Enter sends it, Esc drops it. While typing the keyboard is taken from the game, so the porter does not walk off; keys
// already held are still released to it.
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace mod::chat {
constexpr unsigned Key=0x59;            // Y: free in the supported build's recorded default bindings
constexpr size_t MaxChars=160;          // the server's limit on a line
constexpr uint64_t PollMs=2000,BackoffMs=15000;
inline const char16_t* const Title=u"Say (Enter sends, Esc cancels)";
struct Press {enum Kind:uint8_t {Char,Back,Enter,Escape} kind=Char;char16_t ch=0;};
// What is being typed. UTF-16 units; a surrogate pair is removed whole.
class Composer {
public:
    void clear(){text_.clear();}
    bool type(char16_t);                // false: not taken (a control character, or the line is full)
    void back();
    const std::u16string& text() const {return text_;}
    std::string utf8() const;           // trimmed of spaces at both ends
private:
    std::u16string text_;
};
struct Line {std::string name,text;};
struct Answer {bool read=false,on=false;std::string next;std::vector<Line> lines;};
Answer parse(const std::string& body);  // never throws
std::string sayBody(const std::string& utf8);   // the JSON of one line
std::u16string shown(const Line&);      // "Name: text" for the info log
// What a refused line tells the player ("" when it went through). status 0: no answer.
std::u16string refusal(int status,const std::string& body);
struct Result {int status=0;std::string body;};
// The reads and the sends, on their own thread (never the game's). get and post are the server in the mod, a test's in a test.
class Link {
public:
    using Get=std::function<Result(const std::string& since)>;
    using Post=std::function<Result(const std::string& body)>;
    using Schedule=std::function<uint64_t(bool good,unsigned& failures)>;
    Link(Get get,Post post,Schedule schedule={});
    ~Link();
    void say(const std::string& utf8);
    std::vector<std::u16string> take();  // lines for the info log, in order
    struct Counters {unsigned reads=0,sent=0,refused=0,received=0;bool on=false;};
    Counters counters() const;
private:
    void run();
    Get get_;Post post_;Schedule schedule_;mutable std::mutex lock_;std::condition_variable wake_;bool stop_=false;
    std::deque<std::string> out_;std::deque<std::u16string> in_;Counters counters_;std::string next_;std::thread thread_;
};
uint64_t delayAfter(bool good,unsigned& failures);
struct Hud {
    std::function<bool(const std::u16string& title,const std::u16string& text)> show;   // false: no HUD now
    std::function<void()> hide;
};
struct Keyboard {
    std::function<bool()> take;                       // the keys are the mod's from now on; false: could not
    std::function<void()> give;                       // back to the game
    std::function<std::vector<Press>()> presses;      // typed since the last call
};
// The typing itself, on the game thread.
class Box {
public:
    Box(Hud hud,Keyboard keys,std::function<void(const std::string&)> send,std::function<void(const std::string&)> log):hud_(std::move(hud)),keys_(std::move(keys)),send_(std::move(send)),log_(std::move(log)) {}
    // pressed: the chat key went down; held: it is still down; allowed: in the world, no game event, no menu or dialogue up.
    void tick(bool pressed,bool held,bool allowed,const std::vector<Press>& extra={});
    void close();
    bool typing() const {return typing_;}
private:
    void draw();
    Hud hud_;Keyboard keys_;std::function<void(const std::string&)> send_,log_;Composer text_;bool typing_=false,waitRelease_=false;
};
Keyboard nativeKeyboard();
}
