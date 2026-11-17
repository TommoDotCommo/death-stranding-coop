#include "install.h"
#include "../dist/sha256.h"
#include "../world/json.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
namespace launcher::install {
namespace {
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string slurp(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw Error("Cannot read "+p.u8string()+".");return std::string((std::istreambuf_iterator<char>(f)),{});}
bool regular(const fs::path& p){std::error_code ec;return fs::is_regular_file(fs::symlink_status(p,ec));}
bool present(const fs::path& p){std::error_code ec;return fs::exists(fs::symlink_status(p,ec));}
fs::path receiptOf(const fs::path& target){auto r=target;r+=ReceiptSuffix;return r;}
fs::path with(const fs::path& target,const char* suffix){auto r=target;r+=suffix;return r;}
bool hex64(const std::string& s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
// A relative package path that stays below its root: plain characters, '/' between parts, no empty, "." or ".." part.
bool safeRelative(const std::string& rel) {
#ifdef MUTANT_INSTALL_MANIFEST_ESCAPE
    return !rel.empty();
#endif
    if(rel.empty()||rel.size()>200)return false;
    size_t start=0;
    for(size_t i=0;i<=rel.size();++i) {
        if(i==rel.size()||rel[i]=='/'){auto part=rel.substr(start,i-start);if(part.empty()||part=="."||part==".."||part.back()=='.')return false;start=i+1;continue;}
        char c=rel[i];if(!(std::isalnum(static_cast<unsigned char>(c))||c=='.'||c=='_'||c=='-'))return false;
    }
    return true;
}
fs::path relative(const std::string& rel){fs::path p;size_t start=0;for(size_t i=0;i<=rel.size();++i)if(i==rel.size()||rel[i]=='/'){p/=rel.substr(start,i-start);start=i+1;}return p;}
// Refuse a link anywhere between `root` and the target: Setup never writes through a junction or symbolic link.
void unlinked(const fs::path& root,const fs::path& target) {
#ifdef MUTANT_INSTALL_FOLLOWS_LINKS
    (void)root;(void)target;return;
#endif
    for(auto p=target;!p.empty();p=p.parent_path()) {
#ifdef _WIN32
        auto a=GetFileAttributesW(p.c_str());
        if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw Error("Setup refuses a linked folder or file: "+p.u8string());
#else
        std::error_code ec;if(fs::is_symlink(fs::symlink_status(p,ec)))throw Error("Setup refuses a linked folder or file: "+p.u8string());
#endif
        if(p==root||p==p.parent_path())break;
    }
}
// Windows names ignore letter case: the game folder's AVRT.dll is our avrt.dll's place.
fs::path anyCase(const fs::path& folder,const fs::path& rel) {
    auto want=folder/rel;if(rel.has_parent_path())return want;
    std::error_code ec;auto name=lower(rel.u8string());
    for(fs::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec))if(lower(it->path().filename().u8string())==name)return it->path();
    return want;
}
bool hasMarker(const fs::path& p) {
    try{return slurp(p).find(ProxyMarker)!=std::string::npos;}catch(...){return false;}
}
std::string receiptText(const std::string& version,const std::string& installed,const std::string& pending) {
    json::Value r=json::Value::object();r.set("schema",1);r.set("id",Id);r.set("version",version);
    r.set("installed",installed.empty()?json::Value():json::Value(installed));r.set("pending",pending.empty()?json::Value():json::Value(pending));
    return r.dump()+"\n";
}
void replaceFile(const fs::path& from,const fs::path& to) {
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw Error("Windows could not replace "+to.u8string()+".");
#else
    fs::rename(from,to);
#endif
}
void writeText(const fs::path& to,const std::string& bytes) {
    auto temp=with(to,".tmp");
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f)throw Error("Cannot write "+temp.u8string()+".");f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();if(!f)throw Error("Cannot write "+temp.u8string()+".");}
    replaceFile(temp,to);
}
bool removeFile(const fs::path& p){std::error_code ec;return fs::remove(p,ec)&&!ec;}
}

