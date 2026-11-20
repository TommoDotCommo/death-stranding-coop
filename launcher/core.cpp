#include "core.h"
#include "strand.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdio>
#endif

namespace launcher {
static void safeAncestors(const std::filesystem::path& path) {
#ifdef MUTANT_OUTPUT_LINKS
    (void)path;return;
#endif
    for(auto p=path;!p.empty()&&p!=p.root_path();p=p.parent_path()) {
#ifdef _WIN32
        auto a=GetFileAttributesW(p.c_str());
        if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Refusing a reparse path.");
#else
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(p)))throw std::runtime_error("Refusing a linked path.");
#endif
    }
}
unsigned worldPort(const Session& s){return 49100+(s.port-49000);}
std::string worldPrefix(const Session& s){return "w"+std::to_string(s.secret);}
namespace {bool onePc(const Session& s){auto a=mod::peerAddress(s.peer);return a&&(*a>>24)==127;}}
namespace {
// Group play: where this game reaches the server (the host's own game on loopback), and whether that is the host's PC.
std::string groupServer(const Session& s){return s.group==Group::Host?"127.0.0.1":s.server;}
[[maybe_unused]] bool loopback(const std::string& address){auto a=mod::peerAddress(address);return a&&(*a>>24)==127;}
}
std::string networkPeers(const Session& s){
#ifndef MUTANT_GROUP_PEERS
    if(s.group!=Group::None)return groupServer(s);
#endif
    return s.peer;
}
std::string relayAddress(const Session& s){return groupServer(s)+":"+std::to_string(worldPort(s));}
std::string worldAddress(const Session& s){
    if(s.group!=Group::None){
        // A joiner who reaches the server on loopback is on the host's PC, so on the host's Steam account: the address
        // names him by his player id. The host and everybody on another PC are their own Steam accounts.
        std::string who;
#ifndef MUTANT_GROUP_SAME_PC
        if(s.group==Group::Join&&loopback(s.server))who="/k/p"+std::to_string(s.playerId);
#endif
#ifndef MUTANT_GROUP_TICKET
        // A server that takes keys: the ticket the launcher took names the player (never an address anyone can write).
        if(!s.signInTicket.empty())who="/t/"+s.signInTicket;
#endif
        return "http://"+groupServer(s)+":"+std::to_string(worldPort(s))+"/"+worldPrefix(s)+who+"/auth/ds";
    }
#ifndef MUTANT_WORLD_GUEST_ADDRESS
    std::string at=s.host?"127.0.0.1":s.peer;
#else
    std::string at="127.0.0.1";
#endif
    std::string who;
#ifndef MUTANT_WORLD_ONE_ACCOUNT
    if(onePc(s))who=s.host?"/k/host":"/k/guest";
#endif
    return "http://"+at+":"+std::to_string(worldPort(s))+"/"+worldPrefix(s)+who+"/auth/ds";
}
std::vector<std::string> worldArguments(const Session& s,const std::string& dataFile){
    if(s.group==Group::Host){
        // The group's server: the shared world and the relay on one port number (TCP and UDP), one key for both, on
        // every address of this PC (the host's game uses loopback, the others a LAN or VPN address from the invite).
        std::vector<std::string> a{"--port",std::to_string(worldPort(s)),"--bind","0.0.0.0","--prefix",worldPrefix(s),"--data",dataFile,"--idle-exit","21600"};
        // The rules for a new world (the server keeps an existing world's own and says so).
        if(s.policy!="friends"&&s.policy!="solo"&&s.policy!="public")throw std::runtime_error("The group server's rules are Friends, Solo archive or Public.");
#ifndef MUTANT_GROUP_POLICY
        for(auto more:{std::string("--policy"),s.policy})a.push_back(more);
#endif
#ifndef MUTANT_GROUP_NO_RELAY
        for(auto more:{std::string("--relay-port"),std::to_string(worldPort(s)),std::string("--relay-key"),worldPrefix(s)})a.push_back(more);
#endif
        // The host's key is the owner's: his signed calls are the owner's actions on the running server.
        if(!s.ownerKey.empty())for(auto more:{std::string("--owner"),s.ownerKey})a.push_back(more);
        return a;
    }
    if(s.group!=Group::None)return {};
    if(!s.host||!s.sharedWorld||s.solo)return {};
#ifndef MUTANT_WORLD_BIND
    std::string bind=onePc(s)?"127.0.0.1":"0.0.0.0";
#else
    std::string bind="0.0.0.0";
#endif
    return {"--port",std::to_string(worldPort(s)),"--bind",bind,"--prefix",worldPrefix(s),"--data",dataFile,"--idle-exit","21600"};
}
// The chosen route challenge (Session::route) as a config key, or nothing. The mod checks the string's form when the
// config is read and decodes it whole when it starts.
static std::string routeKey(const Session& s){
#ifndef MUTANT_CONFIG_ROUTE_DROPPED
    return s.route.empty()?std::string():",\"route\":\""+s.route+"\"";
#else
    (void)s;return {};
#endif
}
std::string config(const Session& s) {
    if(s.port<49000||s.port>49014||!s.secret||!mod::peerAddress(s.peer))throw std::runtime_error("Use a unicast IPv4 peer and host port 49000..49014.");
    if(s.name.empty()||s.name.size()>32)throw std::runtime_error("Your name must have 1..32 characters.");
    for(unsigned char c:s.name)if(c<32||c>126||c=='"'||c=='\\')throw std::runtime_error("Use a name with plain letters, numbers and spaces.");
    auto slot=s.port-49000;
#ifndef MUTANT_GROUP_PEER
    if(s.group!=Group::None){
        // Group play: no peer, no role and nothing shared over a two-player session. The server is the only address;
        // the other players are shown (presence only) with their names; the shared world is the server's own.
        auto server=groupServer(s);
        if(!mod::peerAddress(server))throw std::runtime_error("The group server's address must be a numeric IPv4 address.");
        if(!playerIdValid(s.playerId))throw std::runtime_error("This launcher has no player number yet. Start it again.");
#ifndef MUTANT_GROUP_KEY
        std::string key=worldPrefix(s);
#else
        std::string key="w"+std::to_string(s.secret^1);
#endif
#ifndef MUTANT_GROUP_RELAY_TOKEN
        if(!s.relayToken.empty())key=s.relayToken;   // a server that takes keys: this player's relay token, not the group's key
#endif
        std::string json="{\"server\":\""+server+"\",\"slot\":"+std::to_string(slot)+",\"nickname\":\""+s.name+"\",\"provider\":\"game\",\"session\":"+std::to_string(s.secret)
            +",\"required\":true,\"logging\":true,\"game_thread\":true,\"puppet_probe\":true,\"partner_tag\":true,\"others\":"+std::to_string(std::min(s.othersDrawn,32u))
#ifndef MUTANT_GROUP_NO_HELP
            +",\"help_page\":true,\"server_info\":true"   // hold F1, and P for the server page (../mod/README.md)
#endif
#ifndef MUTANT_GROUP_NO_GEAR
            // What other players wear and carry, on their bodies (../mod/README.md, "Other players' gear and carried cases").
            +",\"others_gear\":true,\"others_carry\":"+std::to_string(GroupCarried)
#endif
            +",\"relay\":\""+relayAddress(s)+"\",\"relay_key\":\""+key+"\",\"player_id\":"+std::to_string(s.playerId)
#ifndef MUTANT_GROUP_PAIRS
            +",\"relay_mode\":\"broadcast\""   // publish once, the server copies to everyone near (../mod/README.md, "The broadcast mode")
#endif
            +",\"online_server\":\""+worldAddress(s)+"\""
#ifndef MUTANT_GROUP_BUILD_RECEIVED
            // Other players' structures built when they arrive (../mod/README.md, "Received structures built"): the
            // player's setting, on unless he turned it off; they skip the game's spacing and area checks.
#ifndef MUTANT_GROUP_BUILD_SETTING
            +",\"build_received\":"+std::string(s.buildReceived?"true":"false")
#else
            +",\"build_received\":true"
#endif
#endif
#ifndef MUTANT_GROUP_NO_VEHICLES
            +",\"others_vehicles\":true"   // other players' trucks and bikes, and riding along (J)
            +",\"others_carts\":true"      // the floating carriers other players tow
#endif
#if !defined(MUTANT_GROUP_NO_PINGS)&&!defined(MUTANT_GROUP_PAIRS)   // pings need the broadcast mode: the mod refuses them over pairs
            +",\"group_pings\":true"   // B pings where this porter stands, L likes the nearest player, through the relay
#endif
#ifndef MUTANT_GROUP_NOTICE_TOKEN
            // With the token: the notices, and the save's older strand offered while the server says his offer_old is
            // yes (../mod/README.md, "Offering your existing strand"); the server's word decides, so it is always on.
            +(s.noticeToken.empty()?std::string():std::string(",\"strand_notices\":true")
#ifndef MUTANT_GROUP_NO_ROADS
                +",\"group_roads\":true"   // the others' paver materials given in full (the game takes a share only)
#endif
#ifndef MUTANT_GROUP_NO_ENEMIES
                +",\"group_enemies\":true"   // MULEs down and camps alarmed through the server; its rule `enemies` decides
#endif
#ifndef MUTANT_GROUP_NO_ORDERS
                +",\"group_orders\":true"   // standard orders taken together by the players at one terminal
#endif
#ifndef MUTANT_GROUP_NO_CARGO
                +",\"group_cargo\":true"   // cargo set down shared through the server; its rule `cargo` decides
#endif
#ifndef MUTANT_GROUP_NO_SERVER_WORLD
                +(s.serverWorld.empty()?std::string():",\"server_world\":\""+s.serverWorld+"\"")
#endif
#ifndef MUTANT_GROUP_NO_MAP
                +",\"player_map\":true"   // the others on the game's map; the server's rules say whether anyone is listed
                +",\"player_hud\":true"   // and those not drawn as bodies as markers on the HUD
#endif
#ifndef MUTANT_GROUP_NO_CHAT
                +",\"chat\":true"   // Y, through the server; its rules say whether anyone may (../mod/README.md, "Text chat")
#endif
#ifndef MUTANT_GROUP_OFFER_STRAND
                +",\"offer_strand\":true"
#endif
                +",\"strand_token\":\""+s.noticeToken+"\"")
#endif
            +routeKey(s)
#ifndef MUTANT_GROUP_TEST_OWN
            +(s.serverFreeBuild?",\"free_build\":true":"")
            +(s.serverTestMode?TestModeConfig:"")   // the server's rules, shown before the game started
#else
            +(s.testMode?TestModeConfig:"")
#endif
            +",\"discovery_sha256\":\""+mod::discovery::PinnedHash+"\"}\n";
        mod::parseConfig(json);
        return json;
    }
#endif
#ifdef MUTANT_CONFIG_ROLE
    bool host=!s.host;
#else
    bool host=s.host;
#endif
    // Play setup: the game-thread hook, the partner drawn as the mod's own Sam wearing the porter's gear, on his vehicle and
    // with the floating carriers he tows,
    // session notices in the game's info log, the host's rain and timefall shown in the guest's world, and shared
    // cargo and structures, MULEs put down and camp alarms, and standard orders (mod/README.md). The network-only
    // self_tick setup draws nothing.
#ifndef MUTANT_CONFIG_PLAY
    const std::string play="\"game_thread\":true,\"puppet_probe\":true,\"partner\":\"own\",\"partner_gear\":true,\"partner_marker\":true,\"partner_vehicle\":true,\"partner_cart\":true,\"carry_visual\":true,\"partner_tag\":true,\"help_page\":true,\"server_info\":true,\"notices\":"+std::string(s.solo?"false":"true")+",\"share_environment\":\"rain\",\"share_enemies\":true,\"share_orders\":true,\"share_order_units\":true,\"share_roads\":true,\"share_saved_structures\":true";
#else
    const std::string play="\"self_tick\":true";
#endif
#ifndef MUTANT_WORLD_SOLO
    const std::string world=s.sharedWorld&&!s.solo?",\"online_server\":\""+worldAddress(s)+"\"":"";
#else
    const std::string world=s.sharedWorld?",\"online_server\":\""+worldAddress(s)+"\"":"";
#endif
    std::string json="{\"role\":\""+std::string(host?"host":"client")+"\",\"peer\":\""+s.peer+"\",\"slot\":"+std::to_string(slot+(s.host?0:1))+",\"peer_slot\":"+std::to_string(slot+(s.host?1:0))+",\"nickname\":\""+s.name+"\",\"provider\":\"game\",\"session\":"+std::to_string(s.secret)+",\"required\":true,\"logging\":true,"+play+world+routeKey(s)+(s.testMode?TestModeConfig:"")+",\"discovery_sha256\":\""+mod::discovery::PinnedHash+"\"}\n";
    mod::parseConfig(json); // Use the production schema before writing anything.
    return json;
}
std::string settingsText(const std::string& ini,const std::vector<std::pair<std::string,std::string>>& values) {
#ifndef MUTANT_STRAND_SETTINGS_KEEP
    std::string out=ini;
#else
    std::string out;(void)ini;
#endif
    for(auto& v:values)out=strand::iniSet(out,"launcher",v.first,v.second);
    return out;
}
void atomicWrite(const std::filesystem::path& path,const std::string& bytes) {
    safeAncestors(path);
    std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=".tmp";
    // A trusted, quiescent per-user data folder is required (same as the proxy).
    std::error_code ec;std::filesystem::remove(temp,ec);
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.exceptions(std::ios::badbit|std::ios::failbit);f.write(bytes.data(),bytes.size());f.flush();}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace launcher configuration.");
#else
    std::filesystem::rename(temp,path);
