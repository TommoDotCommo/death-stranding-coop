#include "config.h"
#include <map>
#include <stdexcept>
#include <cctype>
#include <set>
namespace mod {
Config parseConfig(const std::string& s) {
    if(s.size()>4096) throw std::runtime_error("config exceeds 4096 bytes");
    size_t at=0;
    auto ws=[&]{while(at<s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;};
    auto take=[&](char c){ws(); if(at==s.size() || s[at++]!=c) throw std::runtime_error("malformed config JSON");};
    auto str=[&](){take('"');std::string v;while(at<s.size() && s[at]!='"') {char c=s[at++];if(c<' ' || c=='\\' || static_cast<unsigned char>(c)>126) throw std::runtime_error("config strings require unescaped printable ASCII");v+=c;}take('"');return v;};
    std::map<std::string,std::string> values; std::set<std::string> strings;
    take('{');ws();
    if(at<s.size() && s[at]!='}') for(;;) {
        auto k=str();take(':');ws();std::string v;
        if(at<s.size() && s[at]=='"') {v=str();strings.insert(k);} else {size_t b=at;while(at<s.size() && (std::isalnum(static_cast<unsigned char>(s[at])))) ++at;v=s.substr(b,at-b);}
        if(!values.emplace(k,v).second) throw std::runtime_error("duplicate config key");
        ws();if(at==s.size() || s[at]!=',') break;++at;
    }
    take('}');ws();if(at!=s.size()) throw std::runtime_error("trailing JSON");
    for(auto& kv:values) if(!std::set<std::string>{"slot","share_environment","join_move","carry_visual","partner","partner_gear","partner_marker","partner_vehicle","partner_likes","peer_slot","role","peer","nickname","provider","session","logging","required","impairment","fault","discovery","discovery_sha256","discovery_hz","discovery_max_mib","discovery_animation_slots","discovery_player_rva","discovery_worldnode_rva","discovery_controller_rva","discovery_animation_rva","discovery_vehicle_rva","discovery_bb_rva","discovery_velocity_rva","discovery_angular_velocity_rva","self_tick","game_thread","puppet_probe","virtual_pad","auto_continue","presence_log_max_mib","bind","progress_mismatch","join_at_host"}.count(kv.first)) throw std::runtime_error("unknown config key: "+kv.first);
    auto text=[&](const char* k){if(!strings.count(k)) throw std::runtime_error(std::string("missing/string expected: ")+k);return values.at(k);};
    auto number=[&](const char* k){auto v=values[k];if(strings.count(k)||v.empty()||v.size()>15||v.find_first_not_of("0123456789")!=std::string::npos||(v.size()>1&&v[0]=='0')) throw std::runtime_error(std::string("invalid integer: ")+k);return std::stoull(v);};
    auto boolean=[&](const char* k,bool d){if(!values.count(k)) return d;auto v=values[k];if(strings.count(k)||(v!="true"&&v!="false")) throw std::runtime_error("boolean expected");return v=="true";};
    Config c; auto slot=number("slot"),peer=number("peer_slot");if(slot>=16||peer>=16||slot==peer) throw std::runtime_error("distinct slots 0..15 required");c.slot=slot;c.peerSlot=peer;
    c.session=number("session");if(!c.session) throw std::runtime_error("nonzero session required");
    if(values.count("role"))c.role=text("role");
    c.peer=text("peer");c.nickname=text("nickname");c.provider=text("provider");
    {auto a=peerAddress(c.peer);if(!a)throw std::runtime_error("peer must be a numeric unicast IPv4 address");c.peerAddress=*a;}
    if(values.count("bind")){
        c.bind=text("bind");auto b=peerAddress(c.bind);
        if(!b)throw std::runtime_error("bind must be a numeric unicast IPv4 address");
        if((c.peerAddress>>24)==127&&(*b>>24)!=127)throw std::runtime_error("a loopback peer needs a loopback bind");
        c.bindAddress=*b;
    }
    if((c.role!="none"&&c.role!="host"&&c.role!="guest"&&c.role!="client")||(c.provider!="synthetic"&&c.provider!="game")||c.nickname.empty()||c.nickname.size()>32) throw std::runtime_error("invalid role/provider/nickname");
    c.logging=boolean("logging",true);c.required=boolean("required",false);c.impairment=boolean("impairment",false);
    if(values.count("fault")) c.fault=text("fault");
    if(c.fault!="none"&&c.fault!="build"&&c.fault!="version"&&c.fault!="session") throw std::runtime_error("invalid probe fault");
    if(c.provider!="synthetic" && (c.impairment || c.fault!="none")) throw std::runtime_error("probe options require synthetic provider");
    c.discovery=boolean("discovery",false);
    if(c.discovery) {
        if(c.provider!="game") throw std::runtime_error("discovery requires game provider");
        c.discoveryOptions.sha256=text("discovery_sha256");
        if(c.discoveryOptions.sha256!=discovery::PinnedHash) throw std::runtime_error("discovery pinned SHA-256 required");
    }
    c.selfTick=boolean("self_tick",false);
    c.gameThread=boolean("game_thread",false);c.puppetProbe=boolean("puppet_probe",false);
    if(c.gameThread&&(c.provider!="game"||c.discovery||c.selfTick))throw std::runtime_error("game_thread requires game provider, no discovery or self_tick");
    if(c.puppetProbe&&(!c.gameThread||!c.required))throw std::runtime_error("puppet_probe requires game_thread and required true");
    if(values.count("share_environment"))c.shareEnvironment=text("share_environment");
    if(c.shareEnvironment!="off"&&c.shareEnvironment!="host"&&c.shareEnvironment!="rain")throw std::runtime_error("share_environment must be off, host or rain");
    if(c.shareEnvironment!="off"&&(c.provider!="game"||!c.gameThread||!c.puppetProbe||!c.required||c.role=="none"))throw std::runtime_error("share_environment host/rain requires game_thread, puppet_probe, required and explicit role");
    c.joinAtHost=boolean("join_at_host",false);
    if(values.count("progress_mismatch"))c.progressMismatch=text("progress_mismatch");
    if(c.progressMismatch!="warn"&&c.progressMismatch!="refuse")throw std::runtime_error("progress_mismatch must be warn or refuse");
    if(c.joinAtHost&&!c.gameThread)throw std::runtime_error("join_at_host requires game_thread");
    if(values.count("join_move"))c.joinMove=text("join_move");
    if(c.joinMove!="log"&&c.joinMove!="native")throw std::runtime_error("join_move must be log or native");
    if(c.joinMove=="native"&&!c.joinAtHost)throw std::runtime_error("join_move native requires join_at_host");
    c.virtualPad=boolean("virtual_pad",false);
    c.autoContinue=boolean("auto_continue",false);
    if(c.autoContinue&&!c.gameThread)throw std::runtime_error("auto_continue requires game_thread");
    if(values.count("partner"))c.partner=text("partner");
    if(c.partner!="off"&&c.partner!="peer"&&c.partner!="own")throw std::runtime_error("partner must be off, peer or own");
#ifndef MUTANT_CONFIG_PARTNER
    if(c.partner!="off"&&(!c.puppetProbe||c.role=="none"))throw std::runtime_error("partner peer/own requires puppet_probe and an explicit role");
#endif
    c.partnerMarker=boolean("partner_marker",false);
    c.partnerVehicle=boolean("partner_vehicle",false);
    c.partnerLikes=boolean("partner_likes",false);
#ifndef MUTANT_CONFIG_MARKER
    if(c.partnerMarker&&!c.puppetProbe)throw std::runtime_error("partner_marker requires puppet_probe");
    if(c.partnerVehicle&&!c.puppetProbe)throw std::runtime_error("partner_vehicle requires puppet_probe");
    if(c.partnerLikes&&!c.puppetProbe)throw std::runtime_error("partner_likes requires puppet_probe");
#endif
    c.partnerGear=boolean("partner_gear",false);
    if(c.partnerGear&&c.provider!="game")throw std::runtime_error("partner_gear requires game provider");
    c.carryVisual=boolean("carry_visual",false);
    if(c.carryVisual&&c.provider!="game")throw std::runtime_error("carry_visual requires game provider");
    if(c.virtualPad&&(c.discovery||!c.required||(!c.selfTick&&!c.gameThread)))throw std::runtime_error("virtual_pad requires a ticking mod (self_tick or game_thread), no discovery and required true");
    if(c.discovery && (c.selfTick||values.count("presence_log_max_mib"))) throw std::runtime_error("discovery is exclusive with self_tick and presence");
    if(c.provider=="game" && !c.discovery) {
        // Reads happen off the game thread (no hook exists), so the mod must tick itself; the executable pin is mandatory.
        if(!c.selfTick&&!c.gameThread) throw std::runtime_error("provider game requires self_tick true or game_thread true");
        c.discoveryOptions.sha256=text("discovery_sha256");
        if(c.discoveryOptions.sha256!=discovery::PinnedHash) throw std::runtime_error("pinned executable SHA-256 required (discovery_sha256)");
    }
    if(values.count("presence_log_max_mib")) {
        if(c.provider!="game") throw std::runtime_error("presence log requires game provider");
        auto v=number("presence_log_max_mib");if(v<1||v>32) throw std::runtime_error("presence_log_max_mib must be 1..32");c.presenceLogMaxMiB=unsigned(v);
    }
    auto bounded=[&](const char* key,unsigned fallback,unsigned lo,unsigned hi){if(!values.count(key))return fallback;auto v=number(key);if(v<lo||v>hi)throw std::runtime_error("discovery budget bounds");return unsigned(v);};
    c.discoveryOptions.hz=bounded("discovery_hz",10,1,20);
    c.discoveryOptions.maxMiB=bounded("discovery_max_mib",32,1,64);
    c.discoveryOptions.animationSlots=bounded("discovery_animation_slots",0,0,32);
    for(auto name:{"player","worldnode","controller","animation","vehicle","bb","velocity","angular_velocity"}) {
        auto key=std::string("discovery_")+name+"_rva";
        if(values.count(key))c.discoveryOptions.roots[name]=number(key.c_str());
    }
    return c;
}
std::optional<uint32_t> peerAddress(const std::string& t){
    if(t.empty()||t.size()>15)return std::nullopt;
    uint32_t a=0;unsigned parts=0;size_t at=0;
    while(at<=t.size()){
        size_t end=t.find('.',at);if(end==std::string::npos)end=t.size();
        auto p=t.substr(at,end-at);
        if(p.empty()||p.size()>3||p.find_first_not_of("0123456789")!=std::string::npos||(p.size()>1&&p[0]=='0'))return std::nullopt;
        auto v=std::stoul(p);if(v>255)return std::nullopt;
        a=a<<8|uint32_t(v);++parts;at=end+1;if(end==t.size())break;
    }
    if(parts!=4)return std::nullopt;
    uint8_t first=uint8_t(a>>24);
#ifndef MUTANT_PEER_ANY
    if(first==0||first>=224)return std::nullopt;   // 0/8, multicast 224/4, reserved 240/4 and broadcast
#endif
    return a;
}
}