std::string sha256File(const fs::path& p) {
    std::ifstream f(p,std::ios::binary);if(!f)throw Error("Cannot read "+p.u8string()+".");
    sha256_ctx c;sha256_init(&c);std::vector<char> b(1<<20);
    while(f){f.read(b.data(),std::streamsize(b.size()));auto n=f.gcount();if(n>0)sha256_update(&c,b.data(),size_t(n));}
    if(!f.eof())throw Error("Cannot read "+p.u8string()+".");
    char out[65];sha256_final_hex(&c,out);return out;
}

Manifest readManifest(const fs::path& package) {
    Manifest m;json::Value v;
    try{v=json::parse(slurp(package/ManifestName));}catch(const std::exception&){throw Error("The package has no readable coop-mod.json. Extract the whole package again.",Usage);}
    if(v.get("schema").integer()!=1||v.get("id").string()!=Id||!v.get("files").isArray())throw Error("coop-mod.json is not this mod's (schema 1, id "+std::string(Id)+").",Usage);
    m.id=Id;m.version=v.get("version").string();
    for(auto& b:v.get("game").get("builds").items())if(hex64(b.get("sha256").string()))m.builds.push_back(b.get("sha256").string());
    for(auto& f:v.get("files").items()) {
        Entry e{f.get("root").isNull()?"game":f.get("root").string(),f.get("path").string(),f.get("sha256").string(),uint64_t(f.get("size").integer())};
        if((e.root!="game"&&e.root!="data")||!safeRelative(e.path)||!hex64(e.sha256))throw Error("coop-mod.json lists an unsafe or unknown file: "+e.path,Usage);
        if(e.root=="game"&&e.path.find('/')!=std::string::npos)throw Error("coop-mod.json puts a file below the game folder's top: "+e.path,Usage);
        m.files.push_back(e);
    }
    if(m.version.empty()||m.files.empty())throw Error("coop-mod.json has no version or no files.",Usage);
    return m;
}

Receipt readReceipt(const fs::path& target) {
    Receipt r;auto p=receiptOf(target);if(!present(p))return r;
    r.present=true;
    try {
        auto v=json::parse(slurp(p));
        r.version=v.get("version").string();r.installed=v.get("installed").string();r.pending=v.get("pending").string();
        r.valid=v.get("schema").integer()==1&&v.get("id").string()==Id&&(r.installed.empty()||hex64(r.installed))&&(r.pending.empty()||hex64(r.pending))&&!(r.installed.empty()&&r.pending.empty());
    }catch(const std::exception&){r.valid=false;}
#ifdef MUTANT_INSTALL_RECEIPT_IGNORES_PENDING
    r.pending.clear();
#endif
    return r;
}

const char* fileName(File f) {
    switch(f){case File::Missing:return "missing";case File::Current:return "current";case File::Outdated:return "outdated";case File::Ours:return "ours";
        case File::Adoptable:return "adoptable";case File::Modified:return "modified";case File::Foreign:return "foreign";}
    return "unknown";
}
File classify(bool there,const std::string& observed,const Receipt& r,const std::string& expected,bool ours) {
    if(!there)return File::Missing;
#ifdef MUTANT_INSTALL_TRUSTS_EXISTENCE
    (void)observed;(void)expected;return r.present?File::Current:File::Adoptable;
#endif
    if(r.present) {
        if(!r.valid)return File::Modified;
        if(observed==expected)return File::Current;
        if(observed==r.installed||(!r.pending.empty()&&observed==r.pending))return File::Outdated;
        return File::Modified;
    }
    if(observed==expected)return File::Adoptable;
#ifdef MUTANT_INSTALL_OVERWRITE_FOREIGN
    (void)ours;return File::Ours;
#else
    return ours?File::Ours:File::Foreign;
#endif
}

