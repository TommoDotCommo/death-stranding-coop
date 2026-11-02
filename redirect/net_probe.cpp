// Windows probe for the outbound network policy. Never runs the game.
// net_probe.exe <proxy.dll>. The expected mode comes from COOP_NETWORK_POLICY (absent = enforce).
//
// Under enforce nothing leaves the PC: every external attempt must be refused before a packet or a DNS query exists.
// Under log-only one UDP datagram goes to 192.0.2.1:9 (TEST-NET-1, RFC 5737: documentation space, never routed) to
// show the call passes and is recorded; no external name is resolved in that mode.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <winhttp.h>
#include <cstdio>
#include <string>

static std::wstring env(const wchar_t* k) {wchar_t b[256];auto n=GetEnvironmentVariableW(k,b,256);return n&&n<256?b:L"";}
static sockaddr_in v4(const char* ip,unsigned short port) {sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,ip,&a.sin_addr);return a;}

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    if(!LoadLibraryW(argv[1])) return 3;
    WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)) return 4;
    bool enforce=env(L"COOP_NETWORK_POLICY")!=L"log-only";
    bool ok=true;
    auto check=[&](bool c){ok=ok&&c;return c;};
    std::string out="{";
    auto field=[&](const char* k,long long v){char b[96];snprintf(b,sizeof b,"%s\"%s\":%lld",out.size()>1?",":"",k,v);out+=b;};

    // 1. Loopback TCP must work in every mode (two game copies on one PC talk this way).
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto any=v4("127.0.0.1",0);int alen=sizeof any;
    bind(listener,(sockaddr*)&any,sizeof any);listen(listener,1);getsockname(listener,(sockaddr*)&any,&alen);
    SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int loop=connect(client,(sockaddr*)&any,sizeof any);int loop_error=loop?WSAGetLastError():0;
    field("loopback_connect_error",loop_error);check(loop==0);
    closesocket(client);closesocket(listener);

    // 2. UDP to an external address: refused under enforce, passed and logged under log-only.
    SOCKET udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);auto testnet=v4("192.0.2.1",9);
    int sent=sendto(udp,"x",1,0,(sockaddr*)&testnet,sizeof testnet);int send_error=sent<0?WSAGetLastError():0;
    field("external_sendto_error",send_error);check(enforce?send_error==WSAEACCES:sent==1);

    // 3. Resolving localhost always works.
    addrinfo* r=nullptr;int local_lookup=getaddrinfo("localhost","9",nullptr,&r);if(r) freeaddrinfo(r);
    field("localhost_lookup_error",local_lookup);check(local_lookup==0);

    // 4. WinHTTP connection handle to an external host (creating it sends nothing in either mode).
    HINTERNET session=WinHttpOpen(L"coop-net-probe",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    HINTERNET connection=session?WinHttpConnect(session,L"example.invalid",443,0):nullptr;
    DWORD http_error=connection?0:GetLastError();
    field("winhttp_session",session!=nullptr);field("winhttp_connect_error",(long long)http_error);
    check(session!=nullptr && (enforce?(connection==nullptr && http_error==ERROR_WINHTTP_CANNOT_CONNECT):connection!=nullptr));
    if(connection) WinHttpCloseHandle(connection);
    if(session) WinHttpCloseHandle(session);

    if(!enforce) {
        // 4b. Log-only: a non-blocking TCP connect to TEST-NET-1. Without any block it stays pending (nothing answers);
        // an outbound firewall block refuses it at once (WSAEACCES). Separates "sent" from "blocked" where UDP cannot:
        // Windows may drop a blocked datagram silently while sendto reports success.
        SOCKET tcp=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);u_long nb=1;ioctlsocket(tcp,FIONBIO,&nb);
        int c=connect(tcp,(sockaddr*)&testnet,sizeof testnet);int first=c?WSAGetLastError():0,final=first;
        if(first==WSAEWOULDBLOCK) {
            fd_set w,e;FD_ZERO(&w);FD_ZERO(&e);FD_SET(tcp,&w);FD_SET(tcp,&e);timeval t{1,0};
            int n=select(0,nullptr,&w,&e,&t);
            if(n==0) final=WSAETIMEDOUT;
            else if(FD_ISSET(tcp,&e)) {int err=0,len=sizeof err;getsockopt(tcp,SOL_SOCKET,SO_ERROR,(char*)&err,&len);final=err;}
            else final=0;
        }
        field("external_tcp_first_error",first);field("external_tcp_error",final);
        closesocket(tcp);
    }
    if(enforce) {
        // 5. External name lookups fail as on an offline PC, before any DNS query.
        r=nullptr;int ext_lookup=getaddrinfo("example.invalid","443",nullptr,&r);if(r) freeaddrinfo(r);
        field("external_lookup_error",ext_lookup);check(ext_lookup==WSAHOST_NOT_FOUND);
        hostent* h=gethostbyname("example.invalid");int hb_error=h?0:WSAGetLastError();
        field("gethostbyname_error",hb_error);check(!h && hb_error==WSAHOST_NOT_FOUND);
        // 6. ConnectEx (an extension pointer from WSAIoctl, not an import) is wrapped too.
        SOCKET tcp=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto bound=v4("0.0.0.0",0);bind(tcp,(sockaddr*)&bound,sizeof bound);
        GUID id=WSAID_CONNECTEX;LPFN_CONNECTEX connect_ex=nullptr;DWORD bytes=0;
        int ioctl=WSAIoctl(tcp,SIO_GET_EXTENSION_FUNCTION_POINTER,&id,sizeof id,&connect_ex,sizeof connect_ex,&bytes,nullptr,nullptr);
        OVERLAPPED o{};BOOL cx=ioctl==0&&connect_ex?connect_ex(tcp,(sockaddr*)&testnet,sizeof testnet,nullptr,0,nullptr,&o):TRUE;
        int cx_error=cx?0:WSAGetLastError();
        field("connectex_ioctl",ioctl);field("connectex_error",cx_error);check(ioctl==0 && !cx && cx_error==WSAEACCES);
        closesocket(tcp);
        // 7. A pointer resolved at run time (GetProcAddress) is the filtered one.
        using SendTo=int(WSAAPI*)(SOCKET,const char*,int,int,const sockaddr*,int);
        auto dynamic=(SendTo)GetProcAddress(GetModuleHandleW(L"ws2_32.dll"),"sendto");
        int dsent=dynamic?dynamic(udp,"x",1,0,(sockaddr*)&testnet,sizeof testnet):0;int d_error=dsent<0?WSAGetLastError():0;
        field("dynamic_sendto_error",d_error);check(dynamic && d_error==WSAEACCES);
    }
    closesocket(udp);
    out+=std::string(",\"mode\":\"")+(enforce?"enforce":"log-only")+"\",\"ok\":"+(ok?"true":"false")+"}";
    std::printf("%s\n",out.c_str());
    return ok?0:5;
}
