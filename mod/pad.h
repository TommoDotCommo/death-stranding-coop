#pragma once
// Scripted virtual gamepad for unattended tests (see redirect/virtual_pad.h for the proxy side).
// Script text, one step per line, '#' starts a comment:
//   pad <run stamp>            first line: the script belongs to this process only
//   press A 150                hold buttons (A, A+B, ...) for ms, then 200 ms released
//   hold START 1000            hold buttons for ms, no release gap
//   stick 0 1 3000 [buttons]   left stick x,y in [-1,1] for ms, optionally holding buttons
//   look 1 0 500               right stick x,y in [-1,1] for ms
//   wait 2000                  nothing pressed for ms
// Buttons: A B X Y START BACK UP DOWN LEFT RIGHT LB RB LS RS LT RT.
// Keyboard and mouse (the proxy feeds them to the game as raw input, with or without the window focus):
//   key W+SHIFT 1500 [mouse DX DY]   hold keys for ms, optionally moving the mouse (counts per second)
//   tap E [150]                hold keys for ms (120), then 200 ms released
//   mouse 400 0 500 [KEYS]     move the mouse at DX,DY counts per second for ms, optionally holding keys
//   click L [150]              hold a mouse button (L R M) for ms (120), then 200 ms released
// Keys: A-Z 0-9 F1-F12 SPACE ENTER ESC TAB BACK SHIFT CTRL ALT UP DOWN LEFT RIGHT, or xNN (hex virtual key).
#include <cstdint>
#include <string>
#include <vector>
namespace mod::pad {
struct Gamepad {uint16_t buttons=0;uint8_t leftTrigger=0,rightTrigger=0;int16_t lx=0,ly=0,rx=0,ry=0;};
constexpr size_t MaxKeys=8;constexpr int MaxMouseSpeed=20000;
struct Keys {uint8_t vk[MaxKeys]{};uint8_t count=0,mouseButtons=0;int16_t mouseX=0,mouseY=0;};   // buttons: 1 L, 2 R, 4 M
struct Step {Gamepad pad;uint32_t ms=0;std::string line;Keys keys;};
constexpr uint32_t ReleaseGapMs=200,MaxStepMs=120000;constexpr size_t MaxSteps=2000,MaxBytes=32768;
// Throws std::runtime_error naming the line on any malformed or foreign script.
std::vector<Step> parse(const std::string& text,const std::string& stamp);
class Player {
    std::vector<Step> steps_;uint64_t start_=0;bool started_=false,done_=true;size_t index_=0;
public:
    void load(std::vector<Step> steps){steps_=std::move(steps);started_=false;done_=steps_.empty();index_=0;}
    void stop(){steps_.clear();done_=true;}
    // Active while a step is running; out is neutral otherwise.
    bool sample(uint64_t now,Gamepad& out,Keys* keys=nullptr);
    bool done() const {return done_;}
    size_t index() const {return index_;}
    size_t size() const {return steps_.size();}
};
}
