// The launcher's Challenges tab without its window (challenges.h, ../docs/ROUTES.md "The launcher"): recordings and
// results read from the routes folder, a route's rules edited, a pasted string read, the challenge for the next game
// in launcher.ini and in the game's config, and the server's list, publish and submit over a stand-in transport.
// MUTANT_CHALLENGES_* and MUTANT_CONFIG_ROUTE_DROPPED must fail here.
#include "challenges.h"
#include "../world/json.h"
#include <cstdio>
#include <fstream>
using namespace launcher;
namespace rt=mod::route;
namespace fs=std::filesystem;
static int failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"challenges oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static void write(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<s;}
static rt::Route ridge(double to=300){rt::Route r;r.name="Ridge";r.author="Ann";r.points={{0,0,0},{to/2,30,0},{to,0,0}};return r;}

static void local(const fs::path& data){
    const std::string a=rt::encode(ridge()),b=rt::encode(ridge(500));
    write(challenges::folder(data)/"recorded-20261118-101500.txt",a+"\n");
    write(challenges::folder(data)/"recorded-20261118-111500.txt",b+"\n");
    write(challenges::folder(data)/"recorded-20261118-121500.txt","not a route\n");
    write(challenges::folder(data)/"other.txt",a);
    auto rec=challenges::recordings(data);
    CHECK(rec.size()==2&&rec[0].text==b&&rec[1].text==a&&rec[0].route.points.size()==3);
    // Results: a finished and a failed run; a damaged file is left out.
    write(challenges::folder(data)/"result-20261118-101600-0000.json","{\"route\":\""+a+"\",\"outcome\":\"finished\",\"ms\":754500,\"reason\":\"\",\"cases_verified\":true,\"track\":\"DST1-X\"}\n");
    write(challenges::folder(data)/"result-20261118-101700-0000.json","{\"route\":\""+a+"\",\"outcome\":\"failed\",\"ms\":1000,\"reason\":\"left the corridor\",\"cases_verified\":false,\"track\":\"\"}\n");
    write(challenges::folder(data)/"result-20261118-101800-0000.json","{\"route\":\"nope\"}");
    auto res=challenges::results(data);
    CHECK(res.size()==2&&res[0].outcome=="failed"&&res[1].outcome=="finished"&&res[1].ms==754500&&res[1].track=="DST1-X");
    CHECK(challenges::describe(res[1])=="Ridge: finished in 12:34.5"&&challenges::describe(res[0])=="Ridge: failed (left the corridor), cases unverified");
    // Reading a pasted string: what it holds, or why not; lower case and spaces read the same and are written canonical.
    CHECK(challenges::describe(a)=="Ridge by Ann: 10 m wide, 5 s grace; 300 m, 3 points");
    CHECK(challenges::describe("DSG-ABCDE").find("not a challenge")!=std::string::npos);
    std::string lower;for(char c:a)lower+=char(c>='A'&&c<='Z'?c+32:c);
    CHECK(challenges::canonical("  "+lower+" ")==a);
    bool threw=false;try{challenges::canonical(a.substr(0,a.size()-3));}catch(const std::invalid_argument&){threw=true;}CHECK(threw);
    // Editing the rules: a new string with the same points.
    auto e=challenges::editOf(a);CHECK(e.name=="Ridge"&&e.rules.width==10);
    e.name="Ridge, 5 m";e.rules.width=5;e.rules.noVehicles=true;e.rules.minCases=3;e.rules.timeLimit=1200;e.rules.grace=2;
    auto changed=challenges::edited(a,e);auto back=rt::decode(changed);
    CHECK(back&&back->name=="Ridge, 5 m"&&back->rules==e.rules&&back->points.size()==3&&rt::decode(a)->points.size()==3);
    if(back){auto was=rt::decode(a);for(size_t i=0;i<3&&back->points.size()==3;++i)CHECK(back->points[i].x==was->points[i].x&&back->points[i].z==was->points[i].z);}
    CHECK(challenges::describe(changed)=="Ridge, 5 m by Ann: 5 m wide, 2 s grace, no vehicles, carry at least 3 cases, within 20:00; 300 m, 3 points");
    for(auto bad:{1u,101u}){auto x=e;x.rules.width=bad;threw=false;try{challenges::edited(a,x);}catch(const std::invalid_argument&){threw=true;}CHECK(threw);}
    {auto x=e;x.name="";threw=false;try{challenges::edited(a,x);}catch(const std::invalid_argument&){threw=true;}CHECK(threw);}
    // The challenge for the next game: in launcher.ini, canonical; into the game's config as `route`.
    std::string ini="[launcher]\nname=Ann\n";
    ini=challenges::choose(ini,lower);CHECK(challenges::chosen(ini)==a&&strand::iniGet(ini,"launcher","name")=="Ann");
    threw=false;try{challenges::choose(ini,"DSR1-AAAAA");}catch(const std::invalid_argument&){threw=true;}CHECK(threw&&challenges::chosen(ini)==a);
    CHECK(challenges::chosen(challenges::choose(ini,""))=="");
    Session s;s.route=challenges::chosen(ini);
    auto cfg=config(s);CHECK(cfg.find("\"route\":\""+a+"\"")!=std::string::npos&&mod::parseConfig(cfg).route==a);
    Session g=s;g.group=Group::Join;g.server="10.0.0.2";g.playerId=77;g.port=49000;
    cfg=config(g);CHECK(mod::parseConfig(cfg).route==a);
    s.route="";CHECK(config(s).find("route")==std::string::npos);
}
static void server(){
    const std::string a=rt::encode(ridge());
    std::vector<std::pair<std::string,std::string>> sent;   // path, body
    std::string list="{\"format\":1,\"challenges\":[{\"id\":\"c4\",\"name\":\"Ridge\",\"author\":\"Ann\",\"author_key\":\"a1b2 c3d4 e5f6 a7b8\",\"route\":\""+a+
                     "\",\"rules_text\":\"10 m wide, 5 s grace\",\"length_m\":300,\"runs\":2,\"board\":[{\"place\":1,\"name\":\"Bob\",\"key\":\"\",\"ms\":40000,\"time\":\"0:40.0\",\"when\":1}]}]}";
    strand::Transport io=[&](const std::string& method,const std::string& path,const strand::Headers&,const std::string& body,strand::Http& out){
        sent.push_back({method+" "+path,body});
        if(path=="/w9/coop/challenges"){out.status=200;out.body=list;return true;}
        if(path=="/w9/coop/challenge-publish"){out.status=200;out.body="{\"id\":\"c5\"}";return true;}
        if(path=="/w9/coop/challenge-result"){out.status=200;out.body="{\"place\":2}";return true;}
        return false;};
    strand::Target t;t.address="127.0.0.1";t.port=49100;t.prefix="w9";t.account="ann";t.name="Ann";
    std::vector<challenges::Listed> got;std::string why;
    CHECK(challenges::fetchList(t,io,got,why)&&got.size()==1&&got[0].id=="c4"&&got[0].board.size()==1);
    if(got.size()==1)CHECK(got[0].text()=="Ridge by Ann (a1b2 c3d4 e5f6 a7b8): 10 m wide, 5 s grace; 300 m, 2 runs; best 0:40.0 by Bob");
    std::string id;std::string lower;for(char c:a)lower+=char(c>='A'&&c<='Z'?c+32:c);
    CHECK(challenges::publish(t,io,lower,id,why)&&id=="c5");
    auto body=json::parse(sent.back().second);CHECK(body.get("route").string()==a&&body.get("player").string()=="ann"&&body.get("name").string()=="Ann");
    CHECK(!challenges::publish(t,io,"DSR1-AAAAA",id,why)&&why.find("incomplete")!=std::string::npos);
    challenges::Result done;done.route=a;done.outcome="finished";done.ms=40000;done.track="DST1-ABC";
    CHECK(challenges::challengeOf(got,done)=="c4");
    challenges::Result other=done;other.route=rt::encode(ridge(280));CHECK(challenges::challengeOf(got,other)=="");
    int64_t place=0;size_t before=sent.size();
    CHECK(challenges::submit(t,io,"c4",done,place,why)&&place==2);
    body=json::parse(sent.back().second);CHECK(body.get("track").string()=="DST1-ABC"&&body.get("challenge").string()=="c4");
    // A failed run is never sent.
    challenges::Result failed=done;failed.outcome="failed";before=sent.size();
    CHECK(!challenges::submit(t,io,"c4",failed,place,why)&&sent.size()==before);
    // An unknown answer and no answer.
    strand::Transport none=[](const std::string&,const std::string&,const strand::Headers&,const std::string&,strand::Http&){return false;};
    CHECK(!challenges::fetchList(t,none,got,why)&&why=="The server did not answer.");
    CHECK(challenges::parseList(list).size()==1);
    bool threw=false;try{challenges::parseList("{\"format\":1,\"challenges\":[{\"id\":\"c1\",\"route\":\"x\"}]}");}catch(const std::exception&){threw=true;}CHECK(threw);
}
int main(){
    auto data=fs::temp_directory_path()/("ds-challenges-test-"+std::to_string(std::rand()));
    fs::remove_all(data);
    local(data);server();
    fs::remove_all(data);
    if(failures){std::fprintf(stderr,"launcher challenges: %d checks failed\n",failures);return 1;}
    std::puts("launcher challenges: recordings, results, editing, the chosen challenge in the config, the server's list, publish and submit");
    return 0;
}
