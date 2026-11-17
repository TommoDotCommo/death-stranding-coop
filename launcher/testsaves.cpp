#include "testsaves.h"
#include "saves.h"
#include "core.h"
#include "../world/json.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace fs=std::filesystem;
namespace launcher::testsaves {
namespace {
[[noreturn]] void damaged(const std::string& why) {throw PackError("The test save pack cannot be used: "+why+". Download it again from the release page.");}
uint32_t rotr(uint32_t x,int n) {return (x>>n)|(x<<(32-n));}
std::string lower(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=char(c+32);return s;}
// Text for the window: control characters become spaces (a description keeps its line breaks), at most `max` bytes,
// never cutting a UTF-8 character.
std::string clean(std::string s,size_t max,bool lines) {
    for(auto& c:s)if((static_cast<unsigned char>(c)<0x20&&!(lines&&c=='\n'))||c==0x7f)c=' ';
    if(s.size()>max){size_t n=max;while(n&&(static_cast<unsigned char>(s[n])&0xC0)==0x80)--n;s.resize(n);}
    return s;
}
bool idValid(const std::string& id) {
    if(id.empty()||id.size()>48)return false;
    for(size_t i=0;i<id.size();++i){char c=id[i];bool ok=(c>='a'&&c<='z')||(c>='0'&&c<='9')||(i&&(c=='-'||c=='.'||c=='_'));if(!ok)return false;}
    return true;
}
bool hexValid(const std::string& h) {return h.size()==64&&h.find_first_not_of("0123456789abcdef")==std::string::npos;}
// A path the pack may hold: <set id>/<save folder>/checkpoint.dat, nothing else.
bool checkpointPath(const std::string& path,const std::string& id) {
#ifdef MUTANT_TESTSAVES_PATH
    (void)id;return !path.empty();
#else
    auto a=path.find('/'),b=a==std::string::npos?a:path.find('/',a+1);
    if(b==std::string::npos||path.substr(0,a)!=id)return false;
    auto folder=path.substr(a+1,b-a-1);
    return saves::saveFolder(folder)&&folder==lower(folder)&&path.substr(b+1)=="checkpoint.dat";
#endif
}
uint32_t le(const std::string& b,size_t at,int n) {uint32_t v=0;for(int k=n-1;k>=0;--k)v=(v<<8)|uint8_t(b[at+size_t(k)]);return v;}
std::string readAt(const fs::path& file,uint64_t at,uint64_t size) {
    std::ifstream f(file,std::ios::binary);if(!f)damaged("it cannot be read");
    f.seekg(std::streamoff(at));std::string out(size_t(size),'\0');
    if(size&&!f.read(out.data(),std::streamsize(size)))damaged("it is cut short");
    return out;
}
// Names inside a zip or under the folder: forward slashes, no empty or dot parts, no drive or root.
bool nameSafe(std::string n) {
    if(!n.empty()&&n.back()=='/')n.pop_back();   // "dir/": a folder entry
    if(n.empty()||n.size()>512||n[0]=='/'||n.find('\\')!=std::string::npos||n.find(':')!=std::string::npos)return false;
    for(size_t at=0;;) {
        auto next=n.find('/',at);auto part=n.substr(at,next==std::string::npos?std::string::npos:next-at);
        if(part.empty()||part=="."||part=="..")return false;
        if(next==std::string::npos)return true;
        at=next+1;
    }
}
}

std::string sha256(const std::string& bytes) {
    static const uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string m=bytes;uint64_t bits=uint64_t(bytes.size())*8;m+=char(0x80);while(m.size()%64!=56)m+='\0';
    for(int k=7;k>=0;--k)m+=char(bits>>(8*k));
    for(size_t at=0;at<m.size();at+=64) {
        uint32_t w[64];
        for(int i=0;i<16;++i)w[i]=uint32_t(uint8_t(m[at+4*i]))<<24|uint32_t(uint8_t(m[at+4*i+1]))<<16|uint32_t(uint8_t(m[at+4*i+2]))<<8|uint8_t(m[at+4*i+3]);
        for(int i=16;i<64;++i){uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];
        for(int i=0;i<64;++i){
            uint32_t t1=x+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i],t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
            x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;
    }
    std::string out;for(auto v:h)for(int k=28;k>=0;k-=4)out+="0123456789abcdef"[(v>>k)&15];
    return out;
}

void parseManifest(const std::string& text,std::string& version,std::vector<Set>& sets) {
    json::Value v;
    try{v=json::parseFile(text,ManifestMax);}catch(const std::exception&){damaged("its list (test-saves.json) is not readable");}
    if(!v.isObject()||!v.get("schema").isInt()||v.get("schema").integer()!=1)damaged("its list is of another version; this launcher reads version 1");
    version=v.get("version").string();
    if(version.empty()||version.size()>64||clean(version,64,false)!=version)damaged("its list names no version");
    auto& list=v.get("sets").items();
    if(list.empty()||list.size()>SetsMax)damaged("it lists no test saves");
    sets.clear();
    for(auto& s:list) {
        Set set;set.id=s.get("id").string();
        if(!s.isObject()||!idValid(set.id))damaged("a test save has no usable id");
        for(auto& other:sets)if(other.id==set.id)damaged("two test saves are both called "+set.id);
        set.title=clean(s.get("title").string(),saves::TitleMax,false);
        if(set.title.find_first_not_of(' ')==std::string::npos)damaged("test save "+set.id+" has no title");
        set.description=clean(s.get("description").string(),DescriptionMax,true);
        set.playTime=clean(s.get("play_time").string(),32,false);
        set.source=clean(s.get("source").string(),300,false);
        auto& e=s.get("episode");
        if(!e.isNull()){if(!e.isInt()||e.integer()<0||e.integer()>99)damaged("test save "+set.id+" has an impossible episode");set.episode=int(e.integer());}
        auto& files=s.get("files").items();
        if(files.empty()||files.size()>FilesMax)damaged("test save "+set.id+" has no checkpoints");
        for(auto& f:files) {
            File file;file.path=f.get("path").string();file.sha256=f.get("sha256").string();
            auto size=f.get("size");
            if(!f.isObject()||!nameSafe(file.path)||!checkpointPath(file.path,set.id))damaged("test save "+set.id+" holds something that is not a checkpoint ("+clean(file.path,80,false)+")");
            if(!hexValid(file.sha256)||!size.isInt()||size.integer()<1||uint64_t(size.integer())>saves::MaxSave)damaged("test save "+set.id+" lists a file without its size and SHA-256");
            file.size=uint64_t(size.integer());
            for(auto& other:set.files)if(other.path==file.path)damaged("test save "+set.id+" lists one checkpoint twice");
            set.files.push_back(file);
        }
        sets.push_back(std::move(set));
    }
}

Pack Pack::open(const fs::path& where) {
    Pack p;p.where=where;std::error_code ec;std::string manifest;
    std::vector<std::string> present;   // every file the pack holds, by its name inside it
    if(fs::is_directory(where,ec)) {
        if(!fs::is_regular_file(where/Manifest,ec))damaged("test-saves.json is missing");
        for(fs::recursive_directory_iterator it(where,fs::directory_options::none,ec),end;!ec&&it!=end;it.increment(ec)) {
            auto st=it->symlink_status(ec);
            if(fs::is_symlink(st))damaged("it holds a link");
            if(fs::is_directory(st))continue;
            auto name=it->path().lexically_relative(where).generic_string();
            if(!fs::is_regular_file(st)||!nameSafe(name))damaged("it holds something that is not a file");
            present.push_back(name);
        }
        if(ec)damaged("its folder cannot be read");
        manifest=p.raw(Manifest);
    } else {
        p.zip=true;
        auto total=fs::file_size(where,ec);if(ec)damaged("it is missing");
        if(total<22)damaged("it is not a zip");
        uint64_t tail=std::min<uint64_t>(total,22+65535);auto end=readAt(where,total-tail,tail);
        size_t at=std::string::npos;
        for(size_t i=end.size()-22+1;i-->0;)if(le(end,i,4)==0x06054b50&&i+22+le(end,i+20,2)==end.size()){at=i;break;}
        if(at==std::string::npos)damaged("it is not a zip");
        uint32_t count=le(end,at+10,2),cdSize=le(end,at+12,4),cdAt=le(end,at+16,4);
        if(le(end,at+4,2)||le(end,at+6,2)||count!=le(end,at+8,2)||count==0xFFFF||cdSize==0xFFFFFFFF||cdAt==0xFFFFFFFF)damaged("it is a zip this launcher cannot read");
        if(count>4096||uint64_t(cdAt)+cdSize>total)damaged("its zip directory is damaged");
        auto cd=readAt(where,cdAt,cdSize);size_t c=0;
        for(uint32_t n=0;n<count;++n) {
            if(c+46>cd.size()||le(cd,c,4)!=0x02014b50)damaged("its zip directory is damaged");
            uint32_t flags=le(cd,c+8,2),method=le(cd,c+10,2),crc=le(cd,c+16,4),packed=le(cd,c+20,4),size=le(cd,c+24,4),nameLen=le(cd,c+28,2),extra=le(cd,c+30,2),comment=le(cd,c+32,2),local=le(cd,c+42,4);
            if(c+46+nameLen+extra+comment>cd.size())damaged("its zip directory is damaged");
            std::string name=cd.substr(c+46,nameLen);c+=46+nameLen+extra+comment;
            if(!nameSafe(name))damaged("it holds a file with an unsafe name");
            if(name.back()=='/')continue;   // a folder entry
            if(flags&1)damaged("it is encrypted");
            if(method!=0||packed!=size)damaged("it was re-packed with compression; use the release's zip as downloaded, or extract it into the test-saves folder");
            if(size==0xFFFFFFFF||uint64_t(local)+30>cdAt)damaged("its zip directory is damaged");
            auto head=readAt(where,local,30);
            if(le(head,0,4)!=0x04034b50)damaged("a file in it is damaged");
            uint64_t data=uint64_t(local)+30+le(head,26,2)+le(head,28,2);
            if(readAt(where,uint64_t(local)+30,le(head,26,2))!=name||data+size>cdAt)damaged("a file in it is damaged");
            if(p.entries.count(name))damaged("it holds one file twice");
            p.entries[name]={data,size,crc};present.push_back(name);
        }
        auto m=p.entries.find(Manifest);if(m==p.entries.end())damaged("test-saves.json is missing");
        if(m->second.size>ManifestMax)damaged("its list is too large");
        manifest=p.raw(Manifest);
        if(saves::crc32(manifest)!=m->second.crc)damaged("its list is damaged");
    }
    parseManifest(manifest,p.version,p.sets);
    std::map<std::string,uint64_t> listed;for(auto& s:p.sets)for(auto& f:s.files)listed[f.path]=f.size;
    for(auto& name:present) {
        if(name==Manifest)continue;
#ifndef MUTANT_TESTSAVES_EXTRA
        if(!listed.count(name))damaged("it holds a file its list does not name ("+clean(name,80,false)+")");
#endif
        listed.erase(name);
    }
    if(!listed.empty())damaged("a checkpoint its list names is missing ("+clean(listed.begin()->first,80,false)+")");
    return p;
}
std::string Pack::raw(const std::string& path) const {
    if(!zip) {
        std::error_code ec;auto file=where/fs::path(path);auto size=fs::file_size(file,ec);
        if(ec||size>std::max<uint64_t>(saves::MaxSave,ManifestMax))damaged(path+" cannot be read");
        return readAt(file,0,size);
    }
    auto e=entries.find(path);if(e==entries.end())damaged(path+" is missing");
    return readAt(where,e->second.offset,e->second.size);
}
const Set* Pack::find(const std::string& id) const {for(auto& s:sets)if(s.id==id)return &s;return nullptr;}
std::string Pack::read(const File& f) const {
    auto bytes=raw(f.path);
#ifndef MUTANT_TESTSAVES_HASH
    if(bytes.size()!=f.size||sha256(bytes)!=f.sha256)damaged(f.path+" does not match its list");
#endif
    return bytes;
}

// The set becomes the account's co-op saves. Here and not in saves.cpp, which is part of the mod's wire identity
// (mod/build_id.py): the same rules as saves::install (checkpoints only, re-signed for the account, the saves that
// were there moved aside and never deleted, linked save folders refused), for several checkpoints.
namespace {
bool linked(const fs::path& p) {
    std::error_code ec;auto s=fs::symlink_status(p,ec);
    return fs::is_symlink(s)||(fs::is_regular_file(s)&&fs::hard_link_count(p,ec)!=1);
}
int installSet(const fs::path& account,uint32_t accountId,const std::vector<std::pair<std::string,std::string>>& files,const fs::path& aside) {
    if(!accountId)throw std::runtime_error("Your Steam account is not known yet. Start Steam, then try again.");
    std::vector<std::pair<std::string,std::string>> mine;   // everything re-signed first: nothing changes when one is bad
    for(auto& f:files) {
        if(!saves::saveFolder(f.first))throw std::runtime_error("A test save names a folder that is not a save folder.");
        for(auto& m:mine)if(m.first==lower(f.first))throw std::runtime_error("A test save names one folder twice.");
        if(!saves::inspect(f.second).checkpoint)throw std::runtime_error("This is not a Death Stranding save.");
#ifdef MUTANT_TESTSAVES_RESIGN
        mine.emplace_back(lower(f.first),f.second);
#else
        mine.emplace_back(lower(f.first),saves::resign(f.second,saves::userForAccount(accountId)));
#endif
    }
    if(mine.empty()||linked(account))throw std::runtime_error("Refusing linked save folders.");
    std::vector<fs::path> present;std::error_code ec;
    for(fs::directory_iterator it(account,ec),end;!ec&&it!=end;it.increment(ec))if(saves::saveFolder(it->path().filename().string()))present.push_back(it->path());
    std::sort(present.begin(),present.end());
    for(auto& folder:present)if(linked(folder))throw std::runtime_error("Refusing linked save folders.");
    int moved=0;
    for(auto& folder:present) {
#ifndef MUTANT_TESTSAVES_BACKUP
        fs::create_directories(aside);fs::rename(folder,aside/folder.filename());
#else
        (void)aside;fs::remove_all(folder);
#endif
        ++moved;
    }
    for(auto& m:mine)atomicWrite(account/m.first/"checkpoint.dat",m.second);
    return moved;
}
}

bool versionBefore(const std::string& a,const std::string& b) {
    size_t i=0,j=0;
    while(i<a.size()&&j<b.size()) {
        bool da=a[i]>='0'&&a[i]<='9',db=b[j]>='0'&&b[j]<='9';
        if(da&&db) {
            size_t ie=i,je=j;while(ie<a.size()&&a[ie]>='0'&&a[ie]<='9')++ie;while(je<b.size()&&b[je]>='0'&&b[je]<='9')++je;
            auto x=a.substr(i,ie-i),y=b.substr(j,je-j);x.erase(0,std::min(x.find_first_not_of('0'),x.size()));y.erase(0,std::min(y.find_first_not_of('0'),y.size()));
            if(x.size()!=y.size())return x.size()<y.size();
            if(x!=y)return x<y;
            i=ie;j=je;continue;
        }
        if(a[i]!=b[j])return a[i]<b[j];
        ++i;++j;
    }
    return a.size()-i<b.size()-j;
}
std::optional<fs::path> find(const fs::path& launcherFolder) {
    std::error_code ec;auto folder=launcherFolder/FolderName;
    if(fs::is_regular_file(folder/Manifest,ec))return folder;
    std::optional<fs::path> best;std::string bestVersion;const std::string prefix=ZipPrefix;
    for(fs::directory_iterator it(launcherFolder,ec),end;!ec&&it!=end;it.increment(ec)) {
        auto name=it->path().filename().string();
        if(name.size()<=prefix.size()+4||lower(name.substr(0,prefix.size()))!=prefix||lower(name.substr(name.size()-4))!=".zip"||!it->is_regular_file(ec))continue;
        auto version=name.substr(prefix.size(),name.size()-prefix.size()-4);
        if(!best||versionBefore(bestVersion,version)){best=it->path();bestVersion=version;}
    }
    return best;
}

fs::path coopAccount(const fs::path& data,const fs::path& local,uint32_t accountId) {
#ifdef MUTANT_TESTSAVES_ORDINARY
    (void)data;return saves::gameFolder(local)/std::to_string(accountId);
#else
    (void)local;return saves::gameFolder(data/"redirect-data"/"local")/std::to_string(accountId);
#endif
}

int use(const Pack& pack,const Set& set,const fs::path& account,uint32_t accountId,const fs::path& aside) {
    std::vector<std::pair<std::string,std::string>> files;   // the first one listed (Continue's) is written last
    for(size_t i=1;i<=set.files.size();++i) {
        auto& f=set.files[i%set.files.size()];
        auto a=f.path.find('/'),b=f.path.find('/',a+1);
        auto bytes=pack.read(f);bool checkpoint=false;
        try{checkpoint=saves::inspect(bytes).checkpoint;}catch(const std::exception&){}
        if(!checkpoint)damaged(f.path+" is not a checkpoint");
        files.emplace_back(f.path.substr(a+1,b-a-1),std::move(bytes));
    }
    return installSet(account,accountId,files,aside);
}

Outcome listCommand(const std::optional<fs::path>& where) {
    Outcome o;
    if(!where){o.code=ExitPack;o.message="No test save pack found. Get it from the release page ("+std::string(ReleasePage)+") and put the zip beside the launcher.";return o;}
    try {
        auto pack=Pack::open(*where);
        for(auto& s:pack.sets)o.message+=s.id+"\t"+s.title+"\n";
        o.code=ExitOk;
    }catch(const PackError& e){o.code=ExitPack;o.message=e.what();}
    catch(const std::exception& e){o.code=ExitFailed;o.message=e.what();}
    return o;
}
Outcome useCommand(const std::optional<fs::path>& where,const std::string& id,const fs::path& data,const fs::path& local,uint32_t accountId,bool allowed,long long now) {
    Outcome o;
    try {
        if(!where){o.code=ExitPack;o.message="No test save pack found. Get it from the release page ("+std::string(ReleasePage)+") and put the zip beside the launcher.";return o;}
        auto pack=Pack::open(*where);
        auto set=pack.find(id);if(!set){o.message="The test save pack has no test save called \""+clean(id,48,false)+"\".";return o;}
#ifndef MUTANT_TESTSAVES_RUNNING
        if(!allowed){o.code=ExitRunning;o.message="Leave the lobby and close the game before using a test save.";return o;}
#else
        (void)allowed;
#endif
        if(!accountId){o.message="Your Steam account is not known yet. Start Steam, then try again.";return o;}
        auto base=data/"backups"/("before-test-save-"+std::to_string(now));o.aside=base;std::error_code ec;
        for(int n=2;fs::exists(o.aside,ec);++n){o.aside=base;o.aside+="-"+std::to_string(n);}
        o.moved=use(pack,*set,coopAccount(data,local,accountId),accountId,o.aside);
        o.code=ExitOk;
        o.message="Continue in the game loads "+set->title+"."+(o.moved>0?" Your "+std::to_string(o.moved)+" earlier co-op save(s) are in backups\\"+o.aside.filename().string()+".":"");
    }catch(const PackError& e){o.code=ExitPack;o.message=e.what();}
    catch(const std::exception& e){o.code=ExitFailed;o.message=std::string("The test save was not used: ")+e.what();}
    return o;
}
}
