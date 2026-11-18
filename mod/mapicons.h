#pragma once
// The other players on the game's world map (config `player_map`). Where they are comes from the server (GET .../coop/map
// with this player's notice token; ../world/README.md, "Players on the map"), read on a thread of its own, and each is added
// to the icon list the map menu builds at every update (game_offsets.md, "Icons on the device map"). No marker, entity or
// mission is made: nothing reaches the save, the world or the HUD, and walking up to a place triggers nothing.
#include "discovery.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace mod::mapicons {
constexpr uint64_t PollMs=2000,BackoffMs=15000;
constexpr size_t MaxIcons=32,ItemSize=0x60;
constexpr uint8_t DefaultKind=0xb5;     // the map's NPC icon: a person, in the class of icons shared with structures
struct Player {std::string id,name;double x=0,y=0,z=0;uint32_t area=0;};
struct Answer {bool read=false,on=false;std::vector<Player> players;};
Answer parse(const std::string& body);  // never throws
struct Icon {float x=0,y=0,z=0;uint32_t area=0;uint64_t id=0;};
// The icons of an answer: not this player's own (self: his relay id as text), only finite places inside the world's range,
// at most MaxIcons. Each id is stable for a player and cannot be a real marker's.
std::vector<Icon> icons(const Answer&,const std::string& self);
// The latest icons, for a reader that must not wait (the map's update): published whole, read without a lock.
class Board {
public:
    struct Picture {uint32_t count=0;std::array<Icon,MaxIcons> icons{};};
    void publish(const std::vector<Icon>&);
    void read(Picture&) const;
private:
    static constexpr unsigned Slots=8;      // a reader would have to stall for Slots-1 publications to meet the writer
    std::array<Picture,Slots> slots_{};std::atomic<unsigned> current_{0};
};
// The game's list item for an icon.
void fillItem(uint8_t (&item)[ItemSize],const Icon&,uint8_t kind);
// One map update: every icon of the area on show goes to `add` (false: the game did not take it). shownArea 0 or an icon's
// area 0: not known, shown. Returns how many were taken. Nothing here allocates.
template<class Add> unsigned addIcons(const Board& board,uint32_t shownArea,uint8_t kind,Add&& add){
    Board::Picture p;board.read(p);unsigned taken=0;
    for(uint32_t i=0;i<p.count&&i<MaxIcons;++i){const Icon& n=p.icons[i];
#ifndef MUTANT_MAPICONS_AREA
        if(shownArea&&n.area&&n.area!=shownArea)continue;
#endif
        alignas(16) uint8_t item[ItemSize];fillItem(item,n,kind);if(add(item))++taken;}
    return taken;
}
struct Result {int status=0;std::string body;};
// The reads, on their own thread (never the game's). extra: icons a lab test adds to every answer.
class Link {
public:
    using Get=std::function<Result()>;using Schedule=std::function<uint64_t(bool good,unsigned& failures)>;
    Link(Get get,Board& board,std::string self,Schedule schedule={});
    ~Link();
    void setExtra(std::vector<Icon>);
    struct Counters {unsigned reads=0,shown=0;bool on=false;};
    Counters counters() const;
private:
    void run();
    Get get_;Board& board_;std::string self_;Schedule schedule_;mutable std::mutex lock_;std::condition_variable wake_;bool stop_=false;
    std::vector<Icon> extra_;Counters counters_;std::thread thread_;
};
uint64_t delayAfter(bool good,unsigned& failures);
struct Stats {uint64_t updates=0,taken=0,refused=0;uint32_t shownArea=0,mode=0;};
// Windows: puts the map's update through the mod (a retargeted call; every byte checked first, nothing changed on a mismatch).
// Throws when it cannot. board lives as long as the process. Game thread.
void install(discovery::Sampler&,const Board& board);
void setKind(uint8_t);
Stats stats();
// The area this porter is in, as the map names it (0: not known yet).
std::function<uint32_t()> nativeArea(discovery::Sampler&);
}
