#include "core.h"
#include "strand.h"
#include "owner.h"
#include <array>
#include "../world/signatures.h"   // the server's check of a signed call, standing in for the server
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <map>
namespace fs=std::filesystem;
static void check(bool b,const char* what) {if(!b){std::cerr<<"launcher oracle: "<<what<<'\n';std::exit(1);}}
template<class F> static void rejects(F f) {bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,"invalid input accepted");}
int main(int argc,char** argv) {
    if((argc==5||argc==6)&&std::string(argv[1])=="--group") {   // for test_group.py: what the launcher gives the group's server and the games
        launcher::Session h;h.name="Host";h.port=unsigned(std::stoul(argv[2]));h.secret=uint32_t(std::stoul(argv[3]));h.group=launcher::Group::Host;h.playerId=77;if(argc==6)h.policy=argv[5];
        for(auto& a:launcher::worldArguments(h,argv[4]))std::cout<<"arg "<<a<<'\n';
        auto hc=mod::parseConfig(launcher::config(h));
        std::cout<<"relay "<<hc.relay<<"\nkey "<<hc.relayKey<<"\nsession "<<hc.session<<"\nhost "<<hc.onlineServer<<'\n';
        ci_invite v{};v.secret=h.secret;v.port=uint16_t(launcher::worldPort(h));v.n=1;v.route[0]={CI_LAN,{127,0,0,1}};
        launcher::Session me;me.name="Joiner";me.playerId=78;
        auto joiner=launcher::groupJoin(v,0,me);
        std::cout<<"join "<<mod::parseConfig(launcher::config(joiner)).onlineServer<<"\njoinaccount "<<launcher::strand::account(joiner,1234)<<'\n';
        return 0;
    }
    if(argc>=6&&std::string(argv[1]).rfind("--strand",0)==0) {   // for test_group.py: the launcher's side of the server's rules, on a real socket
        namespace st=launcher::strand;st::Target t;t.address=argv[2];t.port=unsigned(std::stoul(argv[3]));t.prefix=argv[4];t.account=argv[5];auto send=st::transport(t,2000);
        if(std::string(argv[1])=="--strand"&&(argc==7||argc==9)) {   // the join: ADDRESS PORT PREFIX ACCOUNT INI [BUILDS OLD]
            std::ifstream f(argv[6],std::ios::binary);std::string ini((std::istreambuf_iterator<char>(f)),{});f.close();
            st::Choices given;if(argc==9){given.offerBuilds=argv[7];given.offerOld=argv[8];}
            auto j=st::join(t,send,ini,given);
            std::ofstream(argv[6],std::ios::binary|std::ios::trunc)<<j.ini;
            std::cout<<"fetched "<<int(j.fetched)<<"\nstart "<<j.start<<"\nmessage "<<j.message<<"\nnotice "<<j.passes.noticeToken<<"\npolicy "<<j.raw<<'\n';
            return 0;
        }
        if(std::string(argv[1])=="--strand-mine"&&argc==6) {   // ADDRESS PORT PREFIX ACCOUNT: the strand view's rows
            std::vector<st::Object> list;std::string why;st::Me me;
            if(!st::fetchMine(t,send,list,why)||!st::fetchMe(t,send,me,why)){std::cout<<"refused "<<why<<'\n';return 0;}
            std::cout<<"me "<<me.text()<<'\n';for(auto& o:list)std::cout<<"row "<<o.id<<' '<<o.state<<' '<<o.likes<<' '<<o.text()<<'\n';
            return 0;
        }
        if(std::string(argv[1])=="--strand-act"&&argc==8) {   // ADDRESS PORT PREFIX ACCOUNT offer|withdraw OBJECT|all
            std::string result,object=argv[7];bool ok=st::act(t,send,argv[6],object=="all"?"":object,result);
            std::cout<<(ok?"done ":"refused ")<<result<<'\n';return 0;
        }
        return 2;
    }
    // For test_identity.py: the launcher's side of a server that takes keys, as its --dev-direct --dev-start path runs
    // it (--group-host, --group-join with --prepare-only), on a real socket. KEYFILE is a player.key.
    if(argc>=3&&std::string(argv[1]).rfind("--keyed",0)==0) {
        namespace st=launcher::strand;namespace wid=world::identity;
        const std::string mode=argv[1];auto keys=launcher::playerKeys(argv[argc-1]);
        auto print=[](const launcher::Session& s){auto c=mod::parseConfig(launcher::config(s));
            std::cout<<"online "<<c.onlineServer<<"\nrelay "<<c.relay<<"\nkey "<<c.relayKey<<"\nplayer "<<c.playerId<<"\nstrand "<<c.strandNotices<<"\ntoken "<<c.strandToken<<"\noffer "<<c.offerStrand<<"\nbuild "<<c.buildReceived<<'\n';};
        if(mode=="--keyed-keys"&&argc==3){std::cout<<"pub "<<wid::hex(keys.pub)<<"\naccount "<<wid::account(keys.pub)<<"\nrelay "<<wid::relayId(keys.pub)<<"\nshort "<<wid::shortText(keys.pub)<<'\n';return 0;}
        if((mode=="--keyed-host"||mode=="--keyed-host-config")&&argc==6) {   // PORT SECRET WORLD-FILE KEYFILE: the server's command line, or the host's game
            launcher::Session h;h.name="Host";h.port=unsigned(std::stoul(argv[2]));h.secret=uint32_t(std::stoul(argv[3]));h.group=launcher::Group::Host;h.playerId=77;h.policy="public";
            h.ownerKey=wid::hex(keys.pub);
            if(mode=="--keyed-host"){for(auto& a:launcher::worldArguments(h,argv[4]))std::cout<<"arg "<<a<<'\n';return 0;}
            auto t=st::target(h,0);t.keys=&keys;st::Passes p;bool keyed=false;std::string why;   // launch_win.h: after the server starts, before the config
            if(!st::hostPasses(t,st::transport(t,2000),20,p,keyed,why)){std::cout<<"refused "<<why<<'\n';return 0;}
            if(keyed)st::usePasses(h,p);
            st::useNoticeToken(h,p);
            std::cout<<"keyed "<<keyed<<"\naccount "<<t.account<<'\n';print(h);return 0;}
        if(mode=="--keyed-join"&&(argc==7||argc==9)) {   // ADDRESS PORT SECRET INI [BUILDS OLD] KEYFILE: the join, then the game's config
            ci_invite v{};v.secret=uint32_t(std::stoul(argv[4]));v.port=uint16_t(std::stoul(argv[3]));v.n=1;v.route[0].kind=CI_LAN;
            auto ip=mod::peerAddress(argv[2]);check(ip.has_value(),"address");for(int i=0;i<4;++i)v.route[0].ip[i]=uint8_t(*ip>>(24-8*i));
            std::ifstream f(argv[5],std::ios::binary);std::string ini((std::istreambuf_iterator<char>(f)),{});f.close();
            // The player's name as the launcher keeps it (launcher.ini name=), as the real launcher's Session has it.
            launcher::Session me;me.name="Joiner";me.playerId=78;
            if(auto at=ini.find("\nname=");at!=std::string::npos){auto e=ini.find_first_of("\r\n",at+6);me.name=ini.substr(at+6,e==std::string::npos?std::string::npos:e-at-6);}
            auto s=launcher::groupJoin(v,0,me);
            auto t=st::target(s,0);t.keys=&keys;
            st::Choices given;if(argc==9){given.offerBuilds=argv[6];given.offerOld=argv[7];}
            auto j=st::join(t,st::transport(t,2000),ini,given);std::ofstream(argv[5],std::ios::binary|std::ios::trunc)<<j.ini;
            std::cout<<"fetched "<<int(j.fetched)<<"\nstart "<<j.start<<"\nkeyed "<<j.keyed<<"\naccount "<<j.target.account<<"\nmessage "<<j.message<<'\n';
            if(j.start){if(j.keyed)st::usePasses(s,j.passes);st::useNoticeToken(s,j.passes);print(s);}
            return 0;}
        if(mode=="--keyed-friends"&&argc==7) {   // PORT SECRET ADD|- SHOW|- KEYFILE: the friends window's change as this key, then the list
            launcher::Session me;me.name="Joiner";me.playerId=78;me.group=launcher::Group::Join;me.port=unsigned(std::stoul(argv[2]))-100;me.secret=uint32_t(std::stoul(argv[3]));
            auto t=st::target(me,0);t.keys=&keys;st::Policy p;std::string why;
            if(st::fetchPolicy(t,st::transport(t,2000),p)!=st::Fetch::Ok||!st::identify(t,p,int64_t(std::time(nullptr)),why)){std::cout<<"refused "<<why<<'\n';return 0;}
            st::Friends f;std::string add=argv[4],show=argv[5];
            bool ok=add=="-"&&show=="-"?st::fetchFriends(t,st::transport(t,2000),f,why):st::changeFriends(t,st::transport(t,2000),add=="-"?std::vector<std::string>{}:std::vector<std::string>{add},{},show=="-"?"":show,f,why);
            if(!ok){std::cout<<"refused "<<why<<'\n';return 0;}
            std::cout<<"show "<<f.show<<'\n';for(auto& x:f.list)std::cout<<"friend "<<x.key<<'\n';return 0;}
        if(mode=="--keyed-signed"&&argc==8) {   // PORT PREFIX METHOD CALL BODY KEYFILE: one signed call's headers, for the test to send
            st::Target t;t.address="127.0.0.1";t.port=unsigned(std::stoul(argv[2]));t.prefix=argv[3];t.keys=&keys;st::Policy p;
            check(st::fetchPolicy(t,st::transport(t,2000),p)==st::Fetch::Ok,"the policy of the keyed server");
            const std::string target="/"+t.prefix+"/coop/"+argv[5];
            std::cout<<"target "<<target<<'\n';
            for(auto& h:wid::signRequest(keys,argv[4],target,p.server,argv[6],p.time?p.time:int64_t(std::time(nullptr)),wid::newNonce()))std::cout<<"header "<<h.first<<' '<<h.second<<'\n';
            return 0;}
        return 2;
    }
    // For test_owner.py: the owner's calls as the launcher's --dev-direct --owner-... path makes them (owner::run), on a
    // real socket, from the host's launcher: PORT SECRET KIND [TARGET [VALUE] | PRESET|- SET...] KEYFILE; the report.
    if(argc>=6&&std::string(argv[1])=="--owner-run") {
        namespace ow=launcher::owner;auto keys=launcher::playerKeys(argv[argc-1]);
        launcher::Session h;h.name="Host";h.port=unsigned(std::stoul(argv[2]));h.secret=uint32_t(std::stoul(argv[3]));h.group=launcher::Group::Host;h.playerId=77;h.policy="public";
        h.ownerKey=world::identity::hex(keys.pub);
        ow::Action a;a.kind=argv[4];std::vector<std::string> rest(argv+5,argv+argc-1);
        if(a.kind=="policy"){if(!rest.empty()&&rest[0]!="-")a.preset=rest[0];if(!rest.empty())a.sets.assign(rest.begin()+1,rest.end());}
        else{if(rest.size()>0)a.target=rest[0];if(rest.size()>1)a.value=rest[1];}
        auto t=launcher::strand::target(h,0);t.keys=&keys;
        std::cout<<ow::run(t,launcher::strand::transport(t,2000),a).dump()<<'\n';return 0;
    }
    check(argc==2,"temporary test root required");fs::path root=argv[1];fs::create_directories(root);
    launcher::Session s;s.peer="100.90.1.2";s.name="Porter";s.port=49003;s.secret=0x12345678;
    for(bool host:{false,true}) {s.host=host;launcher::writeConfig(root,s);
        std::ifstream f(root/"redirect-data/mod/mod.json");std::string bytes((std::istreambuf_iterator<char>(f)),{});auto c=mod::parseConfig(bytes);
        check(c.role==(host?"host":"client")&&c.slot==(host?3:4)&&c.peerSlot==(host?4:3),"role and exact ports");
        check(c.peer==s.peer&&c.session==s.secret&&c.required&&!c.selfTick&&c.gameThread&&c.puppetProbe&&c.partner=="own"&&c.partnerGear&&c.partnerMarker&&c.partnerVehicle&&c.notices&&c.shareEnvironment=="rain"&&c.shareEnemies&&c.shareOrders&&c.shareOrderUnits&&c.shareRoads&&c.shareSavedStructures&&!c.autoContinue&&!c.virtualPad,"session and play defaults");}
    {auto solo=s;solo.solo=true;check(!mod::parseConfig(launcher::config(solo)).notices&&mod::parseConfig(launcher::config(s)).notices,"no session notices in a solo session");}
    {   // Test mode (alpha) in the player's own games, solo and two-player: his Settings choice, off by default.
        check(!mod::parseConfig(launcher::config(s)).testMode.on,"no test mode by default");
        auto t=s;t.testMode=true;check(mod::parseConfig(launcher::config(t)).testMode.on,"Settings: test mode in a two-player game");
        auto solo=t;solo.solo=true;auto c=mod::parseConfig(launcher::config(solo)).testMode;check(c.on&&c.materialsKey==0x55,"and in a solo game");
        auto server=s;server.serverTestMode=true;check(!mod::parseConfig(launcher::config(server)).testMode.on,"a server's rules do not reach a two-player game");}
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
    {   // Group play: no partner and no lobby. One player hosts the server (shared world and relay, one key); every
        // player's game names that server and nothing else; a group invite carries the key, the port and the routes.
        using launcher::Group;
        launcher::Session h;h.name="Porter";h.port=49003;h.secret=0x12345678;h.group=Group::Host;h.playerId=123456789012345ull;
        auto hc=mod::parseConfig(launcher::config(h));
        check(hc.group&&hc.peer.empty()&&hc.role=="none"&&hc.server=="127.0.0.1"&&hc.slot==3,"a group player has no partner");
        check(hc.others==launcher::GroupOthers&&hc.relay=="127.0.0.1:49103"&&hc.relayKey=="w305419896"&&hc.playerId==h.playerId&&hc.partnerTag&&hc.session==h.secret&&hc.gameThread&&hc.puppetProbe&&hc.required,"group host config");
        check(hc.relayMode=="broadcast","group play uses the relay's broadcast mode");
        // Other players' structures built when they arrive: the player's setting, on unless he turned it off.
        check(hc.buildReceived,"a group game builds other players' structures when they arrive");
        {auto off=h;off.buildReceived=false;check(!mod::parseConfig(launcher::config(off)).buildReceived,"unless the player turned it off");}
        // Test mode in group play: the server's rules, never the player's own Settings choice.
        check(!hc.testMode.on,"no test mode unless the server's rules say so");
        {auto own=h;own.testMode=true;check(!mod::parseConfig(launcher::config(own)).testMode.on,"the player's own choice does not put a group game in test mode");
         auto rules=h;rules.serverTestMode=true;auto t=mod::parseConfig(launcher::config(rules)).testMode;
         check(t.on&&t.materialsKey==0x55&&t.stamina&&t.blood,"the server's rules put every game there in test mode (U, stamina, blood)");}
        check(hc.onlineServer=="http://127.0.0.1:49103/w305419896/auth/ds","the host's game signs in on loopback, as its own account");
        check(hc.partner=="off"&&!hc.shareOrders&&!hc.shareEnemies&&!hc.shareRoads&&!hc.shareSavedStructures&&hc.shareEnvironment=="off"&&!hc.notices,"nothing of the two-player session in group play");
        auto a=launcher::worldArguments(h,"D");
        check(a==std::vector<std::string>{"--port","49103","--bind","0.0.0.0","--prefix","w305419896","--data","D","--idle-exit","21600","--policy","friends","--relay-port","49103","--relay-key","w305419896"},"group server command line: shared world, rules and relay");
        {auto pub=h;pub.policy="public";auto b=launcher::worldArguments(pub,"D");check(b[10]=="--policy"&&b[11]=="public","the host's choice of rules for a new world");
         auto other=h;other.policy="club";rejects([&]{launcher::worldArguments(other,"D");});}
        check(a.back()==hc.relayKey&&hc.onlineServer.find("/"+a[5]+"/")!=std::string::npos,"one key for the server and the games");
        check(launcher::networkPeers(h)=="127.0.0.1"&&launcher::networkPeers(s)=="100.90.1.2","the proxy lists the server in group play, the partner otherwise");
        std::vector<ci_route> routes{{CI_LAN,{192,168,1,20}},{CI_VPN,{100,70,1,2}}};
        auto code=launcher::groupInvite(h.secret,launcher::worldPort(h),routes);
        check(code.rfind("DSG-",0)==0,"group invite prefix");
        cr_result r{};bool group=false;
        for(auto text:{code,"deathstranding-coop://join/"+code,"https://tommodotcommo.github.io/death-stranding-coop/join/#"+code,"  "+code+"\n"}) {
            int kind=launcher::recognise(text,r,group);
            check((kind==CR_INVITE||kind==CR_LINK)&&group&&r.invite.secret==h.secret&&r.invite.port==49103&&r.invite.n==2&&r.invite.route[1].ip[0]==100,"group invite round trip");}
        ci_invite lobby{};lobby.secret=7;lobby.port=49000;lobby.n=1;lobby.route[0]={CI_LAN,{192,168,1,20}};char text[CI_CODE_MAX];
        check(ci_encode(&lobby,"DS",text,sizeof(text))>0,"lobby invite");
        {cr_result q{};bool g=true;check(launcher::recognise(text,q,g)==CR_INVITE&&!g&&q.invite.secret==7&&q.invite.port==49000,"a two-player lobby invite is still read as one");}
        {ci_invite old{};const char* other=nullptr;check(ci_decode(code.data(),code.size(),"DS",&old,&other)==CI_WRONG_GAME,"a group invite is never taken for a lobby invite");}
        check(ci_encode(&lobby,"MDK2",text,sizeof(text))>0,"another game's invite");
        {cr_result q{};bool g=true;check(launcher::recognise(text,q,g)==CR_ERROR&&!g&&q.error==CI_WRONG_GAME,"another game's invite is refused as before");}
        {auto damaged=code;damaged[8]=damaged[8]=='1'?'2':'1';cr_result q{};bool g=false;check(launcher::recognise(damaged,q,g)==CR_ERROR&&g&&q.error!=CI_WRONG_GAME,"a damaged group invite is refused as a group invite");}
        {cr_result q{};bool g=true;check(launcher::recognise("192.168.1.20:49000",q,g)==CR_ADDRESS&&!g,"addresses as before");}
        check(launcher::groupInvite(0,49103,routes).empty()&&launcher::groupInvite(1,49003,routes).empty()&&launcher::groupInvite(1,49103,{}).empty(),"no invite without a key, a server port or a route");
        check(launcher::groupRoute(r.invite,{false,true})==1&&launcher::groupRoute(r.invite,{true,true})==0&&launcher::groupRoute(r.invite,{false,false})==-1&&launcher::groupRoute(r.invite,{})==-1,"the first route whose server answered");
        launcher::Session me;me.name="Fragile";me.playerId=77;me.peer="10.9.9.9";
        auto j=launcher::groupJoin(r.invite,1,me);auto jc=mod::parseConfig(launcher::config(j));
        check(jc.group&&jc.server=="100.70.1.2"&&jc.relay=="100.70.1.2:49103"&&jc.relayKey==hc.relayKey&&jc.session==hc.session&&jc.playerId==77&&jc.slot==3&&jc.nickname=="Fragile"&&jc.others==launcher::GroupOthers,"a joiner's config names the server from the invite");
        check(jc.onlineServer=="http://100.70.1.2:49103/w305419896/auth/ds","a joiner on another PC signs in as its own account");
        check(jc.buildReceived&&jc.relayMode=="broadcast","a joiner builds other players' structures too");
        {auto off=me;off.buildReceived=false;check(!mod::parseConfig(launcher::config(launcher::groupJoin(r.invite,1,off))).buildReceived,"a joiner's own setting");}
        check(launcher::networkPeers(j)=="100.70.1.2","a joiner's proxy lists the server and nothing else");
        check(launcher::worldArguments(j,"D").empty(),"only the host runs the group's server");
        auto third=me;third.name="Deadman";third.playerId=78;auto t=mod::parseConfig(launcher::config(launcher::groupJoin(r.invite,0,third)));
        check(t.server=="192.168.1.20"&&t.session==jc.session&&t.relayKey==jc.relayKey&&t.playerId!=jc.playerId,"more than two players join one server");
        rejects([&]{launcher::groupJoin(r.invite,-1,me);});rejects([&]{launcher::groupJoin(r.invite,2,me);});
        {auto wrong=r.invite;wrong.port=49003;rejects([&]{launcher::groupJoin(wrong,0,me);});}
        {auto nobody=me;nobody.playerId=0;rejects([&]{launcher::groupJoin(r.invite,0,nobody);});}
        {auto local=r.invite;local.route[0]={CI_LAN,{127,0,0,1}};
         check(mod::parseConfig(launcher::config(launcher::groupJoin(local,0,me))).onlineServer=="http://127.0.0.1:49103/w305419896/k/p77/auth/ds","a joiner on the host's PC (one Steam account) signs in under its own name");}
        for(uint64_t random:{uint64_t(0),uint64_t(1),~uint64_t(0),uint64_t(0x123456789abcdefull),(uint64_t(1)<<49)-1,uint64_t(1)<<49})check(launcher::playerIdValid(launcher::playerId(random)),"a player number the mod's config takes, never 0");
        check(launcher::playerId(5)!=launcher::playerId(6)&&!launcher::playerIdValid(0)&&launcher::playerIdValid(launcher::PlayerIdMax)&&!launcher::playerIdValid(launcher::PlayerIdMax+1),"player number range");
        check(launcher::redact("\"strand_notices\":true,\"strand_token\":\"Ab12cd34\"}")=="\"strand_notices\":true,\"strand_token\":\"<hidden>\"}","a game's notice token is hidden in diagnostics");
        check(launcher::redact("\"relay_key\":\"w305419896\",\"online_server\":\"http://127.0.0.1:49103/w305419896/k/p77/auth/ds\" "+code+" x")
              =="\"relay_key\":\"<hidden>\",\"online_server\":\"http://127.0.0.1:49103/w<hidden>/k/p77/auth/ds\" DSG-<hidden> x","the group's key is hidden in diagnostics in every form");
        check(launcher::redact("data/world/world.json /w12x /w1234567890123/ DSG-AB")=="data/world/world.json /w12x /w1234567890123/ DSG-AB","only the key's own forms change");
        check(launcher::redact("\"online_server\":\"http://127.0.0.1:49103/w305419896/t/9f8e7d6c5b4a39281706f5e4d3c2b1a0/auth/ds\" /t/ab /t/x")
              =="\"online_server\":\"http://127.0.0.1:49103/w<hidden>/t/<hidden>/auth/ds\" /t/ab /t/x","a sign-in ticket is hidden in diagnostics");
        // Identity keys: the game signs in with the ticket and says HELLO with the relay token the launcher took, as the
        // relay id made from its key; the host gives his server his key as the owner's.
        {auto k=h;k.signInTicket="9f8e7d6c5b4a39281706f5e4d3c2b1a0";k.relayToken="0a1b2c3d4e5f60718293a4b5c6d7e8f9";k.playerId=123456;k.ownerKey=std::string(64,'c');
         auto kc=mod::parseConfig(launcher::config(k));
         check(kc.onlineServer=="http://127.0.0.1:49103/w305419896/t/9f8e7d6c5b4a39281706f5e4d3c2b1a0/auth/ds"&&kc.relayKey==k.relayToken&&kc.playerId==123456,"a keyed host signs in with his ticket and says HELLO with his token");
         auto ka=launcher::worldArguments(k,"D");check(ka.size()==a.size()+2&&ka[ka.size()-2]=="--owner"&&ka.back()==k.ownerKey,"the host's key is the owner's");
         auto kj=launcher::groupJoin(r.invite,0,me);kj.signInTicket="abc123";kj.relayToken="tok9";auto kjc=mod::parseConfig(launcher::config(kj));
         check(kjc.onlineServer=="http://192.168.1.20:49103/w305419896/t/abc123/auth/ds"&&kjc.relayKey=="tok9","a keyed joiner too");
         auto same=r.invite;same.route[0]={CI_LAN,{127,0,0,1}};auto kl=launcher::groupJoin(same,0,me);kl.signInTicket="abc123";
         check(mod::parseConfig(launcher::config(kl)).onlineServer=="http://127.0.0.1:49103/w305419896/t/abc123/auth/ds","on the host's PC the ticket names him, not a name in the address");}
    }
    {   // This install's key: made once, kept, the same on every start; a damaged file is an error and stays as it was.
        namespace wid=world::identity;auto file=root/"player.key";std::error_code ec;fs::remove(file,ec);
        auto first=launcher::playerKeys(file),again=launcher::playerKeys(file);
        std::ifstream f(file,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{});f.close();
        check(first.pub==again.pub&&first.seed==again.seed&&text.find("ed25519-seed "+wid::hex(first.seed))!=std::string::npos,"the key is made once and kept");
        check(wid::keysFromSeed(first.seed).pub==first.pub&&wid::shortText(first.pub).size()==19,"the public half comes from the kept seed");
        std::ofstream(file,std::ios::binary|std::ios::trunc)<<"ed25519-seed zz\n";rejects([&]{launcher::playerKeys(file);});
        std::ifstream g(file,std::ios::binary);std::string after((std::istreambuf_iterator<char>(g)),{});check(after=="ed25519-seed zz\n","a damaged key file is never replaced");
        fs::remove(file,ec);
    }
    {   // A group server's rules (strand.h): read before the game connects and shown with the player's own choices for
        // that server, which are kept per server in launcher.ini and sent; then what he has there.
        namespace st=launcher::strand;using launcher::Group;
        launcher::Session h;h.name="Porter";h.port=49003;h.secret=0x12345678;h.group=Group::Host;h.playerId=77;
        check(st::account(h,1234)=="76561197960266962"&&st::account(h,0).empty(),"the host's game signs in as his Steam account");
        auto t=st::target(h,1234);check(t.address=="127.0.0.1"&&t.port==49103&&t.prefix=="w305419896","the host's launcher asks its own server");
        ci_invite v{};v.secret=h.secret;v.port=49103;v.n=1;v.route[0]={CI_LAN,{127,0,0,1}};launcher::Session me;me.name="Joiner";me.playerId=78;
        auto local=launcher::groupJoin(v,0,me);check(st::account(local,1234)=="p78","a joiner on the host's PC acts as the name his game signs in with, never as the host's Steam account");
        v.route[0]={CI_LAN,{192,168,1,20}};auto far=launcher::groupJoin(v,0,me);
        check(st::account(far,99)=="76561197960265827"&&st::target(far,99).address=="192.168.1.20","a joiner on another PC is his own Steam account at the server's address");
        const std::string doc=R"({"format":1,"server":"a1b2c3d4e5f6a7b8","preset":"public","players":{"count":2,"limit":16},"settings":[)"
            R"({"key":"join","value":"open","preset":"open","text":"Open to anyone."},{"key":"build","value":"everyone","preset":"members","text":"What any player builds here may be shared."},)"
            R"({"key":"offer_builds","value":"ask","preset":"yes","text":"Unless you choose otherwise, what you build here waits for your answer."},)"
            R"({"key":"offer_old","value":"no","preset":"no","text":"Your older strand is not taken over."}]})";
        auto p=st::parsePolicy(doc);
        check(p.server=="a1b2c3d4e5f6a7b8"&&p.preset=="public"&&p.players==2&&p.limit==16&&p.settings.size()==4&&p.value("build")=="everyone","the policy read");
        auto shown=p.text();check(shown.find("What any player builds here may be shared. (the owner changed this from the preset's members)")!=std::string::npos&&shown.find("2 of 16 players")!=std::string::npos
                                  &&shown.find("act for any player")!=std::string::npos,"the policy in words, with what the owner changed and that there is no identity");
        auto d=st::defaults(p);check(d.offerBuilds=="ask"&&d.offerOld=="no","a player who chose nothing has the server's choices");
        {   // Test mode: the server's rules decide for its games; a server without the setting (older) is off.
            launcher::Session g;st::usePolicy(g,p);check(!g.serverTestMode,"no test_mode setting: off");
            auto on=st::parsePolicy(doc.substr(0,doc.size()-2)+R"(,{"key":"test_mode","value":"on","preset":"off","text":"Test mode: unlimited materials (U), stamina and blood for everyone here."}]})");
            st::usePolicy(g,on);check(g.serverTestMode&&on.text().find("Test mode: unlimited materials (U)")!=std::string::npos,"test_mode on: the game starts in test mode, and the rules shown say so");
            auto off=st::parsePolicy(doc.substr(0,doc.size()-2)+R"(,{"key":"test_mode","value":"off","preset":"off","text":"No test mode: the game plays as usual."}]})");
            st::usePolicy(g,off);check(!g.serverTestMode,"test_mode off: not in test mode");}
        auto edit=[&](const std::string& from,const std::string& to){auto x=doc;auto at=x.find(from);check(at!=std::string::npos,"fixture");return x.replace(at,from.size(),to);};
        for(auto wrong:{edit("\"format\":1","\"format\":2"),edit("a1b2c3d4e5f6a7b8","a1b2]\\nx=y"),edit("a1b2c3d4e5f6a7b8","ab"),edit("\"preset\":\"public\"","\"preset\":\"club\""),
                        edit("\"key\":\"offer_builds\"","\"key\":\"offer_new\""),edit("Open to anyone.","Open\\u0007"),edit("\"value\":\"ask\"","\"value\":\"maybe\""),
                        edit("\"players\":{\"count\":2","\"players\":{\"count\":\"2\""),std::string("[]"),std::string("not json"),std::string()})
            rejects([&]{st::parsePolicy(wrong);});
        // Kept per server: two servers, two sets of choices; the launcher's own settings untouched; another server
        // starts from its defaults.
        std::string ini="[launcher]\nname=Porter\nplayer_id=78\n";
        ini=st::remember(ini,"a1b2c3d4e5f6a7b8",{"no","yes"});ini=st::remember(ini,"ffff0000ffff0000",{"yes","no"});ini=st::remember(ini,"a1b2c3d4e5f6a7b8",{"no","no"});
        auto mine=st::remembered(ini,p);check(mine.offerBuilds=="no"&&mine.offerOld=="no","this server's remembered choices");
        auto other=p;other.server="ffff0000ffff0000";check(st::remembered(ini,other).offerBuilds=="yes","another server's own choices");
        other.server="0000aaaa0000aaaa";check(st::remembered(ini,other).offerBuilds=="ask"&&st::remembered(ini,other).offerOld=="no","a server not seen before: its defaults");
        check(st::iniGet(ini,"launcher","name")=="Porter"&&ini.find("offer_builds=no\noffer_old=no\n")!=std::string::npos&&ini.find("offer_builds",ini.find("offer_old"))!=std::string::npos,"one line per choice per server");
        auto settings=launcher::settingsText(ini,{{"name","Fragile"},{"group_policy","public"}});
        check(st::iniGet(settings,"launcher","name")=="Fragile"&&st::iniGet(settings,"launcher","group_policy")=="public"&&st::iniGet(settings,"launcher","player_id")=="78"&&st::remembered(settings,p).offerBuilds=="no","saving the settings keeps the choices kept per server");
        rejects([&]{st::iniSet(ini,"launcher","name","a\nb");});
        check(st::remembered("[server-a1b2c3d4e5f6a7b8]\noffer_builds=maybe\n",p).offerBuilds=="ask","a remembered value that is no choice gives way to the server's");
        // The join, against answers given here: the rules read, the choices sent, the game started only when the server took them.
        struct Fake {std::map<std::string,st::Http> answers;std::vector<std::string> sent;bool reach=true;};
        auto fake=[](Fake& f){return st::Transport([&f](const std::string& method,const std::string& path,const st::Headers&,const std::string& body,st::Http& out){
            if(!f.reach){return false;}f.sent.push_back(method+" "+path+" "+body);auto a=f.answers.find(method+" "+path);if(a==f.answers.end())return false;out=a->second;return true;});};
        t.account="76561197960266962";
        {Fake f;f.answers["GET /w305419896/coop/policy"]={200,doc};f.answers["POST /w305419896/coop/choices"]={200,"{}"};
         f.answers["POST /w305419896/coop/notice-token"]={200,"{\"format\":1,\"token\":\"n0t1ce5\",\"expires\":1}"};
         auto j=st::join(t,fake(f),"[launcher]\nname=Porter\n",{});
         check(j.fetched==st::Fetch::Ok&&j.start&&j.choices.offerBuilds=="ask"&&j.raw==doc&&j.message.find("waits for your answer")!=std::string::npos,"the rules read, the server's defaults chosen, the game may start");
         check(f.sent.size()==3&&f.sent[1]=="POST /w305419896/coop/choices {\"player\":\"76561197960266962\",\"offer_builds\":\"ask\",\"offer_old\":\"no\"}","the choices sent for this account");
         // What the game is told: a notice token for the same account, into the game's config with strand_notices.
         check(f.sent[2]=="POST /w305419896/coop/notice-token {\"player\":\"76561197960266962\"}"&&j.passes.noticeToken=="n0t1ce5","a notice token for this account");
         {launcher::Session g=launcher::groupJoin(v,0,me);st::useNoticeToken(g,j.passes);auto gc=mod::parseConfig(launcher::config(g));
          check(gc.strandNotices&&gc.strandToken=="n0t1ce5"&&!gc.notices,"the game's config reads its builds' states with it");
          check(gc.offerStrand,"and offers the save's older strand while the server says yes to it");
          auto bare=mod::parseConfig(launcher::config(launcher::groupJoin(v,0,me)));check(!bare.strandNotices&&!bare.offerStrand&&bare.strandToken.empty(),"without a token: no strand notices, nothing offered");
          auto host=h;st::useNoticeToken(host,j.passes);auto hc2=mod::parseConfig(launcher::config(host));check(hc2.offerStrand&&hc2.strandNotices&&hc2.buildReceived,"the host's game the same");}
         // A server that gives none (an older one, or one that refuses): the game starts all the same, without them.
         {Fake o;o.answers=f.answers;o.answers.erase("POST /w305419896/coop/notice-token");auto k=st::join(t,fake(o),"",{});
          check(k.start&&k.passes.noticeToken.empty()&&o.sent.size()==3,"no notice token: joined all the same");
          Fake bad;bad.answers=f.answers;bad.answers["POST /w305419896/coop/notice-token"]={200,"{\"token\":\"a b\"}"};check(st::join(t,fake(bad),"",{}).passes.noticeToken.empty(),"a token that is none: not taken");
          check(!st::join(t,fake(f),"",{},false).passes.noticeToken.size(),"the choices only (My strand): no new token");}
         check(st::remembered(j.ini,p).offerBuilds=="ask"&&st::iniGet(j.ini,"launcher","name")=="Porter","and remembered for this server");
         Fake g;g.answers=f.answers;auto k=st::join(t,fake(g),"[server-a1b2c3d4e5f6a7b8]\noffer_builds=no\noffer_old=yes\n",{"","no"});
         check(k.start&&k.choices.offerBuilds=="no"&&k.choices.offerOld=="no"&&g.sent[1].find("\"offer_builds\":\"no\",\"offer_old\":\"no\"")!=std::string::npos,"the remembered choices, with what is given over them");
         Fake r;r.answers=f.answers;r.answers["POST /w305419896/coop/choices"]={403,"{\"status\":\"403\",\"code\":403,\"message\":\"this world has all its players\"}"};
         auto refused=st::join(t,fake(r),"",{});
         check(refused.fetched==st::Fetch::Ok&&!refused.start&&refused.message.find("this world has all its players")!=std::string::npos,"choices the server does not take: the game is not started");
         Fake n;n.answers=f.answers;auto none=t;none.account.clear();check(!st::join(none,fake(n),"",{}).start&&n.sent.size()==1,"no account known: nothing sent, nothing started");
         Fake w;w.answers=f.answers;check(!st::join(t,fake(w),"",{"maybe",""}).start&&w.sent.size()==1,"no choice that is not one");}
        {Fake f;f.answers["GET /w305419896/coop/policy"]={404,"{}"};f.answers["GET /w305419896/gate/e/getVersion"]={401,"{}"};
         auto j=st::join(t,fake(f),"",{});check(j.fetched==st::Fetch::Old&&j.start&&f.sent.size()==2&&j.message.find("shares what every player builds")!=std::string::npos,"an older server: said so, joined with its defaults, nothing sent");}
        {Fake f;f.answers["GET /w305419896/coop/policy"]={404,"{}"};f.answers["GET /w305419896/gate/e/getVersion"]={404,"{}"};
         auto j=st::join(t,fake(f),"",{});check(j.fetched==st::Fetch::WrongKey&&!j.start,"a server that does not know the group's key: not joined");}
        {Fake f;f.reach=false;auto j=st::join(t,fake(f),"",{});check(j.fetched==st::Fetch::Unreachable&&!j.start&&j.message.find("did not answer")!=std::string::npos,"no server: not joined");}
        for(auto body:{std::string("garbage"),edit("\"format\":1","\"format\":3")}){Fake f;f.answers["GET /w305419896/coop/policy"]={200,body};
            auto j=st::join(t,fake(f),"",{});check(j.fetched==st::Fetch::Bad&&!j.start,"an answer this launcher cannot read: not joined");}
        {Fake f;f.answers["GET /w305419896/coop/policy"]={500,"{}"};check(st::join(t,fake(f),"",{}).fetched==st::Fetch::Bad,"a server error is not an older server");}
        // Who he sees on the relay: his choice and his friend list, read and changed (the body the server takes).
        {Fake f;f.answers["GET /w305419896/coop/friends?player=76561197960266962"]={200,R"({"format":1,"player":"zygo_x","known":true,"show_others":"friends","friends":[{"key":"ed25519-)"+std::string(64,'a')+R"(","id":"zygo_y","short":"aaaa aaaa aaaa aaaa","known":true,"name":"Bea"},{"key":"cy","id":"zygo_z","short":"","known":false,"name":""}]})"};
         st::Friends got;std::string why;check(st::fetchFriends(t,fake(f),got,why)&&got.show=="friends"&&got.list.size()==2&&got.list[0].text()=="Bea  aaaa aaaa aaaa aaaa"&&got.list[1].text()=="(not seen here yet)  cy","the friend list read");
         f.answers["POST /w305419896/coop/friends"]=f.answers["GET /w305419896/coop/friends?player=76561197960266962"];
         check(st::changeFriends(t,fake(f),{"aaaa aaaa aaaa aaaa"},{"cy"},"nobody",got,why)&&f.sent.back()=="POST /w305419896/coop/friends {\"player\":\"76561197960266962\",\"add\":[\"aaaa aaaa aaaa aaaa\"],\"remove\":[\"cy\"],\"show_others\":\"nobody\"}","a change sent");
         check(st::changeFriends(t,fake(f),{},{},"default",got,why)&&f.sent.back()=="POST /w305419896/coop/friends {\"player\":\"76561197960266962\",\"show_others\":\"default\"}","the choice alone");
         f.answers["POST /w305419896/coop/friends"]={403,R"({"status":"403","code":403,"message":"no player of this world has the key aaaa aaaa aaaa aaaa"})"};
         check(!st::changeFriends(t,fake(f),{"aaaa aaaa aaaa aaaa"},{},"",got,why)&&why=="no player of this world has the key aaaa aaaa aaaa aaaa","the server's reason");
         f.answers["GET /w305419896/coop/friends?player=76561197960266962"]={200,R"({"format":1,"show_others":"some","friends":[]})"};check(!st::fetchFriends(t,fake(f),got,why),"a choice that is none: not read");
         f.answers["GET /w305419896/coop/friends?player=76561197960266962"]={404,"{}"};check(!st::fetchFriends(t,fake(f),got,why)&&why.find("before them")!=std::string::npos,"an older server");
         check(st::shortKeyText("A1B2C3D4 e5f6-a7b8")=="a1b2 c3d4 e5f6 a7b8"&&st::shortKeyText("a1b2 c3d4 e5f6 a7b")==""&&st::shortKeyText("g1b2 c3d4 e5f6 a7b8")=="","short keys as typed");}
        // What he has there, and his offer or withdrawal.
        {Fake f;f.answers["GET /w305419896/coop/mine?player=76561197960266962"]={200,R"({"format":1,"objects":[{"id":"lAAAA","type":"l","kind":"ladder","area":226,"state":"held","waiting_for":"you","held":"member","held_text":"only members build on this server","likes":0},{"id":"eBBBB","kind":"cairn","area":5,"state":"shared","likes":1}]})"};
         f.answers["GET /w305419896/coop/me?player=76561197960266962"]={200,R"({"known":true,"shared":1,"held":{"member":1,"limit":2},"likes":1})"};
         f.answers["POST /w305419896/coop/withdraw"]={200,R"({"remnants":1,"removed":2})"};f.answers["POST /w305419896/coop/offer"]={403,R"({"message":"lAAAA stays held: only members build on this server"})"};
         std::vector<st::Object> list;std::string why;st::Me m;
         check(st::fetchMine(t,fake(f),list,why)&&list.size()==2&&list[0].text()=="ladder  area 226  held: only members build on this server (then your answer)  0 likes  (lAAAA)"&&list[1].text()=="cairn  area 5  shared  1 like  (eBBBB)","the rows of the strand view");
         check(st::fetchMe(t,fake(f),m,why)&&m.held==3&&m.text().find("1 shared")!=std::string::npos,"what he has there");
         std::string result;check(st::act(t,fake(f),"withdraw","",result)&&result=="Withdrawn: 2 removed, 1 stay as remnants."&&f.sent.back()=="POST /w305419896/coop/withdraw {\"player\":\"76561197960266962\",\"all\":true}","withdraw all of his");
         check(!st::act(t,fake(f),"offer","lAAAA",result)&&result=="Refused: lAAAA stays held: only members build on this server."&&f.sent.back().find("\"object\":\"lAAAA\"")!=std::string::npos,"a refusal in the server's words");
         rejects([&]{st::act(t,fake(f),"remove-player","",result);});
         f.answers["GET /w305419896/coop/mine?player=76561197960266962"]={404,"{}"};check(!st::fetchMine(t,fake(f),list,why)&&why.find("before the launcher")!=std::string::npos,"an older server has no list");}
        // HTTP in and out.
        check(st::request(t,"POST","/w1/coop/offer",{},"{}")=="POST /w1/coop/offer HTTP/1.1\r\nHost: 127.0.0.1:49103\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}","the request");
        check(st::request(t,"GET","/w1/coop/me",{{"X-Coop-Key","ab"}},"")=="GET /w1/coop/me HTTP/1.1\r\nHost: 127.0.0.1:49103\r\nContent-Type: application/json\r\nContent-Length: 0\r\nX-Coop-Key: ab\r\nConnection: close\r\n\r\n","a signed request's headers");
        st::Http got;check(st::parseResponse("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",got)&&got.status==403&&got.body=="{}","an answer");
        for(auto wrong:{"HTTP/1.1 200 OK\r\n\r\n{}","HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}","HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}","HTTP/1.1 2x0 OK\r\nContent-Length: 2\r\n\r\n{}",
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",""})check(!st::parseResponse(wrong,got),"an answer in no form this launcher reads");
        // A server that takes keys: every call signed with this install's key and checked as the server checks it, the
        // choices for the key's account, the game's passes taken; the secret half never in a request.
        namespace wid=world::identity;auto keys=wid::makeKeys();const std::string acct=wid::account(keys.pub);
        auto keyedDoc=[&](){auto x=edit("\"preset\":\"public\",","\"preset\":\"public\",\"time\":"+std::to_string(int64_t(std::time(nullptr))+1000)+",");auto at=x.rfind("]}");
            return x.replace(at,2,R"(,{"key":"identity","value":"keys","preset":"keys","text":"Players are known by the key their launcher made."}]})");};
        struct Server {wid::Checker checker;std::map<std::string,st::Http> answers;std::vector<std::string> sent,signers;bool refuseTicket=false,otherRelayId=false,keysOnly=true;};
        auto server=[&](Server& sv){return st::Transport([&sv,&keys](const std::string& method,const std::string& path,const st::Headers& headers,const std::string& body,st::Http& out){
            std::map<std::string,std::string> h;std::string all=method+" "+path+" "+body;
            for(auto& x:headers){std::string n=x.first;for(auto& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));h[n]=x.second;all+=" "+x.first+": "+x.second;}
            sv.sent.push_back(all);
            std::string who,why;auto r=sv.checker.check(h,method,path,body,"a1b2c3d4e5f6a7b8",int64_t(std::time(nullptr))+1000,who,why);
            sv.signers.push_back(r==wid::Checker::Result::Ok?who:r==wid::Checker::Result::Bad?"bad":"");
            if(path.find("/coop/policy")!=std::string::npos){auto a=sv.answers.find("policy");out=a->second;return true;}
            if(r==wid::Checker::Result::Bad||(sv.keysOnly&&r!=wid::Checker::Result::Ok)){out={401,"{\"message\":\"this server takes signed calls from the launcher\"}"};return true;}
            if(path.find("/coop/choices")!=std::string::npos){out={200,"{}"};return true;}
            if(path.find("/coop/ticket")!=std::string::npos){out=sv.refuseTicket?st::Http{403,"{\"message\":\"this world has all its players\"}"}:st::Http{200,"{\"format\":1,\"ticket\":\"9f8e7d6c5b4a39281706f5e4d3c2b1a0\"}"};return true;}
            if(path.find("/coop/notice-token")!=std::string::npos){out={200,"{\"format\":1,\"token\":\"k3yedn0tice\"}"};return true;}
            if(path.find("/coop/relay-token")!=std::string::npos){out={200,"{\"format\":1,\"token\":\"0a1b2c3d4e5f6071\",\"relay_id\":"+std::to_string(wid::relayId(keys.pub)+(sv.otherRelayId?1:0))+"}"};return true;}
            return false;});};
        auto u=t;u.account="76561197960266962";u.keys=&keys;
        {Server sv;sv.answers["policy"]={200,keyedDoc()};auto j=st::join(u,server(sv),"",{});
         check(j.fetched==st::Fetch::Ok&&j.keyed&&j.start&&j.target.account==acct&&j.target.sign,"a server that takes keys: joined as the key's account");
         check(j.passes.ticket=="9f8e7d6c5b4a39281706f5e4d3c2b1a0"&&j.passes.relayToken=="0a1b2c3d4e5f6071"&&j.passes.relayId==wid::relayId(keys.pub),"the game's ticket and relay token, for the relay id of the key");
         check(sv.sent.size()==5&&sv.signers[1]==acct&&sv.signers[2]==acct&&sv.signers[3]==acct&&sv.signers[4]==acct&&sv.sent[1].find("\"player\":\""+acct+"\"")!=std::string::npos,"every call signed by the key, for its own account, on the server's clock");
         check(j.passes.noticeToken=="k3yedn0tice"&&sv.sent[4].find("/coop/notice-token")!=std::string::npos,"and a notice token for its game, signed too");
         bool leaked=false;for(auto& x:sv.sent)leaked|=x.find(wid::hex(keys.seed))!=std::string::npos;check(!leaked,"the secret half of the key is never sent");
         check(j.policy.text().find("knows players by their keys")!=std::string::npos,"the rules say players are known by their keys");
         launcher::Session g=launcher::groupJoin(v,0,me);st::usePasses(g,j.passes);auto gc=mod::parseConfig(launcher::config(g));
         check(gc.onlineServer.find("/t/9f8e7d6c5b4a39281706f5e4d3c2b1a0/auth/ds")!=std::string::npos&&gc.relayKey=="0a1b2c3d4e5f6071"&&gc.playerId==wid::relayId(keys.pub),"the game's config takes the passes");}
        {Server sv;sv.answers["policy"]={200,keyedDoc()};sv.refuseTicket=true;auto j=st::join(u,server(sv),"",{});
         check(j.keyed&&!j.start&&j.message.find("no sign-in ticket")!=std::string::npos&&j.message.find("all its players")!=std::string::npos,"no ticket: the game is not started");}
        {Server sv;sv.answers["policy"]={200,keyedDoc()};sv.otherRelayId=true;check(!st::join(u,server(sv),"",{}).start,"a server that names another relay id for the key: not joined");}
        {Server sv;sv.answers["policy"]={200,keyedDoc()};auto nokey=u;nokey.keys=nullptr;auto j=st::join(nokey,server(sv),"",{});
         check(!j.start&&sv.sent.size()==1&&j.message.find("has none")!=std::string::npos,"no key: nothing sent, not joined");}
        {Server sv;sv.answers["policy"]={200,doc};sv.keysOnly=false;auto j=st::join(u,server(sv),"",{});
         check(j.start&&!j.keyed&&j.target.account=="76561197960266962"&&!j.target.sign&&j.passes.ticket.empty()&&sv.signers[1].empty(),"a server without keys: as before, unsigned, no passes");}
        {Server sv;sv.answers["policy"]={200,keyedDoc()};st::Passes p;bool k2=false;std::string why;auto host=u;
         check(st::hostPasses(host,server(sv),1,p,k2,why)&&k2&&p.ticket.size()==32&&host.account==acct&&p.noticeToken=="k3yedn0tice","the host's own game takes its passes too, and a notice token");
         Server old;old.answers["policy"]={200,doc};old.keysOnly=false;st::Passes q;auto h2=u;check(st::hostPasses(h2,server(old),1,q,k2,why)&&!k2&&q.ticket.empty()&&q.noticeToken=="k3yedn0tice","a host server without keys: no passes, a notice token");
         Fake nobody;nobody.reach=false;auto h3=u;check(!st::hostPasses(h3,fake(nobody),2,q,k2,why)&&why.find("did not answer")!=std::string::npos,"a host server that does not start: said so");}
        // The server owner (owner.h): only the host who started the server with this key; every owner call signed with
        // it on the server's clock, also on a server without player keys; the lists, the actions and the refusals.
        namespace ow=launcher::owner;auto withKey=[&]{auto x=t;x.keys=&keys;return x;};
        {launcher::Session hs=h;hs.ownerKey=wid::hex(keys.pub);check(ow::available(hs,keys),"the host who gave the server his key is its owner");
         auto otherKeys=wid::makeKeys();check(!ow::available(hs,otherKeys),"another key is not the owner");
         launcher::Session js=launcher::groupJoin(v,0,me);js.ownerKey=hs.ownerKey;check(!ow::available(js,keys),"a joiner is never the owner");
         launcher::Session pair;pair.ownerKey=hs.ownerKey;check(!ow::available(pair,keys),"nor a two-player session");
         launcher::Session none=h;check(!ow::available(none,keys),"nor a host whose server has no owner key");}
        struct Owned {wid::Checker checker;std::string policy;std::vector<std::string> sent,signers;std::vector<std::array<std::string,3>> queue;bool refuse=false;};
        const std::string players=R"({"format":1,"players":[{"id":"zygo_ann","name":"Ann","known":true,"member":false,"key":"a1b2 c3d4 e5f6 a7b8","likes":4,"offer_builds":"","offer_old":"","shared":3,"waiting":2,"kept":1,"held":1,"remnants":2,"imported":0},)"
                                  R"({"id":"zygo_far","name":"Far · North","known":false,"member":false,"key":"","likes":0,"offer_builds":"","offer_old":"","shared":0,"waiting":0,"kept":0,"held":0,"remnants":0,"imported":5}]})";
        auto owned=[&](Owned& o){return st::Transport([&o,&acct,players](const std::string& method,const std::string& path,const st::Headers& headers,const std::string& body,st::Http& out){
            if(path.find("/coop/policy")!=std::string::npos){out={200,o.policy};return true;}
            std::map<std::string,std::string> hd;for(auto& x:headers){std::string n=x.first;for(auto& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));hd[n]=x.second;}
            std::string who,why;auto r=o.checker.check(hd,method,path,body,"a1b2c3d4e5f6a7b8",int64_t(std::time(nullptr))+1000,who,why);
            o.sent.push_back(method+" "+path+" "+body);o.signers.push_back(r==wid::Checker::Result::Ok?who:"");
            if(r!=wid::Checker::Result::Ok){out={401,"{\"message\":\"the owner's calls are signed with the owner's key\"}"};return true;}
            if(who!=acct||o.refuse){out={403,"{\"message\":\"only the owner's key does this\"}"};return true;}
            if(path.find("/coop/owner/players")!=std::string::npos){out={200,players};return true;}
            if(path.find("/coop/owner/review")!=std::string::npos&&method=="GET"){
                std::string list;for(auto& q:o.queue)list+=std::string(list.empty()?"":",")+R"({"id":")"+q[0]+R"(","creator":")"+q[1]+R"(","name":"Ann","type":"l","kind":"ladder","area":226,"updated":5,"held":"","waiting_for":")"+q[2]+"\"}";
                out={200,"{\"format\":1,\"waiting\":["+list+"]}"};return true;}
            if(path.find("/coop/owner/review")!=std::string::npos){
                auto in=json::parse(body);for(size_t i=0;i<o.queue.size();++i)if(o.queue[i][0]==in.get("reject").string()||o.queue[i][0]==in.get("accept").string()){o.queue.erase(o.queue.begin()+long(i));break;}
                out={200,"{\"accepted\":1,\"rejected\":1}"};return true;}
            if(path.find("/coop/owner/remove-player")!=std::string::npos){out={200,"{\"removed\":9}"};return true;}
            if(path.find("/coop/owner/member")!=std::string::npos){out={200,"{\"players\":[]}"};return true;}
            if(path.find("/coop/owner/policy")!=std::string::npos){out={200,o.policy};return true;}
            return false;});};
        // A server without player keys (friends): the owner's calls are signed all the same, on the server's clock.
        const std::string friendsDoc=edit("\"preset\":\"public\",","\"preset\":\"public\",\"name\":\"North\",\"time\":"+std::to_string(int64_t(std::time(nullptr))+1000)+",");
        {Owned o;o.policy=friendsDoc;o.queue={{"lA1","zygo_ann","owner"},{"lA2","zygo_ann","owner"},{"lA3","zygo_ann","builder"},{"lB1","zygo_bob","owner"}};
         auto ot=t;ot.keys=&keys;st::Policy pol;std::string why;
         check(ow::connect(ot,owned(o),pol,why)&&ot.sign&&ot.server=="a1b2c3d4e5f6a7b8","the owner's target signs, also where players' calls are not signed");
         ow::View view;check(ow::fetch(ot,owned(o),view,why)&&view.name=="North"&&view.players.size()==2&&view.waiting.size()==4,"the players and the review queue read");
         check(o.signers.size()==2&&o.signers[0]==acct&&o.signers[1]==acct,"every owner call signed by the key on the server's clock");
         check(view.players[0].text()=="Ann  [a1b2 c3d4 e5f6 a7b8]  not a member  shared 3, waiting 2, held 1, kept 1, remnants 2; 4 likes  (zygo_ann)"
               &&view.players[1].text()=="Far \xC2\xB7 North  from linked servers: 5 objects  (zygo_far)","a player's row: key short, counts and likes; a builder from a linked server by his home");
         check(view.waiting[2].text()=="ladder  area 226  by Ann  waits for its builder's answer  (lA3)"&&view.waiting[0].text()=="ladder  area 226  by Ann  waits for you  (lA1)","a waiting object's row");
         auto done=ow::act(ot,owned(o),{"reject-player","zygo_ann","","",{}});
         check(done.ok&&done.message.find("Rejected 2 objects")!=std::string::npos&&o.queue.size()==2&&o.queue[0][0]=="lA3"&&o.queue[1][0]=="lB1","rejecting all of one player: his that wait for the owner, nobody else's, not what waits for him");
         done=ow::act(ot,owned(o),{"member","zygo_ann","yes","",{}});
         check(done.ok&&o.sent.back()=="POST /w305419896/coop/owner/member {\"player\":\"zygo_ann\",\"member\":\"yes\"}","make a member");
         done=ow::act(ot,owned(o),{"remove","zygo_ann","","",{}});
         check(done.ok&&done.message.find("Removed 9 objects")!=std::string::npos&&done.message.find("His account stays")!=std::string::npos&&o.sent.back().find("remove-player {\"player\":\"zygo_ann\"}")!=std::string::npos,"remove a player's objects");
         done=ow::act(ot,owned(o),{"accept","lB1","","",{}});check(done.ok&&o.sent.back().find("review {\"accept\":\"lB1\"}")!=std::string::npos,"accept one");
         done=ow::act(ot,owned(o),{"accept-player","zygo_bob","","",{}});check(done.ok&&o.sent.back().find("review {\"accept_player\":\"zygo_bob\"}")!=std::string::npos,"accept all of a player");
         done=ow::act(ot,owned(o),{"policy","","","friends",{"limit=5","withdrawal=stay"}});
         check(done.ok&&o.sent.back().find("policy {\"preset\":\"friends\",\"set\":[\"limit=5\",\"withdrawal=stay\"]}")!=std::string::npos,"the rules changed");
         for(auto wrong:std::vector<ow::Action>{{"ban","zygo_ann","","",{}},{"member","zygo_ann","maybe","",{}},{"remove","","","",{}},{"policy","","","",{}},{"accept","x","yes","",{}}}){
             auto before=o.sent.size();check(!ow::act(ot,owned(o),wrong).ok&&o.sent.size()==before,"not an action of the owner's: nothing sent");}
         auto report=ow::run(withKey(),owned(o),{"list","","","",{}});
         check(report.get("ok").boolean()&&report.get("name").string()=="North"&&report.get("players").items().size()==2&&report.get("waiting").items().size()==1&&report.get("policy").get("server").string()=="a1b2c3d4e5f6a7b8","the report without a window");
         // Refused: the server's words, and nothing reported as done.
         o.refuse=true;done=ow::act(ot,owned(o),{"member","zygo_ann","no","",{}});
         check(!done.ok&&done.message=="Refused: only the owner's key does this.","a refusal in the server's words");
         check(!ow::act(ot,owned(o),{"reject-player","zygo_ann","","",{}}).ok&&!ow::act(ot,owned(o),{"policy","","","public",{}}).ok,"no refused action is reported as done");
         report=ow::run(withKey(),owned(o),{"remove","zygo_ann","","",{}});check(!report.get("ok").boolean()&&report.get("message").string().find("Refused")!=std::string::npos,"a refused report");
         auto nokey=t;nokey.keys=nullptr;check(!ow::connect(nokey,owned(o),pol,why)&&why.find("no player key")!=std::string::npos,"no key: not the owner");
         Fake older;older.answers["GET /w305419896/coop/policy"]={404,"{}"};older.answers["GET /w305419896/gate/e/getVersion"]={401,"{}"};
         auto ot2=t;ot2.keys=&keys;check(!ow::connect(ot2,fake(older),pol,why)&&why.find("from before the owner's calls")!=std::string::npos,"an older server: said so");}
        // The lists, strictly; what removal means, in the server's own rule.
        rejects([&]{ow::parsePlayers("[]");});rejects([&]{ow::parsePlayers("{\"format\":2,\"players\":[]}");});rejects([&]{ow::parsePlayers("{\"format\":1,\"players\":[{\"id\":\"\"}]}");});
        rejects([&]{ow::parsePlayers("{\"format\":1,\"players\":[{\"id\":\"zygo_a\",\"shared\":\"3\"}]}");});rejects([&]{ow::parsePlayers("{\"format\":1,\"players\":[{\"id\":\"zygo_a\\u0007\"}]}");});
        rejects([&]{ow::parseWaiting("{\"format\":1,\"waiting\":[{\"id\":\"l1\",\"creator\":\"zygo_a\",\"waiting_for\":\"nobody\"}]}");});rejects([&]{ow::parseWaiting("{\"waiting\":[]}");});
        {st::Policy stay,go;stay.settings={{"withdrawal","stay","stay","If you take back what you shared, it stays."}};go.settings={{"withdrawal","go","stay","If you take back what you built, it is removed for everyone."}};
         stay.preset=go.preset="public";
         ow::Player ann;ann.name="Ann";ann.shared=3;ann.waiting=1;ann.remnants=2;ann.imported=1;
         auto a=ow::removalText(stay,ann),b=ow::removalText(go,ann);
         check(a.find("7 objects")!=std::string::npos&&a.find("\"stay\"")!=std::string::npos&&a.find("remnant")!=std::string::npos&&a.find("nothing stays")!=std::string::npos,"removal under withdrawal stay: what stay means, and that nothing stays");
         check(b.find("\"go\"")!=std::string::npos&&b.find("removed for everyone")!=std::string::npos&&a!=b,"removal under withdrawal go");
         check(a.find("This is not a ban")!=std::string::npos&&b.find("This is not a ban")!=std::string::npos,"removal is not a ban, either way");
         auto rows=ow::rules(go);check(rows.size()==1&&rows[0]=="withdrawal = go (preset public: stay)   If you take back what you built, it is removed for everyone.","a rule's row: the value, the preset's where it differs, its words");}
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
        R any=allowUdp;any.protocol=256;check(verdict({any})==F::Allowed,"an any-protocol allow rule counts");
        R nobody=allowUdp;nobody.application="";R nobodyBlocks=blockUdp;nobodyBlocks.application="";
        check(verdict({nobody})==F::NoRule&&verdict({nobodyBlocks,allowUdp})==F::Allowed,"a rule that names no program does not decide (packaged apps, services, one interface)");
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
        check(launcher::redact("Local\\KojimaProductions\\DeathStrandingDC\\239700001\\profile account 239700002 steam_76561198000000001 76561198000000002 userdata/239700003/1850570")
              =="Local\\KojimaProductions\\DeathStrandingDC\\<steam-account>\\profile account <steam-account> steam_<steam-account> <steam-id> userdata/<steam-account>/1850570","Steam accounts are hidden in the text report");
        check(launcher::redact("sessions 12 DS-AB 10.1.2.3 300.1.2.3 x\\users\\")=="sessions 12 DS-AB 10.1.x.x 300.1.2.3 x\\users\\","only real secrets, addresses and names change");
        auto big=launcher::diagnostics(d,std::string(400000,'h'));
        check(big.size()<=launcher::DiagnosticsMax&&big.find("(report shortened)")!=npos&&r.size()<launcher::DiagnosticsMax,"report bounded");
        check(launcher::diagnostics(root/"no-such-folder","h").find("(missing)")!=npos,"a missing data folder still gives a report");
    }
    // Setup from the launcher: install_test.cpp (the shared install core).
    std::cout<<"PASS: launcher config, DS invites, discovery, rendezvous, safe release startup, isolated backups and diagnostics\n";
}
