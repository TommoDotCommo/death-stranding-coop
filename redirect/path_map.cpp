#include "redirect.h"
#include <cwctype>
namespace redirect {
std::wstring canonical(const std::wstring& input) {
    auto p = input;
    for (auto& c : p) {
        if (c < 32 || c > 126) return L""; // fail closed on unreviewed case aliases
        if (c == L'/') c = L'\\';
#ifndef MUTANT_CASE
        c = std::towlower(c);
#endif
    }
    if (p.size()<3 || p[0]<L'a' || p[0]>L'z' || p[1]!=L':' || p[2]!=L'\\') return L"";
    if (p.size()>3 && p.back()==L'\\') p.pop_back();
    for (size_t at=3; at<p.size();) {
        auto end=p.find(L'\\',at); if(end==std::wstring::npos) end=p.size();
        auto c=p.substr(at,end-at);
#ifndef MUTANT_TRAVERSAL
        if(c.empty() || c==L"." || c==L".." || c.back()==L'.' || c.back()==L' ') return L"";
#endif
        if(c.find_first_of(L":*?\"<>|~")!=std::wstring::npos) return L"";
        auto stem=c.substr(0,c.find(L'.'));
        if(stem==L"con" || stem==L"prn" || stem==L"aux" || stem==L"nul" ||
           (stem.size()==4 && (stem.substr(0,3)==L"com" || stem.substr(0,3)==L"lpt") && stem[3]>=L'0' && stem[3]<=L'9')) return L"";
        at=end+1;
    }
    return p;
}
bool within(const std::wstring& p,const std::wstring& r) {
    return !r.empty() && (p==r || (p.size()>r.size() && p.compare(0,r.size(),r)==0 && p[r.size()]==L'\\'));
}
std::wstring hex_flags(unsigned long v) {
#ifdef MUTANT_HEX
    return std::to_wstring(v); // the live-run bug: decimal digits after a 0x prefix
#else
    if(!v) return L"0";
    std::wstring out;
    for(;v;v>>=4) out.insert(out.begin(),L"0123456789abcdef"[v&15]);
    return out;
#endif
}
static const wchar_t* const hid_interface_guid=L"#{4d1e55b2-f16f-11cf-88cb-001111000030}";
Split split_namespace(const std::wstring& raw) {
    std::wstring s=raw;
    for(auto& c:s) if(c==L'/') c=L'\\';
    if(s.size()<4 || s[0]!=L'\\' || s[1]!=L'\\' || (s[2]!=L'?' && s[2]!=L'.') || s[3]!=L'\\') return {PathKind::plain,raw};
    auto body=s.substr(4);
    if(body.size()>=3 && ((body[0]>=L'a'&&body[0]<=L'z')||(body[0]>=L'A'&&body[0]<=L'Z')) && body[1]==L':' && body[2]==L'\\') {
#ifdef MUTANT_PREFIX
        return {PathKind::plain,raw}; // prefix left in place: the classifier no longer sees a drive path
#else
        return {PathKind::drive,body};
#endif
    }
    std::wstring lower;
    for(auto c:body) {
        if(c<32 || c>126) return {PathKind::refused,L""};
        lower+=(c>=L'A'&&c<=L'Z')?wchar_t(c+32):c;
    }
    // hid#<vid/pid/instance>#{interface guid}[\\<reference string>...]: the live run opened `...{guid}\\kbd`.
    // The suffix is device-relative; it may not climb (`.`/`..`), be empty or carry separators of other kinds.
    const std::wstring tail=hid_interface_guid, head=L"hid#";
    std::wstring device=lower, suffix;
    auto slash=lower.find(L'\\');
    if(slash!=std::wstring::npos) {
#ifdef MUTANT_SUFFIX
        // the old rule: nothing may follow the interface guid
#else
        device=lower.substr(0,slash);suffix=lower.substr(slash+1);
        for(size_t at=0;;) {
            auto end=suffix.find(L'\\',at);auto c=suffix.substr(at,end==std::wstring::npos?end:end-at);
            if(c.empty() || c==L"." || c==L".." || c.back()==L'.' || c.back()==L' ') return {PathKind::refused,L""};
            if(end==std::wstring::npos) break;
            at=end+1;
        }
#endif
    }
    size_t hashes=0; for(auto c:device) if(c==L'#') ++hashes;
    bool hid=device.size()>head.size()+tail.size() && device.compare(0,head.size(),head)==0 &&
        device.compare(device.size()-tail.size(),tail.size(),tail)==0 && hashes>=3 &&
        device.find_first_of(L"\\:*?\"<>|/")==std::wstring::npos &&
        suffix.find_first_of(L":*?\"<>|/")==std::wstring::npos && (slash==std::wstring::npos || !suffix.empty());
    if(hid) return {PathKind::device,raw};
    return {PathKind::refused,L""};
}
std::vector<Mapping> driver_roots(const std::wstring& real_local,const std::wstring& program_data,const std::wstring& instance) {
    std::vector<Mapping> out;
    auto local=canonical(real_local), pd=canonical(program_data), inst=canonical(instance);
    const std::wstring suffix=L"\\appdata\\local";
    if(!inst.empty() && local.size()>suffix.size() && local.compare(local.size()-suffix.size(),suffix.size(),suffix)==0)
        out.push_back({local+L"low",inst+L"\\locallow"}); // %USERPROFILE%\AppData\LocalLow, a sibling of Local
    if(!inst.empty() && !pd.empty()) {
        // Only the two files the driver wrote in the live run, not the ProgramData tree.
        const std::wstring drs=pd+L"\\nvidia corporation\\drs";
#ifdef MUTANT_NVROOT
        out.push_back({pd,inst+L"\\programdata"});
#else
        for(auto name:{L"nvapptimestamps",L"nvdrssel.bin"}) out.push_back({drs+L"\\"+name,inst+L"\\programdata\\nvidia-drs\\"+name});
#endif
    }
    return out;
}
bool reparse_open_allowed(bool write,Action a) {
#ifdef MUTANT_REPARSE
    (void)write;(void)a;return true;
#else
    return !write || a==Action::redirect;
#endif
}
Result map(const Policy& c,const std::wstring& raw,bool write,bool unsafe) {
    auto sp=split_namespace(raw);
    if(sp.kind==PathKind::device) {
#ifdef MUTANT_DEVICE
        return {Action::deny,raw}; // the old behaviour: a device handle is treated as an unmapped file
#else
        return {unsafe?Action::deny:Action::device,raw};
#endif
    }
    if(sp.kind==PathKind::refused) return {Action::deny,L""};
    auto p=canonical(sp.path), instance=canonical(c.instance), steam=canonical(c.steam);
    auto deny=[&]() { return Result{
#ifdef MUTANT_DENY
        Action::allow,
#else
        Action::deny,
#endif
        p}; };
    if(p.empty() || instance.empty() || steam.empty() || unsafe) return deny();
    if(write && within(p,steam)) return deny();
    if(within(p,instance)) return {Action::allow,p};
    for(auto& r:c.roots) {
        auto real=canonical(r.real), target=canonical(r.target);
        if(!within(target,instance) || within(instance,real)) return deny();
        if(within(p,real)) {
#ifdef MUTANT_ROOT
            return {Action::redirect,p};
#else
            return {Action::redirect,target+p.substr(real.size())};
#endif
        }
    }
    if(write) return deny();
    return {Action::allow,p}; // reads of game/system/driver/cache data cannot mutate them
}
}

