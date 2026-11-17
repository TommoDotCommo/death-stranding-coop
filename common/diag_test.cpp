#include "diag_log.h"
#include "../mod/bugmark.h"
#include "../mod/config.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool good,const char* why){if(!good)throw std::runtime_error(why);}
std::string read(const diag::fs::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
void put(const diag::fs::path& p,const std::string& text){std::ofstream(p,std::ios::binary)<<text;}
struct Provider:ds::StateProvider {
    bool readLocal(ds::State&)override{return true;}
    bool isInCutsceneOrLoading()const override{return false;}
    void applyRemote(const ds::Render&)override{}
};
struct Transport:ds::Transport {void send(const ds::Bytes&)override{}};
void wireTest() {
    Provider provider;Transport transport;ds::Core core({ds::Role::Host,42,100,120},provider,transport);core.tick(0);
    ds::Packet p;p.role=ds::Role::Client;p.kind=ds::Kind::Ack;p.session=42;p.nonce=200;p.echo=100;
    auto deliver=[&]{auto b=ds::encode(p);check(core.receive(b.data(),b.size(),100)==ds::Error::None,"wire receive");};
    deliver();p.kind=ds::Kind::Cargo;p.sequence=1;p.state.action=1;p.t0=123;p.t1=1;deliver();
    p.sequence=2;p.state.action=mod::bugmark::WireType;p.t0=17;p.t1=2;deliver();deliver();
    auto marks=core.takeCargo(mod::bugmark::WireType);check(marks.size()==1&&mod::bugmark::Decode(marks[0])==17,"mark delivered once");
    auto cargo=core.takeCargo();check(cargo.size()==1&&cargo[0].type==1&&cargo[0].item==123,"diagnostic leaves cargo queued");
    check(core.takeCargo(mod::bugmark::WireType).empty(),"no echoed mark");
}
int main()try{
    wireTest();
    unsigned hosted=0;const diag::Host host{&hosted,
        [](void* p,unsigned){++*static_cast<unsigned*>(p);return true;},
        [](void* p,const char*){++*static_cast<unsigned*>(p);return true;},
        [](void* p,const char*,uint64_t){++*static_cast<unsigned*>(p);},
        [](void* p,uint32_t,uint64_t){++*static_cast<unsigned*>(p);return true;},
        [](void* p){++*static_cast<unsigned*>(p);},
        [](void* p,bool){++*static_cast<unsigned*>(p);}};
    diag::Session bound;bound.Bind(&host);check(bound.Open({},"game",5),"proxy hosts open");
    bound.Line("raw");bound.Flight("presence");bound.Bugmark(1);bound.MarkCrashed();bound.Close();
    check(hosted==6,"all diagnostics hosted outside redirection");
    auto root=diag::fs::temp_directory_path()/("ds-diag-test-"+std::to_string(diag::Pid()));
    diag::fs::remove_all(root);diag::fs::create_directories(root);
#ifndef _WIN32
    diag::fs::create_directory(root/"outside");diag::fs::create_directory_symlink(root/"outside",root/"alias");
    diag::Session unsafe;check(!unsafe.Open(root/"alias"/"logs","world",5),"symlink path refused");
    check(!diag::fs::exists(root/"outside"/"logs"),"no writes through symlink");
#endif
    uint32_t chain=diag::Basis;
    auto line=diag::FormatLine(chain,0,123,"Hello");
    // Independently computed FNV-1a test vector, including spaces and case.
    uint32_t expected=diag::Basis;for(unsigned char c:std::string("0 123 Hello")){expected^=c;expected*=16777619u;}
    check(chain==expected&&line=="0 123 "+[] (uint32_t n){char b[9];std::snprintf(b,sizeof b,"%08x",n);return std::string(b);}(expected)+" Hello\n","chain format");
    auto before=chain;diag::FormatLine(chain,1,124,"hello");check(chain!=before,"continued chain");
    uint32_t changed=diag::Basis;diag::FormatLine(changed,0,123,"hello");check(changed!=expected,"case changes chain");
    auto dir=root/"prune";diag::fs::create_directory(dir);
    const std::string old="20260101-000000_game_p1",newer="20260102-000000_game_p2",crash="20250101-000000_game_p3",current="20260103-000000_game_p4";
    put(dir/(old+".log"),std::string(100,'a'));put(dir/(newer+".log"),std::string(100,'b'));
    put(dir/(crash+".log"),std::string(100,'c'));put(dir/(crash+".crash"),"crashed");put(dir/(crash+".dmp"),"dump");put(dir/(crash+".flight-1.txt"),"flight");
    put(dir/(current+".log"),std::string(100,'d'));put(dir/"foreign.txt",std::string(100,'x'));
    auto removed=diag::Prune(dir,320,current);
    check(removed==1&&!diag::fs::exists(dir/(old+".log"))&&diag::fs::exists(dir/(newer+".log")),"oldest first");
    diag::Prune(dir,0,current);
    for(auto ext:{".log",".crash",".dmp",".flight-1.txt"})check(diag::fs::exists(dir/(crash+ext)),"crashed run kept");
    check(diag::fs::exists(dir/(current+".log"))&&diag::fs::exists(dir/"foreign.txt"),"current and foreign kept");
    check(diag::KeyOf("20260101-000000_other_p1.log").empty()&&diag::KeyOf(old+".unrelated").empty(),"scope");
    auto logs=root/"logs";diag::Session session;
    check(session.Open(logs,"game",5,700),"open");
    const std::string raw="user=Porter email=porter@example.invalid secret=example-token IPv4=192.0.2.9 UTF8=\xc3\xa9";
    check(session.Line(raw),"raw line");
    session.Flight("expired presence",1000);session.Flight("PRESENCE fresh",61001);session.Flight("LINK fresh",61001);session.Flight("STATE fresh",61001);
    check(session.Bugmark(17,61001),"flight file");
    auto flight=read(diag::fs::path(session.Base()).concat(".flight-17.txt"));
    check(flight.find("expired")==std::string::npos&&flight.find("BUGMARK 17")!=std::string::npos&&flight.find("PRESENCE fresh")!=std::string::npos&&flight.find("LINK fresh")!=std::string::npos&&flight.find("STATE fresh")!=std::string::npos,"60-second flight");
    bool refused=false;for(unsigned i=0;i<30;++i)refused|=!session.Line(std::string(100,'x'));
    session.Flight(std::string(2000,'z'),61001);check(session.Bugmark(18,61001),"large flight saved");
    check(diag::fs::file_size(diag::fs::path(session.Base()).concat(".flight-18.txt"))<=700,"flight cap");
    diag::Prune(logs,0,"");
    check(diag::fs::exists(diag::fs::path(session.Base()).concat(".log"))&&diag::fs::exists(diag::fs::path(session.Base()).concat(".flight-17.txt")),"active run and flight retained");
    session.Close();auto logPath=diag::fs::path(session.Base()).concat(".log");auto log=read(logPath);
    check(refused&&diag::fs::file_size(logPath)<=700,"active cap");
    check(log.find(raw)!=std::string::npos,"no redaction");
    check(log.find("session end: clean exit")!=std::string::npos,"clean exit");
    check(log.find("BUGMARK 17")!=std::string::npos,"bugmark in log");
    diag::Session collision;check(!collision.Open(logs,"game",5),"exclusive creation");
    diag::Session crashed;check(crashed.Open(logs,"proxy",5),"crash session");crashed.MarkCrashed();crashed.Close();
    check(diag::fs::exists(diag::fs::path(crashed.Base()).concat(".crash")),"marker");
    check(read(diag::fs::path(crashed.Base()).concat(".log")).find("clean exit")==std::string::npos,"crash not clean");
    mod::bugmark::Button button;
    check(!button.press(false,0),"idle key");auto mark=button.press(true,0);check(mark&&*mark==1,"F2 edge");
    check(!button.press(true,0),"held F2 fires once");button.press(false,0);auto second=button.press(true,0);check(second&&*second==2,"next press");
    auto event=mod::bugmark::Encode(*mark);check(event.type<=ds::CargoTypeMax&&mod::bugmark::Decode(event)==mark,"wire mark");
    event.item=0;check(!mod::bugmark::Decode(event),"invalid mark");
    auto good=std::string(R"({"role":"host","peer":"127.0.0.1","slot":0,"peer_slot":1,"nickname":"test","provider":"synthetic","session":123)");
    check(mod::parseConfig(good+"}").maxLogMb==200,"default cap");
    for(auto n:{5,200,2000})check(mod::parseConfig(good+",\"max_log_mb\":"+std::to_string(n)+"}").maxLogMb==unsigned(n),"configured cap");
    for(auto n:{"0","4","2001","-1","true","\"200\"","2.5"}){bool bad=false;try{mod::parseConfig(good+",\"max_log_mb\":"+n+"}");}catch(...){bad=true;}check(bad,"invalid cap refused");}
    for(auto key:{"passenger_key","story_key"}){bool bad=false;try{mod::parseConfig(good+",\""+key+"\":113}");}catch(...){bad=true;}check(bad,"F2 reserved");}
    diag::fs::remove_all(root);std::cout<<"diagnostic oracles passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"oracle: "<<e.what()<<'\n';return 1;}
