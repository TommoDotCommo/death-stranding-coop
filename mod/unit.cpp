#include "config.h"
#include "providers.h"
#include "signature.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using mod::uniqueSignature;
    assert(uniqueSignature({0,0x48,0x89,1,0xc3},{0x48,0x89,-1,0xc3})==1);
    assert(!uniqueSignature({1,2,1,2},{1,2}));
    assert(uniqueSignature({1,2},{1,2})==0);
    assert(!uniqueSignature({1},{1,2}));assert(!uniqueSignature({}, {1}));
    assert(!uniqueSignature({1,2,3},{2,4}));
    for(auto p:std::vector<std::vector<int>>{{},{-1,-1},{256},{-2}}) {bool threw=false;try{uniqueSignature({},p);}catch(const std::invalid_argument&){threw=true;}assert(threw);}
    std::string good=R"({"role":"host","peer":"127.0.0.1","slot":0,"peer_slot":1,"nickname":"test","provider":"synthetic","session":123})";
    auto c=mod::parseConfig(good);assert(c.slot==0&&c.peerSlot==1&&c.logging);
    for(auto s:{"{}", "[]", "{\"slot\":01}", "{\"slot\":0,\"slot\":1}", "{\"role\":true}", "{\"x\":0}"}) {bool threw=false;try{mod::parseConfig(s);}catch(const std::exception&){threw=true;}assert(threw);}
    for(auto replacement: {std::pair<std::string,std::string>{"\"peer_slot\":1","\"peer_slot\":16"},{"\"peer_slot\":1","\"peer_slot\":0"},{"\"session\":123","\"session\":0"},{"\"synthetic\"","\"unknown\""}}) {auto s=good;s.replace(s.find(replacement.first),replacement.first.size(),replacement.second);bool threw=false;try{mod::parseConfig(s);}catch(const std::exception&){threw=true;}assert(threw);}
    auto discovery=good;discovery.replace(discovery.find("synthetic"),9,"game");discovery.pop_back();
    discovery+=",\"discovery\":true,\"discovery_sha256\":\""+std::string(mod::discovery::PinnedHash)+"\"}";
    assert(mod::parseConfig(discovery).discovery);
    assert(mod::parseConfig(discovery).discoveryOptions.hz==10);
    for(auto option:{"\"discovery_hz\":0","\"discovery_hz\":21","\"discovery_max_mib\":0","\"discovery_max_mib\":65","\"discovery_animation_slots\":33"}) {
        auto bad=discovery;bad.pop_back();bad+=std::string(",")+option+"}";bool threw=false;try{mod::parseConfig(bad);}catch(const std::exception&){threw=true;}assert(threw);
    }
    // Pose exchange keys: provider game needs self_tick and the executable pin; presence_log_max_mib is 1..32.
    auto refused=[&](const std::string& config){bool threw=false;try{mod::parseConfig(config);}catch(const std::exception&){threw=true;}return threw;};
    auto withKeys=[&](std::string base,const std::string& extra){base.pop_back();return base+","+extra+"}";};
    std::string pin="\"discovery_sha256\":\""+std::string(mod::discovery::PinnedHash)+"\"";
    auto gameBase=good;gameBase.replace(gameBase.find("synthetic"),9,"game");
    auto game=withKeys(gameBase,"\"self_tick\":true,"+pin);
    auto parsed=mod::parseConfig(game);assert(parsed.selfTick&&!parsed.discovery&&parsed.presenceLogMaxMiB==8&&parsed.provider=="game");
    assert(!mod::parseConfig(good).selfTick);
    assert(refused(gameBase));                                                   // game without self_tick or pin
    assert(refused(withKeys(gameBase,pin)));                                     // game without self_tick
    assert(refused(withKeys(gameBase,"\"self_tick\":false,"+pin)));
    assert(refused(withKeys(gameBase,"\"self_tick\":true")));                    // no executable pin
    assert(refused(withKeys(gameBase,"\"self_tick\":true,\"discovery_sha256\":\"00\"")));
    assert(mod::parseConfig(withKeys(good,"\"self_tick\":true")).selfTick);       // synthetic may self-tick (probe tests)
    for(auto bad:{"\"self_tick\":\"true\"","\"self_tick\":1","\"self_tick\":yes","\"self_tick\":true,\"self_tick\":true","\"self_tick\":TRUE"}) assert(refused(withKeys(game.substr(0,game.find(",\"self_tick\""))+"}",bad+std::string(",")+pin)));
    for(auto value:{"1","8","32"}) assert(mod::parseConfig(withKeys(game,std::string("\"presence_log_max_mib\":")+value)).presenceLogMaxMiB==unsigned(std::stoi(value)));
    for(auto bad:{"0","33","100","01","-1","8.5","\"8\"","true","1000000000000000"}) assert(refused(withKeys(game,std::string("\"presence_log_max_mib\":")+bad)));
    assert(refused(withKeys(game,"\"presence_log_max_mib\":8,\"presence_log_max_mib\":8")));
    assert(refused(withKeys(good,"\"presence_log_max_mib\":8")));                // presence log is a game-provider feature
    assert(refused(withKeys(game,"\"discovery\":true")));                        // discovery is exclusive with self_tick
    assert(refused(withKeys(discovery,"\"self_tick\":true")));
    assert(refused(withKeys(discovery,"\"presence_log_max_mib\":8")));
    assert(!mod::parseConfig(discovery).selfTick);
    assert(refused(withKeys(game,"\"nickname2\":1")));                           // unknown key
    assert(refused(withKeys(good,"\"pad\":\""+std::string(4100,'a')+"\"")));      // 4096-byte cap
    {   auto big=withKeys(game,"\"pad\":\"");big.pop_back();big+=std::string(4096-big.size()-2,'a')+"\"}";   // exactly at the cap still parses structurally
        assert(big.size()==4096);assert(refused(big));                           // ...but "pad" is an unknown key
    }
    // Synthetic provider behaviour is unchanged, including its explicit stub-free trajectory.
    auto a=mod::trajectory(1000,0),b=mod::trajectory(1001,0);assert(ds::sanitise(a));assert(std::abs((b.position[0]-a.position[0])*1000-a.velocity[0])<.02);
    assert(ds::portForSlot(0)==49000&&ds::portForSlot(1)==49001);
    std::cout<<"signature/config/provider tests passed\n";
}
