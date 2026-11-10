#include "core.h"
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
std::string config(const Session& s) {
    if(s.port<49000||s.port>49014||!s.secret||!mod::peerAddress(s.peer))throw std::runtime_error("Use a unicast IPv4 peer and host port 49000..49014.");
    if(s.name.empty()||s.name.size()>32)throw std::runtime_error("Your name must have 1..32 characters.");
    for(unsigned char c:s.name)if(c<32||c>126||c=='"'||c=='\\')throw std::runtime_error("Use a name with plain letters, numbers and spaces.");
    auto slot=s.port-49000;
#ifdef MUTANT_CONFIG_ROLE
    bool host=!s.host;
#else
    bool host=s.host;
#endif
    // Play setup: the game-thread hook, the partner drawn as the mod's own Sam wearing the porter's gear and on his vehicle,
    // session notices in the game's info log, the host's rain and timefall shown in the guest's world, and shared
    // cargo and structures (mod/README.md). The network-only self_tick setup draws nothing.
#ifndef MUTANT_CONFIG_PLAY
    const std::string play="\"game_thread\":true,\"puppet_probe\":true,\"partner\":\"own\",\"partner_gear\":true,\"partner_marker\":true,\"partner_vehicle\":true,\"carry_visual\":true,\"partner_tag\":true,\"notices\":true,\"share_environment\":\"rain\"";
#else
    const std::string play="\"self_tick\":true";
#endif
    std::string json="{\"role\":\""+std::string(host?"host":"client")+"\",\"peer\":\""+s.peer+"\",\"slot\":"+std::to_string(slot+(s.host?0:1))+",\"peer_slot\":"+std::to_string(slot+(s.host?1:0))+",\"nickname\":\""+s.name+"\",\"provider\":\"game\",\"session\":"+std::to_string(s.secret)+",\"required\":true,\"logging\":true,"+play+",\"discovery_sha256\":\""+mod::discovery::PinnedHash+"\"}\n";
    mod::parseConfig(json); // Use the production schema before writing anything.
    return json;
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
void writeConfig(const std::filesystem::path& data,const Session& s) {
    auto bytes=config(s);
    atomicWrite(data/"redirect-data"/"mod"/"mod.json",bytes);
}
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
std::string redact(const std::string& in) {
    std::string out;out.reserve(in.size());
    for(size_t i=0;i<in.size();) {
        [[maybe_unused]] unsigned parts[4];
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
std::string packet(const Packet& p) {
    if(p.port<49000||p.port>49014||!p.nonce)throw std::runtime_error("Invalid lobby packet.");
    std::string b="DSL1";b+=char(p.kind);b+=char(p.ready);b+=char(p.port);b+=char(p.port>>8);
    for(auto n:{p.secret,p.nonce})for(unsigned k=0;k<4;++k)b+=char(n>>(k*8));
    return b;
}
bool parsePacket(const std::string& b,Packet& out) {
    if(b.size()!=16||b.substr(0,4)!="DSL1"||uint8_t(b[4])<1||uint8_t(b[4])>6||uint8_t(b[5])>1)return false;
    Packet p;p.kind=Kind(uint8_t(b[4]));p.ready=b[5];p.port=uint8_t(b[6])|(unsigned(uint8_t(b[7]))<<8);
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
