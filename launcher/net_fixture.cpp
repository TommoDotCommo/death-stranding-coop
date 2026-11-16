// Native Windows, game-free: two launchers on loopback. No process launch or game/save IO.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "net_win.h"
#include "saves_vectors.h"
#include <cstdio>
#include <stdexcept>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    try{
        WSADATA ws;check(WSAStartup(MAKEWORD(2,2),&ws)==0,"Winsock startup");
        launcher::Network host,client;
        launcher::Session s;s.name="fixture";s.port=49012;
        host.openHost(s,true);
        cd_row row{};row.ip=0x7f000001;row.port=s.port;row.players=1;row.limit=2;row.state=CD_IN_LOBBY;strcpy(row.name,"fixture");
        client.joinHost(row,s,host.session.secret^1);
        for(int i=0;i<40;++i){client.tick();host.tick();Sleep(5);}
        check(!host.joined&&!client.joined,"wrong secret accepted");client.leave();
        client.joinHost(row,s,host.session.secret);
        auto wait=[&](auto predicate,const char* message){auto start=GetTickCount();while(!predicate()&&GetTickCount()-start<3000){host.tick();client.tick();Sleep(5);}check(predicate(),message);};
        wait([&]{return host.joined&&client.joined;},"join rendezvous");
        check(host.session.secret==client.session.secret&&host.session.peer=="127.0.0.1"&&client.session.peer=="127.0.0.1","same session and peer route");
        // The guest starts from the host's save: offered in the lobby, fetched on Ready, Ready reported once it is placed.
        std::string a(VectorA,sizeof(VectorA)-1);size_t meta=std::stoul(a.substr(0,8)),picture=std::stoul(a.substr(8,8));
        std::string block=a.substr(24+meta+picture),file;for(unsigned i=0;i<700000;++i)block+=char(i*2654435761u>>24);
        for(size_t v:{meta,picture,block.size()}){auto t=std::to_string(v);t.resize(8,'\0');file+=t;}
        file+=a.substr(24,meta+picture)+block;
        host.offer.offer(launcher::saves::Checkpoint{"manualsave0",file,"Fixture save"});
        wait([&]{return client.incoming.offered;},"the host's save is offered in the lobby");
        check(client.incoming.size==file.size()&&client.incoming.title=="Fixture save","offer names the save");
        client.setReady();check(client.fetchingSave()&&!client.ready,"Ready first fetches the save");
        for(int i=0;i<20;++i){host.tick();client.tick();Sleep(5);}
        check(!host.peerReady,"the host does not see Ready before the save is in place");
        wait([&]{return client.saveArrived;},"the save crosses the lobby socket");
        check(client.incoming.bytes()==file,"the save arrives unchanged");
        client.saveInstalled();wait([&]{return host.peerReady;},"ready exchange");host.start();
        wait([&]{return host.launch&&client.launch;},"both launch requests acknowledged");
        check(mod::parseConfig(launcher::config(host.session)).slot==12&&mod::parseConfig(launcher::config(client.session)).slot==13,"matching mod ports");
        {   // Group play: a joiner takes the first route of a group invite whose server answers (a TCP listener stands in
            // for coop-world), and its config names that server and nothing else.
            SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto at=launcher::endpoint(0x7f000001,49112);
            check(listener!=INVALID_SOCKET&&!bind(listener,reinterpret_cast<sockaddr*>(&at),sizeof(at))&&!listen(listener,4),"stand-in group server");
            ci_invite v{};v.secret=77;v.port=49112;v.n=2;v.route[0]={CI_LAN,{127,0,0,9}};v.route[1]={CI_LAN,{127,0,0,1}};   // nobody listens on the first
            auto answered=launcher::probeServers(v);
            check(answered.size()==2&&!answered[0]&&answered[1]&&launcher::groupRoute(v,answered)==1,"the route whose server answers");
            launcher::Session me;me.name="fixture";me.playerId=5;
            auto c=mod::parseConfig(launcher::config(launcher::groupJoin(v,launcher::groupRoute(v,answered),me)));
            check(c.group&&c.server=="127.0.0.1"&&c.relay=="127.0.0.1:49112"&&c.session==77,"a joiner's config names the answering server");
            closesocket(listener);
            answered=launcher::probeServers(v,300);
            check(launcher::groupRoute(v,answered)==-1,"no route once the server is gone");
        }
        std::puts("PASS: game-free Windows lobby, wrong-secret refusal, save handoff, ready/start, matching configuration and group routes");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"lobby fixture oracle: %s\n",e.what());return 1;}
}
