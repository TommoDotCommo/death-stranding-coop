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
        } else if(w[0]=="wait"){
            if(w.size()!=2)fail(n,"wait MS");
            s.ms=duration(w[1],n);steps.push_back(s);
        } else fail(n,"unknown command "+w[0]);
        if(steps.size()>MaxSteps)fail(n,"too many steps");
    }
    if(!header)throw std::runtime_error("pad script empty");
    return steps;
}
bool Player::sample(uint64_t now,Gamepad& out){
    out={};
    if(done_)return false;
    if(!started_){started_=true;start_=now;}
    uint64_t at=start_;
    for(size_t i=0;i<steps_.size();++i){
        at+=steps_[i].ms;
#ifndef MUTANT_PADSCRIPT_TIMING
        if(now<at){index_=i;out=steps_[i].pad;return true;}
#else
        if(now<=at){index_=i;out=steps_[i].pad;return true;}
#endif
    }
    index_=steps_.size();done_=true;return false;
}
}
