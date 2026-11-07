#include "module_policy.h"
namespace redirect {
bool module_allowed(const ModulePolicy& c,const std::wstring& raw,bool evidence) {
#ifdef MUTANT_MODULE_ALL
    (void)c;(void)raw;(void)evidence;return true;
#else
    if(c.mode==ModuleMode::log_only) return true;
    if(c.mode==ModuleMode::strict) return false;
    auto p=canonical(raw), i=canonical(c.instance), w=canonical(c.windows), s=canonical(c.steam);
    if(!evidence || p.empty() || i.empty() || w.empty() || s.empty()) return false;
#ifdef MUTANT_MODULE_ROOT
    i=s;
#endif
    if(within(p,i)) return true;
#ifndef MUTANT_MODULE_RELEASE_ROOT
    auto game=canonical(c.release_game);
    if(!game.empty()) {
#ifdef MUTANT_MODULE_RELEASE_WIDE
        game=game.substr(0,game.find_last_of(L'\\'));
#endif
        if(within(p,game))return true;
    }
#endif
    if(within(p,w+L"\\system32") || within(p,w+L"\\syswow64")) return true;
    // DriverStore/FileRepository is inside System32 (not an arbitrary DriverStore).
    // Steam client code only: never steamapps, userdata, caches or arbitrary bin DLLs.
    auto at=p.find_last_of(L'\\'); auto dir=p.substr(0,at), base=p.substr(at+1);
#ifndef MUTANT_MODULE_DEFENDER
    // Defender's platform folder: ProgramData\Microsoft\Windows Defender\Platform\<version>\MpOav.dll
    // exactly. Not any ProgramData DLL, not another Defender file, not a deeper folder.
    {
        auto pd=canonical(c.program_data);
        const std::wstring platform=pd+L"\\microsoft\\windows defender\\platform\\";
        if(!pd.empty() && base==L"mpoav.dll" && dir.size()>platform.size() && dir.compare(0,platform.size(),platform)==0) {
            auto version=dir.substr(platform.size());
            bool ok=!version.empty() && version.find(L'\\')==std::wstring::npos;
            for(auto ch:version) if(!((ch>=L'0'&&ch<=L'9')||ch==L'.'||ch==L'-'||ch==L'_')) ok=false;
            if(ok) return true;
        }
    }
#else
    {auto pd=canonical(c.program_data); if(!pd.empty() && within(p,pd)) return true;}
#endif
    return (dir==s || dir==s+L"\\bin" || dir==s+L"\\bin\\win64") &&
        (base==L"steamclient64.dll" || base==L"tier0_s64.dll" || base==L"vstdlib_s64.dll" ||
         base==L"gameoverlayrenderer64.dll");
#endif
}
bool loader_flags_allowed(const ModulePolicy& c,unsigned long flags) {
    if(c.mode==ModuleMode::log_only) return true;
#ifdef MUTANT_MODULE_FLAGS
    return true;
#else
    if(c.mode==ModuleMode::strict) return false;
    // allowlist: data-file (0x2), exclusive data-file (0x40), image-resource (0x20) and DONT_RESOLVE_DLL_REFERENCES (0x1)
    // all map the file without running its initializer or resolving its imports. The path itself must still pass
    // module_allowed. Code in a no-resolve image cannot reach an unhooked file API: its import slots hold names, not
    // addresses, so a call crashes first. Live 2026-11-01: the crypto stack loads System32\cryptnet.dll with 0x11
    // (no-resolve | LOAD_IGNORE_CODE_AUTHZ_LEVEL); refusing it ended the game after ~10 s twice.
    (void)flags; return true;
#endif
}
std::wstring module_request_path(const std::wstring& requested) {
#ifdef MUTANT_MODULE_NO_PREFIX
    return requested;   // the old behaviour: a prefixed driver request is refused and the game ends
#else
#ifndef MUTANT_MODULE_ANY_PREFIX
    if(requested.size()<4 || requested.compare(0,4,L"\\\\?\\")!=0) return requested;
#endif
    auto split=split_namespace(requested);
    return split.kind==PathKind::drive?split.path:requested;
#endif
}
bool is_api_set_name(const std::wstring& r) {
    if(r.find_first_of(L"\\/:")!=std::wstring::npos) return false;
    std::wstring l; for(auto ch:r) l+=(ch>=L'A'&&ch<=L'Z')?wchar_t(ch+32):ch;
    return l.size()>11 && (l.compare(0,11,L"api-ms-win-")==0 || l.compare(0,11,L"ext-ms-win-")==0);
}
RequestAction classify_request(const ModulePolicy& c,const std::wstring& requested,bool already_loaded,bool file_exists) {
    if(c.mode==ModuleMode::log_only || already_loaded) return RequestAction::policy;
#ifdef MUTANT_MODULE_REQUEST
    return RequestAction::policy; // api-set names and absent files reach the policy and end the process
#else
    if(c.mode==ModuleMode::allowlist && is_api_set_name(requested)) return RequestAction::api_set_forward;
    if(!file_exists) return RequestAction::not_found;
    return RequestAction::policy;
#endif
}
bool module_event(const ModulePolicy& c,const std::wstring& p,bool evidence,bool existing,
                  ModulePhase phase,const std::function<void(bool)>& audit,
                  const std::function<void()>& patch) {
    bool allowed=existing || module_allowed(c,p,evidence);
    audit(allowed);
    if(!allowed) return false;
#ifndef MUTANT_MODULE_PATCH
    if(phase==ModulePhase::loaded) patch();
#else
    (void)phase;(void)patch;
#endif
    return true;
}
}
