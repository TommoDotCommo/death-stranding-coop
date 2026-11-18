#include "debugmenu.h"
namespace mod::debugmenu {
std::u16string body(const std::vector<Item>& items,size_t selected){
    std::u16string out=u"Test mode";
    for(size_t i=0;i<items.size();++i){
        out+=i==selected?u"\n> ":u"\n   ";out+=items[i].name;out+=*items[i].on?u": ON":u": OFF";}
    return out;
}
bool Menu::show(unsigned row){
    dialog::Request r;r.body=body(items_,selected_);r.rows={u"Switch",u"Next",u"Close"};r.start=row;
    if(!api_.open(r))return false;
    open_=true;seen_=false;row_=row;++opens_;return true;
}
void Menu::tick(bool pressed,bool allowed,uint64_t now){
    if(open_){
        const unsigned row=api_.take();
        if(row==dialog::NoAnswer){
            const bool up=api_.showing();
            if(up)seen_=true;
#ifndef MUTANT_DEBUGMENU_STUCK
            else if(seen_){open_=false;log_("DEBUGMENU closed by the game\n");}
#endif
            return;
        }
        open_=false;
        if(row==RowSwitch&&!items_.empty()){
            bool& on=*items_[selected_].on;
#ifndef MUTANT_DEBUGMENU_NO_SWITCH
            on=!on;
#endif
            ++switches_;log_("DEBUGMENU switched "+std::to_string(selected_)+(on?" on\n":" off\n"));
            again_=true;row_=RowSwitch;until_=now+ReopenMs;
        }else if(row==RowNext&&!items_.empty()){
#ifndef MUTANT_DEBUGMENU_NEXT
            selected_=(selected_+1)%items_.size();
#else
            selected_=0;
#endif
            again_=true;row_=RowNext;until_=now+ReopenMs;
        }else log_("DEBUGMENU closed\n");
        return;
    }
    if(again_){
#ifndef MUTANT_DEBUGMENU_FOREVER
        if(now>=until_){again_=false;log_("DEBUGMENU not reopened: the game stayed busy\n");return;}
#endif
        if(allowed&&api_.free()&&show(row_))again_=false;
        return;
    }
#ifndef MUTANT_DEBUGMENU_EVENT
    const bool ok=allowed;
#else
    const bool ok=true;
#endif
    if(!pressed)return;
    if(!ok||!api_.free()){log_(ok?"DEBUGMENU not now: a menu or dialogue of the game is up\n":"DEBUGMENU not now: not in the world, or a game event runs\n");return;}
    if(show(RowSwitch))log_("DEBUGMENU opened\n");else log_("DEBUGMENU could not open\n");
}
}
