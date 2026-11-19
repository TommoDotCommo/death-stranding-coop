#include "world.h"
#include "idtag.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
namespace world {
namespace {
using json::Value;
Reply ok(const Value& v){return Reply{200,v.dump()};}
Reply refuse(int status,const char* code,const std::string& message){
    Value e=Value::object();e.set("status",std::to_string(status));e.set("code",status);e.set("message",message+" ("+code+")");return Reply{status,e.dump()};}
uint64_t fnv(const std::string& s,uint64_t seed){uint64_t h=1469598103934665603ull^seed;for(unsigned char c:s){h^=c;h*=1099511628211ull;}return h;}
// Session tokens, sign-in tickets and relay tokens are secrets: each is drawn from the system's generator, never from a
// generator whose state its earlier outputs give away.
std::string defaultToken(){
    static std::mutex m;std::lock_guard<std::mutex> g(m);
#ifdef _WIN32
    static std::random_device rd;
#else
    static std::random_device rd("/dev/urandom");
#endif
    const char* hex="0123456789abcdef";std::string t;for(int i=0;i<8;++i){uint32_t v=rd();for(int k=0;k<8;++k){t+=hex[v&15];v>>=4;}}return t;}
// The same time for any two texts of at most `most` bytes: no early exit on the first byte that differs.
bool sameText(const std::string& a,const std::string& b,size_t most){
    unsigned char diff=a.size()>most||b.size()>most?1:0;
    for(size_t i=0;i<most;++i)diff|=static_cast<unsigned char>((i<a.size()?a[i]:0)^(i<b.size()?b[i]:0));
    return diff==0;}
bool passText(const std::string& t){
    if(t.empty()||t.size()>PassBytes)return false;
    for(char c:t)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
    return true;}
std::string randomHex(size_t n){
    static std::mutex m;static std::mt19937_64 rng{std::random_device{}()};std::lock_guard<std::mutex> g(m);
    const char* hex="0123456789abcdef";std::string t;while(t.size()<n){uint64_t v=rng();for(int k=0;k<16&&t.size()<n;++k){t+=hex[v&15];v>>=4;}}return t;}
const char* Digits62="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const char* DocumentKind="coop-world-objects";
[[noreturn]] void bad(const std::string& why){throw std::runtime_error(why);}
bool lettersAndDigits(const std::string& s,size_t least,size_t most,const char* also=""){
    if(s.size()<least||s.size()>most)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||(c&&std::strchr(also,c))))return false;
    return true;}
std::string trim(const std::string& s){size_t b=0,e=s.size();while(b<e&&(s[b]==' '||s[b]=='\t'))++b;while(e>b&&(s[e-1]==' '||s[e-1]=='\t'))--e;return s.substr(b,e-b);}
std::vector<std::string> split(const std::string& s,char by){std::vector<std::string> out;size_t at=0;while(true){size_t end=s.find(by,at);if(end==std::string::npos){out.push_back(trim(s.substr(at)));return out;}out.push_back(trim(s.substr(at,end-at)));at=end+1;}}
// The kinds whose letter is known (mod/game_offsets.md, "Online protocol"); any other kind is named by its letter.
const std::pair<const char*,char> TypeNames[]={{"post",'p'},{"charger",'c'},{"safehouse",'s'},{"watchtower",'w'},{"bridge",'b'},{"shelter",'n'},{"rope",'r'},
    {"ladder",'l'},{"bike",'v'},{"cairn",'e'},{"road",'o'},{"mushroom",'x'},{"chiralbridge",'g'},{"jumpstand",'j'},{"sign",'t'},{"zipline",'z'},{"catapult",'a'}};
bool oneOf(const std::string& s,std::initializer_list<const char*> allowed){for(auto a:allowed)if(s==a)return true;return false;}
// An account key as the API takes it (api.h accountKeyText): 1..MaxKeyBytes letters, digits, '_', '-', '.'.
bool keyText(const std::string& k){
    if(k.empty()||k.size()>MaxKeyBytes)return false;
    for(char c:k)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;}
bool wholeNumber(const std::string& s,int64_t& out){
    if(s.empty()||s.size()>18)return false;
    size_t i=s[0]=='-'?1:0;
    if(i==s.size())return false;
    for(size_t k=i;k<s.size();++k)if(s[k]<'0'||s[k]>'9')return false;
    out=std::stoll(s);return true;}
// An object id of the game's own shape for a type letter: the letter and 12 letters or digits (what the game reads the
// kind from, and the ids this server makes).
bool gameIdText(const std::string& id,const std::string& type){
    if(id.size()!=13||type.size()!=1||id[0]!=type[0])return false;
    for(size_t i=1;i<id.size();++i)if(!id[i]||!std::strchr(Digits62,id[i]))return false;
    return true;}
Value triple(const Value& post,const char* a,const char* b,const char* c){Value v=Value::array();v.push(post.get(a).integer());v.push(post.get(b).integer());v.push(post.get(c).integer());return v;}
// UTF-8: how many bytes the character that starts with `c` has (a byte that starts none counts as one), and how many
// characters a text has.
size_t charBytes(unsigned char c){return c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:1;}
size_t chars(const std::string& s){size_t n=0;for(unsigned char c:s)if((c>>6)!=2)++n;return n;}
// Well-formed UTF-8 without control characters.
bool utf8Text(const std::string& s){
    for(size_t i=0;i<s.size();){
        unsigned char c=static_cast<unsigned char>(s[i]);size_t n=charBytes(c);
        if(c<0x20||c==0x7f||(c>=0x80&&n==1)||i+n>s.size())return false;
        for(size_t k=1;k<n;++k)if((static_cast<unsigned char>(s[i+k])>>6)!=2)return false;
        i+=n;}
    return true;}
}
bool serverNameText(const std::string& s){
    return !s.empty()&&s.front()!=' '&&s.back()!=' '&&s.size()<=MaxHomeBytes&&utf8Text(s)&&chars(s)<=MaxHomeChars;}
std::string cutText(const std::string& s,size_t most,size_t bytes){
    std::string out;size_t n=0;
    for(size_t i=0;i<s.size();){
        size_t len=1,want=charBytes(static_cast<unsigned char>(s[i]));
        while(len<want&&i+len<s.size()&&(static_cast<unsigned char>(s[i+len])>>6)==2)++len;
        if(n+1>most||out.size()+len>bytes)break;
        out.append(s,i,len);++n;i+=len;}
    while(!out.empty()&&out.back()==' ')out.pop_back();
    return out;
}
std::string builderLabel(const std::string& name,const std::string& home){
    const std::string h=cutText(home,MaxHomeChars,MaxHomeBytes),dot=" \xC2\xB7 ";   // " · "
    if(h.empty())return name;
#ifndef MUTANT_LOOKUP_CUT
    // The name part is cut, never the server's: a long name still says where its builder is from.
    std::string n=cutText(name,MaxLabelChars-3-chars(h),MaxLabelBytes-dot.size()-h.size());
    if(n.empty())n="Porter";
    return n+dot+h;
#else
    return cutText((name.empty()?std::string("Porter"):name)+dot+h,MaxLabelChars,MaxLabelBytes);
#endif
}
std::string heldText(const std::string& why){
    return why=="build"?"nobody builds on this server":why=="member"?"only members build on this server":why=="limit"?"this server's limit of objects per player":why;}
std::string typeName(const std::string& letter){for(auto& n:TypeNames)if(letter.size()==1&&letter[0]==n.second)return n.first;return letter;}
std::string unwrap(const std::string& body){
    const std::string head="{\"data\":\"",tail="\"}";
    if(body.size()>=head.size()+tail.size()&&body.compare(0,head.size(),head)==0&&body.compare(body.size()-tail.size(),tail.size(),tail)==0)
        return body.substr(head.size(),body.size()-head.size()-tail.size());
    return body;
}
std::string playerId(const std::string& key){
    const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id="zygo_";
    for(uint64_t part=0;id.size()<32;++part){uint64_t h=fnv(key,part*0x9e3779b97f4a7c15ull);for(int i=0;i<9&&id.size()<32;++i){id+=digits[h%62];h/=62;}}
    return id;
}
std::string collectionBuilderId(const std::string& importer){return playerId(importer+"/collection");}
World::World(std::function<std::string()> randomToken):random_(randomToken?std::move(randomToken):defaultToken),serverId_(randomHex(16)),epoch_(randomHex(8)){}
const World::Player* World::playerById(const std::string& id) const {for(auto& p:players_)if(p.second.id==id)return &p.second;return nullptr;}
// Who the games are told built an object (../docs/COLLECTIONS.md, "Who the games are told built it"): its builder, but
// for a structure imported from a collection its importer's collection builder. The importer's own game sets an object
// whose builder is its own player and that its save lacks to Fail and never builds it; told another builder, it builds
// it with the other players' structures. On the server the object stays the importer's (`creator`).
std::string World::shownCreator(const Object& o) const {
#ifndef MUTANT_COLLECTION_CREATOR_SELF
    if(!o.collection.empty()&&o.originServer.empty())return collectionBuilderId(o.creator);
#endif
    return o.creator;}
// A friend's objects come first where the policy orders by likes (curate): his friends by their player ids.
std::set<std::string> World::friendsOf(const std::string& player) const {
    std::set<std::string> out;
#ifndef MUTANT_CURATE_FRIENDS_NONE
    if(auto* p=playerById(player))for(auto& f:p->friends)out.insert(playerId(f));
#else
    (void)player;
#endif
    return out;}
