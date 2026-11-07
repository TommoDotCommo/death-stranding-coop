#include "core.h"
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
        check(c.peer==s.peer&&c.session==s.secret&&c.required&&c.selfTick&&!c.gameThread&&!c.puppetProbe,"session and read-only defaults");}
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
    std::cout<<"PASS: launcher config, DS invites, discovery, rendezvous, safe release startup and isolated backups\n";
}
