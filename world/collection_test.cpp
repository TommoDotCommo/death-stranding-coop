// Collections (collection.h, ../docs/COLLECTIONS.md): the DSK string written and read back, the forms a paste may take,
// the turn about the vertical, and every refusal: wrong prefix, version, flags, check sum, count, ranges, order, a byte
// left over, a longer encoding than needed, stray bits, a character too many, a text too long. Then a fuzzer: random
// collections round trip, every changed character is refused, several random edits are never read as another
// collection, and random bytes never crash the reader (built with the sanitizers where the compiler has them).
//   collection-test [ITERATIONS]
#include "collection.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
using namespace collection;
static unsigned failures=0;
#define CHECK(...) do{if(!(__VA_ARGS__)){std::fprintf(stderr,"collection oracle: %s (line %d)\n",#__VA_ARGS__,__LINE__);++failures;}}while(0)
static const char* Alpha="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
// The test's own base32, so it can write data the library would never write.
static std::string encode(const std::vector<uint8_t>& b){
    std::string out="DSK1-";int bits=0;uint32_t acc=0;size_t n=0;
    for(size_t i=0;i<b.size()||bits;){
        if(bits<5&&i<b.size()){acc=(acc<<8)|b[i++];bits+=8;continue;}
        int v=bits>=5?int(acc>>(bits-5))&31:int(acc<<(5-bits))&31;bits=bits>=5?bits-5:0;acc&=(1u<<bits)-1;
        if(n&&n%5==0){out+='-';}
        out+=Alpha[v];++n;}
    return out;}
static std::vector<uint8_t> decode(const std::string& t){
    std::vector<uint8_t> b;uint32_t acc=0;int bits=0;
    for(size_t i=5;i<t.size();++i){if(t[i]=='-')continue;acc=(acc<<5)|uint32_t(std::strchr(Alpha,t[i])-Alpha);bits+=5;if(bits>=8){b.push_back(uint8_t(acc>>(bits-8)));bits-=8;acc&=(1u<<bits)-1;}}
    return b;}
// Data with its check sum made right again: what a refusal must come from is the change, not the CRC.
static std::string sealed(std::vector<uint8_t> body){
    uint32_t c=crc32(body.data(),body.size());for(int k=0;k<4;++k)body.push_back(uint8_t(c>>(8*k)));return encode(body);}
static std::vector<uint8_t> bodyOf(const std::string& text){auto b=decode(text);b.resize(b.size()-4);return b;}
static bool refused(const std::string& text,const char* reason=nullptr){
    Collection c;std::string why;bool ok=parse(text,c,why);
    if(!ok&&reason&&why.find(reason)==std::string::npos){std::fprintf(stderr,"collection oracle: refused for '%s', expected '%s'\n",why.c_str(),reason);++failures;}
    return !ok;}
static bool same(const Collection& a,const Collection& b){std::string w;return write(a,w)==write(b,w);}
static Collection sample(){
    Collection c;c.area=226;c.x=106404184/1000;c.y=32080970/1000;c.z=1467307;c.headingKnown=true;c.heading=-4500;c.name="Camp at Lake Knot";
    Item ladder;ladder.type='l';ladder.dx=120;ladder.dy=-340;ladder.dz=15;ladder.rz=-11963270;ladder.level="0";ladder.cell=true;ladder.cellX=9;ladder.cellY=7;ladder.map=22123;
    Item cairn;cairn.type='e';cairn.dx=-2500;cairn.dy=1800;cairn.blocks={Block{1,{{"r",1}}}};
    Item bridge;bridge.type='b';bridge.dx=4000;bridge.dy=4000;bridge.dz=-200;bridge.rx=150000;bridge.rz=9000000;bridge.level="30";bridge.p=true;bridge.materials=true;bridge.material={0,0,1000,0,0,0};
    bridge.blocks={Block{2,{{"a",-111525}}}};bridge.ownArea=true;bridge.area=227;
    Item sign;sign.type='t';sign.level="Type1001";sign.comment=true;sign.phrases={3,17,65535};sign.text="Watch out: BTs ↓";
    Item rope;rope.type='r';rope.dz=-1000;rope.level="0";rope.blocks={Block{0,{{"h",4500000},{"l",1200000},{"p",-3000000}}},Block{5,{{"aq",228}}}};
    c.items={ladder,cairn,bridge,sign,rope};return c;}
