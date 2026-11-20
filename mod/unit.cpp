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
    // Bodies: guarded and not joined to this porter's update group by default; joined again without the guard.
    assert(c.bodyRepGuard&&!c.bodyUpdateLink);
    {auto b=good;b.pop_back();b+=",\"body_rep_guard\":false}";auto u=mod::parseConfig(b);assert(!u.bodyRepGuard&&u.bodyUpdateLink);}
    {auto b=good;b.pop_back();b+=",\"body_update_link\":true}";auto u=mod::parseConfig(b);assert(u.bodyRepGuard&&u.bodyUpdateLink);}
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
    // What other players wear and carry: off by default; gear needs other players and the game provider; carried cases
    // need the gear (they hang on the backpack's copy) and are for at most 8 players at once.
    {std::string porter=R"({"server":"10.1.2.3","slot":0,"nickname":"test","provider":"game","session":123,"game_thread":true,"puppet_probe":true,"required":true,)"+pin+"}";
     auto o=withKeys(porter,"\"others\":4");
     assert(!mod::parseConfig(o).othersGear&&mod::parseConfig(o).othersCarry==0);
     auto g=mod::parseConfig(withKeys(o,"\"others_gear\":true,\"others_carry\":4"));assert(g.othersGear&&g.othersCarry==4);
     assert(mod::parseConfig(withKeys(o,"\"others_gear\":true")).othersCarry==0&&mod::parseConfig(withKeys(o,"\"others_gear\":true,\"others_carry\":8")).othersCarry==8);
     assert(refused(withKeys(o,"\"others_gear\":true,\"others_carry\":9")));assert(refused(withKeys(o,"\"others_carry\":4")));
     assert(refused(withKeys(o,"\"others_gear\":true,\"others_carry\":true")));assert(refused(withKeys(o,"\"others_gear\":1")));
     assert(refused(withKeys(porter,"\"others_gear\":true")));                            // no other players
     assert(mod::parseConfig(withKeys(o,"\"others_gear\":false,\"others_carry\":0")).othersCarry==0);
     // Pings and likes (grouppings.h): off by default; group play over the broadcast relay with others shown.
     auto relayed=withKeys(o,"\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\"");
     assert(!mod::parseConfig(relayed).groupPings&&mod::parseConfig(withKeys(relayed,"\"group_pings\":true")).groupPings);
     assert(refused(withKeys(o,"\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"group_pings\":true")));   // pairs
     assert(refused(withKeys(o,"\"group_pings\":true"))&&refused(withKeys(relayed,"\"group_pings\":1")));                          // no relay; not a bool
     assert(refused(withKeys(porter,"\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"group_pings\":true")));}   // no others
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
     // How other players travel: pairs by default, or broadcast; nothing else, and only with a relay.
     assert(c.relayMode=="pairs"&&mod::parseConfig(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"pairs\"")).relayMode=="pairs");
     assert(mod::parseConfig(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\"")).relayMode=="broadcast");
     for(auto bad:{"\"fanout\"","\"Broadcast\"","\"\"","true","2"})assert(refused(r((std::string("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":")+bad).c_str())));
     assert(refused(r("\"relay_mode\":\"broadcast\"")));                                         // no relay
     // Who he sees: everyone by default, friends or nobody, only in broadcast and only with a relay.
     assert(c.showOthers=="everyone");
     for(auto show:{"everyone","friends","nobody"})assert(mod::parseConfig(r((std::string("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"show_others\":\"")+show+"\"").c_str())).showOthers==show);
     for(auto bad:{"\"all\"","\"Friends\"","\"\"","1"})assert(refused(r((std::string("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"show_others\":")+bad).c_str())));
     assert(refused(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"show_others\":\"friends\"")));   // pairs
     assert(refused(r("\"show_others\":\"nobody\"")));                                         // no relay
     // Fights with visitors: off by default; only in broadcast, with a relay, the game provider and the game thread.
     assert(!c.hostFights&&!c.joinFights);
     {auto b=r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"host_fights\":true,\"join_fights\":true");
      auto f=mod::parseConfig(b);assert(f.hostFights&&f.joinFights);
      assert(!mod::parseConfig(r("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"join_fights\":true")).hostFights);}
     for(auto key:{"host_fights","join_fights"}){
         assert(refused(r((std::string("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"")+key+"\":true").c_str())));   // pairs
         assert(refused(r((std::string("\"")+key+"\":true").c_str())));                                                                        // no relay
         assert(refused(r((std::string("\"relay\":\"127.0.0.1:8090\",\"relay_key\":\"k\",\"player_id\":7,\"relay_mode\":\"broadcast\",\"")+key+"\":1").c_str())));}
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
     auto full=mod::parseConfig(withKeys(game,"\"partner_tag\":true,\"help_page\":true,\"server_info\":true,\"others\":15,\"relay\":\"10.1.2.3:49100\",\"relay_key\":\"w77\",\"player_id\":999999999999999,\"online_server\":\"http://10.1.2.3:49100/w77/auth/ds\""));
     assert(full.group&&full.others==15&&full.relayAddress==0x0a010203u&&full.relayPort==49100&&full.playerId==999999999999999ull&&full.onlineServer=="http://10.1.2.3:49100/w77/auth/ds"&&full.partnerTag&&full.helpPage&&full.serverInfo);
     assert(refused(withKeys(game,"\"others\":15,\"relay\":\"10.1.2.3:49100\",\"relay_key\":\"w77\",\"player_id\":1000000000000000")));   // an id is at most 15 digits
     auto o=withKeys(game,"\"others\":4");
     assert(refused(withKeys(o,"\"relay\":\"10.9.9.9:49100\",\"relay_key\":\"k\",\"player_id\":7")));           // not the listed server
     assert(mod::parseConfig(withKeys(o,"\"relay\":\"127.0.0.1:49100\",\"relay_key\":\"k\",\"player_id\":7")).relayAddress==0x7f000001u);   // the server's own PC
     assert(refused(withKeys(game,"\"online_server\":\"http://10.9.9.9:49100/w77/auth/ds\"")));
     assert(mod::parseConfig(withKeys(game,"\"online_server\":\"http://127.0.0.1:49100/w77/auth/ds\"")).group);
     // What the game is told (strand_notices.h): with online_server and the notice token the launcher took; nothing alone.
     {auto online=withKeys(game,"\"online_server\":\"http://10.1.2.3:49100/w77/t/T1/auth/ds\"");
      assert(mod::parseConfig(withKeys(game,"\"free_build\":true")).freeBuild);
      {auto seen=mod::parseConfig(withKeys(online,"\"player_map\":true,\"strand_token\":\"abcDEF123\""));assert(seen.playerMap&&!seen.chat);
       assert(refused(withKeys(online,"\"player_map\":true"))&&refused(withKeys(game,"\"player_map\":true,\"strand_token\":\"abc\"")));}
      {auto kept=mod::parseConfig(withKeys(online,"\"server_world\":\"a1b2c3d4\",\"strand_token\":\"abcDEF123\""));assert(kept.serverWorld=="a1b2c3d4");
       assert(refused(withKeys(online,"\"server_world\":\"a1b2c3d4\""))&&refused(withKeys(game,"\"server_world\":\"a1b2c3d4\",\"strand_token\":\"abc\""))&&refused(withKeys(online,"\"server_world\":\"a1\",\"strand_token\":\"abc\""))&&refused(withKeys(online,"\"server_world\":\"a b c d\",\"strand_token\":\"abc\"")));}
      {auto said=mod::parseConfig(withKeys(online,"\"chat\":true,\"strand_token\":\"abcDEF123\""));assert(said.chat&&!said.strandNotices);
       assert(refused(withKeys(online,"\"chat\":true"))&&refused(withKeys(game,"\"chat\":true,\"strand_token\":\"abc\"")));}
      auto told=mod::parseConfig(withKeys(online,"\"strand_notices\":true,\"strand_token\":\"abcDEF123\""));
      assert(told.strandNotices&&told.strandToken=="abcDEF123"&&!mod::parseConfig(online).strandNotices&&!told.notices);
      assert(refused(withKeys(game,"\"strand_notices\":true,\"strand_token\":\"abc\"")));                  // no online server
      for(auto bad:{"\"strand_notices\":true","\"strand_notices\":true,\"strand_token\":\"\"","\"strand_notices\":true,\"strand_token\":\"a-b\"",
                    "\"strand_notices\":true,\"strand_token\":\"abcdefghijklmnopqrstuvwxyz0123456\"","\"strand_token\":\"abc\"","\"strand_notices\":1,\"strand_token\":\"abc\""})
          assert(refused(withKeys(online,bad)));
      assert(refused(withKeys(withKeys(game,"\"online_server\":\"http://10.1.2.3:49100/w77/gate\""),"\"strand_notices\":true,\"strand_token\":\"abc\"")));
      assert(mod::serverApiBase("http://10.1.2.3:49100/w77/auth/ds")==std::string("/w77")&&mod::serverApiBase("http://10.1.2.3:49100/w77/k/ann/auth/ds")==std::string("/w77"));
      assert(mod::serverApiBase("http://10.1.2.3:49100/w77/t/T1/auth/ds")==std::string("/w77")&&mod::serverApiBase("http://1.2.3.4:80/auth/ds")==std::string()&&mod::serverApiBase("http://1.2.3.4:80/k/ann/auth/ds")==std::string());
      for(auto bad:{"http://1.2.3.4:80/a/b/auth/ds","http://1.2.3.4:80/w77/x/y/auth/ds","http://1.2.3.4:80/w77//auth/ds","http://1.2.3.4:80/w77/auth/dsx","http://1.2.3.4:80","ftp://1.2.3.4:80/auth/ds"})assert(!mod::serverApiBase(bad));}
     // Received structures built (received.h): off by default, only with the server the game signs in to.
     {auto online=withKeys(game,"\"online_server\":\"http://10.1.2.3:49100/w77/auth/ds\"");
      assert(!mod::parseConfig(online).buildReceived&&mod::parseConfig(withKeys(online,"\"build_received\":true")).buildReceived);
      assert(!mod::parseConfig(withKeys(online,"\"build_received\":false")).buildReceived);
      assert(refused(withKeys(game,"\"build_received\":true")));                                       // no online server
      assert(!mod::parseConfig(withKeys(game,"\"build_received\":false")).buildReceived);
      // Cargo through the server (groupcargo.h): group play with the notice token only.
      auto token=withKeys(online,"\"strand_notices\":true,\"strand_token\":\"abcDEF123\"");
      assert(!mod::parseConfig(token).groupCargo&&mod::parseConfig(withKeys(token,"\"group_cargo\":true")).groupCargo);
      assert(refused(withKeys(online,"\"group_cargo\":true"))&&refused(withKeys(game,"\"group_cargo\":true")));
      assert(mod::parseConfig(withKeys(token,"\"group_enemies\":true")).groupEnemies&&refused(withKeys(online,"\"group_enemies\":true")));
      assert(mod::parseConfig(withKeys(token,"\"group_enemies\":true,\"group_replays\":true")).groupReplays&&!mod::parseConfig(withKeys(token,"\"group_enemies\":true")).groupReplays);
      assert(refused(withKeys(token,"\"group_replays\":true")));   // without the world events
      // Orders taken together (grouporders.h): group play with the notice token only, never beside a partner.
      assert(!mod::parseConfig(token).groupOrders&&mod::parseConfig(withKeys(token,"\"group_orders\":true")).groupOrders);
      assert(refused(withKeys(online,"\"group_orders\":true"))&&refused(withKeys(game,"\"group_orders\":true")));
      assert(mod::parseConfig(withKeys(online,"\"group_roads\":true")).groupRoads&&refused(withKeys(game,"\"group_roads\":true")));
      // Other players' vehicles (othervehicles.h): group play with others drawn.
      assert(!mod::parseConfig(withKeys(game,"\"others\":8")).othersVehicles&&mod::parseConfig(withKeys(game,"\"others\":8,\"others_vehicles\":true")).othersVehicles);
      assert(refused(withKeys(game,"\"others_vehicles\":true")));   // no others drawn
      assert(mod::parseConfig(withKeys(game,"\"others\":8,\"others_carts\":true")).othersCarts&&refused(withKeys(game,"\"others_carts\":true")));
      assert(refused(withKeys(online,"\"build_received\":1"))&&refused(withKeys(online,"\"build_received\":\"true\"")));}
     // Offering the existing strand (offer_strand.h): off by default; with online_server and the notice token only.
     {auto online=withKeys(game,"\"online_server\":\"http://10.1.2.3:49100/w77/auth/ds\"");
      auto offer=mod::parseConfig(withKeys(online,"\"offer_strand\":true,\"strand_token\":\"abcDEF123\""));
      assert(offer.offerStrand&&offer.strandToken=="abcDEF123"&&!offer.strandNotices&&!mod::parseConfig(online).offerStrand);
      auto both=mod::parseConfig(withKeys(online,"\"offer_strand\":true,\"strand_notices\":true,\"strand_token\":\"abc\""));assert(both.offerStrand&&both.strandNotices);
      assert(!mod::parseConfig(withKeys(online,"\"offer_strand\":false")).offerStrand);
      for(auto bad:{"\"offer_strand\":true","\"offer_strand\":true,\"strand_token\":\"\"","\"offer_strand\":true,\"strand_token\":\"a-b\"",
                    "\"offer_strand\":true,\"strand_token\":\"abcdefghijklmnopqrstuvwxyz0123456\"","\"offer_strand\":1,\"strand_token\":\"abc\"","\"offer_strand\":false,\"strand_token\":\"abc\""})
          assert(refused(withKeys(online,bad)));
      assert(refused(withKeys(game,"\"offer_strand\":true,\"strand_token\":\"abc\"")));                                   // no online server
      assert(refused(withKeys(withKeys(game,"\"online_server\":\"http://10.1.2.3:49100/w77/gate\""),"\"offer_strand\":true,\"strand_token\":\"abc\"")));}
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
    assert(refused(withKeys(good,"\"pad\":\""+std::string(8200,'a')+"\"")));      // 8192-byte cap
    {   auto big=withKeys(game,"\"pad\":\"");big.pop_back();big+=std::string(mod::ConfigMax-big.size()-2,'a')+"\"}";   // exactly at the cap still parses structurally
        assert(big.size()==mod::ConfigMax);assert(refused(big));                 // ...but "pad" is an unknown key
    }
    // Route challenges (docs/ROUTES.md): "route" in the string's own form (the mod decodes it whole when it starts) with
    // the game provider; the recording key K by default, 0 off, never F2 or another co-op key.
    {   auto c=mod::parseConfig(withKeys(game,"\"route\":\"DSR1-ABCDE-12345\""));assert(c.route=="DSR1-ABCDE-12345"&&c.routeRecordKey==0x4b);
        assert(mod::parseConfig(game).route.empty());
        for(auto bad:{"\"dsr1-abcde\"","\"DSG-ABCDE-12345\"","\"DSR1-ABCDU-12345\"","\"DSR1-ABCDE 12345\"","\"DSR1-\"","\"DSR1-ABCD\"","1","true"})
            assert(refused(withKeys(game,std::string("\"route\":")+bad)));
        std::string longest="DSR1-";while(longest.size()<mod::RouteMax)longest+=longest.size()%6==4?'-':'Z';
        auto full=withKeys(game,"\"route\":\""+longest+"\"");
        assert(full.size()<mod::ConfigMax&&mod::parseConfig(full).route==longest);
        assert(refused(withKeys(game,"\"route\":\""+longest+"Z\"")));
        assert(refused(withKeys(good,"\"route\":\"DSR1-ABCDE-12345\"")));       // synthetic positions are no run
        assert(mod::parseConfig(withKeys(game,"\"route_record_key\":0")).routeRecordKey==0);
        assert(mod::parseConfig(withKeys(game,"\"route_record_key\":85")).routeRecordKey==85);
        for(auto bad:{"113","255","74","\"75\""})assert(refused(withKeys(game,std::string("\"route_record_key\":")+bad)));
        assert(refused(withKeys(game,"\"story_key\":75")));                     // K is taken by the recording key
        assert(mod::parseConfig(withKeys(game,"\"story_key\":75,\"route_record_key\":0")).storyKey==75);
    }
    {   // Test mode (testmode.h): off without the object; the object (even empty) turns every switch on, U drops crates.
        auto probe=withKeys(threaded,"\"puppet_probe\":true,\"required\":true");
        auto off=mod::parseConfig(probe).testMode;assert(!off.on&&!off.materialsKey&&!off.stamina&&!off.blood&&!off.battery);
        auto on=mod::parseConfig(withKeys(probe,"\"test_mode\":{}")).testMode;
        assert(on.on&&on.materialsKey==0x55&&on.stamina&&on.blood&&on.battery);
        auto some=mod::parseConfig(withKeys(probe,"\"test_mode\":{\"materials\":72,\"stamina\":false,\"battery\":false}")).testMode;
        assert(some.on&&some.materialsKey==72&&!some.stamina&&some.blood&&!some.battery);
        auto none=mod::parseConfig(withKeys(probe,"\"test_mode\":{ \"materials\" : 0 }")).testMode;assert(none.on&&!none.materialsKey);
        // The mod's own keys, mouse buttons and modifiers are refused, and the partner's passenger and catch-up keys.
        for(auto key:{"89","114","80","113","112","9","66","76","1","2","16","17","18","27","13","74","120"})
            assert(refused(withKeys(probe,std::string("\"test_mode\":{\"materials\":")+key+"}")));
        assert(refused(withKeys(probe,"\"passenger_key\":72,\"test_mode\":{\"materials\":72}")));
        assert(mod::parseConfig(withKeys(probe,"\"passenger_key\":72,\"test_mode\":{\"materials\":74}")).testMode.materialsKey==74);   // J once the passenger key moved
        for(int vk=0;vk<=0xfe;++vk)assert(mod::testKeyRefused(unsigned(vk))==(vk==1||vk==2||vk==4||vk==5||vk==6||vk==9||vk==13||vk==27||(vk>=16&&vk<=18)||(vk>=0xa0&&vk<=0xa5)||vk==0x70||vk==0x71||vk==0x72||vk==0x50||vk==0x59||vk==0x42||vk==0x4c));
        // Malformed: not an object, unknown or repeated switches, strings, nesting, out-of-range keys, other objects.
        for(auto bad:{"\"test_mode\":true","\"test_mode\":\"on\"","\"test_mode\":{\"sprint\":true}","\"test_mode\":{\"stamina\":1}",
                      "\"test_mode\":{\"stamina\":true,\"stamina\":true}","\"test_mode\":{\"materials\":\"U\"}","\"test_mode\":{\"blood\":{}}",
                      "\"test_mode\":{\"materials\":255}","\"test_mode\":{\"materials\":085}","\"test_mode\":{\"materials\":-1}","\"test_mode\":{},\"test_mode\":{}",
                      "\"notices\":{}","\"test_mode\":{\"stamina\":true"})
            assert(refused(withKeys(probe,bad)));
        // Only where the game thread runs; the crates only with the game's cargo calls (puppet_probe).
        assert(refused(withKeys(good,"\"test_mode\":{\"materials\":0}"))&&refused(withKeys(game,"\"test_mode\":{\"materials\":0}")));
        assert(refused(withKeys(threaded,"\"test_mode\":{}"))&&mod::parseConfig(withKeys(threaded,"\"test_mode\":{\"materials\":0}")).testMode.stamina);
    }
    // Synthetic provider behaviour is unchanged, including its explicit stub-free trajectory.
    auto a=mod::trajectory(1000,0),b=mod::trajectory(1001,0);assert(ds::sanitise(a));assert(std::abs((b.position[0]-a.position[0])*1000-a.velocity[0])<.02);
    assert(ds::portForSlot(0)==49000&&ds::portForSlot(1)==49001);
    std::cout<<"signature/config/provider tests passed\n";
}
