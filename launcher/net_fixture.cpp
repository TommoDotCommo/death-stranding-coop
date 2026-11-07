// Native Windows, game-free: two launchers on loopback. No process launch or game/save IO.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "net_win.h"
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
        client.setReady();wait([&]{return host.peerReady;},"ready exchange");host.start();
        wait([&]{return host.launch&&client.launch;},"both launch requests acknowledged");
        check(mod::parseConfig(launcher::config(host.session)).slot==12&&mod::parseConfig(launcher::config(client.session)).slot==13,"matching mod ports");
        std::puts("PASS: game-free Windows lobby, wrong-secret refusal, ready/start and matching configuration");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"lobby fixture oracle: %s\n",e.what());return 1;}
}
