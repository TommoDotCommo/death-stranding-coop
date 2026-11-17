// Game-free oracle for testsaves.{h,cpp}: the pack (zip and folder) against its manifest, the refusals, and using a
// set (re-signed for the player, the co-op saves moved aside first, the ordinary saves never touched). Synthetic saves
// only (saves_vectors.h). With a second argument, also a pack made by tools/make_test_saves.py (test_testsaves.py).
#include "testsaves.h"
#include "saves.h"
#include "saves_vectors.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
namespace fs=std::filesystem;
using namespace launcher;
using namespace launcher::testsaves;
static void check(bool b,const char* what) {if(!b){std::cerr<<"launcher oracle: "<<what<<'\n';std::exit(1);}}
template<class F> static bool refusedPack(F f) {try{f();}catch(const PackError&){return true;}catch(const std::exception&){}return false;}
#define VEC(v) std::string(v,sizeof(v)-1)
static void write(const fs::path& p,const std::string& b) {fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(b.data(),std::streamsize(b.size()));}
static std::string read(const fs::path& p) {std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
static std::string big(size_t extra,unsigned seed) {   // a valid checkpoint of any size (saves_test.cpp)
    std::string a=VEC(VectorA);size_t meta=std::stoul(a.substr(0,8)),picture=std::stoul(a.substr(8,8));
    std::string block=a.substr(24+meta+picture);std::mt19937 r(seed);for(size_t i=0;i<extra;++i)block+=char(r());
    std::string out;for(size_t v:{meta,picture,block.size()}){auto t=std::to_string(v);t.resize(8,'\0');out+=t;}
    return out+a.substr(24,meta+picture)+block;
}
// Every file under a folder with its bytes: "nothing changed" is this map being equal before and after.
static std::map<std::string,std::string> tree(const fs::path& root) {
    std::map<std::string,std::string> out;std::error_code ec;
    for(fs::recursive_directory_iterator it(root,ec),end;!ec&&it!=end;it.increment(ec))if(it->is_regular_file())out[it->path().lexically_relative(root).generic_string()]=read(it->path());
    return out;
}
// A zip as the release's is made (stored entries), or with one entry deflated (method 8) when `deflate` names it.
static void put16(std::string& b,uint32_t v){b+=char(v);b+=char(v>>8);}
static void put32(std::string& b,uint32_t v){for(int k=0;k<4;++k)b+=char(v>>(8*k));}
static void zip(const fs::path& out,const std::vector<std::pair<std::string,std::string>>& files,const std::string& deflate="") {
    std::string body,cd;
    for(auto& f:files) {
        uint32_t at=uint32_t(body.size()),crc=saves::crc32(f.second),method=f.first==deflate?8:0;
        body+="PK\x03\x04";put16(body,20);put16(body,0);put16(body,method);put16(body,0);put16(body,0x21);put32(body,crc);put32(body,uint32_t(f.second.size()));put32(body,uint32_t(f.second.size()));
        put16(body,uint32_t(f.first.size()));put16(body,0);body+=f.first;body+=f.second;
        cd+="PK\x01\x02";put16(cd,20);put16(cd,20);put16(cd,0);put16(cd,method);put16(cd,0);put16(cd,0x21);put32(cd,crc);put32(cd,uint32_t(f.second.size()));put32(cd,uint32_t(f.second.size()));
        put16(cd,uint32_t(f.first.size()));put16(cd,0);put16(cd,0);put16(cd,0);put16(cd,0);put32(cd,0);put32(cd,at);cd+=f.first;
    }
    std::string end="PK\x05\x06";put16(end,0);put16(end,0);put16(end,uint32_t(files.size()));put16(end,uint32_t(files.size()));put32(end,uint32_t(cd.size()));put32(end,uint32_t(body.size()));put16(end,0);
    write(out,body+cd+end);
}
struct Fixture {std::string id,title;std::vector<std::pair<std::string,std::string>> files;};   // (path in the pack, bytes)
static std::string manifest(const std::vector<Fixture>& sets,const std::map<std::string,std::string>& hashOverride={}) {
    std::string j="{\"schema\":1,\"version\":\"0.1.0-alpha.1\",\"sets\":[";
    for(size_t i=0;i<sets.size();++i) {
        auto& s=sets[i];j+=std::string(i?",":"")+"{\"id\":\""+s.id+"\",\"title\":\""+s.title+"\",\"description\":\"Synthetic.\\nTwo lines.\",\"episode\":3,\"play_time\":\"28 h\",\"source\":\"test\",\"files\":[";
        for(size_t k=0;k<s.files.size();++k){auto& f=s.files[k];auto o=hashOverride.find(f.first);
            j+=std::string(k?",":"")+"{\"path\":\""+f.first+"\",\"sha256\":\""+(o==hashOverride.end()?sha256(f.second):o->second)+"\",\"size\":"+std::to_string(f.second.size())+"}";}
        j+="]}";
    }
    return j+"]}";
}
static std::vector<std::pair<std::string,std::string>> entries(const std::vector<Fixture>& sets,const std::string& listing) {
    std::vector<std::pair<std::string,std::string>> out{{Manifest,listing}};
    for(auto& s:sets)for(auto& f:s.files)out.push_back(f);
    return out;
}
int main(int argc,char** argv) {
    check(argc==2||argc==3,"temporary test root required");fs::path root=argv[1];
    const std::string A=VEC(VectorA),B=VEC(VectorB),Profile=VEC(VectorProfile);
    const uint32_t accountB=39734272;const std::string userB="steam_76561198000000000";

    // SHA-256 (FIPS 180-2 examples) and version order
    check(sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha256 of nothing");
    check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha256 abc");
    check(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","sha256 two blocks");
    check(sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","sha256 a million");
    check(versionBefore("0.1.0-alpha.9","0.1.0-alpha.10")&&!versionBefore("0.1.0-alpha.10","0.1.0-alpha.9")&&versionBefore("0.1.0","0.1.0.1")&&!versionBefore("1.0","1.0"),"versions compare by their numbers");

    const std::vector<Fixture> good={
        {"ep1","Episode 1 \xc3\xa9",{{"ep1/manualsave0/checkpoint.dat",A},{"ep1/autosave2/checkpoint.dat",big(3000,4)}}},
        {"late","Late game",{{"late/quicksave1/checkpoint.dat",B}}}};
    const std::string listing=manifest(good);
    auto packZip=root/"launcher"/"deathstranding-coop-test-saves-0.1.0-alpha.10.zip";zip(packZip,entries(good,listing));

    {   // reading the pack
        auto p=Pack::open(packZip);
        check(p.version=="0.1.0-alpha.1"&&p.sets.size()==2&&p.sets[0].id=="ep1"&&p.sets[0].title=="Episode 1 \xc3\xa9"&&p.sets[0].files.size()==2,"lists the sets");
        check(p.sets[0].description=="Synthetic.\nTwo lines."&&p.sets[0].episode==3&&p.sets[0].playTime=="28 h"&&p.sets[0].source=="test","reads a set's details");
        check(p.read(p.sets[1].files[0])==B&&p.find("late")&&!p.find("nope"),"reads a file through the zip");
        auto folder=root/"launcher"/FolderName;
        for(auto& e:entries(good,listing))write(folder/e.first,e.second);
        auto q=Pack::open(folder);check(q.sets.size()==2&&q.read(q.sets[0].files[1])==big(3000,4),"reads an extracted pack");
        check(find(root/"launcher")==folder,"an extracted pack in the test-saves folder comes first");
        fs::remove_all(folder);
        write(root/"launcher"/"deathstranding-coop-test-saves-0.1.0-alpha.9.zip","x");write(root/"launcher"/"other.zip","x");
        check(find(root/"launcher")==packZip,"else the newest zip beside the launcher");
        check(!find(root/"empty"),"no pack, nothing found");
        auto l=listCommand(packZip);check(l.code==ExitOk&&l.message=="ep1\tEpisode 1 \xc3\xa9\nlate\tLate game\n","lists ids and titles");
        check(listCommand(std::nullopt).code==ExitPack&&listCommand(root/"none.zip").code==ExitPack,"a missing pack is exit 3");
    }
    {   // refusals: every one a PackError
        auto bad=[&](const char* name,const std::vector<std::pair<std::string,std::string>>& files,const std::string& deflate=""){auto p=root/"bad"/name;zip(p,files,deflate);return p;};
        // a file that is not a checkpoint folder's checkpoint.dat, listed in the manifest
        for(auto path:{"ep1/profile/profile.dat","ep1/manualsave0/notes.txt","ep1/manualsave0/checkpoint.dat.bak","ep1/../ep1/manualsave0/checkpoint.dat","late/manualsave0/checkpoint.dat","ep1/ManualSave0/checkpoint.dat","ep1/x/manualsave0/checkpoint.dat"}) {
            std::vector<Fixture> s={{"ep1","T",{{path,A}}}};
            check(refusedPack([&]{Pack::open(bad("path.zip",entries(s,manifest(s))));}),"a listed file that is not a checkpoint is refused");
        }
        {   // a file the manifest does not list
            auto files=entries(good,listing);files.push_back({"ep1/profile/profile.dat",Profile});
            check(refusedPack([&]{Pack::open(bad("extra.zip",files));}),"an unlisted file in the zip is refused");
            auto folder=root/"bad"/"extra-folder";for(auto& e:files)write(folder/e.first,e.second);
            check(refusedPack([&]{Pack::open(folder);}),"an unlisted file in the folder is refused");
            auto f2=entries(good,listing);f2.push_back({"README.txt","hello"});
            check(refusedPack([&]{Pack::open(bad("extra2.zip",f2));}),"any other file is refused");
        }
        {auto files=entries(good,listing);files.pop_back();check(refusedPack([&]{Pack::open(bad("missing.zip",files));}),"a listed file that is missing is refused");}
        check(refusedPack([&]{Pack::open(bad("deflated.zip",entries(good,listing),"ep1/manualsave0/checkpoint.dat"));}),"a compressed entry is refused");
        {auto files=entries(good,listing);files.push_back({"../escape.dat","x"});check(refusedPack([&]{Pack::open(bad("escape.zip",files));}),"an unsafe name is refused");}
        for(auto text:{"{","[]","{\"schema\":2,\"version\":\"1\",\"sets\":[]}","{\"schema\":1,\"version\":\"1\",\"sets\":[]}"}) {
            check(refusedPack([&]{std::string v;std::vector<Set> s;parseManifest(text,v,s);}),"a manifest of another kind is refused");
        }
        {std::vector<Fixture> twice={good[0],good[0]};check(refusedPack([&]{Pack::open(bad("twice.zip",entries({good[0]},manifest(twice))));}),"one id twice is refused");}
        {std::vector<Fixture> s={{"Bad Id","T",{{"Bad Id/manualsave0/checkpoint.dat",A}}}};check(refusedPack([&]{Pack::open(bad("id.zip",entries(s,manifest(s))));}),"an id with spaces is refused");}
        {std::vector<Fixture> s={{"ep1","",{{"ep1/manualsave0/checkpoint.dat",A}}}};check(refusedPack([&]{Pack::open(bad("title.zip",entries(s,manifest(s))));}),"a set without a title is refused");}
        {auto files=entries(good,listing);files[0].second[files[0].second.size()/2]^=1;check(refusedPack([&]{Pack::open(bad("crc.zip",files));}),"a damaged manifest is refused");}
        check(refusedPack([&]{Pack::open(bad("junk.zip",{}));})&&refusedPack([&]{write(root/"bad"/"junk2.zip",std::string(5000,'x'));Pack::open(root/"bad"/"junk2.zip");}),"what is not a zip is refused");
    }

    // using a set: the co-op data and the ordinary saves, as on a player's PC
    auto data=root/"local"/"DeathStrandingCoop",local=root/"local";
    auto coop=saves::gameFolder(data/"redirect-data"/"local")/std::to_string(accountB);
    auto ordinary=local/"KojimaProductions"/"DeathStrandingDC"/std::to_string(accountB);
    write(ordinary/"manualsave1"/"checkpoint.dat",A);write(ordinary/"profile"/"profile.dat",Profile);
    write(coop/"autosave5"/"checkpoint.dat",A);write(coop/"profile"/"profile.dat",Profile);
    const auto ordinaryBefore=tree(local/"KojimaProductions");
    check(coopAccount(data,local,accountB)==coop,"test saves go into the co-op account folder");
    {   // refused before anything changes
        const auto before=tree(data);
        check(useCommand(packZip,"ep1",data,local,accountB,false,1000).code==ExitRunning,"the game or the lobby open: exit 4");
        check(tree(data)==before,"and nothing changed");
        check(useCommand(std::nullopt,"ep1",data,local,accountB,true,1000).code==ExitPack,"no pack: exit 3");
        check(useCommand(packZip,"nope",data,local,accountB,true,1000).code==ExitFailed,"an unknown id: exit 1");
        check(useCommand(packZip,"ep1",data,local,0,true,1000).code==ExitFailed,"no Steam account: exit 1");
        // the manifest's hash does not match the file: refused whole, nothing moved
        auto files=entries(good,manifest(good,{{"ep1/autosave2/checkpoint.dat",sha256("something else")}}));
        auto wrong=root/"bad"/"hash.zip";zip(wrong,files);
        auto o=useCommand(wrong,"ep1",data,local,accountB,true,1000);
        check(o.code==ExitPack,"a file that does not match its SHA-256 is refused (exit 3)");
        check(tree(data)==before,"and nothing changed");
        // the file's bytes changed after the manifest was made (same size)
        auto changed=entries(good,listing);changed[2].second[100]^=0x40;auto damaged=root/"bad"/"bytes.zip";zip(damaged,changed);
        check(useCommand(damaged,"ep1",data,local,accountB,true,1000).code==ExitPack&&tree(data)==before,"changed bytes are refused");
        // a listed checkpoint whose bytes are something else (a profile), with the right hash
        std::vector<Fixture> s={{"ep1","T",{{"ep1/manualsave0/checkpoint.dat",Profile}}}};auto prof=root/"bad"/"profile.zip";zip(prof,entries(s,manifest(s)));
        check(useCommand(prof,"ep1",data,local,accountB,true,1000).code==ExitPack&&tree(data)==before,"a checkpoint that is not one is refused");
        // a linked save folder in the co-op account is never moved (it could pull an outside folder into the backups)
        auto outside=root/"outside";write(outside/"checkpoint.dat",A);fs::create_directory_symlink(outside,coop/"autosave9");
        check(useCommand(packZip,"ep1",data,local,accountB,true,1000).code==ExitFailed&&!fs::exists(data/"backups")&&read(outside/"checkpoint.dat")==A,"linked save folders are refused");
        fs::remove(coop/"autosave9");check(tree(data)==before,"and nothing changed");
    }
    {   // used
        auto o=useCommand(packZip,"ep1",data,local,accountB,true,1000);
        check(o.code==ExitOk,o.message.c_str());
        check(o.message.find("Continue in the game loads Episode 1 \xc3\xa9.")==0,"says what Continue loads");
        check(o.moved==1&&o.aside==data/"backups"/"before-test-save-1000","reports the backup");
        check(read(o.aside/"autosave5"/"checkpoint.dat")==A,"the co-op saves that were there are in the backup");
        check(!fs::exists(coop/"autosave5"),"and no longer in the way");
        auto first=read(coop/"manualsave0"/"checkpoint.dat"),second=read(coop/"autosave2"/"checkpoint.dat");
        check(first==B,"the checkpoint is re-signed for the player's account");
        check(saves::inspect(second).user==userB&&second==saves::resign(big(3000,4),userB),"every checkpoint is re-signed for the player's account");
        check(fs::last_write_time(coop/"manualsave0"/"checkpoint.dat")>=fs::last_write_time(coop/"autosave2"/"checkpoint.dat"),"Continue's checkpoint is written last");
        check(read(coop/"profile"/"profile.dat")==Profile,"the profile stays");
        check(tree(local/"KojimaProductions")==ordinaryBefore,"the ordinary saves are never touched");
        auto again=useCommand(packZip,"late",data,local,accountB,true,1000);
        check(again.code==ExitOk&&again.aside==data/"backups"/"before-test-save-1000-2"&&again.moved==2,"a second use in the same second gets its own backup");
        check(read(again.aside/"manualsave0"/"checkpoint.dat")==B&&read(coop/"quicksave1"/"checkpoint.dat")==B&&!fs::exists(coop/"manualsave0"),"and replaces the first set");
        check(tree(local/"KojimaProductions")==ordinaryBefore,"the ordinary saves are still untouched");
    }
    if(argc==3) {   // a pack made by tools/make_test_saves.py
        auto made=Pack::open(argv[2]);auto set=made.find("ep1");
        check(set&&set->title=="Episode one"&&set->files.size()==2&&set->files[0].path=="ep1/manualsave0/checkpoint.dat","reads the builder's pack");
        auto d=root/"made"/"DeathStrandingCoop";
        auto o=useCommand(fs::path(argv[2]),"ep1",d,root/"made",accountB,true,5);
        check(o.code==ExitOk,o.message.c_str());
        auto account=coopAccount(d,root/"made",accountB);
        check(read(account/"manualsave0"/"checkpoint.dat")==B,"the builder's checkpoint is re-signed for the player");
        check(saves::inspect(read(account/"autosave2"/"checkpoint.dat")).user==userB,"all of them");
    }
    std::cout<<"test saves: pack, refusals and use pass\n";
    return 0;
}
