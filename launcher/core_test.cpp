#include "core.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
namespace fs=std::filesystem;
static void check(bool b,const char* what) {if(!b){std::cerr<<"launcher oracle: "<<what<<'\n';std::exit(1);}}
template<class F> static void rejects(F f) {bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,"invalid input accepted");}
int main(int argc,char** argv) {
    check(argc==2,"temporary test root required");fs::path root=argv[1];fs::create_directories(root);
    launcher::Session s;s.peer="100.90.1.2";s.name="Porter";s.port=49003;s.secret=0x12345678;
    for(bool host:{false,true}) {s.host=host;launcher::writeConfig(root,s);
        std::ifstream f(root/"redirect-data/mod/mod.json");std::string bytes((std::istreambuf_iterator<char>(f)),{});auto c=mod::parseConfig(bytes);
        check(c.role==(host?"host":"client")&&c.slot==(host?3:4)&&c.peerSlot==(host?4:3),"role and exact ports");
        check(c.peer==s.peer&&c.session==s.secret&&c.required&&!c.selfTick&&c.gameThread&&c.puppetProbe&&c.partner=="own"&&c.partnerGear&&c.partnerMarker&&c.partnerVehicle&&c.notices&&c.shareEnvironment=="rain"&&!c.autoContinue&&!c.virtualPad,"session and play defaults");}
    {auto solo=s;solo.solo=true;check(!mod::parseConfig(launcher::config(solo)).notices&&mod::parseConfig(launcher::config(s)).notices,"no session notices in a solo session");}
    {   // Shared world (opt-in): the host's game signs in on loopback, the guest's at the host; one secret path for both.
        auto h=s;h.host=true;h.sharedWorld=true;auto g=h;g.host=false;
        check(mod::parseConfig(launcher::config(s)).onlineServer.empty(),"no online server unless asked");
        check(mod::parseConfig(launcher::config(h)).onlineServer=="http://127.0.0.1:49103/w305419896/auth/ds","host signs in on loopback");
        check(mod::parseConfig(launcher::config(g)).onlineServer=="http://100.90.1.2:49103/w305419896/auth/ds","guest signs in at the host");
        auto a=launcher::worldArguments(h,"D");
        check(a==std::vector<std::string>{"--port","49103","--bind","0.0.0.0","--prefix","w305419896","--data","D","--idle-exit","21600"},"server command line");
        check(launcher::worldArguments(g,"D").empty(),"only the host runs the server");
        auto off=h;off.sharedWorld=false;check(launcher::worldArguments(off,"D").empty(),"no server unless asked");
        auto solo=h;solo.solo=true;check(launcher::worldArguments(solo,"D").empty()&&mod::parseConfig(launcher::config(solo)).onlineServer.empty(),"no shared world in a solo session");
        auto lh=h;lh.peer="127.0.0.1";auto lg=g;lg.peer="127.0.0.1";   // one PC: one Steam account, so the address names the player
        check(mod::parseConfig(launcher::config(lh)).onlineServer=="http://127.0.0.1:49103/w305419896/k/host/auth/ds"&&mod::parseConfig(launcher::config(lg)).onlineServer=="http://127.0.0.1:49103/w305419896/k/guest/auth/ds","two players on one PC");
        check(launcher::worldArguments(lh,"D")[3]=="127.0.0.1","loopback only on one PC");
    }
    {   // The lobby packets carry the sender's shared-world setting; the host's decides.
        launcher::Packet p;p.kind=launcher::Kind::accepted;p.secret=7;p.nonce=9;p.port=49002;
        for(bool ready:{false,true})for(bool world:{false,true}){p.ready=ready;p.world=world;launcher::Packet q;
            check(launcher::parsePacket(launcher::packet(p),q)&&q.ready==ready&&q.world==world&&q.secret==7&&q.nonce==9&&q.port==49002,"lobby packet round trip");}
        auto bytes=launcher::packet(p);bytes[5]=4;launcher::Packet q;check(!launcher::parsePacket(bytes,q),"unknown lobby flag refused");
        check(launcher::sessionWorld(true,true,false)&&!launcher::sessionWorld(true,false,true),"the host's own setting");
        check(launcher::sessionWorld(false,false,true)&&!launcher::sessionWorld(false,true,false),"a guest follows the host");
    }
    for(auto peer:{"0.0.0.0","example.com","255.255.255.255","192.168.01.2","224.0.0.1"}) {s.peer=peer;rejects([&]{launcher::config(s);});}s.peer="127.0.0.1";
    for(auto port:{0u,48999u,49015u,65535u}) {s.port=port;rejects([&]{launcher::config(s);});}s.port=49000;
    for(auto name:{"", "x\"", "x\\", "x\n", "012345678901234567890123456789012"}){s.name=name;rejects([&]{launcher::config(s);});}s.name="Porter";
    ci_invite v{};v.secret=s.secret;v.port=49000;v.n=2;v.route[0]={CI_LAN,{192,168,1,20}};v.route[1]={CI_VPN,{100,70,1,2}};
    char code[CI_CODE_MAX];check(ci_encode(&v,"DS",code,sizeof(code))>0,"DS code encoding");
    for(auto text:{std::string(code),std::string("deathstranding-coop://join/")+code,std::string("https://tommodotcommo.github.io/death-stranding-coop/join/#")+code}) {
        cr_result r{};auto kind=coop_recognise(text.data(),text.size(),"DS",&r);check((kind==CR_INVITE||kind==CR_LINK)&&r.invite.secret==v.secret&&r.invite.port==49000&&r.invite.n==2,"DS invite round trip");}
    ci_invite other{};check(ci_decode(code,strlen(code),"WIT",&other,nullptr)==CI_WRONG_GAME,"wrong game rejected");
    code[6]=code[6]=='1'?'2':'1';check(ci_decode(code,strlen(code),"DS",&other,nullptr)!=CI_OK,"damaged DS invite rejected");
    cd_host host{"DSDC","v-test","Porter",CD_IN_LOBBY,0,0,1,2};uint8_t query[13],reply[97];cd_reply parsed{};
    cd_query(query,777,"DSDC",0);int n=cd_answer(query,sizeof(query),&host,reply);check(n&&cd_parse_reply(reply,n,777,"DSDC",&parsed),"DS discovery round trip");
    check(!cd_parse_reply(reply,n,778,"DSDC",&parsed)&&!cd_parse_reply(reply,n,777,"WIT ",&parsed),"nonce and game isolation");
    for(int k=0;k<n;k++)check(!cd_parse_reply(reply,k,777,"DSDC",&parsed),"truncated discovery packet");
    launcher::Packet p;p.secret=123;p.nonce=678;p.port=49002;launcher::Packet q;
    check(launcher::parsePacket(launcher::packet(p),q)&&q.secret==123&&q.nonce==678&&q.port==49002,"rendezvous round trip");
    check(launcher::allowJoin(q,123,true,false)&&!launcher::allowJoin(q,124,true,false)&&!launcher::allowJoin(q,123,true,true),"lobby secret and capacity");
    p.secret=0;check(launcher::allowJoin(p,123,false,false)&&!launcher::allowJoin(p,123,true,false),"open versus invite only");
    auto b=launcher::packet(p);for(size_t i=0;i<b.size();++i)check(!launcher::parsePacket(b.substr(0,i),q),"rendezvous length");
    release_startup::Values values{{"COOP_MODULE_INSTANCE","C:\\Data"},{"COOP_REDIRECT_INSTANCE","C:\\Data\\redirect-data"},{"COOP_REDIRECT_STEAM","C:\\Steam"},
        {"COOP_REAL_LOCAL","C:\\Users\\P\\Local"},{"COOP_REAL_ROAMING","C:\\Users\\P\\Roaming"},{"COOP_REAL_DOCUMENTS","C:\\Users\\P\\Documents"},{"COOP_REAL_SAVEDGAMES","C:\\Users\\P\\Saved Games"},
        {"COOP_MODULE_POLICY","allowlist"},{"COOP_NETWORK_POLICY","enforce"},{"COOP_NETWORK_PEERS","100.90.1.2"}};
    auto t=release_startup::parse(launcher::ticket(values,"C:\\Game\\ds.exe",1000));
    check(release_startup::valid(t,L"c:\\game\\DS.exe",1000)&&!release_startup::valid(t,L"C:\\Other\\ds.exe",1000),"ticket bound to selected copy");
    check(!release_startup::valid(t,L"C:\\Game\\ds.exe",1200)&&!release_startup::valid(t,L"C:\\Game\\ds.exe",900),"ticket expiry and future bounds");
    using M=release_startup::Mode;check(release_startup::decide(true,false,false)==M::passthrough,"missing config must not close normally started game");
    check(release_startup::decide(false,false,false)==M::environment&&release_startup::decide(true,true,false)==M::environment,"lab remains fail closed");
    check(release_startup::decide(true,false,true)==M::ticket,"valid ticket enables isolation");
    {
        using namespace redirect;
        Policy p{L"c:\\users\\p\\local\\deathstrandingcoop\\redirect-data",L"c:\\steam",{{L"c:\\users\\p\\local",L"c:\\users\\p\\local\\deathstrandingcoop\\redirect-data\\local"}}};
        auto real=L"c:\\users\\p\\local\\kojimaproductions\\save";
        check(map(p,real,true).action==Action::deny,"lab still refuses nested real roots");
        check(map_query(p,real).action==Action::deny,"lab still refuses nested real-root queries");
        p.allowNestedInstance=true;
        auto r=map(p,real,true);
        check(r.action==Action::redirect&&r.path==p.instance+L"\\local\\kojimaproductions\\save","release LocalAppData save isolation");
        check(map(p,p.instance+L"\\local\\save",true).action==Action::allow,"release instance exclusion prevents recursion");
        check(map_query(p,real).action==Action::redirect,"release save attributes use isolated copy");
        auto files=map_query(p,L"c:\\users\\p\\local\\kojimaproductions\\*",true);
        check(files.action==Action::redirect&&files.path==p.instance+L"\\local\\kojimaproductions\\*","release enumeration uses isolated copy");
        check(map(p,L"c:\\users\\p\\elsewhere\\save",true).action==Action::deny,"release still refuses outside writes");
    }
    auto broken=t;broken["COOP_NETWORK_POLICY"]="log-only";check(!release_startup::valid(broken,L"C:\\Game\\ds.exe",1000),"network enforcement");
    broken=t;broken.erase("COOP_REAL_LOCAL");check(!release_startup::valid(broken,L"C:\\Game\\ds.exe",1000),"partial ticket rejected");
    rejects([&]{release_startup::parse("target=x\ntarget=y\n");});
    fs::create_directories(root/"redirect-data/local/Game");std::ofstream(root/"redirect-data/local/Game/save")<<"fake isolated save";
    fs::create_directories(root/"real");std::ofstream(root/"real/save")<<"untouched sentinel";
    launcher::backup(root,root/"backup1");check(fs::exists(root/"backup1/local/Game/save")&&!fs::exists(root/"backup1/mod"),"backup only isolated save trees");
    fs::create_symlink(root/"real/save",root/"redirect-data/local/leak");rejects([&]{launcher::backup(root,root/"backup2");});
    fs::remove(root/"redirect-data/local/leak");fs::create_hard_link(root/"real/save",root/"redirect-data/local/hardlink");rejects([&]{launcher::backup(root,root/"backup3");});
    fs::remove(root/"redirect-data/local/hardlink");
    check(fs::file_size(root/"real/save")==18,"real save sentinel unchanged");
    check(launcher::backupAllowed(false,false)&&!launcher::backupAllowed(false,true)&&!launcher::backupAllowed(true,false),"backup requires a closed game and lobby");
    fs::create_directory_symlink(root/"real",root/"linked");rejects([&]{launcher::atomicWrite(root/"linked/child/out","x");});check(!fs::exists(root/"real/child"),"no write through linked ancestor");
    {   // Windows Firewall verdict for the game's incoming UDP, as Windows decides it.
        using F=launcher::Firewall;using R=launcher::FirewallRule;const std::string exe="C:\\Games\\DS\\ds.exe";const unsigned Private=2,Public=4;
        auto verdict=[&](std::vector<R> rules,unsigned active=2,unsigned port=49000,bool on=true){return launcher::firewallVerdict(rules,exe,on,active,port);};
        R allowUdp{"c:/games/ds/DS.EXE",true,true,true,17,"*",Private|Public},blockUdp{exe,true,false,true,17,"",Private|Public};
        check(verdict({})==F::NoRule&&verdict({},Private,49000,false)==F::Off,"no rule: Windows will ask; firewall off");
        check(verdict({allowUdp})==F::Allowed,"allow rule for this program, any slash and case");
        check(verdict({allowUdp,blockUdp})==F::Blocked&&verdict({blockUdp,allowUdp})==F::Blocked,"a block rule wins over an allow rule");
        R tcp=allowUdp;tcp.protocol=6;R other=allowUdp;other.application="C:\\Games\\Other\\ds.exe";R off=allowUdp;off.enabled=false;R out=allowUdp;out.inbound=false;
        check(verdict({tcp})==F::NoRule&&verdict({other})==F::NoRule&&verdict({off})==F::NoRule&&verdict({out})==F::NoRule,"TCP, another program, disabled and outbound rules do not count");
        R priv=allowUdp;priv.profiles=Private;check(verdict({priv},Public)==F::NoRule&&verdict({priv},Private|Public)==F::Allowed,"a rule counts only on an active profile");
        R ports=allowUdp;ports.localPorts="80, 49000-49014";check(verdict({ports},Private,49001)==F::Allowed&&verdict({ports},Private,49015)==F::NoRule,"port lists and ranges");
        R named=allowUdp;named.localPorts="RPC";check(verdict({named})==F::NoRule,"named ports match none");
        R any=allowUdp;any.protocol=256;any.application="";check(verdict({any})==F::Allowed,"an any-program, any-protocol allow rule counts");
        R oldBlock=blockUdp;oldBlock.localPorts="50000";check(verdict({oldBlock,allowUdp})==F::Allowed,"a block rule for another port does not count");
        for(auto f:{F::Unknown,F::Off,F::Allowed,F::Blocked,F::NoRule})check(std::string(launcher::firewallText(f)).size()>20,"a sentence for every verdict");
    }
    {   // Diagnostics report: what a bug report needs, without the session secret, invites, other people's addresses
        // or the Windows user name; the newest logs only, their ends only; bounded.
        const auto npos=std::string::npos;fs::path d=root/"diag";fs::create_directories(d/"redirect-data/mod");
        {std::ofstream f(d/"redirect-data/mod/mod.json");f<<"{\"role\":\"host\",\"peer\":\"203.0.113.7\",\"session\":305419896,\"nickname\":\"Porter\"}";}
        {std::ofstream f(d/"redirect-data/mod/mod-1-a.log");f<<"old log\n";}
        {std::ofstream f(d/"redirect-data/mod/mod-2-b.log");
         for(int i=0;i<400;++i)f<<"line "<<i<<" peer 203.0.113.7:49001 session=305419896 at C:\\Users\\Tom Smith\\AppData\\Local\\X\n";
         f<<"HANDSHAKE ok 127.0.0.1 version 1.2.3.4.5\n";}
        fs::last_write_time(d/"redirect-data/mod/mod-1-a.log",fs::last_write_time(d/"redirect-data/mod/mod-2-b.log")-std::chrono::hours(1));
        {std::ofstream f(d/"redirect-data/redirect-9-c.log",std::ios::binary);std::string s="READY\tinvite DS-ABCD1234EFGH5678 from /Users/tom/x\r\n";
         f.put(char(0xff));f.put(char(0xfe));for(char c:s){f.put(c);f.put(0);}}
        auto r=launcher::diagnostics(d,"launcher 0.1.0 test\ngame folder ok");
        check(r.find("launcher 0.1.0 test")!=npos&&r.find("\"role\":\"host\"")!=npos&&r.find("mod-2-b.log")!=npos&&r.find("line 399 ")!=npos
              &&r.find("READY")!=npos&&r.find("crash record ==\n(none)")!=npos&&r.find("redirect-data/mod/mod.json")!=npos,"diagnostics content");
        check(r.find("old log")==npos&&r.find("line 50 ")==npos&&r.find("line 101 ")!=npos,"newest log, last lines only");
        check(r.find("305419896")==npos&&r.find("ABCD1234")==npos&&r.find("\"session\":<hidden>")!=npos&&r.find("DS-<hidden>")!=npos,"session secret and invite hidden");
        check(r.find("203.0.113.7")==npos&&r.find("203.0.x.x:49001")!=npos&&r.find("127.0.0.1")!=npos&&r.find("1.2.3.4.5")!=npos,"other addresses shortened; loopback and version numbers kept");
        check(r.find("Tom Smith")==npos&&r.find("Users\\<user>\\AppData")!=npos&&r.find("/Users/<user>/x")!=npos,"Windows user name hidden");
        check(launcher::redact("sessions 12 DS-AB 10.1.2.3 300.1.2.3 x\\users\\")=="sessions 12 DS-AB 10.1.x.x 300.1.2.3 x\\users\\","only real secrets, addresses and names change");
        auto big=launcher::diagnostics(d,std::string(400000,'h'));
        check(big.size()<=launcher::DiagnosticsMax&&big.find("(report shortened)")!=npos&&r.size()<launcher::DiagnosticsMax,"report bounded");
        check(launcher::diagnostics(root/"no-such-folder","h").find("(missing)")!=npos,"a missing data folder still gives a report");
    }
    std::cout<<"PASS: launcher config, DS invites, discovery, rendezvous, safe release startup, isolated backups and diagnostics\n";
}
