#pragma once
// A small JSON value for the world server: integers stay 64-bit (the game sends times and counters beyond 2^53),
// objects keep their key order. Strict parser with depth and size caps; throws std::runtime_error on bad input.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace json {
class Value {
public:
    enum Kind {Null,Bool,Int,Real,String,Array,Object};
    Value()=default;
    Value(bool b):kind_(Bool),i_(b){}
    Value(int v):kind_(Int),i_(v){}
    Value(unsigned v):kind_(Int),i_(v){}
    Value(int64_t v):kind_(Int),i_(v){}
    Value(uint64_t v):kind_(Int),i_(int64_t(v)){}
    Value(double v):kind_(Real),d_(v){}
    Value(const char* s):kind_(String),s_(s){}
    Value(std::string s):kind_(String),s_(std::move(s)){}
    static Value array(){Value v;v.kind_=Array;return v;}
    static Value object(){Value v;v.kind_=Object;return v;}
    Kind kind() const {return kind_;}
    bool isNull() const {return kind_==Null;} bool isObject() const {return kind_==Object;} bool isArray() const {return kind_==Array;}
    bool isString() const {return kind_==String;} bool isInt() const {return kind_==Int;}
    bool boolean(bool d=false) const {return kind_==Bool?i_!=0:d;}
    int64_t integer(int64_t d=0) const {return kind_==Int?i_:kind_==Real?int64_t(d_):d;}
    const std::string& string() const {static const std::string empty;return kind_==String?s_:empty;}
    const std::vector<Value>& items() const {static const std::vector<Value> none;return kind_==Array?a_:none;}
    const std::vector<std::pair<std::string,Value>>& members() const {static const std::vector<std::pair<std::string,Value>> none;return kind_==Object?o_:none;}
    void push(Value v){if(kind_!=Array){*this=array();}a_.push_back(std::move(v));}
    // Object access: get returns a null value for a missing key; set replaces or appends.
    const Value& get(const std::string& k) const {static const Value none;for(auto& m:o_)if(m.first==k)return m.second;return none;}
    bool has(const std::string& k) const {for(auto& m:o_)if(m.first==k)return true;return false;}
    Value& set(const std::string& k,Value v){if(kind_!=Object)*this=object();for(auto& m:o_)if(m.first==k){m.second=std::move(v);return m.second;}o_.emplace_back(k,std::move(v));return o_.back().second;}
    void erase(const std::string& k){for(size_t i=0;i<o_.size();++i)if(o_[i].first==k){o_.erase(o_.begin()+long(i));return;}}
    std::string dump() const {std::string out;write(out);return out;}
private:
    Kind kind_=Null;int64_t i_=0;double d_=0;std::string s_;std::vector<Value> a_;std::vector<std::pair<std::string,Value>> o_;
    static void quote(std::string& out,const std::string& s){
        out+='"';
        for(unsigned char c:s){
            if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
            else if(c<0x20){char b[8];std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}
            else out+=char(c);}
        out+='"';}
    void write(std::string& out) const {
        switch(kind_){
        case Null:out+="null";break;
        case Bool:out+=i_?"true":"false";break;
        case Int:out+=std::to_string(i_);break;
        case Real:{char b[40];std::snprintf(b,sizeof b,"%.9g",d_);out+=b;break;}
        case String:quote(out,s_);break;
        case Array:{out+='[';bool first=true;for(auto& v:a_){if(!first)out+=',';first=false;v.write(out);}out+=']';break;}
        case Object:{out+='{';bool first=true;for(auto& m:o_){if(!first)out+=',';first=false;quote(out,m.first);out+=':';m.second.write(out);}out+='}';break;}
        }}
    friend class Parser;
};
class Parser {
public:
    static constexpr size_t MaxBytes=4*1024*1024;static constexpr unsigned MaxDepth=32;
    static Value parse(const std::string& text){
        if(text.size()>MaxBytes)throw std::runtime_error("json: too large");
        Parser p(text);p.ws();Value v=p.value(0);p.ws();if(p.at_!=text.size())throw std::runtime_error("json: trailing data");return v;}
private:
    explicit Parser(const std::string& t):t_(t){}
    const std::string& t_;size_t at_=0;
    [[noreturn]] void bad(const char* why){throw std::runtime_error(std::string("json: ")+why+" at "+std::to_string(at_));}
    void ws(){while(at_<t_.size()&&(t_[at_]==' '||t_[at_]=='\t'||t_[at_]=='\n'||t_[at_]=='\r'))++at_;}
    bool take(char c){if(at_<t_.size()&&t_[at_]==c){++at_;return true;}return false;}
    void word(const char* w){for(;*w;++w)if(!take(*w))bad("bad literal");}
    std::string str(){
        if(!take('"'))bad("string expected");
        std::string out;
        while(true){
            if(at_>=t_.size())bad("open string");
            unsigned char c=static_cast<unsigned char>(t_[at_++]);
            if(c=='"')return out;
            if(c<0x20)bad("control character in string");
            if(c!='\\'){out+=char(c);continue;}
            if(at_>=t_.size())bad("open escape");
            char e=t_[at_++];
            switch(e){
            case '"':case '\\':case '/':out+=e;break;
            case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{
                if(at_+4>t_.size())bad("short \\u");
                unsigned v=0;
                for(int i=0;i<4;++i){char h=t_[at_++];v<<=4;if(h>='0'&&h<='9')v|=unsigned(h-'0');else if(h>='a'&&h<='f')v|=unsigned(h-'a'+10);else if(h>='A'&&h<='F')v|=unsigned(h-'A'+10);else bad("bad \\u");}
                if(v<0x80)out+=char(v);else if(v<0x800){out+=char(0xc0|(v>>6));out+=char(0x80|(v&0x3f));}
                else{out+=char(0xe0|(v>>12));out+=char(0x80|((v>>6)&0x3f));out+=char(0x80|(v&0x3f));}   // surrogate halves pass through as they are
                break;}
            default:bad("bad escape");}
        }}
    Value number(){
        size_t b=at_;bool real=false;
        if(take('-')){}
        while(at_<t_.size()&&((t_[at_]>='0'&&t_[at_]<='9')||t_[at_]=='.'||t_[at_]=='e'||t_[at_]=='E'||t_[at_]=='+'||t_[at_]=='-')){if(t_[at_]=='.'||t_[at_]=='e'||t_[at_]=='E')real=true;++at_;}
        std::string n=t_.substr(b,at_-b);if(n.empty()||n=="-")bad("number expected");
        try{
            if(!real){size_t used=0;long long v=std::stoll(n,&used);if(used==n.size())return Value(int64_t(v));}
            size_t used=0;double d=std::stod(n,&used);if(used!=n.size())bad("bad number");return Value(d);
        }catch(const std::logic_error&){   // an integer beyond 64 bits: keep it as a real
            try{return Value(std::stod(n));}catch(const std::logic_error&){bad("bad number");}}
    }
    Value value(unsigned depth){
        if(depth>MaxDepth)bad("too deep");
        if(at_>=t_.size())bad("value expected");
        char c=t_[at_];
        if(c=='{'){++at_;Value o=Value::object();ws();if(take('}'))return o;
            while(true){ws();std::string k=str();ws();if(!take(':'))bad("colon expected");ws();o.o_.emplace_back(std::move(k),value(depth+1));ws();if(take('}'))return o;if(!take(','))bad("comma expected");}}
        if(c=='['){++at_;Value a=Value::array();ws();if(take(']'))return a;
            while(true){ws();a.a_.push_back(value(depth+1));ws();if(take(']'))return a;if(!take(','))bad("comma expected");}}
        if(c=='"')return Value(str());
        if(c=='t'){word("true");return Value(true);}
        if(c=='f'){word("false");return Value(false);}
        if(c=='n'){word("null");return Value();}
        return number();
    }
};
inline Value parse(const std::string& text){return Parser::parse(text);}
}
