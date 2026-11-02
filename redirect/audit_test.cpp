#include "audit_throttle.h"
#include <iostream>
using namespace redirect;
int main() {
    int bad=0; auto check=[&](bool b,const char* what){if(!b){++bad;std::cerr<<"failed: "<<what<<"\n";}};
    std::vector<std::wstring> lines; size_t bytes=0;
    Throttle::Sink sink=[&](const std::wstring& l){lines.push_back(l);bytes+=l.size()*2;};
    // Repeats: identical records are written once, counted, and flushed on the interval.
    Throttle t(5000);
    uint64_t now=1000;
    for(int i=0;i<100;++i) t.record(L"DENY",L"c:\\x",L"",now+=10,sink);
    check(lines.size()==1 && lines[0].find(L"id=1")!=std::wstring::npos,"first record written once, 100 repeats within 1 s are silent");
    t.record(L"DEVICE",L"\\\\?\\hid#a",L"",now+=10,sink);
    check(lines.size()==2,"a different record is written at once");
    now+=5000; t.record(L"DENY",L"c:\\x",L"",now,sink);
    check(lines.size()==3 && lines[2].find(L"REPEATS")==0 && lines[2].find(L"1:100")!=std::wstring::npos,"counts flushed after the interval");
    t.record(L"DENY",L"c:\\x",L"",now+=10,sink); t.record(L"DENY",L"c:\\x",L"",now+=10,sink);
    check(lines.size()==3,"no second flush inside the interval");
    t.flush(now,sink,true);
    check(lines.size()==4 && lines[3].find(L"1:2")!=std::wstring::npos,"shutdown flush writes the pending counts");
    t.flush(now,sink,true); check(lines.size()==4,"nothing pending, nothing written");
    // Overflow of distinct keys is counted, not stored.
    Throttle small(1000,2); lines.clear();
    small.record(L"DENY",L"a",L"",0,sink);small.record(L"DENY",L"b",L"",1,sink);small.record(L"DENY",L"c",L"",2,sink);small.record(L"DENY",L"d",L"",3,sink);
    small.flush(10,sink,true);
    check(lines.size()==3 && lines[2].find(L"overflow:2")!=std::wstring::npos,"overflow counted");
    check(Throttle::throttled(L"DENY") && Throttle::throttled(L"DEVICE") && !Throttle::throttled(L"MODULE_REFUSE") &&
          !Throttle::throttled(L"MODULE_LOAD_FAILED") && !Throttle::throttled(L"REDIRECT") && Throttle::throttled(L"REDIRECT_READ"),"only DENY, DEVICE and REDIRECT_READ (queries repeat every frame) are throttled");
    // The four-minute live run shape: 34 devices polled 110 times a second, 80 prefix denies, 52k records in all.
    Throttle run(5000); lines.clear(); bytes=0; uint64_t clock=0;
    for(int tick=0;tick<240*110;++tick) {
        clock=(uint64_t)tick*1000/110;
        run.record(L"DEVICE",L"\\\\?\\hid#vid_1532&pid_02a7&mi_0"+std::to_wstring(tick%34)+L"#9&2fcb1b56&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",L"",clock,sink);
    }
    run.flush(clock,sink,true);
    std::cout<<"four-minute run: "<<lines.size()<<" lines, "<<bytes<<" bytes\n";
    check(bytes<300000,"four-minute run well under 1 MB (live run: 7,114,206 bytes)");
    check(lines.size()<34+60,"line count is the distinct paths plus one flush line per interval");
    if(bad) std::cerr<<bad<<" audit failures\n";
    return bad?1:0;
}
