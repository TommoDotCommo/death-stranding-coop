#include "offer_strand.h"
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
namespace mod::offer_strand {
std::optional<Object> read(const discovery::Sampler& s,uint64_t o){
    Object x;uint8_t flags=0;uint64_t id=0;
    if(!s.read(o+0x34,&x.type,1)||!s.read(o+0x36,&x.state,1)||!s.read(o+0xb2,&x.owner,1)||!s.read(o+0xab,&flags,1)||
       !s.read(o+0x84,&x.level,4)||!s.read(o+0x40,x.position.data(),24)||!s.read(o+0x58,x.rotation.data(),36)||!s.read(o+0xb8,&id,8))
        return std::nullopt;
    x.held=flags&0x80;
    // The game's body takes a bridge's angle from +0x528, a chiral bridge's from +0x590, a jump stand's from +0x580.
    uint64_t at=x.type==31||x.type==34?0x528:x.type==41?0x590:x.type==44?0x580:0;
    if(at&&!s.read(o+at,&x.angle,4))return std::nullopt;
    // An engine string: the characters, their length (u32) 8 bytes in front.
    uint32_t n=0;
    if(id&&s.read(id-8,&n,4)&&n){
        if(n>MaxId)return std::nullopt;              // no id of the game's is this long: not a string
        x.online.assign(n,'\0');if(!s.read(id,x.online.data(),n))return std::nullopt;
    }
    return x;
}
char letter(uint8_t t){
    switch(t){
        case 3:return 's';case 6:case 7:return 'p';case 9:return 'w';case 12:return 'a';case 15:return 'c';
        case 18:return 'n';case 22:return 'z';case 25:return 'l';case 28:return 'r';case 31:case 34:return 'b';
        case 36:return 'h';case 41:return 'g';case 44:return 'j';default:return 0;
    }
}
// An id as the game's servers make them: the kind's letter (the original service's own uploads may carry it in
// capitals) and 12 letters or digits.
static bool gameShape(const std::string& id,char kind){
#ifndef MUTANT_OFFER_SHAPE
    if(id.size()!=13||(id[0]!=kind&&id[0]!=std::toupper(static_cast<unsigned char>(kind))))return false;
#else
    if(id.size()!=13)return false;
#endif
    for(size_t i=1;i<id.size();++i)if(!std::isalnum(static_cast<unsigned char>(id[i])))return false;
    return true;
}
Decision plan(const Object& x){
#ifndef MUTANT_OFFER_OWNER
    if(x.owner!=Player)return {Plan::Skip,"not built by this player"};
#endif
#ifndef MUTANT_OFFER_PENDING
    if(x.state==0)return {Plan::Skip,"not built"};   // a request the game has not built: nothing stands there
#endif
#ifndef MUTANT_OFFER_HELD
    if(x.held)return {Plan::Skip,"the game does not upload it"};
#endif
    char k=letter(x.type);
    if(!k)return {Plan::Skip,"a kind the game does not upload"};
    if(x.online.empty())return {Plan::Upload,"on no server"};
    if(!gameShape(x.online,k))return {Plan::Skip,"an id of another shape"};
    return {Plan::Recreate,"on another server"};
}
std::array<int32_t,2> cell(double x,double y,int32_t tile,int32_t width,int32_t height){
    if(tile<=0||width<=0||height<=0)return {0,0};
    double t=static_cast<double>(static_cast<float>(tile));   // the game: int to float to double
    auto one=[&](double v,int32_t n){
        double f=std::floor(v/t);if(!(f>-1e9&&f<1e9))f=f<0?-1e9:1e9;
        int64_t c=static_cast<int64_t>(f)+n/2;
#ifndef MUTANT_OFFER_CLAMP
        if(c<0)c=0;else if(c>n-1)c=n-1;
#endif
        return static_cast<int32_t>(c/2);};
    return {one(x,width),one(y,height)};
}
// cvttss2si: truncation, and the "integer indefinite" value for what does not fit.
static int32_t truncate(float v){
    if(!(v>-2147483648.0f&&v<2147483648.0f))return INT32_MIN;
    return static_cast<int32_t>(v);
}
static const float Degrees=57.2957763671875f,Scale=100000.0f;
std::array<int32_t,3> angles(const std::array<float,9>& m){
    // 0x19c1270: ry = asin(-m02); rx = atan2(m12, m22), rz = atan2(m01, m00); near the poles rx 0, rz atan2(-m10, m11).
    float s=-m[2];if(!(s>-1.0f))s=-1.0f;else if(s>1.0f)s=1.0f;
    float ry=std::asin(s),rx=0,rz;
#ifndef MUTANT_OFFER_POLE
    if(std::cos(ry)>1.9073486328125e-06f){rx=std::atan2(m[5],m[8]);rz=std::atan2(m[1],m[0]);}
    else rz=std::atan2(-m[3],m[4]);
#else
    rx=std::atan2(m[5],m[8]);rz=std::atan2(m[1],m[0]);
#endif
    return {truncate((Degrees*rx)*Scale),truncate((Degrees*ry)*Scale),truncate((Degrees*rz)*Scale)};
}
static int32_t place(double v){
#ifndef MUTANT_OFFER_FLOAT
    return truncate(static_cast<float>(v)*Scale);    // the game multiplies the f32 of the position
#else
    double d=v*100000.0;return d>-2147483648.0&&d<2147483648.0?static_cast<int32_t>(d):INT32_MIN;
#endif
}
std::string body(const Object& x,const Placement& p,bool recreate){
    char k=letter(x.type);
    if(!k||k=='r')return {};                         // a rope's "ri" is filled through a call into the object
    std::string st;
    if(k=='l')st=std::to_string(x.level);
#ifndef MUTANT_OFFER_BRIDGE
    else if(x.type==31)st="30";                      // the 30 m bridge; the 45 m bridge sends an empty "st"
#else
    else if(k=='b')st="30";
#endif
    auto a=angles(x.rotation);
    char b[512];
    int n=std::snprintf(b,sizeof b,"{\"x\":%d,\"y\":%d,\"m\":%d,\"t\":\"%c\",\"px\":%d,\"py\":%d,\"pz\":%d,\"rx\":%d,\"ry\":%d,\"rz\":%d,"
        "\"exp\":5,\"q\":%d,\"st\":\"%s\",\"p\":%d",p.x,p.y,p.map,k,place(x.position[0]),place(x.position[1]),place(x.position[2]),
        a[0],a[1],a[2],p.area,st.c_str(),p.priority);
    std::string out(b,n>0?size_t(n):0);
    if(k=='b'||k=='g'||k=='j')out+=",\"bi\":{\"a\":"+std::to_string(truncate(Scale*(Degrees*x.angle)))+"}";
    if(p.alternative>0)out+=",\"ei\":{\"aq\":"+std::to_string(p.alternative)+"}";
    out+=",\"lp\":"+std::to_string(p.likes);
    if(recreate){
        if(!gameShape(x.online,k))return {};
        out+=",\"id\":\""+x.online+"\"";
    }
    return out+"}";
}
}