const char* stateName(State s) {
    switch(s){case State::NotAGameFolder:return "not-a-game-folder";case State::UnsupportedBuild:return "unsupported-build";case State::NotInstalled:return "not-installed";
        case State::UpToDate:return "up-to-date";case State::UpdateAvailable:return "update-available";case State::NeedsRepair:return "needs-repair";
        case State::Conflict:return "conflict";case State::ByModManager:return "installed-by-mod-manager";}
    return "unknown";
}
const char* button(State s) {
    switch(s){case State::NotInstalled:return "Install";case State::UpdateAvailable:return "Update";case State::NeedsRepair:return "Repair";
        case State::UpToDate:case State::ByModManager:return "Up to date";default:return "Needs attention";}
}
State decide(const std::vector<File>& files,bool linked,bool versions) {
    auto any=[&](std::initializer_list<File> set){for(auto f:files)for(auto s:set)if(f==s)return true;return false;};
    if(any({File::Modified,File::Foreign}))return State::Conflict;
    if(std::all_of(files.begin(),files.end(),[](File f){return f==File::Missing;}))return State::NotInstalled;
    if(any({File::Outdated,File::Ours}))return State::UpdateAvailable;
    if(any({File::Missing}))return State::NeedsRepair;
    if(any({File::Adoptable}))return linked?State::ByModManager:State::NeedsRepair;
    return versions?State::UpdateAvailable:State::UpToDate;
}

namespace {
// The plan: every file of the manifest (without the launcher's folder when the layout leaves it out), plus the manifest
// itself in the launcher's folder, as found on disk. Order: the co-op data first, the game's proxy last (a proxy without
// its mod would stop the next co-op start).
std::vector<Item> plan(const Layout& l,const Manifest& m) {
    std::vector<Item> items;
    auto add=[&](const std::string& root,const std::string& rel,const fs::path& source,const std::string& expected) {
        bool app=root=="data"&&rel.rfind("app/",0)==0;
        if(app&&!l.app)return;
        Item i;i.root=root;i.path=rel;i.source=source;i.expected=expected;
        i.target=root=="game"?anyCase(l.game,relative(rel)):l.data/relative(rel);
        unlinked(root=="game"?l.game:l.data,i.target);
        i.receipt=readReceipt(i.target);
        bool there=present(i.target);
        if(there&&!regular(i.target))throw Error("Not a plain file: "+i.target.u8string());
        if(there)i.observed=sha256File(i.target);
        bool ours=root=="data"||(there&&hasMarker(i.target));   // the co-op data folder is ours; a game file only with the marker
        i.state=classify(there,i.observed,i.receipt,expected,ours);
        items.push_back(i);
    };
    for(auto& e:m.files){auto rel=e.path;if(e.root=="data"&&rel.rfind("app/",0)==0)rel=rel.substr(4);add(e.root,e.path,l.package/relative(rel),e.sha256);}
    if(l.app)add("data","app/"+std::string(ManifestName),l.package/ManifestName,sha256File(l.package/ManifestName));
    std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){return a.root!="game"&&b.root=="game";});
    return items;
}
std::string problem(const Item& i) {
    switch(i.state) {
    case File::Missing:return i.path+": missing";
    case File::Outdated:return i.path+": an earlier version (Update replaces it)";
    case File::Ours:return i.path+": an earlier co-op version without a receipt (Update replaces it)";
    case File::Adoptable:return i.path+": installed without Setup (Repair lets Setup look after it)";
    case File::Modified:return i.path+": changed since Setup installed it, so Setup leaves it alone. Delete it (and "+i.target.filename().u8string()+ReceiptSuffix+") to repair";
    case File::Foreign:return i.root=="game"?"The game folder already has an "+i.target.filename().u8string()+" that is not from the co-op mod (another mod's file?). "
        "The co-op proxy cannot load another "+i.target.filename().u8string()+" after itself, so Setup changes nothing. Remove or rename that file (or uninstall the other mod), then run Setup again."
        :i.path+": not the co-op mod's file; Setup leaves it alone";
    default:return {};
    }
}
}

