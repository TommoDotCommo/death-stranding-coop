#include "story.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace fs=std::filesystem;
// launcher/saves.cpp writes its files through this; the mod links that file without the launcher's core.
namespace launcher {
void atomicWrite(const fs::path& path,const std::string& bytes){
    fs::create_directories(path.parent_path());
    auto temp=path;temp+=".tmp";std::error_code ec;fs::remove(temp,ec);
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.exceptions(std::ios::badbit|std::ios::failbit);f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();}
    fs::rename(temp,path);
}
}
namespace mod::story {
namespace saves=launcher::saves;
// The news on the wire. An event's position is metres, sent as centimetres held to +-100 km, so a size put there
// arrived as 100000 and the fetch never completed; the integers must each stay under 2^53. So: the checksum in id, and
// the size above 28 bits of generation in ref (16 MiB << 28 is 2^52).
constexpr unsigned GenerationBits=28;
ds::CargoEvent encode(const News& n){
    ds::CargoEvent e;e.type=WireType;e.item=n.what;e.id=n.sum;
    e.ref=uint64_t(n.size)<<GenerationBits|(n.generation&((1u<<GenerationBits)-1));return e;
}
std::optional<News> decode(const ds::CargoEvent& e){
    uint64_t generation=e.ref&((1u<<GenerationBits)-1),size=e.ref>>GenerationBits;
    if(e.type!=WireType||e.item<Ready||e.item>Failed||!generation||e.id>0xffffffffull)return std::nullopt;
#ifndef MUTANT_STORY_SIZE_BOUND
    if(size>saves::MaxSave)return std::nullopt;
#endif
    for(double v:e.position)if(v!=0)return std::nullopt;
    uint64_t sum=e.id;
    News n;n.what=e.item;n.generation=uint32_t(generation);n.size=uint32_t(size);n.sum=uint32_t(sum);
#ifndef MUTANT_STORY_EMPTY
    if(n.what==Ready&&!n.size)return std::nullopt;
#endif
    return n;
}
static uint64_t mix(uint64_t x){x^=x>>33;x*=0xff51afd7ed558ccdull;x^=x>>33;x*=0xc4ceb9fe1a85ec53ull;x^=x>>33;return x;}
uint32_t secretOf(uint64_t session){uint32_t s=uint32_t(mix(session^0x53544f5259ull));return s?s:1;}
uint32_t nonceOf(uint64_t session,uint32_t generation){
#ifdef MUTANT_STORY_NONCE
    (void)generation;uint32_t n=uint32_t(mix(session)>>32);
#else
    uint32_t n=uint32_t(mix(session+0x9e3779b97f4a7c15ull*generation)>>32);
#endif
    return n?n:1;
}

HostOut Host::tick(const HostIn& in){
    HostOut out;
    if(!in.connected){primed_=false;pending_=false;tries_=0;if(state_==State::Saving)state_=State::Idle;return out;}
    bool episodeMoved=primed_&&in.episode!=ds::UnknownProgress&&episode_!=ds::UnknownProgress&&in.episode!=episode_;
    bool orderDone=primed_&&in.completed>completed_;
    if(in.episode!=ds::UnknownProgress)episode_=in.episode;
    completed_=in.completed;primed_=true;
    bool asked=in.asked&&(state_==State::Idle||episodeMoved||orderDone);   // a save is being made already: that one answers
    const char* why=asked?"the partner asked":episodeMoved?"the episode changed":orderDone?"a story order was completed":nullptr;
#ifndef MUTANT_STORY_UNASKED
    if(!automatic_&&!asked)why=nullptr;
#endif
    if(why){
        if(!pending_&&state_==State::Idle)out.log.push_back(std::string("STORY checkpoint wanted: ")+why);
        pending_=true;tries_=0;why_=why;
    }
    if(state_==State::Idle){
        bool gap=!saved_||in.now-savedAt_>=Gap;
#ifdef MUTANT_STORY_GAP
        gap=true;
#endif
        bool quiet=in.inWorld&&!in.saveBusy;
#ifdef MUTANT_STORY_EVENT
        quiet=true;
#endif
        if(pending_&&gap&&quiet){
            pending_=false;++tries_;state_=State::Saving;since_=in.now;before_=in.newest;candidate_.reset();saved_=true;savedAt_=in.now;
            out.save=true;out.log.push_back("STORY asking the game for a checkpoint ("+why_+")");
        }
        return out;
    }
    bool fresh=in.newest&&(!before_||!(*in.newest==*before_));
#ifdef MUTANT_STORY_STALE
    fresh=bool(in.newest);
#endif
    if(fresh){
        if(!candidate_||!(*candidate_==*in.newest)){candidate_=in.newest;stableSince_=in.now;}
        bool settled=in.now-stableSince_>=Settle&&!in.saveBusy;
#ifdef MUTANT_STORY_HASTY
        settled=true;
#endif
        if(settled){
            state_=State::Idle;savedAt_=in.now;offered_=true;out.offer=true;out.generation=++generation_;
            out.log.push_back("STORY checkpoint written: "+candidate_->folder+", "+std::to_string(candidate_->size)+" bytes, generation "+std::to_string(generation_));
            return out;
        }
    }
    if(in.now-since_>=SaveWait){
        state_=State::Idle;savedAt_=in.now;
        bool again=tries_<Tries;
        out.log.push_back(std::string("STORY no checkpoint appeared in ")+std::to_string(SaveWait/1000)+" s (the game does not allow a save here?)"+(again?"; trying again":"; giving up until the story moves again"));
        if(again)pending_=true;
    }
    return out;
}
void Serve::offer(uint64_t session,uint32_t generation,const saves::Checkpoint& c){
    tx_.offer(c);secret_=secretOf(session);nonce_=nonceOf(session,generation);
    news_={};
    if(tx_.offering()){news_.what=Ready;news_.generation=generation;news_.size=uint32_t(c.bytes.size());news_.sum=saves::crc32(c.bytes);}
}
std::string Serve::answer(const std::string& datagram) const {
    if(!tx_.offering())return {};
    return tx_.answer(datagram,secret_,nonce_);
}
bool Fetch::done(){
    if(!rx_.done){verdict_=0;return false;}
#ifdef MUTANT_STORY_SUM
    return true;
#else
    if(!verdict_)verdict_=rx_.size==want_.size&&saves::crc32(rx_.bytes())==want_.sum?1:-1;
    return verdict_==1;
#endif
}

void Guest::take(const News& n,GuestOut& out){
    if(n.what!=Ready)return;
#ifndef MUTANT_STORY_OLD
    if(n.generation<=loaded_||(state_!=State::Idle&&n.generation<=offer_.generation))return;
#endif
    if(state_==State::Installed||state_==State::Loading||state_==State::Returning){next_=n;return;}   // after this load
    offer_=n;
    bool go=automatic_||asked_;
#ifdef MUTANT_STORY_KEYLESS
    go=true;
#endif
    if(go){state_=State::Fetching;fetchStarted_=false;out.fetch=n;out.log.push_back("STORY fetching the partner's checkpoint, generation "+std::to_string(n.generation)+", "+std::to_string(n.size)+" bytes");}
    else{state_=State::Offered;out.notice=true;out.log.push_back("STORY the partner's story moved on: checkpoint generation "+std::to_string(n.generation)+" is on offer (catch-up key loads it)");}
}
GuestOut Guest::tick(const GuestIn& in){
    GuestOut out;
    // The host's generations count from one in every process of the host: a new connection starts over.
    if(linked_&&!in.connected&&state_!=State::Installed&&state_!=State::Loading&&state_!=State::Returning){loaded_=0;offer_={};}
    linked_=in.connected;
    if(!in.connected&&(state_==State::Offered||state_==State::Fetching||state_==State::Fetched)){
        state_=State::Idle;asked_=false;out.log.push_back("STORY the partner left: the checkpoint on offer is dropped");
    }
    if(in.connected)for(auto& n:in.news)take(n,out);
    if(in.key&&in.connected){
        if(state_==State::Offered){state_=State::Fetching;fetchStarted_=false;out.fetch=offer_;out.log.push_back("STORY catch-up key: fetching generation "+std::to_string(offer_.generation));}
        else if(state_==State::Idle){
            bool gap=!everAsked_||in.now-askedAt_>=AskGap;
#ifdef MUTANT_STORY_ASK_FLOOD
            gap=true;
#endif
            if(gap){everAsked_=true;askedAt_=in.now;asked_=true;out.ask=true;out.log.push_back("STORY catch-up key: asking the partner for a checkpoint");}
        }
    }
    if(state_==State::Fetching&&!fetchStarted_){fetchStarted_=true;fetchSince_=in.now;}
    if(state_==State::Fetching&&in.fetched&&!out.fetch){state_=State::Fetched;out.log.push_back("STORY checkpoint fetched");}
#ifndef MUTANT_STORY_FETCH_FOREVER
    else if(state_==State::Fetching&&in.now-fetchSince_>=FetchWait){
        state_=State::Idle;asked_=false;News t=offer_;t.what=Failed;out.tell=t;out.fetch.reset();
        out.log.push_back("STORY the checkpoint did not arrive within "+std::to_string(FetchWait/1000)+" s: given up");
    }
#endif
    switch(state_){
    case State::Fetched:
        if(in.inWorld)out.install=true;        // the caller installs now and answers with installed()
        break;
    case State::Installed: {
        bool quiet=in.inWorld&&in.porter&&!in.saveBusy;
#ifdef MUTANT_STORY_BUSY_LOAD
        quiet=true;
#endif
        if(quiet){state_=State::Loading;since_=in.now;out.load=true;out.log.push_back("STORY asking the game to load the partner's checkpoint");}
        break; }
    case State::Loading:
        // Only a porter that cannot be read means the world is gone: a cutscene that starts by itself is no load.
#ifndef MUTANT_STORY_EVENT_LEFT
        if(!in.porter)
#else
        if(!in.inWorld)
#endif
            {state_=State::Returning;since_=in.now;out.log.push_back("STORY the game left the world: loading");}
        else if(in.now-since_>=LeaveWait){
            state_=State::Idle;asked_=false;News t=offer_;t.what=Failed;out.tell=t;
            out.log.push_back("STORY the game did not start loading within "+std::to_string(LeaveWait/1000)+" s: load refused");
        }
        break;
    case State::Returning:
        if(in.inWorld&&in.porter){
            state_=State::Idle;asked_=false;loaded_=offer_.generation;News t=offer_;t.what=Loaded;out.tell=t;
            out.log.push_back("STORY loaded the partner's checkpoint, generation "+std::to_string(loaded_)+", after "+std::to_string((in.now-since_)/1000)+" s");
            if(next_){auto n=*next_;next_.reset();if(in.connected)take(n,out);}
        }
        else if(in.now-since_>=ReturnWait){state_=State::Idle;asked_=false;out.log.push_back("STORY the porter did not come back after the load");}
        break;
    default:break;
    }
    return out;
}
GuestOut Guest::refused(uint64_t){
    GuestOut out;if(state_!=State::Loading)return out;
    state_=State::Idle;asked_=false;News t=offer_;t.what=Failed;out.tell=t;return out;
}
void Guest::installed(bool ok){
    if(state_!=State::Fetched)return;
    if(ok)state_=State::Installed;else{state_=State::Idle;asked_=false;}
}

Account account(const fs::path& modFolder){
    auto game=saves::gameFolder(modFolder.parent_path()/"local");
    auto ids=saves::accounts(game);
    if(ids.empty())throw std::runtime_error("no account folder in the copy's save tree");
    if(ids.size()!=1)throw std::runtime_error("more than one account folder in the copy's save tree");
    return {game/std::to_string(ids[0]),ids[0]};
}
std::optional<Stamp> newest(const fs::path& account){
    std::optional<Stamp> best;std::error_code ec;
    for(fs::directory_iterator it(account,ec),end;!ec&&it!=end;it.increment(ec)){
        auto name=it->path().filename().string();if(!saves::saveFolder(name))continue;
        auto file=it->path()/"checkpoint.dat";std::error_code e;
        if(fs::is_symlink(fs::symlink_status(it->path(),e))||fs::is_symlink(fs::symlink_status(file,e))||!fs::is_regular_file(file,e))continue;
        auto t=fs::last_write_time(file,e);if(e)continue;auto size=fs::file_size(file,e);if(e)continue;
        Stamp s{name,int64_t(t.time_since_epoch().count()),uint64_t(size)};
        if(!best||s.time>best->time)best=s;
    }
    return best;
}
static std::string slurp(const fs::path& p){
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read a save file");
    std::string b((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());return b;
}
void install(const Account& a,const std::string& hostFile){
    if(!a.id)throw std::runtime_error("account unknown");
    if(!saves::inspect(hostFile).checkpoint)throw std::runtime_error("not a checkpoint");
    auto mine=saves::resign(hostFile,saves::userForAccount(a.id));
    std::error_code ec;
    for(auto p=a.folder;!p.empty()&&p!=p.root_path();p=p.parent_path())
        if(fs::is_symlink(fs::symlink_status(p,ec)))throw std::runtime_error("refusing a linked save folder");
    if(!fs::is_directory(a.folder,ec))throw std::runtime_error("no account folder");
    auto slot=a.folder/saves::InstallFolder,file=slot/"checkpoint.dat";
    if(fs::is_symlink(fs::symlink_status(slot,ec))||fs::is_symlink(fs::symlink_status(file,ec)))throw std::runtime_error("refusing a linked save folder");
    if(fs::is_regular_file(file,ec)){
        if(slurp(file)==mine)return;
#ifndef MUTANT_STORY_KEEP
        auto aside=a.folder.parent_path()/"story-backups";fs::create_directories(aside);
        for(unsigned n=0;;++n){auto to=aside/(std::to_string(a.id)+"-"+std::to_string(n));if(!fs::exists(to,ec)){fs::rename(slot,to);break;}if(n>100000)throw std::runtime_error("too many story backups");}
#else
        fs::remove_all(slot);
#endif
    }
    launcher::atomicWrite(file,mine);
    fs::last_write_time(file,fs::file_time_type::clock::now(),ec);
}

// The game. GameModule G = *(image+0x7be6318) (null at the title screen); its save system is G+0x2a0 with the count of
// requests at +0x88; G+0x450 is the module's state byte (3 while a restore runs). The DS save manager is
// M = *(*(image+0x7c0d6b0)+8); M+0xc8 is the type of the save it is waiting to make (0xff: none).
SaveState readSaveState(const discovery::Sampler& s,uint64_t base){
    SaveState out;uint64_t module=0,holder=0,manager=0;int32_t count=0;
    if(!s.read(base+0x7be6318,&module,8))return out;
    out.readable=true;if(!module)return out;
    out.inGame=true;
    if(!s.read(module+0x2a0+0x88,&count,4)||!s.read(module+0x450,&out.module,1)){out.readable=false;return out;}
    out.requests=count>0;
    if(s.read(base+0x7c0d6b0,&holder,8)&&holder&&s.read(holder+8,&manager,8)&&manager&&s.read(manager+0xc8,&out.pendingType,1))out.pending=out.pendingType!=0xff;
    return out;
}
#ifdef _WIN32
namespace {
struct Site{uint64_t rva;uint8_t bytes[16];};
void pinned(discovery::Sampler& s,const Site* sites,size_t n,const char* what){
    for(size_t i=0;i<n;++i){uint8_t got[16]{};if(!s.read(s.base+sites[i].rva,got,16)||std::memcmp(got,sites[i].bytes,16))throw std::runtime_error(what);}
}
}
#endif
std::function<void()> nativeSave(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native save API requires Windows");
#else
    // The node DSCreateSaveGame (0x365c560) -> its common body (0x365c770) -> the DS save manager's request
    // (0x3411580 -> 0x3411590). The manager makes the save by itself once no "unsave reason" holds.
    const Site sites[]={{0x365c560,{0x48,0x83,0xec,0x58,0x48,0x8d,0x44,0x24,0x40,0x48,0xc7,0x44,0x24,0x40,0x00,0x00}},
                        {0x365c770,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57}},
                        {0x3411580,{0x48,0x8b,0x49,0x08,0xe9,0x07,0x00,0x00,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
                        {0x3411590,{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24}}};
    pinned(s,sites,4,"save request function differs");
    const uint64_t base=s.base;auto* sp=&s;
    return [base,sp]{
        uint64_t module=0,holder=0;
        if(!sp->read(base+0x7be6318,&module,8)||!module||!sp->read(base+0x7c0d6b0,&holder,8)||!holder)return;
        // (type 4 = Automatic, three flag words 0, no demo flag, no activity): the game's own autosave, next slot.
        reinterpret_cast<void(*)(uint8_t,uint16_t,uint16_t,uint16_t,uint8_t,void*)>(base+0x365c560)(4,0,0,0,0,nullptr);
    };
#endif
}
std::function<int(uint8_t,uint8_t)> nativeLoad(discovery::Sampler& s){
#ifndef _WIN32
    (void)s;throw std::runtime_error("native load API requires Windows");
#else
    // The save dialog's "load this slot" while a game runs (0x3668f00): checks the type and slot against their enums,
    // that the save exists (0x26376e0) and asks the application to restore it (0x2636990 -> the save system's
    // restore request 0x362d750 with reason Manual).
    const Site sites[]={{0x3668f00,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x23,0x81,0x8f,0x01,0x48,0x33,0xc4}},
                        {0x2636990,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x40,0x41}},
                        {0x362d750,{0x48,0x89,0x5c,0x24,0x20,0x88,0x54,0x24,0x10,0x55,0x57,0x41,0x54,0x41,0x55,0x41}},
                        {0x26376e0,{0x48,0x83,0xec,0x68,0x48,0x8b,0x05,0x45,0x99,0x92,0x02,0x48,0x33,0xc4,0x48,0x89}}};
    pinned(s,sites,4,"load request function differs");
    const uint64_t base=s.base;auto* sp=&s;
    return [base,sp](uint8_t type,uint8_t slot)->int{
        uint64_t module=0;
        if(!sp->read(base+0x7be6318,&module,8)||!module)return 0;                    // no game running
        if((type!=1&&type!=2&&type!=4)||slot>22)return 0;                             // a named type and a numbered slot only
        auto* app=reinterpret_cast<void*>(base+0x7be6330);
        if(!reinterpret_cast<uint8_t(*)(void*,uint8_t,uint8_t)>(base+0x26376e0)(app,type,slot))return 1;   // the game does not list that save
        reinterpret_cast<void(*)(void*,uint8_t,uint8_t)>(base+0x3668f00)(nullptr,type,slot);
        return 2;
    };
#endif
}
}
