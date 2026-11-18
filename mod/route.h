#pragma once
// Route challenges (docs/ROUTES.md): a recorded path with a corridor around it, rules, a string to share it, and the
// checker that watches a run. Pure: no game, no files, no clock; the mod, the launcher and the server use the same code.
//
// Axes: the mod's presence axes (core/core.h State): x and z on the ground, y up. The corridor is flat: distances are
// measured on the ground only, so a corridor over a mountain is as wide as on the plain.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace mod::route {
constexpr unsigned WidthMin=2,WidthMax=100,RadiusMin=2,RadiusMax=50,GraceMax=60,CasesMax=40;
constexpr unsigned TimeLimitMax=43200;                  // 12 h
constexpr size_t PointsMax=300,NameMax=32,AuthorMax=24;
constexpr size_t BytesMax=2048;                         // a challenge before base32
constexpr double ReachMax=100000;                       // metres from the start point
constexpr size_t TrackPointsMax=1024,TrackBytesMax=8192;
constexpr uint64_t TrackEveryMs=2000;                   // a track point while running
constexpr double FinishSlack=2;                        // metres: how far short of the finish zone's edge progress may be
constexpr double Lookahead=60;                          // metres of the route searched beyond what was reached
constexpr double JumpMetres=25;constexpr uint64_t JumpMs=500;   // a jump this far this fast is a teleport
constexpr double TeleportSpeed=60;                      // m/s on the ground, measured over at least SpeedWindowMs
constexpr double FootSpeed=15;constexpr uint64_t FootWindowMs=2000;   // "no vehicles": faster over 2 s is not on foot
constexpr uint64_t SpeedWindowMs=1000;
// verify: the claimed time against the track's own. Its points are 2 s apart and quantised to 0.5 m, so the replay may
// see the start or the finish one point late (a point a few centimetres outside a zone lands on its edge).
constexpr uint64_t ClaimSlackMs=1000+TrackEveryMs;

struct Point { double x=0,y=0,z=0; };                   // y up
struct Rules {
    unsigned width=10,startRadius=10,finishRadius=10,grace=5,minCases=0,timeLimit=0;   // m, m, m, s, cases, s (0: none)
    bool noVehicles=false;
    bool operator==(const Rules& o) const {return width==o.width&&startRadius==o.startRadius&&finishRadius==o.finishRadius&&grace==o.grace&&minCases==o.minCases&&timeLimit==o.timeLimit&&noVehicles==o.noVehicles;}
};
struct Route {
    std::string name,author;Rules rules;std::vector<Point> points;   // points: the start first, the finish last
};
// The checks of one route's fields (caps, ranges, text); "" when it may be encoded, else why not.
std::string invalid(const Route&);
bool nameText(const std::string&,size_t most,bool empty);   // printable ASCII, no leading or trailing space

// ---- Geometry (flat) ---------------------------------------------------------------------------------------------
double ground(const Point& a,const Point& b);           // distance on the ground
struct Nearest { double distance=1e300,along=0;size_t segment=0;Point at; };   // along: arc length of the nearest point
// The nearest point of the route's line to p, on the ground; only segments whose start lies within [from,to] metres of
// arc length count (the whole line by default).
Nearest nearest(const std::vector<Point>& line,const Point& p,double from=0,double to=1e300);
double length(const std::vector<Point>&);               // on the ground
bool inCorridor(const Route&,const Point&);             // within width/2 of the line, or in a zone
// Douglas-Peucker on the ground (heights kept): every dropped point is within `tolerance` of the kept line.
std::vector<Point> simplify(const std::vector<Point>&,double tolerance);
// Simplified until it has at most `most` points: the tolerance starts at `tolerance` and doubles. Repeated points
// (closer than 0.25 m on the ground and 0.5 m up) are dropped first.
std::vector<Point> fit(const std::vector<Point>&,size_t most=PointsMax,double tolerance=0.5);