Report status(const Layout& l) {
    Report r;auto m=readManifest(l.package);r.version=m.version;r.game=l.game;
    auto exe=anyCase(l.game,GameExe);
    if(l.game.empty()||!regular(exe)){r.state=State::NotAGameFolder;r.problems.push_back("No ds.exe in this folder. Choose the DEATH STRANDING DIRECTOR'S CUT folder.");return r;}
    r.gameSha256=sha256File(exe);
#ifndef MUTANT_INSTALL_NO_BUILD_CHECK
    if(!m.builds.empty()&&std::find(m.builds.begin(),m.builds.end(),r.gameSha256)==m.builds.end()) {
        r.state=State::UnsupportedBuild;r.build="unsupported";
        r.problems.push_back("This game build is not the one the co-op mod supports (Steam Director's Cut build 13300582), so co-op would stay off. "
                             "Verify the game's files in Steam (Properties, Installed Files) and opt out of betas, then run Setup again.");
        return r;
    }
#endif
    r.build="Steam build 13300582";
    r.items=plan(l,m);
    std::vector<File> states;bool linked=false,versions=false,receipts=false;
    for(auto& i:r.items) {
        states.push_back(i.state);
        if(i.receipt.present){receipts=true;if(i.receipt.version!=m.version)versions=true;if(i.root=="game"||r.installedVersion.empty())r.installedVersion=i.receipt.version;}
        std::error_code ec;if(i.state==File::Adoptable&&fs::hard_link_count(i.target,ec)>1&&!ec)linked=true;
        auto p=problem(i);if(!p.empty())r.problems.push_back(p);
    }
    r.state=decide(states,linked,versions);
    r.installedBy=receipts?"setup":r.state==State::ByModManager?"mod manager (hard-linked files, likely Vortex)":r.state==State::NotInstalled?"":"without Setup";
    return r;
}

std::string reportJson(const Report& r) {
    json::Value v=json::Value::object();
    v.set("schema",1);v.set("game",r.game.u8string());v.set("state",stateName(r.state));v.set("button",button(r.state));
    v.set("version",r.version);v.set("installed_version",r.installedVersion);v.set("installed_by",r.installedBy);
    v.set("build",r.build);v.set("game_sha256",r.gameSha256);
    json::Value problems=json::Value::array();for(auto& p:r.problems)problems.push(p);v.set("problems",problems);
    json::Value files=json::Value::array();
    for(auto& i:r.items){json::Value f=json::Value::object();f.set("root",i.root);f.set("path",i.path);f.set("state",fileName(i.state));f.set("target",i.target.u8string());files.push(f);}
    v.set("files",files);
    return v.dump();
}

namespace {
// Receipts of files an earlier version installed: beside the proxy in the game folder, the mod in the data folder and,
// when the layout includes it, everywhere in the launcher's folder. Returns the targets.
std::vector<std::pair<std::string,fs::path>> receipted(const Layout& l) {
    std::vector<std::pair<std::string,fs::path>> out;std::error_code ec;
    auto suffix=std::string(ReceiptSuffix);
    auto take=[&](const std::string& root,const fs::path& p){auto n=p.filename().u8string();if(n.size()>suffix.size()&&lower(n.substr(n.size()-suffix.size()))==suffix)out.push_back({root,p.parent_path()/fs::u8path(n.substr(0,n.size()-suffix.size()))});};
    for(fs::directory_iterator it(l.game,ec),end;!ec&&it!=end;it.increment(ec))take("game",it->path());
    ec.clear();for(fs::directory_iterator it(l.data/"redirect-data"/"mod",ec),end;!ec&&it!=end;it.increment(ec))take("data",it->path());
    if(l.app){ec.clear();for(fs::recursive_directory_iterator it(l.data/"app",fs::directory_options::none,ec),end;!ec&&it!=end;it.increment(ec))if(!it->is_symlink())take("data",it->path());}
    return out;
}
std::string key(const fs::path& p){return lower(p.lexically_normal().generic_u8string());}
}