#endif
}
world::identity::Keys playerKeys(const std::filesystem::path& file) {
    namespace id=world::identity;
#ifndef MUTANT_KEYS_KEEP
    std::ifstream f(file,std::ios::binary);
    if(f) {
        std::string text((std::istreambuf_iterator<char>(f)),{});id::Seed seed{};
        auto at=text.find("ed25519-seed ");
        if(at==std::string::npos||!id::unhex(text.substr(at+13,64),seed.data(),seed.size()))
            throw std::runtime_error("The player key file "+file.filename().string()+" cannot be read. Move it away to make a new key (on servers that take keys you are then a new player).");
        return id::keysFromSeed(seed);
    }
#endif
    auto k=id::makeKeys();
    atomicWrite(file,"# Death Stranding co-op player key: made once by this launcher, never sent anywhere. Keep it private;\n"
                     "# on a server that takes keys it is you. Deleting it makes you a new player there.\n"
                     "ed25519-seed "+id::hex(k.seed)+"\n");
    return k;
}
void writeConfig(const std::filesystem::path& data,const Session& s) {
    auto bytes=config(s);
    atomicWrite(data/"redirect-data"/"mod"/"mod.json",bytes);
}
// Setup (what goes where, receipts, the one button): install.h, shared with Setup.
std::string ticket(const release_startup::Values& values,const std::string& target,uint64_t now) {
    auto v=values;v["target"]=target;v["expires"]=std::to_string(now+120);
    if(!release_startup::valid(v,release_startup::wide(target),now))throw std::runtime_error("Invalid isolated launch paths or policies.");
    std::string out;
    for(auto& e:v)out+=e.first+"="+e.second+"\n";
    // Reject newline injection, duplicates and non-ASCII before publication.
    if(release_startup::parse(out)!=v)throw std::runtime_error("Invalid launch ticket text.");
    return out;
}
void backup(const std::filesystem::path& data,const std::filesystem::path& destination) {
    safeAncestors(data/"redirect-data");safeAncestors(destination);
    auto root=std::filesystem::weakly_canonical(data/"redirect-data");
    if(std::filesystem::exists(destination))throw std::runtime_error("Choose a new backup folder.");
    auto dest=std::filesystem::weakly_canonical(destination);
    if(dest==root||dest.string().find(root.string()+std::string(1,std::filesystem::path::preferred_separator))==0)throw std::runtime_error("Backup must be outside the save tree.");
    // Only these isolated trees, never a user-supplied real-save source or mod DLLs/logs.
    for(auto leaf:{"local","roaming","documents","savedgames","profile"}) {
        auto from=root/leaf;
        safeAncestors(from);
        if(!std::filesystem::exists(from))continue;
        for(auto& e:std::filesystem::recursive_directory_iterator(from)) {
            (void)e;
#ifndef MUTANT_BACKUP_LINKS
            safeAncestors(e.path());
            if(e.is_regular_file()&&std::filesystem::hard_link_count(e.path())!=1)throw std::runtime_error("Refusing hard-linked saves.");
#endif
        }
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(from)))throw std::runtime_error("Refusing linked save folders.");
        auto to=dest/leaf;std::filesystem::create_directories(to);
        std::filesystem::copy(from,to,std::filesystem::copy_options::recursive);
    }
}
namespace {
bool digit(char c){return c>='0'&&c<='9';}
// "a.b.c.d" at i (each part 1..3 digits, not inside a longer number or version string): its length, else 0.
[[maybe_unused]] size_t ipv4(const std::string& s,size_t i,unsigned parts[4]) {
    if(i&&(digit(s[i-1])||s[i-1]=='.'))return 0;
    size_t p=i;
    for(int k=0;k<4;++k){
        size_t start=p;unsigned v=0;while(p<s.size()&&digit(s[p])&&p-start<3)v=v*10+unsigned(s[p++]-'0');
        if(p==start||v>255)return 0;
        parts[k]=v;
        if(k<3){if(p>=s.size()||s[p]!='.')return 0;++p;}
    }
    if(p<s.size()&&(digit(s[p])||s[p]=='.'))return 0;
    return p-i;
}
bool startsNoCase(const std::string& s,size_t i,const char* word){
    for(size_t k=0;word[k];++k){if(i+k>=s.size())return false;char c=s[i+k];if(c>='A'&&c<='Z')c=char(c+32);if(c!=word[k])return false;}
    return true;
}
// The newest regular file in `folder` whose name starts with `prefix`; empty when there is none.
std::filesystem::path newest(const std::filesystem::path& folder,const std::string& prefix) {
    std::filesystem::path best;std::filesystem::file_time_type when{};std::error_code ec;
    for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)){
        auto name=it->path().filename().string();std::error_code e2;
        if(name.rfind(prefix,0)!=0||name.size()<4||name.substr(name.size()-4)!=".log"||!it->is_regular_file(e2))continue;
        auto t=it->last_write_time(e2);if(e2)continue;
        if(best.empty()||t>when){best=it->path();when=t;}
    }
    return best;
}
// The last `lines` lines of a log as plain ASCII (the proxy writes UTF-16LE; other bytes become '?'), at most 64 KiB read.
std::string tail(const std::filesystem::path& file,size_t lines) {
    std::ifstream f(file,std::ios::binary);if(!f)return "(unreadable)\n";
    f.seekg(0,std::ios::end);auto size=std::streamoff(f.tellg());const std::streamoff window=64*1024;
    auto from=size>window?size-window:0;from-=from%2;f.seekg(from);
    std::string raw(size_t(size-from),'\0');f.read(raw.data(),std::streamsize(raw.size()));raw.resize(size_t(f.gcount()));
    size_t zeros=0;for(size_t i=1;i<raw.size();i+=2)zeros+=raw[i]==0;
    bool wide=raw.size()>=2&&zeros*4>raw.size();
    std::string text;
    for(size_t i=0;i<raw.size();i+=wide?2:1){
        unsigned char c=static_cast<unsigned char>(raw[i]);bool plain=!wide||(i+1<raw.size()&&raw[i+1]==0);
        if(wide&&i==0&&c==0xff)continue;                       // byte-order mark
        text+=(plain&&(c=='\n'||c=='\t'||(c>=32&&c<127)))?char(c):(c=='\r'?'\0':'?');
    }
    std::string clean;for(char c:text)if(c)clean+=c;
    size_t at=clean.size(),seen=0;
    while(at>0){--at;if(clean[at]=='\n'&&at+1<clean.size()&&++seen>lines){++at;break;}}
    return clean.substr(at);
}
}
namespace {
std::string lowerPath(const std::string& s){std::string o;for(char c:s){if(c=='/')c='\\';if(c>='A'&&c<='Z')c=char(c+32);o+=c;}return o;}
// "49000", "49000-49014", "80,49000-49014" (a rule's LocalPorts); "" and "*" mean every port; names such as RPC match none.
bool portListed(const std::string& list,unsigned port){
    if(list.empty()||list=="*")return true;
    size_t i=0;
    while(i<=list.size()){
        size_t e=list.find(',',i);if(e==std::string::npos)e=list.size();
        std::string part=list.substr(i,e-i);i=e+1;
        while(!part.empty()&&part.front()==' ')part.erase(part.begin());
        while(!part.empty()&&part.back()==' ')part.pop_back();
        if(part.empty()||part.find_first_not_of("0123456789-")!=std::string::npos)continue;
        auto dash=part.find('-');
        try{
            if(dash==std::string::npos){if(std::stoul(part)==port)return true;}
            else if(dash>0&&dash+1<part.size()&&part.find('-',dash+1)==std::string::npos){
                auto lo=std::stoul(part.substr(0,dash)),hi=std::stoul(part.substr(dash+1));if(lo<=port&&port<=hi)return true;}
        }catch(const std::exception&){}
    }
    return false;
}
}
Firewall firewallVerdict(const std::vector<FirewallRule>& rules,const std::string& gameExe,bool firewallOn,unsigned activeProfiles,unsigned port) {
    if(!firewallOn)return Firewall::Off;
    auto exe=lowerPath(gameExe);bool allowed=false;
    for(auto& r:rules) {
        if(!r.enabled||!r.inbound||!(r.profiles&activeProfiles))continue;
        if(r.protocol!=17&&r.protocol!=256)continue;                      // UDP or any
        // Only a rule that names the game decides. A rule without a program is nearly always scoped some other way this
        // list does not show (a packaged app, a service, one interface such as a VPN's): counting those called every
        // game "allowed" on a real PC (2026-11-12).
#ifndef MUTANT_FIREWALL_ANY_PROGRAM
        if(lowerPath(r.application)!=exe)continue;
#else
        if(!r.application.empty()&&lowerPath(r.application)!=exe)continue;
#endif
        if(!portListed(r.localPorts,port))continue;
#ifndef MUTANT_FIREWALL_BLOCK_WINS
        if(!r.allow)return Firewall::Blocked;
        allowed=true;
#else
        allowed=allowed||r.allow;
#endif
    }
    return allowed?Firewall::Allowed:Firewall::NoRule;
}
const char* firewallText(Firewall f) {
    switch(f) {
    case Firewall::Off:return "Windows Firewall is off for this network.";
    case Firewall::Allowed:return "Windows Firewall lets the game receive your partner's data.";
    case Firewall::Blocked:return "Windows Firewall BLOCKS the game: your partner's data cannot arrive. Allow ds.exe under Windows Security > Firewall & network protection > Allow an app through firewall.";
    case Firewall::NoRule:return "Windows Firewall has no rule for the game yet: it will ask when a session starts. Choose Allow.";
    default:return "Windows Firewall could not be read.";
    }
}
std::string redact(const std::string& in) {
    std::string out;out.reserve(in.size());
    for(size_t i=0;i<in.size();) {
        [[maybe_unused]] unsigned parts[4];
#ifndef MUTANT_DIAG_STEAM_ACCOUNT
        // Steam accounts: the 32-bit number that names save and userdata folders ("account N" in the mod's log too),
        // steam_<64-bit id> inside saves, and 64-bit ids (7656119...). Each one finds a player's Steam profile.
        {   bool hit=false;
            for(const char* lead:{"deathstrandingdc\\","deathstrandingdc/","deathstranding\\","deathstranding/","userdata\\","userdata/","account ","account=","steam_"}) {
                size_t n=std::char_traits<char>::length(lead);if(!startsNoCase(in,i,lead))continue;
                size_t d=i+n;while(d<in.size()&&digit(in[d]))++d;
                if(d-(i+n)>=3){out.append(in,i,n);out+="<steam-account>";i=d;hit=true;break;}
            }
            if(hit)continue;
            if(in.compare(i,7,"7656119")==0&&(i==0||!digit(in[i-1]))){size_t d=i;while(d<in.size()&&digit(in[d]))++d;if(d-i==17){out+="<steam-id>";i=d;continue;}}
        }
#endif
#ifndef MUTANT_DIAG_SECRET
        // "session":123 (config, status lines) and session=123 / session 123 (logs).
        if(startsNoCase(in,i,"session")&&(i==0||!((in[i-1]>='a'&&in[i-1]<='z')||(in[i-1]>='A'&&in[i-1]<='Z')))) {
            size_t p=i+7;while(p<in.size()&&(in[p]=='"'||in[p]==':'||in[p]=='='||in[p]==' '))++p;
            size_t d=p;while(d<in.size()&&digit(in[d]))++d;
            if(d>p&&p>i+7){out.append(in,i,p-i);out+="<hidden>";i=d;continue;}
        }
        // Invite codes: DS- followed by the code's letters and digits.
        if(in.compare(i,3,"DS-")==0&&(i==0||!((in[i-1]>='A'&&in[i-1]<='Z')||digit(in[i-1])))) {
            size_t p=i+3;while(p<in.size()&&((in[p]>='A'&&in[p]<='Z')||digit(in[p])||in[p]=='-'))++p;
            if(p-i>=11){out+="DS-<hidden>";i=p;continue;}
        }
#endif
#ifndef MUTANT_DIAG_GROUP_KEY
        // A group's key in its other forms: the group invite (DSG-...), the server's secret path element (/w123...,
        // in online_server and in the mod's log) and the relay key ("relay_key":"...").
        if(in.compare(i,4,"DSG-")==0&&(i==0||!((in[i-1]>='A'&&in[i-1]<='Z')||digit(in[i-1])))) {
            size_t p=i+4;while(p<in.size()&&((in[p]>='A'&&in[p]<='Z')||digit(in[p])||in[p]=='-'))++p;
            if(p-i>=12){out+="DSG-<hidden>";i=p;continue;}
        }
#ifndef MUTANT_DIAG_TICKET
        // A sign-in ticket in its address (/t/TICKET/, identity keys).
        if(in.compare(i,3,"/t/")==0) {
            size_t p=i+3;while(p<in.size()&&((in[p]>='a'&&in[p]<='z')||(in[p]>='A'&&in[p]<='Z')||digit(in[p])))++p;
            if(p-i>=4&&p<in.size()&&in[p]=='/'){out+="/t/<hidden>";i=p;continue;}
        }
#endif
        if(in[i]=='/'&&i+2<in.size()&&in[i+1]=='w'&&digit(in[i+2])) {
            size_t p=i+2;while(p<in.size()&&digit(in[p]))++p;
            if(p-i<=12&&(p==in.size()||in[p]=='/'||in[p]=='"'||in[p]==' '||in[p]=='\n'||in[p]=='\r')){out+="/w<hidden>";i=p;continue;}
        }
#ifndef MUTANT_DIAG_NOTICE_TOKEN
        // A game's notice token ("strand_token":"...", ../mod/README.md "Strand notices"): it reads his builds' states.
        if(startsNoCase(in,i,"strand_token")&&(i==0||!((in[i-1]>='a'&&in[i-1]<='z')||(in[i-1]>='A'&&in[i-1]<='Z')))) {
            size_t p=i+12;while(p<in.size()&&(in[p]=='"'||in[p]==':'||in[p]=='='||in[p]==' '))++p;
            size_t d=p;while(d<in.size()&&((in[d]>='a'&&in[d]<='z')||(in[d]>='A'&&in[d]<='Z')||digit(in[d])))++d;
            if(d>p&&p>i+12){out.append(in,i,p-i);out+="<hidden>";i=d;continue;}
        }
#endif
        if(startsNoCase(in,i,"relay_key")&&(i==0||!((in[i-1]>='a'&&in[i-1]<='z')||(in[i-1]>='A'&&in[i-1]<='Z')))) {
            size_t p=i+9;while(p<in.size()&&(in[p]=='"'||in[p]==':'||in[p]=='='||in[p]==' '))++p;
            size_t d=p;while(d<in.size()&&((in[d]>='a'&&in[d]<='z')||(in[d]>='A'&&in[d]<='Z')||digit(in[d])||in[d]=='-'||in[d]=='_'))++d;
            if(d>p&&p>i+9){out.append(in,i,p-i);out+="<hidden>";i=d;continue;}
        }
#endif
#ifndef MUTANT_DIAG_ADDRESS
        if(digit(in[i]))if(size_t n=ipv4(in,i,parts)) {
            // This PC's loopback stays; any other address keeps its first two numbers (enough to tell LAN, VPN, internet).
            if(parts[0]==127||(parts[0]==0&&parts[1]==0))out.append(in,i,n);
            else out+=std::to_string(parts[0])+"."+std::to_string(parts[1])+".x.x";
            i+=n;continue;
        }
#endif
#ifndef MUTANT_DIAG_USER
        // C:\Users\name\ and /Users/name/ (any drive, either slash): the name is replaced.
        if((in[i]=='\\'||in[i]=='/')&&startsNoCase(in,i+1,"users")&&i+6<in.size()&&(in[i+6]=='\\'||in[i+6]=='/')) {
            size_t p=i+7,e=p;while(e<in.size()&&in[e]!='\\'&&in[e]!='/'&&in[e]!='\n'&&in[e]!='"'&&in[e]!='\t')++e;
            if(e>p){out.append(in,i,p-i);out+="<user>";i=e;continue;}
        }
#endif
        out+=in[i++];
    }
    return out;
}
std::string diagnostics(const std::filesystem::path& data,const std::string& header) {
    auto mod=data/"redirect-data"/"mod";auto proxy=data/"redirect-data";
    std::string out="Death Stranding co-op diagnostics\n"+header;
    if(out.back()!='\n')out+='\n';
    out+="data folder: "+data.string()+"\n\n== mod.json ==\n";
    {std::ifstream f(mod/"mod.json",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(f)),{});
     out+=f||!bytes.empty()?bytes.substr(0,4096)+"\n":"(missing)\n";}
    out+="\n== files ==\n";
    for(auto& folder:{data,proxy,mod}) {
        std::error_code ec;size_t listed=0;
        for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end&&listed<80;it.increment(ec),++listed) {
            std::error_code e2;bool dir=it->is_directory(e2);auto size=dir?0:it->file_size(e2);
            out+=(dir?"[folder] ":"")+std::filesystem::relative(it->path(),data,e2).generic_string()+(dir?"":"  "+std::to_string(size)+" bytes")+"\n";
        }
        if(ec)out+="(cannot list "+std::filesystem::relative(folder,data,ec).generic_string()+")\n";
    }
    struct Part{const char* title;std::filesystem::path folder;const char* prefix;size_t lines;};
    for(auto& part:{Part{"mod log",mod,"mod-",300},Part{"proxy log",proxy,"redirect-",200},Part{"crash record",proxy,"crash-",200}}) {
        auto file=newest(part.folder,part.prefix);
        out+="\n== "+std::string(part.title)+(file.empty()?" ==\n(none)\n":": "+file.filename().string()+" ==\n"+tail(file,part.lines));
        if(out.back()!='\n')out+='\n';
    }
    out=redact(out);
