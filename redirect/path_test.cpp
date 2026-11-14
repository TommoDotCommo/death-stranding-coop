#include "redirect.h"
#include <iostream>
int main() {
    using namespace redirect;
    Policy p{L"c:\\lab\\s0\\redirect-data",L"c:\\steam",{{L"c:\\users\\player\\appdata\\local",L"c:\\lab\\s0\\redirect-data\\local"}}};
    int bad=0;
    auto check=[&](const wchar_t* path,Action action,const wchar_t* target=L"",bool write=true,bool unsafe=false) {
        auto r=map(p,path,write,unsafe);
        if(r.action!=action || (*target && r.path!=target)) { ++bad; std::wcerr<<L"failed: "<<path<<L"\n"; }
    };
    check(L"C:/USERS/PLAYER/AppData/Local/KojimaProductions/DeathStrandingDC/probe",Action::redirect,L"c:\\lab\\s0\\redirect-data\\local\\kojimaproductions\\deathstrandingdc\\probe");
    check(L"c:\\steam\\userdata\\save",Action::deny);
    check(L"c:\\users\\other\\save",Action::deny);
    check(L"c:\\lab\\s0\\redirect-data\\marker",Action::allow);
    check(L"c:\\users\\player\\appdata\\locality\\save",Action::deny);
    check(L"c:\\users\\player\\appdata\\local\\..\\save",Action::deny);
    check(L"c:\\lab\\s0\\redirect-data\\..\\ds.exe",Action::deny);
    check(L"c:\\lab\\s0\\redirect-data\\alias",Action::deny,L"",true,true);
    for(auto s:{L"c:save",L"x",L"c:\\x:ads",L"c:\\x.\\a",L"c:\\con",L"c:\\progra~1\\x"}) check(s,Action::deny);
    check(L"c:\\windows\\system32\\kernel32.dll",Action::allow,L"",false);
    // (1) Device namespaces: HID interface paths pass through untouched, in either prefix form.
    const wchar_t* hid=L"\\\\?\\hid#vid_1532&pid_02a7&mi_03#9&2fcb1b56&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    check(hid,Action::device,hid,true);check(hid,Action::device,hid,false);
    check(L"\\\\.\\HID#VID_048D&PID_5711&MI_01#A&2DC6C1AB&0&0000#{4D1E55B2-F16F-11CF-88CB-001111000030}",Action::device);
    check(hid,Action::deny,L"",true,true); // unsafe filesystem evidence still wins
    // ...and nothing else behind a prefix does: other device classes, raw disks, volumes, UNC, malformed HID paths.
    for(auto s:{L"\\\\.\\PhysicalDrive0",L"\\\\.\\C:",L"\\\\?\\C:",L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\x",
                L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\x",L"\\\\?\\UNC\\server\\share\\x",L"\\\\.\\pipe\\x",
                L"\\\\?\\storage#volume#_??_usbstor#disk&ven_x#{53f5630d-b6bf-11d0-94f2-00a0c91efb8b}",
                L"\\\\?\\hid#a#b{4d1e55b2-f16f-11cf-88cb-001111000030}",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\..\\x",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\kbd\\..",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\k:bd",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\k*d",L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-001111000030}\\kbd\\\\x",
                L"\\\\?\\hid#a#b#c#{4d1e55b2-f16f-11cf-88cb-00111100003f}",L"\\\\server\\share\\x",L"\\\\?\\"}) {check(s,Action::deny,L"",true);check(s,Action::deny,L"",false);}
    // (1b) The live run's last four DENY records ended in a device-relative component: any safe suffix is covered.
    for(auto suffix:{L"\\kbd",L"\\KBD",L"\\col01\\kbd",L"\\a.b"}) {
        std::wstring dev=std::wstring(hid)+suffix;
        check(dev.c_str(),Action::device,dev.c_str(),true);check(dev.c_str(),Action::device,dev.c_str(),false);
        check(dev.c_str(),Action::deny,L"",true,true);
    }
    check(L"\\\\?\\hid#vid_1532&pid_02a7&mi_01&col01#9&2fcb1b56&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}\\kbd",Action::device);
    // (1c) XInput's interface (XUSB): a Bluetooth Xbox pad (hid#, the shape of the live 2026-11-13 path in both letter
    // cases, with the pad's own address and instance replaced) and a wired one (usb#). The usb# head passes with that
    // interface only, the interface passes behind hid# and usb# only, and a near-miss guid stays refused.
    for(auto s:{L"\\\\?\\hid#{00001812-0000-1000-8000-00805f9b34fb}&dev&vid_045e&pid_0b22&rev_0522&0123456789ab&col01&ig_00#c&1a2b3c4&0&0000#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}",
                L"\\\\?\\HID#{00001812-0000-1000-8000-00805f9b34fb}&Dev&VID_045e&PID_0b22&REV_0522&0123456789ab&Col01&IG_00#c&1a2b3c4&0&0000#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}",
                L"\\\\?\\usb#vid_045e&pid_02ea#0123456789abcd#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}"}) {
        check(s,Action::device,s,true);check(s,Action::device,s,false);check(s,Action::deny,L"",true,true);
    }
    for(auto s:{L"\\\\?\\usb#vid_045e&pid_02ea#0123456789abcd#{4d1e55b2-f16f-11cf-88cb-001111000030}",
                L"\\\\?\\usb#vid_0781&pid_5581#0123456789#{a5dcbf10-6530-11d2-901f-00c04fb951ed}",
                L"\\\\?\\hid#a#b#c#{ec87f1e3-c13b-4100-b5f7-8b84d54260cf}",L"\\\\?\\usbstor#a#b#c#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}",
                L"\\\\?\\usb#a#b{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}",L"\\\\?\\usb#a#b#c#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}\\..\\x"}) {
        check(s,Action::deny,L"",true);check(s,Action::deny,L"",false);
    }
    // (2) Drive paths behind \\?\ and \\.\ are classified like the plain path.
    const wchar_t* local=L"c:\\lab\\s0\\redirect-data\\local\\nvidia\\dxcache\\x.nvph";
    check(L"\\\\?\\C:\\Users\\Player\\AppData\\Local\\NVIDIA\\DXCache\\x.nvph",Action::redirect,local);
    check(L"\\\\.\\C:\\Users\\Player\\AppData\\Local\\NVIDIA\\DXCache\\x.nvph",Action::redirect,local);
    check(L"//?/C:/Users/Player/AppData/Local/NVIDIA/DXCache/x.nvph",Action::redirect,local);
    check(L"\\\\?\\c:\\x",Action::deny,L"",true);                           // unmapped write
    check(L"\\\\?\\c:\\x",Action::allow,L"c:\\x",false);                  // unmapped read keeps the plain-path rule
    check(L"\\\\?\\c:\\steam\\userdata\\save",Action::deny);
    check(L"\\\\?\\c:\\lab\\s0\\redirect-data\\marker",Action::allow);
    check(L"\\\\?\\c:\\users\\player\\appdata\\local\\..\\save",Action::deny);      // traversal after the prefix
    check(L"\\\\?\\c:\\users\\player\\appdata\\local\\..\\..\\..\\steam\\x",Action::deny);
    check(L"\\\\?\\c:\\lab\\s0\\redirect-data\\..\\ds.exe",Action::deny);
    check(L"\\\\?\\c:\\users\\player\\appdata\\locality\\save",Action::deny);
    check(L"\\\\?\\c:\\x:ads",Action::deny);check(L"\\\\?\\c:\\con",Action::deny);check(L"\\\\?\\c:save",Action::deny);
    {auto s=split_namespace(L"\\\\?\\C:\\a");if(s.kind!=PathKind::drive||s.path!=L"C:\\a"){++bad;std::wcerr<<L"failed: split drive\n";}
     s=split_namespace(L"C:\\a");if(s.kind!=PathKind::plain){++bad;std::wcerr<<L"failed: split plain\n";}}
    // (5) Driver cache and settings: LocalLow and two NVIDIA settings files map into the instance, nothing wider.
    {
        Policy q=p;
        for(auto& m:driver_roots(L"c:\\users\\player\\appdata\\local",L"C:\\ProgramData",p.instance)) q.roots.push_back(m);
        auto chk=[&](const wchar_t* path,Action action,const wchar_t* target=L"",bool write=true) {
            auto r=map(q,path,write,false);
            if(r.action!=action || (*target && r.path!=target)) { ++bad; std::wcerr<<L"failed (driver roots): "<<path<<L"\n"; }
        };
        chk(L"\\\\?\\C:\\Users\\Player\\AppData\\LocalLow\\NVIDIA\\DXCache\\x.nvph",Action::redirect,L"c:\\lab\\s0\\redirect-data\\locallow\\nvidia\\dxcache\\x.nvph");
        chk(L"C:\\Users\\Player\\AppData\\LocalLow\\NVIDIA\\DXCache\\x.nvph",Action::redirect);
        chk(L"C:\\ProgramData\\NVIDIA Corporation\\Drs\\nvAppTimestamps",Action::redirect,L"c:\\lab\\s0\\redirect-data\\programdata\\nvidia-drs\\nvapptimestamps");
        chk(L"C:\\ProgramData\\NVIDIA Corporation\\Drs\\nvdrssel.bin",Action::redirect);
        chk(L"C:\\ProgramData\\NVIDIA Corporation\\Drs\\nvdrsdb0.bin",Action::deny);   // the profile database is never written
        chk(L"C:\\ProgramData\\NVIDIA Corporation\\Drs\\nvdrsdb0.bin",Action::allow,L"",false);
        chk(L"C:\\ProgramData",Action::deny);chk(L"C:\\ProgramData\\Other\\x",Action::deny);
        chk(L"C:\\WINDOWS\\System32\\CatRoot2\\dberr.txt",Action::deny);chk(L"C:\\WINDOWS\\system32\\catroot2",Action::deny);
        chk(L"C:\\Users\\Player\\AppData\\LocalLowX\\x",Action::deny);
        if(!driver_roots(L"c:\\users\\player\\documents",L"",p.instance).empty()) {++bad;std::wcerr<<L"failed: driver roots from a non-AppData\\Local root\n";}
        // A reparse-flag open is refused for writes to unmapped targets, never for redirected or read-only opens.
        if(reparse_open_allowed(true,Action::allow) || !reparse_open_allowed(true,Action::redirect) || !reparse_open_allowed(false,Action::allow)) {++bad;std::wcerr<<L"failed: reparse_open_allowed\n";}
    }
    // (6) The loader-flags record prints the value in hex (the live run printed decimal digits after "0x").
    {
        struct { unsigned long v; const wchar_t* hex; } cases[]={{0,L"0"},{0x20,L"20"},{0x1002,L"1002"},{0x800,L"800"},{0xffffffffUL,L"ffffffff"},{0x2a,L"2a"}};
        for(auto& k:cases) if(hex_flags(k.v)!=k.hex) {++bad;std::wcerr<<L"failed: hex_flags "<<k.v<<L" gives "<<hex_flags(k.v)<<L"\n";}
    }
    // (7) Queries obey the roots: the real save tree is never read by an instance, the instance copy answers.
    {
        Policy q{L"c:\\lab\\s0\\redirect-data",L"c:\\steam",{
            {L"c:\\users\\player\\appdata\\local",L"c:\\lab\\s0\\redirect-data\\local"},
            {L"c:\\users\\player\\documents",L"c:\\lab\\s0\\redirect-data\\documents"}}};
        const std::wstring real=L"C:\\Users\\Player\\AppData\\Local\\KojimaProductions\\DeathStrandingDC\\12345678";
        const std::wstring inst=L"c:\\lab\\s0\\redirect-data\\local\\kojimaproductions\\deathstrandingdc\\12345678";
        auto query=[&](const std::wstring& path,Action action,const std::wstring& target=L"",bool wild=false) {
            auto r=map_query(q,path,wild);
            if(r.action!=action || (!target.empty() && r.path!=target)) {++bad;std::wcerr<<L"failed (query): "<<path<<L" -> "<<(int)r.action<<L" "<<r.path<<L"\n";}
        };
        query(real+L"\\profile\\profile.dat",Action::redirect,inst+L"\\profile\\profile.dat");   // a read of the real profile.dat
        query(real,Action::redirect,inst);                                                          // attributes of the real account folder
        query(real+L"\\",Action::redirect,inst);
        query(L"c:/users/PLAYER/appdata/local/kojimaproductions/deathstrandingdc/12345678/autosave0",Action::redirect,inst+L"\\autosave0");
        query(L"\\\\?\\"+real+L"\\profile",Action::redirect,inst+L"\\profile");                       // prefixed drive path
        query(L"C:\\Users\\Player\\Documents\\My Games\\x.ini",Action::redirect,L"c:\\lab\\s0\\redirect-data\\documents\\my games\\x.ini"); // second root: its own copy
        // Enumeration: the wildcard pattern under the real root lists the instance copy.
        query(real+L"\\*",Action::redirect,inst+L"\\*",true);
        query(real+L"\\*.sav",Action::redirect,inst+L"\\*.sav",true);
        query(real+L"\\profile\\???.dat",Action::redirect,inst+L"\\profile\\???.dat",true);
        query(L"C:\\Users\\Player\\AppData\\Local\\*",Action::redirect,L"c:\\lab\\s0\\redirect-data\\local\\*",true);
        query(L"C:\\Users\\Player\\Documents\\*",Action::redirect,L"c:\\lab\\s0\\redirect-data\\documents\\*",true);
        query(real+L"\\*",Action::deny,L"",false);                       // a wildcard is not a name unless the API takes patterns
        // Outside the roots, in the instance, devices and other namespaces: untouched, as before.
        query(L"c:\\windows\\system32\\kernel32.dll",Action::allow);
        query(L"c:\\steam\\steamapps\\common\\x\\data\\a.bin",Action::allow);
        query(L"c:\\users\\player\\appdata\\locality\\x",Action::allow);
        query(L"c:\\users\\player\\appdata\\*",Action::allow,L"",true);
        query(L"c:\\lab\\s0\\redirect-data\\local\\x",Action::allow);
        query(L"c:\\lab\\s0\\redirect-data\\*",Action::allow,L"",true);
        query(hid,Action::allow);query(L"\\\\?\\UNC\\server\\share\\x",Action::allow);query(L"\\\\.\\pipe\\x",Action::allow);
        query(L"relative\\x",Action::allow);query(L"c:save",Action::allow);
        // Anything that normalises into a real root without being a clean path is denied, never passed through.
        query(L"c:\\users\\player\\x\\..\\appdata\\local\\kojimaproductions",Action::deny);
        query(real+L"\\..\\..\\.\\DeathStrandingDC",Action::deny);
        query(real+L".\\profile",Action::deny);query(real+L"::$DATA",Action::deny);
        query(L"c:\\users\\player\\appdata\\local\\kojimaproductions \\x",Action::deny);
        query(L"c:\\users\\player\\appdata\\local\\..\\..\\documents\\x",Action::deny);
        query(L"c:\\users\\player\\appdata\\local\\kojimaproductions\\con",Action::deny);
        // Short names may alias a real root: the caller may expand them and ask again.
        query(L"c:\\users\\USERNA~1\\appdata\\local\\x",Action::deny,L"alias");
        query(L"c:\\progra~1\\x",Action::deny,L"alias");
        query(L"c:\\lab\\s0\\redirect-data\\progra~1\\x",Action::allow);
        // The instance may not sit inside a real root: every query is then refused.
        Policy bad_root{L"c:\\users\\player\\appdata\\local\\inst",L"c:\\steam",{{L"c:\\users\\player\\appdata\\local",L"c:\\users\\player\\appdata\\local\\inst\\local"}}};
        if(map_query(bad_root,L"c:\\users\\player\\appdata\\local\\x").action!=Action::deny) {++bad;std::wcerr<<L"failed: instance inside a real root\n";}
    }
    std::cout<<"failures="<<bad<<"\n"; return bad?1:0;
}