// ---- The challenge string ----------------------------------------------------------------------------------------
std::string base32(const std::vector<uint8_t>&);         // Crockford, groups of five joined by '-'
std::optional<std::vector<uint8_t>> unbase32(const std::string&);   // case-insensitive, I/L=1, O=0, '-' and ' ' skipped
uint32_t crc32(const uint8_t*,size_t);
// "DSR1-...". Points are quantised (0.25 m on the ground, 0.5 m up, the start to 0.1 m); throws std::invalid_argument
// with invalid()'s reason, or when the encoded route is over BytesMax.
std::string encode(const Route&);
// The route of a string, or nullopt with `why` set ("This challenge is incomplete. Ask for the whole string again."
// for a damaged one). Quantised as encoded: decode(encode(r)) is r as stored.
std::optional<Route> decode(const std::string&,std::string* why=nullptr);
uint32_t id(const std::string& encoded);                // the CRC-32 the string carries (0 for a bad string)
std::string rulesText(const Rules&);                    // "10 m wide, no vehicles, at least 3 cases, 20:00"
std::string clock(uint64_t ms);                         // "12:34.5", "1:02:03.4"

// ---- Recording ---------------------------------------------------------------------------------------------------
class Recorder {
public:
    static constexpr double Step=1.0;static constexpr uint64_t MinGapMs=250;static constexpr size_t Samples=20000;
    void start(){points_.clear();on_=true;thin_=false;skip_=false;}
    bool on() const {return on_;}
    void sample(const Point&,uint64_t ms);
    // Stops and returns the fitted points (fewer than two: nothing worth a route).
    std::vector<Point> stop();
    size_t samples() const {return points_.size();}
private:
    std::vector<Point> points_;uint64_t at_=0;bool on_=false,thin_=false,skip_=false;
};

// ---- A run -------------------------------------------------------------------------------------------------------
struct Sample {
    uint64_t ms=0;Point at;double yaw=0;   // yaw: the provider's (atan2 of forward x over forward z in these axes)
    bool vehicle=false,teleport=false;    // riding (presence Vehicle or Passenger); the provider's teleport flag
    std::optional<unsigned> cases;        // the carried cases; nullopt: unreadable
};
enum class Phase : uint8_t { Away, Ready, Running, Outside, Failed, Finished };
const char* phaseName(Phase);
enum class Kind : uint8_t { Away, Ready, Started, Leaving, Back, Failed, Finished, Wait };
struct Event { Kind kind;std::string text;uint64_t ms=0; };   // text: the info log's sentence
struct TrackPoint { uint64_t ms=0;Point at;bool vehicle=false; };   // ms: from the first point
class Checker {
public:
    explicit Checker(Route r);
    // One sample per tick, in time order. Returns what to tell the player (in order).
    std::vector<Event> feed(const Sample&);
    Phase phase() const {return phase_;}
    const Route& route() const {return route_;}
    uint64_t elapsed() const {return phase_==Phase::Running||phase_==Phase::Outside?last_-startMs_:result_;}
    uint64_t result() const {return result_;}           // finished: the time
    const std::string& reason() const {return reason_;} // failed: why
    double progress() const {return progress_;}         // metres along the route reached
    double outside() const {return outsideBy_;}         // metres outside the corridor at the last sample (0 inside)
    bool casesVerified() const {return casesVerified_;}
    const std::vector<TrackPoint>& track() const {return track_;}
    // Server replay (verify): no away notices, and each track point counts as it is.
    void quiet(bool q){quiet_=q;}
private:
    void fail(std::vector<Event>&,const std::string& why,uint64_t ms);
    void keep(const Sample&,bool force=false);
    std::string where(const Sample&) const;
    Route route_;double total_=0;Phase phase_=Phase::Away;
    std::optional<Sample> last_s_;uint64_t last_=0,startMs_=0,result_=0,outsideSince_=0,awayTold_=0,fewSince_=0;
    bool fewCases_=false,casesVerified_=true,quiet_=false,awayOnce_=false;
    std::optional<Sample> speedFrom_,footFrom_,readySample_;
    double progress_=0,outsideBy_=0;std::string reason_;
    std::vector<TrackPoint> track_;uint64_t trackAt_=0,trackBase_=0;
};

// ---- The track a result carries ----------------------------------------------------------------------------------
struct Track { uint32_t route=0;uint64_t claimedMs=0;std::vector<TrackPoint> points; };
// "DST1-..."; relative to `origin` (the route's start point); quantised 0.1 s, 0.5 m on the ground, 1 m up. Above
// TrackPointsMax every second point (but the first two and the last) is dropped until it fits.
std::string encodeTrack(const Track&,const Point& origin);
std::optional<Track> decodeTrack(const std::string&,const Point& origin,std::string* why=nullptr);
// The server's check of a finished run (docs/ROUTES.md, "The result and its track"): "" when it holds, else why not.
std::string verify(const Route&,const Track&);
}
