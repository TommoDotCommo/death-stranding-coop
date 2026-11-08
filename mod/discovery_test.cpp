#include "fake_memory.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
using namespace mod::discovery;
using namespace fakemem;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class F> void rejected(F f,const char* oracle){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,oracle);}
int main(int argc,char** argv){
    try {
        std::string which=argc>1?argv[1]:"all";
        auto run=[&](const char* name,auto f){if(which=="all"||which==name)f();};
        run("clean",[]{Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);check(m.signatures.size()==6,"six signatures");Options o;o.roots["player"]=0x3800;auto s=m.sample(100,o);check(s.find("\"position_c8\":null")==std::string::npos,"position present");check(s.find("\"coherent\":true")!=std::string::npos,"coherent");});
        run("hash",[]{Fake f;auto m=f.sampler();rejected([&]{m.scan("wrong",PinnedHash);},"hash oracle");check(f.calls==0,"hash-before-read oracle");rejected([&]{m.scan(PinnedHash,"wrong");},"expected hash oracle");});
        run("unmapped",[]{Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);f.blocked=f.base+0x60c8;Options o;o.roots["player"]=0x3800;check(m.sample(1,o).find("\"position_c8\":null")!=std::string::npos,"unmapped oracle");});
        run("torn",[]{Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);f.torn=true;unsigned char b[16];std::memset(b,42,16);
            check(!m.read(f.base+0x4000,b,16)&&b[0]==42,"torn oracle");
            // One aligned value of up to 8 bytes cannot tear: it is read once, and a change between samples is just data.
            uint64_t v=7;check(m.read(f.base+0x4000,&v,8),"aligned single read");unsigned char one=0;check(m.read(f.base+0x4001,&one,1),"byte single read");
            uint32_t odd=0;check(!m.read(f.base+0x4001,&odd,4),"unaligned value still double-read");});
        run("short",[]{Fake f;auto m=f.sampler();f.deny=true;unsigned char b=42;check(!m.read(f.base,&b,1)&&b==42,"short read oracle");
            unsigned char w[16];std::memset(w,42,16);check(!m.read(f.base,w,16)&&w[0]==42,"short read oracle");});
        run("null",[]{Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);f.put<uint64_t>(0x3800,0);Options o;o.roots["player"]=0x3800;check(m.sample(1,o).find("\"position_c8\":null")!=std::string::npos,"null chain oracle");rejected([&]{m.pointer(f.base+0x3800);},"null pointer oracle");});
        run("duplicate",[]{Fake f;memcpy(f.data.data()+0x1800,f.data.data()+0x1100,32);f.put<int32_t>(0x1807,0x3000-0x180b);f.put<int32_t>(0x181a,0x3000-0x181e);auto m=f.sampler();rejected([&]{m.scan(PinnedHash,PinnedHash);},"duplicate oracle");});
        run("zero",[]{Fake f;f.data[0x1300]=0;auto m=f.sampler();rejected([&]{m.scan(PinnedHash,PinnedHash);},"zero oracle");});
        run("rip",[]{Fake f;f.put<int32_t>(0x131a,0x1800-0x131e);auto m=f.sampler();rejected([&]{m.scan(PinnedHash,PinnedHash);},"RIP target oracle");});
        run("rip_unmapped",[]{Fake f;f.blocked=f.base+0x3000;auto m=f.sampler();rejected([&]{m.scan(PinnedHash,PinnedHash);},"RIP readability oracle");});
        run("root_section",[]{Fake f;f.put<uint64_t>(0x1800,f.base+0x5000);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);Options o;o.roots["player"]=0x1800;check(m.sample(1,o).find("\"position_c8\":null")!=std::string::npos,"root section oracle");});
        run("section",[]{Fake f;f.put<uint32_t>(0x98+56,0x8000);auto m=f.sampler();rejected([&]{m.scan(PinnedHash,PinnedHash);},"section bounds oracle");});
        run("overflow",[]{Fake f;auto m=f.sampler();unsigned char b;check(!m.read(UINT64_MAX,&b,2)&&f.calls==0,"overflow oracle");});
        run("save_count",[]{Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);f.put<int32_t>(0x4200+0x328,65);f.put<uint64_t>(0x4200+0x330,f.base+0x8000);for(unsigned i=0;i<65;++i)f.put<uint64_t>(0x8000+i*8,f.base+0x9000);check(m.sample(1,{}).find("\"save_array_invalid\":null")!=std::string::npos,"save count oracle");});
        run("chain",[]{Fake f;bool changed=false;auto m=Sampler([&](uint64_t a,void* p,size_t n){bool ok=f.read(a,p,n);if(a==f.base+0x60c8){f.put<uint64_t>(0x5070,f.base+0x7000);changed=true;}return ok;},f.base);m.scan(PinnedHash,PinnedHash);Options o;o.roots["player"]=0x3800;check(m.sample(1,o).find("\"coherent\":false")!=std::string::npos&&changed,"chain recheck oracle");});
        // H1 fast path: selection target of signature 1 is fake offset 0x3100 (k=1); the record starts 4 bytes before it.
        auto selection=[](Fake& f,int32_t index,int32_t mode,int32_t kind,uint64_t other){f.put<int32_t>(0x30fc,index);f.put<int32_t>(0x3100,mode);f.put<int32_t>(0x3104,kind);f.put<uint64_t>(0x310c,other);f.put<uint64_t>(0x4000+0x48+8*2,f.base+0x5000);};
        run("selection",[&]{Fake f;selection(f,2,1,2,0);auto m=f.sampler();m.scan(PinnedHash,PinnedHash);auto s=m.sample(1,{});
            check(s.find("\"position_c8\":null")==std::string::npos,"selection oracle: fast path finds the player without a configured slot");check(s.find("\"coherent\":true")!=std::string::npos,"selection oracle: coherent");
            for(auto bad:{std::array<int64_t,4>{2,0,2,0},{2,1,3,0},{2,1,2,1},{-1,1,2,0},{64,1,2,0}}){Fake g;selection(g,int32_t(bad[0]),int32_t(bad[1]),int32_t(bad[2]),uint64_t(bad[3]));auto n=g.sampler();n.scan(PinnedHash,PinnedHash);check(n.sample(1,{}).find("\"position_c8\":null")!=std::string::npos,"selection oracle: slow path or bad index is not followed");}
            Fake h;selection(h,0,1,2,0);auto q=h.sampler();q.scan(PinnedHash,PinnedHash);check(q.sample(1,{}).find("\"position_c8\":null")!=std::string::npos,"selection oracle: the index selects the slot");});
        run("log_cap",[]{Budget b{100};check(b.append(90)&&!b.append(11)&&b.bytes==90,"log cap oracle");});
        run("rate_cap",[]{Budget b{100};check(b.sample(0)&&!b.sample(1)&&b.sample(50000)&&!b.sample(49999),"rate cap oracle");});
        run("kill",[]{auto dir=std::filesystem::temp_directory_path()/"discovery-test-stop";std::filesystem::create_directory(dir);check(stopRequested(dir),"kill switch oracle");std::filesystem::remove(dir);check(!stopRequested(dir),"absent kill switch oracle");});
        if(which=="dump"){Fake f;auto m=f.sampler();m.scan(PinnedHash,PinnedHash);Options o;o.roots["player"]=0x3800;std::cout<<m.sample(1,o)<<'\n';}
        else std::cout<<"discovery fake-memory checks passed: "<<which<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