#ifndef MUTANT_DIAG_BOUND
    if(out.size()>DiagnosticsMax){const std::string cut="\n(report shortened)\n";out.resize(DiagnosticsMax-cut.size());out+=cut;}
#endif
    return out;
}
std::string groupInvite(uint32_t secret,unsigned serverPort,const std::vector<ci_route>& routes) {
    if(!secret||serverPort<49100||serverPort>49114||routes.empty()||routes.size()>CI_MAX_ROUTES)return {};
    ci_invite v{};v.secret=secret;v.port=uint16_t(serverPort);v.n=int(routes.size());
    for(size_t i=0;i<routes.size();++i)v.route[i]=routes[i];
    char code[CI_CODE_MAX];return ci_encode(&v,GroupPrefix,code,sizeof(code))?std::string(code):std::string();
}
int recognise(const std::string& text,cr_result& result,bool& group) {
    group=false;
    int kind=coop_recognise(text.data(),text.size(),Prefix,&result);
#ifndef MUTANT_GROUP_INVITE_KIND
    // The kit says "an invite for another game" for the other prefix of this game: read it as what it is.
    if(kind==CR_ERROR&&result.error==CI_WRONG_GAME){
        cr_result other{};int k=coop_recognise(text.data(),text.size(),GroupPrefix,&other);
        if(k!=CR_ERROR||other.error!=CI_WRONG_GAME){result=other;group=true;return k;}
    }
#else
    if(kind==CR_ERROR){cr_result other{};int k=coop_recognise(text.data(),text.size(),GroupPrefix,&other);if(k==CR_INVITE||k==CR_LINK){result=other;return k;}}
#endif
    return kind;
}
int groupRoute(const ci_invite& invite,const std::vector<bool>& answered) {
    for(int i=0;i<invite.n&&i<int(answered.size());++i) {
#ifndef MUTANT_GROUP_ROUTE
        if(answered[size_t(i)])return i;
#else
        return i;
#endif
    }
    return -1;
}
Session groupJoin(const ci_invite& invite,int route,Session mine) {
    if(!invite.secret||invite.port<49100||invite.port>49114||(invite.flags&CI_FLAG_STEAM))throw std::runtime_error("This is not a group invite this version can use. Ask for a new one.");
    if(route<0||route>=invite.n)throw std::runtime_error("The group's server did not answer. Ask the host to start it, check the invite and the host's Windows Firewall, then try again.");
    auto& ip=invite.route[route].ip;
    mine.group=Group::Join;mine.host=false;mine.solo=false;mine.secret=invite.secret;mine.port=unsigned(invite.port)-100;
    mine.server=std::to_string(ip[0])+"."+std::to_string(ip[1])+"."+std::to_string(ip[2])+"."+std::to_string(ip[3]);
    config(mine);   // the production schema decides
    return mine;
}
std::string packet(const Packet& p) {
    if(p.port<49000||p.port>49014||!p.nonce)throw std::runtime_error("Invalid lobby packet.");
    std::string b="DSL1";b+=char(p.kind);b+=char((p.ready?1:0)|(p.world?2:0));b+=char(p.port);b+=char(p.port>>8);
    for(auto n:{p.secret,p.nonce})for(unsigned k=0;k<4;++k)b+=char(n>>(k*8));
    return b;
}
bool parsePacket(const std::string& b,Packet& out) {
    if(b.size()!=16||b.substr(0,4)!="DSL1"||uint8_t(b[4])<1||uint8_t(b[4])>6||uint8_t(b[5])>3)return false;
    Packet p;p.kind=Kind(uint8_t(b[4]));p.ready=b[5]&1;
#ifndef MUTANT_LOBBY_WORLD_BIT
    p.world=b[5]&2;
#endif
p.port=uint8_t(b[6])|(unsigned(uint8_t(b[7]))<<8);
    p.secret=cd_le32(reinterpret_cast<const uint8_t*>(b.data()+8));p.nonce=cd_le32(reinterpret_cast<const uint8_t*>(b.data()+12));
    if(p.port<49000||p.port>49014||!p.nonce)return false;
    out=p;return true;
}
bool allowJoin(const Packet& p,uint32_t secret,bool inviteOnly,bool occupied) {
#ifdef MUTANT_LOBBY_SECRET
    (void)inviteOnly;(void)secret;
    return !occupied&&p.kind==Kind::join;
#else
    return !occupied&&p.kind==Kind::join&&(p.secret==secret||(!inviteOnly&&!p.secret));
#endif
}
}
