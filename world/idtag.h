#pragma once
// Object ids a game can tell as this server's without asking it. An id is the type letter and 12 letters or digits, the
// shape the game needs; on this server the last four are a keyed hash of the rest, the key being the server's id. The mod
// decides in the moment the game makes a structure (../mod/serverworld.h), so the test takes no network and no memory. It
// is a label, not a signature: the server's id is public, and what it decides is only what one player's game shows.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
namespace idtag {
constexpr size_t IdChars=13,TagChars=4,BodyChars=IdChars-TagChars;
inline void tagOf(const char* key,size_t keyChars,const char* id,char out[TagChars]){
    static constexpr char Digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr uint64_t Prime=1099511628211ull;
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<keyChars;++i){h^=uint8_t(key[i]);h*=Prime;}
    h^=0xff;h*=Prime;
    for(size_t i=0;i<BodyChars;++i){h^=uint8_t(id[i]);h*=Prime;}
    h^=h>>29;h*=0xbf58476d1ce4e5b9ull;h^=h>>32;
    for(size_t i=0;i<TagChars;++i){out[i]=Digits[h%62];h/=62;}
}
inline bool carries(const char* key,size_t keyChars,const char* id,size_t idChars){
    if(idChars!=IdChars||!id)return false;
    char tag[TagChars];tagOf(key,keyChars,id,tag);
    return std::memcmp(tag,id+BodyChars,TagChars)==0;
}
inline bool carries(const std::string& key,const std::string& id){return carries(key.data(),key.size(),id.data(),id.size());}
inline void stamp(const std::string& key,std::string& id){   // an id of the full length gets this server's tag
    if(id.size()!=IdChars)return;
    char tag[TagChars];tagOf(key.data(),key.size(),id.data(),tag);id.replace(BodyChars,TagChars,tag,TagChars);
}
}
