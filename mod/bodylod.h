#pragma once
// Cheaper bodies far away (config `body_detail_metres`, `body_hide_metres`): what each of the other players' bodies is
// to look like from its distance to this porter. A far body is small on screen, so its shadow and its cloth simulation
// are not worth what they cost (live 2026-11-20, 32 bodies within 12 m: shadows off +4..8 fps, cloth simulation off
// +2..4); a hidden body is nearly free. Game-free: mod.cpp (perfBodies) makes the game's calls.
//
//   Policy   per player: reduced (no shadow, no cloth simulation) beyond the detail distance, hidden beyond the hide
//            distance, each with a margin, so a body near a boundary does not flip every tick;
//   drawn    the players drawn here as bodies for the HUD's far players: a body hidden for its distance is a far player;
//   look     what one body is to look like, the lab's switches (puppet.cargo perfshadow, perfpbd) first;
//   Applied  per entity: which of the game's calls to make so it looks so, and the game's own cloth value, put back
//            when the body comes near again.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>
namespace mod::bodylod {
constexpr unsigned DetailMetres=30;   // config body_detail_metres: shadow and cloth off beyond it (0: never)
constexpr unsigned HideMetres=0;      // config body_hide_metres: hidden beyond it (0: never; the HUD markers show far players)
constexpr unsigned MetresMax=10000;   // the most either may be
constexpr double KeepMetres=5;        // what changed beyond a distance changes back only this far inside it (at most half of it)
struct Settings {double detailMetres=DetailMetres,hideMetres=HideMetres;};
// Beyond `metres` (0: never): true beyond it; once true, false again only within metres less the margin.
bool beyond(bool was,double distance,double metres);

// One body as planned this tick: its player's key and where it is to stand (nothing: the plan hides it already).
struct Body {uint64_t key=0;std::optional<std::array<double,3>> at;};
struct State {bool reduced=false,hidden=false;};
class Policy {
public:
    // Every body of this tick; a key not listed is forgotten. here: this porter, in the bodies' axes (nothing: not known,
    // and every state is kept as it was). A body the plan hides keeps its state.
    void tick(const std::vector<Body>&,const std::array<double,3>* here,const Settings&);
    State state(uint64_t key) const {auto it=states_.find(key);return it==states_.end()?State{}:it->second;}
    bool hidden(uint64_t key) const {return state(key).hidden;}
    size_t reduced() const;
    size_t hiddenCount() const;
private:
    std::map<uint64_t,State> states_;
};
// The players drawn here as bodies (hudmarkers.h: everybody else on the map is a far player and gets a marker): each
// (player, body entity) that has a body, less those hidden for their distance.
std::set<uint64_t> drawn(const std::vector<std::pair<uint64_t,uint64_t>>& bodies,const Policy&);

// What one body is to look like. cloth: the multiplier of its cloth simulation's distance (nothing: the game's own).
struct Look {bool shadow=true;std::optional<float> cloth;};
constexpr float ClothOff=0;   // a reduced body's cloth multiplier
// The lab's switches (9 and 999: not set; perfshadow 0|1, perfpbd percent) override the policy for experiments, each
// for the bodies beyond the lab's perffar distance (labDistant) and the game's own for the rest.
struct Lab {uint8_t shadow=9;uint32_t cloth=999;};
Look look(const State&,const Lab&,bool labDistant);

// One entity of the mod's bodies. A fresh one is as the game made it (shadow on, its own cloth value).
struct Change {std::optional<bool> shadow;std::optional<float> cloth;};   // the calls to make now
class Applied {
public:
    // clothNow: the cloth value read from the body's model this tick (nothing: unreadable, and cloth is left as it is
    // this tick). The game's own is the first sane read (finite, 0..100) before anything was set; until there is one,
    // cloth is left alone (it could not be put back).
    Change step(const Look&,std::optional<float> clothNow);
    std::optional<float> ownCloth() const {return own_;}
private:
    bool shadowOff_=false;std::optional<float> own_,set_;
};
}
