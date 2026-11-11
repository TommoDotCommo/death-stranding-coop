#include "front.h"
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
bool keyText(const std::string& k){
    if(k.empty()||k.size()>MaxKeyBytes)return false;
    for(char c:k)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;}
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
    std::string key;
    if(path.compare(0,3,"/k/")==0){
        size_t end=path.find('/',3);if(end==std::string::npos)return notFound();
        key=path.substr(3,end-3);path.erase(0,end);
        if(!keyText(key))return notFound();}
    if(path=="/auth/ds"){
        if(r.method!="GET")return notFound();
        if(key.empty())key=steamIdOf(query(q,"code"));
        if(key.empty())return Reply{400,"{\"status\":\"400\",\"code\":400,\"message\":\"no account in the sign-in\"}"};
        auto host=r.headers.find("host");
        if(host==r.headers.end()||host->second.empty())return Reply{400,"{\"status\":\"400\",\"code\":400,\"message\":\"no host\"}"};
        return world_.signIn(key,query(q,"display_name"),"http://"+host->second+base+"/gate");}
    if(path.compare(0,6,"/gate/")==0&&key.empty()){
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
