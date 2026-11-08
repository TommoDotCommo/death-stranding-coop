#include "discovery.h"
#include "timing.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif
namespace mod::discovery {
#include "discovery_signatures.h"
namespace {
void require(bool b,const char* s) {if(!b) throw std::runtime_error(s);}
uint64_t add(uint64_t a,uint64_t b) {require(b<=UINT64_MAX-a,"address overflow");return a+b;}
template<class T> T load(const Sampler& m,uint64_t a) {T v{};require(m.read(a,&v,sizeof v),"unreadable or torn memory");return v;}
std::string hex(const unsigned char* p,size_t n) {std::ostringstream o;o<<std::hex<<std::setfill('0');for(size_t i=0;i<n;++i)o<<std::setw(2)<<unsigned(p[i]);return o.str();}
std::string address(uint64_t p) {std::ostringstream o;o<<"0x"<<std::hex<<p;return o.str();}
// A region always includes raw bytes, numeric alternatives, and errors as null.
std::string block(const Sampler& m,uint64_t a,size_t n) {
    std::vector<unsigned char> b(n);if(!a||!m.read(a,b.data(),n))return "null";
    std::ostringstream o;o<<"{\"address\":\""<<address(a)<<"\",\"hex\":\""<<hex(b.data(),n)<<"\"";
    auto floats=[&](const char* name,size_t width){o<<",\""<<name<<"\":[";for(size_t i=0;i+width<=n;i+=width) {if(i)o<<',';double d;if(width==4){float f;memcpy(&f,b.data()+i,4);d=f;}else memcpy(&d,b.data()+i,8);if(std::isfinite(d))o<<std::setprecision(17)<<d;else o<<"null";}o<<']';};
    floats("f32",4);floats("f64",8);
    if(n>=24) {
        o<<",\"split_f32x4_f64\":[";
        for(unsigned i=0;i<4;++i){float f;memcpy(&f,b.data()+i*4,4);if(i)o<<',';if(std::isfinite(f))o<<std::setprecision(17)<<f;else o<<"null";}
        double d;memcpy(&d,b.data()+16,8);o<<',';if(std::isfinite(d))o<<std::setprecision(17)<<d;else o<<"null";o<<']';
    }
    o<<",\"u32\":[";for(size_t i=0;i+4<=n;i+=4){uint32_t v;memcpy(&v,b.data()+i,4);o<<(i?",":"")<<v;}o<<']';
    if(n>=24)o<<",\"split16_8\":[\""<<hex(b.data(),16)<<"\",\""<<hex(b.data()+16,8)<<"\"]";
    o<<'}';return o.str();
}
}
bool Sampler::read(uint64_t a,void* out,size_t n) const {
    if(!a||!n||n>128*1024*1024||n>UINT64_MAX-a)return false;
    // Never expose a partially copied buffer. Two reads reject observed tearing;
    // this is NOT an atomic engine snapshot or a lifetime/type guarantee.
    std::vector<unsigned char> x(n),y(n);
    if(!read_(a,x.data(),n)||!read_(a,y.data(),n)||x!=y)return false;
    memcpy(out,x.data(),n);return true;
}
bool Sampler::readOnce(uint64_t a,void* out,size_t n) const {
    if(!a||!n||n>128*1024*1024||n>UINT64_MAX-a)return false;
    return read_(a,out,n);
}
bool Sampler::region(uint64_t a,size_t n,bool nonExecutable) const {
    if(!n||n>UINT64_MAX-a)return false;
    for(auto s:sections)if(a>=s.address&&a-s.address<s.size&&n<=s.size-(a-s.address)&&(!nonExecutable||!s.executable))return true;
    return false;
}
uint64_t Sampler::pointer(uint64_t a) const {auto p=load<uint64_t>(*this,a);require(p&&p<0x0000800000000000ULL,"null/noncanonical chain");return p;}
void Sampler::scan(const std::string& actual,const std::string& expected) {
    // Identity gate precedes even DOS header reads.
    require(expected==PinnedHash&&actual==expected,"DISCOVERY REFUSED: executable SHA-256 mismatch");
    require(load<uint16_t>(*this,base)==0x5a4d,"MZ header");
    auto peoff=load<uint32_t>(*this,add(base,0x3c));require(peoff<=1024*1024,"PE offset cap");auto pe=add(base,peoff);
    require(load<uint32_t>(*this,pe)==0x4550&&load<uint16_t>(*this,add(pe,4))==0x8664,"AMD64 PE header");
    auto count=load<uint16_t>(*this,add(pe,6)),opt=load<uint16_t>(*this,add(pe,20));
    require(count>0&&count<=96&&opt>=112&&opt<=4096&&load<uint16_t>(*this,add(pe,24))==0x20b,"PE32+ section bounds");
    auto imageSize=load<uint32_t>(*this,add(pe,24+56));require(imageSize>4096&&imageSize<=512*1024*1024,"image size cap");
    sections.clear();signatures.clear();auto table=add(pe,24+opt);
    for(unsigned i=0;i<count;++i) {
        auto h=add(table,i*40);auto vs=load<uint32_t>(*this,add(h,8)),rva=load<uint32_t>(*this,add(h,12)),raw=load<uint32_t>(*this,add(h,16));
        auto size=std::max(vs,raw);require(size&&rva<imageSize&&size<=imageSize-rva,"section outside image");
        Section s{add(base,rva),size,bool(load<uint32_t>(*this,add(h,36))&0x20000000)};
        for(auto prev:sections)require(s.address>=add(prev.address,prev.size)||prev.address>=add(s.address,s.size),"overlapping sections");
        sections.push_back(s);
    }
    // Snapshot executable sections once; every copy uses the guarded reader.
    std::vector<std::pair<Section,std::vector<unsigned char>>> code;
    for(auto s:sections)if(s.executable) {require(s.size<=128*1024*1024,"code size cap");std::vector<unsigned char> b(s.size);require(read(s.address,b.data(),b.size()),"unreadable code section");code.emplace_back(s,std::move(b));}
    for(auto c:candidates()) {
        std::vector<int> pattern;std::istringstream tokens(c.pattern);std::string t;
        while(tokens>>t)pattern.push_back(t=="??"?-1:std::stoi(t,nullptr,16));
        require(!pattern.empty()&&pattern.size()<=32,"pattern bounds");
        for(auto r:c.refs) {require(r.length>=6&&r.instruction+r.length<=pattern.size(),"RIP bounds");for(size_t j=r.instruction+r.length-4;j<r.instruction+r.length;++j)require(pattern[j]==-1,"RIP mask");}
        size_t matches=0;Signature result{c.name,0,{}};
        for(auto& pair:code) {auto& b=pair.second;if(b.size()<pattern.size())continue;
            for(size_t i=0;i<=b.size()-pattern.size();++i) {
                bool ok=true;for(size_t j=0;j<pattern.size();++j)if(pattern[j]!=-1&&pattern[j]!=b[i+j]){ok=false;break;}
                if(!ok)continue;
                ++matches;require(matches==1,"duplicate signature");result.match=add(pair.first.address,i);
                for(auto r:c.refs) {auto end=i+r.instruction+r.length;require((b[end-5]&0xc7)==5,"RIP ModRM");int32_t d;memcpy(&d,b.data()+end-4,4);
                    auto ip=add(pair.first.address,end);uint64_t target;
                    if(d<0){require(ip>=uint64_t(-int64_t(d)),"negative RIP target");target=ip-uint64_t(-int64_t(d));}else target=add(ip,d);
                    require(region(target,8,true),"RIP target outside non-executable section");uint64_t probe;require(read(target,&probe,sizeof probe),"RIP target unreadable");result.targets[r.name]=target;
                }
            }
        }
        require(matches>0,"zero signature matches");signatures.push_back(result);
    }
    require(signatures[0].targets.at("player_manager_global")==signatures[1].targets.at("player_manager_global"),"manager signatures disagree");
}
Sampler::Selection Sampler::selectPlayer(uint64_t manager) const {
    Selection r;
    if(signatures.size()!=6){r.reason="sampler not validated";return r;}
    if(!manager){r.reason="manager null";return r;}
    auto sel=signatures[1].targets.at("player_selection_state");
    if(sel<4){r.reason="selection unreadable";return r;}
    try {
        auto index=load<int32_t>(*this,sel-4),mode=load<int32_t>(*this,sel),kind=load<int32_t>(*this,add(sel,4));
        auto other=load<uint64_t>(*this,add(sel,0xc));
        if(mode==1&&kind==2&&other==0&&index>=0&&index<64) {
            auto slot=add(manager,0x48+8*uint64_t(index));
            uint64_t p=0;
            if(!read(slot,&p,sizeof p)){r.reason="player slot unreadable";return r;}
            if(!p||p>=0x0000800000000000ULL){r.reason="player null";return r;}
            r.slot=slot;r.player=p;
        } else r.reason=(mode==1&&kind==2&&other==0)?"bad index":"slow selection path";
    }catch(const std::exception&){r.slot=0;r.player=0;r.reason="selection unreadable";}
    return r;
}
std::string Sampler::sample(uint64_t us,const Options& options) const {
    require(signatures.size()==6,"sampler not validated");
    std::ostringstream o;o<<"{\"type\":\"sample\",\"t_us\":"<<us<<",\"regions\":{";bool first=true;
    auto emit=[&](std::string name,uint64_t a,size_t n){o<<(first?"":",")<<'"'<<name<<"\":"<<block(*this,a,n);first=false;};
    std::map<std::string,uint64_t> roots;
    auto root=[&](const std::string& name,uint64_t slot){emit(name+"_slot",slot,8);try{roots[name]=pointer(slot);}catch(const std::exception&){roots[name]=0;}return roots[name];};
    auto global=[&](unsigned sig,const char* key,const char* name,size_t n){auto p=root(name,signatures[sig].targets.at(key));emit(name,p,n);return p;};
    global(0,"player_manager_global","manager",256);
    auto save=global(3,"save_dialog_manager_global","save_manager",0x338);
    global(4,"baggage_related_global","baggage",0x18);
    global(5,"cutscene_sound_global","cutscene_sound",64);
    for(auto name:{"player","worldnode","controller","animation","vehicle","bb","velocity","angular_velocity"}) {
        auto i=options.roots.find(name);uint64_t p=0;
        if(i!=options.roots.end()&&i->second&&i->second<=UINT64_MAX-base&&region(base+i->second,8,true))p=root(name,base+i->second);
        roots[name]=p;
    }
    auto field=[&](const char* name,uint64_t p,uint64_t offset,size_t n){emit(name,p&&offset<=UINT64_MAX-p?p+offset:0,n);};
    // H1 fast path, read from Player_sExportedGetLocalPlayer_impl (0x23ca2fc in the pinned build): the selection record
    // starts 4 bytes before the signature's selection target: int32 index (-4), int32 mode (+0) == 1, int32 kind (+4) == 2,
    // pointer (+0xc) == null; then player = *(manager + 0x48 + 8*index). Any other state is the slow path (a list walk with
    // per-entry flags) and is not followed: the player stays null and the selection bytes are logged. An explicit
    // discovery_player_rva still wins.
    uint64_t player_slot=0;
    {   auto sel=signatures[1].targets.at("player_selection_state");
        emit("selection",sel>=4?sel-4:0,24);
        if(!roots["player"]&&roots["manager"]) {
            auto chosen=selectPlayer(roots["manager"]);
            if(chosen.player) {player_slot=chosen.slot;roots["player"]=chosen.player;}
        }
        emit("player_from_selection_slot",player_slot,8);
    }
    auto player=roots["player"];emit("player",player,0x80);field("local_flags",player,0x78,2);
    uint64_t entity=0;try{if(player)entity=pointer(add(player,0x70));}catch(const std::exception&){}
    emit("entity_slot",player?add(player,0x70):0,8);
    field("position_c8",entity,0xc8,24);field("position_d8",entity,0xd8,24);field("world_transform",entity,0xc8,60);
    field("velocity_object_slots",entity,0xb0,16);
    emit("velocity",roots["velocity"],16);emit("angular_velocity",roots["angular_velocity"],16);
    field("worldnode_position",roots["worldnode"],0x20,24);field("worldnode_rotation",roots["worldnode"],0x38,36);
    field("move",roots["controller"],0x14c,8);
    std::vector<std::pair<uint64_t,uint64_t>> nested;
    auto follow=[&](uint64_t slot){auto p=pointer(slot);nested.emplace_back(slot,p);return p;};
    for(auto item:{std::pair<const char*,uint64_t>{"velocity_primary",0xb0},{"velocity_fallback",0xb8}}) {
        uint64_t object=0,vtable=0;
        try{if(entity)object=follow(add(entity,item.second));if(object)vtable=follow(object);}catch(const std::exception&){}
        emit(item.first,object,64);
        field((std::string(item.first)+"_linear_vtable_slot").c_str(),vtable,item.second==0xb0?0xc8:0x130,8);
        if(item.second==0xb0)field("angular_vtable_slot",vtable,0xd8,8);
    }
    uint64_t cargo=0,record=0;
    try{if(player)cargo=follow(add(player,0x54d0));if(cargo)record=follow(add(cargo,0x50));}catch(const std::exception&){}
    emit("cargo_owner_candidate",cargo,0x58);field("cargo_id_candidate",record,0xc8,4);
    auto anim=roots["animation"];field("animation_records_slot",anim,0x58,8);
    uint64_t records=0;try{if(anim)records=follow(add(anim,0x58));}catch(const std::exception&){}
    for(unsigned i=0;i<std::min(options.animationSlots,32u);++i) {auto name="animation_"+std::to_string(i);field(name.c_str(),records,0x20+24*i,4);}
    field("vehicle_4cc",roots["vehicle"],0x4cc,4);field("vehicle_63c",roots["vehicle"],0x63c,4);field("vehicle_528",roots["vehicle"],0x528,8);
    field("bb",roots["bb"],0x7844,16);
    // H13 array length is signed and bounded. Never iterate an unchecked count.
    if(save)try {
        auto n=load<int32_t>(*this,add(save,0x328));require(n>=0&&n<=64,"save array count cap");
        if(n){auto a=follow(add(save,0x330));for(int i=0;i<n;++i){uint64_t entry=follow(add(a,i*8));field(("save_entry_"+std::to_string(i)).c_str(),entry,0xc,4);}}
    }catch(const std::exception&) {emit("save_array_invalid",0,4);}
    // Recheck pointer slots after sampling. A changed chain invalidates the whole
    // sample rather than presenting data as one coherent observation.
    bool coherent=true;
    for(auto& kv:roots) {uint64_t slot=0;if(kv.first=="manager")slot=signatures[0].targets.at("player_manager_global");else if(kv.first=="save_manager")slot=signatures[3].targets.at("save_dialog_manager_global");else if(kv.first=="baggage")slot=signatures[4].targets.at("baggage_related_global");else if(kv.first=="cutscene_sound")slot=signatures[5].targets.at("cutscene_sound_global");else {auto i=options.roots.find(kv.first);if(i!=options.roots.end()&&i->second&&i->second<=UINT64_MAX-base)slot=base+i->second;}
        if(slot&&kv.second)try{coherent&=pointer(slot)==kv.second;}catch(const std::exception&){coherent=false;}
    }
    for(auto link:nested)try{coherent&=pointer(link.first)==link.second;}catch(const std::exception&){coherent=false;}
    if(player_slot&&player)try{coherent&=pointer(player_slot)==player;}catch(const std::exception&){coherent=false;}
    if(player&&entity)try{coherent&=pointer(add(player,0x70))==entity;}catch(const std::exception&){coherent=false;}
    o<<"},\"coherent\":"<<(coherent?"true":"false")<<",\"unknown\":[\"player type/lifetime and the slow selection path\",\"velocity virtual return values\",\"animation bounds/names\",\"vehicle composite predicate\",\"BB owner\",\"cutscene audio function result\",\"visual cutscene/loading gate\"]}";
    return o.str();
}
#ifdef _WIN32
namespace {
// ReadProcessMemory is a kernel-guarded copy: races after VirtualQuery fail as
// an error/short read instead of causing an in-process access violation (MinGW
// does not support MSVC __try). Never dereference any game pointer in C++.
bool windowsRead(uint64_t a,void* out,size_t n) {
    if(!a||n>UINT64_MAX-a)return false;
    uint64_t end=a+n;
    for(uint64_t p=a;p<end;) {MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        auto protection=m.Protect&0xff;if(protection!=PAGE_READONLY&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY&&protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;
        auto start=reinterpret_cast<uint64_t>(m.BaseAddress);if(m.RegionSize>UINT64_MAX-start||start+m.RegionSize<=p)return false;p=std::min(end,start+m.RegionSize);
    }
    SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),out,n,&copied)&&copied==n;
}
std::string fileHash(const std::filesystem::path& path) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(f!=INVALID_HANDLE_VALUE,"cannot open host executable read-only");
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::string result;
    try {require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"SHA256 provider");require(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)==0,"SHA256 init");
        std::array<unsigned char,65536> b;DWORD n;
        for(;;){require(ReadFile(f,b.data(),b.size(),&n,nullptr),"host executable read failed");if(!n)break;require(BCryptHashData(hash,b.data(),n,0)==0,"SHA256 update");}
        std::array<unsigned char,32> digest;require(BCryptFinishHash(hash,digest.data(),digest.size(),0)==0,"SHA256 finish");result=hex(digest.data(),digest.size());
    }catch(...){if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);throw;}
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);return result;
}
}
#endif
std::unique_ptr<Sampler> openHostSampler(const std::string& expected) {
#ifndef _WIN32
    (void)expected;throw std::runtime_error("game provider requires Windows; host tests use fake memory only");
#else
    std::array<wchar_t,32768> path{};auto len=GetModuleFileNameW(nullptr,path.data(),path.size());require(len&&len<path.size(),"host module path failed");
    auto hash=fileHash(std::filesystem::path(path.data()));
    auto m=std::make_unique<Sampler>(windowsRead,reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)));m->scan(hash,expected);return m;
