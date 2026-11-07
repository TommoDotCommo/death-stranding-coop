#include "core.h"
#include <fstream>
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
    std::string json="{\"role\":\""+std::string(host?"host":"client")+"\",\"peer\":\""+s.peer+"\",\"slot\":"+std::to_string(slot+(s.host?0:1))+",\"peer_slot\":"+std::to_string(slot+(s.host?1:0))+",\"nickname\":\""+s.name+"\",\"provider\":\"game\",\"session\":"+std::to_string(s.secret)+",\"required\":true,\"logging\":true,\"self_tick\":true,\"discovery_sha256\":\""+mod::discovery::PinnedHash+"\"}\n";
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
