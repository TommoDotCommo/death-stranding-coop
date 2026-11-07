#include "module_policy.h"
#include <fstream>
#include <iostream>
#include <vector>
using namespace redirect;
int main(int argc,char** argv) {
    int failures=0;
    auto check=[&](bool b){if(!b) ++failures;};
    ModulePolicy c{ModuleMode::allowlist,L"c:\\lab\\s0",L"c:\\windows",L"c:\\steam",L"c:\\programdata"};
    for(auto p:{L"C:/WINDOWS/System32/d3d12.dll",L"c:\\windows\\syswow64\\x.dll",
                L"c:\\windows\\system32\\driverstore\\filerepository\\gpu\\driver.dll",
                L"c:\\lab\\s0\\steam_api64.dll",L"c:\\steam\\steamclient64.dll",
                L"c:\\steam\\bin\\win64\\tier0_s64.dll"}) check(module_allowed(c,p,true));
    for(auto p:{L"c:\\windows2\\system32\\x.dll",L"c:\\lab\\s01\\x.dll",
                L"c:\\steam\\steamapps\\x.dll",L"c:\\steam\\userdata\\steamclient64.dll",
                L"c:\\steam\\bin\\evil.dll",L"c:\\windows\\driverstore\\x.dll",
                L"c:\\lab\\s0\\..\\evil.dll",L"c:\\lab\\S0~1\\x.dll",L"\\\\server\\s0\\x.dll",
                L"x.dll",L"c:x.dll",L"\\\\?\\c:\\windows\\system32\\x.dll"}) check(!module_allowed(c,p,true));
    check(!module_allowed(c,L"c:\\windows\\system32\\junction\\x.dll",false));
    {
        auto release=c;
        release.instance=L"c:\\users\\p\\local\\deathstrandingcoop";
        release.release_game=L"c:\\steam\\steamapps\\common\\ds";
        check(module_allowed(release,L"c:\\steam\\steamapps\\common\\ds\\bink2w64.dll",true));
        check(!module_allowed(release,L"c:\\steam\\steamapps\\common\\ds\\bink2w64.dll",false));
        check(!module_allowed(release,L"c:\\steam\\steamapps\\common\\other\\bink2w64.dll",true));
        check(!module_allowed(release,L"c:\\steam\\steamapps\\common\\ds2\\bink2w64.dll",true));
        check(!module_allowed(c,L"c:\\steam\\steamapps\\common\\ds\\bink2w64.dll",true));
    }
    // Prefixed requests (live 2026-11-07: the new NVIDIA driver loads nvwgf2um.dll as \\?\C:\...): checked in plain form.
    {
        auto nv=module_request_path(L"\\\\?\\C:\\WINDOWS\\System32\\DriverStore\\FileRepository\\nvmdi.inf_amd64_c8fe3aa22cae8673\\nvwgf2um.dll");
        check(nv==L"C:\\WINDOWS\\System32\\DriverStore\\FileRepository\\nvmdi.inf_amd64_c8fe3aa22cae8673\\nvwgf2um.dll");
        check(module_allowed(c,nv,true));
        check(!module_allowed(c,module_request_path(L"\\\\?\\c:\\steam\\bin\\evil.dll"),true));     // stripped, still refused
        check(!module_allowed(c,module_request_path(L"\\\\?\\c:\\lab\\s0\\..\\evil.dll"),true));  // traversal after the prefix
        for(auto p:{L"\\\\.\\c:\\windows\\system32\\x.dll",L"\\\\?\\UNC\\server\\share\\x.dll",
                    L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\windows\\system32\\x.dll",L"\\\\?\\c:"}) {
            check(module_request_path(p)==p);check(!module_allowed(c,module_request_path(p),true));
        }
        check(module_request_path(L"c:\\windows\\system32\\x.dll")==L"c:\\windows\\system32\\x.dll");
    }
    // Defender platform module: exactly <ProgramData>\Microsoft\Windows Defender\Platform\<version>\MpOav.dll.
    check(module_allowed(c,L"C:\\ProgramData\\Microsoft\\Windows Defender\\Platform\\4.18.26100.4-0\\MpOav.dll",true));
    for(auto p:{L"c:\\programdata\\evil\\mpoav.dll",L"c:\\programdata\\x.dll",
                L"c:\\programdata\\microsoft\\windows defender\\platform\\4.18\\other.dll",
                L"c:\\programdata\\microsoft\\windows defender\\platform\\mpoav.dll",
                L"c:\\programdata\\microsoft\\windows defender\\platform\\4.18\\sub\\mpoav.dll",
                L"c:\\programdata\\microsoft\\windows defender\\platform\\evil\\mpoav.dll",
                L"c:\\programdata\\microsoft\\windows defender\\platform\\4.18.1\\mpoav.dll.exe"}) check(!module_allowed(c,p,true));
    check(!module_allowed(c,L"c:\\programdata\\microsoft\\windows defender\\platform\\4.18\\mpoav.dll",false));
    // Negatives that a wrong-root or allow-everything build would admit.
    for(auto p:{L"c:\\users\\someone\\downloads\\evil.dll",L"c:\\users\\someone\\appdata\\local\\temp\\x.dll",
                L"c:\\steam\\steamapps\\common\\death stranding directors cut\\bink2w64.dll",
                L"c:\\steam\\steamapps\\common\\death stranding directors cut\\ds.exe",
                L"c:\\program files\\x\\y.dll",L"c:\\windows\\temp\\x.dll",L"c:\\windows\\x.dll"}) check(!module_allowed(c,p,true));
    // Every module of the live log-only run (136 paths) passes with the instance, Windows, Steam and ProgramData
    // roots of that machine. Observed system paths may differ from the lexical form only in case.
    {
        ModulePolicy live{ModuleMode::allowlist,L"C:\\Games\\DSCoop\\s0",L"C:\\WINDOWS",L"C:\\Program Files (x86)\\Steam",L"C:\\ProgramData"};
        std::ifstream in(argc>1?argv[1]:"module_fixture.txt"); std::string line; size_t count=0;
        while(std::getline(in,line)) {
            if(line.empty()) continue;
            std::wstring w(line.begin(),line.end()); ++count;
            if(!module_allowed(live,w,true)) {++failures;std::wcerr<<L"fixture path refused: "<<w<<L"\n";}
            if(module_allowed(live,w,false)) {++failures;std::wcerr<<L"fixture path admitted without evidence: "<<w<<L"\n";}
        }
        if(count!=136) {++failures;std::cerr<<"fixture has "<<count<<" paths, expected 136\n";}
    }
    // Request classification before the policy: API-set names and files that do not exist cannot execute code.
    check(is_api_set_name(L"api-ms-win-core-winrt-l1-1-0.dll") && is_api_set_name(L"EXT-MS-WIN-core-resourcepolicy-l1-1-0.dll"));
    check(!is_api_set_name(L"c:\\x\\api-ms-win-evil.dll") && !is_api_set_name(L"api-ms-win-x/..\\y.dll") && !is_api_set_name(L"apix.dll"));
    check(classify_request(c,L"ext-ms-win-core-resourcepolicy-l1-1-0.dll",false,false)==RequestAction::api_set_forward);
    check(classify_request(c,L"hkCompatFormats.dll",false,false)==RequestAction::not_found);
    check(classify_request(c,L"c:\\lab\\s0\\x.dll",false,true)==RequestAction::policy);
    check(classify_request(c,L"x.dll",true,false)==RequestAction::policy);
    {auto s=c;s.mode=ModuleMode::strict; check(classify_request(s,L"api-ms-win-core-x-l1-1-0.dll",false,false)==RequestAction::not_found);}
    check(loader_flags_allowed(c,0x2) && loader_flags_allowed(c,0x40) && loader_flags_allowed(c,0x20) && loader_flags_allowed(c,0x1) && loader_flags_allowed(c,0x3) && loader_flags_allowed(c,0x11));
    {auto s=c;s.mode=ModuleMode::strict; check(!loader_flags_allowed(s,0x2));}
    {auto s=c;s.mode=ModuleMode::log_only; check(loader_flags_allowed(s,0x1));}
    {auto s=c;s.mode=ModuleMode::log_only; check(classify_request(s,L"hkCompatFormats.dll",false,false)==RequestAction::policy);}
    // Fake loader: request, mapped dependency, successful return. The patch callback
    // is the observable effect, not a log assertion.
    std::vector<int> events; bool patched=false;
    auto audit=[&](bool allowed){events.push_back(allowed?1:0);};
    auto patch=[&](){patched=true;events.push_back(2);};
    check(module_event(c,L"c:\\lab\\s0\\x.dll",true,false,ModulePhase::request,audit,patch));
    check(!patched);
    check(module_event(c,L"c:\\lab\\s0\\x.dll",true,false,ModulePhase::loaded,audit,patch));
    check(patched && events==std::vector<int>({1,1,2}));
    patched=false;events.clear();
    check(!module_event(c,L"c:\\evil.dll",true,false,ModulePhase::mapped,audit,patch));
    check(!patched && events==std::vector<int>({0}));
    c.mode=ModuleMode::strict;
    check(!module_allowed(c,L"c:\\windows\\system32\\x.dll",true));
    check(module_event(c,L"c:\\windows\\system32\\x.dll",true,true,ModulePhase::loaded,audit,patch));
    c.mode=ModuleMode::log_only;patched=false;
    check(module_event(c,L"relative.dll",false,false,ModulePhase::loaded,audit,patch));check(patched);
    if(failures) std::cerr<<failures<<" module policy/event failures\n";
    return failures?1:0;
}