static void roundTrip(){
    Collection c=sample();std::string why,text=write(c,why);
    CHECK(!text.empty()&&why.empty()&&text.rfind("DSK1-",0)==0);
    // Groups of five, separated by '-'.
    for(size_t i=5,n=0;i<text.size();++i){if((n+1)%6==0){CHECK(text[i]=='-');}else CHECK(std::strchr(Alpha,text[i])!=nullptr);++n;}
    Collection back;CHECK(parse(text,back,why)&&same(back,c));
    CHECK(back.items.size()==5&&back.name=="Camp at Lake Knot"&&back.headingKnown&&back.heading==-4500&&back.items[3].text=="Watch out: BTs ↓"&&back.items[3].phrases.size()==3);
    CHECK(back.items[2].ownArea&&back.items[2].area==227&&back.items[2].p&&back.items[2].material[2]==1000&&back.items[2].blocks[0].fields[0].second==-111525);
    CHECK(back.items[4].blocks.size()==2&&back.items[4].blocks[1].id==5&&back.items[0].cell&&back.items[0].map==22123&&back.items[0].rz==-11963270);
    // The forms a paste takes: lower case, wrapped, without dashes, I/L for 1 and O for 0, the prefix in lower case.
    std::string lower=text;for(auto& ch:lower)if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
    CHECK(parse(lower,back,why)&&same(back,c));
    std::string wrapped;for(size_t i=0;i<text.size();++i){wrapped+=text[i];if(i%40==39)wrapped+="\r\n  ";}
    CHECK(parse(wrapped,back,why)&&same(back,c));
    std::string bare="DSK1-";for(size_t i=5;i<text.size();++i)if(text[i]!='-')bare+=text[i];
    CHECK(parse(bare,back,why)&&same(back,c));
    std::string look=text;for(size_t i=5;i<look.size();++i){if(look[i]=='1')look[i]='I';else if(look[i]=='0')look[i]='O';}
    CHECK(parse(look,back,why)&&same(back,c));
    // Found in a chat line and in a file with line breaks; not inside a longer word.
    CHECK(find("here it is: "+text+" have fun")==text);
    CHECK(parse(find("> "+wrapped+"\n\nbye"),back,why)&&same(back,c));
    CHECK(parse(find(text+"\n"),back,why)&&same(back,c)&&find("look: "+text.substr(0,30)+" more")==text.substr(0,30));
    CHECK(find("XDSK1-ABCDE")==""&&find("nothing here")=="");
    // The smallest collection: one structure at the anchor, no name, no heading.
    Collection one;one.items.push_back(Item{});std::string t1=write(one,why);
    CHECK(!t1.empty()&&parse(t1,back,why)&&back.items.size()==1&&!back.headingKnown&&back.name.empty());
    CHECK(describe(c)=="5 structures (1 bridge, 1 cairn, 1 ladder, 1 rope, 1 sign), named Camp at Lake Knot");
    Collection three;three.items={Item{},Item{},Item{}};three.items[2].type='e';
    CHECK(describe(three)=="3 structures (2 ladders, 1 cairn)");
    CHECK(tag(c,text)=="Camp at Lake Knot"&&tag(three,write(three,why)).rfind("DSK1-",0)==0&&tag(three,write(three,why)).size()==13);
}
static void refusals(){
    std::string why;const std::string good=write(sample(),why);const auto body=bodyOf(good);
    CHECK(refused("",nullptr)&&refused("DSK1-","too many or too few")&&refused("DSG-ABCDE","begins with DSK1-")&&refused("DSK2-ABCDE","begins with DSK1-"));
    CHECK(refused("DSK1-ABCU",nullptr)&&refused("DSK1-AB*CD","not in a collection"));
    // The check sum: one byte changed without it.
    {auto b=decode(good);b[10]^=0x01;CHECK(refused(encode(b),"check sum"));}
    // Another version, unknown flags.
    {auto b=body;b[0]=2;CHECK(refused(sealed(b),"format 2"));}
    {auto b=body;b[1]|=4;CHECK(refused(sealed(b),"unknown flags"));}
    // A byte left over after the last structure.
    {auto b=body;b.push_back(0);CHECK(refused(sealed(b),"left over"));}
    // A number written longer than it needs (the count, 5, as 0x85 0x00): the same collection, not its canonical text.
    {auto b=body;CHECK(b[2]==5);b[2]=0x85;b.insert(b.begin()+3,0x00);CHECK(refused(sealed(b),"not written the way"));}
    // A character too many, and stray bits in the last one.
    {std::string t=good;t+="0";CHECK(refused(t));t=good+"00";CHECK(refused(t));}
    {std::string t=good;char& last=t.back();last=Alpha[(std::strchr(Alpha,last)-Alpha)|1];if(t!=good)CHECK(refused(t,nullptr));}
    // The data ends too early.
    {auto b=body;b.resize(b.size()-3);CHECK(refused(sealed(b),nullptr));}
    // 201 structures: 200 at the anchor, then one more, and the count says 201.
    {Collection c;c.items.assign(200,Item{});std::string t=write(c,why);CHECK(!t.empty());
     Collection more=c;more.items.push_back(Item{});CHECK(write(more,why).empty()&&why.find("at most 200")!=std::string::npos);
     auto b=bodyOf(t);CHECK(b[2]==0xc8&&b[3]==0x01);b[2]=0xc9;for(uint8_t x:{uint8_t('l'),uint8_t(0),uint8_t(0),uint8_t(0),uint8_t(0),uint8_t(0)})b.push_back(x);
     CHECK(refused(sealed(b),"1 to 200"));}
    // A structure 2 km and a centimetre from the anchor (the second structure's dx written by hand).
    {Collection c;c.items.assign(1,Item{});c.items[0].dx=MaxOffsetCm;CHECK(!write(c,why).empty());c.items[0].dx=MaxOffsetCm+1;CHECK(write(c,why).empty());
     c.items[0].dx=MaxOffsetCm;auto b=bodyOf(write(c,why));
     // dx is the zigzag of 200000 = 400000 = 0x80 0xb5 0x18; 200001 -> 400002 = 0x82 0xb5 0x18.
     size_t at=0;for(size_t i=0;i+2<b.size();++i)if(b[i]==0x80&&b[i+1]==0xb5&&b[i+2]==0x18){at=i;break;}
     CHECK(at>0);b[at]=0x82;CHECK(refused(sealed(b),"more than 2 km"));}
    // Blocks out of their order: a rope's blocks swapped.
    {Collection c;Item r;r.type='r';r.blocks={Block{0,{{"h",1}}},Block{1,{{"r",1}}}};c.items={r};auto b=bodyOf(write(c,why));
     size_t at=0;for(size_t i=0;i+3<b.size();++i)if(b[i]==2&&b[i+1]==0&&b[i+2]==1&&b[i+3]==1){at=i;break;}
     CHECK(at>0);b[at+1]=1;b[at+6]=0;CHECK(refused(sealed(b),"in their order"));
     Collection wrong=c;std::swap(wrong.items[0].blocks[0],wrong.items[0].blocks[1]);CHECK(write(wrong,why).empty());}
    // Names and texts the writer refuses.
    {Collection c;c.items.assign(1,Item{});c.name=" padded";CHECK(write(c,why).empty());c.name=std::string(33,'a');CHECK(write(c,why).empty());c.name="tab\there";CHECK(write(c,why).empty());
     c.name="ok";c.items[0].level="a b";CHECK(write(c,why).empty());c.items[0].level="";c.items[0].type='A';CHECK(write(c,why).empty());
     c.items[0].type='l';c.items[0].comment=true;CHECK(write(c,why).empty());c.items[0].text=std::string("\xc0\xaf");CHECK(write(c,why).empty());}
    // Too long: 200 signs with 120 bytes of text each. The writer refuses it; written by hand, the reader does too.
    {Collection c;Item s;s.type='t';s.comment=true;s.text=std::string(120,'x');c.items.assign(200,s);CHECK(write(c,why).empty()&&why.find("choose fewer")!=std::string::npos);
     std::vector<uint8_t> b={1,0,0xc8,0x01,0,0,0,0};
     for(int k=0;k<200;++k){b.push_back('t');b.push_back(64);for(int q=0;q<4;++q)b.push_back(0);b.push_back(0);b.push_back(120);for(int q=0;q<120;++q)b.push_back('x');}
     std::string t=sealed(b);CHECK(t.size()>MaxTextChars&&refused(t,"longer than"));}
}
static void turning(){
    // Facing +y, turned to face +x (9000): what was 1 m ahead is 1 m to +x, and rz turns by -90 degrees.
    int64_t dx=0,dy=100,rz=0;turn(dx,dy,rz,9000);CHECK(dx==100&&dy==0&&rz==-9000000);
    dx=100;dy=0;rz=0;turn(dx,dy,rz,9000);CHECK(dx==0&&dy==-100);
    dx=30;dy=40;rz=17000000;turn(dx,dy,rz,-9000);CHECK(dx==-40&&dy==30&&rz==-10000000);   // 170 + 90 = 260 = -100 degrees
    dx=30;dy=40;rz=5;turn(dx,dy,rz,0);CHECK(dx==30&&dy==40&&rz==5);
    dx=0;dy=1000;rz=0;turn(dx,dy,rz,18000);CHECK(dx==0&&dy==-1000&&rz==18000000);
}
static Collection random(std::mt19937& r){
    auto pick=[&](int64_t lo,int64_t hi){return std::uniform_int_distribution<int64_t>(lo,hi)(r);};
    Collection c;c.area=pick(0,MaxArea);c.x=pick(-MaxAnchorCm,MaxAnchorCm);c.y=pick(-MaxAnchorCm,MaxAnchorCm);c.z=pick(-MaxAnchorCm,MaxAnchorCm);
    if(pick(0,1)){c.headingKnown=true;c.heading=pick(-MaxHeading,MaxHeading);}
    if(pick(0,1)){c.name="n";for(int k=pick(0,20);k>0;--k)c.name+=char('a'+pick(0,25));}
    for(int n=int(pick(1,pick(0,9)?8:60));n>0;--n){
        Item i;i.type=char('a'+pick(0,25));i.dx=pick(-MaxOffsetCm,MaxOffsetCm);i.dy=pick(-2000,2000);i.dz=pick(-500,500);i.rz=pick(-MaxAngle,MaxAngle);
        if(pick(0,3)==0){i.rx=pick(-MaxAngle,MaxAngle);i.ry=pick(1,100);}
        if(pick(0,1))i.level=std::to_string(pick(0,40));
        if(pick(0,4)==0){i.ownArea=true;i.area=c.area==0?1:c.area-1;}
        if(pick(0,2)==0){i.cell=true;i.cellX=pick(-MaxCell,MaxCell);i.cellY=pick(-50,50);i.map=pick(0,MaxCell);}
        i.p=pick(0,1);
        if(pick(0,3)==0){i.materials=true;for(auto& m:i.material)m=pick(0,MaxMaterial);}
        if(pick(0,5)==0){i.comment=true;for(int k=int(pick(0,16));k>0;--k)i.phrases.push_back(pick(0,65535));if(i.phrases.empty()||pick(0,1))i.text="sign "+std::to_string(pick(0,999));}
        for(uint8_t id=0;id<MaxBlocks;++id)if(pick(0,6)==0){Block b;b.id=id;std::string name="a";for(int k=int(pick(1,MaxBlockFields));k>0;--k){b.fields.emplace_back(name,pick(-MaxBlockValue,MaxBlockValue));name[0]=char(name[0]+1);}i.blocks.push_back(b);}
        c.items.push_back(i);}
    return c;}
