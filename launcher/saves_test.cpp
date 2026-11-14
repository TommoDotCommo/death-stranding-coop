// Game-free oracle for saves.{h,cpp}: the file format against vectors from an independent implementation, the folder
// operations in a temporary tree, and the transfer over a link that loses, repeats, reorders and damages datagrams.
#include "saves.h"
#include "saves_vectors.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
namespace fs=std::filesystem;
using namespace launcher::saves;
static void check(bool b,const char* what) {if(!b){std::cerr<<"launcher oracle: "<<what<<'\n';std::exit(1);}}
template<class F> static bool throws(F f) {try{f();}catch(const std::exception&){return true;}return false;}
#define VEC(v) std::string(v,sizeof(v)-1)
static void write(const fs::path& p,const std::string& b) {fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(b.data(),b.size());}
static std::string read(const fs::path& p) {std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
// A valid checkpoint of any size: the vector's header and picture, its block followed by `extra` bytes.
static std::string big(size_t extra,unsigned seed) {
    std::string a=VEC(VectorA);size_t meta=std::stoul(a.substr(0,8)),picture=std::stoul(a.substr(8,8));
    std::string block=a.substr(24+meta+picture);std::mt19937 r(seed);for(size_t i=0;i<extra;++i)block+=char(r());
    std::string out;for(size_t v:{meta,picture,block.size()}){auto t=std::to_string(v);t.resize(8,'\0');out+=t;}
    return out+a.substr(24,meta+picture)+block;
}
int main(int argc,char** argv) {
    check(argc==2,"temporary test root required");fs::path root=argv[1];
    const std::string A=VEC(VectorA),B=VEC(VectorB),Short=VEC(VectorShort),Profile=VEC(VectorProfile);
    const std::string userA="steam_76561197960265729",userB="steam_76561198000000000";const uint32_t accountB=39734272;

    {   // the file
        auto i=inspect(A);
        check(i.checkpoint&&i.user==userA,"reads the account a checkpoint belongs to");
        check(i.title=="Order No. 7: \xc3\xa9t\xc3\xa9","decodes the title");
        check(resign(A,userB)==B,"re-signs like the reference");
        check(resign(A,"steam_9")==Short,"re-signs to a shorter id like the reference");
        check(resign(B,userA)==A&&resign(Short,userA)==A,"re-signing back restores the file");
        check(inspect(B).user==userB,"the re-signed file names the new account");
        auto p=inspect(Profile);check(!p.checkpoint&&p.user==userA,"reads a profile");
        check(userForAccount(1)==userA&&userForAccount(accountB)==userB,"account number to id");
        check(crc32("123456789")==0xCBF43926u,"checksum");
        for(size_t n:{size_t(0),size_t(10),size_t(23),size_t(40),A.size()-1})check(throws([&]{inspect(A.substr(0,n));}),"refuses a cut file");
        {auto x=A;x[0]='x';check(throws([&]{inspect(x);}),"refuses a bad size field");}
        {auto x=Profile;x[0]^=1;check(throws([&]{inspect(x);}),"refuses a wrong magic");}
        check(throws([&]{resign(A,"");})&&throws([&]{resign(A,std::string(300,'a'));}),"refuses an impossible id");
        check(saveFolder("autosave12")&&saveFolder("ManualSave0")&&saveFolder("quicksave3"),"save folder names");
        for(auto bad:{"manualsave","manualsave0.moved-20261108","save1","..","autosave12345","profile","manualsave-1","autosave1/x"})check(!saveFolder(bad),"not a save folder");
        check(big(5000,1).size()==A.size()+5000&&inspect(big(5000,1)).checkpoint,"test builder makes valid files");
        std::mt19937 r(7);
        for(int k=0;k<20000;++k){std::string junk(r()%64,'\0');for(auto& c:junk)c=char(r());if(r()%4==0)junk=A.substr(0,r()%A.size())+junk;try{inspect(junk);resign(junk,userB);}catch(const std::exception&){}}
    }
    {   // folders: newest, install
        auto game=root/"local"/"kojimaproductions"/"deathstrandingdc";auto account=game/"123";
        write(account/"autosave1"/"checkpoint.dat",A);write(account/"manualsave2"/"checkpoint.dat",big(100,2));
        write(account/"profile"/"profile.dat",Profile);write(account/"autosave7.moved-20261108"/"checkpoint.dat",A);
        fs::create_directories(game/"_coop_probe");fs::create_directories(game/"0");
        fs::last_write_time(account/"autosave1"/"checkpoint.dat",fs::file_time_type::clock::now()-std::chrono::hours(2));
        check(gameFolder(root/"local")==game,"finds the game's folder whatever its letter case");
        check(accounts(game)==std::vector<uint32_t>{123},"lists account folders only");
        auto n=newest(account);check(n&&n->folder=="manualsave2"&&n->bytes==big(100,2)&&n->title=="Order No. 7: \xc3\xa9t\xc3\xa9","offers the newest checkpoint");
        check(!newest(root/"nothing"),"no checkpoint, nothing to offer");
        {auto broken=game/"555";write(broken/"autosave1"/"checkpoint.dat",A);write(broken/"autosave2"/"checkpoint.dat","garbage");
         fs::last_write_time(broken/"autosave1"/"checkpoint.dat",fs::file_time_type::clock::now()-std::chrono::hours(2));
         auto b=newest(broken);check(b&&b->folder=="autosave1","a damaged newest file is passed over");fs::remove_all(broken);}
        auto aside=root/"backups"/"before-join-1";
        check(throws([&]{install(account,0,A,aside);}),"needs the account");
        check(throws([&]{install(account,accountB,Profile,aside);})&&throws([&]{install(account,accountB,"junk",aside);}),"installs checkpoints only");
        check(read(account/"autosave1"/"checkpoint.dat")==A&&!fs::exists(aside),"a refused install changes nothing");
        check(install(account,accountB,A,aside)==2,"moves the saves that were there");
        check(read(account/"manualsave0"/"checkpoint.dat")==B,"the installed save is re-signed for this account");
        check(read(aside/"autosave1"/"checkpoint.dat")==A&&read(aside/"manualsave2"/"checkpoint.dat")==big(100,2),"the earlier saves are kept aside");
        check(!fs::exists(account/"autosave1")&&!fs::exists(account/"manualsave2"),"only the host's save is left to continue");
        check(read(account/"profile"/"profile.dat")==Profile&&fs::exists(account/"autosave7.moved-20261108"),"the profile and other folders stay");
        check(install(account,accountB,A,root/"backups"/"before-join-2")==-1&&!fs::exists(root/"backups"/"before-join-2"),"the same save again changes nothing");
        auto fresh=game/"456";check(install(fresh,accountB,A,root/"backups"/"x")==0&&read(fresh/"manualsave0"/"checkpoint.dat")==B,"installs into an account folder that does not exist yet");
        // a linked save folder is never moved (it could pull an outside folder into the backups)
        auto outside=root/"outside";write(outside/"checkpoint.dat",A);auto linkAccount=game/"789";fs::create_directories(linkAccount);
        fs::create_directory_symlink(outside,linkAccount/"autosave4");
        check(throws([&]{install(linkAccount,accountB,A,root/"backups"/"y");}),"refuses linked save folders");
        check(read(outside/"checkpoint.dat")==A&&!fs::exists(linkAccount/"manualsave0"),"and leaves everything as it was");
    }
    {   // copying the ordinary saves in
        auto from=root/"real"/"KojimaProductions"/"DeathStrandingDC"/"123",to=root/"coop"/"kojimaproductions"/"deathstrandingdc"/"123";
        write(from/"manualsave1"/"checkpoint.dat",A);write(from/"autosave3"/"checkpoint.dat","garbage");write(from/"notes"/"checkpoint.dat",A);
        write(from/"profile"/"bindings.cfg","keys");write(from/"profile"/"profile.dat",Profile);
        write(to/"autosave9"/"checkpoint.dat",B);
        auto aside=root/"backups"/"before-import-1";
        check(throws([&]{importOrdinary(root/"real"/"none",to,aside);})&&throws([&]{importOrdinary(from,from,aside);}),"needs ordinary saves, from elsewhere");
        check(importOrdinary(from,to,aside)==1,"copies the valid checkpoints");
        check(read(to/"manualsave1"/"checkpoint.dat")==A&&!fs::exists(to/"autosave3")&&!fs::exists(to/"notes"),"copies checkpoints only");
        check(read(aside/"autosave9"/"checkpoint.dat")==B&&!fs::exists(to/"autosave9"),"keeps the co-op saves that were there aside");
        check(read(to/"profile"/"bindings.cfg")=="keys"&&read(to/"profile"/"profile.dat")==Profile,"copies the settings when there were none");
        check(read(from/"manualsave1"/"checkpoint.dat")==A&&read(from/"autosave3"/"checkpoint.dat")=="garbage"&&read(from/"profile"/"bindings.cfg")=="keys","the ordinary saves are untouched");
        write(to/"profile"/"bindings.cfg","mine");
        check(importOrdinary(from,to,root/"backups"/"before-import-2")==1&&read(to/"profile"/"bindings.cfg")=="mine","existing co-op settings are kept");
        check(read(root/"backups"/"before-import-2"/"manualsave1"/"checkpoint.dat")==A,"a second copy keeps the first aside");
    }
    {   // the transfer
        const uint32_t secret=0x1234abcd,nonce=77;
        Sender none;none.offer(std::nullopt);Receiver r0;
        auto q=r0.poll(secret,nonce,true,8);check(q.size()==1,"asks what the host has");
        check(none.answer(q[0],secret+1,nonce).empty()&&none.answer(q[0],secret,nonce+1).empty(),"another lobby's request gets no answer");
        {Receiver z;check(z.poll(0,nonce,true,8).empty(),"no request before the lobby secret is known");}
        r0.take(none.answer(q[0],secret,nonce),secret,nonce);
        check(r0.offered&&!r0.size&&!r0.done&&r0.poll(secret,nonce,true,8).empty(),"a host without a save offers nothing");
        for(size_t extra:{size_t(0),size_t(734),size_t(1024*3-A.size()),size_t(300000)}) {
            Checkpoint c{"manualsave3",big(extra,unsigned(extra)),"T\xc3\xa9"};Sender host;host.offer(c);Receiver guest;
            std::mt19937 rnd(unsigned(extra)+5);bool damaged=false;int rounds=0;
            check(guest.poll(secret,nonce,false,8).size()==1,"asks first");
            for(;!guest.done&&rounds<20000;++rounds) {
                auto out=guest.poll(secret,nonce,rounds>3,32);std::vector<std::string> back;
                for(auto& d:out){if(rnd()%10<3)continue;int copies=rnd()%10==0?2:1;for(int k=0;k<copies;++k){auto a=host.answer(d,secret,nonce);if(!a.empty())back.push_back(a);}}
                std::shuffle(back.begin(),back.end(),rnd);
                for(auto& d:back){if(rnd()%10<3)continue;
                    if(!damaged&&d.size()>40&&uint8_t(d[4])==4){damaged=true;auto x=d;x[x.size()-1]^=0x55;guest.take(x,secret,nonce);continue;}
                    {auto wrong=d;wrong[5]^=1;guest.take(wrong,secret,nonce);}   // wrong secret: ignored
                    guest.take(d,secret,nonce);if(rnd()%10==0)guest.take(d,secret,nonce);}
                if(rounds==2)check(guest.offered&&guest.size==c.bytes.size()&&guest.title==c.title&&!guest.done&&guest.percent()==0,"nothing is fetched until it is wanted");
            }
            check(guest.done&&guest.bytes()==c.bytes&&guest.percent()==100,"the save arrives whole through loss, repeats, reordering and damage");
            check(guest.poll(secret,nonce,true,8).empty(),"and then nothing more is asked");
        }
        {   // hostile or broken hosts
            Receiver g;auto offer=[&](uint32_t size,uint32_t crc,const std::string& title){std::string b("DSS1\x02",5);for(uint32_t v:{secret,nonce,size,crc})for(int k=0;k<4;++k)b+=char(v>>(8*k));b+=char(title.size());b+=title;return b;};
            g.take(offer(uint32_t(MaxSave)+1,1,"x"),secret,nonce);check(!g.offered,"an oversized offer is ignored");
            g.take(offer(2000,1,std::string(200,'a')),secret,nonce);check(!g.offered,"an overlong title is ignored");
            g.take(offer(2000,5,"a\x01" "b"),secret,nonce);check(g.offered&&g.title=="a b","control characters never reach the window");
            auto chunk=[&](uint32_t index,size_t len){std::string b("DSS1\x04",5);for(uint32_t v:{secret,nonce,uint32_t(5),index})for(int k=0;k<4;++k)b+=char(v>>(8*k));b.append(len,'z');return b;};
            g.take(chunk(2,10),secret,nonce);g.take(chunk(0,1000),secret,nonce);g.take(chunk(1,1024),secret,nonce);check(g.percent()==0,"chunks of the wrong place or size are ignored");
            g.take(chunk(0,1024),secret,nonce);g.take(chunk(1,976),secret,nonce);check(!g.done&&g.percent()==0,"a file that fails its check is fetched again, never handed over");
            std::mt19937 r(9);Sender host;host.offer(Checkpoint{"manualsave0",big(3000,3),"t"});
            for(int k=0;k<20000;++k){std::string junk(r()%40,'\0');for(auto& c:junk)c=char(r());if(r()%2)junk.replace(0,std::min<size_t>(junk.size(),5),std::string("DSS1")+char(1+r()%4),0,std::min<size_t>(junk.size(),5));g.take(junk,secret,nonce);host.answer(junk,secret,nonce);}
        }
    }
    std::cout<<"saves: file format, folders and transfer pass\n";
    return 0;
}