Report install(const Layout& l,const std::function<bool()>& running) {
    auto before=status(l);
    switch(before.state) {
    case State::NotAGameFolder:throw Error(before.problems.front(),Failed);
    case State::UnsupportedBuild:throw Error(before.problems.front(),UnsupportedBuild);
    case State::Conflict:{std::string why;for(auto& i:before.items)if(i.state==File::Modified||i.state==File::Foreign)why+=(why.empty()?"":"\n")+problem(i);throw Error(why,Failed);}
    case State::UpToDate:case State::ByModManager:return before;
    default:break;
    }
#ifndef MUTANT_INSTALL_IGNORES_RUNNING
    if(running&&running())throw Error("The game is running. Close it, then try again.",GameRunning);
#endif
    auto m=readManifest(l.package);
#ifndef MUTANT_INSTALL_NO_PACKAGE_CHECK
    for(auto& i:before.items)if(!regular(i.source)||sha256File(i.source)!=i.expected)
        throw Error("The package is damaged: "+i.path+" does not match coop-mod.json. Download the release again.");
#endif
    struct Done {fs::path target;bool hadFile;bool hadReceipt;std::string oldReceipt;};
    std::vector<Done> done;std::vector<fs::path> made;
    auto keepOld=[&](const fs::path& target,bool hadFile) {
        Done d{target,hadFile,present(receiptOf(target)),""};
        if(d.hadReceipt)d.oldReceipt=slurp(receiptOf(target));
        if(hadFile){removeFile(with(target,".coop-old"));fs::copy_file(target,with(target,".coop-old"));}
        done.push_back(d);
    };
    try {
        for(auto& i:before.items) {
            if(i.state==File::Current&&i.receipt.present&&i.receipt.valid&&i.receipt.version==m.version)continue;
            std::vector<fs::path> missing;for(auto p=i.target.parent_path();!present(p);p=p.parent_path())missing.push_back(p);
            for(auto it=missing.rbegin();it!=missing.rend();++it){fs::create_directory(*it);made.push_back(*it);}
            unlinked(i.root=="game"?l.game:l.data,i.target);
            bool there=i.state!=File::Missing;
            keepOld(i.target,there);
            if(i.state==File::Current||i.state==File::Adoptable){writeText(receiptOf(i.target),receiptText(m.version,i.expected,""));continue;}   // the bytes are right: the receipt only
            // Journal first: a crash from here on leaves a receipt naming both the old and the new bytes (Update repairs it).
            writeText(receiptOf(i.target),receiptText(m.version,there?i.observed:"",i.expected));
            auto stage=with(i.target,".coop-stage");removeFile(stage);
            fs::copy_file(i.source,stage);
            if(sha256File(stage)!=i.expected)throw Error("A copied file does not match the package: "+i.path);
            replaceFile(stage,i.target);
            writeText(receiptOf(i.target),receiptText(m.version,i.expected,""));
        }
        // Files an earlier version installed that this one no longer ships: removed when unchanged.
        std::set<std::string> planned;for(auto& i:before.items)planned.insert(key(i.target));
#ifndef MUTANT_INSTALL_STALE_KEPT
        for(auto& [root,target]:receipted(l))if(!planned.count(key(target))) {
            auto r=readReceipt(target);if(!r.valid)continue;
            if(!present(target)){removeFile(receiptOf(target));continue;}
            auto h=sha256File(target);if(h==r.installed||h==r.pending){removeFile(target);removeFile(receiptOf(target));}
        }
#endif
    }catch(...) {
#ifndef MUTANT_INSTALL_NO_ROLLBACK
        for(auto it=done.rbegin();it!=done.rend();++it) {
            removeFile(with(it->target,".coop-stage"));
            if(it->hadFile){std::error_code ec;fs::rename(with(it->target,".coop-old"),it->target,ec);}else removeFile(it->target);
            if(it->hadReceipt){try{writeText(receiptOf(it->target),it->oldReceipt);}catch(...){}}else removeFile(receiptOf(it->target));
        }
        for(auto it=made.rbegin();it!=made.rend();++it){std::error_code ec;fs::remove(*it,ec);}
#endif
        throw;
    }
    for(auto& d:done)removeFile(with(d.target,".coop-old"));
    return status(l);
}