std::string World::playerOf(const std::string& token) const {std::lock_guard<std::mutex> g(lock_);auto t=tokens_.find(token);return t==tokens_.end()?std::string():t->second;}
Reply World::signIn(const std::string& key,const std::string& displayName,const std::string& gateway){
    std::lock_guard<std::mutex> g(lock_);
#ifndef MUTANT_IDENTITY_NAME_SIGN_IN
    // Under keys the account comes from a launcher's ticket only: not from an address anyone can write, nor from a
    // Steam ticket the server does not check.
    if(policy_.identity=="keys"){note("sign-in refused: this server takes a ticket from the launcher (identity keys)");return refuse(403,"keys","sign in through the launcher: this server knows players by their keys");}
#endif
#ifndef MUTANT_IDENTITY_KEY_NAME
    if(identity::accountIsKey(key)){note("sign-in refused: an account of a key signs in with a ticket");return refuse(403,"key-account","an account of a key signs in with a ticket from its launcher");}
#endif
    return signInLocked(key,displayName,gateway);
}
Reply World::signInTicket(const std::string& ticket,const std::string& displayName,const std::string& gateway,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    auto t=passText(ticket)?tickets_.find(ticket):tickets_.end();
#ifdef MUTANT_NOTICES_TOKEN_TICKET
    if(t==tickets_.end()&&passText(ticket))if(auto n=noticeTokens_.find(ticket);n!=noticeTokens_.end())t=tickets_.emplace(ticket,n->second).first;
#endif
    bool good=t!=tickets_.end();
#ifndef MUTANT_TICKET_EXPIRY
    if(good&&!t->second.used&&now>t->second.issued+TicketUnusedSeconds)good=false;
#endif
#ifndef MUTANT_TICKET_REUSE
    if(good&&t->second.used&&now>t->second.used+TicketGraceSeconds)good=false;
#endif
    if(!good){note("sign-in refused: unknown or expired ticket");return refuse(403,"ticket","the sign-in ticket is unknown or expired: start the game from the launcher again");}
    if(!t->second.used){t->second.used=now;dirty_=true;}
    // Under player keys the name comes from the player's launcher (apiTicket), never from the game: the game names
    // the player by his Steam name, which strangers on this server must not see.
#ifndef MUTANT_TICKET_STEAM_NAME
    (void)displayName;return signInLocked(t->second.account,"",gateway);
#else
    return signInLocked(t->second.account,displayName,gateway);
#endif
}
World::Player& World::playerFor(const std::string& account,const char* why){
    auto found=players_.find(account);
#ifndef MUTANT_BAN_PASS
    if(found!=players_.end()&&found->second.banned){note(std::string(why)+" refused: "+found->second.id+" is banned");bad("this player is banned from this server");}
#endif
    if(found!=players_.end())return found->second;
    size_t room=std::min(MaxAccounts,size_t(policy_.players));
    if(players_.size()>=room){note(std::string(why)+" refused: world is full ("+std::to_string(room)+" players)");bad("this world has all its players");}
    Player p;p.key=account;p.id=playerId(account);p.name="Porter";note("new player "+p.id+" ("+why+")");
    dirty_=true;++changes_;return players_.emplace(account,p).first->second;
}
// One good pass of a kind per player: a new one ends his older one, and passes that can no longer be used are dropped.
std::string World::pass(std::map<std::string,Pass>& passes,const std::string& account,uint64_t relayId,int64_t now){
    const bool ticket=&passes==&tickets_,notice=&passes==&noticeTokens_;
    for(auto it=passes.begin();it!=passes.end();){
        const Pass& p=it->second;
        bool over=ticket?(p.used?now>p.used+TicketGraceSeconds:now>p.issued+TicketUnusedSeconds):
                  notice?(p.used?now>p.used+NoticeTokenIdleSeconds:now>p.issued+NoticeTokenUnusedSeconds):(p.used?now>p.used+RelayTokenIdleSeconds:now>p.issued+RelayTokenUnusedSeconds);
#ifndef MUTANT_TICKET_REPLACE
        over=over||p.account==account;
#endif
        if(over)it=passes.erase(it);else ++it;}
    std::string text;
    for(int tries=0;tries<8&&(!passText(text)||passes.count(text));++tries)text=random_().substr(0,PassBytes);
    if(!passText(text)||passes.count(text))bad("no fresh ticket could be made");
    passes[text]=Pass{account,relayId,now,0};dirty_=true;return text;
}
json::Value World::apiTicket(const std::string& account,int64_t now,const std::string& name){
    std::lock_guard<std::mutex> g(lock_);
    if(policy_.identity!="keys")bad("this server has no player keys (identity none): its game sign-in needs no ticket");
    if(!identity::accountIsKey(account))bad("a ticket is for a player's key");
    auto& player=playerFor(account,"ticket");
    // The launcher's name for its player (printable, short): what the others see, instead of a placeholder or Steam name.
    bool printable=!name.empty()&&name.size()<=MaxNameBytes;for(unsigned char c:name)printable=printable&&c>=32&&c<127;
#ifndef MUTANT_TICKET_NAME
    if(printable&&player.name!=name){player.name=name;++changes_;}
#else
    (void)printable;
#endif
    Value out=Value::object();out.set("format",1);out.set("ticket",pass(tickets_,account,0,now));out.set("expires",now+TicketUnusedSeconds);
    note("sign-in ticket for "+playerId(account));return out;
}
json::Value World::apiRelayToken(const std::string& account,uint64_t relayId,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(policy_.identity!="keys")bad("this server has no player keys (identity none): its relay takes the group's key");
    if(!identity::accountIsKey(account)||!relayId)bad("a relay token is for a player's key and his relay id");
    playerFor(account,"relay token");
    Value out=Value::object();out.set("format",1);out.set("token",pass(relayTokens_,account,relayId,now));out.set("relay_id",int64_t(relayId));out.set("expires",now+RelayTokenUnusedSeconds);
    note("relay token for "+playerId(account));return out;
}
json::Value World::apiNoticeToken(const std::string& account,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(account.empty()||account.size()>MaxKeyBytes)bad("a notice token is for a player's account");
    const Player& p=playerFor(account,"notice token");
    Value out=Value::object();out.set("format",1);out.set("token",pass(noticeTokens_,account,0,now));out.set("expires",now+NoticeTokenUnusedSeconds);
    note("notice token for "+p.id);return out;
}
Reply World::noticeTokenRefused(){return refuse(403,"notice-token","the notice token is unknown or expired: start the game from the launcher again");}
const World::Player* World::chatPlayer(const std::string& token,int64_t now){
    auto t=passText(token)?noticeTokens_.find(token):noticeTokens_.end();
    if(t==noticeTokens_.end())return nullptr;
#ifndef MUTANT_CHAT_EXPIRY
    if(t->second.used?now>t->second.used+NoticeTokenIdleSeconds:now>t->second.issued+NoticeTokenUnusedSeconds)return nullptr;
#endif
    auto found=players_.find(t->second.account);
    if(found==players_.end()||found->second.banned)return nullptr;
    if(!t->second.used||now>=t->second.used+NoticeTokenSaveSeconds)dirty_=true;
    t->second.used=std::max(t->second.used,now);
    return &found->second;
}
Reply World::chatRead(const std::string& token,const std::string& since,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(!chatPlayer(token,now))return refuse(403,"notice-token","the notice token is unknown or expired: start the game from the launcher again");
    Value out=Value::object(),lines=Value::array();out.set("format",1);out.set("on",policy_.chat=="on");uint64_t next=chatNext_;
    uint64_t from=0;bool have=!since.empty()&&since.size()<=19;for(char c:since){have=have&&c>='0'&&c<='9';if(have)from=from*10+uint64_t(c-'0');}
#ifndef MUTANT_CHAT_HISTORY
    if(have&&from<=chatNext_){
#else
    {
#endif
        size_t given=0;
        for(auto& l:chat_){if(l.n<from)continue;if(given==ChatBatch){next=l.n;break;}
            Value v=Value::object();v.set("n",int64_t(l.n));v.set("name",l.name);v.set("text",l.text);lines.push(v);++given;}}
    out.set("next",int64_t(next));out.set("lines",lines);
#ifndef MUTANT_CHAT_WAIT
    out.set("wait_ms",std::min(ChatWaitMaxMs,std::max(ChatWaitMinMs,int64_t(online_.size())*10)));
#else
    out.set("wait_ms",ChatWaitMinMs);
#endif
    Reply r;r.body=out.dump();return r;
}
Reply World::chatSay(const std::string& token,const std::string& body,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    auto* p=chatPlayer(token,now);
    if(!p)return refuse(403,"notice-token","the notice token is unknown or expired: start the game from the launcher again");
#ifndef MUTANT_CHAT_OFF
    if(policy_.chat!="on")return refuse(403,"chat-off","text chat is off on this server");
#endif
    std::string text;
    try{auto d=json::parse(body);text=d.get("text").string();}catch(const std::exception&){return refuse(400,"chat-text","no text");}
    while(!text.empty()&&text.front()==' ')text.erase(0,1);
    while(!text.empty()&&text.back()==' ')text.pop_back();
#ifndef MUTANT_CHAT_TEXT
    if(text.empty()||text.size()>ChatMaxBytes||!utf8Text(text)||chars(text)>ChatMaxChars)return refuse(400,"chat-text","a line of 1 to 160 characters, without control characters");
#endif
    auto& times=chatTimes_[p->id];while(!times.empty()&&now-times.front()>=ChatWindowSeconds)times.pop_front();
#ifndef MUTANT_CHAT_RATE
    if(times.size()>=ChatPerWindow)return refuse(429,"chat-rate","too many lines: wait a moment");
#endif
    times.push_back(now);
    ChatLine l;l.n=chatNext_++;l.name=p->name.empty()?"Porter":p->name;l.text=text;chat_.push_back(l);
#ifndef MUTANT_CHAT_KEEP
    while(chat_.size()>ChatKept)chat_.pop_front();
#endif
    note("chat "+p->id+": "+text);
    Value out=Value::object();out.set("format",1);out.set("n",int64_t(l.n));Reply r;r.body=out.dump();return r;
}
Reply World::notices(const std::string& token,const std::string& since,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    auto t=passText(token)?noticeTokens_.find(token):noticeTokens_.end();
    bool good=t!=noticeTokens_.end();
#ifndef MUTANT_NOTICES_EXPIRY
    if(good&&(t->second.used?now>t->second.used+NoticeTokenIdleSeconds:now>t->second.issued+NoticeTokenUnusedSeconds))good=false;
#endif
    auto found=good?players_.find(t->second.account):players_.end();
    if(found==players_.end()){note("notices refused: unknown or expired notice token");return refuse(403,"notice-token","the notice token is unknown or expired: start the game from the launcher again");}
    // Kept good while the mod reads; the time of the last read reaches the world file now and then, not on every read.
    if(!t->second.used||now>=t->second.used+NoticeTokenSaveSeconds)dirty_=true;
    t->second.used=std::max(t->second.used,now);
    const std::string id=found->second.id;
    // `since` names a version of this process (its epoch and a stamp): only what changed after it. Anything else (the
    // first read, a server started again) gets every one of his objects, and the mod compares them with what it knows.
    uint64_t after=0;bool full=true;const std::string mark=epoch_+"-";
#ifndef MUTANT_NOTICES_EPOCH
    if(since.size()>mark.size()&&since.size()<=mark.size()+19&&since.compare(0,mark.size(),mark)==0){
#else
    if(auto dash=since.find('-');dash!=std::string::npos&&since.size()>dash+1&&since.size()<=dash+20){const std::string mark=since.substr(0,dash+1);
#endif
        uint64_t n=0;bool digits=true;for(size_t i=mark.size();i<since.size();++i){digits=digits&&since[i]>='0'&&since[i]<='9';if(digits)n=n*10+uint64_t(since[i]-'0');}
        if(digits&&n<=stamps_){after=n;full=false;}}
    Value list=Value::array();
    for(auto& o:objects_){
        const Object& ob=o.second;
#ifndef MUTANT_NOTICES_OTHERS
        if(ob.creator!=id)continue;
#endif
#ifndef MUTANT_NOTICES_IMPORTS
        if(!ob.originServer.empty())continue;   // its builder id is what the other server says: not his here
#endif
#ifndef MUTANT_NOTICES_SINCE
        if(!full&&ob.stamp<=after)continue;
#endif
        Value v=Value::object();v.set("id",ob.id);v.set("kind",typeName(ob.post.get("t").string()));
        std::string state=ob.remnant?"remnant":ob.share.empty()?"shared":ob.share=="kept"?"kept":ob.share=="held"?"held":"waiting";
        v.set("state",state);
        v.set("why",ob.share=="held"?ob.held:ob.share=="kept"?ob.kept:ob.share=="ask"?std::string("you"):ob.share=="review"?std::string("owner"):std::string());
        list.push(v);}
    Value out=Value::object();out.set("format",1);out.set("name",name_);out.set("version",epoch_+"-"+std::to_string(stamps_));out.set("full",full);out.set("objects",list);
    // What applies to his older strand here (his choice, else the policy's): his game's mod offers it only on "yes".
#ifndef MUTANT_NOTICES_OFFER_OLD
    out.set("offer_old",found->second.offerOld.empty()?policy_.offerOld:found->second.offerOld);
#else
    out.set("offer_old",policy_.offerOld);
#endif
    return ok(out);
}
bool World::relayAdmit(uint64_t id,const std::string& token,const std::string& groupKey,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    if(policy_.identity!="keys")return sameText(token,groupKey,PassBytes);
#ifdef MUTANT_RELAY_TOKEN_GROUP_KEY
    if(sameText(token,groupKey,PassBytes))return true;
#endif
    auto t=relayTokens_.find(token);
    if(t==relayTokens_.end())return false;
    Pass& p=t->second;
#ifndef MUTANT_RELAY_TOKEN_EXPIRY
    if(p.used?now>p.used+RelayTokenIdleSeconds:now>p.issued+RelayTokenUnusedSeconds)return false;
#endif
#ifndef MUTANT_RELAY_TOKEN_ID
    if(p.relayId!=id)return false;
#else
    (void)id;
#endif
    p.used=std::max(p.used,now);return true;
}
void World::setOwner(const std::string& account){
    if(!account.empty()&&!identity::accountIsKey(account))bad("the owner is a key (ed25519- and 64 hex digits)");
    std::lock_guard<std::mutex> g(lock_);
    if(owner_!=account){owner_=account;dirty_=true;++changes_;note(account.empty()?"no owner key":"owner key "+account.substr(8,16));}
}
Reply World::signInLocked(const std::string& key,const std::string& displayName,const std::string& gateway){
    if(key.empty()||key.size()>MaxKeyBytes||displayName.size()>MaxNameBytes)return refuse(400,"bad-account","account key or name refused");
    auto found=players_.find(key);
    if(found==players_.end()){
        // The hard cap, or fewer when the policy says so (solo: one).
#ifndef MUTANT_POLICY_PLAYERS
        size_t room=std::min(MaxAccounts,size_t(policy_.players));
#else
        size_t room=MaxAccounts;
#endif
#ifndef MUTANT_WORLD_PLAYER_CAP
        if(players_.size()>=room){note("sign-in refused: world is full ("+std::to_string(room)+" players)");return refuse(403,"full","this world has all its players");}
#else
        (void)room;
#endif
        Player p;p.key=key;p.id=playerId(key);p.name=displayName.empty()?"Porter":displayName;
        found=players_.emplace(key,p).first;note("new player "+p.id+" "+p.name);
    }else if(!displayName.empty()&&found->second.name!=displayName){found->second.name=displayName;++changes_;}
    auto& p=found->second;
#ifndef MUTANT_BAN_SIGN_IN
    if(p.banned){note("sign-in refused: "+p.id+" is banned");return refuse(403,"banned","this player is banned from this server");}
#endif
    // One session per player: a new sign-in ends the old one.
    for(auto it=tokens_.begin();it!=tokens_.end();)if(it->second==p.id)it=tokens_.erase(it);else ++it;
    std::string token=random_();tokens_[token]=p.id;dirty_=true;
    Value user=Value::object();user.set("provider","steam");user.set("providerId",key);user.set("id",p.id);user.set("entity",Value::object());
    user.set("displayName",p.name);user.set("display_name",p.name);
    Value session=Value::object();session.set("zone","ds");session.set("token",token);session.set("gateway",gateway);
    session.set("properties",Value::object());session.set("roles",Value::array());
    Value out=Value::object();out.set("user",user);out.set("session",session);return ok(out);
}
json::Value World::servedPost(const Value& post) const {
    Value out=post;
    for(const char* k:{"mt","rmt"}){
        if(!post.get(k).isArray())continue;
        Value list=Value::array();
        for(auto e:post.get(k).items()){
            // A contributor's account: "" is the builder's own entry (the game sends it so for its own player); a player
            // of this world stays; any other (the original online service's accounts, another server's) is taken out.
            if(e.isObject()&&e.has("c")&&!(e.get("c").isString()&&(e.get("c").string().empty()||playerById(e.get("c").string()))))e.set("c","");
            list.push(e);}
        out.set(k,list);}
    return out;
}
json::Value World::objectResponse(const Object& o) const {
    const Value& p=o.post;Value r=Value::object();
    r.set("id",o.id);r.set("c",shownCreator(o));r.set("t",p.get("t"));
    r.set("m",triple(p,"x","y","m"));r.set("l",triple(p,"px","py","pz"));r.set("r",triple(p,"rx","ry","rz"));
    r.set("exp",p.get("exp").integer());r.set("lp",o.likes);r.set("ut",o.updated);r.set("q",p.get("q").integer());
#ifndef MUTANT_WORLD_STRIP_SERVE
    const Value q=servedPost(o.post);
#else
    const Value& q=o.post;
#endif
    r.set("mt",q.has("mt")?q.get("mt"):Value::array());r.set("rmt",q.has("rmt")?q.get("rmt"):Value::array());r.set("bgs",Value::array());
    r.set("st",p.get("st").isString()?p.get("st"):Value(""));
    Value comments=Value::array();if(p.get("c").isObject())comments.push(p.get("c"));r.set("cm",comments);
    for(const char* k:{"ri","si","bi","pi","vi","ei","ci"})if(p.get(k).isObject())r.set(k,p.get(k));
    r.set("tags",p.has("tags")?p.get("tags"):Value::array());
    return r;
}
Reply World::call(const std::string& token,const std::string& name,const std::string& body,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    const Player* player=nullptr;
    auto t=tokens_.find(token);if(t!=tokens_.end())player=playerById(t->second);
#ifndef MUTANT_IDENTITY_OLD_SESSION
    // A session from before the world took keys belongs to an account anyone could sign in as.
    if(player&&policy_.identity=="keys"&&!identity::accountIsKey(player->key)){note("refused "+name+": a session from before identity keys");return refuse(401,"no-session","sign in again through the launcher: this server knows players by their keys");}
#endif
#ifndef MUTANT_WORLD_NO_TOKEN
    if(!player){note("refused "+name+": no session");return refuse(401,"no-session","sign in first");}
#else
    if(!player&&!players_.empty())player=&players_.begin()->second;
    if(!player)return refuse(401,"no-session","sign in first");
#endif
    Value in;
    try{auto text=unwrap(body);in=text.empty()?Value::object():json::parse(text);}
    catch(const std::exception& e){note("refused "+name+": "+e.what());return refuse(400,"bad-body","request body is not JSON");}
    return gateway(*player,name,in,now);
}
Reply World::gateway(const Player& me,const std::string& name,const Value& in,int64_t now){
    if(name=="getVersion"){
        Value v=Value::object();v.set("version",std::to_string(VersionMajor)+"."+std::to_string(VersionMinor));v.set("major",VersionMajor);v.set("minor",VersionMinor);return ok(v);}
    if(name=="createObject"){
        if(!in.get("t").isString())return refuse(400,"bad-object","object type missing");
        // The hard cap on what one player stores here (the server's memory and disk). What the policy does not let him
        // share is not refused: his game builds in his own world whatever a server says, so the server holds it for him.
        // The game reads an object's kind from the first letter of its id (its own ids are the type letter and 12
        // characters): an id of any other shape makes the builder's game delete its upload at once.
        const std::string type=in.get("t").string();
#ifndef MUTANT_WORLD_ID_LETTER
        if(type.size()!=1||type[0]<'a'||type[0]>'z')return refuse(400,"bad-object","object type must be one letter");
#endif
        // A structure the game already has an id for, sent again with that id (its "re-create": the same body and
        // "id"). The game keeps its own id whatever the answer says, so the server keeps it or refuses: a new id would
        // leave the game and the server naming the structure differently.
        std::string given;
        if(in.has("id")){
            given=in.get("id").string();
#ifndef MUTANT_WORLD_ID_SHAPE
            if(!gameIdText(given,type))return refuse(400,"bad-id","an object id is the type letter and 12 letters or digits");
#endif
            if(auto found=objects_.find(given);found!=objects_.end()){
                const Object& was=found->second;
                if(was.creator==me.id&&was.originServer.empty()&&!was.remnant){
#ifndef MUTANT_WORLD_ID_AGAIN
                    // The same structure again (a second offer, a game that did not see the first answer): no second object.
                    note("object "+given+" sent again by "+me.name+": the same object");return ok(objectResponse(was));
#else
                    given.clear();
#endif
                }
#ifndef MUTANT_WORLD_ID_TAKEN
                else{note("refused createObject: "+given+" is "+(!was.originServer.empty()?"imported":was.remnant?"withdrawn":"another player's"));
                     return refuse(403,"id-taken","that object id is not yours here");}
#endif
            }
#ifdef MUTANT_WORLD_ID_KEEP
            given.clear();
#endif
        }
        size_t mine=0;for(auto& o:objects_)if(o.second.creator==me.id&&o.second.originServer.empty()&&!o.second.remnant)++mine;
#ifndef MUTANT_WORLD_OBJECT_CAP
        if(mine>=MaxObjectsPerPlayer){note("refused createObject: "+me.id+" has too many");return refuse(403,"too-many","too many objects");}
#endif
        Object o;o.id=given;
        while(o.id.empty()||(given.empty()&&objects_.count(o.id))){
            const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
#ifndef MUTANT_WORLD_ID_LETTER
            o.id=type;
#else
            o.id="o";
#endif
            uint64_t h=fnv(me.id,nextObject_++*0x9e3779b97f4a7c15ull);
            for(int i=0;i<12;++i){o.id+=digits[h%62];h/=62;if(i==5)h^=fnv(o.id,nextObject_);}
#ifndef MUTANT_WORLD_ID_UNTAGGED
            idtag::stamp(serverId_,o.id);
#endif
        }
        o.creator=me.id;o.post=in;o.post.erase("id");o.updated=now;o.likes=in.get("lp").integer();o.name=me.name;
#ifndef MUTANT_WORLD_STRIP_STORE
        o.post=servedPost(o.post);
#endif
        // Kept for its builder only, held (the policy does not let him share it), waiting for his answer, or offered
        // (and then as accept_offers says).
        const std::string& choice=me.offerBuilds.empty()?policy_.offerBuilds:me.offerBuilds;
        const std::string why=choice=="no"?std::string():blocked(me,false);
#ifndef MUTANT_POLICY_ASK
        o.share=choice=="no"?"kept":!why.empty()?"held":choice=="ask"?"ask":shareAfterOffer();
#else
        o.share=choice=="no"?"kept":!why.empty()?"held":shareAfterOffer();
#endif
        if(o.share=="held"){o.held=why;o.heldAsk=choice=="ask";note("object held for "+me.name+" only: "+heldText(why));}
        if(o.share=="kept")o.kept=choice=="no"?"choice":"server";
        touch(o);
        auto& kept=objects_[o.id]=o;dirty_=true;++changes_;note("object "+kept.id+" type "+in.get("t").string()+" in area "+std::to_string(in.get("q").integer())+" by "+me.name+(o.share.empty()?"":" ("+o.share+")"));
        return ok(objectResponse(kept));}
    if(name=="deleteObject"||name=="updateObject"){
        auto found=objects_.find(in.get("id").string());
        if(found==objects_.end())return refuse(404,"no-object","no such object");
#ifndef MUTANT_WORLD_OWNER
        if(found->second.creator!=me.id){note("refused "+name+": "+found->first+" is not "+me.id+"'s");return refuse(403,"not-yours","only its builder can change an object");}
#endif
#ifndef MUTANT_LINK_IMPORT_OWNER
        // An imported object belongs to its own server: nobody here changes it, whatever his player id is.
        if(!found->second.originServer.empty()){note("refused "+name+": "+found->first+" is imported");return refuse(403,"not-yours","an imported object is changed on its own server");}
#endif
#ifndef MUTANT_POLICY_REMNANT_CHANGE
        // A remnant no longer belongs to the account that built it.
        if(found->second.remnant){note("refused "+name+": "+found->first+" was withdrawn");return refuse(403,"not-yours","a withdrawn object belongs to nobody");}
#endif
        dirty_=true;++changes_;
        if(name=="deleteObject"){note("object "+found->first+" removed by "+me.name);dropped(found->first);objects_.erase(found);return ok(Value::object());}
        for(const char* k:{"si","pi","vi","ci","ei"})if(in.get(k).isObject())found->second.post.set(k,in.get(k));
        found->second.updated=now;return ok(objectResponse(found->second));}
    if(name=="findQpidObjects"){
        // The other players' objects in one area that every player is handed, at most the asked number per type, chosen
        // as the policy says and the same ones as last time while they are there (curate). Not the asker's own: the game
        // has those in its save. Where the world is the server's (policy `world`), his own are handed too, told as his
        // collection builder's: a game whose save has the object does not make it twice, and one whose save lacks it
        // builds it as another player's, where told as his own it would set it to Fail.
        int64_t area=in.get("q").integer();const Value& counts=in.get("o").get("ct");
        std::map<std::string,int64_t> asked;for(auto& m:counts.members())asked[m.first]=m.second.integer();
        std::vector<Candidate> here;for(auto& o:objects_)if(o.second.post.get("q").integer()==area){
            here.push_back(candidateOf(o.second));
#ifndef MUTANT_WORLD_NO_HAND_BACK
            if(handsBack(o.second,me.id)){here.back().creator=collectionBuilderId(me.id);here.back().owner=me.id;}
#endif
        }
        std::vector<std::string> before;
        if(auto p=picks_.find(me.id);p!=picks_.end())if(auto a=p->second.find(area);a!=p->second.end())before=a->second.ids;
        Curated chosen=curate(me.id,area,counts.isObject()?&asked:nullptr,here,before,policy_,friendsOf(me.id));
        remember(me.id,area,chosen.remember,now);
        Value list=Value::array();
        for(auto& id:chosen.handed){
            const Object& o=objects_.at(id);Value r=objectResponse(o);
#ifndef MUTANT_WORLD_BACK_AS_OWN
            if(handsBack(o,me.id))r.set("c",collectionBuilderId(me.id));
#endif
            list.push(r);}
        Value set=Value::object();set.set("ro",Value::array());set.set("m",Value::array());set.set("oa",list);
        for(const char* k:{"ob","od","oe","op"})set.set(k,Value::array());
        Value out=Value::object();out.set("n",set);return ok(out);}
    if(name=="getQpidObjects"){
        Value list=Value::array();
        for(auto& id:in.get("ids").items()){auto found=objects_.find(id.string());if(found!=objects_.end()&&servedTo(found->second,me.id))list.push(objectResponse(found->second));}
        Value out=Value::object();out.set("objects",list);return ok(out);}
    if(name=="sendLike"){
        auto found=objects_.find(in.get("oid").string());
        if(found!=objects_.end()&&servedTo(found->second,me.id)){
            int64_t n=in.get("lp").integer()+in.get("lp2").integer();
#ifndef MUTANT_WORLD_LIKE_RANGE
            if(n<0||n>100000)return refuse(400,"bad-like","like count refused");
#endif
            found->second.likes+=n;dirty_=true;++changes_;
            // The builder hears of it in his like history (never of his own likes); a remnant's likes stay on it.
#ifndef MUTANT_POLICY_REMNANT_LIKES
            if(found->second.remnant)n=0;
#endif
            if(found->second.creator!=me.id&&n>0&&playerById(found->second.creator)){
                Like l;l.to=found->second.creator;l.from=me.id;l.object=found->first;l.type=in.get("lt").string();l.time=now;
                l.points=in.get("lp").integer();l.autoPoints=in.get("lp2").integer();
                if(likes_.size()>=MaxLikes)likes_.erase(likes_.begin());
                likes_.push_back(l);}}
        return ok(Value::object());}
    if(name=="lookup"||name=="reverseLookup"){
        // Who is behind these ids: only the players of this world are known here.
        Value users=Value::array();
        for(auto& id:in.get(name=="lookup"?"ids":"bs").items())for(auto& p:players_){
            if((name=="lookup"?p.second.id:p.second.key)!=id.string())continue;
            // This server's own players: their name only.
#ifndef MUTANT_LOOKUP_LOCAL_SUFFIX
            const std::string shown=p.second.name;
#else
            const std::string shown=builderLabel(p.second.name,homeLocked(serverId_,name_));
#endif
            Value u=Value::object();u.set("account",p.second.id);u.set("boundAccount",p.second.key);u.set("displayName",shown);u.set("provider","steam");users.push(u);}
        // A player's collection builder (what he imported from collections): "Name · collection", and no account.
        if(name=="lookup")for(auto& id:in.get("ids").items()){
            if(playerById(id.string()))continue;
            for(auto& p:players_){
                if(collectionBuilderId(p.second.id)!=id.string())continue;
#ifndef MUTANT_COLLECTION_LOOKUP_NAME
                const std::string shown=builderLabel(p.second.name,CollectionHome);
#else
                const std::string shown=p.second.name;
#endif
                Value u=Value::object();u.set("account",id.string());u.set("boundAccount","");u.set("displayName",shown);u.set("provider","steam");users.push(u);break;}}
        // The builders of imported objects: their name as their own server gave it with that server's display name
        // ("Name · Home", README "Builder names"), and no account.
        if(name=="lookup"&&!byOrigin_.empty())for(auto& id:in.get("ids").items()){
            if(playerById(id.string()))continue;
            for(auto& o:objects_)if(!o.second.originServer.empty()&&o.second.creator==id.string()){
                const std::string builder=o.second.originName.empty()?"Porter":o.second.originName;
#ifndef MUTANT_LOOKUP_HOME_MISSING
                const std::string shown=builderLabel(builder,homeLocked(o.second.originServer,o.second.home));
#else
                const std::string shown=builder;
#endif
                Value u=Value::object();u.set("account",o.second.creator);u.set("boundAccount","");u.set("displayName",shown);u.set("provider","steam");users.push(u);break;}}
        Value out=Value::object();out.set("users",users);return ok(out);}
    if(name=="devoteHighwayResources"){
        // What the player put into the game's auto-pavers since his last call (README "Roads paved together"): per entry
        // the paver's construction id `ci`, the resource `ri` and the amount `n`. The whole body is checked first.
        const Value& list=in.get("ph");
        if(!list.isArray()||list.items().size()>MaxHighwayEntries)return refuse(400,"bad-highway","ph: a list of at most 64 entries");
        std::map<std::pair<int64_t,int64_t>,int64_t> add;
        for(auto& e:list.items()){
            const int64_t ci=e.get("ci").integer(-1),ri=e.get("ri").integer(-1),n=e.get("n").integer(-1);
#ifndef MUTANT_HIGHWAY_RANGE
            if(!e.get("ci").isInt()||!e.get("ri").isInt()||!e.get("n").isInt()||ci<1||ci>MaxPaverId||ri<0||ri>=HighwayResources||n<1||n>MaxHighwayAmount)
                return refuse(400,"bad-highway","an entry is a paver id, a resource 0..5 and an amount 1..100000");
#endif
#ifdef MUTANT_HIGHWAY_WHOLE
            devoted_[me.id][{ci,ri}]+=n;continue;
#endif
            add[{ci,ri}]=std::min(MaxHighwayTotal,add[{ci,ri}]+n);}
        if(add.empty()){Value out=Value::object();out.set("e",0);return ok(out);}
        auto& mine=devoted_[me.id];
        size_t fresh=0;for(auto& a:add)if(!mine.count(a.first))++fresh;
#ifndef MUTANT_HIGHWAY_KEYS
        if(mine.size()+fresh>MaxHighwayKeys){if(mine.empty())devoted_.erase(me.id);note("refused devoteHighwayResources: "+me.name+" has too many pavers");return refuse(403,"too-many","too many pavers");}
#endif
        for(auto& a:add)mine[a.first]=std::min(MaxHighwayTotal,mine[a.first]+a.second);
        dirty_=true;++changes_;note("paver materials from "+me.name+": "+std::to_string(add.size())+" entries");
        Value out=Value::object();out.set("e",0);return ok(out);}
    if(name=="getHighwayResources"){
        // The game asks for some of its pavers (`cds`: construction id and the time it last took materials, which is not
        // used) and the resources roads take (`rids`). Per paver and resource: `n` what the other players put in whose
        // contributions the policy shares now (the game adds what grew since its last answer), `un` his own; per paver the
        // other players who put something in, most first, with the likes their contributions earned; `ul` his own.
        const Value& asked=in.get("cds");const Value& rids=in.get("rids");
        if(!asked.isArray()||asked.items().size()>MaxHighwayEntries||!rids.isArray()||rids.items().size()>size_t(HighwayResources))
            return refuse(400,"bad-highway","cds: at most 64 pavers, rids: at most 6 resources");
        std::set<int64_t> pavers,resources;
        for(auto& c:asked.items()){const int64_t cid=c.get("cid").integer(-1);if(!c.get("cid").isInt()||cid<1||cid>MaxPaverId)return refuse(400,"bad-highway","a paver id is 1..999999");pavers.insert(cid);}
        for(auto& r:rids.items()){if(!r.isInt()||r.integer()<0||r.integer()>=HighwayResources)return refuse(400,"bad-highway","a resource is 0..5");resources.insert(r.integer());}
        {std::string ids;size_t k=0;for(int64_t cid:pavers){if(k++==6){ids+=" ...";break;}ids+=" "+std::to_string(cid);}
         note("paver ask from "+me.name+": "+std::to_string(pavers.size())+" pavers"+ids);}
#ifdef MUTANT_HIGHWAY_ASKED
        for(auto& d:devoted_)for(auto& k:d.second){pavers.insert(k.first.first);resources.insert(k.first.second);}
#endif
        // Whose contributions the asker gets: every other player's that the policy shares now.
        std::vector<std::pair<std::string,const std::map<std::pair<int64_t,int64_t>,int64_t>*>> others;
        for(auto& d:devoted_){
#ifndef MUTANT_HIGHWAY_OWN
            if(d.first==me.id)continue;
#endif
            const Player* p=playerById(d.first);if(!p||!contributes(*p))continue;
            others.push_back({d.first,&d.second});}
        auto at=[](const std::map<std::pair<int64_t,int64_t>,int64_t>& m,int64_t cid,int64_t rid){auto f=m.find({cid,rid});return f==m.end()?int64_t(0):f->second;};
        auto mine=devoted_.find(me.id);
        Value pr=Value::array(),cs=Value::array();
        for(int64_t cid:pavers){
            for(int64_t rid:resources){
                int64_t n=0,un=mine==devoted_.end()?0:at(mine->second,cid,rid);
                for(auto& o:others)n+=at(*o.second,cid,rid);
                if(n==0&&un==0)continue;
                Value e=Value::object();e.set("id",std::to_string(cid)+"_"+std::to_string(rid));e.set("cid",cid);e.set("rid",rid);e.set("n",n);e.set("un",un);pr.push(e);}
            std::vector<std::pair<int64_t,std::string>> who;
            for(auto& o:others){int64_t t=0;for(auto& k:*o.second)if(k.first.first==cid)t+=k.second;if(t>0)who.push_back({-t,o.first});}
            if(who.empty())continue;
            std::sort(who.begin(),who.end());
            Value list=Value::array();
            for(auto& w:who){Value u=Value::object();u.set("u",w.second);auto l=highwayLikes_.find(w.second);u.set("l",l==highwayLikes_.end()?int64_t(0):l->second);list.push(u);}
            Value c=Value::object();c.set("cid",cid);c.set("cs",list);cs.push(c);}
        auto liked=highwayLikes_.find(me.id);
        Value out=Value::object();out.set("pr",pr);out.set("cs",cs);out.set("ul",liked==highwayLikes_.end()?int64_t(0):liked->second);return ok(out);}
    if(name=="sendHighwayLikes"){
        // Likes for the players whose paver contributions the giver got: to anyone else (himself, a player whose
        // contributions are not shared, an unknown id) they are dropped. The whole body is checked first.
        const Value& list=in.get("hls");
        if(!list.isArray()||list.items().size()>MaxHighwayEntries)return refuse(400,"bad-like","hls: a list of at most 64 entries");
        for(auto& h:list.items())if(!h.get("u").isString()||!h.get("l").isInt()||h.get("l").integer()<0||h.get("l").integer()>MaxHighwayAmount)return refuse(400,"bad-like","like count refused");
        for(auto& h:list.items()){
            const std::string to=h.get("u").string();const Player* p=playerById(to);
#ifndef MUTANT_HIGHWAY_LIKES
            if(!p||to==me.id||!contributes(*p)||!devoted_.count(to))continue;
#else
            if(!p)continue;
#endif
            if(h.get("l").integer()==0)continue;
            auto& l=highwayLikes_[to];l=std::min(MaxHighwayTotal,l+h.get("l").integer());dirty_=true;++changes_;}
        return ok(Value::object());}
    if(name=="getRankingSchedules"){Value out=Value::object();out.set("rs",Value::array());out.set("ut",now);return ok(out);}
    if(name=="getWastedBaggages"){Value out=Value::object();out.set("ws",Value::array());out.set("ud",now);return ok(out);}
    if(name=="findMissions"||name=="getOrderedMissions"||name=="getMissionsInProgress"){Value out=Value::object();out.set("missions",Value::array());return ok(out);}
    if(name=="getLikeHistory"){
        // What others gave this player's objects after `since`; nobody reads another player's history.
        int64_t since=in.get("since").integer();Value list=Value::array();
        for(auto& l:likes_){
#ifndef MUTANT_WORLD_LIKES_MINE
            if(l.to!=me.id)continue;
#endif
#ifndef MUTANT_WORLD_LIKES_SINCE
            if(l.time<=since)continue;
#endif
            Value v=Value::object();v.set("t",l.time);v.set("oid",l.object);v.set("lp",l.points);v.set("lp2",l.autoPoints);v.set("uid",l.from);v.set("sc",1);v.set("lt",l.type);list.push(v);}
        Value out=Value::object();out.set("like_histories",list);return ok(out);}
    if(name=="getRelationships"){Value out=Value::object();out.set("strand",Value::array());out.set("history",Value::array());return ok(out);}
    if(name=="getPlayerProfile"){Value out=Value::object();out.set("profiles",Value::array());return ok(out);}
    note("unhandled "+name);
    return ok(Value::object());
}
json::Value World::snapshot() const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",2);out.set("server_id",serverId_);out.set("next_object",int64_t(nextObject_));
    if(!name_.empty())out.set("name",name_);
    Value policy=Value::object();policy.set("preset",policy_.preset);
    for(auto& item:policyItems(policy_)){int64_t n=0;if(wholeNumber(item.second,n))policy.set(item.first,n);else policy.set(item.first,item.second);}
#ifndef MUTANT_POLICY_SAVE_LOST
    out.set("policy",policy);
#endif
    Value players=Value::array();
    for(auto& p:players_){Value v=Value::object();v.set("key",p.second.key);v.set("id",p.second.id);v.set("name",p.second.name);
        if(!p.second.offerBuilds.empty())v.set("offer_builds",p.second.offerBuilds);
        if(!p.second.offerOld.empty())v.set("offer_old",p.second.offerOld);
        if(p.second.member)v.set("member",true);
#ifndef MUTANT_BAN_SAVE_LOST
        if(p.second.banned)v.set("banned",true);
#endif
#ifndef MUTANT_FRIENDS_SAVE_LOST
        if(!p.second.showOthers.empty())v.set("show_others",p.second.showOthers);
        if(!p.second.friends.empty()){Value f=Value::array();for(auto& x:p.second.friends)f.push(x);v.set("friends",f);}
#endif
        players.push(v);}
    out.set("players",players);
    Value tokens=Value::object();for(auto& t:tokens_)tokens.set(t.first,t.second);out.set("sessions",tokens);
    if(!owner_.empty())out.set("owner",owner_);
    for(auto* passes:{&tickets_,&relayTokens_,&noticeTokens_}){
        Value list=Value::array();
        for(auto& t:*passes){Value v=Value::object();v.set("text",t.first);v.set("account",t.second.account);v.set("issued",t.second.issued);v.set("used",t.second.used);
            if(passes==&relayTokens_)v.set("relay_id",int64_t(t.second.relayId));
            list.push(v);}
#ifdef MUTANT_NOTICES_SAVE
        if(passes==&noticeTokens_)continue;
#endif
        if(passes==&noticeTokens_&&list.items().empty())continue;   // a world without notice tokens is written as before
        out.set(passes==&tickets_?"tickets":passes==&relayTokens_?"relay_tokens":"notice_tokens",list);}
    Value objects=Value::array();
    for(auto& o:objects_){Value v=Value::object();v.set("id",o.second.id);v.set("creator",o.second.creator);v.set("updated",o.second.updated);v.set("likes",o.second.likes);v.set("post",o.second.post);
        if(!o.second.name.empty())v.set("name",o.second.name);
#ifndef MUTANT_POLICY_SAVE_LOST
        if(!o.second.share.empty())v.set("share",o.second.share);
        if(!o.second.held.empty())v.set("held",o.second.held);
        if(o.second.heldAsk)v.set("held_ask",true);
        if(!o.second.kept.empty())v.set("kept",o.second.kept);
#endif
        if(o.second.remnant)v.set("remnant",true);
#ifndef MUTANT_COLLECTION_SAVE_LOST
        if(!o.second.collection.empty())v.set("collection",o.second.collection);
        if(o.second.costed){Value c=Value::array();for(auto n:o.second.cost)c.push(n);v.set("cost",c);}
#endif
        if(!o.second.originServer.empty()){Value from=Value::object();from.set("server",o.second.originServer);from.set("id",o.second.originId);from.set("via",o.second.via);
            from.set("name",o.second.originName);from.set("likes",o.second.originLikes);if(!o.second.home.empty())from.set("home",o.second.home);v.set("origin",from);}
        objects.push(v);}
    out.set("objects",objects);
    // Cargo set down for the others; a world without any is written as before.
#ifndef MUTANT_CARGO_SAVE_LOST
    Value cargo=Value::array();
    for(auto& k:crates_){const Crate& c=k.second;Value v=Value::object();v.set("id",c.id);v.set("creator",c.creator);v.set("hash",int64_t(c.hash));
        v.set("x",c.x);v.set("y",c.y);v.set("z",c.z);v.set("yaw",c.yaw);v.set("area",c.area);v.set("placed",c.placed);cargo.push(v);}
    if(!cargo.items().empty())out.set("cargo",cargo);
#endif
    Value likes=Value::array();
    for(auto& l:likes_){Value v=Value::object();v.set("to",l.to);v.set("from",l.from);v.set("object",l.object);v.set("type",l.type);v.set("time",l.time);v.set("points",l.points);v.set("auto",l.autoPoints);likes.push(v);}
    out.set("likes",likes);
    Value picks=Value::array();
#ifndef MUTANT_CURATE_SAVE_LOST
    for(auto& p:picks_)for(auto& a:p.second){Value v=Value::object();v.set("player",p.first);v.set("area",a.first);v.set("asked",a.second.asked);
        Value ids=Value::array();for(auto& id:a.second.ids)ids.push(id);v.set("ids",ids);picks.push(v);}
#endif
    out.set("picks",picks);
    // Roads paved together; a world without any is written as before.
#ifndef MUTANT_HIGHWAY_SAVE_LOST
    Value highway=Value::array(),liked=Value::array();
    for(auto& d:devoted_)for(auto& k:d.second){Value v=Value::object();v.set("player",d.first);v.set("cid",k.first.first);v.set("rid",k.first.second);v.set("n",k.second);highway.push(v);}
    for(auto& l:highwayLikes_){Value v=Value::object();v.set("player",l.first);v.set("likes",l.second);liked.push(v);}
    if(!highway.items().empty())out.set("highway",highway);
    if(!liked.items().empty())out.set("highway_likes",liked);
#endif
    return out;
}
void World::restore(const json::Value& v){
    std::lock_guard<std::mutex> g(lock_);
    // Format 1 is from before the server policy: friends, everything shared, nothing withdrawn.
    int64_t format=v.get("format").integer();
    if(format!=1&&format!=2)throw std::runtime_error("world file: unknown format");
    Policy policy;
    if(format==2&&v.has("policy")){
        try{
            policy=presetPolicy(v.get("policy").get("preset").string());
            for(auto& m:v.get("policy").members())if(m.first!="preset")world::setPolicy(policy,m.first+"="+(m.second.isInt()?std::to_string(m.second.integer()):m.second.isString()?m.second.string():std::string("?")));
            // A world file from before identity keys: no keys, whatever its preset says now.
            if(!v.get("policy").has("identity"))policy.identity="none";
        }catch(const std::exception& e){throw std::runtime_error(std::string("world file: ")+e.what());}}
    const std::string owner=v.get("owner").string(),worldName=v.get("name").string();
    if(!owner.empty()&&!identity::accountIsKey(owner))throw std::runtime_error("world file: bad owner");
    if(v.has("name")&&!serverNameText(worldName))throw std::runtime_error("world file: bad server name");
    players_.clear();tokens_.clear();objects_.clear();byOrigin_.clear();tickets_.clear();relayTokens_.clear();noticeTokens_.clear();++changes_;policy_=policy;owner_=owner;name_=worldName;
    // A world file from before linked servers has no id: it keeps the one made for this process, and is saved with it.
    bool hadId=v.has("server_id");
    if(hadId){if(!serverIdText(v.get("server_id").string()))throw std::runtime_error("world file: bad server id");serverId_=v.get("server_id").string();}
    nextObject_=uint64_t(std::max<int64_t>(1,v.get("next_object").integer()));
    for(auto& p:v.get("players").items()){Player pl;pl.key=p.get("key").string();pl.id=p.get("id").string();pl.name=p.get("name").string();
        pl.offerBuilds=p.get("offer_builds").string();pl.offerOld=p.get("offer_old").string();pl.member=p.get("member").boolean();
        if(p.has("banned")&&p.get("banned").kind()!=Value::Bool)throw std::runtime_error("world file: bad player");
        pl.banned=p.get("banned").boolean();
        pl.showOthers=p.get("show_others").string();
        for(auto& f:p.get("friends").items()){if(!f.isString()||!keyText(f.string())||f.string()==pl.key)throw std::runtime_error("world file: bad friend");pl.friends.insert(f.string());}
        if(pl.key.empty()||pl.id!=playerId(pl.key)||!oneOf(pl.offerBuilds,{"","yes","no","ask"})||!oneOf(pl.offerOld,{"","yes","no"})||!oneOf(pl.showOthers,{"","everyone","friends","nobody"})
           ||(p.has("friends")&&!p.get("friends").isArray())||pl.friends.size()>MaxFriends)throw std::runtime_error("world file: bad player");
        players_[pl.key]=pl;}
    for(auto& t:v.get("sessions").members())if(playerById(t.second.string()))tokens_[t.first]=t.second.string();
    for(auto* passes:{&tickets_,&relayTokens_})
        for(auto& t:v.get(passes==&tickets_?"tickets":"relay_tokens").items()){
            Pass p{t.get("account").string(),uint64_t(t.get("relay_id").integer()),t.get("issued").integer(),t.get("used").integer()};const std::string text=t.get("text").string();
            if(!passText(text)||!identity::accountIsKey(p.account)||!players_.count(p.account)||(passes==&relayTokens_)!=(p.relayId!=0)||!passes->emplace(text,p).second)
                throw std::runtime_error("world file: bad ticket or relay token");}
    // The mods' notice tokens: for any account of this world (identity none has accounts that are no key).
    for(auto& t:v.get("notice_tokens").items()){
        Pass p{t.get("account").string(),0,t.get("issued").integer(),t.get("used").integer()};const std::string text=t.get("text").string();
        if(!passText(text)||!players_.count(p.account)||t.has("relay_id")||!noticeTokens_.emplace(text,p).second)throw std::runtime_error("world file: bad notice token");}
    for(auto& o:v.get("objects").items()){Object ob;ob.id=o.get("id").string();ob.creator=o.get("creator").string();ob.updated=o.get("updated").integer();ob.likes=o.get("likes").integer();ob.post=o.get("post");
        ob.name=o.get("name").string();ob.share=o.get("share").string();ob.remnant=o.get("remnant").boolean();ob.held=o.get("held").string();ob.heldAsk=o.get("held_ask").boolean();
        ob.kept=o.get("kept").string();
        ob.collection=o.get("collection").string();
        if((o.has("collection")&&(!o.get("collection").isString()||ob.collection.empty()||ob.collection.size()>64||o.has("origin")))||(o.has("cost")&&(!o.get("cost").isArray()||o.get("cost").items().size()!=6)))
            throw std::runtime_error("world file: bad object");
        if(o.has("cost")){ob.costed=true;size_t k=0;for(auto& n:o.get("cost").items()){if(!n.isInt()||n.integer()<0)throw std::runtime_error("world file: bad object");ob.cost[k++]=n.integer();}}
        if((!ob.kept.empty()&&ob.share!="kept")||!oneOf(ob.kept,{"","choice","owner","server"}))throw std::runtime_error("world file: bad object");
        if(ob.id.empty()||!ob.post.isObject()||!oneOf(ob.share,{"","kept","ask","review","held"})||(o.has("origin")&&(ob.remnant||!ob.share.empty()))
           ||(ob.share=="held")!=oneOf(ob.held,{"build","member","limit"})||(ob.heldAsk&&ob.share!="held"))throw std::runtime_error("world file: bad object");
        if(o.has("origin")){const Value& from=o.get("origin");ob.originServer=from.get("server").string();ob.originId=from.get("id").string();ob.via=from.get("via").string();
            ob.originName=from.get("name").string();ob.originLikes=from.get("likes").integer();ob.home=from.get("home").string();
            if(!serverIdText(ob.originServer)||!serverIdText(ob.via)||!objectIdText(ob.originId)||ob.originServer==serverId_||ob.home.size()>MaxNameBytes||!utf8Text(ob.home)
               ||!byOrigin_.emplace(std::make_pair(ob.originServer,ob.originId),ob.id).second)
                throw std::runtime_error("world file: bad imported object");}
        objects_[ob.id]=ob;}
    crates_.clear();
    for(auto& k:v.get("cargo").items()){Crate c;c.id=k.get("id").string();c.creator=k.get("creator").string();c.hash=uint64_t(k.get("hash").integer());
        c.x=k.get("x").real();c.y=k.get("y").real();c.z=k.get("z").real();c.yaw=k.get("yaw").real();c.area=k.get("area").integer();c.placed=k.get("placed").integer();
        if(!objectIdText(c.id)||c.creator.empty()||!c.hash||!crates_.emplace(c.id,c).second)throw std::runtime_error("world file: bad cargo");}
    likes_.clear();
    for(auto& l:v.get("likes").items()){Like k;k.to=l.get("to").string();k.from=l.get("from").string();k.object=l.get("object").string();k.type=l.get("type").string();
        k.time=l.get("time").integer();k.points=l.get("points").integer();k.autoPoints=l.get("auto").integer();
        if(likes_.size()<MaxLikes)likes_.push_back(k);}
    // What each player's game was handed per area; a world file from before has none.
    picks_.clear();
    for(auto& p:v.get("picks").items()){
        const std::string player=p.get("player").string();auto& mine=picks_[player];
        if(!playerById(player)||!p.get("area").isInt()||!p.get("asked").isInt()||!p.get("ids").isArray()||p.get("ids").items().empty()||p.get("ids").items().size()>MaxPicksPerArea
           ||mine.count(p.get("area").integer())||mine.size()>=MaxPickAreas)throw std::runtime_error("world file: bad picks");
        Picks k;k.asked=p.get("asked").integer();
        for(auto& id:p.get("ids").items()){if(!id.isString()||!objectIdText(id.string()))throw std::runtime_error("world file: bad picks");k.ids.push_back(id.string());}
        mine[p.get("area").integer()]=k;}
    // Roads paved together: what each player put into each paver, and the likes for it; a world file from before has
    // neither.
    devoted_.clear();highwayLikes_.clear();
    if(v.has("highway")&&!v.get("highway").isArray())throw std::runtime_error("world file: bad highway");
    for(auto& h:v.get("highway").items()){
        const std::string player=h.get("player").string();const int64_t cid=h.get("cid").integer(-1),rid=h.get("rid").integer(-1),n=h.get("n").integer(-1);
        if(!playerById(player)||!h.get("cid").isInt()||!h.get("rid").isInt()||!h.get("n").isInt()
           ||cid<1||cid>MaxPaverId||rid<0||rid>=HighwayResources||n<1||n>MaxHighwayTotal)throw std::runtime_error("world file: bad highway");
        auto& mine=devoted_[player];
        if(mine.count({cid,rid})||mine.size()>=MaxHighwayKeys)throw std::runtime_error("world file: bad highway");
        mine[{cid,rid}]=n;}
    if(v.has("highway_likes")&&!v.get("highway_likes").isArray())throw std::runtime_error("world file: bad highway likes");
    for(auto& l:v.get("highway_likes").items()){
        const std::string player=l.get("player").string();const int64_t n=l.get("likes").integer(-1);
        if(!playerById(player)||!l.get("likes").isInt()||n<1||n>MaxHighwayTotal||highwayLikes_.count(player))throw std::runtime_error("world file: bad highway likes");
        highwayLikes_[player]=n;}
    dirty_=!hadId;
}
// ---- Linked servers: filters, the travelling form of an object, export, import, forget.
bool serverIdText(const std::string& s){return lettersAndDigits(s,4,32);}
bool objectIdText(const std::string& s){return lettersAndDigits(s,2,64);}
Filter parseFilter(const std::string& text){
    Filter f;std::string all=trim(text);
    if(all=="all")return f;
    if(all=="none"){f.none=true;return f;}
    if(all.empty())bad("filter: empty (write all, none or terms such as types=l,r;areas=226)");
    std::set<std::string> seen;
    for(auto& term:split(all,';')){
        auto eq=term.find('=');if(eq==std::string::npos)bad("filter: '"+term+"' is not key=value");
        std::string key=trim(term.substr(0,eq)),value=trim(term.substr(eq+1));
        if(!seen.insert(key).second)bad("filter: "+key+" is given twice");
        if(value.empty())bad("filter: "+key+" has no value");
        if(key=="types"){
            for(auto& t:split(value,',')){
                std::string letter;
                if(t.size()==1&&t[0]>='a'&&t[0]<='z')letter=t;
                else for(auto& n:TypeNames)if(t==n.first)letter=std::string(1,n.second);
                if(letter.empty())bad("filter: unknown object type '"+t+"'");
                f.types.insert(letter);}}
        else if(key=="owners"){
            for(auto& o:split(value,',')){if(o.empty()||o.size()>MaxNameBytes)bad("filter: bad owner");f.owners.insert(o);}}
        else if(key=="areas"){
            for(auto& a:split(value,',')){int64_t n=0;if(!wholeNumber(a,n))bad("filter: area '"+a+"' is not a number");f.areas.insert(n);}}
        else if(key=="max-age"){
            int64_t n=0;char unit=value.back();int64_t scale=unit=='s'?1:unit=='m'?60:unit=='h'?3600:unit=='d'?86400:0;
            if(!scale||!wholeNumber(value.substr(0,value.size()-1),n)||n<0||n>1000000)bad("filter: max-age is a number and s, m, h or d (30d)");
            f.maxAgeSeconds=n*scale;}
        else if(key=="likes"||key=="imported"){
            if(value!="yes"&&value!="no")bad("filter: "+key+" is yes or no");
            (key=="likes"?f.likes:f.imported)=value=="yes";}
        else bad("filter: unknown term '"+key+"'");}
    return f;
}
bool passes(const Filter& f,const Shared& s,bool imported,int64_t now){
    if(f.none)return false;
#ifndef MUTANT_LINK_PASS_IMPORTED
    if(imported&&!f.imported)return false;
#else
    (void)imported;
#endif
#ifndef MUTANT_LINK_FILTER_TYPE
    if(!f.types.empty()&&!f.types.count(s.type()))return false;
#endif
#ifndef MUTANT_LINK_FILTER_OWNER
    if(!f.owners.empty()&&!f.owners.count(s.creator)&&(s.name.empty()||!f.owners.count(s.name)))return false;
#endif
#ifndef MUTANT_LINK_FILTER_AREA
    if(!f.areas.empty()&&!f.areas.count(s.area()))return false;
#endif
#ifndef MUTANT_LINK_FILTER_AGE
    if(f.maxAgeSeconds>=0&&now-s.updated>f.maxAgeSeconds)return false;
#else
    (void)now;
#endif
    return true;
}
json::Value writeShared(const Shared& s){
    Value v=Value::object();v.set("server",s.server);v.set("id",s.id);v.set("creator",s.creator);v.set("name",s.name);v.set("updated",s.updated);v.set("likes",s.likes);v.set("post",s.post);
    if(!s.home.empty())v.set("home",s.home);   // only when its server has a display name: the form from before otherwise
    return v;}
Shared readShared(const json::Value& v){
    // Seven fields, or eight with `home` (since display names): a document from before is read as it was.
    if(!v.isObject()||!(v.members().size()==7||(v.members().size()==8&&v.has("home"))))bad("import: an object is not in the export form");
    if(v.has("home")&&(!v.get("home").isString()||v.get("home").string().empty()||v.get("home").string().size()>MaxNameBytes||!utf8Text(v.get("home").string())))
        bad("import: an object's home server name is not a name");
    for(const char* k:{"server","id","creator","name"})if(!v.get(k).isString())bad(std::string("import: an object has no ")+k);
    Shared s;s.server=v.get("server").string();s.id=v.get("id").string();s.creator=v.get("creator").string();s.name=v.get("name").string();
    if(!serverIdText(s.server))bad("import: bad origin server id");
    if(!objectIdText(s.id))bad("import: bad object id");
    if(!lettersAndDigits(s.creator,1,64,"_"))bad("import: bad builder id");
    if(s.name.size()>MaxNameBytes)bad("import: builder name too long");
    for(unsigned char c:s.name)if(c<0x20||c==0x7f)bad("import: control character in a builder name");
    if(!v.get("updated").isInt()||!v.get("likes").isInt())bad("import: an object's time or likes is not a whole number");
    s.updated=v.get("updated").integer();s.likes=v.get("likes").integer();
    if(s.updated<0||s.likes<0||s.likes>1000000000000ll)bad("import: an object's time or likes is out of range");
    s.post=v.get("post");s.home=v.get("home").string();
    if(!s.post.isObject())bad("import: an object has no data");
    const Value& t=s.post.get("t");
    if(!t.isString()||t.string().size()!=1||t.string()[0]<'a'||t.string()[0]>'z')bad("import: object type must be one letter");
    if(s.post.has("q")&&!s.post.get("q").isInt())bad("import: an object's area is not a number");
#ifndef MUTANT_LINK_SIZE
    if(s.post.dump().size()>MaxSharedPostBytes)bad("import: an object is too large");
#endif
    return s;
}
void World::setServerId(const std::string& id){
    std::lock_guard<std::mutex> g(lock_);if(!serverIdText(id))bad("server id: 4 to 32 letters and digits");
    serverId_=id;dirty_=true;}
void World::setName(const std::string& n){
    if(!n.empty()&&!serverNameText(n))bad("server name: 1 to "+std::to_string(MaxHomeChars)+" characters (at most "+std::to_string(MaxHomeBytes)+" bytes of UTF-8), no control characters, no space at either end");
    std::lock_guard<std::mutex> g(lock_);
    if(name_!=n){name_=n;dirty_=true;++changes_;note(n.empty()?"no server name":"server name "+n);}   // what an export carries changed
}
void World::setLinkName(const std::string& server,const std::string& n){std::lock_guard<std::mutex> g(lock_);if(serverIdText(server)&&!n.empty())linkNames_[server]=n;}
std::string World::homeLocked(const std::string& server,const std::string& given) const {
    if(!given.empty())return given;
    auto l=linkNames_.find(server);if(l!=linkNames_.end())return l->second;
    return server.substr(0,HomeIdChars);
}
std::string World::homeName(const std::string& server) const {
    std::lock_guard<std::mutex> g(lock_);
    for(auto& o:objects_)if(o.second.originServer==server&&!o.second.home.empty())return homeLocked(server,o.second.home);   // what it calls itself
    return homeLocked(server,{});
}
Shared World::sharedOf(const Object& o) const {
    Shared s;bool imported=!o.originServer.empty();
    s.server=imported?o.originServer:serverId_;s.id=imported?o.originId:o.id;s.creator=o.creator;s.updated=o.updated;s.likes=o.likes;s.post=servedPost(o.post);
    s.home=imported?o.home:name_;   // an import passes on what its first server said of itself, nothing made up here
    if(imported)s.name=o.originName;else if(o.remnant)s.name=o.name;else if(auto* p=playerById(o.creator))s.name=p->name;else s.name=o.name;
    return s;
}
json::Value World::exportObjects(const Filter& f,const std::string& forServer,int64_t now,const std::string& after,size_t limit,size_t bytes) const {
    std::lock_guard<std::mutex> g(lock_);
    Value out=Value::object();out.set("format",1);out.set("kind",DocumentKind);out.set("server",serverId_);out.set("time",now);out.set("version",versionLocked());
    Value list=Value::array();bool more=false;std::string next;size_t size=0;
    for(auto it=after.empty()?objects_.begin():objects_.upper_bound(after);it!=objects_.end();++it){
        const Object& o=it->second;bool imported=!o.originServer.empty();
#ifndef MUTANT_POLICY_EXPORT_PENDING
        if(!o.share.empty())continue;   // kept or held for its builder, or waiting: nobody else is handed it, nor is another server
#endif
#ifndef MUTANT_LINK_ECHO
        // Never back to where it came from, whatever the filter says.
        if(imported&&!forServer.empty()&&(o.originServer==forServer||o.via==forServer))continue;
#endif
        Shared s=sharedOf(o);
        if(!passes(f,s,imported,now))continue;
        if(!f.likes)s.likes=0;
        // What the other side would refuse never leaves: an object of a shape or size the travelling form does not allow.
        if(!objectIdText(s.id)||s.type().size()!=1||s.type()[0]<'a'||s.type()[0]>'z'||(s.post.has("q")&&!s.post.get("q").isInt()))continue;
        if(s.name.size()>MaxNameBytes)s.name.clear();
        if(s.home.size()>MaxNameBytes||!utf8Text(s.home))s.home.clear();
        Value v=writeShared(s);size_t n=v.dump().size();
        if(s.post.dump().size()>MaxSharedPostBytes)continue;
        if(list.items().size()>=limit||(size+n>bytes&&!list.items().empty())){more=true;break;}
        size+=n;list.push(std::move(v));next=it->first;}
    out.set("objects",list);out.set("more",more);out.set("next",next);return out;
}
std::string World::importedId(char type,const std::pair<std::string,std::string>& origin) const {
#ifdef MUTANT_LINK_KEEP_ID
    (void)type;return origin.second;
#else
    // The game's own shape (the type letter and 12 characters), made from where the object was first built: the same
    // origin gets the same id here every time, and an id that is taken (by anything) is stepped past.
    for(uint64_t n=0;;++n){
        std::string id(1,type);
        uint64_t a=fnv(origin.first+"/"+origin.second,n*0x9e3779b97f4a7c15ull),b=fnv(origin.second+"/"+origin.first,~n*0xbf58476d1ce4e5b9ull);
        for(int i=0;i<6;++i){id+=Digits62[a%62];a/=62;}
        for(int i=0;i<6;++i){id+=Digits62[b%62];b/=62;}
        if(!objects_.count(id))return id;}
#endif
}
ImportResult World::importObjects(const json::Value& document,const Filter& f,bool prune,int64_t now){
    if(!document.isObject()||document.get("format").integer()!=1||document.get("kind").string()!=DocumentKind)bad("import: not an export of coop-world objects (format 1)");
    if(!document.get("server").isString()||!document.get("objects").isArray())bad("import: no server id or no object list");
    std::vector<Shared> list;
    for(auto& v:document.get("objects").items()){
#ifndef MUTANT_LINK_STRICT
        list.push_back(readShared(v));
#else
        try{list.push_back(readShared(v));}catch(const std::exception&){}
#endif
    }
    return importShared(document.get("server").string(),list,f,prune,now);
}
ImportResult World::importShared(const std::string& via,const std::vector<Shared>& list,const Filter& f,bool prune,int64_t now){
    std::lock_guard<std::mutex> g(lock_);
    using Key=std::pair<std::string,std::string>;
    if(!serverIdText(via))bad("import: bad server id");
    if(via==serverId_)bad("import: this is this server's own export");
    ImportResult r;std::set<Key> listed,kept;std::vector<const Shared*> take;size_t adds=0;
    for(auto& s:list){
        Key k{s.server,s.id};
        if(!listed.insert(k).second)bad("import: an object is listed twice");
#ifndef MUTANT_LINK_IMPORT_OWN
        if(s.server==serverId_){++r.own;continue;}   // built here: it is already here, or was removed here
#endif
        if(!passes(f,s,s.server!=via,now)){++r.filtered;continue;}
        kept.insert(k);take.push_back(&s);if(!byOrigin_.count(k))++adds;}
    size_t have=0;for(auto& o:objects_)if(o.second.via==via)++have;
#ifndef MUTANT_LINK_CAP
    if(prune?take.size()>MaxImportedPerServer:have+adds>MaxImportedPerServer)bad("import: too many objects from one server");
#endif
    for(auto* s:take){
        Key k{s->server,s->id};int64_t likes=f.likes?s->likes:0;
        auto found=byOrigin_.find(k);
#ifdef MUTANT_LINK_DUPLICATE
        found=byOrigin_.end();
#endif
        if(found!=byOrigin_.end()&&!objects_.count(found->second)){byOrigin_.erase(found);found=byOrigin_.end();}
        if(found!=byOrigin_.end()){
            Object& o=objects_.find(found->second)->second;
            if(o.id[0]==s->type()[0]){
                // Held already. From another server than the one it was taken from, only a newer state replaces it.
                if(o.via!=via&&s->updated<=o.updated){++r.unchanged;continue;}
                if(o.updated==s->updated&&o.originLikes==likes&&o.creator==s->creator&&o.originName==s->name&&o.home==s->home&&o.post.dump()==s->post.dump()){++r.unchanged;continue;}
#ifndef MUTANT_LINK_LIKES
                o.likes=std::max<int64_t>(0,o.likes+likes-o.originLikes);   // the likes given here stay
#else
                o.likes=likes;
#endif
                o.originLikes=likes;o.creator=s->creator;o.originName=s->name;o.home=s->home;o.updated=s->updated;o.post=s->post;touch(o);++r.updated;continue;}
            dropped(found->second);objects_.erase(found->second);byOrigin_.erase(found);   // another kind now: another id (the id says the kind)
        }
        Object o;o.id=importedId(s->type()[0],k);o.creator=s->creator;o.post=s->post;o.updated=s->updated;o.likes=o.originLikes=likes;
        o.originServer=k.first;o.originId=k.second;o.via=via;o.originName=s->name;o.home=s->home;touch(o);
        byOrigin_[k]=o.id;objects_[o.id]=std::move(o);++r.added;}
#ifndef MUTANT_LINK_NO_PRUNE
    if(prune)for(auto it=objects_.begin();it!=objects_.end();){
        if(it->second.via==via&&!kept.count({it->second.originServer,it->second.originId})){byOrigin_.erase({it->second.originServer,it->second.originId});dropped(it->first);it=objects_.erase(it);++r.removed;}
        else ++it;}
#endif
    if(r.added||r.updated||r.removed){dirty_=true;++changes_;}
    return r;
}
size_t World::forget(const std::string& server){
    std::lock_guard<std::mutex> g(lock_);size_t n=0;
    for(auto it=objects_.begin();it!=objects_.end();){
        const Object& o=it->second;
#ifndef MUTANT_LINK_FORGET_ANY
        bool gone=!o.originServer.empty()&&(o.originServer==server||o.via==server);
#else
        bool gone=!o.originServer.empty();
#endif
        if(gone){byOrigin_.erase({o.originServer,o.originId});dropped(it->first);it=objects_.erase(it);++n;}else ++it;}
    if(n){dirty_=true;++changes_;note("forgot "+std::to_string(n)+" objects of server "+server);}
    return n;
}
std::vector<World::Origin> World::origins() const {
    std::lock_guard<std::mutex> g(lock_);std::map<std::pair<std::string,std::string>,size_t> count;
    for(auto& o:objects_)if(!o.second.originServer.empty())++count[{o.second.originServer,o.second.via}];
    std::vector<Origin> out;for(auto& c:count)out.push_back({c.first.first,c.first.second,c.second});return out;
}
Reply World::linkPull(const Filter& f,const std::string& body,int64_t now) const {
    std::string server,have,after;
    try{
        if(body.size()>MaxLinkRequestBytes)bad("too long");
        Value in=json::parse(body);
        if(!in.isObject())bad("not an object");
        for(auto& m:in.members())if(m.first!="format"&&m.first!="server"&&m.first!="have"&&m.first!="after")bad("unknown field");
        if(!in.get("format").isInt()||in.get("format").integer()!=1)bad("format");
        for(const char* k:{"server","have","after"})if(!in.get(k).isString())bad(k);
        server=in.get("server").string();have=in.get("have").string();after=in.get("after").string();
        if(!serverIdText(server)||have.size()>64||(!after.empty()&&!objectIdText(after)))bad("value");
    }catch(const std::exception& e){note(std::string("link request refused: ")+e.what());return refuse(400,"bad-link","link request refused");}
    {std::lock_guard<std::mutex> g(lock_);
     if(server==serverId_){note("link request refused: the asking server has this server's id");return refuse(400,"bad-link","link request refused");}
     if(!have.empty()&&after.empty()&&have==versionLocked()){
         Value same=Value::object();same.set("format",1);same.set("kind",DocumentKind);same.set("server",serverId_);same.set("version",versionLocked());same.set("same",true);return ok(same);}}
    return ok(exportObjects(f,server,now,after,LinkPageObjects,LinkPageBytes));
}
// ---- Which objects a game is handed (README, "Which objects a game is handed").
Curated curate(const std::string& player,int64_t area,const std::map<std::string,int64_t>* counts,const std::vector<Candidate>& candidates,
               const std::vector<std::string>& previous,const Policy& policy,const std::set<std::string>& friends){
    // Who may be handed what: every player's shared objects of this area, never the asker's own (by the builder the
    // games are told: what he imported from a collection is told as another builder's, ../docs/COLLECTIONS.md).
    // Whose an object is (`owner`): its builder, or the importer of a structure from a collection.
    auto owner=[](const Candidate& c)->const std::string&{return c.owner.empty()?c.creator:c.owner;};
    std::vector<const Candidate*> pool;std::map<std::string,const Candidate*> byId;
    for(auto& c:candidates){
        if(c.area!=area)continue;
#if !defined(MUTANT_WORLD_OWN)&&!defined(MUTANT_CURATE_OWN)
        if(c.creator==player)continue;
#endif
        // Kept, held or waiting: nobody's but its owner's. His own builds are gone already; what is left of his is what
        // he imported, which only the server has, so he is handed it in any state (it is his, as getQpidObjects says).
#ifndef MUTANT_CURATE_UNSHARED
#ifndef MUTANT_COLLECTION_UNSHARED_HIDDEN
        if(!c.shared&&owner(c)!=player)continue;
#else
        if(!c.shared)continue;
#endif
#endif
        pool.push_back(&c);byId[c.id]=&c;}
    // The order: newest first; with `liked`, the asker's friends first, then the most liked, then the newest.
    const bool liked=policy.order=="liked";
    std::stable_sort(pool.begin(),pool.end(),[&](const Candidate* a,const Candidate* b){
#ifndef MUTANT_CURATE_ORDER
        if(liked){bool fa=friends.count(owner(*a))>0,fb=friends.count(owner(*b))>0;if(fa!=fb)return fa;if(a->likes!=b->likes)return a->likes>b->likes;}
        if(a->updated!=b->updated)return a->updated>b->updated;
#else
        (void)liked;
#endif
        return a->id<b->id;});
    Curated out;std::set<std::string> taken;std::map<std::string,int64_t> left;if(counts)left=*counts;
    std::map<std::pair<std::string,std::string>,int64_t> byBuilder;std::map<std::string,std::vector<const Candidate*>> placed;
    auto room=[&](const Candidate& c){
        if(!counts)return true;
        auto l=left.find(c.type);
#ifndef MUTANT_CURATE_COUNTS
        return l!=left.end()&&l->second>0;
#else
        return l!=left.end()&&l->second>=0;
#endif
    };
    auto take=[&](const Candidate& c){out.handed.push_back(c.id);taken.insert(c.id);if(counts)--left[c.type];++byBuilder[{c.type,owner(c)}];if(c.placed)placed[c.type].push_back(&c);};
    // What this game was handed here before comes first, as long as it is still there and shared.
#ifndef MUTANT_CURATE_UNSTABLE
    for(auto& id:previous){auto f=byId.find(id);if(f!=byId.end()&&!taken.count(id)&&room(*f->second))take(*f->second);}
#endif
    for(auto* c:pool){
        if(taken.count(c->id)||!room(*c))continue;
#ifndef MUTANT_CURATE_BUILDER_CAP
        // No one builder fills a kind of an area.
        if(policy.perBuilder>0&&byBuilder[{c->type,owner(*c)}]>=policy.perBuilder)continue;
#endif
#ifndef MUTANT_CURATE_SPREAD
        // The game fails an object placed too close to another of its kind: one farther away is handed instead.
        if(policy.spread>0&&c->placed){
            bool near=false;double d2=double(policy.spread)*double(policy.spread);
            for(auto* o:placed[c->type]){double dx=c->x-o->x,dy=c->y-o->y,dz=c->z-o->z;if(dx*dx+dy*dy+dz*dz<d2){near=true;break;}}
            if(near)continue;}
#endif
        take(*c);}
    // Remembered for next time: what was handed now, then what was handed before and is still there (a kind the game
    // did not ask for this time, or fewer of it).
    out.remember=out.handed;
    for(auto& id:previous)if(out.remember.size()<MaxPicksPerArea&&byId.count(id)&&!taken.count(id)){out.remember.push_back(id);taken.insert(id);}
    if(out.remember.size()>MaxPicksPerArea)out.remember.resize(MaxPicksPerArea);
    return out;
}
Candidate World::candidateOf(const Object& o) const {
    Candidate c;const Value& p=o.post;
    c.id=o.id;c.creator=shownCreator(o);c.owner=o.creator;c.type=p.get("t").string();c.area=p.get("q").integer();c.updated=o.updated;c.likes=o.likes;c.shared=o.share.empty();
    // The game's own reading: l / 10^exp, and an exp of zero or less counts as 2 (mod/game_offsets.md).
    if(p.get("px").isInt()&&p.get("py").isInt()&&p.get("pz").isInt()){
        int64_t e=p.get("exp").integer();if(e<=0)e=2;if(e>18)e=18;double scale=std::pow(10.0,double(e));
        c.placed=true;c.x=double(p.get("px").integer())/scale;c.y=double(p.get("py").integer())/scale;c.z=double(p.get("pz").integer())/scale;}
    return c;
}
void World::remember(const std::string& player,int64_t area,const std::vector<std::string>& ids,int64_t now){
    auto mine=picks_.find(player);
    if(ids.empty()){
        if(mine!=picks_.end()&&mine->second.erase(area)){dirty_=true;if(mine->second.empty())picks_.erase(mine);}
        return;}
    auto& areas=picks_[player];auto& p=areas[area];
    if(p.ids!=ids){p.ids=ids;dirty_=true;}
    p.asked=now;
#ifndef MUTANT_CURATE_UNBOUNDED
    // Bounded: the areas this player asked for longest ago are forgotten first.
    while(areas.size()>MaxPickAreas){
        auto oldest=areas.end();
        for(auto it=areas.begin();it!=areas.end();++it)if(it->first!=area&&(oldest==areas.end()||it->second.asked<oldest->second.asked))oldest=it;
        areas.erase(oldest);dirty_=true;}
#endif
}
// ---- Server policy (README, "Server policy"): presets, settings, the players' choices, withdrawal, moderation.
Policy presetPolicy(const std::string& name){
    Policy p;p.preset=name;
    if(name=="friends")return p;
    if(name=="solo"){p.players=1;p.chat="off";p.map="off";p.cargo="off";return p;}
    // Everyone on a public server sees the same world: the server's. Friends and a solo server keep what each save holds.
    if(name=="public"){p.chat="off";p.map="off";p.world="server";p.join="open";p.build="members";p.acceptOffers="review";p.limit=200;p.withdrawal="stay";p.order="liked";p.perBuilder=3;p.identity="keys";p.nearCap=16;p.challenges="members";p.importWho="members";return p;}
    bad("policy: unknown preset '"+name+"' (friends, solo or public)");
}
void setPolicy(Policy& p,const std::string& keyValue){
    auto eq=keyValue.find('=');if(eq==std::string::npos)bad("policy: '"+keyValue+"' is not KEY=VALUE");
    const std::string key=trim(keyValue.substr(0,eq)),value=trim(keyValue.substr(eq+1));
    auto pick=[&](std::string& field,std::initializer_list<const char*> allowed){
        if(oneOf(value,allowed)){field=value;return;}
        std::string list;for(auto a:allowed)list+=std::string(list.empty()?"":", ")+a;bad("policy: "+key+" is one of "+list);};
    auto number=[&](int64_t& field,int64_t least,int64_t most){
        int64_t n=0;if(!wholeNumber(value,n)||n<least||n>most)bad("policy: "+key+" is a number from "+std::to_string(least)+" to "+std::to_string(most));field=n;};
    if(key=="join")pick(p.join,{"invite","open"});
    else if(key=="build")pick(p.build,{"everyone","members","nobody"});
    else if(key=="accept_offers")pick(p.acceptOffers,{"auto","review","never"});
    else if(key=="withdrawal")pick(p.withdrawal,{"stay","go"});
    else if(key=="offer_builds")pick(p.offerBuilds,{"yes","no","ask"});
    else if(key=="offer_old")pick(p.offerOld,{"yes","no"});
    else if(key=="limit")number(p.limit,0,int64_t(MaxObjectsPerPlayer));
    else if(key=="players")number(p.players,1,int64_t(MaxAccounts));
    else if(key=="order")pick(p.order,{"newest","liked"});
    else if(key=="per_builder")number(p.perBuilder,0,1000);
    else if(key=="spread")number(p.spread,0,1000);
    else if(key=="identity")pick(p.identity,{"none","keys"});
    else if(key=="near_cap")number(p.nearCap,0,MaxNearCap);
    else if(key=="challenges")pick(p.challenges,{"everyone","members","owner","nobody"});
    else if(key=="import")pick(p.importWho,{"everyone","members","owner","nobody"});
    else if(key=="import_cost")pick(p.importCost,{"free","materials"});
    else if(key=="chat")pick(p.chat,{"on","off"});
    else if(key=="cargo")pick(p.cargo,{"on","off"});
    else if(key=="map")pick(p.map,{"on","off"});
    else if(key=="world")pick(p.world,{"save","server"});
    else if(key=="free_build")pick(p.freeBuild,{"off","on"});
    else if(key=="test_mode")pick(p.testMode,{"off","on"});
    else bad("policy: unknown setting '"+key+"' (join, build, accept_offers, limit, withdrawal, players, offer_builds, offer_old, order, per_builder, spread, identity, near_cap, challenges, import, import_cost, chat, map, world, cargo, free_build, test_mode)");
}
std::vector<std::pair<std::string,std::string>> policyItems(const Policy& p){
    return {{"join",p.join},{"build",p.build},{"accept_offers",p.acceptOffers},{"limit",std::to_string(p.limit)},{"withdrawal",p.withdrawal},
            {"players",std::to_string(p.players)},{"offer_builds",p.offerBuilds},{"offer_old",p.offerOld},
            {"order",p.order},{"per_builder",std::to_string(p.perBuilder)},{"spread",std::to_string(p.spread)},{"identity",p.identity},{"near_cap",std::to_string(p.nearCap)},{"challenges",p.challenges},
            {"import",p.importWho},{"import_cost",p.importCost},{"chat",p.chat},{"map",p.map},{"world",p.world},{"cargo",p.cargo},{"free_build",p.freeBuild},{"test_mode",p.testMode}};
}
void World::setPolicy(const Policy& p){std::lock_guard<std::mutex> g(lock_);policy_=p;dirty_=true;++changes_;}
bool World::handsBack(const Object& o,const std::string& player) const {
    return policy_.world=="server"&&o.creator==player&&o.collection.empty()&&o.originServer.empty()&&!o.remnant;}
bool World::servedTo(const Object& o,const std::string& player) const {
    if(o.creator==player)return true;   // his own, whatever its state (the game never asks a list of those)
#ifndef MUTANT_POLICY_PENDING_SERVED
    if(o.share=="ask"||o.share=="review")return false;
#endif
#ifndef MUTANT_POLICY_OFFER_NO_SERVED
    if(o.share=="kept")return false;
#endif
#ifndef MUTANT_POLICY_HELD_SERVED
    if(o.share=="held")return false;
#endif
    return true;
}
// What the policy says to one more shared object of this player (`counted`: it counts for the limit already, as an
// object that waits does): "" when it may be shared, else build, member or limit.
std::string World::blocked(const Player& p,bool counted) const {
#ifndef MUTANT_POLICY_BUILD_NOBODY
    if(policy_.build=="nobody")return "build";
#endif
#ifndef MUTANT_POLICY_BUILD_MEMBERS
    if(policy_.build=="members"&&!p.member)return "member";
#endif
#ifndef MUTANT_POLICY_LIMIT
    if(policy_.limit>0&&!counted&&offered(p.id)>=size_t(policy_.limit))return "limit";
#else
    (void)counted;
#endif
    return "";
}
size_t World::offered(const std::string& player) const {
    size_t n=0;
    for(auto& o:objects_)if(o.second.creator==player&&o.second.originServer.empty()&&!o.second.remnant&&oneOf(o.second.share,{"","ask","review"}))++n;
    return n;
}
// Roads paved together: a player's paver contributions reach the others exactly while an upload of his would be shared
// at once (his standing yes, a server that takes offers without review, and he may build here). Nobody can be asked
// about a delivery, and there is no review of one; whatever stops them now also stops what he put in before.
bool World::contributes(const Player& p) const {
#ifndef MUTANT_HIGHWAY_CHOICE
    if((p.offerBuilds.empty()?policy_.offerBuilds:p.offerBuilds)!="yes")return false;
#endif
#ifndef MUTANT_HIGHWAY_REVIEW
    if(policy_.acceptOffers!="auto")return false;
#endif
#ifndef MUTANT_HIGHWAY_BUILD
    if(policy_.build=="nobody"||(policy_.build=="members"&&!p.member))return false;
#endif
    return true;
}
std::string World::shareAfterOffer() const {
#ifndef MUTANT_POLICY_REVIEW
    if(policy_.acceptOffers=="review")return "review";
#endif
    return policy_.acceptOffers=="never"?"kept":"";
}
void World::setPlayer(const std::string& id,const std::string& keyValue){
    std::lock_guard<std::mutex> g(lock_);
    Player* p=nullptr;for(auto& q:players_)if(q.second.id==id)p=&q.second;
    if(!p)bad("no player "+id+" on this server (coop-world players lists them)");
    auto eq=keyValue.find('=');if(eq==std::string::npos)bad("'"+keyValue+"' is not KEY=VALUE");
    const std::string key=trim(keyValue.substr(0,eq)),value=trim(keyValue.substr(eq+1));
    if(key=="offer_builds"&&oneOf(value,{"yes","no","ask","default"}))p->offerBuilds=value=="default"?"":value;
    else if(key=="offer_old"&&oneOf(value,{"yes","no","default"}))p->offerOld=value=="default"?"":value;
    else if(key=="member"&&oneOf(value,{"yes","no"}))p->member=value=="yes";
    else if(key=="banned"&&oneOf(value,{"yes","no"})){p->banned=value=="yes";if(p->banned)kickLocked(*p);note(p->id+(p->banned?" banned":" no longer banned"));}
    else bad("'"+keyValue+"': offer_builds=yes|no|ask|default, offer_old=yes|no|default, member=yes|no or banned=yes|no");
    dirty_=true;++changes_;
}
// Kick and ban (README, "Kick and ban"): every session and pass of the player ends at once.
size_t World::kickLocked(Player& p){
    size_t n=0;
#ifndef MUTANT_BAN_KEEP_SESSION
    for(auto it=tokens_.begin();it!=tokens_.end();)if(it->second==p.id){it=tokens_.erase(it);++n;}else ++it;
#endif
#ifndef MUTANT_BAN_RELAY_KEEP
    if(identity::accountIsKey(p.key))evictions_.push_back(p.key);
#endif
#ifndef MUTANT_BAN_KEEP_PASSES
    for(auto* passes:{&tickets_,&relayTokens_,&noticeTokens_})
        for(auto it=passes->begin();it!=passes->end();)if(it->second.account==p.key){it=passes->erase(it);++n;}else ++it;
#endif
    dirty_=true;return n;
}
size_t World::kick(const std::string& id){
    std::lock_guard<std::mutex> g(lock_);
    for(auto& q:players_)if(q.second.id==id){size_t n=kickLocked(q.second);note(id+" kicked: "+std::to_string(n)+" sessions and passes ended");return n;}
    bad("no player "+id+" on this server (coop-world players lists them)");return 0;
}
void World::ban(const std::string& id,bool yes){setPlayer(id,std::string("banned=")+(yes?"yes":"no"));}
bool World::banned(const std::string& account) const {
    std::lock_guard<std::mutex> g(lock_);auto it=players_.find(account);return it!=players_.end()&&it->second.banned;}
std::vector<World::PlayerInfo> World::playerList() const {
    std::lock_guard<std::mutex> g(lock_);std::vector<PlayerInfo> out;std::map<std::string,size_t> at;
    for(auto& p:players_){PlayerInfo i;i.id=p.second.id;i.name=p.second.name;i.key=p.second.key;i.offerBuilds=p.second.offerBuilds;i.offerOld=p.second.offerOld;i.member=p.second.member;i.banned=p.second.banned;
        at[i.id]=out.size();out.push_back(i);}
    for(auto& o:objects_){
        const Object& ob=o.second;auto found=at.find(ob.creator);
        // A builder known here only from imports is named as the game names him ("Name · Home").
        if(found==at.end()){PlayerInfo i;i.id=ob.creator;i.name=ob.originServer.empty()?ob.name:builderLabel(ob.originName.empty()?"Porter":ob.originName,homeLocked(ob.originServer,ob.home));
            i.known=false;found=at.emplace(ob.creator,out.size()).first;out.push_back(i);}
        PlayerInfo& i=out[found->second];
        if(!ob.originServer.empty())++i.imported;else if(ob.remnant)++i.remnants;else if(ob.share.empty())++i.shared;else if(ob.share=="kept")++i.kept;
        else if(ob.share=="held")++i.held;else ++i.pending;
        if(ob.originServer.empty()&&!ob.remnant)i.likes+=ob.likes;}
    return out;
}
std::vector<World::Waiting> World::waiting() const {
    std::lock_guard<std::mutex> g(lock_);std::vector<Waiting> out;
    for(auto& o:objects_)if(oneOf(o.second.share,{"ask","review","held"})){
        Waiting w;w.id=o.first;w.creator=o.second.creator;w.type=o.second.post.get("t").string();w.area=o.second.post.get("q").integer();w.updated=o.second.updated;
        w.forBuilder=o.second.share=="ask"||o.second.heldAsk;w.held=o.second.held;auto* p=playerById(o.second.creator);w.name=p?p->name:o.second.name;out.push_back(w);}
    return out;
}
size_t World::review(const std::string& object,bool accept){
    std::lock_guard<std::mutex> g(lock_);
    auto found=objects_.find(object);
    if(found==objects_.end()||(found->second.share!="review"&&found->second.share!="held"))bad("review: "+object+" does not wait for the owner and is not held (coop-world review lists what does)");
    Object& o=found->second;
    if(accept&&o.share=="held"){
        if(o.heldAsk)bad("review: "+object+" waits for its builder's answer first (coop-world offer)");
        const Player* p=playerById(o.creator);if(!p)bad("review: "+object+" has no builder here");
        std::string why=blocked(*p,false);
#ifdef MUTANT_POLICY_HELD_RELEASE
        why.clear();
#endif
        if(!why.empty())bad("review: "+object+" stays held: "+heldText(why));}
#ifndef MUTANT_NOTICES_KEPT_WHY
    o.kept=accept?"":"owner";
#else
    o.kept=accept?"":"choice";
#endif
    o.share=accept?"":"kept";o.held.clear();o.heldAsk=false;
#ifndef MUTANT_NOTICES_STAMP
    touch(o);
#endif
    dirty_=true;++changes_;return 1;
}
size_t World::reviewPlayer(const std::string& player){
    std::lock_guard<std::mutex> g(lock_);
    if(!playerById(player))bad("no player "+player+" on this server (coop-world players lists them)");
    const Player* p=playerById(player);size_t n=0;
    for(auto& o:objects_){
        Object& ob=o.second;if(ob.creator!=player)continue;
        if(ob.share=="held"&&!ob.heldAsk){
            std::string why=blocked(*p,false);
#ifdef MUTANT_POLICY_HELD_RELEASE
            why.clear();
#endif
            if(!why.empty()){if(ob.held!=why){ob.held=why;touch(ob);}continue;}}
        else if(ob.share!="review")continue;
        ob.share="";ob.held.clear();ob.heldAsk=false;ob.kept.clear();
#ifndef MUTANT_NOTICES_STAMP
        touch(ob);
#endif
        ++n;}
    if(n){dirty_=true;++changes_;}
    return n;
}
World::Object& World::ownObject(const std::string& player,const std::string& object){
    auto found=objects_.find(object);
    if(found==objects_.end())bad("no object "+object);
    Object& o=found->second;
#ifndef MUTANT_POLICY_PLAYER_ANY
    if(o.creator!=player)bad(object+" is not "+player+"'s");
#endif
    if(!o.originServer.empty())bad(object+" came from another server: only that server changes it");
    if(o.remnant)bad(object+" is withdrawn already");
    return o;
}
size_t World::offer(const std::string& player,const std::string& object,bool yes){
    std::lock_guard<std::mutex> g(lock_);
    const Player* p=playerById(player);
    if(!p)bad("no player "+player+" on this server (coop-world players lists them)");
    std::vector<Object*> list;
    if(!object.empty()){Object& o=ownObject(player,object);if(o.share!="ask"&&o.share!="held")bad(object+" does not wait for its builder's answer and is not held");list.push_back(&o);}
    else for(auto& o:objects_)if(o.second.creator==player&&(o.second.share=="ask"||o.second.share=="held"))list.push_back(&o.second);
    size_t n=0;
    for(auto* o:list){
        if(yes){
            // Shared only as far as the policy allows: what it does not stays held, with the reason of now.
            std::string why=blocked(*p,o->share=="ask");
#ifdef MUTANT_POLICY_HELD_RELEASE
            why.clear();
#endif
            if(!why.empty()){
                if(!object.empty())bad(object+" stays held: "+heldText(why));
                if(o->share=="held"&&o->held!=why){o->held=why;touch(*o);}
                continue;}}
        o->share=yes?shareAfterOffer():"kept";o->held.clear();o->heldAsk=false;o->kept=o->share!="kept"?"":yes?"server":"choice";
#ifndef MUTANT_NOTICES_STAMP
        touch(*o);
#endif
        ++n;}
    if(n){dirty_=true;++changes_;}
    return n;
}
World::Withdrawn World::withdraw(const std::string& player,const std::string& object){
    std::lock_guard<std::mutex> g(lock_);
    const Player* p=playerById(player);
    if(!p)bad("no player "+player+" on this server (coop-world players lists them)");
    std::vector<std::string> ids;
    if(!object.empty())ids.push_back(ownObject(player,object).id);
    else for(auto& o:objects_)if(o.second.creator==player&&o.second.originServer.empty()&&!o.second.remnant)ids.push_back(o.first);
    Withdrawn r;
    for(auto& id:ids){
        Object& o=objects_[id];
        // What was never handed to anyone just goes; what was shared stays behind under "stay".
#ifndef MUTANT_POLICY_WITHDRAW_STAY_REMOVES
        bool stay=policy_.withdrawal=="stay"&&o.share.empty();
#else
        bool stay=false;
#endif
#ifdef MUTANT_POLICY_WITHDRAW_GO_KEEPS
        stay=o.share.empty();
#endif
        if(stay){o.remnant=true;if(o.name.empty())o.name=p->name;touch(o);++r.remnants;}
        else{dropped(id);objects_.erase(id);++r.removed;}}
    if(!ids.empty()){dirty_=true;++changes_;note("withdrawn for "+player+": "+std::to_string(r.remnants)+" stay as remnants, "+std::to_string(r.removed)+" removed");}
    // Taking back everything takes back what he put into pavers too, under either setting: nobody else's road loses
    // what it has (the game never takes materials back), the others only get no more of his. A remnant of it would be
    // counted as someone else's in his own game, which has it already.
#ifndef MUTANT_HIGHWAY_WITHDRAW
    if(object.empty()&&devoted_.erase(player)+highwayLikes_.erase(player)){dirty_=true;++changes_;note("paver materials of "+player+" taken back");}
#endif
    return r;
}
size_t World::removePlayer(const std::string& id){
    std::lock_guard<std::mutex> g(lock_);
    std::set<std::string> gone;std::set<std::string> fromServers;
    for(auto& o:objects_)if(o.second.creator==id){gone.insert(o.first);if(!o.second.originServer.empty())fromServers.insert(o.second.originServer);}
#ifdef MUTANT_POLICY_REMOVE_PLAYER_OTHERS
    for(auto& o:objects_)if(fromServers.count(o.second.originServer))gone.insert(o.first);
#endif
    if(gone.empty()&&!playerById(id))bad("no player or builder "+id+" here (coop-world players lists them)");
    for(auto& k:gone){auto& o=objects_[k];if(!o.originServer.empty())byOrigin_.erase({o.originServer,o.originId});dropped(k);objects_.erase(k);}
    likes_.erase(std::remove_if(likes_.begin(),likes_.end(),[&](const Like& l){return gone.count(l.object)>0;}),likes_.end());
    if(!gone.empty()){dirty_=true;++changes_;note("removed "+std::to_string(gone.size())+" objects of "+id);}
    // And what he put into pavers, with its likes.
#ifndef MUTANT_HIGHWAY_REMOVE
    if(devoted_.erase(id)+highwayLikes_.erase(id)){dirty_=true;++changes_;note("removed the paver materials of "+id);}
#endif
    return gone.size();
}
}