#endif
}
bool Budget::append(size_t n) {
    if(n>limit-bytes)return false;
    bytes+=n;return true;
}
bool Budget::sample(uint64_t us) {
    if(started&&(us<last||us-last<50000))return false;
    started=true;last=us;return true;
}
bool stopRequested(const std::filesystem::path& path) {
    std::error_code error;bool exists=std::filesystem::exists(path,error);
    require(!error,"cannot inspect discovery stop file");return exists;
}
Worker::Worker(const std::filesystem::path& config,Options options) {
    require(options.hz>=1&&options.hz<=20&&options.maxMiB>=1&&options.maxMiB<=64&&options.animationSlots<=32,"discovery budget bounds");
    require(options.sha256==PinnedHash,"DISCOVERY REFUSED: config must provide pinned profile executable SHA-256");
    // Fixed layout prevents JSON from choosing an output or kill-switch path.
    auto dir=config.parent_path();require(dir.filename()=="mod"&&dir.parent_path().filename()=="redirect-data","discovery config must be in instance/redirect-data/mod");
#ifndef _WIN32
    (void)options;throw std::runtime_error("live discovery requires Windows; host tests use fake memory only");
#else
    thread_=std::thread([this,dir,options]{
        HANDLE log=INVALID_HANDLE_VALUE;
        try {
            auto output=dir/("discovery-"+mod::runStamp()+".jsonl");
            log=CreateFileW(output.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(log!=INVALID_HANDLE_VALUE,"cannot exclusively create discovery log");
            Budget budget{uint64_t(options.maxMiB)*1024*1024};
            auto write=[&](std::string line){line+='\n';require(budget.append(line.size()),"discovery log size cap reached");DWORD n=0;require(WriteFile(log,line.data(),DWORD(line.size()),&n,nullptr)&&n==line.size()&&FlushFileBuffers(log),"discovery log write failed");};
            auto kill=dir.parent_path().parent_path()/"discovery.stop";
            if(stopRequested(kill))throw std::runtime_error("discovery stop file exists");
            std::array<wchar_t,32768> path{};auto len=GetModuleFileNameW(nullptr,path.data(),path.size());require(len&&len<path.size(),"host module path failed");
            auto hash=fileHash(std::filesystem::path(path.data()));
            Sampler m(windowsRead,reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)));m.scan(hash,options.sha256);
            std::ostringstream h;h<<"{\"type\":\"header\",\"schema\":1,\"sha256\":\""<<hash<<"\",\"module_base\":\""<<address(m.base)<<"\",\"hz\":"<<options.hz<<",\"max_mib\":"<<options.maxMiB<<",\"signatures\":[";
            bool first=true;for(auto& s:m.signatures){h<<(first?"":",")<<"{\"name\":\""<<s.name<<"\",\"match_count\":1,\"match\":\""<<address(s.match)<<"\",\"targets\":{";first=false;bool rfirst=true;for(auto& r:s.targets){h<<(rfirst?"":",")<<'"'<<r.first<<"\":\""<<address(r.second)<<'"';rfirst=false;}h<<"}}";}h<<"],\"roots_rva\":{";first=true;for(auto& r:options.roots){h<<(first?"":",")<<'"'<<r.first<<"\":\""<<address(r.second)<<'"';first=false;}h<<"},\"animation_slots\":"<<options.animationSlots<<",\"consistency\":\"double-read and chain recheck; non-atomic\"}";write(h.str());
            auto start=std::chrono::steady_clock::now();
            while(!stop_) {
                if(stopRequested(kill))break;
                auto now=std::chrono::steady_clock::now();auto us=std::chrono::duration_cast<std::chrono::microseconds>(now-start).count();
                require(budget.sample(us),"discovery sample rate cap");write(m.sample(us,options));
                // Relative delay after each sample: never catch up with bursts.
                auto next=std::chrono::steady_clock::now()+std::chrono::microseconds(1000000/options.hz);
                while(!stop_&&std::chrono::steady_clock::now()<next)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }catch(const std::exception& e){failed_=true;std::string message=std::string("DISCOVERY REFUSED/STOPPED: ")+e.what()+"\n";std::cerr<<message;OutputDebugStringA(message.c_str());
            // Fixed failure messages carry the exact reason without exceeding the cap.
            if(log!=INVALID_HANDLE_VALUE){std::string record=std::string("{\"type\":\"error\",\"reason\":\"")+e.what()+"\"}\n";const char* s=record.c_str();DWORD n; // reserve is not needed: error never extends a full log
                auto size=GetFileSize(log,nullptr);if(size!=INVALID_FILE_SIZE&&size+strlen(s)<=uint64_t(options.maxMiB)*1024*1024)WriteFile(log,s,DWORD(strlen(s)),&n,nullptr);}
        }
        if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
    });
#endif
}
Worker::~Worker(){stop_=true;if(thread_.joinable())thread_.join();}
}
