#pragma once
// F3: the test-mode switches, on the game's common dialogue (dialog.h). Only where test mode is on, which on a group
// server is the server's rule. The body lists the switches with the selected one marked; the three rows switch it, move
// to the next one and close. The dialogue is opened again after each answer, so the list always shows what is in force.
#include "dialog.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace mod::debugmenu {
constexpr unsigned Key=0x72;            // F3: free in the supported build's recorded default bindings
constexpr uint64_t ReopenMs=3000;       // how long the game may take to be free again after an answer
enum Row:unsigned {RowSwitch=0,RowNext=1,RowClose=2};
struct Item {const char16_t* name;bool* on;};
std::u16string body(const std::vector<Item>&,size_t selected);
class Menu {
public:
    Menu(dialog::Api api,std::vector<Item> items,std::function<void(const std::string&)> log):api_(std::move(api)),items_(std::move(items)),log_(std::move(log)) {}
    // One game-thread tick. pressed: the key went down; allowed: the player is in the world and no game event runs.
    void tick(bool pressed,bool allowed,uint64_t now);
    bool open() const {return open_;}
    size_t selected() const {return selected_;}
    unsigned opens() const {return opens_;}
    unsigned switches() const {return switches_;}
private:
    bool show(unsigned row);
    dialog::Api api_;std::vector<Item> items_;std::function<void(const std::string&)> log_;
    bool open_=false,seen_=false,again_=false;size_t selected_=0;unsigned row_=RowSwitch,opens_=0,switches_=0;uint64_t until_=0;
};
}
