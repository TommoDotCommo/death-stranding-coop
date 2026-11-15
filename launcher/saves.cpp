#include "saves.h"
// The one thing this file needs from the launcher (core.cpp there); a program that links the saves alone brings its own.
namespace launcher { void atomicWrite(const std::filesystem::path&,const std::string&); }
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fs=std::filesystem;
namespace launcher::saves {
namespace {
constexpr uint32_t Magic=0xC0391E1D;
constexpr uint8_t KeyMaterial[16]={0x46,0x3F,0xBB,0xBC,0x7B,0x6F,0x57,0xED,0x9B,0x41,0x7D,0x36,0xE3,0x26,0x12,0xBC};
[[noreturn]] void bad() {throw std::runtime_error("This is not a Death Stranding save.");}
uint32_t le32(const std::string& b,size_t at) {if(at+4>b.size())bad();uint32_t v;memcpy(&v,b.data()+at,4);return v;}
void put32(std::string& b,uint32_t v) {for(int k=0;k<4;++k)b+=char(v>>(8*k));}
uint64_t rotl(uint64_t x,int r) {return (x<<r)|(x>>(64-r));}
uint64_t fmix(uint64_t k) {k^=k>>33;k*=0xFF51AFD7ED558CCDull;k^=k>>33;k*=0xC4CEB9FE1A85EC53ull;k^=k>>33;return k;}
// MurmurHash3 x64-128 (seed 0x2A) over the block's four salt bytes followed by twelve fixed ones.
void keyFor(const char* salt,uint8_t key[16]) {
    uint8_t in[16];memcpy(in,salt,4);memcpy(in+4,KeyMaterial+4,12);
    uint64_t k0,k1;memcpy(&k0,in,8);memcpy(&k1,in+8,8);
    const uint64_t s=0x2A,c1=0x87C37B91114253D5ull,c2=0x4CF5AD432745937Full;
    k0*=c1;k0=rotl(k0,31);k0*=c2;k0^=s;k0=rotl(k0,27);k0+=s;k0=k0*5+0x52DCE729;
    k1*=c2;k1=rotl(k1,33);k1*=c1;k1^=s;k1=rotl(k1,31);k1+=k0;k1=k1*5+0x38495AB5;
    k0^=16;k1^=16;k0+=k1;k1+=k0;k0=fmix(k0);k1=fmix(k1);k0+=k1;k1+=k0;
    memcpy(key,&k0,8);memcpy(key+8,&k1,8);
}
// Toggles the encryption of a block that starts at the magic. The tail shorter than eight bytes restarts the key.
void cipher(std::string& block) {
    if(block.size()<12)bad();
    uint8_t key[16];keyFor(block.data()+4,key);
    size_t n=block.size()-8,full=n-n%8;
    for(size_t i=0;i<full;++i)block[8+i]^=char(key[i%16]);
    for(size_t i=full;i<n;++i)block[8+i]^=char(key[i-full]);
}
struct Parts { bool checkpoint=false;std::string meta,picture,block; };   // block decrypted
Parts parse(const std::string& file) {
    Parts p;
    if(file.size()>MaxSave||file.size()<16)bad();
    if(le32(file,0)==Magic)p.block=file;
    else {
        size_t size[3];
        for(int i=0;i<3;++i) {
            if(file.size()<24)bad();
            size_t v=0,k=0;for(;k<8&&file[i*8+k];++k){char c=file[i*8+k];if(c<'0'||c>'9')bad();v=v*10+size_t(c-'0');}
            if(!k)bad();
            // What follows the terminator is not read: the game leaves whatever was in its buffer there (live: "503",
            // a zero, then four bytes that were not zero in two saves of five).
#ifdef MUTANT_SAVE_PADDING
            for(;k<8;++k)if(file[i*8+k])bad();
#endif
            size[i]=v;
        }
        if(size[0]>file.size()||size[1]>file.size()||24+size[0]+size[1]+12>file.size())bad();
        p.checkpoint=true;p.meta=file.substr(24,size[0]);p.picture=file.substr(24+size[0],size[1]);p.block=file.substr(24+size[0]+size[1]);
        if(le32(p.block,0)!=Magic||size[2]!=p.block.size())bad();
    }
    cipher(p.block);
    uint32_t n=le32(p.block,8);if(n>255||12+size_t(n)>p.block.size())bad();
    for(uint32_t i=0;i<n;++i){unsigned char c=p.block[12+i];if(c<0x21||c>0x7e)bad();}
    return p;
}
std::string seal(Parts p) {
    cipher(p.block);
    if(!p.checkpoint)return p.block;
    std::string out;
    for(size_t v:{p.meta.size(),p.picture.size(),p.block.size()}){auto t=std::to_string(v);if(t.size()>8)bad();t.resize(8,'\0');out+=t;}
    return out+p.meta+p.picture+p.block;
}
std::string lower(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=char(c+32);return s;}
// The header's "Title = ..." line: percent-encoded UTF-8. Control characters go, and a cut never splits a character.
std::string titleOf(const std::string& meta) {
    auto at=meta.find("Title");
    while(at!=std::string::npos&&at&&meta[at-1]!='\n')at=meta.find("Title",at+1);
    if(at==std::string::npos)return {};
    auto eq=meta.find('=',at),end=meta.find_first_of("\r\n",at);
    if(eq==std::string::npos||(end!=std::string::npos&&eq>end))return {};
    std::string raw=meta.substr(eq+1,end==std::string::npos?std::string::npos:end-eq-1),out;
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(size_t i=0;i<raw.size();++i) {
        char c=raw[i];
        if(c=='%'&&i+2<raw.size()&&hex(raw[i+1])>=0&&hex(raw[i+2])>=0){c=char(hex(raw[i+1])*16+hex(raw[i+2]));i+=2;}
        if(static_cast<unsigned char>(c)<0x20||c==0x7f)continue;
        out+=c;
    }
    auto first=out.find_first_not_of(' ');out=first==std::string::npos?"":out.substr(first);
    if(out.size()>TitleMax){size_t n=TitleMax;while(n&&(static_cast<unsigned char>(out[n])&0xC0)==0x80)--n;out.resize(n);}
    return out;
}
std::string readFile(const fs::path& p) {
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read a save file.");
    std::string bytes;char buffer[65536];
    while(f.read(buffer,sizeof(buffer))||f.gcount()){bytes.append(buffer,size_t(f.gcount()));if(bytes.size()>MaxSave)bad();}
    return bytes;
}
bool linked(const fs::path& p) {
#ifdef MUTANT_SAVE_LINKS
    (void)p;return false;
#else
    std::error_code ec;auto s=fs::symlink_status(p,ec);
    return fs::is_symlink(s)||(fs::is_regular_file(s)&&fs::hard_link_count(p,ec)!=1);
#endif
}
fs::path childNoCase(const fs::path& parent,const std::string& name) {
    std::error_code ec;
    for(fs::directory_iterator it(parent,ec),end;!ec&&it!=end;it.increment(ec))if(lower(it->path().filename().string())==lower(name))return it->path();
    return parent/lower(name);
}
// The checkpoint folders of an account folder, as they are named on disk.
std::vector<fs::path> checkpoints(const fs::path& account) {
    std::vector<fs::path> out;std::error_code ec;
    for(fs::directory_iterator it(account,ec),end;!ec&&it!=end;it.increment(ec))if(saveFolder(it->path().filename().string()))out.push_back(it->path());
    std::sort(out.begin(),out.end());return out;
}
void moveAside(const fs::path& account,const fs::path& aside,int& moved) {
    for(auto& folder:checkpoints(account)) {
        if(linked(folder))throw std::runtime_error("Refusing linked save folders.");
        fs::create_directories(aside);
        fs::rename(folder,aside/folder.filename());++moved;
    }
}
constexpr char Wire[4]={'D','S','S','1'};
enum : uint8_t {Query=1,Offer,Get,Chunk};
std::string head(uint8_t kind,uint32_t secret,uint32_t nonce) {std::string b(Wire,4);b+=char(kind);put32(b,secret);put32(b,nonce);return b;}
constexpr size_t Head=13;
bool ours(const std::string& d,uint8_t kind,uint32_t secret,uint32_t nonce) {
#ifdef MUTANT_SAVE_SECRET
    (void)secret;
    return d.size()>=Head&&!memcmp(d.data(),Wire,4)&&uint8_t(d[4])==kind&&le32(d,9)==nonce;
#else
    return secret&&nonce&&d.size()>=Head&&!memcmp(d.data(),Wire,4)&&uint8_t(d[4])==kind&&le32(d,5)==secret&&le32(d,9)==nonce;
#endif
}
}

uint32_t crc32(const std::string& bytes) {
    static uint32_t table[256];static bool made=false;
    if(!made){for(uint32_t i=0;i<256;++i){uint32_t c=i;for(int k=0;k<8;++k)c=c&1?0xEDB88320u^(c>>1):c>>1;table[i]=c;}made=true;}
    uint32_t c=~0u;for(unsigned char b:bytes)c=table[(c^b)&255]^(c>>8);return ~c;
}
bool saveFolder(const std::string& name) {
    auto n=lower(name);size_t digits=0;
    for(auto prefix:{"autosave","manualsave","quicksave"}) {
        size_t len=strlen(prefix);if(n.compare(0,len,prefix))continue;
        for(size_t i=len;i<n.size();++i){if(n[i]<'0'||n[i]>'9')return false;++digits;}
        return digits>=1&&digits<=4;
    }
    return false;
}
Info inspect(const std::string& file) {auto p=parse(file);return {p.checkpoint,p.block.substr(12,le32(p.block,8)),titleOf(p.meta)};}
std::string resign(const std::string& file,const std::string& user) {
    if(user.empty()||user.size()>255)bad();
    auto p=parse(file);uint32_t n=le32(p.block,8);
#ifdef MUTANT_SAVE_RESIGN
    (void)n;
#else
    std::string block=p.block.substr(0,8);put32(block,uint32_t(user.size()));block+=user;block+=p.block.substr(12+n);p.block=block;
#endif
    return seal(std::move(p));
}
std::string userForAccount(uint32_t account) {return "steam_"+std::to_string(76561197960265728ull+account);}

fs::path gameFolder(const fs::path& local) {return childNoCase(childNoCase(local,"KojimaProductions"),"DeathStrandingDC");}
std::vector<uint32_t> accounts(const fs::path& game) {
    std::vector<uint32_t> out;std::error_code ec;
    for(fs::directory_iterator it(game,ec),end;!ec&&it!=end;it.increment(ec)) {
        auto name=it->path().filename().string();
        if(name.empty()||name.size()>10||name.find_first_not_of("0123456789")!=std::string::npos||!it->is_directory(ec))continue;
        auto v=std::stoull(name);if(v&&v<=0xffffffffull)out.push_back(uint32_t(v));
    }
    std::sort(out.begin(),out.end());return out;
}
std::optional<Checkpoint> newest(const fs::path& account) {
    std::vector<std::pair<fs::file_time_type,fs::path>> found;std::error_code ec;
    for(auto& folder:checkpoints(account)) {
        auto file=folder/"checkpoint.dat";
        if(linked(folder)||linked(file)||!fs::is_regular_file(file,ec))continue;
        auto t=fs::last_write_time(file,ec);if(!ec)found.emplace_back(t,file);
    }
    std::sort(found.begin(),found.end(),[](auto& x,auto& y){return x.first>y.first;});
    for(auto& f:found) {   // a file the game would refuse is not worth sending: the next newest
        try {
            Checkpoint c;c.folder=f.second.parent_path().filename().string();c.bytes=readFile(f.second);
            auto info=inspect(c.bytes);if(!info.checkpoint)continue;
            c.title=info.title;return c;
        } catch(const std::exception&) {}
    }
    return std::nullopt;
}
std::optional<Checkpoint> named(const fs::path& account,const std::string& folder,std::string* why) {
#ifdef MUTANT_SAVE_NAMED_OTHER
    auto fail=[&](const char* text){if(why)*why=text;return newest(account);};
#else
    auto fail=[&](const char* text)->std::optional<Checkpoint>{if(why)*why=text;return std::nullopt;};
#endif
    if(!saveFolder(folder))return fail("not a save folder");
    auto dir=account/folder,file=dir/"checkpoint.dat";std::error_code ec;
    if(linked(dir)||linked(file)||!fs::is_regular_file(file,ec))return fail("no checkpoint file there");
    try {
        Checkpoint c;c.folder=folder;c.bytes=readFile(file);
        auto info=inspect(c.bytes);if(!info.checkpoint)return fail("the file is not a checkpoint");
        c.title=info.title;return c;
    } catch(const std::exception& e) {if(why)*why=e.what();return std::nullopt;}
}
int install(const fs::path& account,uint32_t accountId,const std::string& hostFile,const fs::path& aside) {
    if(!accountId)throw std::runtime_error("Your Steam account is not known yet. Start Steam, or play solo once, then join again.");
    if(!inspect(hostFile).checkpoint)bad();
    auto mine=resign(hostFile,userForAccount(accountId));
    if(linked(account))throw std::runtime_error("Refusing linked save folders.");
    auto target=account/InstallFolder/"checkpoint.dat";
    auto present=checkpoints(account);std::error_code ec;
    if(present.size()==1&&lower(present[0].filename().string())==InstallFolder&&!linked(present[0])&&fs::is_regular_file(present[0]/"checkpoint.dat",ec)
       &&readFile(present[0]/"checkpoint.dat")==mine)return -1;
    int moved=0;
#ifndef MUTANT_SAVE_KEEP
    moveAside(account,aside,moved);
#else
    (void)aside;for(auto& folder:present){fs::remove_all(folder);++moved;}
#endif
    atomicWrite(target,mine);
    return moved;
}
int importOrdinary(const fs::path& from,const fs::path& to,const fs::path& aside) {
    std::error_code ec;
    if(!fs::is_directory(from,ec))throw std::runtime_error("No ordinary Death Stranding saves were found for this account.");
    if(fs::exists(to,ec)&&fs::equivalent(from,to,ec))throw std::runtime_error("The co-op saves are already these saves.");
    std::vector<std::pair<std::string,std::string>> files;   // read everything first: nothing changes when a file is bad
    for(auto& folder:checkpoints(from)) {
        auto file=folder/"checkpoint.dat";
        if(fs::is_symlink(fs::symlink_status(folder,ec))||fs::is_symlink(fs::symlink_status(file,ec))||!fs::is_regular_file(file,ec))continue;
        try {auto bytes=readFile(file);if(inspect(bytes).checkpoint)files.emplace_back(lower(folder.filename().string()),std::move(bytes));}
        catch(const std::exception&) {}
    }
    if(files.empty())throw std::runtime_error("No ordinary Death Stranding saves were found for this account.");
    if(linked(to))throw std::runtime_error("Refusing linked save folders.");
    int moved=0;moveAside(to,aside,moved);
    for(auto& f:files)atomicWrite(to/f.first/"checkpoint.dat",f.second);
    auto profile=childNoCase(from,"profile"),mine=childNoCase(to,"profile");
    if(fs::is_directory(profile,ec)&&!fs::exists(mine,ec))
        for(fs::directory_iterator it(profile,ec),end;!ec&&it!=end;it.increment(ec)) {
            std::error_code e;if(!it->is_regular_file(e)||it->is_symlink(e)||it->file_size(e)>MaxSave)continue;
            atomicWrite(mine/lower(it->path().filename().string()),readFile(it->path()));
        }
    return int(files.size());
}

void Sender::offer(const std::optional<Checkpoint>& c) {
    file.clear();title.clear();sum=0;
    if(!c||c->bytes.empty()||c->bytes.size()>MaxSave)return;
    file=c->bytes;title=c->title.substr(0,TitleMax);sum=crc32(file);
}
std::string Sender::answer(const std::string& d,uint32_t secret,uint32_t nonce) const {
    if(ours(d,Query,secret,nonce)&&d.size()==Head) {
        auto b=head(Offer,secret,nonce);put32(b,uint32_t(file.size()));put32(b,sum);b+=char(title.size());b+=title;return b;
    }
    if(ours(d,Get,secret,nonce)&&d.size()==Head+8&&!file.empty()&&le32(d,Head)==sum) {
        uint32_t index=le32(d,Head+4);size_t at=size_t(index)*ChunkBytes;
        if(at>=file.size())return {};
        auto b=head(Chunk,secret,nonce);put32(b,sum);put32(b,index);b.append(file,at,ChunkBytes);return b;
    }
    return {};
}
std::vector<std::string> Receiver::poll(uint32_t secret,uint32_t nonce,bool wanted,int budget) {
    std::vector<std::string> out;
    if(!secret||!nonce)return out;
    if(!offered){out.push_back(head(Query,secret,nonce));return out;}
    if(!wanted||done||!size)return out;
    for(size_t looked=0;looked<have.size()&&int(out.size())<budget;++looked) {
        uint32_t index=cursor;cursor=uint32_t((cursor+1)%have.size());
        if(have[index])continue;
        auto b=head(Get,secret,nonce);put32(b,sum);put32(b,index);out.push_back(b);
    }
    return out;
}
void Receiver::take(const std::string& d,uint32_t secret,uint32_t nonce) {
    if(ours(d,Offer,secret,nonce)&&d.size()>=Head+9) {
        uint32_t n=le32(d,Head),crc=le32(d,Head+4);size_t len=uint8_t(d[Head+8]);
        if(n>MaxSave||len>TitleMax||d.size()!=Head+9+len)return;
        if(offered&&n==size&&crc==sum)return;
        offered=true;done=false;size=n;sum=crc;title=d.substr(Head+9,len);
        for(auto& c:title)if(static_cast<unsigned char>(c)<0x20||c==0x7f)c=' ';
        got=0;cursor=0;file.assign(n,'\0');have.assign((size_t(n)+ChunkBytes-1)/ChunkBytes,0);
        return;
    }
    if(!offered||done||!size||!ours(d,Chunk,secret,nonce)||d.size()<Head+8||le32(d,Head)!=sum)return;
    uint32_t index=le32(d,Head+4);
    if(index>=have.size()||have[index])return;
    size_t at=size_t(index)*ChunkBytes,want=std::min(ChunkBytes,size_t(size)-at);
    if(d.size()!=Head+8+want)return;
    memcpy(&file[at],d.data()+Head+8,want);have[index]=1;
    if(++got<have.size())return;
    bool good=crc32(file)==sum;(void)good;
#ifndef MUTANT_SAVE_CHECKSUM
    if(good){try{good=inspect(file).checkpoint;}catch(...){good=false;}}
    if(!good){got=0;std::fill(have.begin(),have.end(),0);return;}   // damaged on the way: fetch it again
#endif
    done=true;
}
}
