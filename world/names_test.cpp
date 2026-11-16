// Builder names (world.h, README "Builder names"): a world's display name, the label `lookup` gives a builder ("Name ·
// Home" for one from a linked server, the name only for this server's own players), cut to what the game's label
// holds by cutting the name and never the server; the home name carried by exports, read from documents from before
// it, passed on unchanged, replaced when its server renames itself, and saved with the world.
#include "world.h"
#include <cstdio>
using namespace world;
unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"names oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static unsigned counter=0;
static const std::string Dot=" \xC2\xB7 ";   // " · "
struct Server {
    World w;
    explicit Server(const std::string& id):w([]{return "tok"+std::to_string(++counter);}){w.setServerId(id);}
    std::string signIn(const std::string& key,const std::string& name){return json::parse(w.signIn(key,name,"g").body).get("session").get("token").string();}
    std::string make(const std::string& token,int64_t now){
        return json::parse(w.call(token,"createObject","{\"t\":\"l\",\"q\":226,\"px\":1,\"py\":2,\"pz\":3}",now).body).get("id").string();}
    // What a game is told for this builder id ("" when the server knows nobody by it).
    std::string lookup(const std::string& token,const std::string& id){
        auto users=json::parse(w.call(token,"lookup","{\"ids\":[\""+id+"\"]}",1).body).get("users").items();
        return users.size()==1?users[0].get("displayName").string():std::string();}
    json::Value out(const char* filter="all",int64_t now=500){return w.exportObjects(parseFilter(filter),"",now);}
    void in(const json::Value& d,const char* filter="all",int64_t now=500){w.importObjects(d,parseFilter(filter),false,now);}
};
static size_t chars(const std::string& s){size_t n=0;for(unsigned char c:s)if((c>>6)!=2)++n;return n;}
static bool wholeUtf8(const std::string& s){   // no character cut in half
    for(size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
        if(!n||i+n>s.size())return false;
        for(size_t k=1;k<n;++k)if((static_cast<unsigned char>(s[i+k])>>6)!=2)return false;
        i+=n;}
    return true;}
static bool endsWith(const std::string& s,const std::string& tail){return s.size()>=tail.size()&&s.compare(s.size()-tail.size(),tail.size(),tail)==0;}
static bool fits(const std::string& label){return label.size()<=MaxLabelBytes&&chars(label)<=MaxLabelChars&&wholeUtf8(label);}

