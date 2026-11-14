#include "story.h"
#include "../launcher/saves_vectors.h"
#include <cstdio>
#include <deque>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
using namespace mod::story;
namespace fs=std::filesystem;namespace saves=launcher::saves;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"story oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
#define VEC(v) std::string(v,sizeof(v)-1)
// A valid checkpoint of any size (as launcher/saves_test.cpp makes them): the vector's block followed by more bytes.
static std::string big(size_t extra,unsigned seed){
    std::string a=VEC(VectorA);size_t meta=std::stoul(a.substr(0,8)),picture=std::stoul(a.substr(8,8));
    std::string block=a.substr(24+meta+picture);std::mt19937 r(seed);for(size_t i=0;i<extra;++i)block+=char(r());
    std::string out;for(size_t v:{meta,picture,block.size()}){auto t=std::to_string(v);t.resize(8,'\0');out+=t;}
    return out+a.substr(24,meta+picture)+block;
}
static void write(const fs::path& p,const std::string& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(b.data(),std::streamsize(b.size()));}
static std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
static bool logged(const std::vector<std::string>& log,const char* part){for(auto& l:log)if(l.find(part)!=std::string::npos)return true;return false;}
template<class F> static bool throws(F f){try{f();}catch(const std::exception&){return true;}return false;}
static HostIn hin(uint64_t now,uint32_t episode,uint32_t completed,std::optional<Stamp> newest,bool inWorld=true){HostIn i;i.now=now;i.connected=true;i.inWorld=inWorld;i.episode=episode;i.completed=completed;i.newest=newest;return i;}
static GuestIn gin(uint64_t now,bool inWorld=true){GuestIn i;i.now=now;i.connected=true;i.inWorld=inWorld;i.porter=inWorld;return i;}
// One fetch over a link that loses, repeats and reorders datagrams. Returns the ticks it took, 0 when it never ended.
static unsigned transfer(Serve& serve,Fetch& fetch,unsigned seed,int lossPercent,Serve* later=nullptr,unsigned laterAt=0,unsigned limit=200000){
    std::mt19937 r(seed);std::deque<std::string> toHost,toGuest;
    auto carry=[&](std::deque<std::string>& q,const std::string& d){
        if(int(r()%100)<lossPercent)return;
        q.push_back(d);if(r()%10==0)q.push_back(d);                                        // a repeat
        if(q.size()>1&&r()%4==0)std::swap(q[q.size()-1],q[r()%q.size()]);                  // out of order
    };
    for(unsigned tick=1;tick<=limit;++tick){
        Serve& s=later&&tick>=laterAt?*later:serve;
        for(auto& d:fetch.poll(8))carry(toHost,d);
        while(!toHost.empty()){auto d=toHost.front();toHost.pop_front();auto a=s.answer(d);if(!a.empty())carry(toGuest,a);}
        while(!toGuest.empty()){auto d=toGuest.front();toGuest.pop_front();if(r()%50==0&&!d.empty())d[r()%d.size()]^=char(1<<(r()%8));fetch.take(d);}   // and damage
        if(fetch.done())return tick;
    }
    return 0;
}
int main(){
    const uint64_t session=0x1234567890abcdefull;
    // News on the ordered stream.
    { News n{Ready,7,5000000,0xfedcba98};auto e=encode(n);CHECK(e.type==WireType&&e.type<=ds::CargoTypeMax);
      auto d=decode(e);CHECK(d&&d->what==Ready&&d->generation==7&&d->size==5000000&&d->sum==0xfedcba98);
      auto bad=e;bad.item=9;CHECK(!decode(bad));bad=e;bad.ref&=~0xfffffffull;CHECK(!decode(bad));bad=e;bad.type=12;CHECK(!decode(bad));
      bad=e;bad.ref=(uint64_t(saves::MaxSave)+1)<<28|7;CHECK(!decode(bad));bad=e;bad.position[0]=1;CHECK(!decode(bad));bad=e;bad.id=1ull<<32;CHECK(!decode(bad));
      bad=e;bad.ref=7;CHECK(!decode(bad));                                            // nothing to fetch is no offer
      // What the wire does to an event must not touch the news: positions are held to +-100 km in centimetres (a
      // size carried there arrived as 100000), integers must stay under 2^53.
      CHECK(e.position[0]==0&&e.position[1]==0&&e.position[2]==0);
      { auto big=encode({Ready,(1u<<28)-1,uint32_t(saves::MaxSave),0xffffffffu});CHECK(big.id<(1ull<<53)&&big.ref<(1ull<<53));
        auto d2=decode(big);CHECK(d2&&d2->size==saves::MaxSave&&d2->sum==0xffffffffu&&d2->generation==(1u<<28)-1); }
      CHECK(decode(encode({Ask,1,0,0}))&&decode(encode({Loaded,3,0,0})));
      CHECK(secretOf(session)&&nonceOf(session,1)&&nonceOf(session,1)!=nonceOf(session,2)&&secretOf(session)!=secretOf(session+1)); }
    // Host: automatic. A story move asks the game for a checkpoint in the world only, and offers the new file only
    // once it has stopped changing.
    { Host h(true);Stamp old{"autosave0",100,4000};
      CHECK(!h.tick(hin(0,3,5,old)).save);                                               // the first look learns
      CHECK(!h.tick(hin(1000,3,5,old)).save);
      auto a=h.tick(hin(2000,3,6,old,false));CHECK(!a.save&&logged(a.log,"wanted"));     // completed, but in an event
      a=h.tick(hin(3000,3,6,old,false));CHECK(!a.save);
      { auto busy=hin(3500,3,6,old);busy.saveBusy=true;CHECK(!h.tick(busy).save); }      // the game is saving by itself
      a=h.tick(hin(4000,3,6,old));CHECK(a.save&&!a.offer);
      a=h.tick(hin(5000,3,6,old));CHECK(!a.save&&!a.offer);                              // the old file is not the answer
      Stamp part{"autosave1",200,1000},full{"autosave1",201,5000};
      a=h.tick(hin(6000,3,6,part));CHECK(!a.offer);
      a=h.tick(hin(7000,3,6,full));CHECK(!a.offer);                                      // still growing
      a=h.tick(hin(8500,3,6,full));CHECK(!a.offer);
      { auto busy=hin(9100,3,6,full);busy.saveBusy=true;CHECK(!h.tick(busy).offer); }
      a=h.tick(hin(9200,3,6,full));CHECK(a.offer&&a.generation==1&&h.generation()==1);
      CHECK(!h.tick(hin(9300,3,6,full)).save);
      // The episode changes right after: the next checkpoint waits for the gap.
      a=h.tick(hin(10000,4,6,full));CHECK(!a.save&&logged(a.log,"episode"));
      a=h.tick(hin(9200+Host::Gap-1,4,6,full));CHECK(!a.save);
      a=h.tick(hin(9200+Host::Gap,4,6,full));CHECK(a.save);
      // No checkpoint appears: tries again, then gives up.
      uint64_t t=9200+Host::Gap;unsigned saves_=1;
      for(int i=0;i<2000&&saves_<=Host::Tries+1;++i){t+=1000;auto o=h.tick(hin(t,4,6,full));if(o.save)++saves_;if(logged(o.log,"giving up"))break;}
      CHECK(saves_==Host::Tries);
      for(int i=0;i<200;++i){t+=1000;CHECK(!h.tick(hin(t,4,6,full)).save);}
      // An unknown episode is no move; a partner who leaves forgets what was pending.
      a=h.tick(hin(t+=1000,ds::UnknownProgress,6,full));CHECK(!a.save&&a.log.empty());
      a=h.tick(hin(t+=1000,4,6,full));CHECK(a.log.empty());
      a=h.tick(hin(t+=1000,4,7,full,false));CHECK(logged(a.log,"wanted"));
      HostIn off;off.now=t+=1000;h.tick(off);
      a=h.tick(hin(t+=Host::Gap,4,7,full));CHECK(!a.save);a=h.tick(hin(t+=1000,4,7,full));CHECK(!a.save); }
    // Host with the key only: story moves ask nothing, the partner's key does, once per save.
    { Host h(false);Stamp old{"autosave0",100,4000};h.tick(hin(0,3,5,old));
      CHECK(!h.tick(hin(1000,4,6,old)).save);
      auto ask=hin(2000,4,6,old);ask.asked=true;auto a=h.tick(ask);CHECK(a.save);
      ask.now=2500;a=h.tick(ask);CHECK(a.log.empty());                                   // asked again while saving
      Stamp full{"autosave1",201,5000};h.tick(hin(3000,4,6,full));a=h.tick(hin(3000+Host::Settle,4,6,full));CHECK(a.offer&&a.generation==1);
      for(uint64_t t=6000;t<6000+2*Host::Gap;t+=1000)CHECK(!h.tick(hin(t,4,6,full)).save); }
    // Guest, automatic: fetch, install in the world, load, back in the world; the same generation never twice.
    { Guest g(true);News ready{Ready,1,5000,77};
      auto i=gin(0);i.news={ready};auto o=g.tick(i);CHECK(o.fetch&&o.fetch->generation==1&&!o.install&&!o.notice);
      CHECK(!g.tick(gin(100)).install);
      i=gin(200,false);i.fetched=true;o=g.tick(i);CHECK(!o.install);                    // fetched during a cutscene: waits
      i=gin(300,false);i.fetched=true;o=g.tick(i);CHECK(!o.install&&!o.load);
      i=gin(400);i.fetched=true;o=g.tick(i);CHECK(o.install&&!o.load);g.installed(true);
      o=g.tick(gin(500,false));CHECK(!o.load);                                           // an event began: not now
      { auto busy=gin(550);busy.saveBusy=true;CHECK(!g.tick(busy).load); }             // the game is saving: not now
      o=g.tick(gin(600));CHECK(o.load);
      o=g.tick(gin(700));CHECK(!o.load&&!o.tell);
      { auto cut=gin(750,false);cut.porter=true;o=g.tick(cut);CHECK(o.log.empty()); }    // a cutscene is not the load
      o=g.tick(gin(800,false));CHECK(logged(o.log,"left the world"));
      i=gin(900,false);i.news={{Ready,2,6000,78}};o=g.tick(i);CHECK(!o.fetch);          // a newer one waits for this load
      o=g.tick(gin(20000));CHECK(o.tell&&o.tell->what==Loaded&&o.tell->generation==1&&g.loaded()==1);
      CHECK(o.fetch&&o.fetch->generation==2);
      i=gin(21000);i.news={ready};o=g.tick(i);CHECK(!o.fetch||o.fetch->generation!=1);  // an old one is not taken again
      i=gin(21500);i.news={{Ready,2,6000,78}};o=g.tick(i);CHECK(!o.fetch); }
    // Guest: the game does not start loading -> failed, and told; an install that fails ends it.
    { Guest g(true);auto i=gin(0);i.news={{Ready,1,5000,77}};g.tick(i);i=gin(100);i.fetched=true;CHECK(g.tick(i).install);g.installed(true);
      CHECK(g.tick(gin(200)).load);
      auto o=g.tick(gin(200+Guest::LeaveWait-1));CHECK(!o.tell);
      o=g.tick(gin(200+Guest::LeaveWait));CHECK(o.tell&&o.tell->what==Failed&&!g.busy()&&g.loaded()==0);
      Guest f(true);i=gin(0);i.news={{Ready,1,5000,77}};f.tick(i);i=gin(100);i.fetched=true;CHECK(f.tick(i).install);f.installed(false);
      CHECK(!f.busy()&&!f.tick(gin(200)).load);
      Guest r(true);i=gin(0);i.news={{Ready,1,5000,77}};r.tick(i);i=gin(100);i.fetched=true;r.tick(i);r.installed(true);CHECK(r.tick(gin(200)).load);
      o=r.refused(300);CHECK(o.tell&&o.tell->what==Failed&&!r.busy()); }
    // A checkpoint that never arrives whole is given up, and the host is told.
    { Guest g(true);auto i=gin(1000);i.news={{Ready,1,5000,77}};CHECK(g.tick(i).fetch);
      auto o=g.tick(gin(1000+Guest::FetchWait-1));CHECK(!o.tell&&g.busy());
      o=g.tick(gin(1000+Guest::FetchWait));CHECK(o.tell&&o.tell->what==Failed&&!g.busy());
      i=gin(2000+Guest::FetchWait);i.news={{Ready,2,5000,78}};CHECK(g.tick(i).fetch); }
    // Guest with the key: an offer is only told; the key fetches it. With nothing on offer the key asks the host,
    // not more often than every AskGap, and the answer is then fetched without another press.
    { Guest g(false);auto i=gin(0);i.news={{Ready,1,5000,77}};auto o=g.tick(i);CHECK(!o.fetch&&o.notice&&!g.busy());
      for(uint64_t t=100;t<5000;t+=100){o=g.tick(gin(t));CHECK(!o.fetch&&!o.install&&!o.load);}
      i=gin(5000);i.key=true;o=g.tick(i);CHECK(o.fetch&&o.fetch->generation==1&&!o.ask);
      Guest k(false);i=gin(0);i.key=true;o=k.tick(i);CHECK(o.ask&&!o.fetch);
      i=gin(1000);i.key=true;CHECK(!k.tick(i).ask);
      i=gin(Guest::AskGap);i.key=true;CHECK(k.tick(i).ask);
      i=gin(Guest::AskGap+500);i.news={{Ready,1,5000,77}};o=k.tick(i);CHECK(o.fetch&&!o.notice);
      // The partner leaves: what was on offer is dropped, and a new connection's generations start over.
      Guest d(false);i=gin(0);i.news={{Ready,4,5000,77}};d.tick(i);GuestIn off;off.now=100;o=d.tick(off);CHECK(logged(o.log,"dropped"));
      off.now=200;off.key=true;CHECK(!d.tick(off).ask);
      i=gin(300);i.key=true;CHECK(d.tick(i).ask);i=gin(400);i.news={{Ready,1,5000,77}};CHECK(d.tick(i).fetch); }
    // The bytes: every link gets the whole checkpoint or nothing, and only the checkpoint the news named.
    { unsigned worst=0;
      for(unsigned seed=1;seed<=60;++seed){
          std::mt19937 r(seed);auto file=big(1+r()%300000,seed);saves::Checkpoint c;c.folder="autosave3";c.bytes=file;
          Serve serve;serve.offer(session,seed,c);auto news=serve.news();CHECK(news.what==Ready&&news.size==file.size());
          Fetch fetch(session,news);unsigned ticks=transfer(serve,fetch,seed*7919,int(r()%45));
          CHECK(ticks&&fetch.done()&&fetch.bytes()==file);worst=std::max(worst,ticks);
          Fetch other(session+1,news);CHECK(!transfer(serve,other,seed,0,nullptr,0,300));       // another session gets nothing
      }
      std::printf("story transfer: 60 lossy links, slowest %u ticks\n",worst);
      // The host replaces the checkpoint in the middle: the old generation's fetch never completes with the new
      // file (another nonce); the fetch for the new one does.
      auto one=big(200000,1),two=big(200000,2);saves::Checkpoint a;a.bytes=one;saves::Checkpoint b;b.bytes=two;
      Serve first,second;first.offer(session,1,a);second.offer(session,2,b);
      Fetch stale(session,first.news());CHECK(!transfer(first,stale,5,10,&second,20,5000));CHECK(!stale.done());
      Fetch fresh(session,second.news());CHECK(transfer(second,fresh,6,10)&&fresh.bytes()==two);
      // A host that offers other bytes under the same generation than its news said: never done.
      News lie=second.news();lie.sum^=1;Fetch wrong(session,lie);CHECK(!transfer(second,wrong,7,0,nullptr,0,5000));
      Serve none;saves::Checkpoint empty;none.offer(session,3,empty);CHECK(none.news().what==0&&none.answer(std::string("DSS1\x01",5)+std::string(8,'\0')).empty()); }
    // Files: the one account of the copy's own tree; the host's checkpoint becomes manualsave0 for this account and
    // the earlier one is kept.
    { auto root=fs::temp_directory_path()/("ds-story-test-"+std::to_string(std::random_device{}()));fs::remove_all(root);
      auto modFolder=root/"redirect-data"/"mod";fs::create_directories(modFolder);
      CHECK(throws([&]{account(modFolder);}));
      auto game=root/"redirect-data"/"local"/"kojimaproductions"/"deathstrandingdc";const uint32_t id=39734272;
      auto mine=game/std::to_string(id);fs::create_directories(mine);
      auto acc=account(modFolder);CHECK(acc.id==id&&fs::equivalent(acc.folder,mine));
      CHECK(!newest(acc.folder));
      const std::string A=VEC(VectorA),userMine=saves::userForAccount(id);
      write(mine/"autosave0"/"checkpoint.dat",saves::resign(A,userMine));write(mine/"manualsave0"/"checkpoint.dat",saves::resign(big(100,3),userMine));
      write(mine/"notasave"/"checkpoint.dat","x");
      fs::last_write_time(mine/"autosave0"/"checkpoint.dat",fs::file_time_type::clock::now()+std::chrono::hours(1));
      auto n=newest(acc.folder);CHECK(n&&n->folder=="autosave0"&&n->size==A.size());
      auto before=read(mine/"manualsave0"/"checkpoint.dat"),autosave=read(mine/"autosave0"/"checkpoint.dat");
      auto host=big(5000,9);CHECK(saves::inspect(host).user!=userMine);
      install(acc,host);
      auto got=read(mine/"manualsave0"/"checkpoint.dat");CHECK(got.size()>=host.size()&&saves::inspect(got).checkpoint&&saves::inspect(got).user==userMine);
      CHECK(read(game/"story-backups"/(std::to_string(id)+"-0")/"checkpoint.dat")==before);   // kept, not deleted
      CHECK(read(mine/"autosave0"/"checkpoint.dat")==autosave);                          // the other saves stay
      install(acc,host);CHECK(!fs::exists(game/"story-backups"/(std::to_string(id)+"-1")));   // the same save again: nothing moves
      install(acc,big(6000,10));CHECK(read(game/"story-backups"/(std::to_string(id)+"-1")/"checkpoint.dat")==got);
      n=newest(acc.folder);CHECK(n&&n->folder=="autosave0");
      CHECK(throws([&]{install(acc,"not a save at all, not a save at all");}));
      CHECK(throws([&]{install(acc,VEC(VectorProfile));}));                              // a profile is not a checkpoint
      CHECK(throws([&]{Account zero=acc;zero.id=0;install(zero,host);}));
      fs::create_directories(game/"777");CHECK(throws([&]{account(modFolder);}));        // two accounts: which one is unknown
      fs::remove_all(root); }
    // The game's save state, read-only: no game at the title screen; a pending save and a running request tell "busy".
    { const uint64_t base=0x140000000ull,module=0x20000000,holder=0x30000000,manager=0x40000000;
      std::map<uint64_t,std::vector<uint8_t>> memory;bool deny=false;
      auto put=[&](uint64_t at,uint64_t v,size_t n){std::vector<uint8_t> b(n);std::memcpy(b.data(),&v,n);memory[at]=b;};
      mod::discovery::Sampler s([&](uint64_t a,void* p,size_t n){auto it=memory.find(a);if(deny||it==memory.end()||it->second.size()!=n)return false;std::memcpy(p,it->second.data(),n);return true;},base);
      auto st=readSaveState(s,base);CHECK(!st.readable&&!st.inGame);                    // nothing readable: unknown
      put(base+0x7be6318,0,8);st=readSaveState(s,base);CHECK(st.readable&&!st.inGame&&!st.requests&&!st.pending);
      put(base+0x7be6318,module,8);st=readSaveState(s,base);CHECK(!st.readable);          // a module whose fields cannot be read
      put(module+0x2a0+0x88,0,4);put(module+0x450,0,1);put(base+0x7c0d6b0,holder,8);put(holder+8,manager,8);put(manager+0xc8,0xff,1);
      st=readSaveState(s,base);CHECK(st.readable&&st.inGame&&!st.requests&&!st.pending&&st.pendingType==0xff);
      put(manager+0xc8,4,1);st=readSaveState(s,base);CHECK(st.pending&&st.pendingType==4);
      put(manager+0xc8,0xff,1);put(module+0x2a0+0x88,1,4);put(module+0x450,3,1);st=readSaveState(s,base);CHECK(st.requests&&!st.pending&&st.module==3);
      deny=true;CHECK(!readSaveState(s,base).readable); }
    if(!failures)std::puts("story resync checks passed");
    return failures?1:0;
}