Removed uninstall(const Layout& l,const std::function<bool()>& running) {
#ifndef MUTANT_INSTALL_IGNORES_RUNNING
    if(running&&running())throw Error("The game is running. Close it, then try again.",GameRunning);
#endif
    Removed out;std::set<std::string> seen;
    auto drop=[&](const fs::path& target,const std::string& name){removeFile(target);removeFile(receiptOf(target));out.removed.push_back(name);};
    // What the receipts name, whatever version installed it.
    for(auto& [root,target]:receipted(l)) {
        auto name=(root=="game"?target.filename():target.lexically_relative(l.data)).generic_u8string();
        seen.insert(key(target));
        unlinked(root=="game"?l.game:l.data,target);
        auto r=readReceipt(target);
        if(!r.valid){out.kept.push_back(name+" (its receipt is unreadable)");continue;}
        if(!present(target)){removeFile(receiptOf(target));out.removed.push_back(name);continue;}
        auto h=sha256File(target);
#ifndef MUTANT_INSTALL_UNINSTALL_DELETES_CHANGED
        if(h!=r.installed&&h!=r.pending){out.kept.push_back(name+" (changed since install)");continue;}
#endif
        drop(target,name);
    }
    // Ours without a receipt: the launcher's earlier installs, a copy by hand. Another program's file stays.
    try {
        auto m=readManifest(l.package);
        for(auto& i:plan(l,m)) {
            if(seen.count(key(i.target))||i.state==File::Missing)continue;
#ifndef MUTANT_INSTALL_UNINSTALL_DELETES_FOREIGN
            if(i.state==File::Foreign||i.state==File::Modified){out.foreign.push_back(i.path);continue;}
#endif
            drop(i.target,i.path);
        }
    }catch(const Error&){}   // no readable package: the receipts alone decide
    // The launcher's folder goes when nothing else is left in it; the rest of the co-op data (saves, settings, logs) stays.
    if(l.app) {
        std::vector<fs::path> dirs;std::error_code ec;
        for(fs::recursive_directory_iterator it(l.data/"app",ec),end;!ec&&it!=end;it.increment(ec))if(it->is_directory()&&!it->is_symlink())dirs.push_back(it->path());
        std::sort(dirs.begin(),dirs.end(),[](const fs::path& a,const fs::path& b){return a.u8string().size()>b.u8string().size();});
        for(auto& d:dirs){std::error_code e;if(fs::is_empty(d,e)&&!e)fs::remove(d,e);}
        std::error_code e;if(fs::is_directory(l.data/"app",e)&&fs::is_empty(l.data/"app",e))fs::remove(l.data/"app",e);
    }
    return out;
}

// ---- Steam libraries ----
namespace {
// A KeyValues (VDF) text as nested objects: quoted strings with \\ \" \n \t escapes, braces, // comments.
struct Node {std::string value;std::vector<std::pair<std::string,Node>> children;bool object=false;};
struct Vdf {
    const std::string& t;size_t at=0;
    void space(){for(;;){while(at<t.size()&&std::isspace(static_cast<unsigned char>(t[at])))++at;if(t.compare(at,2,"//")==0){while(at<t.size()&&t[at]!='\n')++at;continue;}return;}}
    bool token(std::string& out,char& brace) {
        space();brace=0;out.clear();if(at>=t.size())return false;
        char c=t[at];if(c=='{'||c=='}'){brace=c;++at;return true;}
        if(c=='"') {
            for(++at;at<t.size()&&t[at]!='"';++at) {
#ifndef MUTANT_INSTALL_VDF_ESCAPES
                if(t[at]=='\\'&&at+1<t.size()){char e=t[++at];out+=e=='n'?'\n':e=='t'?'\t':e;continue;}
#endif
                out+=t[at];
            }
            ++at;return true;
        }
        while(at<t.size()&&!std::isspace(static_cast<unsigned char>(t[at]))&&t[at]!='{'&&t[at]!='}'&&t[at]!='"')out+=t[at++];
        return true;
    }
    void object(Node& n,int depth) {
        n.object=true;std::string k,v;char b=0;
        while(token(k,b)) {
            if(b=='}')return;
            if(b)return;   // a stray '{' where a key belongs: stop reading this object
            if(!token(v,b))return;
            Node child;if(b=='{'){if(depth<16)object(child,depth+1);}else if(b)return;else child.value=v;
            n.children.push_back({k,std::move(child)});
        }
    }
};
Node parseVdf(const std::string& text){Node root;Vdf p{text};p.object(root,0);return root;}
const Node* child(const Node& n,const std::string& name){for(auto& c:n.children)if(lower(c.first)==lower(name))return &c.second;return nullptr;}
}