static void text(){
    for(const char* good:{"North","abcdefghijkl","\xC3\x86r\xC3\xB8 Nord","s\xC3\xBC\x64","\xF0\x9F\x8C\x8D Earth"})CHECK(serverNameText(good));
    for(const char* wrong:{""," North","North ","a\nb","abcdefghijklm","\xC3\x86\xC3\x86\xC3\x86\xC3\x86\xC3\x86\xC3\x86\xC3\x86\xC3\x86\xC3\x86","\xC3","\xFF\x41","a\x7f"})CHECK(!serverNameText(wrong));
    CHECK(cutText("Porter",3,32)=="Por"&&cutText("Porter",10,4)=="Port"&&cutText("ab cd",3,32)=="ab"&&cutText("",5,5).empty());
    CHECK(cutText("\xC3\x86\xC3\x86\xC3\x86",5,5)=="\xC3\x86\xC3\x86");   // never half a character
    // The label: name and home, the name only without a home.
    CHECK(builderLabel("Ann","North")=="Ann"+Dot+"North");CHECK(builderLabel("Ann","")=="Ann");
    // A long name is cut, the server's name is not: 24 characters, 32 bytes at most.
    std::string longName="Bartholomew-the-Porter-of-Lake-Knot";
    auto label=builderLabel(longName,"North");
    CHECK(fits(label)&&endsWith(label,Dot+"North")&&chars(label)==MaxLabelChars&&longName.compare(0,label.size()-Dot.size()-5,label,0,label.size()-Dot.size()-5)==0);
    // In letters of two and four bytes: the byte limit, whole characters, and the home in full.
    std::string wide;for(int i=0;i<20;++i)wide+="\xC3\x98";   // Ø
    label=builderLabel(wide,"\xC3\x86r\xC3\xB8sk\xC3\xB8""bing");
    CHECK(fits(label)&&endsWith(label,Dot+"\xC3\x86r\xC3\xB8sk\xC3\xB8""bing")&&label.size()>Dot.size()+13);
    std::string stars;for(int i=0;i<12;++i)stars+="\xF0\x9F\x8C\x9F";
    label=builderLabel(stars,"North");CHECK(fits(label)&&endsWith(label,Dot+"North")&&label.compare(0,4,"\xF0\x9F\x8C\x9F")==0);
    // A home that is too long (a link name of 32 characters) is cut to its own limit, and the name still shows.
    label=builderLabel("Ann","the-northern-group-server-of-ann");CHECK(fits(label)&&label=="Ann"+Dot+"the-northern");
    label=builderLabel(longName,"the-northern-group-server-of-ann");CHECK(fits(label)&&endsWith(label,Dot+"the-northern")&&label.compare(0,5,"Barth")==0);
    CHECK(builderLabel("   ","North")=="Porter"+Dot+"North");
}
static void lookups(){
    Server north("aaaa1111bbbb2222"),south("serverB1");north.w.setName("North");
    CHECK(north.w.name()=="North");
    {bool threw=false;try{north.w.setName(" bad");}catch(const std::exception&){threw=true;}CHECK(threw&&north.w.name()=="North");}
    auto ann=north.signIn("steam-a","Ann"),bea=south.signIn("steam-b","Bea");north.make(ann,100);
    // This server's own players: the name only, with or without a server name.
    CHECK(north.lookup(ann,playerId("steam-a"))=="Ann");CHECK(south.lookup(bea,playerId("steam-b"))=="Bea");
    // The export carries the server's name; a server without one exports the form from before (seven fields).
    auto doc=north.out();CHECK(doc.get("objects").items().size()==1&&doc.get("objects").items()[0].get("home").string()=="North");
    south.make(bea,100);auto plain=south.out();CHECK(plain.get("objects").items()[0].members().size()==7&&!plain.get("objects").items()[0].has("home"));
    // Imported: "Name · Home"; the home is the first server's name.
    south.in(doc);CHECK(south.lookup(bea,playerId("steam-a"))=="Ann"+Dot+"North");
    CHECK(south.lookup(bea,playerId("steam-b"))=="Bea");
    // A document from before display names: the link's name for that server when there is one, else its id's first characters.
    Server east("cccc3333dddd4444");auto cid=east.signIn("steam-c","Cid");
    json::Value old=doc;{auto list=json::Value::array();for(auto o:doc.get("objects").items()){o.erase("home");list.push(o);}old.set("objects",list);}
    east.in(old);CHECK(east.lookup(cid,playerId("steam-a"))=="Ann"+Dot+"aaaa1111");CHECK(east.w.homeName("aaaa1111bbbb2222")=="aaaa1111");
    east.w.setLinkName("aaaa1111bbbb2222","northlink");CHECK(east.lookup(cid,playerId("steam-a"))=="Ann"+Dot+"northlink");
    // The name the server gives itself wins over the link's.
    east.in(doc,"all",600);CHECK(east.lookup(cid,playerId("steam-a"))=="Ann"+Dot+"North");
    // Passed on with imported=yes: still the first server's name, not the server it came by.
    Server west("eeee5555ffff6666");west.w.setLinkName("serverB1","south");auto dan=west.signIn("steam-d","Dan");
    west.in(south.out("imported=yes"),"imported=yes");
    CHECK(west.lookup(dan,playerId("steam-a"))=="Ann"+Dot+"North");CHECK(west.lookup(dan,playerId("steam-b"))=="Bea"+Dot+"south");
    auto onward=west.out("imported=yes");CHECK(onward.get("objects").items().size()==2);
    for(auto& o:onward.get("objects").items())CHECK(o.get("home").string()==(o.get("server").string()=="aaaa1111bbbb2222"?"North":""));
    // A server that renames itself: the next import changes the name, and nothing else.
    north.w.setName("Nord");auto r=south.w.importObjects(north.out(),parseFilter("all"),false,700);
    CHECK(r.updated==1&&r.added==0&&south.lookup(bea,playerId("steam-a"))=="Ann"+Dot+"Nord");
    // A long name from over there: cut so the home still shows.
    auto zed=north.signIn("steam-z","Zebediah-of-the-Eastern-Crater-Lake");north.make(zed,800);south.in(north.out(),"all",800);
    auto label=south.lookup(bea,playerId("steam-z"));CHECK(fits(label)&&endsWith(label,Dot+"Nord")&&label.compare(0,8,"Zebediah")==0);
    // The owner's list names an imported builder as the game does.
    bool listed=false;for(auto& i:south.w.playerList())if(i.id==playerId("steam-a"))listed=!i.known&&i.name=="Ann"+Dot+"Nord";CHECK(listed);
    // Saved with the world: the server's name and every import's home.
    World back;back.restore(south.w.snapshot());auto tok=json::parse(back.signIn("steam-b","Bea","g").body).get("session").get("token").string();
    CHECK(json::parse(back.call(tok,"lookup","{\"ids\":[\""+playerId("steam-a")+"\"]}",1).body).get("users").items()[0].get("displayName").string()=="Ann"+Dot+"Nord");
    World named;named.restore(north.w.snapshot());CHECK(named.name()=="Nord");
    for(auto change:{std::make_pair(std::string("\"name\":\"Nord\""),std::string("\"name\":\" Nord\"")),std::make_pair(std::string("\"name\":\"Nord\""),std::string("\"name\":\"\""))}){
        auto text=north.w.snapshot().dump();auto at=text.find(change.first);CHECK(at!=std::string::npos);text.replace(at,change.first.size(),change.second);
        bool threw=false;try{World x;x.restore(json::parse(text));}catch(const std::exception&){threw=true;}CHECK(threw);}
    {auto text=south.w.snapshot().dump();auto at=text.find("\"home\":\"Nord\"");CHECK(at!=std::string::npos);text.replace(at,13,"\"home\":\"a\\u0001\"");
     bool threw=false;try{World x;x.restore(json::parse(text));}catch(const std::exception&){threw=true;}CHECK(threw);}
}
static void documents(){
    // Records with a home that is none are refused whole; eight fields are only eight with a home.
    Server a("serverA1"),b("serverB1");a.w.setName("North");auto ann=a.signIn("steam-a","Ann");a.make(ann,100);
    auto doc=a.out();
    auto with=[&](const json::Value& home){json::Value d=doc;auto list=json::Value::array();for(auto o:doc.get("objects").items()){o.set("home",home);list.push(o);}d.set("objects",list);return d;};
    for(auto wrong:{json::Value(""),json::Value(7),json::Value("a\nb"),json::Value(std::string(MaxNameBytes+1,'n')),json::Value("\xC3")}){
        auto before=b.w.snapshot().dump();bool threw=false;try{b.in(with(wrong));}catch(const std::exception&){threw=true;}
        CHECK(threw&&b.w.snapshot().dump()==before);}
    {json::Value d=doc;auto list=json::Value::array();for(auto o:doc.get("objects").items()){o.erase("home");o.set("homes","North");list.push(o);}d.set("objects",list);
     bool threw=false;try{b.in(d);}catch(const std::exception&){threw=true;}CHECK(threw);}
    // A home longer than the label takes is kept as given and cut where it is shown.
    b.in(with(json::Value("the-northern-group-server-of-ann")));auto bea=b.signIn("steam-b","Bea");
    CHECK(b.lookup(bea,playerId("steam-a"))=="Ann"+Dot+"the-northern");
}
int main(){
    for(auto part:{text,lookups,documents}){
        try{part();}catch(const std::exception& e){std::fprintf(stderr,"names oracle: threw %s\n",e.what());++failures;}}
    if(failures)return 1;
    std::puts("names tests passed");return 0;
}
