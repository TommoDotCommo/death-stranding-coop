#include "collection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
namespace collection {
namespace {
const char* Alphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const std::pair<char,const char*> Kinds[]={{'p',"post"},{'c',"charger"},{'s',"safe house"},{'w',"watchtower"},{'b',"bridge"},{'n',"rain shelter"},
    {'r',"rope"},{'l',"ladder"},{'v',"bike"},{'e',"cairn"},{'o',"road"},{'x',"mushroom"},{'g',"chiral bridge"},{'j',"jump ramp"},{'t',"sign"},{'z',"zipline"},{'a',"catapult"}};
struct Fail { std::string why; };
// ---- Writing bytes ----
struct Out {
    std::vector<uint8_t> b;
    void byte(uint8_t v){b.push_back(v);}
    void var(uint64_t v){while(v>=0x80){b.push_back(uint8_t(v|0x80));v>>=7;}b.push_back(uint8_t(v));}
    void svar(int64_t v){var((uint64_t(v)<<1)^uint64_t(v>>63));}
    void text(const std::string& s){byte(uint8_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
};
// ---- Reading bytes: every read is checked; a fault throws Fail ----
struct In {
    const std::vector<uint8_t>& b;size_t at=0;
    explicit In(const std::vector<uint8_t>& bytes):b(bytes){}
    uint8_t byte(){if(at>=b.size())throw Fail{"the data ends too early"};return b[at++];}
    uint64_t var(){
        uint64_t v=0;
        for(int shift=0;shift<64;shift+=7){uint8_t c=byte();v|=uint64_t(c&0x7f)<<shift;if(!(c&0x80))return v;}
        throw Fail{"a number is too long"};}
    int64_t svar(){uint64_t u=var();return int64_t(u>>1)^-int64_t(u&1);}
    std::string text(size_t most,const char* what){
        size_t n=byte();if(n>most)throw Fail{std::string(what)+" is too long"};
        if(at+n>b.size())throw Fail{"the data ends too early"};
        std::string s(b.begin()+long(at),b.begin()+long(at+n));at+=n;return s;}
};
void need(bool ok,const std::string& why){if(!ok)throw Fail{why};}
bool within(int64_t v,int64_t most){return v>=-most&&v<=most;}
bool levelChars(const std::string& s){
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;
    return true;}
bool fieldName(const std::string& s){
    if(s.empty()||s.size()>MaxFieldName)return false;
    for(char c:s)if(c<'a'||c>'z')return false;
    return true;}
// The rules of one collection, checked before writing it (parse checks the same, field by field, as it reads).
void check(const Collection& c){
    need(!c.items.empty(),"a collection holds at least one structure");
    need(c.items.size()<=MaxItems,"a collection holds at most "+std::to_string(MaxItems)+" structures");
    need(c.area>=0&&c.area<=MaxArea,"the anchor's area is out of range");
    need(within(c.x,MaxAnchorCm)&&within(c.y,MaxAnchorCm)&&within(c.z,MaxAnchorCm),"the anchor is too far from the map's origin");
    need(!c.headingKnown||within(c.heading,MaxHeading),"the heading is out of range");
    need(c.name.empty()||nameText(c.name),"the name is 1 to 32 bytes of text, no control characters, no space at either end");
    size_t index=0;
    for(auto& i:c.items){
        const std::string at="structure "+std::to_string(++index)+": ";
        need(i.type>='a'&&i.type<='z',at+"the kind is one letter a to z");
#ifndef MUTANT_COLLECTION_RANGE
        need(within(i.dx,MaxOffsetCm)&&within(i.dy,MaxOffsetCm)&&within(i.dz,MaxOffsetCm),at+"more than 2 km from the anchor");
#endif
        need(within(i.rx,MaxAngle)&&within(i.ry,MaxAngle)&&within(i.rz,MaxAngle),at+"an angle is out of range");
        need(i.level.size()<=MaxLevelBytes&&levelChars(i.level),at+"the level is up to 32 letters, digits, '_', '.', '-'");
        need(!i.ownArea||(i.area>=0&&i.area<=MaxArea&&i.area!=c.area),at+"its own area is out of range or the anchor's");
        need(!i.cell||(within(i.cellX,MaxCell)&&within(i.cellY,MaxCell)&&i.map>=0&&i.map<=MaxCell),at+"the cell is out of range");
        if(i.materials)for(auto m:i.material)need(m>=0&&m<=MaxMaterial,at+"a material count is out of range");
        if(i.comment){
            need(!i.phrases.empty()||!i.text.empty(),at+"a comment has phrases or text");
            need(i.phrases.size()<=MaxPhrases,at+"a comment has at most 16 phrases");
            for(auto p:i.phrases)need(p>=0&&p<=65535,at+"a phrase number is out of range");
            need(i.text.size()<=MaxCommentBytes&&utf8(i.text,true),at+"a comment's text is up to 120 bytes of text without control characters");}
        else need(i.phrases.empty()&&i.text.empty(),at+"phrases or text without a comment");
        need(i.blocks.size()<=MaxBlocks,at+"too many blocks");
        int last=-1;
        for(auto& k:i.blocks){
#ifndef MUTANT_COLLECTION_ORDER
            need(int(k.id)>last&&k.id<MaxBlocks,at+"blocks are in their order, each once");
#endif
            last=k.id;
            need(!k.fields.empty()&&k.fields.size()<=MaxBlockFields,at+"a block has 1 to 8 fields");
            std::string prev;
            for(auto& f:k.fields){need(fieldName(f.first)&&(prev.empty()||f.first>prev),at+"block fields are named a-z and in order, each once");prev=f.first;
                need(within(f.second,MaxBlockValue),at+"a block value is out of range");}}}
}
uint32_t parts(const Item& i){
    return (i.rx||i.ry?Tilt:0)|(i.level.empty()?0:Level)|(i.ownArea?OwnArea:0)|(i.cell?Cell:0)|(i.p?PFlag:0)|(i.materials?Materials:0)|(i.comment?Comment:0)|(i.blocks.empty()?0:Blocks);}
std::vector<uint8_t> bytes(const Collection& c){
    check(c);Out o;
    o.byte(Version);o.byte(uint8_t((c.headingKnown?HeadingKnown:0)|(c.name.empty()?0:Named)));
    o.var(c.items.size());o.var(uint64_t(c.area));o.svar(c.x);o.svar(c.y);o.svar(c.z);
    if(c.headingKnown)o.svar(c.heading);
    if(!c.name.empty())o.text(c.name);
    for(auto& i:c.items){
        const uint32_t f=parts(i);
        o.byte(uint8_t(i.type));o.var(f);o.svar(i.dx);o.svar(i.dy);o.svar(i.dz);o.svar(i.rz);
        if(f&Tilt){o.svar(i.rx);o.svar(i.ry);}
        if(f&Level)o.text(i.level);
        if(f&OwnArea)o.var(uint64_t(i.area));
        if(f&Cell){o.svar(i.cellX);o.svar(i.cellY);o.var(uint64_t(i.map));}
        if(f&Materials)for(auto m:i.material)o.var(uint64_t(m));
        if(f&Comment){o.byte(uint8_t(i.phrases.size()));for(auto p:i.phrases)o.var(uint64_t(p));o.text(i.text);}
        if(f&Blocks){o.byte(uint8_t(i.blocks.size()));
            for(auto& k:i.blocks){o.byte(k.id);o.byte(uint8_t(k.fields.size()));for(auto& fl:k.fields){o.text(fl.first);o.svar(fl.second);}}}}
    uint32_t sum=crc32(o.b.data(),o.b.size());
    for(int k=0;k<4;++k)o.byte(uint8_t(sum>>(8*k)));
    return o.b;
}
Collection read(const std::vector<uint8_t>& b){
    need(b.size()>=5,"the data is too short");
    uint32_t sum=0;for(int k=0;k<4;++k)sum|=uint32_t(b[b.size()-4+size_t(k)])<<(8*k);
#ifndef MUTANT_COLLECTION_CRC
    need(crc32(b.data(),b.size()-4)==sum,"the check sum is wrong: the text was cut or changed");
#endif
    std::vector<uint8_t> body(b.begin(),b.end()-4);In in(body);Collection c;
    const uint8_t version=in.byte();
#ifndef MUTANT_COLLECTION_VERSION
    need(version==Version,"a collection of format "+std::to_string(version)+": this launcher reads format 1");
#else
    (void)version;
#endif
    const uint8_t flags=in.byte();need(!(flags&~(HeadingKnown|Named)),"unknown flags");
    const uint64_t count=in.var();
#ifndef MUTANT_COLLECTION_COUNT
    need(count>=1&&count<=MaxItems,"a collection holds 1 to "+std::to_string(MaxItems)+" structures");
#else
    need(count>=1&&count<=100000,"count");
#endif
    const uint64_t area=in.var();need(area<=uint64_t(MaxArea),"the anchor's area is out of range");c.area=int64_t(area);
    c.x=in.svar();c.y=in.svar();c.z=in.svar();
    need(within(c.x,MaxAnchorCm)&&within(c.y,MaxAnchorCm)&&within(c.z,MaxAnchorCm),"the anchor is too far from the map's origin");
    if(flags&HeadingKnown){c.headingKnown=true;c.heading=in.svar();need(within(c.heading,MaxHeading),"the heading is out of range");}
    if(flags&Named){c.name=in.text(MaxNameBytes,"the name");need(nameText(c.name),"the name is not text");}
    for(uint64_t n=0;n<count;++n){
        const std::string at="structure "+std::to_string(n+1)+": ";
        Item i;i.type=char(in.byte());need(i.type>='a'&&i.type<='z',at+"the kind is one letter a to z");
        const uint64_t f=in.var();need(!(f&~uint64_t(Tilt|Level|OwnArea|Cell|PFlag|Materials|Comment|Blocks)),at+"unknown parts");
        i.dx=in.svar();i.dy=in.svar();i.dz=in.svar();
#ifndef MUTANT_COLLECTION_RANGE
        need(within(i.dx,MaxOffsetCm)&&within(i.dy,MaxOffsetCm)&&within(i.dz,MaxOffsetCm),at+"more than 2 km from the anchor");
#endif
        i.rz=in.svar();
        if(f&Tilt){i.rx=in.svar();i.ry=in.svar();need(i.rx||i.ry,at+"a tilt of zero");}
        need(within(i.rx,MaxAngle)&&within(i.ry,MaxAngle)&&within(i.rz,MaxAngle),at+"an angle is out of range");
        if(f&Level){i.level=in.text(MaxLevelBytes,"a level");need(!i.level.empty()&&levelChars(i.level),at+"the level is letters, digits, '_', '.', '-'");}
        if(f&OwnArea){i.ownArea=true;const uint64_t a=in.var();need(a<=uint64_t(MaxArea)&&int64_t(a)!=c.area,at+"its own area is out of range or the anchor's");i.area=int64_t(a);}
        if(f&Cell){i.cell=true;i.cellX=in.svar();i.cellY=in.svar();const uint64_t m=in.var();
            need(within(i.cellX,MaxCell)&&within(i.cellY,MaxCell)&&m<=uint64_t(MaxCell),at+"the cell is out of range");i.map=int64_t(m);}
        i.p=(f&PFlag)!=0;
        if(f&Materials){i.materials=true;for(auto& m:i.material){const uint64_t v=in.var();need(v<=uint64_t(MaxMaterial),at+"a material count is out of range");m=int64_t(v);}}
        if(f&Comment){i.comment=true;const size_t phrases=in.byte();need(phrases<=MaxPhrases,at+"a comment has at most 16 phrases");
            for(size_t k=0;k<phrases;++k){const uint64_t v=in.var();need(v<=65535,at+"a phrase number is out of range");i.phrases.push_back(int64_t(v));}
            i.text=in.text(MaxCommentBytes,"a comment");need(utf8(i.text,true),at+"a comment's text is not text");
            need(!i.phrases.empty()||!i.text.empty(),at+"an empty comment");}
        if(f&Blocks){const size_t blocks=in.byte();need(blocks>=1&&blocks<=MaxBlocks,at+"1 to 7 blocks");int last=-1;
            for(size_t k=0;k<blocks;++k){Block b2;b2.id=in.byte();
#ifndef MUTANT_COLLECTION_ORDER
                need(int(b2.id)>last&&b2.id<MaxBlocks,at+"blocks are in their order, each once");
#else
                need(b2.id<MaxBlocks,at+"block");
#endif
                last=b2.id;
                const size_t fields=in.byte();need(fields>=1&&fields<=MaxBlockFields,at+"a block has 1 to 8 fields");std::string prev;
                for(size_t q=0;q<fields;++q){std::string name=in.text(MaxFieldName,"a field name");need(fieldName(name)&&(prev.empty()||name>prev),at+"block fields are named a-z and in order, each once");
                    const int64_t v=in.svar();need(within(v,MaxBlockValue),at+"a block value is out of range");prev=name;b2.fields.emplace_back(name,v);}
                i.blocks.push_back(b2);}}
        c.items.push_back(std::move(i));}
#ifndef MUTANT_COLLECTION_TRAILING
    need(in.at==body.size(),"bytes left over after the last structure");
#endif
    return c;
}
int symbol(char ch){
    if(ch>='a'&&ch<='z')ch=char(ch-'a'+'A');
    if(ch=='I'||ch=='L')return 1;
    if(ch=='O')return 0;
    const char* p=ch?std::strchr(Alphabet,ch):nullptr;return p?int(p-Alphabet):-1;}
bool space(char c){return c=='-'||c==' '||c=='\t'||c=='\r'||c=='\n';}
bool prefixAt(const std::string& s,size_t i){
    if(i+5>s.size())return false;
    return (s[i]=='D'||s[i]=='d')&&(s[i+1]=='S'||s[i+1]=='s')&&(s[i+2]=='K'||s[i+2]=='k')&&s[i+3]=='1'&&s[i+4]=='-';}
}
uint32_t crc32(const uint8_t* data,size_t size){
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<size;++i){c^=data[i];for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
    return ~c;}
bool utf8(const std::string& s,bool allowEmpty){
    if(s.empty())return allowEmpty;
    for(size_t i=0;i<s.size();){
        const unsigned char c=static_cast<unsigned char>(s[i]);
        if(c<0x20||c==0x7f)return false;
        if(c<0x80){++i;continue;}
        size_t n=(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
        if(!n||i+n>s.size())return false;
        uint32_t cp=c&(n==2?0x1f:n==3?0x0f:0x07);
        for(size_t k=1;k<n;++k){const unsigned char d=static_cast<unsigned char>(s[i+k]);if((d>>6)!=2)return false;cp=(cp<<6)|(d&0x3f);}
        if((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&(cp<0x10000||cp>0x10ffff))||(cp>=0xd800&&cp<=0xdfff)||(cp>=0x80&&cp<0xa0))return false;
        i+=n;}
    return true;}
bool nameText(const std::string& s){return !s.empty()&&s.size()<=MaxNameBytes&&s.front()!=' '&&s.back()!=' '&&utf8(s,false);}
bool levelText(const std::string& s){return s.size()<=MaxLevelBytes&&levelChars(s);}
std::string write(const Collection& c,std::string& why){
    std::vector<uint8_t> b;
    try{b=bytes(c);}catch(const Fail& f){why=f.why;return {};}
    std::string out=Prefix;int bits=0;uint32_t acc=0;size_t chars=0;
    for(size_t i=0;i<b.size()||bits;){
        if(bits<5&&i<b.size()){acc=(acc<<8)|b[i++];bits+=8;continue;}
        int v=bits>=5?int(acc>>(bits-5))&31:int(acc<<(5-bits))&31;
        bits=bits>=5?bits-5:0;acc&=(1u<<bits)-1;
        if(chars&&chars%5==0)out+='-';
        out+=Alphabet[v];++chars;}
    if(out.size()>MaxTextChars){why="the collection is "+std::to_string(out.size())+" characters, more than "+std::to_string(MaxTextChars)+": choose fewer structures";return {};}
    return out;}
bool parse(const std::string& text,Collection& out,std::string& why){
    try{
#ifndef MUTANT_COLLECTION_SIZE
        need(text.size()<=MaxTextChars,"longer than "+std::to_string(MaxTextChars)+" characters");
#endif
        need(prefixAt(text,0),"a collection begins with DSK1-");
        std::vector<int> symbols;
        for(size_t i=5;i<text.size();++i){
            if(space(text[i]))continue;
            int v=symbol(text[i]);need(v>=0,std::string("a character that is not in a collection: '")+(text[i]>=0x20&&text[i]<0x7f?std::string(1,text[i]):std::string("?"))+"'");
            symbols.push_back(v);}
        // 5 bits a character, the last one padded with zero bits; exactly as many characters as the bytes need.
        const size_t bytesHeld=symbols.size()*5/8;
        need(!symbols.empty()&&(bytesHeld*8+4)/5==symbols.size(),"the text has a character too many or too few");
        std::vector<uint8_t> b;uint32_t acc=0;int bits=0;
        for(int v:symbols){acc=(acc<<5)|uint32_t(v);bits+=5;if(bits>=8){b.push_back(uint8_t(acc>>(bits-8)));bits-=8;acc&=(1u<<bits)-1;}}
        need(acc==0,"the last character has stray bits");
        Collection c=read(b);
#ifndef MUTANT_COLLECTION_CANONICAL
        need(bytes(c)==b,"not written the way a collection is written");
#endif
        out=std::move(c);return true;
    }catch(const Fail& f){why=f.why;return false;}
}
std::string find(const std::string& text){
    for(size_t i=0;i+5<=text.size();++i){
        if(!prefixAt(text,i))continue;
        if(i>0&&((text[i-1]>='A'&&text[i-1]<='Z')||(text[i-1]>='a'&&text[i-1]<='z')||(text[i-1]>='0'&&text[i-1]<='9')))continue;
        // The body: base32 and '-', on as many lines as it was wrapped over (a line break and the spaces after it
        // continue it; a space within a line ends it). Each line break is a place it may end: the longest form that
        // reads as a collection is it, else the longest form (so the reader's reason can be shown).
        std::string out="DSK1-";std::vector<size_t> ends;bool afterBreak=false;
        for(size_t j=i+5;j<text.size()&&out.size()<=MaxTextChars;++j){
            const char c=text[j];
            if(c=='\r'||c=='\n'){if(!afterBreak)ends.push_back(out.size());afterBreak=true;continue;}
            if(c==' '||c=='\t'){if(afterBreak)continue;break;}
            if(c!='-'&&symbol(c)<0)break;
            afterBreak=false;out+=c;}
        ends.push_back(out.size());
        std::string longest;
        for(auto e=ends.rbegin();e!=ends.rend();++e){
            std::string cut=out.substr(0,*e);while(cut.size()>5&&cut.back()=='-')cut.pop_back();
            if(longest.empty())longest=cut;
            Collection c;std::string why;if(parse(cut,c,why))return cut;}
        return longest;}
    return {};}
std::string describe(const Collection& c){
    std::map<std::string,size_t> kinds;
    for(auto& i:c.items){std::string k(1,i.type);for(auto& n:Kinds)if(n.first==i.type)k=n.second;++kinds[k];}
    std::vector<std::pair<std::string,size_t>> list(kinds.begin(),kinds.end());
    std::stable_sort(list.begin(),list.end(),[](auto& a,auto& b){return a.second>b.second;});
    std::string out=std::to_string(c.items.size())+(c.items.size()==1?" structure (":" structures (");
    for(size_t k=0;k<list.size();++k){
        std::string name=list[k].first;if(list[k].second!=1)name+="s";
        out+=(k?", ":"")+std::to_string(list[k].second)+" "+name;}
    out+=")";
    if(!c.name.empty())out+=", named "+c.name;
    return out;}
std::string tag(const Collection& c,const std::string& text){
    if(!c.name.empty())return c.name;
    Collection copy;std::string why;
    if(!parse(text,copy,why))return "DSK1";
    std::vector<uint8_t> b=bytes(copy);char hex[9];std::snprintf(hex,sizeof hex,"%08x",crc32(b.data(),b.size()));
    return std::string("DSK1-")+hex;}
void turn(int64_t& dx,int64_t& dy,int64_t& rz,int64_t by){
    const double a=double(by)/100.0*3.14159265358979323846/180.0,c=std::cos(a),s=std::sin(a);
#ifndef MUTANT_COLLECTION_TURN_SIGN
    const double x=double(dx)*c+double(dy)*s,y=-double(dx)*s+double(dy)*c;
#else
    const double x=double(dx)*c-double(dy)*s,y=double(dx)*s+double(dy)*c;
#endif
    dx=int64_t(std::llround(x));dy=int64_t(std::llround(y));
    int64_t r=rz-by*1000;   // rz is degrees x 10^5, the heading hundredths of a degree
    const int64_t full=36000000,half=18000000;
    r%=full;if(r>half)r-=full;if(r<=-half)r+=full;
    rz=r;}
}
