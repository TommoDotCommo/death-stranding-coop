#pragma once
// Timefall shared in group play (with `group_enemies`; the server's rule `enemies`: timefall is what wakes the BTs).
// Each player speaks only for the ground under him, as his own game decides it: where his game's weather map says
// rain (below weather::Wet) he says "rain" for that cell of a 500 m grid, again every ResendMs while it lasts. A cell
// is wet while someone said so within WetForMs. The others place the game's own timed rain influence (weather.h) at
// each wet cell within ReachMetres, renewed every RenewMs, and let it lapse when the cell dries. Clear is never forced
// (it would put out the game's own rain in BT areas and story scenes), and a cell where this game holds the mod's own
// rain is never reported (the map includes the influence: the rain would keep itself alive).
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace mod::groupweather {
constexpr double CellMetres=500,ReachMetres=1000;
constexpr uint64_t ResendMs=20000,WetForMs=60000,RenewMs=15000,HoldMs=45000;
constexpr float Radius=300.f,Lifetime=45.f;
constexpr size_t Slots=8;   // influences at once (each its own owner key)
// The cell of a game-axis position (x, y; z is up): a 52-bit key; and back to the cell's centre.
int64_t cellKey(double x,double y);
std::array<double,2> cellCentre(int64_t key);
// The wetness said for a condition: 1000 heavy rain (below -1.5), 500 rain (below Wet), 0 dry.
int64_t wetness(float condition);
struct Place {int64_t cell=0;std::array<double,3> at{};size_t slot=0;};
class Rain {
public:
    // What to say now (a cell and its wetness), given this player's place and his map's condition there.
    std::optional<std::pair<int64_t,int64_t>> report(uint64_t now,const std::array<double,3>& here,std::optional<float> condition);
    // A cell the others said is wet (from the server's events).
    void heard(int64_t cell,int64_t value,uint64_t now);
    // The influences to place or renew now (each at most every RenewMs), for wet cells within ReachMetres of `here`.
    std::vector<Place> apply(uint64_t now,const std::array<double,3>& here);
    bool holds(int64_t cell,uint64_t now) const;   // this game holds the mod's rain there
private:
    std::map<int64_t,uint64_t> wet_;               // cell -> last heard wet
    std::map<int64_t,uint64_t> said_;              // cell -> last said
    struct Held {int64_t cell=0;uint64_t placed=0;bool used=false;};std::array<Held,Slots> held_{};
};
}
