// Host test of the crash recorder's pure rules (crash_record.h): options, recorded codes, dedupe and cap,
// module lookup at the edges, bounded text. Mutants: CRASH_NO_DEDUPE, CRASH_NO_CAP, CRASH_FIND_EDGE.
#include "crash_record.h"
#include <cstdio>
#include <string>

using namespace redirect::crash;
static int failures=0;
static void check(bool ok,const char* what) {if(!ok) {std::printf("crash oracle failed: %s\n",what);++failures;}}
template<size_t N> static std::wstring str(Text<N>& t) {return t.data();}

int main() {
    unsigned level=9;
    check(parse(nullptr,level) && level==0,"absent is off");
    check(parse(L"",level) && level==0,"empty is off");
    check(parse(L"0",level) && level==0,"0 is off");
    check(parse(L"1",level) && level==1,"1 records");
    check(parse(L"2",level) && level==2,"2 also dumps at the first record");
    check(!parse(L"yes",level) && level==0,"other words refuse");
    check(!parse(L"3",level) && !parse(L"01",level) && !parse(L" 1",level),"other numbers refuse");

    uint32_t seconds=0;
    check(parse_hang_seconds(nullptr,seconds) && seconds==DefaultHangSeconds,"hang default");
    check(parse_hang_seconds(L"0",seconds) && seconds==0,"hang 0 is off");
    check(parse_hang_seconds(L"600",seconds) && seconds==600,"hang 600");
    check(!parse_hang_seconds(L"601",seconds) && !parse_hang_seconds(L"1000",seconds),"hang above 600 refuses");
    check(!parse_hang_seconds(L"5s",seconds) && !parse_hang_seconds(L"-5",seconds),"hang digits only");

    for(uint32_t code:{0xC0000005u,0xC000001Du,0xC00000FDu,0xC0000094u,0xC0000374u,0xC0000409u})
        check(fatal_name(code)!=nullptr,"a requested fatal class is recorded");
    for(uint32_t code:{0xE06D7363u,0x406D1388u,0x40010006u,0x80000003u,0x80000001u,0u})
        check(fatal_name(code)==nullptr,"C++ throws, thread names, debug prints, breakpoints, guard pages are not");

    {
        Seen s;
        check(s.claim(0x1000)==Seen::Verdict::fresh,"first address is fresh");
        check(s.claim(0x1000)==Seen::Verdict::repeat,"same address is a repeat (dedupe)");
        for(uint64_t i=1;i<FirstChanceCap;++i) check(s.claim(0x1000+i)==Seen::Verdict::fresh,"distinct addresses up to the cap");
        check(s.count()==FirstChanceCap,"cap count");
        check(s.claim(0x9999)==Seen::Verdict::full,"past the cap is full");
        check(s.claim(0x1000)==Seen::Verdict::repeat,"a known address stays a repeat after the cap");
        check(s.count()==FirstChanceCap,"count never passes the cap");
    }
    {
        ModuleTable t;
        const wchar_t* game=L"ds.exe";
        check(t.add(0x140000000,0x5000000,game,6),"add module");
        check(t.add(0x7ff800000000,0x1000,L"D3DCOMPILER_47.dll",18),"add second module");
        check(!t.add(0,0x1000,game,6) && !t.add(0x1000,0,game,6),"empty modules refused");
        check(t.find(0x140000000) && t.find(0x140000000)->base==0x140000000,"base itself is inside");
        check(t.find(0x144ffffff)!=nullptr,"last byte inside");
        check(t.find(0x145000000)==nullptr,"end is outside");
        check(t.find(0x13fffffff)==nullptr,"before base is outside");
        Text<256> x;
        x.location(t,0x141a19b30);
        check(str(x)==L"ds.exe+0x1a19b30","module+offset");
        x.clear();x.location(t,0x7ff800000fff);
        check(str(x)==L"D3DCOMPILER_47.dll+0xfff","second module");
        x.clear();x.location(t,0x7ff800001000);
        check(str(x)==L"?0x7ff800001000","unknown address marked");
        std::wstring longname(200,L'a');
        check(t.add(0x10000,0x100,longname.c_str(),longname.size()),"long name accepted");
        check(std::wstring(t.find(0x10010)->name).size()==ModuleName-1,"long name cut");
        ModuleTable full;
        for(uint32_t i=0;i<ModuleCap;++i) full.add(0x100000+i*0x1000,0x1000,game,6);
        check(!full.add(0x900000000,0x1000,game,6) && full.size()==ModuleCap,"module table capped");
    }
    {
        Text<64> t;
        t.add("CRASH\t").hex(0xC0000005u,8).put(L' ').hex(0,16).put(L' ').dec(0).put(L' ').dec(18446744073709551615ull);
        check(str(t)==L"CRASH\t0xc0000005 0x0000000000000000 0 18446744073709551615","hex and decimal");
        check(!t.overflow(),"no overflow yet");
        Text<32> d;d.dec(7,2).put(L':').dec(2026,2).put(L':').dec(0,2);
        check(str(d)==L"07:2026:00","decimal padding");
        Text<8> s;s.add("0123456789");
        check(str(s)==L"0123456" && s.overflow() && s.size()==7,"bounded text truncates and says so");
    }
    if(failures) {std::printf("crash oracle: %d failure(s)\n",failures);return 1;}
    std::puts("crash record rules ok");
    return 0;
}
