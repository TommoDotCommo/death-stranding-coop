#include "chat.h"
#include "serverinfo.h"
#include "../world/json.h"
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#endif
namespace mod::chat {
bool Composer::type(char16_t c){
#ifndef MUTANT_CHAT_CONTROL
    if(c<0x20||c==0x7f)return false;
#endif
    size_t units=0;for(char16_t u:text_)if(u<0xdc00||u>0xdfff)++units;
#ifndef MUTANT_CHAT_LONG
    if(units>=MaxChars&&!(c>=0xdc00&&c<=0xdfff))return false;
#endif
    text_+=c;return true;
}
void Composer::back(){
    if(text_.empty())return;
    const char16_t last=text_.back();text_.pop_back();
#ifndef MUTANT_CHAT_SURROGATE
    if(last>=0xdc00&&last<=0xdfff&&!text_.empty()&&text_.back()>=0xd800&&text_.back()<=0xdbff)text_.pop_back();
#endif
}
std::string Composer::utf8() const {
    std::string out;
    for(size_t i=0;i<text_.size();++i){uint32_t c=text_[i];
        if(c>=0xd800&&c<=0xdbff&&i+1<text_.size()&&text_[i+1]>=0xdc00&&text_[i+1]<=0xdfff){c=0x10000+((c-0xd800)<<10)+(text_[++i]-0xdc00);}
        else if(c>=0xd800&&c<=0xdfff)continue;
        if(c<0x80)out+=char(c);
        else if(c<0x800){out+=char(0xc0|c>>6);out+=char(0x80|(c&63));}
        else if(c<0x10000){out+=char(0xe0|c>>12);out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else{out+=char(0xf0|c>>18);out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
    const size_t a=out.find_first_not_of(' ');if(a==std::string::npos)return {};
    return out.substr(a,out.find_last_not_of(' ')-a+1);
}
Answer parse(const std::string& body){
    Answer a;
    try{auto d=json::parse(body);if(d.get("format").integer()!=1)return a;
        a.on=d.get("on").boolean();a.next=std::to_string(d.get("next").integer());
        for(auto& l:d.get("lines").items())a.lines.push_back({l.get("name").string(),l.get("text").string()});
        a.read=true;
    }catch(const std::exception&){a=Answer{};}
    return a;
}
std::string sayBody(const std::string& utf8){json::Value v=json::Value::object();v.set("text",utf8);return v.dump();}
std::u16string shown(const Line& l){return serverinfo::wide(l.name)+u": "+serverinfo::wide(l.text);}
std::u16string refusal(int status,const std::string& body){
    if(status==200)return {};
    if(status==429)return u"Chat: too many lines, wait a moment.";
    if(status==403&&body.find("chat-off")!=std::string::npos)return u"Text chat is off on this server.";
    if(status==400)return u"Chat: that line cannot be sent.";
    return u"Chat: the line did not reach the server.";
}
uint64_t delayAfter(bool good,unsigned& failures){
    if(good){failures=0;return PollMs;}
    ++failures;
#ifndef MUTANT_CHAT_BACKOFF
    return failures>=3?BackoffMs:PollMs;
#else
    return PollMs;
#endif
}
Link::Link(Get get,Post post,Schedule schedule):get_(std::move(get)),post_(std::move(post)),schedule_(std::move(schedule)){
    if(!schedule_)schedule_=delayAfter;
    thread_=std::thread([this]{run();});
}
Link::~Link(){{std::lock_guard<std::mutex> g(lock_);stop_=true;}wake_.notify_all();thread_.join();}
void Link::say(const std::string& utf8){{std::lock_guard<std::mutex> g(lock_);if(out_.size()<8)out_.push_back(utf8);}wake_.notify_all();}
std::vector<std::u16string> Link::take(){std::lock_guard<std::mutex> g(lock_);std::vector<std::u16string> v(in_.begin(),in_.end());in_.clear();return v;}
Link::Counters Link::counters() const {std::lock_guard<std::mutex> g(lock_);return counters_;}
void Link::run(){
    unsigned failures=0;std::unique_lock<std::mutex> g(lock_);
    while(!stop_){
        while(!out_.empty()&&!stop_){
            std::string line=out_.front();out_.pop_front();g.unlock();auto r=post_(sayBody(line));g.lock();
            auto why=refusal(r.status,r.body);
            if(why.empty())++counters_.sent;else{++counters_.refused;in_.push_back(why);}
        }
        if(stop_)break;
        const std::string since=next_;g.unlock();auto r=get_(since);g.lock();++counters_.reads;
        Answer a;if(r.status==200)a=parse(r.body);
        if(a.read){counters_.on=a.on;
#ifndef MUTANT_CHAT_REPEAT
            next_=a.next;
#endif
            for(auto& l:a.lines){++counters_.received;in_.push_back(shown(l));while(in_.size()>64)in_.pop_front();}}
        const uint64_t wait=schedule_(a.read,failures);
        wake_.wait_for(g,std::chrono::milliseconds(wait),[this]{return stop_||!out_.empty();});
    }
}
void Box::draw(){
#ifndef MUTANT_CHAT_NO_CURSOR
    hud_.show(Title,text_.text()+u"_");
#else
    hud_.show(Title,text_.text());
#endif
}
void Box::close(){
    if(!typing_)return;
    typing_=false;text_.clear();keys_.give();hud_.hide();log_("CHAT typing ended\n");
}
void Box::tick(bool pressed,bool held,bool allowed,const std::vector<Press>& extra){
    if(!typing_){
        if(!pressed||!allowed)return;
        if(!keys_.take()){log_("CHAT the keyboard could not be taken\n");return;}
        typing_=true;waitRelease_=true;text_.clear();draw();log_("CHAT typing\n");return;
    }
#ifndef MUTANT_CHAT_EVENT
    if(!allowed){close();return;}
#endif
    auto presses=keys_.presses();presses.insert(presses.end(),extra.begin(),extra.end());
    if(waitRelease_){
        if(held){   // the Y that opened this is not part of the line
            presses.erase(std::remove_if(presses.begin(),presses.end(),[](const Press& p){return p.kind==Press::Char&&(p.ch==u'y'||p.ch==u'Y');}),presses.end());
        }else waitRelease_=false;
    }
    bool changed=false;
    for(auto& p:presses){
        if(p.kind==Press::Escape){close();return;}
        if(p.kind==Press::Enter){auto line=text_.utf8();if(!line.empty()){send_(line);log_("CHAT sent a line\n");}close();return;}
        if(p.kind==Press::Back){text_.back();changed=true;}
        else changed=text_.type(p.ch)||changed;
    }
    if(changed){
        // Showing over a caption makes the game fade the old one out and only then fade the new one in; every key would
        // leave the line blank for a moment. Hidden at once first, the new text is there at once (game_offsets.md).
#ifndef MUTANT_CHAT_REDRAW
        hud_.hide();
#endif
        draw();
    }
}
#ifdef _WIN32
namespace {
std::mutex keyLock;std::vector<Press> keyQueue;HHOOK hook=nullptr;std::atomic<bool> taking{false};
void push(Press p){std::lock_guard<std::mutex> g(keyLock);if(keyQueue.size()<128)keyQueue.push_back(p);}
void translate(const RAWKEYBOARD& k){
    const unsigned vk=k.VKey;
    if(vk==VK_RETURN){push({Press::Enter,0});return;}
    if(vk==VK_ESCAPE){push({Press::Escape,0});return;}
    if(vk==VK_BACK){push({Press::Back,0});return;}
    BYTE state[256]{};
    for(int v:{VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_MENU,VK_LMENU,VK_RMENU})if(GetAsyncKeyState(v)&0x8000)state[v]=0x80;
    if(GetKeyState(VK_CAPITAL)&1)state[VK_CAPITAL]=1;
    wchar_t out[4]{};const int n=ToUnicodeEx(vk,k.MakeCode,state,out,4,4,GetKeyboardLayout(0));   // 4: leaves dead-key state alone
    for(int i=0;i<n;++i)push({Press::Char,char16_t(out[i])});
}
LRESULT CALLBACK onMessage(int code,WPARAM removing,LPARAM l){
    if(code==HC_ACTION&&removing==PM_REMOVE&&taking.load(std::memory_order_acquire)){
        auto* m=reinterpret_cast<MSG*>(l);
        if(m->message==WM_INPUT){
            RAWINPUT in;UINT size=sizeof in;
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(m->lParam),RID_INPUT,&in,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&in.header.dwType==RIM_TYPEKEYBOARD){
                // A key going up always reaches the game, or one held when typing began would stay down there.
                if(!(in.data.keyboard.Flags&RI_KEY_BREAK)){translate(in.data.keyboard);m->message=WM_NULL;}
            }
        }else if(m->message==WM_KEYDOWN||m->message==WM_SYSKEYDOWN||m->message==WM_CHAR||m->message==WM_SYSCHAR||m->message==WM_DEADCHAR)m->message=WM_NULL;
    }
    return CallNextHookEx(nullptr,code,removing,l);
}
}
Keyboard nativeKeyboard(){
    Keyboard k;
    k.take=[]{
        HWND front=GetForegroundWindow();DWORD pid=0;DWORD thread=front?GetWindowThreadProcessId(front,&pid):0;
        {std::lock_guard<std::mutex> g(keyLock);keyQueue.clear();}
        if(thread&&pid==GetCurrentProcessId()&&!hook){
            HMODULE self=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&onMessage),&self);
            hook=SetWindowsHookExW(WH_GETMESSAGE,onMessage,self,thread);}
        taking.store(true,std::memory_order_release);return true;};   // without the window in front only scripted lines arrive
    k.give=[]{taking.store(false,std::memory_order_release);if(hook){UnhookWindowsHookEx(hook);hook=nullptr;}};
    k.presses=[]{std::lock_guard<std::mutex> g(keyLock);auto v=std::move(keyQueue);keyQueue.clear();return v;};
    return k;
}
#else
Keyboard nativeKeyboard(){Keyboard k;k.take=[]{return true;};k.give=[]{};k.presses=[]{return std::vector<Press>{};};return k;}
#endif
}
