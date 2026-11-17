// vps-invite: the group invite (DSG-...) for a coop-world on a rented server, made with the launcher's own invite code
// (../../launcher/vendor/coop_invite.h), so testers paste it into Join a friend as they would a host's invite.
//   vps-invite KEY PORT ADDRESS     KEY the group's key (1..4294967295, the server's prefix is "w" and KEY), PORT
//                                   49100..49114 (TCP and UDP), ADDRESS the server's public IPv4 address
//   vps-invite --read CODE          what the launcher's join reads from an invite: key, port and routes
// The invite holds one route, of the kind "internet" (3): the launcher tries it as it tries a host's LAN or VPN route.
// Exit 0, or 2 for anything the launcher would not take (and then nothing is printed on standard output).
#include "../../launcher/vendor/coop_invite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static bool number(const char* text,unsigned long long most,unsigned long long& out){
    if(!*text||std::strlen(text)>10)return false;
    out=0;for(const char* c=text;*c;++c){if(*c<'0'||*c>'9')return false;out=out*10+unsigned(*c-'0');}
    return out<=most;}
// A numeric IPv4 address that is somebody's on the internet: not 0.x, loopback, private, link-local, CGNAT
// (Tailscale's 100.64/10), multicast or reserved. The invite is for testers elsewhere.
static bool publicAddress(const char* text,uint8_t ip[4]){
    unsigned v[4];char end;
    if(std::sscanf(text,"%u.%u.%u.%u%c",&v[0],&v[1],&v[2],&v[3],&end)!=4)return false;
    for(int i=0;i<4;++i){if(v[i]>255)return false;ip[i]=uint8_t(v[i]);}
    if(ip[0]==0||ip[0]==10||ip[0]==127||ip[0]>=224)return false;
    if(ip[0]==169&&ip[1]==254)return false;
    if(ip[0]==172&&ip[1]>=16&&ip[1]<=31)return false;
    if(ip[0]==192&&ip[1]==168)return false;
    if(ip[0]==100&&ip[1]>=64&&ip[1]<=127)return false;
    return true;}
// What the launcher's join requires of a group invite (launcher/core.cpp, groupJoin and recognise).
static bool read(const char* code,ci_invite& v){
    cr_result r{};int kind=coop_recognise(code,std::strlen(code),"DSG",&r);
    if(kind!=CR_INVITE&&kind!=CR_LINK)return false;
    v=r.invite;
    return v.secret&&v.port>=49100&&v.port<=49114&&!(v.flags&CI_FLAG_STEAM)&&v.n>=1;}
int main(int argc,char** argv){
    if(argc==3&&std::strcmp(argv[1],"--read")==0){
        ci_invite v{};if(!read(argv[2],v)){std::fprintf(stderr,"not a group invite the launcher takes\n");return 2;}
        std::printf("key %u port %u",unsigned(v.secret),unsigned(v.port));
        for(int i=0;i<v.n;++i)std::printf(" route %u:%u.%u.%u.%u",unsigned(v.route[i].kind),v.route[i].ip[0],v.route[i].ip[1],v.route[i].ip[2],v.route[i].ip[3]);
        std::printf("\n");return 0;}
    // --private: a rehearsal server on the owner's own LAN or VPN (Tailscale): the address must be private or CGNAT
    // and the route says VPN (2), so such an invite is never mistaken for one to send to testers elsewhere.
    bool priv=argc==5&&std::strcmp(argv[1],"--private")==0;if(priv){++argv;--argc;}
    unsigned long long key=0,port=0;uint8_t ip[4];
    if(priv&&argc==4){uint8_t probe[4];unsigned w[4];char e;
        if(std::sscanf(argv[3],"%u.%u.%u.%u%c",&w[0],&w[1],&w[2],&w[3],&e)!=4||w[0]>255||w[1]>255||w[2]>255||w[3]>255||publicAddress(argv[3],probe)){
            std::fprintf(stderr,"--private takes a LAN or Tailscale address\n");return 2;}
        for(int i=0;i<4;++i)ip[i]=uint8_t(w[i]);
        if(!number(argv[1],4294967295ull,key)||!key||!number(argv[2],49114,port)||port<49100){std::fprintf(stderr,"bad key or port\n");return 2;}
    } else
    if(argc!=4||!number(argv[1],4294967295ull,key)||!key||!number(argv[2],49114,port)||port<49100||!publicAddress(argv[3],ip)){
        std::fprintf(stderr,"usage: vps-invite KEY(1..4294967295) PORT(49100..49114) PUBLIC-IPv4\n       vps-invite --private KEY PORT LAN-OR-TAILSCALE-IPv4   (a rehearsal on your own network)\n       vps-invite --read CODE\n");return 2;}
    ci_invite v{};v.secret=uint32_t(key);v.port=uint16_t(port);v.n=1;v.route[0].kind=priv?CI_VPN:CI_INTERNET;std::memcpy(v.route[0].ip,ip,4);
    char code[CI_CODE_MAX];
    if(!ci_encode(&v,"DSG",code,sizeof code)){std::fprintf(stderr,"the invite could not be made\n");return 2;}
    ci_invite back{};
    if(!read(code,back)||back.secret!=v.secret||back.port!=v.port||back.n!=1||std::memcmp(back.route[0].ip,ip,4)!=0){std::fprintf(stderr,"the invite does not read back\n");return 2;}
    std::printf("%s\n",code);return 0;
}
