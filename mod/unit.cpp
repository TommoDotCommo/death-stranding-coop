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
    auto c=mod::parseConfig(good);assert(c.slot==0&&c.peerSlot==1&&c.logging&&c.peerAddress==0x7f000001&&!c.bindAddress);
    // Peers: loopback or any unicast IPv4 (LAN, VPN, public); never names, 0/8, multicast, reserved or broadcast.
    for(auto a:{"127.0.0.1","192.168.1.20","10.0.0.5","100.101.102.103","8.8.4.4","223.255.255.254"})assert(mod::peerAddress(a));
    assert(*mod::peerAddress("192.168.1.20")==0xc0a80114u);
    for(auto a:{"","localhost","0.0.0.0","0.1.2.3","255.255.255.255","224.0.0.1","239.1.1.1","240.0.0.1","1.2.3","1.2.3.4.5","1.2.3.256","01.2.3.4","1..2.3"," 1.2.3.4","1.2.3.4 ","a.b.c.d"})assert(!mod::peerAddress(a));
    auto lan=good;lan.replace(lan.find("127.0.0.1"),9,"100.64.0.7");
    assert(mod::parseConfig(lan).peerAddress==0x64400007u);
    {auto b=lan;b.pop_back();b+=",\"bind\":\"100.64.0.2\"}";assert(mod::parseConfig(b).bindAddress==0x64400002u);}
    {auto b=good;b.pop_back();b+=",\"bind\":\"192.168.1.2\"}";bool threw=false;try{mod::parseConfig(b);}catch(const std::exception&){threw=true;}assert(threw);}   // loopback peer, LAN bind
    {auto b=lan;b.replace(b.find("100.64.0.7"),10,"224.0.0.9");bool threw=false;try{mod::parseConfig(b);}catch(const std::exception&){threw=true;}assert(threw);}
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
    assert(!mod::parseConfig(good).joinAtHost&&mod::parseConfig(good).progressMismatch=="warn");
    assert(mod::parseConfig(withKeys(good,"\"progress_mismatch\":\"refuse\"")).progressMismatch=="refuse");
    assert(refused(withKeys(good,"\"progress_mismatch\":\"ignore\"")));
    assert(refused(withKeys(good,"\"progress_mismatch\":true")));
    assert(refused(withKeys(good,"\"join_at_host\":true")));
    auto noRole=good;noRole.erase(noRole.find("\"role\":\"host\","),14);assert(mod::parseConfig(noRole).role=="none");
    auto guest=good;guest.replace(guest.find("host"),4,"guest");assert(mod::parseConfig(guest).role=="guest");
    auto threaded=withKeys(gameBase,"\"game_thread\":true,"+pin);
    assert(mod::parseConfig(withKeys(threaded,"\"join_at_host\":true")).joinAtHost);
    // join_move: log by default; native only with join_at_host; nothing else accepted.
    assert(mod::parseConfig(withKeys(threaded,"\"join_at_host\":true")).joinMove=="log");
    assert(mod::parseConfig(withKeys(threaded,"\"join_at_host\":true,\"join_move\":\"native\"")).joinMove=="native");
    assert(mod::parseConfig(withKeys(threaded,"\"join_move\":\"log\"")).joinMove=="log");
    assert(refused(withKeys(threaded,"\"join_move\":\"native\"")));
    assert(refused(withKeys(threaded,"\"join_at_host\":true,\"join_move\":\"teleport\"")));
    assert(refused(withKeys(threaded,"\"join_at_host\":true,\"join_move\":true")));
    assert(mod::parseConfig(threaded).gameThread&&!mod::parseConfig(threaded).selfTick);
    assert(refused(withKeys(threaded,"\"self_tick\":true")));
    assert(refused(withKeys(threaded,"\"discovery\":true")));
    assert(refused(withKeys(good,"\"game_thread\":true")));
    assert(refused(withKeys(threaded,"\"puppet_probe\":true")));
    assert(mod::parseConfig(withKeys(threaded,"\"puppet_probe\":true,\"required\":true")).puppetProbe);
    assert(refused(withKeys(game,"\"puppet_probe\":true,\"required\":true")));
    assert(!mod::parseConfig(good).selfTick);
    // Notices in the game's info log: off by default; only where the game thread runs.
    assert(!mod::parseConfig(threaded).notices&&mod::parseConfig(withKeys(threaded,"\"notices\":true")).notices);
    assert(refused(withKeys(good,"\"notices\":true"))&&refused(withKeys(game,"\"notices\":true")));
    // One shared load per order: off by default, and only with shared orders.
    assert(!mod::parseConfig(threaded).shareOrderLoad&&mod::parseConfig(withKeys(threaded,"\"share_orders\":true,\"share_order_load\":true")).shareOrderLoad);
    assert(refused(withKeys(threaded,"\"share_order_load\":true")));
    assert(!mod::parseConfig(threaded).shareRewards&&mod::parseConfig(withKeys(threaded,"\"share_orders\":true,\"share_rewards\":true")).shareRewards);
    assert(refused(withKeys(threaded,"\"share_rewards\":true")));
    // MULE positions: off by default; only with shared enemies and an explicit role (one game has to be the one that says).
    assert(!mod::parseConfig(threaded).shareEnemyPositions&&!mod::parseConfig(threaded).enemyPositionPull);
    assert(mod::parseConfig(withKeys(threaded,"\"share_enemies\":true,\"share_enemy_positions\":true")).shareEnemyPositions);
    assert(mod::parseConfig(withKeys(threaded,"\"share_enemies\":true,\"share_enemy_positions\":true,\"enemy_position_pull\":true")).enemyPositionPull);
    assert(refused(withKeys(threaded,"\"share_enemy_positions\":true")));
    {auto none=threaded;none.erase(none.find("\"role\":\"host\","),14);assert(mod::parseConfig(withKeys(none,"\"share_enemies\":true")).shareEnemies);
        assert(refused(withKeys(none,"\"share_enemies\":true,\"share_enemy_positions\":true")));}
    assert(refused(withKeys(threaded,"\"share_enemies\":true,\"enemy_position_pull\":true")));
    assert(!mod::parseConfig(threaded).enemyPositionGoal);
    assert(mod::parseConfig(withKeys(threaded,"\"share_enemies\":true,\"share_enemy_positions\":true,\"enemy_position_goal\":true")).enemyPositionGoal);
    assert(refused(withKeys(threaded,"\"share_enemies\":true,\"enemy_position_goal\":true")));
    // The delivery-delay impairment works with the real game too (two copies on one PC standing in for an internet
    // link); injected faults stay synthetic-only.
    assert(!mod::parseConfig(threaded).impairment&&mod::parseConfig(withKeys(threaded,"\"impairment\":true")).impairment);
    // The stand-in link takes a known profile only; none by default.
    assert(mod::parseConfig(threaded).link=="none"&&mod::parseConfig(withKeys(threaded,"\"link\":\"internet\"")).link=="internet"&&mod::parseConfig(withKeys(threaded,"\"link\":\"poor\"")).link=="poor");
    assert(refused(withKeys(threaded,"\"link\":\"lan\"")));
    assert(refused(withKeys(threaded,"\"fault\":\"version\"")));assert(mod::parseConfig(withKeys(good,"\"fault\":\"version\"")).fault=="version");
    // Virtual pad: only on a ticking, required mod; off by default; never with discovery.
    assert(!mod::parseConfig(good).virtualPad);
    assert(!mod::parseConfig(good).carryVisual&&!mod::parseConfig(threaded).carryVisual);   // ds3: off by default
    assert(mod::parseConfig(withKeys(threaded,"\"carry_visual\":true")).carryVisual);
    assert(refused(withKeys(good,"\"carry_visual\":true")));                                // needs the game provider
    // Partner presence from the start: off by default; peer/own need puppet_probe and an explicit role.
    auto probe=withKeys(threaded,"\"puppet_probe\":true,\"required\":true");
    assert(mod::parseConfig(probe).partner=="off");
    assert(mod::parseConfig(withKeys(probe,"\"partner\":\"own\"")).partner=="own");
    assert(mod::parseConfig(withKeys(probe,"\"partner\":\"peer\"")).partner=="peer");
    assert(refused(withKeys(probe,"\"partner\":\"mirror\"")));
    assert(refused(withKeys(threaded,"\"partner\":\"own\"")));                             // no puppet_probe
    {auto none=probe;none.erase(none.find("\"role\":\"host\","),14);assert(refused(withKeys(none,"\"partner\":\"own\"")));}
    assert(mod::parseConfig(withKeys(threaded,"\"partner\":\"off\"")).partner=="off");
    assert(!mod::parseConfig(probe).partnerMarker&&mod::parseConfig(withKeys(probe,"\"partner_marker\":true")).partnerMarker);
    assert(refused(withKeys(threaded,"\"partner_marker\":true")));                          // no puppet_probe
    // Other players (others.h): off by default, at most 32, and only where bodies can be made.
    assert(mod::parseConfig(probe).others==0&&mod::parseConfig(withKeys(probe,"\"others\":32")).others==32&&mod::parseConfig(withKeys(probe,"\"others\":0")).others==0);
    assert(refused(withKeys(probe,"\"others\":33")));assert(refused(withKeys(probe,"\"others\":true")));assert(refused(withKeys(probe,"\"others\":\"4\"")));
    assert(refused(withKeys(threaded,"\"others\":4")));                                    // no puppet_probe
    assert(mod::parseConfig(withKeys(threaded,"\"others\":0")).others==0);
    // The relay for other players: numeric address on loopback (or the partner's), a key, an id, and others on.
    {auto o=withKeys(probe,"\"others\":4");auto r=[&](const char* more){return withKeys(o,more);};
     auto c=mod::parseConfig(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"group-1\",\"player_id\":77"));
     assert(c.relayAddress==0x7f000001&&c.relayPort==8090&&c.relayKey=="group-1"&&c.playerId==77);
     assert(refused(r("\"relay\":\"10.1.2.3:8090\",\"relay_key\":\"k\",\"player_id\":77")));      // neither loopback nor the partner
     assert(refused(r("\"relay\":\"localhost:8090\",\"relay_key\":\"k\",\"player_id\":77")));
     assert(refused(r("\"relay\":\"127.0.0.1:0\",\"relay_key\":\"k\",\"player_id\":77")));
     assert(refused(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\"")));                         // no id
     assert(refused(r("\"relay\":\"127.0.0.1:8090\",\"player_id\":77")));                            // no key
     assert(refused(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"a b\",\"player_id\":77")));
     assert(refused(r("\"relay_key\":\"k\"")));
     assert(refused(withKeys(probe,"\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":77")));}   // others off
    // Group play: a config without `peer` has no partner; the players' own server is listed as `server`, and only a
    // listed address (or loopback, or the partner's) may be the relay or the online server.
    {std::string synthetic=R"({"server":"10.1.2.3","slot":0,"nickname":"test","provider":"synthetic","session":123})";
     auto c=mod::parseConfig(synthetic);assert(c.group&&c.peer.empty()&&c.role=="none"&&c.server=="10.1.2.3"&&c.serverAddress==0x0a010203u&&c.slot==0&&c.peerSlot==1);
     {auto five=synthetic;five.replace(five.find("\"slot\":0"),8,"\"slot\":5");assert(mod::parseConfig(five).slot==5&&mod::parseConfig(five).peerSlot==0);}
     assert(!mod::parseConfig(good).group&&!mod::parseConfig(good).serverAddress);
     assert(refused(R"({"slot":0,"nickname":"test","provider":"synthetic","session":123})"));                  // neither a partner nor a server
     for(auto bad:{"\"localhost\"","\"224.0.0.1\"","\"0.0.0.0\"","\"10.1.2.3:80\"","true","\"\""}){auto b=synthetic;b.replace(b.find("\"10.1.2.3\""),10,bad);assert(refused(b));}
     for(auto more:{"\"role\":\"host\"","\"role\":\"guest\"","\"peer_slot\":1","\"bind\":\"10.1.2.9\"","\"share_orders\":true","\"share_roads\":true","\"share_enemies\":true","\"share_saved_structures\":true"})assert(refused(withKeys(synthetic,more)));
     assert(mod::parseConfig(withKeys(synthetic,"\"role\":\"none\",\"share_orders\":false,\"share_environment\":\"off\",\"partner\":\"off\"")).group);
     std::string game=R"({"server":"10.1.2.3","slot":0,"nickname":"test","provider":"game","session":123,"game_thread":true,"puppet_probe":true,"required":true,)"+pin+"}";
     assert(mod::parseConfig(game).group&&mod::parseConfig(game).puppetProbe);
     for(auto more:{"\"partner\":\"own\"","\"partner\":\"peer\"","\"join_at_host\":true","\"share_environment\":\"rain\"","\"share_orders\":true,\"share_rewards\":true"})assert(refused(withKeys(game,more)));
     auto full=mod::parseConfig(withKeys(game,"\"partner_tag\":true,\"others\":15,\"relay\":\"10.1.2.3:49100\",\"relay_key\":\"w77\",\"player_id\":999999999999999,\"online_server\":\"http://10.1.2.3:49100/w77/auth/ds\""));
     assert(full.group&&full.others==15&&full.relayAddress==0x0a010203u&&full.relayPort==49100&&full.playerId==999999999999999ull&&full.onlineServer=="http://10.1.2.3:49100/w77/auth/ds"&&full.partnerTag);
     assert(refused(withKeys(game,"\"others\":15,\"relay\":\"10.1.2.3:49100\",\"relay_key\":\"w77\",\"player_id\":1000000000000000")));   // an id is at most 15 digits
     auto o=withKeys(game,"\"others\":4");
     assert(refused(withKeys(o,"\"relay\":\"10.9.9.9:49100\",\"relay_key\":\"k\",\"player_id\":7")));           // not the listed server
     assert(mod::parseConfig(withKeys(o,"\"relay\":\"127.0.0.1:49100\",\"relay_key\":\"k\",\"player_id\":7")).relayAddress==0x7f000001u);   // the server's own PC
     assert(refused(withKeys(game,"\"online_server\":\"http://10.9.9.9:49100/w77/auth/ds\"")));
     assert(mod::parseConfig(withKeys(game,"\"online_server\":\"http://127.0.0.1:49100/w77/auth/ds\"")).group);
     assert(mod::onlineAddress("http://10.1.2.3:80/a",0x7f000001,0x0a010203)&&!mod::onlineAddress("http://10.1.2.4:80/a",0x7f000001,0x0a010203)&&!mod::onlineAddress("http://10.1.2.3:80/a",0x7f000001));
     // A partner and a server on a third PC: both addresses are listed, nothing else.
     auto both=withKeys(probe,"\"others\":4,\"server\":\"10.1.2.3\"");
     assert(!mod::parseConfig(both).group&&mod::parseConfig(withKeys(both,"\"relay\":\"10.1.2.3:8090\",\"relay_key\":\"k\",\"player_id\":7")).relayAddress==0x0a010203u);
     assert(refused(withKeys(both,"\"relay\":\"10.9.9.9:8090\",\"relay_key\":\"k\",\"player_id\":7")));}
    assert(mod::parseConfig(withKeys(threaded,"\"virtual_pad\":true,\"required\":true")).virtualPad);
    assert(mod::parseConfig(withKeys(good,"\"virtual_pad\":true,\"required\":true,\"self_tick\":true")).virtualPad);
    assert(refused(withKeys(threaded,"\"virtual_pad\":true")));
    assert(refused(withKeys(good,"\"virtual_pad\":true,\"required\":true")));
    assert(refused(withKeys(discovery,"\"virtual_pad\":true,\"required\":true")));
    assert(refused(withKeys(threaded,"\"virtual_pad\":1,\"required\":true")));
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
