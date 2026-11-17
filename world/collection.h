#pragma once
// Collections (../docs/COLLECTIONS.md): a group of structures a player built, as one line of text ("DSK1-..."), so
// another player can import it on a server. Portable and without the world: the server (world.cpp) fills and reads
// it, the launcher reads it to check and describe a pasted string.
//
// The text: "DSK1-" and Crockford base32 of the data in groups of 5 separated by '-'. The data: version, flags, count,
// the anchor (area, place in centimetres, heading), a name; then each structure (kind, place relative to the anchor,
// angles, optional parts); then the CRC-32 of all of it. `parse` refuses anything that is not exactly what `write`
// makes for the same collection (ranges, order, the CRC, a byte left over, a longer encoding than needed).
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace collection {
constexpr const char* Prefix="DSK1-";
constexpr uint8_t Version=1;
constexpr size_t MaxItems=200,MaxTextChars=16384;
constexpr int64_t MaxAnchorCm=10000000;        // 100 km from the map's origin on each axis
constexpr int64_t MaxOffsetCm=200000;          // 2 km from the anchor on each axis
constexpr int64_t MaxAngle=36000000;           // degrees x 10^5, as the game's rx, ry, rz
constexpr int64_t MaxHeading=18000;            // hundredths of a degree
constexpr int64_t MaxArea=1000000,MaxCell=1000000,MaxMaterial=1000000;
constexpr size_t MaxNameBytes=32,MaxLevelBytes=32,MaxPhrases=16,MaxCommentBytes=120,MaxBlocks=7,MaxBlockFields=8,MaxFieldName=8;
constexpr int64_t MaxBlockValue=2147483647;
// The game's info blocks of a structure, in this order (block ids 0..6).
constexpr std::array<const char*,MaxBlocks> BlockNames{{"ri","si","bi","pi","vi","ei","ci"}};
constexpr uint8_t HeadingKnown=1,Named=2;                                    // header flags
constexpr uint32_t Tilt=1,Level=2,OwnArea=4,Cell=8,PFlag=16,Materials=32,Comment=64,Blocks=128;   // a structure's parts
struct Block { uint8_t id=0; std::vector<std::pair<std::string,int64_t>> fields; };   // id: index in BlockNames
struct Item {
    char type='l';                              // the strand server's type letter, a..z
    int64_t dx=0,dy=0,dz=0;                     // centimetres from the anchor, game axes (z up)
    int64_t rx=0,ry=0,rz=0;                     // degrees x 10^5
    std::string level;                          // the game's `st`; "" none
    bool ownArea=false;int64_t area=0;          // its area when not the anchor's
    bool cell=false;int64_t cellX=0,cellY=0,map=0;
    bool p=false;
    bool materials=false;std::array<int64_t,6> material{};
    bool comment=false;std::vector<int64_t> phrases;std::string text;
    std::vector<Block> blocks;
};
struct Collection {
    int64_t area=0,x=0,y=0,z=0;                 // the anchor: area and place in centimetres
    bool headingKnown=false;int64_t heading=0;  // hundredths of a degree, the porter's facing (0: +y, 9000: +x)
    std::string name;                           // "" none
    std::vector<Item> items;
};
// The text for a collection, or "" with `why` when it breaks a rule (the same rules parse keeps).
std::string write(const Collection&,std::string& why);
// A collection from a text, or false with `why`. Takes the prefix in any case, ignores '-' and white space in the body,
// reads lower case, I/L as 1, O as 0. Refuses anything longer than MaxTextChars.
bool parse(const std::string& text,Collection& out,std::string& why);
// The first DSK string in a pasted text (a chat line, a .dsk file): from the prefix to the last character of the body,
// joined over line breaks. "" when there is none.
std::string find(const std::string& text);
// A short description: "12 structures (5 ladders, 4 cairns, 3 bridges), named Camp" (for the launcher).
std::string describe(const Collection&);
uint32_t crc32(const uint8_t* data,size_t size);
std::string tag(const Collection&,const std::string& text);   // the source an import is stored with: its name, else "DSK1-" and the CRC
bool nameText(const std::string&);                             // 1..MaxNameBytes of UTF-8, no control characters, no space at the ends
bool levelText(const std::string&);
bool utf8(const std::string&,bool allowEmpty);                 // well-formed UTF-8 without control characters
// Turning about the vertical (the porter's heading: forward is (sin h, cos h) in the game's x, y). A collection made
// facing `from` and placed facing `to` turns each offset by to - from (centidegrees) and each `rz` by minus it.
void turn(int64_t& dx,int64_t& dy,int64_t& rz,int64_t byCentidegrees);
}
