#pragma once
#include "discovery_options.h"
#include <optional>
#include <string>
#include <cstdint>
namespace mod {
constexpr unsigned OthersMax=32;   // others.h MaxOthers
struct Config {
    unsigned slot=0, peerSlot=1;
    std::string role="none", progressMismatch="warn", joinMove="log", peer, nickname, provider, fault="none", bind;   // bind: local IPv4 for a non-loopback peer (empty = the route's)
    uint32_t peerAddress=0x7f000001, bindAddress=0;                       // host byte order; 0 = choose the route's local address
    uint64_t session=0;
    bool logging=true, required=false, impairment=false, discovery=false, selfTick=false, gameThread=false, puppetProbe=false, virtualPad=false, autoContinue=false, joinAtHost=false;
    // How the partner is shown from the start, without a test command: "off" (default), "peer" (the game's resident
    // double) or "own" (a Sam the mod makes; the double until it exists). Requires puppet_probe and an explicit role.
    std::string partner="off";
    unsigned others=0;         // the most other players shown besides the partner, presence only (others.h); 0: off
    bool partnerVehicle=false; // the partner's vehicle drawn under him while he rides (ride.h)
    bool partnerTag=false;     // the partner's name (their nickname) over the partner body (nametag.h)
    bool shareOrderUnits=false; // with share_orders: a cargo unit one player hands in is delivered for the partner too (a split delivery); not yet run with a real partial hand-in
    bool shareOrderLoad=false;  // with share_orders: one shared load per order (orderload.h); not run live yet
    unsigned passengerKey=0x4a;      // with partner_vehicle: next to the partner's truck this key gets on and off (passenger.h); 0: off
    bool partnerAnimation=true;
    bool partnerCart=false;          // the floating carriers the partner tows, made and moved here by the game's own calls (carts.h)
    bool recordAnimation=false;      // lab: every animation sample sent also goes into the presence log (coop-partner --replay-animation)      // the partner's own animation controls on his body (controls.h, core ANIMATION)
    bool partnerPoses=true;          // a downed or caught partner in the game's own collapse and BT-drag poses (double.h DeadPoseBit); false: laid flat
    bool bodyUpdateLink=true;        // the partner body is joined to this porter's update group (updated in order with him, never beside him: mod.cpp, linkBodyUpdate); false only to compare
    bool shareSavedStructures=false; // on joining, the structures this player's save already has are built in the partner's world too (structures.h)
    bool shareRoads=false;     // what a player delivers to an auto-paver, and a road it builds, also counts in the partner's game (roads.h)
    bool shareOrders=false;    // a standard order one player takes on, completes or gives up does the same for the partner (orders.h)
    bool shareRewards=false;   // with share_orders: a facility's connection points that rise in one game are topped up in the partner's (rewards.h)
    bool shareStoryOrders=false; // experiment, with share_orders: the host's main and sub orders reach the guest's game the same way; never the other way (orders.h)
    std::string storyResync="off"; // experiment: "key" or "auto": the guest loads the host's fresh checkpoint without leaving the game (story.h)
    unsigned storyKey=0x78;    // with story_resync: the guest's catch-up key (F9); 0: none
    bool shareEnemies=false;   // a MULE one player puts down is down in the partner's game too (enemies.h)
    bool shareEnemyPositions=false; // with share_enemies and an explicit role: the guest's MULEs are put where the host's are (enemy_poses.h); not run live yet
    bool enemyPositionPull=false;   // with share_enemy_positions: small errors are corrected too, in short steps (each one a teleport); not run live yet
    bool enemyPositionGoal=false;   // with share_enemy_positions: a walking MULE is sent to the host's place with the game's forced destination; the teleport waits to 10 m
    bool notices=false;        // session notices in the game's info log: partner joined / left / nobody yet (notice.h)
    bool partnerShout=false;   // the partner's shout is a voice line from his body here (signals.h); unverified live
    unsigned partnerShoutVoice=0; // which of the body's sentence containers a shout plays (0..15)
    std::string partnerScan="off"; // the partner's Odradek scan: "notice" (a line in the info log) or "pulse" (also the game's scanner pulse; unverified live)
    bool partnerVitals=false;  // blood, stamina, overloaded and off balance after the name on the partner's tag (signals.h)
    bool partnerLikes=false;   // L gives the partner a like; the partner's likes arrive as the game's likes (likes.h)
    bool partnerMarker=false; // the partner's map pin and the partner's pings from the start (marker.h); no ping key
    bool partnerGear=false;   // the porter's visible gear copied onto the partner's own body (gear.h)
    bool carryVisual=false;   // ds3: send what the porter carries, show what the partner carries (carry_visual.h)
    std::string link="none";   // a stand-in link between two copies on one PC: "internet" or "poor" (link.h)
    std::string onlineServer;  // a private server's sign-in address for the game's online layer (online.h); empty = none
    unsigned presenceLogMaxMiB=8;
    std::string shareEnvironment="off"; // "host": the host's time and rain; "rain": its rain only (weather.h)
    discovery::Options discoveryOptions;
};
// Deliberately small strict flat JSON schema. No paths from JSON are used for writes.
Config parseConfig(const std::string&);
// A numeric IPv4 address a co-op peer may have: loopback 127.0.0.0/8, or any unicast address (LAN, a VPN such as
// Tailscale, or public). Refuses names, 0.0.0.0/8, broadcast, multicast and reserved 240/4. Host byte order.
std::optional<uint32_t> peerAddress(const std::string&);
// A private online server's sign-in address: "http://A.B.C.D:PORT/path" with A.B.C.D loopback (127/8) or exactly
// `peer` (host byte order), and a path of letters, digits and / _ - . only. Returns it unchanged, or nullopt.
constexpr size_t OnlineAddressMax=120;
std::optional<std::string> onlineAddress(const std::string& url,uint32_t peer);
}
