#include "serverinfo.h"
#include "../world/json.h"
namespace mod::serverinfo {
namespace {
// UTF-8 to UTF-16 for text from the server: control characters and invalid bytes dropped, so it cannot lay out the page.
std::u16string wide(const std::string& s){
    std::u16string out;
    for(size_t i=0;i<s.size();){
        const unsigned char c=s[i];unsigned n=c<0x80?0:(c>>5)==6?1:(c>>4)==14?2:(c>>3)==30?3:4;
        if(n==4||i+n>=s.size()){++i;continue;}
        unsigned v=n==0?c:n==1?c&0x1f:n==2?c&0x0f:c&0x07;bool good=true;
        for(unsigned k=1;k<=n;++k){const unsigned char d=s[i+k];if((d>>6)!=2){good=false;break;}v=v<<6|(d&0x3f);}
        if(!good){++i;continue;}
        i+=n+1;
#ifndef MUTANT_SERVERINFO_CONTROL
        if(v<0x20||v==0x7f)continue;
#endif
        if(v>=0x10000){v-=0x10000;out+=char16_t(0xd800+(v>>10));out+=char16_t(0xdc00+(v&0x3ff));}
        else if(v<0xd800||v>0xdfff)out+=char16_t(v);
    }
    return out;
}
std::u16string number(long n){auto s=std::to_string(n);return std::u16string(s.begin(),s.end());}
std::u16string nameList(const std::vector<std::string>& names){
    std::u16string out;size_t shown=0,total=0;
    for(auto& n:names){auto w=wide(n);if(w.empty())continue;++total;
#ifndef MUTANT_SERVERINFO_NAMES
        if(shown==NamesShown)continue;
#endif
        if(shown++)out+=u", ";out+=w;}
    if(total>shown)out+=u" and "+number(long(total-shown))+u" more";
    return out;
}
}
Policy parse(const std::string& body){
    Policy p;
    try{
        auto d=json::parse(body);
        if(d.get("format").integer()!=1)return p;
        p.name=d.get("name").string();p.preset=d.get("preset").string();
        p.known=long(d.get("players").get("count").integer());p.limit=long(d.get("players").get("limit").integer());
        if(d.has("online")){auto& o=d.get("online");p.onlineKnown=true;p.online=long(o.get("count").integer());for(auto& n:o.get("names").items())p.names.push_back(n.string());}
        for(auto& s:d.get("settings").items())p.rules.push_back(s.get("text").string());
        p.read=true;
    }catch(const std::exception&){p=Policy{};}
    return p;
}
std::vector<std::u16string> pages(const Facts& f,const Policy& p){
    std::u16string first;
    if(f.server&&p.read){
        auto name=wide(p.name);first=name.empty()?u"This server":name;
        if(!p.preset.empty())first+=u" ("+wide(p.preset)+u" rules)";
        if(p.onlineKnown){
            first+=u"\nOnline now: "+number(p.online);
            auto names=nameList(p.names);if(!names.empty())first+=u"\n"+names;
        }
        first+=u"\nPlayers known here: "+number(p.known)+u" of "+number(p.limit);
    }else if(f.server)first=u"The server did not answer.";
    else first=f.group?u"No server.":u"Two-player game";
    if(!f.group){
        auto who=wide(f.partner);if(who.empty())who=u"Partner";
        first+=u"\n"+who+(f.partnerHere?u": connected":u": not connected");
    }
    auto nearby=nameList(f.nearby);first+=u"\nNear you: "+(nearby.empty()?std::u16string(u"nobody"):nearby);
    std::vector<std::u16string> out{first};
    if(!f.server||!p.read)return out;
    const size_t rulePages=(p.rules.size()+RulesPerPage-1)/RulesPerPage;
    for(size_t k=0;k<rulePages;++k){
        std::u16string t=u"Rules "+number(long(k+1))+u" of "+number(long(rulePages));
        for(size_t i=k*RulesPerPage;i<p.rules.size()&&i<(k+1)*RulesPerPage;++i)t+=u"\n"+wide(p.rules[i]);
        out.push_back(t);
    }
    return out;
}
bool Screen::show(){
    dialog::Request r;r.body=pages_[page_];
    if(pages_.size()>1)r.rows={u"Next page",u"Close"};else r.rows={u"Close"};
    if(!api_.open(r)){state_=State::Idle;log_("SERVERINFO could not open\n");return false;}
    state_=State::Open;seen_=false;++opens_;return true;
}
void Screen::tick(bool pressed,bool allowed,const Facts& facts,uint64_t now){
    switch(state_){
    case State::Idle:{
#ifndef MUTANT_SERVERINFO_EVENT
        if(!pressed||!allowed)return;
#else
        if(!pressed)return;
#endif
        if(!api_.free()){log_("SERVERINFO not now: a menu or dialogue of the game is up\n");return;}
        policy_=Policy{};
        if(facts.server&&fetch_.start){fetch_.start();state_=State::Reading;until_=now+WaitMs;return;}
        pages_=pages(facts,policy_);page_=0;if(show())log_("SERVERINFO opened\n");
        return;}
    case State::Reading:{
        auto body=fetch_.poll?fetch_.poll():std::optional<std::string>{};
#ifndef MUTANT_SERVERINFO_WAIT
        if(!body&&now<until_)return;
#else
        if(!body)return;
#endif
        if(body)policy_=parse(*body);
        log_(policy_.read?"SERVERINFO read the server's policy\n":"SERVERINFO no answer from the server\n");
        state_=State::Idle;
        if(!allowed||!api_.free())return;   // the moment passed: nothing pops up later
        pages_=pages(facts,policy_);page_=0;if(show())log_("SERVERINFO opened\n");
        return;}
    case State::Open:{
        const unsigned row=api_.take();
        if(row==dialog::NoAnswer){
            if(api_.showing())seen_=true;else if(seen_){state_=State::Idle;log_("SERVERINFO closed by the game\n");}
            return;
        }
        if(pages_.size()>1&&row==0){
#ifndef MUTANT_SERVERINFO_PAGE
            page_=(page_+1)%pages_.size();
#else
            ++page_;
#endif
            state_=State::Turning;until_=now+ReopenMs;return;
        }
        state_=State::Idle;log_("SERVERINFO closed\n");
        return;}
    case State::Turning:
        if(now>=until_){state_=State::Idle;log_("SERVERINFO not reopened: the game stayed busy\n");return;}
        if(allowed&&api_.free())show();
        return;
    }
}
}
