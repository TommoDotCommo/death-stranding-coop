#pragma once
// Reads Windows Firewall the way Windows lists it (INetFwPolicy2, read-only, no elevation needed) and gives the
// verdict of core.h for the game's incoming UDP. Any failure reads as Firewall::Unknown: the launcher then says it
// could not tell, and nothing else depends on it.
#include "core.h"
#include <windows.h>
#include <netfw.h>
#include <oleauto.h>
#include <filesystem>
namespace launcher {
namespace firewall_detail {
inline std::string narrow(BSTR s){
    if(!s)return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s,int(SysStringLen(s)),nullptr,0,nullptr,nullptr);std::string out(size_t(n>0?n:0),'\0');
    if(n>0)WideCharToMultiByte(CP_UTF8,0,s,int(SysStringLen(s)),out.data(),n,nullptr,nullptr);
    return out;
}
template<class T> struct Held {T* p=nullptr;~Held(){if(p)p->Release();}T** operator&(){return &p;}T* operator->(){return p;}explicit operator bool()const{return p!=nullptr;}};
}
// `ports`: every UDP port the game will listen on; the worst verdict among them is returned.
inline Firewall readFirewall(const std::filesystem::path& gameExe,std::initializer_list<unsigned> ports,size_t* ruleCount=nullptr){
    using namespace firewall_detail;
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool uninit=SUCCEEDED(init);
    Firewall verdict=Firewall::Unknown;
    {
        Held<INetFwPolicy2> policy;
        if(SUCCEEDED(CoCreateInstance(__uuidof(NetFwPolicy2),nullptr,CLSCTX_INPROC_SERVER,__uuidof(INetFwPolicy2),reinterpret_cast<void**>(&policy)))&&policy){
            long active=0;bool on=false,ok=SUCCEEDED(policy->get_CurrentProfileTypes(&active));
            for(long profile:{long(NET_FW_PROFILE2_DOMAIN),long(NET_FW_PROFILE2_PRIVATE),long(NET_FW_PROFILE2_PUBLIC)})if(ok&&(active&profile)){
                VARIANT_BOOL enabled=VARIANT_FALSE;if(FAILED(policy->get_FirewallEnabled(NET_FW_PROFILE_TYPE2(profile),&enabled)))ok=false;else on=on||enabled!=VARIANT_FALSE;
            }
            std::vector<FirewallRule> rules;Held<INetFwRules> list;Held<IUnknown> unknown;Held<IEnumVARIANT> each;
            if(ok&&on){
                ok=SUCCEEDED(policy->get_Rules(&list))&&list&&SUCCEEDED(list->get__NewEnum(&unknown))&&unknown
                    &&SUCCEEDED(unknown->QueryInterface(__uuidof(IEnumVARIANT),reinterpret_cast<void**>(&each)))&&each;
                while(ok){
                    VARIANT item;VariantInit(&item);ULONG got=0;if(each->Next(1,&item,&got)!=S_OK||!got)break;
                    Held<INetFwRule> rule;
                    if(item.vt==VT_DISPATCH&&item.pdispVal&&SUCCEEDED(item.pdispVal->QueryInterface(__uuidof(INetFwRule),reinterpret_cast<void**>(&rule)))&&rule){
                        FirewallRule r;BSTR text=nullptr;VARIANT_BOOL enabled=VARIANT_FALSE;NET_FW_ACTION action=NET_FW_ACTION_BLOCK;NET_FW_RULE_DIRECTION direction=NET_FW_RULE_DIR_OUT;long number=0;
                        if(SUCCEEDED(rule->get_ApplicationName(&text))){r.application=narrow(text);SysFreeString(text);text=nullptr;}
                        if(SUCCEEDED(rule->get_Enabled(&enabled)))r.enabled=enabled!=VARIANT_FALSE;
                        if(SUCCEEDED(rule->get_Action(&action)))r.allow=action==NET_FW_ACTION_ALLOW;
                        if(SUCCEEDED(rule->get_Direction(&direction)))r.inbound=direction==NET_FW_RULE_DIR_IN;
                        if(SUCCEEDED(rule->get_Protocol(&number)))r.protocol=int(number);
                        if(SUCCEEDED(rule->get_LocalPorts(&text))){r.localPorts=narrow(text);SysFreeString(text);text=nullptr;}
                        if(SUCCEEDED(rule->get_Profiles(&number)))r.profiles=unsigned(number);
                        rules.push_back(std::move(r));
                    }
                    VariantClear(&item);
                }
            }
            if(ok){
                if(ruleCount)*ruleCount=rules.size();
                auto rank=[](Firewall f){return f==Firewall::Blocked?3:f==Firewall::NoRule?2:f==Firewall::Allowed?1:0;};
                verdict=Firewall::Off;bool first=true;
                for(unsigned port:ports){auto v=firewallVerdict(rules,gameExe.u8string(),on,unsigned(active),port);if(first||rank(v)>rank(verdict))verdict=v;first=false;}
            }
        }
    }
    if(uninit)CoUninitialize();
    return verdict;
}
}
