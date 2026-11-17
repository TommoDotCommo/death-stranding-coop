// Game-free oracle of the install core (install.h) in fake game, package and data folders: states, receipts, update,
// repair, rollback, uninstall keeping foreign and changed files, Steam library parsing. Every protection has a mutant in
// test_mutants.py that must make this fail ("install oracle:" on stderr, exit 1).
#include "install.h"
#include "../world/json.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
using namespace launcher::install;
static void check(bool b,const std::string& what){if(!b){std::cerr<<"install oracle: "<<what<<'\n';std::exit(1);}}
static void put(const fs::path& p,const std::string& b){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary|std::ios::trunc)<<b;}
static std::string get(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
static int code(const std::function<void()>& f){try{f();}catch(const Error& e){return e.code;}catch(...){return -1;}return 0;}
static std::string message(const std::function<void()>& f){try{f();}catch(const std::exception& e){return e.what();}return {};}
// Every file below a folder with its bytes: "the folder is byte-for-byte what it was".
static std::map<std::string,std::string> tree(const fs::path& d){std::map<std::string,std::string> m;std::error_code ec;
    for(fs::recursive_directory_iterator it(d,ec),end;!ec&&it!=end;it.increment(ec))m[it->path().lexically_relative(d).generic_string()+(it->is_directory()?"/":"")]=it->is_directory()?"":get(it->path());
    return m;}
static const std::string Game="ds.exe of build 13300582";
// A release package: the files coop-mod.json lists, under the paths tools/mod_manifest.py gives them.
static void package(const fs::path& p,const std::string& v,bool extra=false) {
    fs::remove_all(p);
    std::vector<std::pair<std::string,std::string>> files={{"avrt.dll","proxy COOP_REDIRECT_INSTANCE "+v},{"redirect-data/mod/mod.dll","mod "+v},
        {"deathstranding-coop.exe","launcher "+v},{"coop-world.exe","server "+v},{"README.txt","readme "+v}};
    if(extra)files.push_back({"extra.txt","only in "+v});
    json::Value list=json::Value::array();
    for(auto& [rel,bytes]:files) {
        put(p/rel,bytes);
        auto add=[&](const std::string& root,const std::string& path){json::Value e=json::Value::object();e.set("path",path);e.set("sha256",sha256File(p/rel));e.set("size",uint64_t(bytes.size()));e.set("root",root);list.push(e);};
        if(rel=="avrt.dll")add("game",rel);
        if(rel=="redirect-data/mod/mod.dll")add("data",rel);
        add("data","app/"+rel);
    }
    put(p/"build.tmp",Game);
    json::Value build=json::Value::object();build.set("name","Steam build 13300582");build.set("sha256",sha256File(p/"build.tmp"));fs::remove(p/"build.tmp");
    json::Value builds=json::Value::array();builds.push(build);
    json::Value game=json::Value::object();game.set("name","DEATH STRANDING DIRECTOR'S CUT");game.set("steam_appid",1850570);game.set("builds",builds);
    json::Value m=json::Value::object();m.set("schema",1);m.set("id",Id);m.set("name","Death Stranding Co-op");m.set("version",v);m.set("game",game);m.set("files",list);
    put(p/"coop-mod.json",m.dump());
}
static bool none(){return false;}
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"usage: install-test <empty temporary folder>\n";return 2;}
    fs::path root=argv[1];
    {   // the pure decisions
        Receipt no,yes;yes.present=yes.valid=true;yes.installed="a";yes.pending="b";
        check(classify(false,"",yes,"x",true)==File::Missing,"a missing file is missing");
        check(classify(true,"x",no,"x",false)==File::Adoptable&&classify(true,"x",yes,"x",false)==File::Current,"the package's bytes");
        check(classify(true,"y",no,"x",false)==File::Foreign&&classify(true,"y",no,"x",true)==File::Ours,"no receipt: foreign unless ours");
        check(classify(true,"a",yes,"x",false)==File::Outdated&&classify(true,"b",yes,"x",false)==File::Outdated,"what an earlier Setup put there, or was putting there");
        check(classify(true,"c",yes,"x",true)==File::Modified,"a receipted file changed since is modified, even when it looks like ours");
        Receipt bad;bad.present=true;check(classify(true,"x",bad,"x",false)==File::Modified,"an unreadable receipt is never trusted");
        check(decide({File::Missing,File::Missing},false,false)==State::NotInstalled&&decide({File::Current,File::Foreign},false,false)==State::Conflict,"install, conflict");
        check(decide({File::Current,File::Outdated},false,false)==State::UpdateAvailable&&decide({File::Current,File::Missing},false,false)==State::NeedsRepair,"update, repair");
        check(decide({File::Current,File::Current},false,false)==State::UpToDate&&decide({File::Current},false,true)==State::UpdateAvailable,"up to date unless a receipt names another version");
        check(decide({File::Adoptable},true,false)==State::ByModManager&&decide({File::Adoptable},false,false)==State::NeedsRepair,"a mod manager's links, or a copy by hand");
        check(std::string(button(State::NotInstalled))=="Install"&&std::string(button(State::Conflict))=="Needs attention"&&std::string(button(State::UnsupportedBuild))=="Needs attention","the button");
    }
    auto pkg=root/"pkg",game=root/"game",data=root/"data";
    Layout l{pkg,game,data,true};
    package(pkg,"v1",true);
    {   // folders that cannot take it
        fs::create_directories(game);
        check(status(l).state==State::NotAGameFolder&&code([&]{install(l,none);})==Failed&&!fs::exists(data),"no ds.exe: not a game folder, nothing written");
        put(game/"ds.exe","another build");
        check(status(l).state==State::UnsupportedBuild&&code([&]{install(l,none);})==UnsupportedBuild&&!fs::exists(data)&&!fs::exists(game/"avrt.dll"),"another build: exit 3, nothing written");
        put(game/"ds.exe",Game);put(game/"other.dll","theirs");put(game/"Saves"/"x.sav","save");
        check(status(l).state==State::NotInstalled&&std::string(button(status(l).state))=="Install","a fresh game shows Install");
        check(code([&]{install(l,[]{return true;});})==GameRunning&&!fs::exists(game/"avrt.dll")&&!fs::exists(data),"the game running: exit 4, nothing written");
    }
    auto before=tree(game);
    {   // install: every file in its place with its receipt; nothing of the game changes
        auto r=install(l,none);
        check(r.state==State::UpToDate&&r.installedBy=="setup"&&r.installedVersion=="v1","installed and up to date");
        check(get(game/"avrt.dll")==get(pkg/"avrt.dll")&&get(data/"redirect-data/mod/mod.dll")==get(pkg/"redirect-data/mod/mod.dll"),"the proxy into the game, the mod into the data folder");
        for(auto f:{"deathstranding-coop.exe","coop-world.exe","avrt.dll","redirect-data/mod/mod.dll","README.txt","extra.txt","coop-mod.json"})
            check(get(data/"app"/f)==get(pkg/f)&&fs::exists(data/"app"/(std::string(f)+ReceiptSuffix)),std::string("the launcher's folder with receipts: ")+f);
        for(auto f:{game/"avrt.dll",data/"redirect-data/mod/mod.dll"}){auto rc=readReceipt(f);check(rc.valid&&rc.installed==sha256File(f)&&rc.pending.empty()&&rc.version=="v1","a receipt beside "+f.string());}
        auto now=tree(game);now.erase("avrt.dll");now.erase(std::string("avrt.dll")+ReceiptSuffix);check(now==before,"nothing of the game changed");
        check(install(l,[]{return true;}).state==State::UpToDate,"up to date: nothing to do, even while the game runs");
        auto j=json::parse(reportJson(status(l)));check(j.get("state").string()=="up-to-date"&&j.get("button").string()=="Up to date"&&j.get("files").items().size()==9,"status --json");
    }
    {   // repair: a missing file comes back
        fs::remove(data/"redirect-data/mod/mod.dll");
        check(status(l).state==State::NeedsRepair&&std::string(button(State::NeedsRepair))=="Repair","a missing mod needs repair");
        check(install(l,none).state==State::UpToDate&&get(data/"redirect-data/mod/mod.dll")=="mod v1","repaired");
    }
    {   // update: v2 replaces v1, and the file v2 no longer ships goes
        put(data/"redirect-data/mod/mod.json","{\"role\":\"host\"}");put(data/"redirect-data/local/save.bin","a co-op save");
        package(pkg,"v2");
        auto s=status(l);check(s.state==State::UpdateAvailable&&s.installedVersion=="v1"&&s.version=="v2","v2 over v1: Update");
        auto r=install(l,none);
        check(r.state==State::UpToDate&&get(game/"avrt.dll")==get(pkg/"avrt.dll")&&get(data/"app/deathstranding-coop.exe")=="launcher v2","updated");
        check(!fs::exists(data/"app/extra.txt")&&!fs::exists(data/"app/extra.txt.coop-receipt.json"),"a file the new version does not ship is removed");
        check(get(data/"redirect-data/mod/mod.json")=="{\"role\":\"host\"}"&&get(data/"redirect-data/local/save.bin")=="a co-op save","settings and saves untouched");
        check(!fs::exists(game/"avrt.dll.coop-old")&&!fs::exists(game/"avrt.dll.coop-stage"),"no leftovers");
    }
    {   // an update interrupted after the journal: the receipt names the bytes it was putting there, so it is not a conflict
        auto r=readReceipt(game/"avrt.dll");auto v2=get(game/"avrt.dll");
        put(game/"avrt.dll","proxy COOP_REDIRECT_INSTANCE v1.5");
        put(game/(std::string("avrt.dll")+ReceiptSuffix),"{\"schema\":1,\"id\":\"death-stranding-coop\",\"version\":\"v2\",\"installed\":\""+r.installed+"\",\"pending\":\""+sha256File(game/"avrt.dll")+"\"}");
        check(status(l).state==State::UpdateAvailable,"a half-finished update reads as Update, not Needs attention");
        check(install(l,none).state==State::UpToDate&&get(game/"avrt.dll")==v2,"and finishes");
    }
    {   // the player changed a file: Needs attention, never replaced
        auto changed=std::string("proxy COOP_REDIRECT_INSTANCE patched by the player");put(game/"avrt.dll",changed);
        check(status(l).state==State::Conflict&&code([&]{install(l,none);})==Failed&&get(game/"avrt.dll")==changed,"a changed proxy is never replaced");
        auto u=uninstall(l,none);
        check(get(game/"avrt.dll")==changed&&u.kept.size()==1&&fs::exists(game/(std::string("avrt.dll")+ReceiptSuffix)),"nor removed: kept with its receipt");
        check(!fs::exists(data/"redirect-data/mod/mod.dll")&&!fs::exists(data/"app"),"everything else of ours is gone, the launcher's folder too");
        check(get(data/"redirect-data/mod/mod.json")=="{\"role\":\"host\"}"&&get(data/"redirect-data/local/save.bin")=="a co-op save","the co-op data stays");
        fs::remove(game/"avrt.dll");fs::remove(game/(std::string("avrt.dll")+ReceiptSuffix));
        check(tree(game)==before,"the game folder is what it was");
    }
    {   // another mod's avrt.dll, any letter case: refused with a clear message, left alone by uninstall
        put(game/"AVRT.dll","another mod's avrt");
        auto s=status(l);check(s.state==State::Conflict&&std::string(button(s.state))=="Needs attention","a foreign avrt.dll needs attention");
        auto why=message([&]{install(l,none);});
        check(why.find("not from the co-op mod")!=std::string::npos&&why.find("cannot load another")!=std::string::npos,"the refusal says why: "+why);
        check(get(game/"AVRT.dll")=="another mod's avrt"&&!fs::exists(game/"avrt.dll")&&!fs::exists(data/"redirect-data/mod/mod.dll"),"nothing changed");
        auto u=uninstall(l,none);check(get(game/"AVRT.dll")=="another mod's avrt"&&u.foreign.size()==1&&u.kept.empty(),"uninstall keeps another program's file");
        fs::remove(game/"AVRT.dll");
    }
    {   // an older co-op proxy the launcher put there without a receipt: ours, Update replaces it
        put(game/"avrt.dll","proxy COOP_REDIRECT_INSTANCE from an old launcher");
        check(status(l).state==State::UpdateAvailable,"an old co-op proxy without a receipt is an update");
        check(install(l,none).state==State::UpToDate&&get(game/"avrt.dll")==get(pkg/"avrt.dll"),"replaced, with a receipt now");
        uninstall(l,none);check(tree(game)==before,"uninstalled");
    }
    {   // copied by hand: Repair writes the receipts only; hard-linked by a mod manager: left to the mod manager
        fs::copy_file(pkg/"avrt.dll",game/"avrt.dll");
        auto hand=l;hand.app=false;
        fs::create_directories(data/"redirect-data/mod");fs::copy_file(pkg/"redirect-data/mod/mod.dll",data/"redirect-data/mod/mod.dll");
        check(status(hand).state==State::NeedsRepair,"copied by hand: Repair");
        check(install(hand,none).state==State::UpToDate&&readReceipt(game/"avrt.dll").valid,"adopted with receipts");
        uninstall(hand,none);check(!fs::exists(game/"avrt.dll")&&!fs::exists(data/"redirect-data/mod/mod.dll"),"and removed again");
        fs::create_hard_link(pkg/"avrt.dll",game/"avrt.dll");fs::create_hard_link(pkg/"redirect-data/mod/mod.dll",data/"redirect-data/mod/mod.dll");
        check(status(hand).state==State::ByModManager,"hard links: a mod manager's");
        check(install(hand,none).state==State::ByModManager&&!fs::exists(game/(std::string("avrt.dll")+ReceiptSuffix)),"Setup leaves a mod manager's files alone");
        fs::remove(game/"avrt.dll");fs::remove(data/"redirect-data/mod/mod.dll");
    }
    {   // the launcher's own window: the game and the mod only, the launcher's folder is the package
        auto own=l;own.app=false;
        check(install(own,none).state==State::UpToDate&&!fs::exists(data/"app")&&fs::exists(game/"avrt.dll"),"the launcher installs the proxy and the mod only");
        uninstall(own,none);check(tree(game)==before&&!fs::exists(data/"redirect-data/mod/mod.dll"),"and removes them");
    }
    if(geteuid()!=0) {   // a failure part-way undoes everything: the game folder refuses the proxy (written last)
        chmod(game.c_str(),0555);
        auto c=code([&]{install(l,none);});
        chmod(game.c_str(),0755);
        check(c!=0&&!fs::exists(data/"app")&&!fs::exists(data/"redirect-data/mod/mod.dll")&&tree(game)==before,"rolled back after a failure part-way");
    }
    if(geteuid()!=0) {   // a file that cannot be deleted (in use on Windows) is kept and said, with its receipt
        install(l,none);
        chmod((data/"app").c_str(),0555);
        auto u=uninstall(l,none);
        chmod((data/"app").c_str(),0755);
        bool said=false;for(auto& k:u.kept)said|=k.find("in use")!=std::string::npos;
        check(said&&fs::exists(data/"app"/"deathstranding-coop.exe")&&fs::exists(data/"app"/(std::string("deathstranding-coop.exe")+ReceiptSuffix)),"a file that cannot be removed is reported, not counted as removed");
        uninstall(l,none);check(!fs::exists(data/"app")&&tree(game)==before,"and goes on the next uninstall");
    }
    {   // a damaged package: refused before anything is written
        put(pkg/"coop-world.exe","truncated");
        auto why=message([&]{install(l,none);});
        check(why.find("package is damaged")!=std::string::npos&&!fs::exists(data/"app")&&tree(game)==before,"a damaged package is refused: "+why);
        package(pkg,"v2");
    }
    {   // a linked folder is never written through
        fs::create_directories(root/"elsewhere");fs::create_directory_symlink(root/"elsewhere",data/"app");
        check(code([&]{install(l,none);})!=0&&fs::is_empty(root/"elsewhere"),"a linked launcher folder is refused");
        fs::remove(data/"app");
    }
    {   // a manifest path that leaves its folder
        auto m=get(pkg/"coop-mod.json");auto at=m.find("app/README.txt");put(pkg/"coop-mod.json",m.substr(0,at)+"app/../../README.txt"+m.substr(at+14));
        check(code([&]{readManifest(pkg);})==Usage,"a manifest path with .. is refused");
        package(pkg,"v2");
    }
    {   // Steam libraries
        auto lf=libraryFolders("// comment\n\"LibraryFolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"apps\" { \"1850570\" \"1\" }\n\t}\n"
                               "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\Games\\\\Steam \\\"Lib\\\"\"\n\t}\n}\n");
        check(lf.size()==2&&lf[0]=="C:\\Program Files (x86)\\Steam"&&lf[1]=="D:\\Games\\Steam \"Lib\"","libraryfolders.vdf: every library, escapes read");
        auto old=libraryFolders("\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1\"\n\t\"ContentStatsID\"\t\t\"-1\"\n\t\"1\"\t\t\"E:\\\\SteamLibrary\"\n\t\"2\"\t\t\"F:\\\\Steam\"\n}\n");
        check(old.size()==2&&old[0]=="E:\\SteamLibrary"&&old[1]=="F:\\Steam","the old libraryfolders.vdf format");
        check(installDir("\"AppState\"\n{\n\t\"appid\"\t\t\"1850570\"\n\t\"installdir\"\t\t\"DEATH STRANDING DIRECTORS CUT\"\n}\n")=="DEATH STRANDING DIRECTORS CUT","appmanifest installdir");
        check(installDir("\"AppState\" { \"installdir\" \"..\\\\..\\\\x\" }").empty(),"an installdir that leaves steamapps/common is ignored");
        auto steam=root/"Steam",lib2=root/"lib two",lib3=root/"lib3";
        put(steam/"steamapps"/"libraryfolders.vdf","\"libraryfolders\" { \"0\" { \"path\" \""+steam.string()+"\" } \"1\" { \"path\" \""+lib2.string()+"\" } \"2\" { \"path\" \""+lib3.string()+"\" } }");
        put(lib3/"steamapps"/"appmanifest_1850570.acf","\"AppState\" { \"installdir\" \"Death Stranding Custom\" }");
        put(lib3/"steamapps"/"common"/"Death Stranding Custom"/"ds.exe",Game);
        put(lib2/"steamapps"/"common"/"Other"/"ds.exe","not listed by Steam");
        auto found=findGames({steam,steam/""},{lib3/"steamapps"/"common"/"Death Stranding Custom"});
        check(found.size()==1&&found[0]==lib3/"steamapps"/"common"/"Death Stranding Custom","the game in the third library, by its appmanifest, once");
        check(findGames({steam},{}).size()==1,"found through the appmanifest alone");
        check(findGames({root/"no steam"},{}).empty(),"no Steam: nothing found");
    }
    std::cout<<"PASS: install states, receipts beside every file, update, repair, interrupted update, rollback, uninstall keeps foreign and changed files and the co-op data, mod-manager and by-hand installs, Steam libraries\n";
}