namespace redirect {
namespace {
// Lexical normal form of a drive path that failed the strict classifier: separators, ASCII case, `.`/`..`,
// trailing dots/spaces and `:stream` suffixes removed per component, as Windows would. "" if not drive-absolute.
std::wstring lenient(const std::wstring& raw) {
    std::wstring s=raw;
    for(auto& c:s) if(c==L'/') c=L'\\';
    if(s.size()<3 || !((s[0]>=L'a'&&s[0]<=L'z')||(s[0]>=L'A'&&s[0]<=L'Z')) || s[1]!=L':' || s[2]!=L'\\') return L"";
    std::vector<std::wstring> parts;
    for(size_t at=3;at<=s.size();) {
        auto end=s.find(L'\\',at); if(end==std::wstring::npos) end=s.size();
        auto c=s.substr(at,end-at);
        auto colon=c.find(L':'); if(colon!=std::wstring::npos) c.resize(colon);
        while(!c.empty() && (c.back()==L'.' || c.back()==L' ')) { if(c==L"." || c==L"..") break; c.pop_back(); }
        for(auto& ch:c) if(ch>=L'A'&&ch<=L'Z') ch=wchar_t(ch+32);
        if(c==L".." ) { if(!parts.empty()) parts.pop_back(); }
        else if(!c.empty() && c!=L".") parts.push_back(c);
        at=end+1;
    }
    std::wstring out(1,wchar_t((s[0]>=L'A'&&s[0]<=L'Z')?s[0]+32:s[0]));out+=L":\\";
    for(size_t i=0;i<parts.size();++i) out+=(i?L"\\":L"")+parts[i];
    return out;
}
bool wildcard_leaf(const std::wstring& leaf) {
    if(leaf.empty() || leaf==L"." || leaf==L".." || leaf.compare(0,2,L"..")==0) return false;
    bool wild=false;
    for(auto c:leaf) {
        if(c<32 || c>126 || c==L':' || c==L'|' || c==L'\\' || c==L'/' || c==L'~') return false;
        if(c==L'*' || c==L'?' || c==L'<' || c==L'>' || c==L'"') wild=true;
    }
    return wild;
}
}
Result map_query(const Policy& c,const std::wstring& raw,bool wildcard) {
    auto sp=split_namespace(raw);
    if(sp.kind==PathKind::device || sp.kind==PathKind::refused) return {Action::allow,raw}; // not a save path: untouched, as before
    auto instance=canonical(c.instance);
    std::wstring p;
#ifndef MUTANT_FIND
    if(wildcard) {
        auto at=sp.path.find_last_of(L"\\/");
        if(at!=std::wstring::npos && at>=2 && wildcard_leaf(sp.path.substr(at+1))) {
            auto dir=canonical(sp.path.substr(0,at==2?3:at));
            if(!dir.empty()) p=(dir.back()==L'\\'?dir:dir+L"\\")+sp.path.substr(at+1);
        }
    }
#endif
    if(p.empty()) p=canonical(sp.path);
    if(!p.empty()) {
        if(instance.empty()) return {Action::deny,p};
        if(within(p,instance)) return {Action::allow,raw};
        for(auto& r:c.roots) {
            auto real=canonical(r.real), target=canonical(r.target);
            if(!within(target,instance) || within(instance,real)) return {Action::deny,p};
            if(within(p,real)) {
#ifdef MUTANT_QUERY_READS
                return {Action::allow,raw}; // the old behaviour: queries see the real folder
#else
#ifdef MUTANT_QUERY_ROOT0
                return {Action::redirect,canonical(c.roots[0].target)+p.substr(real.size())}; // every root answered from the first root's copy
#else
                return {Action::redirect,target+p.substr(real.size())};
#endif
#endif
            }
        }
        return {Action::allow,raw};
    }
    // Not a clean path. Windows would normalise it; do the same lexically and never let it reach a real root.
    auto lp=lenient(sp.path);
    if(lp.empty()) return {Action::allow,raw};
    if(!instance.empty() && within(lp,instance)) return {Action::allow,raw};
    for(auto& r:c.roots) if(within(lp,canonical(r.real))) return {Action::deny,lp};
    if(lp.find(L'~')!=std::wstring::npos) return {Action::deny,L"alias"};
    return {Action::allow,raw};
}
}
