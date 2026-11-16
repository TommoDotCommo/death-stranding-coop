#include "link.h"
namespace world {
namespace {
bool keyChar(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}
}
bool linkKeyText(const std::string& k){
    if(k.size()<8||k.size()>64)return false;
    for(char c:k)if(!keyChar(c))return false;
    return true;}
bool sameKey(const std::string& a,const std::string& b){
    unsigned char diff=0;
    for(size_t i=0;i<64;++i)diff|=static_cast<unsigned char>((i<a.size()?a[i]:0)^(i<b.size()?b[i]:0));
    return diff==0&&a.size()==b.size()&&a.size()<=64;}
bool parseLinkUrl(const std::string& url,LinkEndpoint& out){
    const std::string scheme="http://";
    if(url.compare(0,scheme.size(),scheme)!=0)return false;
    size_t at=scheme.size(),colon=url.find(':',at);if(colon==std::string::npos)return false;
    LinkEndpoint e;e.host=url.substr(at,colon-at);
    unsigned parts=0,digits=0;int value=0;
    for(size_t i=0;i<=e.host.size();++i){
        char c=i<e.host.size()?e.host[i]:'.';
        if(c>='0'&&c<='9'){value=value*10+(c-'0');if(++digits>3||value>255)return false;}
        else if(c=='.'){if(!digits)return false;++parts;digits=0;value=0;}
        else return false;}
    if(parts!=4)return false;
    size_t slash=url.find('/',colon);std::string port=url.substr(colon+1,slash==std::string::npos?std::string::npos:slash-colon-1);
    if(port.empty()||port.size()>5)return false;
    for(char c:port){if(c<'0'||c>'9')return false;e.port=e.port*10+(c-'0');}
    if(e.port<1||e.port>65535)return false;
    if(slash!=std::string::npos){
        e.prefix=url.substr(slash+1);if(e.prefix.empty())return false;
        for(char c:e.prefix)if(!keyChar(c))return false;}
    out=e;return true;
}
Puller::Result Puller::poll(int64_t now){
    // Now and then everything is taken again whether or not the other side says it changed: an object can grow too
    // old for a filter without anything happening to it.
    bool full=version_.empty()||!haveFull_||now-lastFull_>=LinkFullEverySeconds;
    const std::string mine=world_.serverId();
    std::vector<Shared> all;std::string after,version,peer;
    auto refused=[&](const std::string& why){why_=why;return Result::Refused;};
    for(size_t page=0;;++page){
        if(page>=LinkMaxPages)return Result::TooMany;
        json::Value ask=json::Value::object();ask.set("format",1);ask.set("server",mine);ask.set("have",page==0&&!full?version_:std::string());ask.set("after",after);
        std::string answer;
        if(!fetch_(ask.dump(),answer))return Result::Unreachable;
        json::Value in;
        try{in=json::parse(answer);}catch(const std::exception& e){return refused(e.what());}
        if(!in.isObject()||!in.get("format").isInt()||in.get("format").integer()!=1||in.get("kind").string()!="coop-world-objects")return refused("not a page of coop-world objects");
        const std::string server=in.get("server").string(),v=in.get("version").string();
        if(!serverIdText(server)||v.empty()||v.size()>64)return refused("bad server id or version");
        if(server==mine)return refused("the other server has this server's id (a copy of the same world file?)");
        if(page==0){
            peer=server;version=v;
            if(in.has("same")){
                if(full||in.get("same").kind()!=json::Value::Bool||!in.get("same").boolean()||v!=version_||server!=peer_)return refused("an unasked 'same'");
                return Result::Same;}
        }
#ifndef MUTANT_LINK_VERSION
        else if(server!=peer||v!=version)return Result::Changed;   // its world moved between two pages: start again
#endif
        if(!in.get("objects").isArray()||in.get("objects").items().size()>LinkPageObjects||in.get("more").kind()!=json::Value::Bool||!in.get("next").isString())return refused("bad page");
        try{
            for(auto& o:in.get("objects").items()){
                Shared s=readShared(o);
                // What the import filter drops is not kept while the pages come in; the import checks the rest again.
                if(s.server!=mine&&!passes(config_.importFilter,s,s.server!=peer,now))continue;
                all.push_back(std::move(s));}
        }catch(const std::exception& e){return refused(e.what());}
        if(all.size()>MaxImportedPerServer)return Result::TooMany;
        if(!in.get("more").boolean())break;
        const std::string next=in.get("next").string();
        if(!objectIdText(next)||(!after.empty()&&!(after<next)))return refused("pages out of order");
        after=next;
    }
    try{last_=world_.importShared(peer,all,config_.importFilter,true,now);}
    catch(const std::exception& e){return refused(e.what());}
    peer_=peer;version_=version;lastFull_=now;haveFull_=true;
    return Result::Synced;
}
}
