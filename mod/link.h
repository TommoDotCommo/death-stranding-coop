#pragma once
// A stand-in for the link between two PCs, for lab runs with both copies on one machine. Every packet the mod
// receives gets a fate: lost, or delivered after a delay. Fates depend only on the count of received packets, so a
// run can be repeated. `impairment` (the older key) delays presence only by 15..65 ms and loses nothing; a `link`
// profile applies to every kind of packet, so the ordered streams (cargo, structures, notices) are exercised too.
#include <cstdint>
#include <string>
namespace mod::link {
struct Profile {unsigned lossPerMille=0,delayMinMs=0,delaySpanMs=0;};   // delay = min .. min+span-1
struct Fate {bool lost=false;uint64_t delayMs=0;};
// "internet": a good home connection to a friend abroad; "poor": Wi-Fi at its worst or a mobile link.
inline bool profile(const std::string& name,Profile& out) {
    if(name=="none") {out={};return true;}
    if(name=="internet") {out={20,40,41};return true;}
    if(name=="poor") {out={120,100,201};return true;}
    return false;
}
inline uint32_t mix(uint64_t n) {n^=n>>33;n*=0xff51afd7ed558ccdULL;n^=n>>33;n*=0xc4ceb9fe1a85ec53ULL;n^=n>>33;return uint32_t(n);}
inline Fate fate(const Profile& p,bool impairment,uint64_t received,bool presence) {
    Fate f;
    if(p.delaySpanMs||p.lossPerMille) {
#ifndef MUTANT_LINK_NO_LOSS
        f.lost=mix(received*2+1)%1000<p.lossPerMille;
#endif
#ifdef MUTANT_LINK_PRESENCE_ONLY
        if(!presence) return {};
#endif
#ifdef MUTANT_LINK_FLAT
        f.delayMs=p.delayMinMs;
#else
        f.delayMs=p.delayMinMs+(p.delaySpanMs?mix(received*2)%p.delaySpanMs:0);
#endif
        return f;
    }
    if(impairment&&presence) f.delayMs=15+(received*17)%51;
    return f;
}
}