std::vector<std::string> libraryFolders(const std::string& vdf) {
    std::vector<std::string> out;auto root=parseVdf(vdf);auto lib=child(root,"libraryfolders");
    if(!lib)return out;
    for(auto& c:lib->children) {
        if(c.second.object){if(auto p=child(c.second,"path"))if(!p->value.empty())out.push_back(p->value);}
        else if(!c.first.empty()&&std::all_of(c.first.begin(),c.first.end(),[](char x){return std::isdigit(static_cast<unsigned char>(x));})&&!c.second.value.empty())out.push_back(c.second.value);   // the old format: "1" "D:\\Games"
#ifdef MUTANT_INSTALL_VDF_FIRST_ONLY
        if(!out.empty())break;
#endif
    }
    return out;
}
std::string installDir(const std::string& acf) {
    auto root=parseVdf(acf);auto app=child(root,"AppState");if(!app)return {};
    auto d=child(*app,"installdir");
    if(!d||d->value.empty()||d->value.find_first_of("/\\:")!=std::string::npos||d->value==".."||d->value==".")return {};
    return d->value;
}
std::vector<fs::path> findGames(const std::vector<fs::path>& steamRoots,const std::vector<fs::path>& extra) {
    auto same=[](const fs::path& p){auto s=p.generic_u8string();while(s.size()>1&&s.back()=='/')s.pop_back();return lower(s);};
    auto native=[](std::string s){
#ifdef _WIN32
        for(auto& c:s)if(c=='/')c='\\';
#endif
        return fs::u8path(s);};
    std::vector<fs::path> libraries,found;std::set<std::string> libSeen,gameSeen;
    auto library=[&](const fs::path& p){if(!p.empty()&&libSeen.insert(same(p)).second)libraries.push_back(p);};
    for(auto& r:steamRoots) {
        if(r.empty())continue;
        library(r);
        for(auto vdf:{r/"steamapps"/"libraryfolders.vdf",r/"config"/"libraryfolders.vdf"}) {
            try{if(regular(vdf))for(auto& p:libraryFolders(slurp(vdf)))library(native(p));}catch(const Error&){}
        }
    }
#ifndef MUTANT_INSTALL_GAME_DUPLICATES
    auto game=[&](const fs::path& d){if(regular(d/GameExe)&&gameSeen.insert(same(d)).second)found.push_back(d);};
#else
    auto game=[&](const fs::path& d){if(regular(d/GameExe))found.push_back(d);};
#endif
    for(auto& lib:libraries) {
        auto acf=lib/"steamapps"/("appmanifest_"+std::string(SteamApp)+".acf");
#ifndef MUTANT_INSTALL_ACF_IGNORED
        try{if(regular(acf)){auto dir=installDir(slurp(acf));if(!dir.empty())game(lib/"steamapps"/"common"/fs::u8path(dir));}}catch(const Error&){}
#else
        (void)acf;
#endif
    }
    for(auto& e:extra)if(!e.empty())game(e);
    return found;
}
}
