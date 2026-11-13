#pragma once
// Scripted keyboard and mouse for unattended tests (part of the opt-in virtual pad, COOP_VIRTUAL_PAD=1). The game
// reads both only as raw input: its window procedure passes every WM_INPUT to GetRawInputData. On the game thread,
// in the frame's own message loop, the proxy hands the game one WM_INPUT per scripted event with a handle only its
// GetRawInputData wrapper answers, so a copy takes the input whether or not its window has the focus and nothing is
// sent to the desktop. The game's keyboard clears its keys every frame while another window is in the foreground;
// that one call (ForegroundReturn) is answered with the game's own window while scripted keys are held, and no other
// caller of GetForegroundWindow is. Pure logic here, pinned by kbm_test.cpp; addresses are for the pinned build.
#include <cstdint>
namespace redirect::kbm {
constexpr uint64_t Handle=0x00c0'0910'ba5e'0001ull;   // not a handle Windows gives out (above the user address range)
constexpr uint64_t ForegroundReturn=0x19b9697;        // return address of the keyboard update's GetForegroundWindow
// The game's keyboard and mouse objects (globals the window procedure itself reads) each carry a "disabled" byte that
// the game sets while its window is inactive; their raw-input handlers return at once when it is set. It is cleared
// only for the dispatch of one scripted event and put back before the loop's next PeekMessage returns.
constexpr uint64_t KeyboardSlot=0x5559208,MouseSlot=0x5559200;constexpr unsigned KeyboardDisabled=0x1dc,MouseDisabled=0xb9;
constexpr unsigned MaxKeys=8,QueueSize=64;
struct Input {uint8_t keys[MaxKeys]{};uint8_t keyCount=0,buttons=0;int16_t mouseX=0,mouseY=0;int8_t wheel=0;uint8_t wheelSeq=0;};   // wheel: notches, turned once per new wheelSeq   // mouse: counts per second
// A raw keyboard edge, or one raw mouse report (relative motion and button transitions, RI_MOUSE_* flags).
struct Event {bool mouse=false;uint8_t vk=0;bool up=false;int32_t dx=0,dy=0;uint16_t buttonFlags=0;int16_t wheel=0;};   // wheel: RAWMOUSE usButtonData
inline bool serves(uint64_t handle){
#ifdef MUTANT_KBM_ANY_HANDLE
    (void)handle;return true;
#else
    return handle==Handle;
#endif
}
// Only the keyboard update's own check, and only while the script holds something.
inline bool answersForeground(bool holding,uint64_t callerRva){
#ifndef MUTANT_KBM_ALWAYS_FOREGROUND
    if(!holding)return false;
#endif
#ifndef MUTANT_KBM_ANY_CALLER
    if(callerRva!=ForegroundReturn)return false;
#endif
    return true;
}
// The pointer the game is told about (GetCursorPos) while a script holds something: the middle of its window, pushed
// towards where the scripted mouse is moving by a third of the window's height. The game's selection wheels read the
// pointer, not raw motion; without this they would follow the desktop's real pointer.
struct Point {int32_t x=0,y=0;};
inline bool answersCursor(bool holding){
#ifdef MUTANT_KBM_CURSOR_ALWAYS
    (void)holding;return true;
#else
    return holding;
#endif
}
inline Point cursor(Point middle,int32_t height,int32_t mouseX,int32_t mouseY){
    Point p=middle;
    if(!mouseX&&!mouseY)return p;
    double x=mouseX,y=mouseY,n=x*x+y*y,r=height/3.0,root=1;
    for(int i=0;i<40;++i)root=0.5*(root+n/root);                   // sqrt without <cmath> (the proxy has no CRT maths)
#ifndef MUTANT_KBM_CURSOR_FLAT
    p.x+=int32_t(x/root*r);p.y+=int32_t(y/root*r);
#endif
    return p;
}
class Feed {
    bool down_[256]{};uint8_t buttons_=0,wheelSeq_=0;double carryX_=0,carryY_=0;uint64_t last_=0;
    Event queue_[QueueSize];unsigned head_=0,count_=0;
    void push(const Event& e){if(count_<QueueSize)queue_[(head_+count_++)%QueueSize]=e;}
public:
    // Once per game frame, when the queue is empty: the script's state becomes the next frame's events. An inactive
    // script releases everything it held, so nothing stays down after a script ends or is replaced.
    void frame(bool active,const Input& in,uint64_t nowUs){
        bool wanted[256]{};
        if(active)for(unsigned i=0;i<in.keyCount&&i<MaxKeys;++i)wanted[in.keys[i]]=true;
        wanted[0]=false;
#ifndef MUTANT_KBM_NO_RELEASE
        for(unsigned vk=1;vk<256;++vk)if(down_[vk]&&!wanted[vk]){down_[vk]=false;Event e;e.vk=uint8_t(vk);e.up=true;push(e);}
#endif
        for(unsigned vk=1;vk<256;++vk)if(!down_[vk]&&wanted[vk]){down_[vk]=true;Event e;e.vk=uint8_t(vk);push(e);}
        Event m;m.mouse=true;
        uint8_t buttons=active?uint8_t(in.buttons&7):uint8_t(0);
        for(unsigned b=0;b<3;++b){
            bool was=buttons_&(1u<<b),now=buttons&(1u<<b);
            if(now&&!was)m.buttonFlags|=uint16_t(1u<<(2*b));       // L 0x1, R 0x4, M 0x10 down
            if(!now&&was)m.buttonFlags|=uint16_t(2u<<(2*b));       // L 0x2, R 0x8, M 0x20 up
        }
        buttons_=buttons;
        // The wheel turns once for each new step that asks for it (RI_MOUSE_WHEEL, 120 per notch).
        if(active&&in.wheel&&in.wheelSeq&&in.wheelSeq!=wheelSeq_){
#ifndef MUTANT_KBM_WHEEL_SIGN
            m.buttonFlags|=0x0400;m.wheel=int16_t(in.wheel*120);
#else
            m.buttonFlags|=0x0400;m.wheel=int16_t(-in.wheel*120);
#endif
        }
#ifndef MUTANT_KBM_WHEEL_REPEAT
        if(active&&in.wheel)wheelSeq_=in.wheelSeq;
#endif
        bool moving=active&&(in.mouseX||in.mouseY);
        if(moving&&last_&&nowUs>last_){
            // A stalled frame moves the pointer by at most 100 ms of travel.
            double dt=double(nowUs-last_>100000?100000:nowUs-last_)/1e6;
            carryX_+=in.mouseX*dt;carryY_+=in.mouseY*dt;
            m.dx=int32_t(carryX_);m.dy=int32_t(carryY_);carryX_-=m.dx;carryY_-=m.dy;
        }
        if(!moving){carryX_=carryY_=0;}
        last_=nowUs;
        if(m.dx||m.dy||m.buttonFlags||m.wheel)push(m);
    }
    bool pop(Event& e){if(!count_)return false;e=queue_[head_];head_=(head_+1)%QueueSize;--count_;return true;}
    bool pending() const {return count_!=0;}
    bool holding() const {if(buttons_)return true;for(bool d:down_)if(d)return true;return false;}
};
}
