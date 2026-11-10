#include "notice.h"
#include "nametag.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace mod::notice {
std::u16string text(Kind kind,const std::string& partnerName){
    auto name=mod::nametag::fromUtf8(partnerName);
    while(!name.empty()&&name.back()==u' ')name.pop_back();
    if(name.empty())name=u"Your partner";
    switch(kind){
    case Kind::Joined:return name+u" joined your session.";
    case Kind::Left:return name+u" left the session.";
    case Kind::Alone:return u"No partner yet. If this lasts, check the firewall on both PCs.";
    case Kind::Away:return name+u" is away (cutscene or private room).";
    case Kind::Back:return name+u" is back.";
    }
    return {};
}
void Notifier::add(Kind kind,const std::string& name,uint64_t now){
#ifndef MUTANT_NOTICE_BOUND
    while(pending_.size()>=MaxPending)pending_.pop_front();
#endif
    Entry e;e.kind=kind;e.name=name;e.since=now;e.nextTry=now;e.named=kind!=Kind::Joined||!name.empty();pending_.push_back(std::move(e));
}
void Notifier::tick(uint64_t now,bool inWorld,bool connected,const std::string& partnerName,bool partnerAway){
    if(!partnerName.empty())lastName_=partnerName;
    if(!started_){started_=true;connected_=false;}
    if(connected!=connected_){
        connected_=connected;
        if(connected){everConnected_=true;add(Kind::Joined,partnerName,now);}
#ifndef MUTANT_NOTICE_LEFT
        else add(Kind::Left,lastName_,now);
#endif
    }
    // Away and back: only while connected, only after the state has lasted (a door transition is not worth a line),
    // and "back" only when "away" was told. A partner who leaves while away just leaves.
    bool away=connected&&partnerAway;
    if(away!=away_){away_=away;awaySince_=now;}
    if(!connected)awayTold_=false;
#ifndef MUTANT_NOTICE_AWAY_SETTLE
    if(away&&!awayTold_&&now-awaySince_>=AwayMs){awayTold_=true;add(Kind::Away,lastName_,now);}
#else
    if(away&&!awayTold_){awayTold_=true;add(Kind::Away,lastName_,now);}
#endif
#ifndef MUTANT_NOTICE_BACK_PAIRED
    if(!away&&awayTold_&&connected){awayTold_=false;add(Kind::Back,lastName_,now);}
#else
    if(!away&&connected&&now==awaySince_&&everConnected_&&shown_){awayTold_=false;add(Kind::Back,lastName_,now);}
#endif
    if(inWorld!=inWorld_){inWorld_=inWorld;worldSince_=now;}
    // Nobody yet: said once, after a continuous stretch in the world, never after a partner has been seen.
#ifndef MUTANT_NOTICE_ALONE_ONCE
    if(!aloneTold_&&!everConnected_&&inWorld&&now-worldSince_>=AloneMs){aloneTold_=true;add(Kind::Alone,{},now);}
#else
    if(!everConnected_&&inWorld&&now-worldSince_>=AloneMs&&pending_.empty()){aloneTold_=true;add(Kind::Alone,{},now);}
#endif
    if(pending_.empty())return;
    auto& e=pending_.front();
    // A join told before the name arrived would say "Your partner": wait a little for it.
    if(e.kind==Kind::Joined&&!e.named){
        if(!partnerName.empty()){e.name=partnerName;e.named=true;}
#ifndef MUTANT_NOTICE_NAME_WAIT
        else if(now-e.since<NameMs)return;
#endif
        else e.named=true;
    }
#ifndef MUTANT_NOTICE_WORLD
    if(!inWorld)return;
#endif
    if(now<e.nextTry)return;
#ifndef MUTANT_NOTICE_GAP
    if(shown_&&now-lastShown_<GapMs)return;
#endif
    if(api_.show&&api_.show(text(e.kind,e.name))){
        ++shown_;lastShown_=now;
        static const char* const names[]={"joined","left","alone","away","back"};
        log_(std::string("NOTICE shown: ")+names[unsigned(e.kind)]+"\n");
        pending_.pop_front();return;
    }
    if(now-e.since>=GiveUpMs){log_("NOTICE dropped: the game's info log did not take it\n");pending_.pop_front();return;}
    e.nextTry=now+RetryMs;
}
Api nativeApi(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native notice API requires Windows");
#else
    struct Entry{uint64_t rva;std::vector<uint8_t> bytes;};
    // The info-log queue's add (text, id, icon, colour, sound, expiry, flags, state mask) as 0x3092cda calls it, and
    // the native UI string from UTF-16 and its release (nametag.cpp uses the same two).
    const std::array<Entry,3> entries={{
        {0x3084d10,{0x44,0x88,0x4c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x56}},
        {0x191ca20,{0x40,0x53,0x48,0x83,0xec,0x20,0x45,0x33,0xc0,0x48,0x8b,0xd9,0x4c,0x89,0x01,0x66}},
        {0x191cad0,{0x48,0x8b,0x09,0xb8,0xff,0xff,0xff,0xff,0xf0,0x0f,0xc1,0x01,0x83,0xf8,0x01,0x0f}}}};
    std::array<unsigned,3> counts{};
    for(auto section:s.sections)if(section.executable){
        if(section.size>128*1024*1024)throw std::runtime_error("notice code size cap");
        std::vector<uint8_t> code(section.size);if(!s.read(section.address,code.data(),code.size()))throw std::runtime_error("notice code unreadable");
        for(unsigned k=0;k<entries.size();++k){auto& e=entries[k];auto at=code.begin();
            while((at=std::search(at,code.end(),e.bytes.begin(),e.bytes.end()))!=code.end()){
                ++counts[k];if(section.address+size_t(at-code.begin())!=s.base+e.rva)throw std::runtime_error("notice function signature moved/duplicated");++at;}}
    }
    for(auto n:counts)if(n!=1)throw std::runtime_error("notice function signature missing/ambiguous");
    using AddFn=void(*)(void* queue,void* text,uint32_t id,uint8_t icon,uint8_t colour,uint8_t sound,float expiry,uint32_t flags,uint32_t* states);
    using StrFn=void(*)(void*,const char16_t*);using ReleaseFn=void(*)(void*);
    auto add=reinterpret_cast<AddFn>(s.base+entries[0].rva);auto make=reinterpret_cast<StrFn>(s.base+entries[1].rva);
    auto release=reinterpret_cast<ReleaseFn>(s.base+entries[2].rva);
    auto* sp=&s;const uint64_t base=s.base;
    Api a;
    a.show=[sp,base,add,make,release](const std::u16string& text)->bool{
        if(text.empty()||text.size()>160)return false;
        auto q=[sp](uint64_t at,uint64_t& v){v=0;return at&&at<0x800000000000ULL&&sp->read(at,&v,8);};
        // The UI owner (0x30125a0 returns this global), its info-log HUD logic (+0x48, as 0x307a4f0 reads it), checked
        // by its vtable and by a plausible item table; the UI clock gives the entry's time, as the game stamps its own.
        uint64_t owner=0,hud=0,vtable=0,table=0,clock=0;int32_t count=0;double now=0;
        if(!q(base+UiOwnerSlot,owner)||!q(owner+0x48,hud)||!q(hud,vtable)||vtable!=base+HudVtable)return false;
        if(!q(hud+0x20,table)||!sp->read(table+0x20,&count,4)||count<=0||count>4096)return false;
        if(!q(base+UiClockSlot,clock)||!sp->read(clock+0xd0,&now,8)||!(now>=0&&now<1e9))return false;
        uint32_t states=States;
        // The game takes this slim reader/writer lock around its own adds (0x3092bae..0x3092cf4).
        auto* lock=reinterpret_cast<SRWLOCK*>(hud+QueueLock);
        alignas(16) unsigned char string[16]{};make(string,text.c_str());
        AcquireSRWLockExclusive(lock);
        add(reinterpret_cast<void*>(hud+Queue),string,NoticeId,Icon,Colour,Sound,float(now),0,&states);
        ReleaseSRWLockExclusive(lock);
        release(string);return true;};
    return a;
#endif
}
}
