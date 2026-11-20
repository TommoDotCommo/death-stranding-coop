#pragma once
// Pings and likes between players in group play (config `group_pings`, opt-in; README "Pings and likes in group play").
// They travel through the relay's broadcast mode (core/broadcast.h SIGNAL and SIGNALLED; world/README.md "Pings and
// likes"): the server passes a ping to the players near who are shown its sender, and a like only to the player it
// names, while he is near and shown the sender. This file holds what the mod decides, without a game:
//
//   Pings       the pings shown here: each (sender, serial) once, for ShowMs, at most MaxPings, as HUD marks
//               (hudmarkers.h, a second set of its own) with ids that cannot be the far players' or the game's;
//   likeTarget  whom the like key gives a like: the nearest other player shown here within LikeRangeMetres;
//   LikeGate    whether a like that arrived is given: each (sender, serial) once, at most PerSenderPerMinute from one
//               sender and the partner path's cap for all (likes.h Receiver: one a second, ten a minute);
//   pingLine    the info-log line that names the sender.
#include "hudmarkers.h"
#include "likes.h"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace mod::grouppings {
constexpr unsigned PingKey=0x42,LikeKey=0x4c;   // B and L: the partner path's ping and like keys (marker.h, likes.h)
constexpr uint64_t ShowMs=30000;                // a ping stays on the HUD this long
constexpr size_t MaxPings=16;                   // pings shown at once (the oldest goes)
constexpr size_t Remembered=256;                // (sender, serial) pairs remembered against repeats
constexpr double LikeRangeMetres=25;            // the like key's reach (the server allows 50 m for the delay)
// A ping's HUD id: a tag the far players' marks (0x4855...) and the map's icons (0x4d50...) never carry, and 48 bits
// of the sender and his serial.
uint64_t markId(uint64_t sender,uint32_t serial);
struct Ping { uint64_t sender=0; uint32_t serial=0; std::array<double,3> at{}; uint64_t since=0; };   // game axes (z up)
class Pings {
public:
    // A ping that arrived (or this porter's own): true when it is new and now shown; a repeat of one already taken is
    // false, as is one with a place that is not finite.
    bool take(uint64_t sender,uint32_t serial,const std::array<double,3>& at,uint64_t now);
    // The pings shown now, newest first, as HUD marks; the expired ones go.
    std::vector<hudmarkers::Mark> marks(uint64_t now);
    size_t shown(uint64_t now);
private:
    void expire(uint64_t now);
    std::deque<Ping> live_;std::deque<std::pair<uint64_t,uint32_t>> seen_;
};
// A player shown here: his relay id, where he is (the core's State axes), whether he is shown now.
struct Candidate { uint64_t id=0; std::array<double,3> at{}; bool shown=false; };
// The nearest shown player within range of `here` (same axes), or none.
std::optional<uint64_t> likeTarget(const std::array<double,3>& here,const std::vector<Candidate>&,double range=LikeRangeMetres);
class LikeGate {
public:
    static constexpr unsigned PerSenderPerMinute=3;
    bool accept(uint64_t sender,uint32_t serial,uint64_t now);
private:
    likes::Receiver all_;std::map<uint64_t,std::deque<uint64_t>> by_;std::deque<std::pair<uint64_t,uint32_t>> seen_;
};
// "Ann pinged a place 120 m away." ("here" within 5 m; no distance when not known: metres < 0). The name is cleaned
// like the name tag's; "A player" without one.
std::u16string pingLine(const std::string& name,double metres);
// Serial numbers for this porter's signals: never 0, starting from a base (so a restarted game does not repeat the
// serials the others remember).
class Serials {
public:
    explicit Serials(uint32_t base=1):next_(base?base:1){}
    uint32_t next(){uint32_t s=next_++;if(!next_)next_=1;return s;}
private:
    uint32_t next_;
};
}
