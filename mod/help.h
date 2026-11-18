#pragma once
// Hold F1: the co-op keys, on the game's own common telop (the caption the HUD draws; game_offsets.md, "Common telop").
// Shown while the key is held and the player is in the world, gone on release or when a game event starts. No menu page
// opens and no input is taken from the game.
#include "discovery.h"
#include <cstdint>
#include <functional>
#include <string>
namespace mod::help {
constexpr unsigned Key=0x70;            // F1: free in the supported build's recorded default bindings
constexpr uint64_t RetryMs=500;         // the HUD was not ready: asked again this much later, while the key is held
constexpr float ShowSeconds=600;        // long enough for any hold; the release ends it
inline const char16_t* const Title=u"Co-op keys";
// Where the game draws it: pivot 0 is the text's top left, x and y are in a 1920 x 1080 screen (seen live, 2560 x 1440).
struct Place {int pivot=0;float x=140,y=180;};
struct Api {
    std::function<bool(const std::u16string& title,const std::u16string& body,const Place&)> show;   // false: no HUD now
    std::function<void()> hide;
};
// The mod's keys that do something in this session; 0 or false: not listed.
struct Facts {unsigned ping=0,ride=0,route=0,materials=0,story=0,debug=0;bool like=false;};
std::u16string keyName(unsigned vk);
std::u16string body(const Facts&);
class Page {
public:
    Page(Api api,std::function<void(const std::string&)> log):api_(std::move(api)),log_(std::move(log)) {}
    // One game-thread tick. allowed: the player is in the world and no game event runs.
    void tick(bool held,bool allowed,const Facts&,uint64_t now);
    void close();                       // hidden now, if it is shown
    bool shown() const {return shown_;}
    Place place;
private:
    Api api_;std::function<void(const std::string&)> log_;bool shown_=false;uint64_t retryAt_=0;
};
Api nativeApi(discovery::Sampler&);
}