static void fuzz(long iterations){
    std::mt19937 r(20261118);long mutated=0,accepted=0;
    for(long it=0;it<iterations;++it){
        Collection c=random(r);std::string why,text=write(c,why);
        if(text.empty()){CHECK(why.find("choose fewer")!=std::string::npos);continue;}
        Collection back;CHECK(parse(text,back,why)&&same(back,c));
        // One character changed to another value: always refused (a burst of at most 10 bits; CRC-32 sees every one).
        size_t at=5+size_t(r()%(text.size()-5));
        if(text[at]!='-'){std::string t=text;char v=Alpha[(std::strchr(Alpha,t[at])-Alpha+1+int(r()%31))%32];t[at]=v;CHECK(refused(t));}
        // Several edits (change, delete, insert): never read as another collection.
        std::string t=text;
        for(int k=int(1+r()%4);k>0;--k){size_t p=5+size_t(r()%(t.size()-5));int what=int(r()%3);
            if(what==0)t[p]=Alpha[r()%32];else if(what==1&&t.size()>6)t.erase(p,1);else t.insert(t.begin()+long(p),Alpha[r()%32]);}
        if(t!=text){++mutated;Collection got;if(parse(t,got,why)&&!same(got,c))++accepted;}
        // Random bytes, random text after the prefix: must not crash; anything read must write back to itself.
        std::string junk="DSK1-";for(int k=int(r()%300);k>0;--k)junk+=char(r()%256);
        Collection got;if(parse(junk,got,why)){std::string w=write(got,why);CHECK(!w.empty());}
        std::vector<uint8_t> raw;for(int k=int(r()%200);k>0;--k)raw.push_back(uint8_t(r()));
        if(parse(sealed(raw),got,why)){std::string w=write(got,why);CHECK(!w.empty());}}
    CHECK(accepted==0);
    std::printf("fuzz: %ld collections, %ld edited texts, %ld read as another collection\n",iterations,mutated,accepted);
}
int main(int argc,char** argv){
    long iterations=argc>1?std::atol(argv[1]):3000;
    roundTrip();refusals();turning();fuzz(iterations);
    if(failures){std::fprintf(stderr,"collection oracle: %u failures\n",failures);return 1;}
    std::puts("collection test passed: the DSK string written, read, turned and refused; fuzzed");return 0;
}
