#include "front.h"
#include "api.h"
#include <cctype>
namespace world {
namespace {
Reply notFound(){return Reply{404,"{\"status\":\"404\",\"code\":404,\"message\":\"not found\"}"};}
int hexValue(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
std::string query(const std::string& q,const std::string& name){
    size_t at=0;
    while(at<q.size()){
        size_t end=q.find('&',at);if(end==std::string::npos)end=q.size();
        size_t eq=q.find('=',at);
        if(eq!=std::string::npos&&eq<end&&q.compare(at,eq-at,name)==0){
            std::string out;
            for(size_t i=eq+1;i<end;++i){
                if(q[i]=='%'&&i+2<end&&hexValue(q[i+1])>=0&&hexValue(q[i+2])>=0){out+=char(hexValue(q[i+1])*16+hexValue(q[i+2]));i+=2;}
                else out+=q[i]=='+'?' ':q[i];}
            return out;}
        at=end+1;}
    return {};
}
}
std::string steamIdOf(const std::string& hex){
    // Ticket layout: a 4-byte length (20), an 8-byte token, then the 8-byte Steam id, little-endian.
    if(hex.size()<40)return {};
    uint64_t id=0;
    for(int i=0;i<8;++i){int hi=hexValue(hex[24+2*i]),lo=hexValue(hex[25+2*i]);if(hi<0||lo<0)return {};id|=uint64_t(hi*16+lo)<<(8*i);}
    if((id>>32)!=0x01100001)return {};   // an individual account in the public universe
    return std::to_string(id);
}
Reply Front::handle(const Request& r,int64_t now){
    std::string path=r.target,q;
    if(auto mark=path.find('?');mark!=std::string::npos){q=path.substr(mark+1);path.resize(mark);}
    std::string base;
#ifndef MUTANT_FRONT_PREFIX
    if(!prefix_.empty()){
        std::string want="/"+prefix_;
        if(path.compare(0,want.size(),want)!=0||(path.size()>want.size()&&path[want.size()]!='/'))return notFound();
        path.erase(0,want.size());base=want;}
#else
    if(!prefix_.empty()&&path.compare(0,prefix_.size()+1,"/"+prefix_)==0){path.erase(0,prefix_.size()+1);base="/"+prefix_;}
#endif
    if(path=="/link/pull"){
        // A linked server asking for objects. Its key says which link it is, and so which filter applies; without a
        // configured link, or with a key that is none of theirs, this path does not exist.
        if(r.method!="POST"||links_.empty())return notFound();
        std::string given;auto auth=r.headers.find("authorization");
        if(auth!=r.headers.end()&&auth->second.compare(0,5,"Link ")==0)given=auth->second.substr(5);
        const LinkConfig* link=nullptr;
        for(auto& l:links_)if(sameKey(l.key,given))link=&l;
#ifdef MUTANT_FRONT_LINK_KEY
        if(!link)link=&links_.front();
#endif
        if(!link){if(world_.log)world_.log("link request refused: no link has that key");return notFound();}
        return world_.linkPull(link->exportFilter,r.body,now);}
    // The mod's read of its player's own objects (world.h notices): its notice token is the only credential it has,
    // in a header (never in the address, which --trace writes), and the token reads and does nothing else.
    if(path=="/coop/notices"){
        if(r.method!="GET")return notFound();
        auto token=r.headers.find("x-coop-token");
        return world_.notices(token==r.headers.end()?std::string():token->second,query(q,"since"),now);}
    // The launcher's API (api.h): under the prefix, never under /k/NAME, never the game's.
    if(path.compare(0,6,"/coop/")==0){
        // A signed call is checked whole (identity.h) before the API sees it: the signer is the player it may act for.
        std::string signer,why;
        if(signatures_.check(r.headers,r.method,r.target,r.body,world_.serverId(),now,signer,why)==identity::Checker::Result::Bad){
            if(world_.log)world_.log("signed call refused: "+why);
            Reply e;e.status=401;json::Value v=json::Value::object();v.set("status","401");v.set("code",401);v.set("message",why);e.body=v.dump();return e;}
        const std::string call=path.substr(6);
        if(challenges&&(call=="challenges"||call.compare(0,10,"challenge-")==0))return challenges(r.method,call,r.body,signer,now);
        return launcherApi(world_,r.method,call,query(q,"player"),r.body,signer,now);}
    std::string key,ticket;
    if(path.compare(0,3,"/k/")==0){
        size_t end=path.find('/',3);if(end==std::string::npos)return notFound();
        key=path.substr(3,end-3);path.erase(0,end);
        if(!accountKeyText(key))return notFound();}
    else if(path.compare(0,3,"/t/")==0){
        // A sign-in ticket the player's launcher took for him (identity keys): it names the player, for sign-in only.
        size_t end=path.find('/',3);if(end==std::string::npos)return notFound();
        ticket=path.substr(3,end-3);path.erase(0,end);
        bool text=!ticket.empty()&&ticket.size()<=PassBytes;for(char c:ticket)text=text&&std::isalnum(static_cast<unsigned char>(c));
        if(!text)return notFound();}
    if(path=="/auth/ds"){
        if(r.method!="GET")return notFound();
        if(!ticket.empty()){
            auto host=r.headers.find("host");
            if(host==r.headers.end()||host->second.empty())return Reply{400,"{\"status\":\"400\",\"code\":400,\"message\":\"no host\"}"};
            return world_.signInTicket(ticket,query(q,"display_name"),"http://"+host->second+base+"/gate",now);}
        if(key.empty())key=steamIdOf(query(q,"code"));
        if(key.empty())return Reply{400,"{\"status\":\"400\",\"code\":400,\"message\":\"no account in the sign-in\"}"};
        auto host=r.headers.find("host");
        if(host==r.headers.end()||host->second.empty())return Reply{400,"{\"status\":\"400\",\"code\":400,\"message\":\"no host\"}"};
        return world_.signIn(key,query(q,"display_name"),"http://"+host->second+base+"/gate");}
    if(path.compare(0,6,"/gate/")==0&&key.empty()&&ticket.empty()){
        std::string name=path.substr(6);
        if(name.compare(0,2,"e/")==0)name.erase(0,2);
        if(name.empty()||name.size()>64||name.find('/')!=std::string::npos)return notFound();
        std::string token;auto auth=r.headers.find("authorization");
#ifndef MUTANT_FRONT_BEARER
        if(auth!=r.headers.end()&&auth->second.compare(0,7,"Bearer ")==0)token=auth->second.substr(7);
#else
        if(auth!=r.headers.end())token=auth->second.size()>7?auth->second.substr(7):"";
        if(token.empty()&&!q.empty())token=query(q,"token");
#endif
        return world_.call(token,name,r.body,now);}
    return notFound();
}
}
