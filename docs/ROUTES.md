# Route challenges

A **route challenge** is a delivery route one player records and others run: a start, a path, a corridor of a chosen
width around it, a finish, and a few rules ("no vehicles", "carry at least 3 cases", "within 20 minutes"). The mod
checks that the runner stays inside the corridor and tells them in the game's info log. Challenges travel as a text
string (`DSR1-...`, pasted like an invite) or through the players' own server, which keeps a leaderboard per
challenge.

The owner's request (2026-11-18): "custom orders that players can somehow create and restrict the path of the
delivery to make it really challenging, like force a path 5m wide over a mountain".

Version 1 is a **mod-run challenge**, not one of the game's own orders: nothing is spawned, no terminal is involved,
nothing is written to the save. The porter carries whatever they choose to carry; the mod only watches. Native orders
are under [Later](#later).

State (2026-11-18): built and tested without the game (the core, the format, the mod in the fake world, the server's
API, the launcher's core). **Not run in a real game**; the launcher's Challenges tab has not been seen on a screen.

- [Words](#words)
- [The route](#the-route)
- [The challenge string (DSR1)](#the-challenge-string-dsr1)
- [What the mod checks](#what-the-mod-checks)
- [Recording a route](#recording-a-route)
- [Running a challenge](#running-a-challenge)
- [The result and its track (DST1)](#the-result-and-its-track-dst1)
- [On the server](#on-the-server)
- [The launcher](#the-launcher)
- [Tests](#tests)
- [Not done, and later](#later)

## Words

| Word | Meaning |
|---|---|
| route | the recorded path: a start point, up to 300 points, a finish point |
| corridor | everything within half the width of the route's line, measured flat on the ground (heights are ignored) |
| start zone, finish zone | circles around the first and the last point (radius 2..50 m); both count as inside the corridor |
| challenge | a route with its name, author and rules; one `DSR1-` string |
| run | one attempt at a challenge in one game |
| track | the coarse record of a run (a point every 2 s) that goes with a result to the server (`DST1-`) |
| grace | how long the porter may be outside the corridor before the run fails (0..60 s, default 5) |

## The route

- Points are in the mod's presence axes: `x` and `z` on the ground, `y` up (the game's `x`, `y`, `z` with `z` up,
  reordered; `core/core.h` `State`). The corridor is **two-dimensional**: a 5 m corridor over a mountain is 5 m
  wide seen from above, whatever the slope. Heights are kept for showing the route and for the server's sanity
  checks only. A route that passes over itself (a bridge over its own earlier leg) is one corridor there.
- The corridor is the union of the capsules around every segment, so a corner is rounded on its outside and a
  route may double back on itself.
- Recording keeps a sample when the porter has moved at least 1 m since the last one (and at most four a second);
  stopping simplifies them with Douglas-Peucker to at most 300 points (the tolerance starts at 0.5 m and doubles
  until the route fits, so a tight corridor follows the path closely) and quantises them (0.25 m on the ground,
  0.5 m up).

### Rules

| Rule | Range | Checked from |
|---|---|---|
| corridor width | 2..100 m (whole metres) | the porter's position |
| start radius, finish radius | 2..50 m | the porter's position |
| grace | 0..60 s | the clock |
| time limit | none, or 1 s..12 h | the clock, from leaving the start zone |
| no vehicles | yes/no | the presence flags `Vehicle` and `Passenger` (riding a bike or truck, or along on someone's) |
| carry at least N cases ("must carry") | 0..40 cases; 0: no carrying rule | the porter's carried cases (`carry_visual.h` `readCarried`, cargo slots only) |

**Not offered:** cargo damage and carried weight. The mod has no verified read of a case's damage (the record's
"probably durability" floats at `+30` in `game_offsets.md` were never confirmed) or of the porter's total weight; a
rule the mod cannot check is not offered rather than offered and ignored.

## The challenge string (DSR1)

`DSR1-` and Crockford base32 in groups of five separated by `-` (the invite alphabet: case-insensitive, `I` and `L`
read as `1`, `O` as `0`; dashes and spaces are ignored when read). The bytes, little-endian:

| Bytes | Field |
|---|---|
| 1 | format version, 1 |
| 1 | flags: bit 0 no vehicles; the other bits 0 |
| 1 | width, m (2..100) |
| 1 | start radius, m (2..50) |
| 1 | finish radius, m (2..50) |
| 1 | grace, s (0..60) |
| 1 | at least this many cases (0..40) |
| 2 | time limit, s; 0 none (at most 43200) |
| 1 + n | name: length 1..32, then printable ASCII (no leading or trailing space) |
| 1 + n | author: length 0..24, then printable ASCII |
| 3 x 4 | the start point: `x`, `y` (up), `z` as signed decimetres |
| 2 | number of points, 2..300 (the start is the first) |
| ... | for every further point: `dx`, `dz` (0.25 m), `dy` (0.5 m) from the previous point, each a zigzag varint (at most 4 bytes) |
| 4 | CRC-32 (IEEE) of everything before |

Caps: at most 2048 bytes before base32 (so at most about 3300 characters); every point within 100 km of the start;
no two points in the same place. Anything else, a wrong CRC, trailing bytes, an unknown flag or version, is refused
with a reason ("This challenge is incomplete. Ask for the whole string again." for a damaged string). The parser is
fuzzed (`route_test.cpp`: random strings, every truncation, every single-byte change of good strings, which the CRC
must catch) and every check has a mutant.

The mod config caps a config at 8192 bytes (4096 before this feature) so that a full route fits in `route`.

## What the mod checks

`mod/route.h` `Checker`, fed one sample per game tick (position, the time, the vehicle flags, the carried cases or
"unreadable", whether the provider marked a teleport). It reports:

| State | When |
|---|---|
| away | before the start; "go to the start: 340 m" once a minute while it is more than the start radius away |
| ready | inside the start zone (with the cases, when the rule asks for them); the clock has not started |
| running | the porter left the start zone into the corridor: the clock runs from that moment |
| outside | outside the corridor (and outside both zones); a notice with the seconds left and where the corridor is ("corridor: 12 m to your left") |
| failed | with a reason: left the corridor, teleported, rode a vehicle, too fast on foot, put the cargo down, out of time |
| finished | entered the finish zone having followed the route: the time |

- **Progress:** the run finishes only after following the route: the mod keeps how far along it the porter has
  come (the arc length of the nearest point, searched no more than 60 m ahead of what was reached), and the finish
  zone counts only when the rest of the route is shorter than the finish radius plus those 60 m. A route that ends
  near its start cannot be finished by stepping back into the start.
- **Teleports:** a jump of more than 25 m between two samples less than 0.5 s apart, a horizontal speed over 60 m/s
  measured over at least a second, or the provider's own teleport flag, fails the run as "teleported" (fast travel,
  a respawn, a cheat). With "no vehicles", more than 15 m/s on the ground over at least two seconds fails it as "too
  fast on foot".
- **Grace:** each time the porter leaves the corridor the grace starts again; back inside in time, the run goes on.
- **Cases:** with a carrying rule the start waits until the porter carries enough; during the run fewer than the
  rule for longer than the grace fails it. An unreadable carrier counts as neither (the result says
  `cases unverified`).
- **Left and right** in the guidance use the porter's facing (`yaw`, the provider's `atan2` of the forward vector in
  game axes with `z` up, so `x` east, `y` north). This handedness is not confirmed live.

## Recording a route

Key **K** (config `route_record_key`, a virtual-key code; 0 turns it off; F2 is refused). K is free in the game's
default bindings (`game_offsets.md`, "Keys": free are B H J K L M N O P U X Y; the mod already uses B for pings, J to
ride along, L for likes) and is no shared co-op key (STANDARD.md H: T V B H Tab [ ] F1 F2), so H2 holds. A press acts
once (H7). Press K: "Recording a route." in the info log; walk the route; press K again: the route is simplified and
written:

- a line `ROUTE recorded <points> points, <length> m: DSR1-...` in the game's log, and
- a file `routes\recorded-<UTC yyyymmdd-hhmmss>.txt` in the co-op data folder (`%LOCALAPPDATA%\DeathStrandingCoop`;
  in the fake world `$XDG_STATE_HOME/DeathStrandingCoop`), holding the string. It is recorded with the default
  rules (10 m wide, 10 m zones, 5 s grace, no limits) and the player's nickname as the author; the launcher changes
  them.

Recording needs the game provider (positions); it does not need a partner, a server or `notices` (without `notices`
the log lines are all there is). A recording longer than 20000 samples keeps every second sample from then on.

## Running a challenge

The launcher writes the challenge into the mod config as `"route":"DSR1-..."` (the string's own characters are
ASCII, so the config stays plain). The mod decodes it at start; a bad string refuses the config with the reason, as
any bad key does. Then, while the game runs:

- in the info log (with `notices`; always in the game's log as `ROUTE ...` lines): "Challenge <name>: go to the
  start (340 m).", "At the start of <name>. Go when ready.", "<name> started.", "Leaving the corridor: 5 s.
  Corridor: 12 m to your left.", "Back in the corridor.", "<name> failed: left the corridor.", "<name> finished in
  12:34.5.";
- the mod's status JSON has a `route` object (state, elapsed, progress, distance outside) that tests and the
  launcher read;
- at the end a line `ROUTE result <finished|failed> ...` with the track, and a file
  `routes\result-<UTC>-<crc>.json` beside the recordings: `{"route":"DSR1-...","outcome":"finished","ms":754500,"reason":"","cases_verified":true,"track":"DST1-..."}`.

One run per game start; to run again, start the game again from the launcher (a finished or failed run stays
finished or failed). **Map pins** along the route are not shown: the game's user markers are saved with the game and
the mod's marker code keeps two groups for the partner and pings ([Later](#later)).

## The result and its track (DST1)

`DST1-` and base32 like the challenge. Bytes: version 1; the claimed time in ms (u32); the number of points (u16,
1..1024); the route's CRC-32 (u32, the challenge it belongs to); then per point a varint of `dt` in 100 ms units
shifted left by one with bit 0 the vehicle flag, and `dx`, `dz` (0.5 m), `dy` (1 m) zigzag varints from the previous
point (the first from the route's start point); a CRC-32 at the end; at most 8192 bytes. The mod keeps a point every
2 s while the run is going, the start and the finish; above 1024 points every second point is dropped.

`route::verify(route, track)` replays the track through the same `Checker` with the server's view: the first point
must be in the start zone, every later point in the corridor or a zone (no grace: a 2 s point outside is refused),
no step faster than 60 m/s (15 m/s on foot with "no vehicles", where no point may have the vehicle flag), the last
point in the finish zone after the progress rule, and the claimed time within 2.5 s of the track's own duration and
within the time limit. This catches **obvious** cheats (a teleport, a car where it is forbidden, an edited time);
a modified mod can still make up a plausible track. Nothing more is claimed.

## On the server

`coop-world` keeps challenges with the world (saved in the world file, `"challenges"`). The launcher's API
(`world/api.h`, the same rules for signatures and identity as every launcher call):

| Call | What |
|---|---|
| `GET /coop/challenges` | every challenge: id, name, author name, author key (short), the string, the rules, the length, runs, and the ten best times (`board`: name, key short, ms, when) |
| `POST /coop/challenge-publish` `{"player":KEY,"route":"DSR1-...","name":"Ann"}` | publishes it under that player (signed under identity keys); `name` is how the board shows him |
| `POST /coop/challenge-result` `{"player":KEY,"challenge":ID,"track":"DST1-...","name":"Ann"}` | a finished run: verified with `route::verify` first; refused with the reason |
| `POST /coop/owner/challenge-remove` `{"challenge":ID}` | the owner removes one |

- **Who may publish:** the policy setting `challenges` = `everyone` (friends and public presets), `members`,
  `owner` (solo) or `nobody`. Under `identity none` the publisher is whoever the call names (friends only, as
  everywhere there).
- **Limits:** 200 challenges in a world, 20 per player, the same route (by its CRC) once; a board keeps each
  player's best time only and at most 100 players per challenge; a result body up to 16 KB.
- Results can only be sent for a challenge on that server; a string pasted from elsewhere is run and kept locally.

## The launcher

A **Challenges** tab between Saves and Settings (STANDARD.md A1 allows one game tab there; the Witness has its
Randomizers in the same place):

- **Recorded:** the routes in `routes\recorded-*.txt` with name, points and length; for the selected one: name,
  corridor width, start and finish radius, grace, time limit, no vehicles, at least N cases; **Copy string** puts the
  edited `DSR1-` string on the clipboard (the launcher re-encodes it: `route::encode`), **Publish** sends it to the
  group's server.
- **Run:** a paste box for a string (it says what it read: name, author, width, rules, length, or why not) or the
  server's list with author and best times; **Run this challenge** writes it into the config for the next start
  (`route`), **Clear** takes it out.
- **Results:** the `routes\result-*.json` files; **Send to server** for a finished run of a server's challenge.

The tab's code is `launcher/challenges.*` (game-free, tested in `challenges_test.cpp`) and the window in
`main.cpp`.

## Tests

| Test | What |
|---|---|
| `mod/route_test.cpp` (`make -C mod route-test`) | geometry (slopes, corners, zones, doubling back), the checker's states and notices, grace, time limit, vehicles, cases, teleports and speed, simplification, the format both ways, the parser fuzzed, the track and `verify`; 27 mutants (`MUTANT_ROUTE_*`) |
| `mod/test_route_world.py` | the real mod (`fake.so`) in the fake world: K records a scripted walk and writes the string and the file; a walk inside the corridor gives `ROUTE started` and `ROUTE finished` with the result file; one that leaves it gives `failed: left the corridor`; a jump gives `failed: teleported`; the mutant build `fake-route-corridor.so` (the corridor never checked) must fail it |
| `mod/unit.cpp` | the config keys `route` and `route_record_key` (a bad string, F2, the 8192-byte cap), with the mutant `CONFIG_ROUTE` |
| `world/challenges_test.cpp` | publish, list, the policy, signatures, limits, a verified result, a teleporting or slow-claimed result refused, the board keeping best times, save and restore; mutants `CHALLENGE_*` |
| `launcher/challenges_test.cpp` | listing recordings and results, editing a route's rules, the config key written and cleared |

## Later

- **Native game orders** (a real order with spawned cargo, handed in at a terminal) need research first, none of it
  done: how the order list is built (`orders.h` reads and completes standard orders but creates none), whether a new
  order can be added at run time without a save edit, how a cargo record is spawned at a terminal (the mod has never
  created a cargo record: `carry_visual.h` says why a record held by a body is real cargo), how the hand-in is
  recognised and rewarded, and what is written to the save. Then the corridor would be checked as now.
- **Damage and weight rules**, once a case's damage and the porter's load are read and confirmed live.
- **Map pins** for the start, the finish and the route: the user markers are saved with the game (`marker.h`), so
  they need a group of their own and the same fingerprint-and-remove care as the partner's pin; a route takes up to
  ten points per group.
- **A run again without restarting the game**, a countdown at the start, a ghost of the best run.
- **Challenges across linked servers.**
