#include "pad.h"
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
namespace mod::pad {
namespace {
const std::map<std::string,uint16_t> Buttons={{"UP",0x1},{"DOWN",0x2},{"LEFT",0x4},{"RIGHT",0x8},{"START",0x10},{"BACK",0x20},
    {"LS",0x40},{"RS",0x80},{"LB",0x100},{"RB",0x200},{"A",0x1000},{"B",0x2000},{"X",0x4000},{"Y",0x8000}};
void fail(size_t n,const std::string& why){throw std::runtime_error("pad script line "+std::to_string(n)+": "+why);}
void buttons(const std::string& word,Gamepad& g,size_t n){
    std::stringstream s(word);std::string b;
    while(std::getline(s,b,'+')){
        if(b=="LT")g.leftTrigger=255;else if(b=="RT")g.rightTrigger=255;
        else {auto it=Buttons.find(b);if(it==Buttons.end())fail(n,"unknown button "+b);g.buttons|=it->second;}
    }
}
uint32_t duration(const std::string& word,size_t n){
    if(word.empty()||word.size()>6||word.find_first_not_of("0123456789")!=std::string::npos)fail(n,"duration must be whole ms");
    auto ms=std::stoul(word);
#ifndef MUTANT_PADSCRIPT_BOUNDS
    if(!ms||ms>MaxStepMs)fail(n,"duration out of range");
#endif
    return uint32_t(ms);
}
int16_t axis(const std::string& word,size_t n){
    size_t used=0;double v=0;
    if(word.empty()||word.size()>8||word.find_first_not_of("-.0123456789")!=std::string::npos)fail(n,"axis must be a decimal number");
    try{v=std::stod(word,&used);}catch(const std::exception&){fail(n,"axis must be a number");}
    if(used!=word.size()||!std::isfinite(v)||v<-1||v>1)fail(n,"axis must be in [-1,1]");
    return int16_t(std::lround(v*32767));
}
const std::map<std::string,uint8_t> KeyNames={{"SPACE",0x20},{"ENTER",0x0d},{"ESC",0x1b},{"TAB",0x09},{"BACK",0x08},{"SHIFT",0x10},
    {"CTRL",0x11},{"ALT",0x12},{"UP",0x26},{"DOWN",0x28},{"LEFT",0x25},{"RIGHT",0x27}};
void keys(const std::string& word,Keys& k,size_t n){
    std::stringstream s(word);std::string b;
    while(std::getline(s,b,'+')){
        unsigned vk=0;
        if(b.size()==1&&((b[0]>='A'&&b[0]<='Z')||(b[0]>='0'&&b[0]<='9')))vk=uint8_t(b[0]);
        else if(b.size()>=2&&b.size()<=3&&b[0]=='F'&&b.find_first_not_of("0123456789",1)==std::string::npos){
            auto f=std::stoul(b.substr(1));if(f<1||f>12)fail(n,"unknown key "+b);vk=0x6f+unsigned(f);}
        else if(b.size()==3&&b[0]=='x'&&b.find_first_not_of("0123456789abcdefABCDEF",1)==std::string::npos)vk=unsigned(std::stoul(b.substr(1),nullptr,16));
        else {auto it=KeyNames.find(b);if(it==KeyNames.end())fail(n,"unknown key "+b);vk=it->second;}
        // Mouse buttons are not keys here (click), and 0xff is not a key.
        if(vk<8||vk>0xfe)fail(n,"key out of range "+b);
        if(k.count>=MaxKeys)fail(n,"too many keys");
        bool seen=false;for(size_t i=0;i<k.count;++i)seen=seen||k.vk[i]==vk;
        if(!seen)k.vk[k.count++]=uint8_t(vk);
    }
    if(!k.count)fail(n,"no key");
}
int16_t speed(const std::string& word,size_t n){
    if(word.empty()||word.size()>6||word.find_first_not_of("0123456789",word[0]=='-'?1:0)!=std::string::npos||word=="-")fail(n,"mouse speed must be whole counts per second");
    auto v=std::stol(word);if(v<-MaxMouseSpeed||v>MaxMouseSpeed)fail(n,"mouse speed out of range");
    return int16_t(v);
}
}
std::vector<Step> parse(const std::string& text,const std::string& stamp){
    if(text.size()>MaxBytes)throw std::runtime_error("pad script too large");
    std::vector<Step> steps;std::istringstream in(text);std::string raw;size_t n=0;bool header=false;
    while(std::getline(in,raw)){
        ++n;auto line=raw.substr(0,raw.find('#'));std::istringstream words(line);std::vector<std::string> w;std::string x;
        while(words>>x)w.push_back(x);
        if(w.empty())continue;
        if(!header){
#ifndef MUTANT_PADSCRIPT_STAMP
            if(w.size()!=2||w[0]!="pad"||w[1]!=stamp)fail(n,"first line must be 'pad <run stamp>' of this process");
#endif
            header=true;continue;
        }
        Step s;s.line=line;
        if(w[0]=="press"||w[0]=="hold"){
            if(w.size()!=3)fail(n,w[0]+" BUTTONS MS");
            buttons(w[1],s.pad,n);s.ms=duration(w[2],n);steps.push_back(s);
            if(w[0]=="press"){Step gap;gap.ms=ReleaseGapMs;gap.line="(release)";steps.push_back(gap);}
        } else if(w[0]=="stick"||w[0]=="look"){
            if(w.size()!=4&&!(w[0]=="stick"&&w.size()==5))fail(n,w[0]+" X Y MS"+(w[0]=="stick"?" [BUTTONS]":""));
            auto ax=axis(w[1],n),ay=axis(w[2],n);
            if(w[0]=="stick"){s.pad.lx=ax;s.pad.ly=ay;}else{s.pad.rx=ax;s.pad.ry=ay;}
            s.ms=duration(w[3],n);if(w.size()==5)buttons(w[4],s.pad,n);steps.push_back(s);
        } else if(w[0]=="key"){
            if(w.size()!=3&&!(w.size()==6&&w[3]=="mouse"))fail(n,"key KEYS MS [mouse DX DY]");
            keys(w[1],s.keys,n);s.ms=duration(w[2],n);
            if(w.size()==6){s.keys.mouseX=speed(w[4],n);s.keys.mouseY=speed(w[5],n);}
            steps.push_back(s);
        } else if(w[0]=="tap"||w[0]=="click"){
            if(w.size()!=2&&w.size()!=3)fail(n,w[0]+(w[0]=="tap"?" KEYS [MS]":" L|R|M [MS]"));
            if(w[0]=="tap")keys(w[1],s.keys,n);
            else if(w[1]=="L")s.keys.mouseButtons=1;else if(w[1]=="R")s.keys.mouseButtons=2;else if(w[1]=="M")s.keys.mouseButtons=4;
            else fail(n,"unknown mouse button "+w[1]);
            s.ms=w.size()==3?duration(w[2],n):120;steps.push_back(s);
            Step gap;gap.ms=ReleaseGapMs;gap.line="(release)";steps.push_back(gap);
        } else if(w[0]=="mouse"){
            if(w.size()!=4&&w.size()!=5)fail(n,"mouse DX DY MS [KEYS]");
            s.keys.mouseX=speed(w[1],n);s.keys.mouseY=speed(w[2],n);s.ms=duration(w[3],n);
            if(w.size()==5)keys(w[4],s.keys,n);
            steps.push_back(s);
        } else if(w[0]=="wait"){
            if(w.size()!=2)fail(n,"wait MS");
            s.ms=duration(w[1],n);steps.push_back(s);
        } else fail(n,"unknown command "+w[0]);
        if(steps.size()>MaxSteps)fail(n,"too many steps");
    }
    if(!header)throw std::runtime_error("pad script empty");
    return steps;
}
bool Player::sample(uint64_t now,Gamepad& out,Keys* keys){
    out={};if(keys)*keys={};
    if(done_)return false;
    if(!started_){started_=true;start_=now;}
    uint64_t at=start_;
    for(size_t i=0;i<steps_.size();++i){
        at+=steps_[i].ms;
#ifndef MUTANT_PADSCRIPT_TIMING
        if(now<at){index_=i;out=steps_[i].pad;if(keys)*keys=steps_[i].keys;return true;}
#else
        if(now<=at){index_=i;out=steps_[i].pad;if(keys)*keys=steps_[i].keys;return true;}
#endif
    }
    index_=steps_.size();done_=true;return false;
}
}
