# Design A in-process skeleton (private, game-free milestone)

This implements the adapter, not a playable mod. `mod.dll` exposes the C ABI in
`mod.h`: `mod_init(absolute_config_path)`, `mod_tick(seconds)`, `mod_shutdown`,
`mod_status` and `mod_last_error`. Init/shutdown must run outside the Windows
loader lock. Call tick on one game thread; it reads/applies providers there. A
cache under a mutex separates that thread from the network worker, which calls Core receive/tick. The game thread samples Core frame at the
actual apply time under the same mutex. Shutdown joins that worker before freeing sockets,
providers or the DLL. Call shutdown explicitly before unloading; no detach work,
file IO or threads are started by a custom DllMain. The Runtime has no static
destructor; explicit shutdown performs cleanup, and process exit uses OS reclamation. Never unload while a caller
is inside the API. `mod_last_error` returns a per-calling-thread copy valid until
its next call. One instance per loaded DLL; double init is an error.

The Winsock transport binds loopback only, nonblocking, with a 64-datagram budget
per iteration and a bounded 256-entry receive-delay queue. It filters both source
IP and port, rejects truncated/oversized packets and counts drops. This research
skeleton only permits numeric `127.0.0.1`; remote machines/authentication are later
work. Ports are `ds::portForSlot(slot) = 49000 + slot` (0..15), independently for
local and peer; no port is configured separately. No game slot allocation or
research gate is enabled. Slot numbers here select **standalone probe ports**.
Socket, bind, thread and log errors are explicit failures, never dummy success.

`SyntheticProvider` uses a deterministic 6-second horizontal figure eight, with
analytic velocity, yaw and three animation IDs/phases. All processes on one host
use the same boot-relative monotonic epoch (QueryPerformanceCounter on Windows). `GameProvider` reads the local player's pose through guarded copies (see "Pose exchange (read-only)"); it never writes game memory and creates no actor.
There is no game-thread hook. `game_offsets.md` describes the signature seam.

## Config, paths and identity

Place an exclusively copied (never linked) mod in
`<instance>/redirect-data/mod/mod.dll`, config at `mod.json` beside it:

```json
{"role":"host","peer":"127.0.0.1","slot":0,"peer_slot":1,
 "nickname":"porter0","provider":"synthetic","session":8127391,
 "logging":true,"required":true,"impairment":false,"fault":"none"}
```

The client swaps role and slots and uses the same freshly generated nonzero
session. A cryptographic OS nonce is generated independently per process. The
nickname is local configuration only; protocol 1 has no nickname field. The strict
flat JSON schema rejects duplicate/unknown keys, invalid numbers/types, trailing
content, escapes/non-ASCII strings and configs over 4096 bytes. Config strings
have no path authority. Logging exclusively creates `mod-<pid>.log` beside config;
that directory must already exist, be private, and under redirect-data when
loaded by the proxy. Config/log ancestors reject symlinks/reparse points and files
with multiple links. Logs are flushed, never truncated. Setting logging false
retains stderr/debug diagnostics. Trusted quiescent instance trees are required;
concurrent rename/replace races are not supported.

### Group play: a config without a partner (built 2026-11-16, not run live)

A config **without `peer`** has no partner: the player is in a group that meets on the players' own server. Such a
config names the server instead:

```json
{"server":"100.90.1.2","slot":0,"nickname":"Porter","provider":"game","session":305419896,
 "required":true,"logging":true,"game_thread":true,"puppet_probe":true,"partner_tag":true,
 "others":15,"relay":"100.90.1.2:49100","relay_key":"w305419896","player_id":281474976710657,
 "online_server":"http://100.90.1.2:49100/w305419896/auth/ds","discovery_sha256":"<the pinned hash>"}
```

| Key | Rule |
|---|---|
| `peer` | absent: this is what makes it a group config. With `peer` everything is as before |
| `server` | required without `peer`: the server's numeric unicast IPv4 address (`127.0.0.1` for the player who runs it). Also allowed beside `peer`, for a partner and a server on a third PC |
| `relay`, `online_server` | their address must be loopback, the partner's (`peer`) or exactly `server`; anything else is refused when the config is read |
| `session` | the group's number: every player of a group has the same one (each pair's core session is derived from it and the two ids) |
| `player_id` | this player's number on the relay, 1 to 999999999999999 (the parser takes integers of at most 15 digits). It must be stable and say nothing about the player: the launcher makes it once from 49 random bits and keeps it in its settings; it is never the Steam id. On a server that takes player keys it is the number made from the player's key (`world/README.md`, "Identity"). The relay refuses a second game with an id that is in use |
| `relay_key` | the group's key, the one the server was started with; on a server that takes player keys, this player's relay token, which the launcher takes from the server just before the game starts (the same field, at most 32 letters and digits) |
| `online_server` (group) | `http://SERVER:PORT/KEY/auth/ds`; on a server that takes player keys `http://SERVER:PORT/KEY/t/TICKET/auth/ds`, with the sign-in ticket the launcher took (good for 30 minutes until the game signs in). The mod passes the address on as it is |
| `strand_notices`, `strand_token` | with `online_server`: lines in the game's info log about this player's builds on the server, read with the notice token the launcher took ("Strand notices", below); the launcher writes both |
| `build_received` | with `online_server`: other players' structures that server hands the game are built at once, without the game's range and placement checks ("Received structures built", below); off by default, not run live in the mod |
| `relay_mode` | `"pairs"` (default) or `"broadcast"`: how other players travel through the relay ("Other players", below); requires `relay`. Every player of a group uses the same mode: the server never puts players of different modes near each other |
| `show_others` | `"everyone"` (default), `"friends"` or `"nobody"`: who of the players near this player the server shows him (broadcast only, sent in every HELLO; `world/README.md`, "How many players one player sees"). The server's own record of his choice, set from the launcher, comes first; `friends` needs a server with player keys. Requires `relay_mode` `"broadcast"`. A server from before this refuses a HELLO with anything but everyone |
| `slot` | still required (0..15), but no port is bound from it |
| refused without `peer` | `role` other than `none`, `peer_slot`, `bind`, `partner` other than `off`, `join_at_host` and every `share_*` setting that is on: they need the partner and would silently do nothing |

What the mod does differently: the direct socket is never opened (the log says `NET no partner (group play): the
direct socket is not opened`), and nothing the two-player core asks to send leaves the process. Other players come
through the relay's own connected socket, as in "Other players" below. The other `partner_*` options are accepted
and have nobody to act on, except `partner_tag`.

The proxy reads the same config and refuses to start the mod when `server` is not an address its network policy
reaches (loopback or one listed in `COOP_NETWORK_PEERS`): the config never widens that list
(`redirect/README.md`). So the modded game still talks to loopback and listed addresses only.

Tests: the rules in `unit.cpp` with two mutants (`CONFIG_GROUP`: the partner-only keys accepted and no server
needed; `CONFIG_SERVER_ANY`: any address once a server is listed); `test_probe.py` runs the mod with a group config
on the test transport and counts what reaches the partner's side: nothing, and the core's handshake datagrams from
the mutant build `fake-group-sends.so`. Not run: the Windows socket path in a game (no socket, no bind, the relay alone).

The optional proxy bootstrap is reached on the first D3DCreateBlob/D3DReflect
call, after the Windows loader returns. All 29 names/ordinals are preserved; the
other 27 exports stay forwarded. It reads this config; missing config skips with
an audit record. Malformed config fails closed (exit 125). With required=true,
missing DLL, missing exports or an init failure (a hash mismatch, a signature scan failure, a socket error) exits 125
with `REQUIRED mod failed to load/start; provider or hook unavailable`. Optional
failure is audited. Synthetic mode is for probes only. Game mode cannot claim a
hook exists. The first compiler call must not be made under a loader lock.

The bootstrap has one narrowly scoped, trusted-library loader exception:
`redirect-data/mod/mod.dll`, whose CRT startup must perform no mod IO or thread
creation. Socket/entropy/timer DLLs are preloaded through proxy static imports; C++ and
winpthread are linked statically into the mod. The mod's IAT is patched before
mod_init. The general dynamic-module refusal and all file write/reparse/link
rules remain unchanged. This is not a sandbox for arbitrary third-party DLLs;
DllMain code precedes IAT patching. Only deploy this reviewed artifact. The
original private compiler DLL is also loaded from beside the proxy, and patched
before invoking its export. Driver/native API coverage limits from the redirect
README still apply. No real-game compatibility or loader-lock proof is claimed.

`identity.h` pins a SHA-256/128 manifest over core and adapter production sources.
`build_id.py` checks it, never silently updates it. On deliberate source changes,
review then run `python3 games/death-stranding/mod/build_id.py --write`. The adapter
puts this complete hash on wire and verifies it before translating to the core's
unchanged hash. Checksum failures are preserved. Compiler flags are not in this
source identity: mutant builds deliberately retain identity so only their intended
protection differs. Never deploy mutants as production.

## Pose exchange (read-only)

Real poses over the existing presence transport. **Read-only: no game writes, no actor, no puppet, no hook, nothing beyond
127.0.0.1.** `applyRemote` only logs the interpolated remote pose. This is research evidence code: the object lifetime across
reload/death, vehicles, the slow selection path and the visual cutscene gate are still UNKNOWN (see `game_offsets.md`).

Pieces (all game-free tested with fake memory in `pose_test.cpp`, mutants in `test_pose_mutants.py`):

- `PoseReader` (`pose.h/.cpp`) is built on the discovery `Sampler`: the same hash-gated signature scan, guarded double-read
  copies and chain recheck, and the same `Sampler::selectPlayer` H1 fast path as the discovery log. It returns
  `{available, reason, position[3] f64, rotation[9] f32, coherent}` for: manager global -> selection record (index, mode 1,
  kind 2, null pointer) -> `player = *(manager + 0x48 + 8*index)` -> local-human bytes `player+0x78/0x79 == 01 00` ->
  `entity = *(player + 0x70)` -> position (3 x float64 at `entity+0xc8`) and rotation (3 x 3 float32 row-major at
  `entity+0xe0`). `available` is false, with a reason, for: `manager null` (main menu), `manager unreadable`,
  `slow selection path`, `bad index`, `selection unreadable`, `player null`, `player slot unreadable`, `player unreadable`,
  `not local human`, `entity null`, `pose unreadable`, `non-finite pose`, `incoherent chain` (a pointer changed during the read).
  These are guarded copies off the game thread, not typed game calls, and not an atomic snapshot.
- `GameProvider::readLocal` converts to `ds::State`: `position = (game x, game z, game y)` (the adapter has Y up, the game z up);
  yaw from the forward row (`rotation[3..5]` = m10, m11, m12, called "row 2" in `game_offsets.md`), `phase = 0`, `flags = 0`.
  **Yaw convention** (the same as `SyntheticProvider`, which uses `atan2(vx, vz)`): `yaw = atan2(game fx, game fy)`; yaw 0 faces
  game +y, +90 degrees faces game +x. For a map heading `h = atan2(dy, dx)` this is `yaw = 90 - h`. The core interpolates yaw along
  the shortest arc, so the convention only needs to be consistent, and it makes yaw agree with the velocity direction. Live numbers:
  forward (-0.131, -0.991, 0) gives -172.5 degrees; the measured walk heading of -97.3 degrees gives -172.7 degrees (tested within 0.5).
  Facing straight up or down keeps the previous yaw.
- Velocity is the finite difference of the last two coherent samples, clamped to 20 m/s. It is zero after a gap over 500 ms or a jump
  over 5 m; a jump also sets the core's `Teleport` flag (kept until a presence packet carrying it has been sent, because the core
  drops a fast move without it as impossible motion).
- `isInCutsceneOrLoading()` is true exactly while `readLocal` is unavailable (menu, loading, death). **The visual cutscene gate is
  UNKNOWN**: a cutscene with a valid pose is not gated.
- `self_tick` (bool, default false, **required true with provider "game"**): a thread calls the same tick path as `mod_tick` at a fixed
  20 Hz (absolute deadlines; a stall costs ticks, they are never replayed in a burst) and is joined at shutdown. This is a stand-in until
  a game-thread hook exists. With `self_tick`, an external `mod_tick` is a validated no-op. `mod_status` reports `self_tick.ticks`
  and `pose.available`/`pose.reason`.
- Startup with provider "game": an unavailable pose at init (the main menu) is normal; presence stays gated until a pose is available.
  Startup is still refused for real failures: config errors, a missing or wrong executable pin (`discovery_sha256` is mandatory),
  hash mismatch, signature scan failure, an existing presence log, socket errors, and any non-Windows host library.
- The presence log `redirect-data/mod/presence-<pid>.jsonl` is created exclusively next to the mod log, flushed per line, at most
  4 lines a second, bounded by `presence_log_max_mib` (default 8, hard max 32; the discovery `Budget`). Hitting the cap or a write
  failure stops the log with a message and not the mod. Each line: `t_us` (the monotonic clock all processes on a host share),
  `local` (`available`; if available adapter-axis `x`, `y` up, `z`, `yaw_deg`, `speed`; otherwise `reason`) and `remote`
  (`visible`, `stale`, `snapped`, `delay_ms`, `late`; if visible `x`, `y`, `z`, `yaw_deg` of the interpolated render, `delay_ms` behind real time: 120 ms on a quick link, more on a slow one; `late` = shown past the newest sample).

Config for slot 0 (host) and slot 1 (client); ports are `49000 + slot`; use one freshly generated nonzero `session` for both;
copy `discovery_sha256` from the pinned executable entry in `profile.json`:

```json
{"role":"host","peer":"127.0.0.1","slot":0,"peer_slot":1,"nickname":"porter0","provider":"game",
 "session":8127391,"required":true,"self_tick":true,"presence_log_max_mib":8,
 "discovery_sha256":"9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b"}
```

```json
{"role":"client","peer":"127.0.0.1","slot":1,"peer_slot":0,"nickname":"porter1","provider":"game",
 "session":8127391,"required":true,"self_tick":true,"presence_log_max_mib":8,
 "discovery_sha256":"9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b"}
```

Both go to `instance/redirect-data/mod/mod.json` of their slot. `fault` is synthetic-only; `impairment` (a 15..65 ms presence delivery delay) also works in a real game, for lab runs
of two copies on one PC that stand in for an internet link. `link` (`"none"`, `"internet"`, `"poor"`; `link.h`) is the fuller stand-in: every
received packet of every kind is lost or delayed by the profile (`internet`: 2 %, 40..80 ms; `poor`: 12 %, 100..300 ms), the same way in every
run. The log says `LINK stand-in ...` at the start, `LINK ... lost N of M` every hundred losses, and `LINK partner shown N ms behind`
whenever the presentation delay has moved by more than 30 ms (that line also appears on a real slow link). When the config names no
profile, the environment variable `COOP_LINK` may name one, so a test run can put any setup on the stand-in link.

**Live exit test (the verifier).** Start both instances with these configs and load a game in each. The owner walks Sam in slot 0
(a straight line of at least 20 m, then a turn on the spot) while slot 1 stands still, then the reverse. Slot 1's
`presence-<pid>.jsonl` must show slot 0's track: for every line with `remote.visible` true, `stale` false and `snapped` false,
the remote `x`, `y`, `z` must be within 0.5 m and `yaw_deg` within 10 degrees of slot 0's `local` track at `t_us - 120000`
(interpolate between slot 0's lines; both files use the same clock). Then repeat in the other direction. At the main menu both
logs show `local.available:false, reason:"manager null"` and no presence is sent. Known limitation to look for: the core treats any
quantised height change as a jump (`snapped:true`, no interpolation), so walking on a slope will hold the older sample for up to one
send interval (about 0.2 m at a walk), which still passes the 0.5 m bound but is visible in the log.

Game-free checks: `make -C games/death-stranding/mod pose-test` (PoseReader reasons, conversion, velocity, gate, presence log, schedule
and 33 mutants), `test_probe.py` (the `game_provider_fake` case: two fake-memory game-provider mods, menu startup, gating, both
directions of the track, return to the menu). The fake-memory case lives in the test-only `fake.so`; it is not available in
production libraries and it is **not** a Windows run. `run_mod_probe.py` has no `game-provider-fake` mode: the Windows probe has no
game memory to read, and the fake image is compiled only into the host test library.

## Reproduce in the sandbox

From the repository root, with the existing compiler installations:

```sh
make -C games/death-stranding/mod test all
python3 -B -m unittest discover -s games/death-stranding/tests
make -C games/death-stranding/core test
make -C games/death-stranding/redirect test all
```

The MinGW **posix** compiler variant is required for std::thread/std::mutex:
`x86_64-w64-mingw32-g++-posix`. The system default win32-thread compiler does not
provide these C++17 types. No compiler or package is installed by these commands.
Ignored build/ contains production Windows DLL/probe, two Windows mutants,
host libraries/probe, and test-only fake-network libraries. No Windows binary is
executed by `make test`, Python discover or core test. Core test also invokes the
mod tests. Test-only exports exist only with MOD_FAKE_NETWORK; production has no
packet-injection/protection-disabling API.

The sandbox denies socket creation, so T0 runs two separately loaded adapters
with their actual network threads, C API, providers and core over an in-memory
datagram boundary. It exercises good pairing plus wrong build/protocol/session,
then requires the build-check mutant to fail the same refusal assertion and the
interpolation mutant to fail the same position bound. This is adapter/thread
coverage, not Winsock or endpoint-filter execution evidence. Unit tests cover
strict config, explicit unknown provider behavior and synthetic signature scans.
`make -C games/death-stranding/mod test-udp` is an additional host real-UDP test
for an environment that permits sockets; it is expected to fail in this sandbox.

## Ordered Windows verifier commands (no game required)

Cross-build in WSL, then use **native Windows Python in PowerShell** for the
probe runner so Popen retains each actual Windows process handle. WSL interop
process launching is refused by the runner. No command launches Steam or ds.exe.
Reserve loopback ports 49000/49001 exclusively for these short probes; stop on a
bind failure, never kill an unrelated process. Instance directories must be
trusted, private, disposable and outside protected roots. Fresh random probe,
DLL and config copies are exclusively staged below redirect-data/mod, preserving
logs; deployed mod files and linked game files are never overwritten.

The commands below assume the verifier sets `$repo` to this checkout's Windows
path (a drive path or accessible WSL UNC path), and has Python 3 available as
`py -3`. Build commands run in WSL from repository root; remaining commands run
in PowerShell. Use fresh evidence names; the native profile helper translates
only explicit /mnt/<drive> paths to native syntax, including the actual save
roots. Review its output before continuing.

1. Cross-build in WSL:

   ```sh
   make -C games/death-stranding/mod all
   make -C games/death-stranding/redirect all
   ```

2. In PowerShell, set the checkout path and capture the real save/config baseline
   (the profile is new evidence outside originals; snapshot reads originals):

> Note: `windows_profile.py`, `snapshot_saves.py` and the Windows runner `run_mod_probe.py` are workstation helpers (test-copy profile and a hash
> snapshot of the real save folders) kept outside this repository. Use your own equivalent: the point is that the
> real saves are hashed before and after and must not change.

   ```powershell
   $repo = (Get-Location).Path # run from the checkout, or set its Windows/UNC path
   py -3 -B "$repo/mod/windows_profile.py" --output <test dir>/mod-profile.json
   Get-Content <test dir>/mod-profile.json
   py -3 -B "$repo/snapshot_saves.py" --profile <test dir>/mod-profile.json --output <test dir>/mod-before.json
   ```

   Then run the production network path in two separate Windows processes:

   ```sh
   py -3 -B "$repo/mod/run_mod_probe.py" --profile <test dir>/mod-profile.json --instance0 <test dir>/s0 --instance1 <test dir>/s1 --dll "$repo/mod/build/mod.dll" --exe "$repo/mod/build/mod_probe.exe" --baseline <test dir>/mod-before.json --seconds 5 --repeat 5
   ```

   Expected exit 0; JSON `ok:true`, `saves_unchanged:true`, five good runs (two process reports per run), plus all three refusal cases.
   `good_summary` prints min/median/max across both peers and all repeats for
   p50/p95/p99/max error and RTT. Every repeat must pass, so the maximum p95 and
   p99 across N=5 must meet the bounds; a median pass cannot hide a bad run. Good case: handshake true, >20 accepted presences,
   >50 samples, 0<=RTT<250 ms, first valid pong before 1000 ms, p95<0.30 m, p99<0.45 m. Every report includes
   sent/received/dropped, accepted, wrong_accepted and refusal counters. Refusal
   cases have handshake false, accepted=0, wrong_accepted=0 on both sides and at
   least one explicit matching refusal. Only one partner transmits the bad field,
   so the refusing host need not receive a refusal acknowledgment from client.

3. Repeat with each deliberately broken DLL. **Each command must fail nonzero**:

   ```sh
   py -3 -B "$repo/mod/run_mod_probe.py" --profile <test dir>/mod-profile.json --instance0 <test dir>/s0 --instance1 <test dir>/s1 --dll "$repo/mod/build/mod-no-build.dll" --exe "$repo/mod/build/mod_probe.exe" --baseline <test dir>/mod-before.json --seconds 5 --repeat 5
   py -3 -B "$repo/mod/run_mod_probe.py" --profile <test dir>/mod-profile.json --instance0 <test dir>/s0 --instance1 <test dir>/s1 --dll "$repo/mod/build/mod-no-interpolation.dll" --exe "$repo/mod/build/mod_probe.exe" --baseline <test dir>/mod-before.json --seconds 5 --repeat 5
   ```

   Build mutant must fail `build: refusal assertion failed`; interpolation mutant
   must fail `good: handshake/error bounds failed`. A crash, bind or loader error
   is not a killed mutant. Both must reach their named assertion. Synthetic-only
   impairment adds a deterministic 15..65 ms presence delivery delay in the DLL.
   Compare the rendered puppet to analytic ground truth at now-120 ms, excluding
   first-second warmup, with unchanged Y. No interpolation mutant samples at now,
   causing approximately 1–1.8 m of error on this curve. The bounds below are unchanged. Inspect timing histograms as well as errors.

4. Recompare the original baseline after all processes exit and Steam settles:

   ```sh
   py -3 -B "$repo/snapshot_saves.py" --profile <test dir>/mod-profile.json --compare <test dir>/mod-before.json --settle-seconds 30
   ```

   Expected `ok:true` and no immediate/settled changes. 30 seconds is an example,
   not a proven Cloud interval. The runner compares before and in finally after
   success, assertion failure, timeout or startup failure. It kills only Popen
   children it created, using their retained handles on Windows, never names.
   The runner refuses WSL interop for Windows children rather than guessing their
   process identities.

## Timing design, diagnosis and regression evidence

The original adapter used `GetTickCount64`, relative `sleep_for(2ms)` in the
network worker, and relative `sleep_for(10ms)` in the probe. At the default
Windows timer resolution these can wake on roughly 15.6 ms boundaries; adding
work before each sleep also accumulates drift. More significantly, the worker
stamped the last game-thread cached pose with **send time**, hiding its age,
and the game thread applied a render computed at the previous network wake.
At this trajectory's maximum speed, two extra 15.6 ms ages can contribute
0.462 m. The fake transport's host scheduler did not reproduce those ages.
These are code-level defects; the supplied Windows runs alone cannot tell how
much each contributed to position error.

Run B's 208 samples prove that RTT **was** measured: the old sample collector
required `rttMs()>=0`. There is a concrete end-of-run race in `mod_probe` and the
runner: probes start sequentially; the earlier probe prints status, shuts down,
and sends BYE while the later one can still be ticking. Core receive(BYE) resets
RTT to -1, but the adapter's ever-paired flag and sample history survive. This
explains how a good connection can end with exactly that combination; a timeout
can also reset RTT. Probe metrics now retain the last valid measured `rtt_ms`,
while separate `current_rtt_ms` and `connected` report live core state. `handshake`
continues to mean ever paired. Core BYE/reset semantics are preserved. Error
samples after first-second warmup no longer depend on a successful ping, so an
RTT failure cannot silently remove bad samples. First-pong time proves early
measurement independently of final connection state.

Both DLL and probe now own scoped `timeBeginPeriod(1)` requests, paired with
`timeEndPeriod(1)` on shutdown and exception paths. No persistent settings change
is made. QPC is converted by quotient/remainder to microseconds without a
per-process epoch or multiplication overflow; wire time stays integer milliseconds.
Absolute 2 ms network and 10 ms probe deadlines skip missed periods rather than
catching up in bursts or accumulating drift. Probe dt is measured, not assumed.
Cached local poses carry their **sample** timestamp onto the wire; repeated/stale
samples are not retransmitted with fabricated new timestamps. Rendering happens
at actual game-thread apply time. Real game providers remain unavailable stubs.

The first ping is immediate after ACK, then retries every 100 ms until the first
valid pong; established sessions use 1000 ms. A bounded set of 16 outstanding
ping timestamps permits delayed pongs after a retry, consumes each timestamp once,
and preserves nonce/session/checksum/time validation and the 2000 ms RTT envelope.
Overwriting the single pending timestamp on every fast retry was caught by the
500-session lossy-link fuzzer during development and fixed. Early RTT is a tested
loopback requirement, not a guarantee under indefinite packet loss or process
suspension. The runner asserts the first valid pong is inside the first second.

Always-on `timing` JSON uses allocation-free fixed histograms (microseconds).
`bucket_upper_us` contains inclusive bounds; the final bucket is overflow.
Each histogram has count, mean, maximum and bucket counts:

- `network_tick`, `provider_tick`: actual worker and game/probe tick intervals.
- `presence_send`: presence-only send attempt intervals, excluding control traffic.
- `sample_age`: age of the cached local pose at send time.
- `receive_to_apply`: dequeue from UDP to first subsequent game-thread apply,
  including synthetic impairment. Coalesced accepted packets count as one batch,
  measured from the earliest unapplied packet. This does not measure time waiting
  inside the OS socket queue or assert a snapshot has reached its playout time.
- `delivery_lateness`: pending delivery beyond its scheduled due time, separating
  scheduler overshoot from the deliberate 15..65 ms impairment.

Clock fields report source, frequency, nominal quantum, minimum observed positive
microsecond step in 4096 startup reads (0 means none observed), and the 1000 us
wire quantum. Ping attempt/valid-pong counts and first/last times are milliseconds
relative to init; -1 means no event. Refusal runs normally have no pings/pongs.
Histogram totals and early RTT are exercised in the actual adapter tests.

### Why 120 ms delay and the unchanged bounds

The synthetic path is x=10 sin(wt), z=5 sin(2wt), w=2pi/6 s^-1. Maximum speed
is sqrt(2)*10*w = 14.81 m/s and maximum acceleration is at most
sqrt(10^2+20^2)*w^2 = 24.52 m/s². The 120 ms playout delay covers a normal
50 ms send interval plus up to 65 ms injected delivery jitter with 5 ms margin.
Pose-cache age (normally <=10 ms) can occasionally exhaust that margin; using
its true timestamp lets the existing bounded dead reckoning cover a small gap
rather than shifting the whole trajectory. 120 ms is the least delay. Since 2026-11-12 the core raises it when
samples arrive later than that covers (95th percentile of their lateness plus two send intervals, at most 500 ms,
moved at a tenth of real time; `core.h`), and every comparison here uses the delay of the moment. The core's
150 ms extrapolation cap, horizontal-only motion, freeze/staleness, teleport,
floor, speed, reorder, build and session protections remain active.

At a 50 ms sample spacing the linear interpolation curvature error is bounded
by a*h²/8, about 0.008 m. Residual 1 ms wire quantisation contributes at most
about 0.015 m of time-position error. The 0.30 m p95 and 0.45 m p99 bounds
correspond to about 20.3 ms and 30.4 ms of residual timing error at maximum speed,
well above normal interpolation/quantisation error, but below the old two-wake
age error. These are engineering acceptance budgets for this loopback impairment,
not universal guarantees for arbitrary network loss. A 150 ms extrapolation
can itself incur about a*t²/2 = 0.276 m curvature error; longer outages freeze,
not extrapolate without limit. No-interpolation instead advances the target by
120 ms, up to 1.78 m at maximum speed, so it must fail the same oracle.

`make -C games/death-stranding/mod timing-test` runs 100 fixed seeds using a
fake clock, independent provider/network wakes, random scheduling delays and
15..65 ms packet delay. Legacy mode reinstates 15.6 ms clock quantisation,
coarse relative sleeps, send-time stamping and cached rendering; it must fail
these same bounds. Fixed mode uses QPC-like time, the production deadline
arithmetic, true sample timestamps and apply-time rendering. Across these seeds
fixed worst p95/p99 were 0.0313/0.0344 m; legacy worst p95/p99 were
0.6765/0.7809 m. Dropping the first three pings still yields a valid pong by
319.5 ms; the old 1000 ms retry mutant fails the first-second oracle at about
3020 ms. These are host model results, **not** a Windows stability claim.
The core also tests a delayed pong crossing a retry and rejects its replay;
the pending-history mutant must fail that assertion. All original safety mutants
and both adapter mutants remain required. `make core test` and Python discover
include these timing regressions. The runner collects process/assertion failures,
emits all cases and diagnostics, checks original saves in finally, then exits
nonzero if any case or any repeat failed. Mutant failure messages appear on stderr
and in JSON `failures`; inspect the named oracle rather than any nonzero exit.

## Still unknown

Windows DLL loading, proxy bootstrap and required-mod process exit, Winsock
startup/binding, two-process scheduling, actual UDP mutant results and native Windows
cleanup need the verifier. The standalone probe loads mod.dll directly; it does
not prove proxy policy coverage. Static analysis now identifies six unique signature
candidates and layout hypotheses in `game_offsets.md`; live object semantics, hook
address/thread, actor lifetime, patch compatibility, Steam multi-client behavior and
save redirection in a real game remain unknown. No game was run and no real install,
save, Steam setting or runtime authorization was changed.

## Read-only live discovery (opt-in, runtime still unproven)

A separate discovery worker is selected only by `discovery:true` and
`provider:"game"`. This bypasses the GameProvider, presence and networking (and is exclusive with `self_tick`);
`mod_tick` does no sampling. No game callback, hook, function call, vtable call,
actor creation, game memory write or presence transmission is installed by this
component. The proxy's existing bootstrap and redirect IAT hooks are unchanged.
Init must still occur outside loader lock. Shutdown joins the worker; the caller
must not unload the DLL without shutdown.

Place the strict flat config at `instance/redirect-data/mod/mod.json` (the
proxy's reviewed config location). Add these fields to the ordinary instance
config, retaining role/peer/slot/peer_slot/nickname/session/required:

```json
{"role":"host","peer":"127.0.0.1","slot":0,"peer_slot":1,
 "nickname":"discovery","provider":"game","session":123,"required":true,
 "discovery":true,
 "discovery_sha256":"9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b",
 "discovery_hz":10,"discovery_max_mib":32,"discovery_animation_slots":0}
```

Copy `discovery_sha256` from the pinned executable entry in `profile.json`;
never re-pin automatically. The worker obtains the host executable's actual path
with GetModuleFileNameW, opens that file read-only and hashes it with BCrypt
SHA-256 **before any mapped-image read**. Both the supplied and computed hashes
must equal the reviewed pin. It scans executable sections of the mapped main PE,
requires one match per signature across all sections, validates masked RIP
operands and non-executable mapped-section targets, and requires both manager
references to agree. `generate_discovery_signatures.py` checks exact parity with
`find_signatures.py`; there is no historical-RVA fallback.

Every memory copy checks the full span with VirtualQuery and uses
ReadProcessMemory on this process, checking success and exact length. This is the
MinGW-compatible kernel-guarded equivalent to SEH around a dereference. A page
becoming inaccessible after VirtualQuery yields a failed copy, not a C++ load
through an invalid pointer. Two copies must agree; chains are rechecked after
sampling. **These checks cannot make an asynchronous snapshot atomic or prove
object type/lifetime.** An ABA reuse or stable freed allocation may be readable.
No log alone establishes safe production game reads.

The six signatures supply manager, save-dialog, baggage and sound globals.
The current evidence does **not** supply the manager-to-Player selection layout,
WorldNode/controller/animation/vehicle/BB owners, velocity virtual return storage,
or a cutscene function result. Default discovery logs their regions as null,
logs manager/global bytes, and does not invent links. To test an independently
reviewed pointer slot in this pinned module, optional decimal integer fields
`discovery_player_rva`, `discovery_worldnode_rva`, `discovery_controller_rva`,
`discovery_animation_rva`, `discovery_vehicle_rva`, `discovery_bb_rva`,
`discovery_velocity_rva`, and `discovery_angular_velocity_rva` identify **pointer
slots relative to the module base**, not object addresses or functions. Each
slot must lie in a non-executable section. The header records the supplied RVAs.
These are explicit experimental candidates, not validated owner paths. Do not
supply a guessed RVA. Once a Player candidate exists, +0x70 is followed and the
H3 flags, both 24-byte position windows, 60-byte transform and velocity object
slots are recorded. Other reviewed roots expose H6/H8/H10/H12 candidates.

Animation slots default to **zero** because readable pages do not prove valid
indices. Only set `discovery_animation_slots` after independent metadata bounds
are established; max 32. Records follow manager+0x58 and `records+0x20+24*i`.
H13 uses signed count +0x328, capped at 64, array +0x330, entry+0xc; invalid arrays
are explicit. H11 also follows the speculative Player+0x54d0/+0x50 candidate and logs the
record +0xc8 value; H14 captures global-object bytes only. No cargo ID semantics or
sound/visual gate result is claimed. Calling those exports is forbidden here.

Logs are exclusively created as `redirect-data/mod/discovery-PID.jsonl`, compact
schema-1 JSONL, flushed per record. A header contains actual build hash, base,
signature matches/targets, candidate root RVAs and limits. Samples have monotonic
`t_us`, `coherent`, and `regions`: each readable region contains address, raw
hex, float32/float64/u32 alternatives, and (for >=24 bytes) raw 16+8 and numeric
float32x4+float64 split interpretations. Non-finite numeric interpretations are
null; original bits remain in hex. Unreadable regions are null. Incoherent chain
samples are excluded by the analyser. Default 10 Hz, hard max 20 Hz (no catch-up
bursts); default 32 MiB, hard max 64 MiB. Hitting a limit or failing identity/
scan/read setup stops the worker with a loud debugger/stderr message and bounded
error record; it never falls through to a provider. A stopped worker does not
prove mod shutdown or game exit. A kill file `instance/discovery.stop` stops
sampling within one interval plus any in-progress guarded read/flush. Create it
from the verifier, outside the game; remove it only before a new reviewed run.

```sh
python3 -B games/death-stranding/mod/analyse_discovery.py /path/to/discovery-PID.jsonl \
  --actions open-menu walk run jump crouch enter-vehicle cutscene
```

Pass only actions actually observed. The report covers H1–H14 and lists exact
missing actions for each hypothesis plus remaining semantic evidence. It decodes
raw bytes independently, compares scalar candidates, motion, finite differences,
velocity vector axis permutations, speed, discontinuities, local flag stability,
rotation orthonormality, transform prefix/final word, move bounds, animation
variation, BB value/suppression and the narrow save predicate. Units remain
unknown until a measured 10 m walk. A zero-motion log cannot confirm movement,
even with action annotations; equal speeds with swapped axes are flagged.
All semantic verdicts stay unconfirmed: this mode supplies evidence for review,
not the independent object identity, typed lifetime and action-ground-truth
proof the hypotheses require. Sound/visual cutscene results stay unavailable.
Malformed, wrong-build, ambiguous-signature or error-containing logs are refused.

Game-free checks (no Windows execution):

```sh
make -C games/death-stranding/mod discovery-test test all
python3 -B -m unittest discover -s games/death-stranding/tests -v
```

The production sampler is tested against a fake mapped PE and memory reader,
including null/unmapped/torn reads, identity, zero/duplicate signatures, invalid
RIP targets, section/address bounds, save-array bounds, chain changes, log/rate
caps and stop files. Every such guard has an independently failing mutant oracle.
The analyser has static-log and swapped-axis mutants. These do not establish
Windows API behavior, filesystem isolation or game compatibility.


## Puppet call-point research

`python3 -B mod/find_message_calls.py "<game dir>/ds.exe"` (from the repository
root) inventories the pinned executable's PeekMessageA/GetMessageA import call
candidates and containing exception-directory ranges. It opens the file read-only
and refuses another hash. See `game_offsets.md` for independently disassembled
results and the application callback chain. A message-pump thread and a safe
world-mutation phase are still unverified; no puppet is created by this tool.


## Experimental game-thread dispatch and one-shot actor probe

Defaults retain read-only pose exchange. A game-provider config may select
`game_thread:true` instead of `self_tick:true` (the two are exclusive). It also
requires `COOP_THREAD_TRACE=1` in the disposable process environment and enforced
networking. The proxy waits for an empty PeekMessage return on its startup thread,
non-nested dispatch, and the reviewed main-loop stack. It calls the versioned
`mod_game_thread_tick` ABI at most 20 times a second. The mod independently checks
the real Windows stack, ABI fields, thread identity, caller and network attestation;
its lifecycle try-lock skips bootstrap/shutdown contention. External `mod_tick`
is a no-op in this mode. Hash-gated unique code signatures and reviewed branch
operands are verified before callbacks are accepted. Thread IDs are bound per run,
not taken from a previous observation.

`puppet_probe:true` additionally requires `game_thread:true` and `required:true`.
It validates the resource-getter/creator/transform/removal functions at init but
calls none until an operator arms this process. The native Sam resource is quarantined: arming now records
`resource_rejected` before creation. There is no config bypass. A future resource
backend needs an independently reviewed creation context. Use a disposable copy, normal loaded
gameplay, a stationary Sam and a closed pause menu. A valid pose is not a proven
visual-cutscene or loading gate. The experiment has no supported mid-game DLL
unload/reinitialization path: the proxy retains the module for process lifetime.
Stop the test process when finished.

Example additional config fields (retain role, ports, session and the pinned
`discovery_sha256` from the normal game config):

```json
{"self_tick":false,"game_thread":true,"puppet_probe":true,"required":true}
```

After fresh isolation/module/network probes for the matching production DLLs and
an authorized supervised launch, run from native Windows Python:

```sh
python mod/probe_puppet.py --log "<instance>/redirect-data/redirect-<pid>-<start>.log" --arm
```

The tool requires an enforced-network/armed-dispatch audit record and the explicit
probe config. It validates process creation time, waits for fresh stationary pose
samples, checks the entity-world manager, then atomically publishes a command
containing this process's PID and creation time beside `mod.json`. An old process's
command is ignored. Without `--arm` it only watches an already armed experiment.
It opens the process with query/read access only and independently reads the
actor/local transforms and model/mover pointers. Evidence output belongs outside
the repository.

The game-free backend exercises creation three metres ahead, one transform change,
and deferred removal with independent local-player and actor readbacks. These
operations have **not succeeded in the game**. The first supervised native call
crashed during component activation because the clone lacked DSPlayerController;
no actor returned and no transform setter ran. See `game_offsets.md`.

The reviewed creator ABI uses a non-null C string name, a 48-byte padded rotation,
null instigator/attack-event pointers and a zero-tagged 16-byte value. The transform
setter consumes 60 bytes. Removal takes a second boolean argument: false selects
queued removal. A removal request would not prove deallocation. Camera, input,
BB, cargo, generation ownership and a safe world-mutation phase remain unknown.

Failures stop further probe operations; unexpected engine/ABI failures may stop
the disposable process. Nothing in this mode talks to the online service. The
same source build must be deployed to both peers before another co-op launch.
Game-free coverage includes actual fake-memory C ABI calls and worker threads,
creation/movement readbacks, command stamping, shutdown exclusion, typed creator
and deferred-remove argument transfer (also run on Windows), plus mutants for
dispatch, world/local identity, one-shot behavior, arguments and readbacks.

## Read-only actor resource discovery

`python mod/find_actor_types.py <disposable-copy>/ds.exe` builds a hash-pinned
static catalog of resource types derived from `HumanoidResource`. It follows
bounded class metadata ancestry and the reviewed `lea rax,[rip+...]; ret` getter
form, then reports data-table/factory candidates. Matches are discovery leads;
no catalog entry approves an engine call. The pinned build exposes distinct
`HumanoidResource` and `DSPlayerEntityResource` tables.

For a loaded disposable process with enforced networking and `COOP_THREAD_TRACE=1`,
run from native Windows Python:

```sh
python mod/read_actor_resources.py --log "<copy>/redirect-data/redirect-<pid>-<start>.log" --max-mib 1024 --seconds 20
```

The reader checks process creation time, exact disposable image path, executable
hash, trace identity and loaded table/getter identity. It opens query/read handles
and scans only committed readable private pages, excluding guard pages. Scans
have byte/time/candidate limits. It rechecks candidate headers twice and reports
partial coverage and failed reads; a missing candidate does not prove absence.
Use `--start-address <next_address>` to continue a capped scan. Restart after an
enumeration timeout before the first readable region (`next_address:0x0`).
Output contains addresses and resource type names, never heap contents; keep
reports outside the repository. Concurrent stable reads cannot prove object
lifetime, ownership or a safe spawn contract. The reader never creates an actor
and never changes the native Sam-resource quarantine.

`python -m unittest test_actor_resources` (inside `mod`) exercises synthetic
metadata, ancestry, decoys, hash refusal, torn reads and scan bounds.
`python test_actor_resource_mutants.py` removes nine guards and requires the
unchanged suite to fail. Native Windows `test_actor_reader_windows.py` also
checks process identity, the actual OS-granted query/read rights, region layout
and an independently allocated fixture buffer.

## Many stand-in players for a relay (`coop-swarm`)

`swarm.cpp` (`build/coop-swarm`, `build/coop-swarm.exe`) is K stand-in players in one process for the server's relay:
each says HELLO, walks a circle around a given point and runs the real protocol core, with this mod's wire identity,
with every player the relay says is near (`--mode pairs`, the default), or runs the real broadcast client and posts
its name as the mod does (`--mode broadcast`, the mod's `relay_mode`). Each second it prints, in both modes alike, the
datagrams and bytes each stand-in received and sent on average and how many stand-in and player pairs it heard from,
so the two modes can be compared under the same crowd. With `--replay FILE [--with-animation]` the stand-ins walk their circle
at the speed of a recorded player and send his recorded animation samples (a presence log made with the lab option
`record_animation`, the file `coop-partner --replay` reads), each stand-in a share of the recording further on, so a
game shows many differently animated Sams instead of gliding ones; that has not been run against a game. The
stand-ins also read the animation frames a game sends them and count them. With `--names` (broadcast) it prints at the
end every player near a stand-in with the name he shows, read from his name state as `Crowd` reads it. Usage and what it prints:
[`world/README.md`](../world/README.md), "A swarm of stand-in players"; every option is in the comment at the top of
`swarm.cpp`. `world`'s `make test` runs it against the built server. The pure parts (`CircleWalker`,
`AnimationCursor`, `controlsOnly`, `fromWireLong` in `partner.h`) are pinned by `partner_test.cpp` with the mutants
CIRCLE_SPEED, CURSOR_ONCE, CONTROLS_ONLY and WIRE_LONG_ANY.

## A stand-in partner for one game copy (`coop-partner`)

Two copies of the game on one GPU cost each other about half their frame rate even with the GPU half idle, so playing
against a second copy on the same PC is not a fair test. `build/coop-partner.exe` stands in for the second copy: it
binds that copy's loopback slot and runs the real protocol core, so the game cannot tell it from a partner.

```
coop-partner --slot 1 --peer-slot 0 --role client --session <the game copy's mod.json session> --follow
coop-partner ... --replay presence-<pid>-<start>.jsonl [--where-recorded]
coop-partner ... --drop 10 125983117      # place one cargo item where the partner stands, 10 s after connecting
coop-partner ... --carry 125983117:1:2,7:7 [--carry-drop-after 30]   # the partner carries this (see below)
```

`--follow` keeps a place 3 m to the porter's right and 1 m behind, walking, running or sprinting to it with a person's
acceleration and facing where it goes; a gap over 60 m is a jump. `--replay` plays a recorded walk (the `local` rows of
a presence log) from beside the porter, or where it was recorded. It prints a status line a second and every cargo
event the game sends, so cargo put down in the game is seen arriving. The movement rules live in `partner.h` and are
pinned by `partner_test.cpp` and two mutants. Two partners run against each other on Linux (`build/coop-partner`, one as
slot 0 host replaying, one as slot 1 client following) as an end-to-end check of the protocol without any game.

## What the partner carries (`carry_visual`, off by default)

With `"carry_visual": true` in `mod.json` (requires the game provider), the mod reads what its porter carries four
times a second, read-only (`carry_visual.h`; the layout is in `game_offsets.md`, "Carried cargo"), and logs it on every
change: `CARRY porter carries 2 entries; 125983117 BackPack stack 0 level 0 x2; ...`. In partner mode (`peer` or `own`)
it sends that summary to the partner as CARGO events of type 6 (at most 16 entries: item, slot, stack, level, count),
once it has been the same for 0.5 s, at most once a second, and again after every reconnection. The partner's mod
keeps the latest complete summary and logs `CARRY partner shows ...` (or `shows nothing` after a put-down or a
disconnection). It never creates, moves or deletes cargo: a cargo record held by the partner body would be the game's
real cargo, counted as this porter's. Instead the partner body shows what the partner carries on the backpack as plain
cases (`cargo_stack.h`): one of the game's cargo case meshes per item (at most 12), each item's own carried-case model
and appearance (its definition found by the item id; the record's variant bit travels in the summary; a plain case for
anything else; packaged weapons and construction machines in the cases the game draws for them; body bags and carts never), with no record, carrier or collision, packed in two
columns by size on the copy of the backpack the body wears (or on the body without one), moved every tick, removed when the summary shrinks, the body hides or a game event starts, and
forgotten without a call when the world is replaced (`STACK N cases on the partner`, `STACK N cases removed`). Lab
commands in `puppet.ride`: `stack N` draws N cases whatever the partner carries, `stack auto` returns to the summary,
`stack off` draws none, `stack at OX OY OZ UX UY UZ` (offset and stacking direction) and `stack across AX AY AZ
COLUMNS` move them (anchor axes right, forward, up, in metres). Tests: `cargo_stack_test.cpp` (fake engine; mutants
EXTRA, LEAK and COLUMN) and the variant check in `carry_visual_test.cpp` (mutant VARIANT). `coop-partner --carry SPEC` plays the partner's side: SPEC is `hash[:slot[:count]],...` (slots 1 BackPack,
6 LeftHand, 7 RightHand; `none` for nothing); `--carry-drop-after S` puts it all down S seconds after connecting; what the
game's porter carries is printed as `CARRY the porter carries ...`. Tests: `carry_visual_test.cpp` (fake memory, two
copies through the real core with loss and an outage, seven mutants in `test_game_thread_mutants.py`) and
`test_partner_carry.py` (two stand-ins on loopback, one restarted).

## Session notices in the game's info log (`notices`, off by default)

With `"notices": true` (game provider and `game_thread`) the game's own info log, the message feed at the side of the
screen, tells the player what happens to the session (`notice.h`): "<name> joined your session." when the partner
connects and is in the world (a partner whose game is still loading is announced when it has loaded; it waits up
to 3 s for his name, else "Your partner"), "<name> left the session." when the link ends, and
once, after a minute in the world with nobody there since the mod started, "No partner yet. If this lasts, check the
firewall on both PCs." When the partner has been in a cutscene or private room for 2 s
(where his body is hidden) it says "<name> is away (cutscene or private room)." and "<name> is back." when he returns.
With `partner_likes`, a like he sends shows "<name> sent you a like." with the game's like icon. Nothing is shown outside the world (title, loading, game events);
what happened meanwhile is told afterwards, in order, 2.5 s apart; at most four notices wait, and one the HUD does
not take for 30 s is dropped. The name is cleaned like the name tag's.

The mod adds a finished text to the info-log queue with the game's own add function (0x3084d10), under the lock
the game takes, stamped with the UI clock, looking like the game's plain messages; it uses no table item and saves
nothing. The HUD object is found through the UI owner global and checked by its vtable on every call
(`game_offsets.md`, "Info log"). Log lines start with `NOTICE`. `puppet.ride` "notice" shows one test line without
the config. In group play (no `peer`) nobody is expected, so "No partner yet" is never said. Tests: `notice_test.cpp` (mutants BOUND, GROUP_ALONE, LEFT, ALONE_ONCE, NAME_WAIT, WORLD, GAP, AWAY_SETTLE, BACK_PAIRED, LIKE_ICON, JOIN_HOLD). The launcher's play
setup turns it on.

## Strand notices (`strand_notices`, off by default; live once, 2026-11-17)

The rule behind it: no surprises. A game builds in its own world whatever the server does, uploads the structure and
is answered as always; whether the server shares it is the server's policy and the player's choices
(`../world/README.md`, "Server policy"). With `"strand_notices": true`, `"strand_token": "..."` and `online_server`,
the game's info log says so, naming the server by its display name ("this server" without one):

| What became of an upload | The line |
|---|---|
| held: members only | "Your ladder is not shared on North: only members' builds are shared here." |
| held: nobody's are shared | "Your ladder is not shared on North: no builds are shared here." |
| held: his limit | "Your ladder is not shared on North: you have reached the limit of shared builds here." |
| kept: his choice (`offer_builds` no) | "Your ladder is kept for you on North (your choice)." |
| kept: the owner's review said no | "Your ladder is kept for you on North: the owner did not accept it." |
| kept: the server takes no offers | "Your ladder is kept for you on North: builds are not shared here." |
| waits for the owner | "Your ladder is waiting for the owner's review on North." |
| waits for his answer (`ask`) | "Your ladder is waiting for your answer on North: offer it in the launcher." |
| later shared (made a member, accepted, offered) | "Your ladder is now shared on North." |

Once per object and state; nothing for what is shared at once, for a withdrawal (a remnant), for his own no in the
launcher, or for what he had on the server before the game started. Several of one kind of news in one read are one
line ("3 of your builds are not shared on North: ..."). The lines go through the notices' queue: shown only in the
world, 2.5 s apart, at most four waiting (`notice.h`). They need no partner and turn on no partner notice.

How the mod learns it: it cannot see the game's own upload, so a thread of its own (never the game's) asks the server
every 5 s, `GET [/PREFIX]/coop/notices?since=VERSION` with the header `X-Coop-Token: TOKEN`, at the address and
prefix of `online_server` (`../world/README.md`, "What the game is told"). The first answer lists all of the player's
objects there and is taken as what was there before; after that the server lists only what changed since the version
of the last answer, or everything again after it started again, and the mod compares with what it knows
(`strand_notices.h`, `Tracker`). A read that fails is tried again after 10, 20, then every 30 s; a token the server
refuses (403) ends the reads until the game is started again from the launcher. Log lines start with `STRAND`;
`mod_status` has `"strand": {on, reads, answers, failures, told, known, refused, pending}`.

Why a token: the game signs in with its own HTTP calls and the mod holds no key (under identity keys only the
launcher has one). The launcher takes a notice token for the player just before the game starts (any identity: on a
server that takes keys by a signed call) and writes it into `mod.json` (`../launcher/README.md`); the token reads
that player's own objects built on that server and nothing else (no sign-in, no session, no API call, no relay). It
travels in a header, never in the address (which the server's `--trace` writes), and plain HTTP like the rest: use a
LAN or a VPN. Why opt-in in the mod and on in the launcher: a hand-written config does only what it says, as for
every feature; the launcher's player has just seen the server's rules, and a build of his that is not shared should
not be a surprise later.

Config: `strand_notices` needs `online_server` of the form `http://ADDRESS:PORT[/PREFIX][/k/NAME|/t/TICKET]/auth/ds`
and `strand_token` (1 to 32 letters and digits); `strand_token` alone is refused (mutant CONFIG_STRAND).

Tests: `strand_notices_test.cpp` (the answer read strictly; every line above and their length; what is told from a
run of answers: nothing first, once per object and state, nothing for shared, remnant or his own no, one line per
kind of news, "now shared"; when to read again; the reads on their thread against a scripted server, from each
version, stopping on a refusal; one HTTP read over loopback), mutants PARSE, FIRST, ONCE, SHARED, CHOICE, GROUP,
SINCE, STOP, BACKOFF (`test_game_thread_mutants.py strand`). `test_strand_notices.py` runs the real mod in the fake
world (`build/fake.so`, its info log recorded and shown in `mod_status` as `notices_shown`) in a group config
against the built `coop-world` named North with members' builds only, the test uploading as the player's game: nothing
for what was there before; a held ladder and a held cairn told once each in the words above; nothing for another
player's upload; after the owner made him a member and accepted the ladder with the server stopped and started again,
"Your ladder is now shared on North." and nothing again for the cairn; nothing for an upload shared at once; and
without `strand_notices` no line and no read of the server. Three mutant builds must fail it: `fake-strand-once.so`
(told again whenever listed), `fake-strand-partner.so` (lines only beside the partner's notices),
`fake-strand-off.so` (a game reads the server without `strand_notices`). Not run with a game: the info log is the one
the partner notices use live; whether the game's upload and the server's answer come before the next read (5 s) is
as designed, not measured.

## Shared MULEs, BTs, orders and roads (`share_enemies`, `share_orders`, `share_order_units`, `share_roads`; off by default)

Each game keeps simulating its own world; these settings share the outcome of what a player does in it. They need
`game_thread` and a partner; the launcher's play setup turns the first two on.

- `"share_enemies": true` (`enemies.h`): a MULE one player puts down (knocked out, fainted, asleep, dead) is put
  down in the partner's game with the game's own defeat function, and a camp that comes to know someone is there is
  alarmed in the partner's game. MULEs are matched by the UUID of their locator, which is the same in every game.
  A voidout's crater is shared too (`voidout.h`): the crater a voidout leaves in one world is added to the other's
  list of craters the way the game queues one, already activated, so the hole and its floating debris appear without
  the blast's effects; it is saved with the world like the game's own.
  Where MULEs are and who they chase is not shared. On joining, the MULEs that are down and the BTs that are
  destroyed in each player's world are applied in the other's too.
  The same setting shares BTs (`bts.h`): a BT area is active for both players through the shared rain
  (`share_environment`); a BT one player destroys is gone in the partner's game; when one player is pulled into a
  boss fight, it starts for a partner within 300 m (and for one who comes near while it runs); and when either
  player's fight ends with the area cleared, the partner's ends the same way. The two games' bosses share one health
  bar: the life a player takes off its own boss is taken off the partner's (in percent of full life, so different
  difficulties compare). The mod never takes a boss to zero itself: the game only lets a boss die on a real hit, so
  the player who lands the last hit sees the death and the partner's fight ends with the cleared area. Each game
  still runs its own BTs: where they drift, whom they notice and what the boss does is not shared. For more than
  two players, where one game owns the fight and the others visit it, see "Fights with visitors" below.
- `"share_roads": true` (`roads.h`; on in the launcher's setup): what a player delivers to an auto-paver is counted
  at the partner's paver as delivered by others, through the game's own routine for online contributions, and a road
  one player completes is completed for the partner, so both see it built. Pavers are matched by the game's own
  construction id. On joining (and after a save is loaded) each player's game also tells the other all its pavers
  hold and which roads are built, and each paver is topped up to the larger of the two: both worlds end up with the
  roads of either, and they stay in the save. The partner's cargo is not touched and the partner gets no likes.
- Structure upgrades need no setting (they go with shared structures): a structure that reaches a higher level in one
  game, the builder's own or the partner's copy of it, is raised to that level in the other (`structures.h`,
  `LevelSync`). The other game needs the game's own build time for the level-up (about a minute). Materials are
  shared as they are delivered (`MaterialSync`): what one player puts into a structure, his own or the partner's
  copy, counts in the other game as delivered by others.
- `"share_saved_structures": true` (`structures.h`; on in the launcher's setup): on joining, and after a save is
  loaded, the structures a player's save already has are built in the partner's world too, as the game's structures
  of other players; the partner skips those that already stand there. Without it only structures built while the
  two play together are shared.
- `"share_orders": true` (`orders.h`): a standard order (the repeatable deliveries and collections) one player takes
  on, completes or gives up does the same in the partner's game, through the game's own mission requests. Main and
  sub orders are never shared. Each game has its own cargo for the order; whoever hands it in completes it for both.
- `"share_order_units": true` (with `share_orders`; also on in the launcher's setup): a cargo unit one player hands
  in is delivered in the partner's game too and the partner's copy of that cargo is removed, so two players can
  each carry part of an order.

- `"share_order_load": true` (with `share_orders`, best with `share_order_units`; `orderload.h`; run live with two
  copies, 2026-11-16: the taking of an order, a case put down, picked up by the partner and handed in by him, the
  rest handed in by the taker, the order a success in both): one shared load per order. Only the player who took the order at the terminal keeps its cargo: the game
  that took the order on for the partner removes the cargo it made for it (cases lying in the world only, never one
  on the porter). A case of the order that a porter puts down goes to the partner as shared cargo with a tag naming
  the order, the unit and the item, and the partner's game makes it with the game's own cargo creator, filled as the
  mission system fills it, so the copy is the order's cargo there and either player can pick it up and hand it in.
  When one picks it up the other's copy goes, as with all shared cargo. Orders both games already had under way when
  the players met keep their two loads (the game refuses a second case of a unit it still holds). The copy gets the
  case's two condition values as they were when it was put down. A case that is handed in at a terminal also goes
  from the porter to the world; it is told apart by its unit, which is delivered by then, and is not sent (the first
  live hand-in made the delivered unit again as a new case at the partner's delivery terminal), and a game never
  makes a case of a unit it counts as delivered.

Log lines: `ENEMY ...`, `ORDER ...`, `ORDERLOAD ...`. Test commands in `puppet.cargo` (lab only): `mule <kind>`,
`order <id>`, `unit <id*10+n>`, `unitlose <id*10+n>`, `loadinfo <id>` (every cargo record of the order as the shared
load reads it), `loadsave <id*100+n>` and `loadmake 0` (remember a case, then make it again behind the porter with the
creator the shared load uses); in `puppet.ride`: `place X Y Z HEADING`.

## Delivery rewards and connection levels (`share_rewards`, with `share_orders`; off by default)

Run live: the read, the add routine alone, and two copies (one facility's point added in one game reached the other,
which added the shortfall with its own routine; both read the same total, nothing bounced). Adding points up to a
facility's cap left its level at 2: static reading (`game_offsets.md`, "Why points at the cap left the level at 2")
shows the add routine does recompute the level, but never above the facility's level cap, a fact the story
changes; the mod reads the right field and has nothing else to call. Both were then seen live: that facility's
level cap reads 2, and a facility below its cap (level 3, cap 5) went to level 4 with one point added and to level 5
at its points cap, in the add call itself, without a fault. A real delivery between two copies ran on 2026-11-16
(step 4 below): an order handed in in parts by both players at the Engineer's left that facility at 793 points in
both games (the game that had paid less added 86), and nothing bounced. Each game paid its own porter for the order:
183 likes to the one who handed in three cases, 97 to the one who handed in one.

`"share_rewards": true` (`rewards.h`; refused without `share_orders`). A facility's connection level (the stars of a
knot city or a prepper) is a points total and a level in the game's residents system. Each mod reads every
facility's record once a second. When a facility's points rise in its game (a delivery handed in there), it tells
the partner the new total (CARGO event type 23: name hash, points, level), and the partner's game is topped up to
that total with the game's own "add connection points" routine, the one a delivery's reward calls, which also runs
the level-ups and what they unlock. The level the game gives is the number of thresholds the points have reached,
held to the facility's level cap in that game: with a partner who is further in the story the points are equal and
the level can stay behind until the local story raises the cap and points are next added. Rules:

- totals travel, never amounts: a delivery both games rewarded is not counted twice;
- only up: a total at or below the local one adds nothing, a total of zero is refused on the wire, and nothing is
  added past the game's own cap for that facility;
- what is added here for the partner is not sent back; more than three facilities rising in one look is a loaded
  save and is learnt silently; a top-up waits until the porter is in the world and no game event runs;
- a record counts only when its resource is one the residents system lists, of the same class as the others, with
  the record's key as its name, and its numbers in the game's range;
- only rises seen while connected are sent: two saves that already differ are not levelled on joining.

Static research (`game_offsets.md`, "Delivery rewards") says the partner's game already runs the game's reward
routine when an order is completed by request; how much it gives there is unknown, and the top-up closes the gap
for connection points. The porter's likes and the five grade totals are read for the log only and are not shared.
Connection points are real, saved progress, which is why the setting is off unless asked for.

Log lines: `REWARD sharing on: add armed`, `REWARD facility <hash> rose here to <points> points (level <n>) ->
partner`, `REWARD partner's facility <hash> is at <points> points (level <n>): <k> added here, <a> -> <b> points,
level <x> -> <y>`, followed by `(level cap <H>, points alone give level <n>)` when the level stays behind the
partner's (or `nothing to add`, `unknown here`, `not added (add unavailable)`), `REWARD refused: malformed
news`. Test commands in `puppet.cargo` (lab only): `rewards 0` (every facility: row, name hash, points, level, floor,
cap, then `level cap <H>, points alone give level <n>`; the cap is `unknown` when the fact database cannot be
read), `rewards 1` (the porter's likes and grade totals), `reward_add <row*10000+points>` (adds 1..9999 points to
that row's facility with the game's routine and prints the same two numbers; needs `share_rewards`).

Live checklist (Episode 3 save in both copies, the crash recorder on, `share_orders`, `share_order_units` and
`share_rewards` on in both; steps 1 and 2 have been run as written, the others not):

1. Read only, one copy: `rewards 0`, then `rewards 1`. Expect `REWARD test: <n> facilities` with n above 0 and one
   row per facility; the facilities the save has delivered to show points above their floor and a level that
   matches the stars on the map; the others sit at their floor. Expect `REWARD test: porter readable
   likes <a> online <b> experience <five numbers> level <five numbers>`, with likes equal to the totals of the
   porter's status screen. `unreadable`, or levels that do not match the status screen, means the grade state's
   masks are not understood: stop and record the numbers. Zero facilities means the table layout is wrong.
2. The candidate call alone, one copy, standing in the open: `reward_add <row*10000+1>` on a facility below its cap.
   Expect `REWARD test: facility <hash> points <p> level <l> ...: 1 added, now points <p+1> level <l>` and no
   fault record. Then an amount that crosses the next threshold, on a row whose level is below its `level cap`
   (a row at its cap keeps its level whatever is added; the thresholds are not printed: add in steps until the
   level changes). Expect the level to rise by one and the game's own level-up notice; record what the
   game shows and whether anything is unlocked. A `STALL` line or a crash record means the routine
   is not safe to call from the mod's tick: stop, keep the record, leave `share_rewards` off.
3. Save and reload that copy: `rewards 0` shows the raised numbers (they are saved state).
4. What a request pays, two connected copies: `rewards 0` and `rewards 1` in both, then one player takes a standard
   order at a terminal, carries it and hands it in at the other facility's terminal. Expect on his side `ORDER
   success of <id> here -> partner` and `REWARD facility <hash> rose here to <P> points (level <L>) -> partner`; on
   the partner's side `ORDER partner: success of <id> requested`, then either `REWARD facility <hash> rose here ...`
   (the request paid there too) and `REWARD partner's facility <hash> is at <P> points ...: <k> added here` with k
   the shortfall, or `nothing to add`. Read `rewards 0` and `rewards 1` in both again: the facility's points and
   level are equal in both games; the difference of each porter's likes and grade totals before and after is the
   answer to "what is each player paid".
5. No echo and no double count: after step 4 neither log gains another `REWARD` line for that facility, and a
   second `rewards 0` ten seconds later shows the same points in both games.
6. The other direction: the partner hands in an order at a facility where he is ahead of the first player; the
   first player's game is topped up, the partner's is not lowered.
7. A loaded save: load an older checkpoint in one copy while connected. Expect no `REWARD facility ... -> partner`
   lines from the load, and no lowering in the partner's game.
8. After every run: the real saves are byte-identical.
## MULEs in the same place (`share_enemy_positions`; experiment, off by default)

Run live, two copies about 60 m from a camp: the guest's game moved its MULEs to the host's places (20 moves in 40 s);
one MULE stayed within 0.3 to 3.7 m of the host's. Seen and not solved: the guest's own MULE walks off again after
each move (its game gives it another goal), so a MULE that the two games send different ways is put back about every
2 s, which shows as a jump of 4 to 5 m. Position alone is not enough for those; their goal has to follow too. Outside
the range the two worlds differ freely (the same patrol group was 100 to 296 m apart).
What gives a MULE its goal, and the ways to make it follow the host's, are read from the code in `game_offsets.md`
("What gives a MULE its goal"): the best supported is the game's own forced destination for a walk in progress. None
is implemented; that section has the live checks that come first.

With `share_enemies` each game has the same MULEs (same locator UUIDs) but moves them itself, so in a fight the two
players see them in different places. `"share_enemy_positions": true` (needs `share_enemies` and a `role`;
`enemy_poses.h`) makes the host's game the one that says where they are:

- **Host**, five times a second: the standing MULEs within 80 m of either player (100 m for one it already sends, so
  one at the edge does not go in and out), the twelve nearest, each with its
  identity (52 bits of the locator UUID), position, heading and two flags (it has a body in the host's game; it moves
  faster than a person, i.e. it rides). They go in the core's unreliable `ENEMIES` datagram (`core/CORE.md`), not in
  the ordered event stream: a lost sample is replaced by the next one.
- **Guest**, five times a second, for each of its own MULEs the host named: nothing while it is within 0.75 m of the
  host's place; from 4 m, once that has lasted 0.3 s and not more often than every 2 s, the MULE actor's own teleport
  to the host's pose (`game_offsets.md`, "Moving a MULE": the call the game's script teleport makes; the game puts it
  on the navigation mesh); a MULE with no body in the guest's game (out of sight) is put there from 0.75 m, at most
  once a second. With `"enemy_position_pull": true` an error between 0.75 m and 4 m is taken away in steps of half
  a metre, at most two a second (each step is a short teleport; whether that looks right is not known, so it is a
  separate key).
- **Guest, walking instead of jumping** (`"enemy_position_goal": true`, needs `share_enemy_positions`; live
  2026-11-15): every look, up to six MULEs with a body that are more than 0.75 m off get the host's place as their
  walk's destination through the game's own forced destination (`mule_goal.h`: three doubles and one bit in the
  MULE's request block, a checked write and no call; the game's walk reads it, clears it and walks there). A MULE
  whose write was taken in the last 1.5 s is not stepped and is teleported only from 10 m. The write is refused,
  and the teleport rules above stay in force, for a MULE that is not walking (it stands in a wait step, or runs
  with another goal), whose AI is held still, that is nearer than 6 m to the guest's porter, or whose objects are
  not the ones read from the image; nothing is written while the last destination is still waiting to be read.
  Live, two copies 60 m from a camp for 40 s: 144 destinations taken, 4 teleports (20 in the same scenario without
  the key), the MULEs the host sent within 0.1 to 4.6 m of the host's, no fault. A second run, with a truck patrol
  in range and most MULEs standing: 170 taken, 983 refused as not walking, 59 teleports, within 1.3 m.
- **Riders** (2026-11-16): a MULE on a truck is a child entity of the truck (read live: the five MULEs of a truck
  patrol had a parent entity, the same one in each game, and no other MULE had any). The host flags such a MULE like
  a fast one and the guest moves neither a flagged MULE nor one that rides in its own game: the actor's teleport
  does not take a rider out of its seat. Truck patrols are therefore each game's own. `muleat` ends a rider's row
  with `rides`.
- **A teleport that does not move the MULE** for another reason is tried again later and later (4 s, 8 s, ... up to
  a minute) instead of every second or two; `ENEMYPOS guest: MULE <key> stayed where it was ...` says so. (This is
  how the riders were found: five MULEs 140 m from the host's, put there every second without effect.)
- **Never moved:** a MULE that is down, dead or removed in the guest's game; one within 3 m of the guest's porter
  (a fight hand to hand stays the guest's own, and its hits land where the guest sees them); a pose within 1.5 m of
  the guest's porter; a MULE that rides (the host's flag, a parent entity in the guest's game, or faster than 9 m/s there); anything when
  the host's newest sample is over a second old or the link is down; a MULE the host did not name; an error over
  150 m. At most two native calls per look, the largest errors first. The native call checks the actor each time
  (committed memory, the MULE actor's vtable with the teleport in its slot, the live byte) and the functions' code
  when it is first used (`ENEMYPOS unavailable: ...` when the image differs: then nothing is moved).

What this cannot do: the guest's MULEs still chase the guest's porter and the host's chase the host's, so a MULE that
runs at the guest is pulled back to where the host's game has it (walking somewhere else) until it is within 3 m of
him. MULEs that fight the partner's body need the AI to know that body (`game_offsets.md`, same section; research
only).

Log lines: `ENEMYPOS host: N MULEs near the players -> partner (sample S)` (the first sample and every 300th),
`ENEMYPOS guest: the host's first sample, ...`, `ENEMYPOS guest: MULE <key> teleported|stepped, it was E m from the
host's, to X,Y,Z (moved M, refused R)` (the first twenty and every fiftieth); with `enemy_position_goal`, `ENEMYGOAL guest: MULE <key>
sent walking, it was E m from the host's, to X,Y,Z (writes W, refused R)` (the first twenty and every 200th) and every
ten seconds `ENEMYGOAL guest: writes W refused R walking now N teleports T; <reason> <count>; ...`. Test commands in `puppet.cargo` (lab
only): `muleat <n> <stamp>` (read-only: the n nearest MULEs with key, id, LOD, state, position, heading, distance) and
`mulemove <cm> <stamp>` (the nearest standing MULE moved that many centimetres east with the same native call, then
read back: `MULEPOS test: key K lod L D m away, from ... asked ... now ... moved`). The stand-in partner sends a fixed
pose as a host would: `coop-partner --role host ... --mule KEY X Y Z HEADING`.

Two more test commands in `puppet.cargo` look at a MULE's movement goal (both run live 2026-11-15: the chain reads as
described in both games, and a forced destination is taken within half a second) (`mule_goal.h`; `game_offsets.md`, "What gives
a MULE its goal"). Lab only, no config key of their own; both answer `MULEGOAL test: off (share_enemy_positions is not
set)` without `share_enemy_positions`:

- `mulegoal <n> <stamp>` (read-only, guarded reads, nothing called): a header `MULEGOAL test: M MULEs with a body, N
  shown, porter at X,Y,Z`, then for each of the n nearest MULEs with a body (at most 12) one line
  `MULEGOAL test: key K id I life S at X,Y,Z dist D` followed by the chain fields, always in this order:
  `entity E component C componentvt V ai50 A ai80 A agree yes|no aivt V state B walk yes|no goal G dest9e0 X,Y,Z
  dest850 X,Y,Z d D dvt V index I request R forced X,Y,Z flags32 F flags8 F parent P [parentvt V]` (the parent is
  the entity the MULE hangs under: 0, or a rider's truck). Addresses and flags are hexadecimal
  without a prefix; a vtable is `image+OFFSET` when it lies in the game's image; a link that could not be read, and
  everything behind it, is the word `unreadable`. `component 0` = no AI component, `walk no goal 0` = the body-goal
  table holds no walk goal now.
- `mulegoto <cm> <stamp>` (a checked write of three doubles and one bit; no call): the nearest standing MULE with a
  body that walks (a walk goal whose destination is over 1 m from it), 6 m or more from the porter, gets a forced
  destination `cm` centimetres (at most 5000) east of its own destination. `mulegoto <cm+1000000> <stamp>` repeats the
  write every tick for 5 s; `mulegoto <cm+2000000> <stamp>` writes once for the nearest MULE that stands at its walk
  goal and watches it for 20 s (does a standing MULE take it when it next walks? not answered yet). Lines: `MULEGOAL test: forced before key K cm C repeat 0|1 to X,Y,Z at X,Y,Z dist D` +
  chain fields; `MULEGOAL test: forced after key K result written` (or `result REFUSED: reason`) + chain fields; then
  twice a second, until 3 s after the last write, `MULEGOAL test: forced watch key K phase writing|after writes W
  refusals R last ok|reason at X,Y,Z` + chain fields; at the end `MULEGOAL test: forced end key K writes W refusals R
  last ok|reason`. Nothing is written, and `MULEGOAL test: forced refused: reason` is logged, when the offset is over
  5000, an earlier `mulegoto` is still watched, or none of the 12 nearest standing MULEs passes every check: a
  `DSAIIndividualComponent` (image+0x3dca218), the AI the same by both of its pointers and a `DSAIIndividual`
  (image+0x3e11880), the AI running (its state byte 0), a walk goal with both destinations equal and finite, the request owner's vtable in the image's
  data, the request index 0 to 31, and plain committed read-write memory from the owner to the end of the request.
  Each repeated write reads and checks the chain again and stops (`last reason`) when an object changed.

Game-free test: `mod/mule_goal_test.cpp` (the chain on fake memory, every broken link, the write and its refusals, the wait for a destination not yet taken; 16
mutants).

Game-free tests: `core/tests/enemies.cpp` (the datagram; six guards with mutants; lossy-link fuzz),
`mod/enemy_poses_test.cpp` (selection, the correction policy, the actor checks on fake memory, and 120 fuzzed
sessions of two diverging worlds joined by two cores over a link with loss, repeats, reordering and an outage; 16
mutants, one per rule), the config rule in `unit.cpp` (`MUTANT_CONFIG_ENEMY_POSITIONS`).

### Live checklist (nothing of this has run)

A save with a MULE camp in reach (Episode 3). Positions are the game's axes. Assert from process state (the actor
list: id, LOD, position, life state of every MULE), not from the log alone.

1. **Read only, one copy.** Porter 60 m from a camp. `muleat 12`: twelve rows; positions equal to the actor list read
   from the process; headings change when a MULE turns. No native call is made.
2. **One teleport, one copy, out of sight.** Porter over 100 m from the camp (no MULE at LOD 2). `mulemove 500`:
   `... lod 0 ... moved`, `now` = `asked` within 0.5 m in x and y (the navigation mesh may shift it), and the actor
   list shows that MULE there 5 s later (the game did not put it back). Then walk to it: it stands there, alive,
   patrols on. Crash record empty.
3. **One teleport with a body.** Porter 30 to 40 m from a standing MULE, unseen (camp phase 0). `mulemove 500`: the
   same checks with `lod 2`; in a screenshot series the MULE is at the new place, upright, animated, and walks on
   (not frozen, not sunk, not in the air). Repeat with `mulemove 50` four times, 0.5 s apart: does it slide, stutter or
   restart its animation? This decides whether `enemy_position_pull` is usable.
4. **One teleport of an alerted MULE.** As 3 with the camp on alert (`mule 9`) and the MULE running: it keeps its
   state, target and weapon after the move.
5. **The whole path with the stand-in, one copy as guest.** Take a key and a position from `muleat`; start the
   stand-in as host with `--mule KEY X+10 Y Z 0`, porter 40 m from that MULE. Expected: `ENEMYPOS guest: the host's
   first sample, 1 MULEs`, one `teleported` line with an error near 10 m, then none while the MULE stays within 4 m
   of the pose and a new one, not sooner than 2 s after the last, each time it has walked 4 m away; the actor list has
   it within 4 m of the pose. Stop the stand-in: no further `ENEMYPOS guest` line after one second.
6. **The protections, same setup.** (a) Walk the porter to within 3 m of that MULE and change the stand-in's pose to
   20 m away: no move while he is within 3 m (distance asserted from process state); (b) put the MULE down
   (`mule 1`): no move; (c) a pose 1 m from the porter: no move; (d) mutant run: the same scene with a mod built
   with `-DMUTANT_ENEMYPOS_REACH` must move the MULE in (a).
7. **Two copies.** Host and guest 30 m apart, both 40 m from the same camp, `share_enemy_positions` in both.
   Expected in the host's log `ENEMYPOS host: N MULEs ...` with N > 0 and no `ENEMYPOS guest` line; in the guest's
   the first sample and moves. Every 2 s for 2 minutes compare the two actor lists: each MULE the host sends (standing
   in both, further than 3 m from the guest's porter) is within 4 m in both games (0.75 m with the pull, and for
   MULEs without a body in the guest's game); before this feature they were up to 17 m apart. Frame time and the
   `stages` timing of both copies unchanged (two reads of 150 actors five times a second).
8. **A fight, the guest far from the host.** Guest 60 m or more from the host (asserted from both processes'
   porter positions, further than any MULE weapon reaches), the host seen by the camp. The guest's MULEs must be
   where the host's are (they run at the host's place, where the guest sees the partner's body), and none of them is
   moved while within 3 m of the guest's porter. The guest knocks one out with his hands: it goes down in both games
   (`share_enemies`) and is not moved afterwards. Not expected, and to be recorded as observed: whether any MULE
   targets the guest (each game's AI knows only its own player).
9. **Link.** The walking regression's poor link on the guest (loss and delay): moves continue, no move uses a sample
   over a second old (log ages), and after the link is cut for 5 s nothing is moved until samples return.

## Story within a session (`share_story_orders`, `story_resync`; experiments, off by default)

Run live: `story_resync` in key mode between two copies (the guest's key, the host's checkpoint written, fetched,
put in place, loaded in 2 s, the guest standing where the host saved, the two seeing each other again), and steps 1
to 5 of the checklist below on one copy. Not run live: `auto` mode, `share_story_orders`, and a resync across an
episode change.

The host's save is the story, and a guest who joins through the launcher starts from the host's checkpoint. These two
settings are for what happens after that, while both play. Both need an explicit `role`; the guest never moves the
host's story. Research and addresses: `game_offsets.md`, "Saving and loading on request" and "Story orders".

**`"share_story_orders": true`** (with `share_orders`; `orders.h`): the host's main and sub orders travel like standard
orders: started, a unit handed in (with `share_order_units`), completed, and failed (state 8 only). The guest's game
gets the game's own mission request, the one its story scripts use, so its own story logic runs. The guest's story
orders are never sent; a story change the host receives is refused (`ORDER partner's ... does not apply here`).
Both sides log every change of a story order: `ORDER story <id hex> (detail 0|1) state A->B result C->D`.
What a main order's request sets off in the receiving game is unknown; a request that does not fit the guest's state
is refused and logged with the guest's state for that order: that line means the two stories differ, and
`story_resync` is the way back.

**`"story_resync": "key"` or `"auto"`** (`story.h`; needs the game provider and `game_thread`): the host's game writes a
checkpoint, the file goes to the guest over the mod's link, the guest's mod puts it in place as `manualsave0`
re-signed for the guest's account (the `manualsave0` that was there moves to `story-backups` beside the account
folder; the other saves stay) and asks the game to load it as the pause menu's Load Game does.

- Host, `auto`: a checkpoint is asked for when the episode changes or a main or sub order is completed, at most every
  30 s, only in the world and while the game is not saving. Host, `key`: only when the guest asks.
- Guest, `auto`: an offered checkpoint is fetched and loaded at once. Guest, `key`: the info log says the partner
  has moved the story on (with `notices`); `story_key` (a virtual-key code, default 120 = F9, 0 = none) fetches and
  loads it. With nothing on offer the key asks the host for a fresh checkpoint (at most every 10 s) and loads it when
  it arrives.
- The load waits until the guest's porter is in the world, no game event runs and the game is not saving. After the
  load the order sync learns the new world before it reports anything.
- What fails is told and given up, never retried by itself: a checkpoint that has not arrived whole after 300 s, an
  install that fails, a game that has not left the world 30 s after the load request (`STORY ... load refused`).
- The news travels on the ordered event stream (type 22: ready, ask, loaded, failed); the bytes travel as the
  launcher's save datagrams (`launcher/saves.h`) on the mod's socket, keyed by the session and the checkpoint's
  generation, checked by size and checksum, and refused unless they are a checkpoint.
- The mod only writes inside the copy's own data folder (`<data>/local/KojimaProductions/DeathStrandingDC/<the one
  account>`, found from the mod's folder); two account folders there, or none, turn the feature off with a log line.

Log lines start with `STORY`. Lab commands, file `puppet.story` in the mod folder (stamp-guarded like the others;
they need `story_resync` set, and each makes one call alone): `state 0 <stamp>` (the game's save state and the
newest checkpoint), `watch <seconds> <stamp>` (that twice a second), `save 1 <stamp>` (ask the game for a
checkpoint), `load <type*100+slot> <stamp>` (ask the game to load a save: `100` = `manualsave0`, `400` = `autosave0`),
`key 1 <stamp>` (the catch-up key).

Live checklist (an authorized operator; disposable copies, one copy unless stated; expected values are what the
static reading predicts; steps 1 to 5 and the key mode of step 6 have been seen as written, and a save that changed
under the running game does load, the open question of step 5):

1. Arming. Config: the play setup plus `"story_resync":"key"` and `"role":"host"`. Expect at the first tick in the
   world `STORY resync on: host, key, key 120, save armed, load armed, account <id>`. `save off` or `load off` with
   `... function differs` means another build: stop.
2. Reading. `state 0`: expect `readable=1 in_game=1 requests=0 pending_type=255 module_state=0` (module_state is
   unknown: record it) and `newest=<the folder the copy was continued from>`.
3. Save on request. `watch 60`, then `save 1`. Expect `STORY test: asked; after: ... pending_type=4`, then in the
   watch rows `pending_type` back to 255, `requests=1` for a moment, and `newest=` changing to an autosave folder
   with a later time within a few seconds; the game shows its saving icon. Compare the file on disk with the row
   (size). If `pending_type` goes back to 255 and no file appears, the manager dropped the request: record the
   porter's situation (near a terminal, in timefall, carrying, in a BT area) and try in the open. Repeat inside a
   private room and during a cutscene to learn the refusals.
4. Load on request, the game's own save. With an existing `autosave0`: `watch 90`, then `load 400`. Expect
   `STORY test: load result 2`, `requests=1` and `module_state=3` right after, `porter=0` in the watch rows during
   the loading screen, then `porter=1` again at the save's place. `result 1` means the game does not list that save.
   A `STALL ... in 'story'` line means the call faulted: stop the copy and keep the crash record.
5. Load of a file that changed under the running game (the open question). Stop watching; copy another valid
   checkpoint of the same account over `manualsave0/checkpoint.dat` in the copy's save tree while the game runs
   (a save baseline of the test copy), then `load 100`. Expect result 2 and the world of the new file. Result 1, or the
   old world, means the game keeps its list or its bytes from start-up: the resync then needs another way to make
   the game look again (record which).
6. Two copies, key mode: host `"role":"host"`, guest `"role":"guest"`, both `"story_resync":"key"`, the guest started
   from the host's save. On the guest: `key 1`. Expect on the guest `STORY catch-up key: asking the partner for a
   checkpoint`; on the host `STORY checkpoint wanted: the partner asked`, `STORY asking the game for a checkpoint`,
   `STORY checkpoint written: autosaveN, <bytes>, generation 1`, `STORY offering ...`; on the guest `STORY fetching
   ... generation 1`, `STORY fetch 25%` .. `STORY checkpoint fetched`, `STORY the partner's checkpoint is in place as
   manualsave0`, `STORY load request result 2`, `STORY the game left the world: loading`, `STORY loaded the partner's
   checkpoint, generation 1, after N s`; on the host `STORY the partner loaded generation 1`. Then compare from both
   processes: the episode (`PROGRESS episode=`), the order records (read from both processes) and the porter's
   position (the guest stands where the host stood when the checkpoint was written). The partner body, gear and
   name must come back on both sides as after a reload.
7. Two copies, `"story_resync":"auto"` and `"share_story_orders":true` with `share_orders`: the host takes a sub
   order at a terminal and completes it. Expect `ORDER story ... state 1->4` on the host, `ORDER start of <id> here ->
   partner`, on the guest `ORDER partner: start of <id> requested` and its own `ORDER story ... state 1->4`; on
   completion `ORDER story ... result ->4` on the host, the guest's `ORDER partner: success of <id> requested`, and
   (auto) one checkpoint offered and loaded by the guest. Watch the guest's screen through the completion: a cutscene,
   a move of the porter or a result screen there is the answer to "what does a main order's request set off".
8. After every run: the real saves are byte-identical, and the guest copy's `story-backups` holds what `manualsave0`
   was before.

## The partner's name over his body (`partner_tag`, off by default)

With `"partner_tag": true` (requires `puppet_probe`) the partner body shows the partner's name (their `nickname`, which
the launcher writes from the name each player types) above its head, in the game's own hologram-name label
(`nametag.h`). Each mod sends its own nickname to the partner on every connection as CARGO events of type 10 (UTF-8 in
9-byte chunks, at most 36 bytes); until a name arrives the label reads "Partner". The label is one of the game's
hologram-name records, which the mod allocates, registers with the game's record list (0x307e170) and moves every tick;
the game's HUD projects and draws the nearest few. It goes when the body hides, a game event starts or the partner
leaves (the record is unregistered with 0x3081db0 and freed); when the world is replaced it is unregistered only if the
game's record list is still the one it joined (otherwise left alone, never freed). Log lines start with `TAG`. `puppet.ride` "tag on" / "tag off" shows it without the config. Tests:
`nametag_test.cpp` (mutants MARKUP, STILL and ORDER); live, two copies: each showed the other's nickname over the
partner body, and the label went when the other copy closed. The launcher's play setup turns it on.

**Other players' names (built, not run live).** With the same setting every other player's body (`others`, below)
gets a label of its own with the name that player sent, "Porter" until one has arrived. All labels are one set
(`nametag::Tags`): key 0 is the partner, a player id another player; each is a record of its own, made through the
same three calls only after its body exists, moved to 2.1 m above that body every tick, removed when the body hides
(a stale player, a private room, a game event here), when the player leaves, and handed back when the world is
replaced. No new game call or address is used: per body it is exactly what the partner's label does, and the native
side already kept its records in a table. The game's HUD presents three hologram names at a time, the nearest by its
own sort (`game_offsets.md`, "hologram-name records"), and how many records its list takes has only been seen with
the one partner label. So the mod registers at most four records at once (`LabelsAtOnce`): the partner's, then those
of the other players nearest this porter, a body keeping its label until another is 2 m nearer; among many players
the names appear as the porter comes near them. The lab command `tag all` in `puppet.ride` lifts the limit (a record
for every shown body) and `tag near` puts it back; neither path has run live. Log lines read `TAG player <id> shown`.
Tests: `nametag_test.cpp` (mutants TAGS_KEEP, TAGS_MIX, TAGS_BODY, TAGS_EVENT, TAGS_FORGET, TAGS_LIMIT, TAGS_NEAREST,
TAGS_FLAP, TAGS_FIRST), `others_test.cpp` `labelling` (OTHERS_LABEL_BODY, OTHERS_LABEL_NAME) and the fake world
(`test_others_partner.py`).

## Riding along on the partner's truck (`passenger_key`, with `partner_vehicle`)

When the partner drives a truck, standing next to it (4.5 m) and pressing **J** holds this porter on the truck's side;
J again lets go. He is also let go by himself when the partner gets out or leaves, a game event starts or the link
drops, and is set down on the ground beside the truck. It uses the game's own "follow a helper" mode of the player
mover (`passenger.h`; game_offsets.md, "Riding along"): the partner stays the only driver and nothing is saved.
While he rides, his samples carry the passenger flag and no speed: the driver's game seats his body in the cab beside
the driver, in the riding pose, on its own truck where that truck is now (held against the truck's side, the body
pushed the real truck over: live), so it neither trails behind by the link delay nor pushes.
Not riding, a porter who walks into the partner's truck is kept out of its box (the display truck has no collision
of its own). `"passenger_key": N` sets another virtual-key code, 0 turns it off (and the keep-out with it). Log lines start with `PASSENGER`. Whether J is
free in the game's default keys is not checked. Lab test: `puppet.ride passenger on HELPER [parent]` / `off` on the
mod's own lead truck. Tests: `passenger_test.cpp` (mutants FAR and STAY).

## Likes for the partner (`partner_likes`, off by default)

With `"partner_likes": true` (requires `puppet_probe`) pressing **L** (free in the game's default keys) while the partner
is connected and no game event runs sends the partner one like, at most one per 1.5 s (`likes.h`, a CARGO event of
type 9). The partner's mod gives its porter that like the game's own way, `DSLikeCommand_sReceiveLike` with reason
Other and no sender, which shows the game's like counter popup; it gives at most one a second and ten a minute whatever
arrives. These are the game's real likes: they are saved and count toward the porter's evaluation and the like trophies,
which is why the feature is off unless asked for. Log lines start with `LIKE` (`sent to the partner`, `received from the
partner`, `dropped: ...`). Tests: `likes_test.cpp` (mutants GAP and CAP); live, two copies: one L press raised the
other porter's likes by one with the counter popup.

## Shout, scan and vitals for the partner (`partner_shout`, `partner_scan`, `partner_vitals`, off by default)

Three small things that make the partner feel alive from a distance (`signals.h`; `game_offsets.md`, "Shout, scan and
vitals for the partner"). Each key requires `puppet_probe`, covers both directions (what this porter does is sent,
what the partner does is shown), and should be set by both players. Run live with one copy and the stand-in
partner (2026-11-15): the scan counter, the call-out, the tag, the scan notice, one scan and one shout sent, and the
voice and pulse calls without a fault; what the voice lines sound like and whether the pulse shows has not been
judged, and two real copies have not been run.

- `"partner_shout": true`: when this porter calls out (the game's shout key), the partner gets one event (CARGO type
  19, delivered exactly once; at most one a second), and his game asks the partner body for a voice line from its
  head with the game's own function for that body class (`0x273bb10`), sentence container `partner_shout_voice`
  (0..15, default 0). Which container suits a shout is not known: `puppet.ride` `voice N` plays container N alone.
  The shout pose itself comes with the mirrored animation controls. Log: `SHOUT sent to the partner`, `SHOUT voice
  asked on the partner body, container N`.
- `"partner_scan": "notice"`: this porter's Odradek scan (his scan counter going up, read-only) is one event of the
  same type; the partner reads "<name> scanned the area." in the info log (with `notices`), at most once every 20 s.
  `"pulse"` also writes the game's scanner pulse variables at the partner body (an experiment: it may draw nothing);
  `puppet.ride` `pulse` tries that alone. Log: `SCAN counter read`, `SCAN sent to the partner`, `SCAN the partner
  scanned`, `SCAN pulse variables written, slot N`.
- `"partner_vitals": true` (with `partner_tag`): the porter's blood and stamina (percent, in steps of 5) and whether he
  is overloaded or off balance go to the partner as three bytes (CARGO type 21) on every connection and then only
  when what would be shown changes, never more often than every 2 s; the name tag reads `Name  B90 S60 heavy
  unsteady`, its text changing at most every 2 s. The first status pair is taken for blood and the second for
  stamina: live, the second fell steadily in a sprint and a 12 m drop took the first from 1.00 to 0.82
  (`game_offsets.md`); `puppet.ride` `vitals` logs this porter's raw readings (`SIGNAL test: ...`).

A receiver shows at most one shout and one scan every 1.5 s and twenty of each a minute, whatever arrives. The stand-in
partner can play the other side: `--shout-every S`, `--scan-every S`, `--vitals BLOOD STAMINA BITS`. Tests:
`signals_test.cpp` (mutants KIND, SHOUT_HELD, SCAN_LEARN, SCAN_GAP, RECEIVE_GAP, SCAN_OWNER, VITALS_RANGE, INFO_OWNER,
INFO_RANGE, STATE_HOLD, STEP, VITALS_CHANGED, VITALS_GAP, LABEL_GAP) and the scan notice in `notice_test.cpp` (SCAN_GAP).

## Many bodies (lab command `crowd N`)

`crowd <0..200>` in `puppet.double` makes that many more bodies beside the partner's own body (rings of 6, 12, 18 ...,
2 m apart, at his height, or around this porter while no partner is shown), each with its own `OwnBody` and `Double`
and the partner's animation sample; `crowd 0` removes them. The game is asked for one new body every 300 ms. Every 5 s
the log says `CROWD wanted N made M driving D given up G tick avg .. us worst .. us`. It is an experiment for the
question "how many Sams does one game hold" (STATUS.md, "Many Sams in one game"); nothing of it is on the wire.

Since the others were added (next section) the crowd's bodies are the same `Body` and `Bodies` classes the other
players use (`others.h`); only where they stand and what animates them is the crowd's own. Two things differ from the
code that ran live on 2026-11-16 and have not run live: after a load the bodies are made again one every 300 ms
instead of all in one tick, and a crowd with nowhere to stand (no partner shown and no readable porter) waits hidden.

## Other players (`others`, off by default; first live run 2026-11-16)

The transport is the relay of the players' own server (`core/relay.h`, `world/README.md` "The relay"). Config:
`"relay": "ADDRESS:PORT"` (numeric IPv4, on loopback, at the partner's address or at the listed `server`; "Group
play" above), `"relay_key"` (the group's key,
letters, digits, `-` and `_`) and `"player_id"` (this player's number on the relay, not 0); they require `others`.
The mod opens a second UDP socket connected to that address, reports this porter's position (none while loading or
in a cutscene), and feeds who is near and their datagrams to the sessions below. The log says `RELAY online`,
`RELAY offline: everybody near has left`, and every 10 s `RELAY online near N in .. out .. accepted .. refused ..`.
That is the relay's pairs mode (`"relay_mode": "pairs"`, the default); the broadcast mode is the next section but one.


Besides the one partner the mod can hold up to `"others": N` more remote players (0..32, default 0; requires
`puppet_probe`), **presence only**. Each one has

- a 64-bit id, given by whatever carries the datagrams;
- a presence-core session of its own (`core/CORE.md`, "Several peers"): handshake, clock mapping, the interpolated
  pose with speed and flags, the adaptive presentation delay, and the animation samples (kind 10) when they arrive;
- the name it sends (its `nickname`, as the core's name events), kept with the session and logged; with
  `partner_tag` it is drawn over that player's body (the section on the partner's name, above);
- a body of its own, made like the partner's own body (`OwnBody`) and driven every game-thread tick by its own
  `Double` to that player's pose with that player's animation sample.

A body is hidden (asleep and invisible where it stands, still the mod's) while its player is not connected, is stale
(the core's rule: no pose for 1.5 s), is in a private room or cutscene, or while this game is in a game event or
loading. It is removed when the player leaves. After a load every body is made again in the new world, one every
300 ms. A player who is down lies in the collapse pose, one caught by BTs in the drag pose, and a rider sits in the
riding pose without a vehicle (as the partner does without `partner_vehicle`).

Nothing that changes the world exists for others: no cargo put down or picked up, structures, enemies, orders,
story, time or rain, vehicles or carts, no map pin and no notices. The one exception, opt-in and in broadcast mode
only, is a catcher fight one game owns and others visit ("Fights with visitors", below). What is only shown can be turned on: the gear each
player wears and the cases each player carries (`others_gear`, `others_carry`, the next section). Otherwise what this
porter sends them is his pose, his name and his animation controls (not what he holds, his wheels or his carts). The
partner path is not touched by any of it: with `others` at 0, or with no relay configured, no session exists and
nothing is sent.

### The broadcast mode (`relay_mode`; live once, 2026-11-16)

With `"relay_mode": "broadcast"` the same players reach each other through the server's broadcast mode (envelope
version 2: `core/broadcast.h` and `core/broadcast_client.h`; the server's side is `world/relay.h`). The mod then runs no session
per player: `Crowd` (`crowd.h`) holds one `ds::broadcast::Client`, which publishes this porter's pose and animation
controls once a tick to the server, and the server copies every update to each player near (every update within its
full distance, fewer beyond it, animation only within its animation distance), several players' updates to one
datagram, on the server's clock. What a player only shows of himself is state the server keeps, one blob per kind:
the name, the worn set, the carry summary and the carried cases' places, each blob one complete output of the same
senders the pairs mode uses (`nametag.h`, `gear.h` WornSender, `carry_visual.h` Sender, `carry_place.h` Sender), its
events written as the core's CARGO fields (`crowd.h` has the layout). The server hands it to a player who comes
near, to everybody near when it changes, and again to one who sees he missed a version.

`Crowd` and `Others` are both `others::Players`, so bodies, poses, name labels, gear and cases are the same code in
both modes. What is the same: the most players (`others`), who is taken when that many are near and who waits, the
accepted kinds (the name always, gear with `others_gear`, carried cases and places with `others_carry`; blobs of a kind
that is off are dropped and counted in `dropped_events`), what this porter sends (nothing of gear or cases without the
setting; the controls only), a blob shown only on its own player's body, a hidden body for a player who is stale, in a
private room or cutscene, or while this game is in a game event or loading. What differs:

- a player is connected while his presence arrives (a sample taken within the last 3 s; standing still he is sent
  twice a second) instead of while a core session is up; there is no handshake and no pair session number;
- what a player carried is forgotten when he goes quiet and taken again from the server's copy when he is heard again
  (nothing is resent: the server holds it); worn gear and the name stay;
- a blob is taken only whole: every event of its kind's type, and the kind's receiver completing its set exactly
  at the last event; anything else is refused and counted as malformed;
- the game's datagrams to the server do not grow with the players near it (pairs: a session's worth for each);
- every other check of a session's presence (the core's decoder for every value, order, freshness, impossible motion,
  one teleport in five seconds) is made per source by the client, and the source of every update is the server's own
  record of who sent it.

The server shows a player at most a number of the players near him (its `near_cap`, 32 on a friends server and 16 on
a public one; `world/README.md`, "How many players one player sees"): his friends first, then the nearest, kept while
they stay about as near. Those are the players his roster has, and nothing of the others near reaches the game, so the
mod's own `others` (at most 32) chooses among what the server sends. `"show_others": "friends"` or `"nobody"` asks the
server for fewer (the launcher's choice on the server comes first). Built and tested without a game (2026-11-17).

Players of a linked server (`world/README.md`, "Presence across linked servers") arrive as ordinary broadcast
players and need nothing here: the server gives each an id of its own, keeps his build, and puts his home server
into his name state (`Bea · north`, at most 24 characters), so his label shows it. Built and tested without a game
(2026-11-17), with `coop-swarm` stand-ins on two linked servers; no game has seen one.

The log says `OTHERS ready (broadcast)`, `RELAY online (broadcast)`, `RELAY refused this player: the server wants
another build`, and every 10 s `RELAY broadcast online near N taken N in .. (.. bytes) out .. (.. bytes) frames ..
accepted .. refused .. unknown .. malformed .. fetches .. clock offset .. ms rtt .. ms` (and `near with another
build N` when the server sees players of another build where this porter is). `mod_status` `"others"` has `"mode"`.
In the fake world the test is the relay socket: `mod_test_relay_drain` gives the client's next datagram for the
server, `mod_test_relay_deliver` hands it one from the server.

Tests: `crowd_test.cpp` (three Crowds and a bare client through the real server, `world/relay.cpp`, in memory, each on
a clock of its own: blobs, names, gear and cases with and without the settings, the cap, a player who goes quiet and
comes back, pulses, whole sets) with 13 mutants (CROWD_BLOB_LENGTH, _TYPE, _RANGE, _WHOLE, _EVENTS_OFF, _SEND_OFF,
_MIX, _CAP, _LEFT, _CARRY_LOST, _CARRY_BACK, _ROSE, _GATED); `test_others_broadcast.py` (three copies of the fake
world in group configs, each with a UDP socket to the real `coop-world`, and three `coop-swarm --mode broadcast`
stand-ins: bodies made and shown, the names over them, the stand-ins' too, gear and cases with the settings and none
without, a player leaving and coming back, the stand-ins leaving, this game's traffic) and its mutant
`fake-relay-mode.so` (the setting ignored); `unit.cpp` for the config keys with the mutants CONFIG_RELAY_MODE and
CONFIG_SHOW_OTHERS (`show_others`: its three values, four that are none, refused in pairs and without a relay).

**Live** (2026-11-16/17): two real games and 12 stand-ins replaying a recorded walk, all near each other, each game
showing the other 13 with names (the lab's group regression in broadcast mode), and two real games on two linked
servers ("Presence across linked servers" in `../world/README.md`). The launcher's group configs use it since
2026-11-17. Not yet looked at live: gear and cases in broadcast mode, and the poses of players beyond the
server's full distance (one update in 200 ms by default: with loss the pose stops for a moment; the client measures
that interval and waits for it: `adaptDelay` in `core/broadcast_client.cpp`).

### Other players' gear and carried cases (`others_gear`, `others_carry`; built, not run live)

`"others_gear": true` (requires `others` and the game provider; default false) copies onto every other player's body
the gear that player wears, as the partner's gear is copied onto the partner's body (`partner_gear`, below).
`"others_carry": N` (0..8, default 0; requires `others_gear`, since the cases hang on the copy of the backpack) also
draws the carried cases of the N nearest other players, as `carry_visual` draws the partner's.

- **What travels.** Each pair's own presence-core session carries the same reliable events the partner path uses: type
  7 (the worn set, `gear.h` WornSender: once it has held for 5 s, on every new connection), type 6 (the carry summary,
  `carry_visual.h`) and type 16 (where each case sits, `carry_place.h`). `Others` keeps them per player: a set from
  one player is only ever shown on that player's body. With the setting off they are not sent and, arriving, are
  acknowledged and dropped like every other type (`core/CORE.md`, "Several peers"). Accepted from others, exactly:
  10 always; 7 with `others_gear`; 6 and 16 with `others_carry`. Never: cargo put down or picked up, structures,
  enemies, orders, vehicles, story, the cart's cases (18), the outfit, likes, or any other type. What a player
  carried is forgotten when that session drops (it is sent again on connecting); what a player wears stays until a
  new set arrives.
- **Gear (`gear.h`, `Mirrors`).** One `Mirror` per body in a keyed set, key 0 the partner and a player's id another
  player, sharing one injected `Api` as the name labels share `nametag::Tags`. Per body it does exactly what the
  partner's mirror does: copies made from this porter's matching pieces only after the body exists and its place can
  be read, hidden whenever the body is hidden (a stale player, a private room, a game event here), removed when the
  player leaves, forgotten (never touched) when the world is replaced. Another player has a save of his own, so
  nothing is made for him until his set has arrived (the partner still falls back to what this porter shows). When a
  player leaves, his copies are removed one tick before his body (`others::undress`). The game is asked for at most 2
  new copies a game-thread tick over all bodies, the partner's first (30 players arriving at once once held the game
  thread for 5 s before bodies were made one a tick), and this porter's gear is read about once a second for all
  other bodies together instead of once per body.
- **Carried cases (`cargo_stack.h`, `Stacks`).** One `Stack` per body, the same shared `Api` and the same budget: at
  most 2 new case meshes a tick over all bodies, the partner's first. Of the other players only those who are
  connected and visible, whose body wears a copy of the backpack and who carry something are candidates; the
  `others_carry` nearest of them get their cases (a full backpack is up to 24 meshes), and one who has them keeps
  them until another is 2 m nearer (`others::Nearest`, the same rule as the name labels). With measured places a
  case goes exactly there on the backpack copy (hips and arms too), otherwise the summary's backpack cases are stacked
  by the layout. A hidden body or an unreadable backpack copy has none; a game event here: nobody.
- **The partner path** is the same code with key 0 listed first: with `others_gear` off and `others_carry` 0 it makes,
  sends and draws exactly what it did with the single mirror and stack, and no new game call or address is used for
  anybody.

Log lines: `GEAR player <id> ...`, `STACK player <id> ...`, `OTHERS player <id> shows N pieces of gear` / `carries N
entries`, and `GEAR N events of this porter's ... -> other players`. `mod_status` has `"gear_shown"` (`pieces`, `by`
key, `sets` taken) and `"cases_shown"` (`cases`, `by` key, `sets` taken, `dropped_events`).

**Not run live; what a live run must check.** The calls per body are the partner's, but they have only ever run on
one body. A live run (two real games with `others_gear` and `others_carry`, then stand-ins) must look at:

- the game thread's tick time and the `STACK slow` lines while many bodies get their copies and cases;
- the culling guard (`GUARD the culling job met a drawable ...`) and fault records, with many attached copies on
  bodies that hide, show and are removed (the partner's cuff copy replaced at a terminal once left a drawable in
  the culling tree);
- the end-of-tick flush of deferred rep calls covers only the partner's body (`REP` lines): whether attaching copies
  to other bodies leaves calls waiting on those bodies' queues is unknown (the comment `SEAM others_gear` in
  `mod.cpp`);
- every backpack copy gets an empty cargo carrier of the mod's own, kept for the life of the process (about 8.5 kB
  each): watch the memory over many arrivals and departures;
- that a body is never removed with copies still attached (the undress order), and that a world reload with others
  near leaves nothing behind;
- that each body shows its own player's gear and cases and not this porter's (another save, other gear).

Tests: `gear_test.cpp` (the set: budget, keys that leave, mixing, unreadable places, one shared reading, the
no-local rule; mutants GEAR_SET_BUDGET, SET_KEEP, SET_MIX, SET_SITE, SET_READS, SET_LOCAL), `cargo_stack_test.cpp`
(STACK_SET_BUDGET, SET_KEEP, SET_MIX, SET_FORGET), `others_test.cpp` `wearing`, `undressing`, `carrying` and `kinds`
(OTHERS_EVENTS_OFF, OTHERS_EVENTS_MIX, OTHERS_WEAR_BODY, OTHERS_WEAR_MIX, OTHERS_CARRY_LOST, OTHERS_CARRY_LIMIT,
OTHERS_CARRY_NEAREST, OTHERS_CARRY_FLAP, OTHERS_CARRY_HIDDEN, OTHERS_CARRY_PACK, OTHERS_CARRY_MIX, OTHERS_UNDRESS,
OTHERS_PLACE_FRESH), the config in `unit.cpp` (CONFIG_OTHERS_SHOW), and the fake world (`test_others_partner.py`
`dressed`: the partner and three other players, each body with its own gear, the nearest carrier's cases, a player
without the settings taking nothing, a game event, players leaving and coming back; mutant library
`fake-others-undressed.so`).

**The seam to the transport.** `others.h` talks to a `Link`:

```cpp
struct Link {std::function<void(uint64_t to,const uint8_t* data,size_t size)> send;};
class Others {
    bool entered(uint64_t id,uint64_t nowMs);          // that player is near
    void left(uint64_t id,uint64_t nowMs);             // no longer
    ds::Error received(uint64_t from,const uint8_t* data,size_t size,uint64_t nowMs);
    void local(const Local&);                          // this porter, once per game-thread tick
    void tick(uint64_t nowMs);                         // handshakes, pings, presence, names
    bool animation(const ds::Animation&,uint64_t nowMs);
    std::vector<Shown> frame(uint64_t nowMs);          // everybody as shown now
    void clear(uint64_t nowMs);                        // everybody leaves
};
```

Times are the mod's monotonic milliseconds, the clock the core uses. Every call must come from one thread at a time.
In `mod.cpp` the seam is marked `SEAM`: `othersStart(self, link)`, `othersEntered`, `othersLeft`, `othersReceived`
and `othersStop`, called under the runtime lock: by the relay code from the network thread, where the partner's
socket is polled, and in the fake world by the test. Without `relay` nothing calls them, so `others` alone starts
nothing. `MemoryHub` in `others.h` is a link in memory for tests.

Limits to know before running it live:

- A player over the limit is not shown; it is remembered and taken when somebody leaves.
- Every pair has its own session, so this porter's upstream grows with the number of others (CORE.md).
- Bodies exist only in the Windows build; on other builds the sessions run without bodies.
- Whether many bodies each with a different animation hold as well as the crowd's 80 with one animation, how they
  behave on uneven ground, and the cost of N sessions on the network thread are unknown until it runs.

Tests: `others_test.cpp` with a fake world of bodies and several players on independent clocks over a link with
delay and loss: several enter, walk, go stale, drop out and come back, leave and return; the limit; a datagram of one
player given as another's; an unknown id; another build; a jump; animation samples and one-frame pulses per receiver;
a new world; a plain core as the peer. Mutants (`mutants.py`, 15): OTHERS_CAP, OTHERS_UNKNOWN, OTHERS_BUILD,
OTHERS_SELF, OTHERS_LEFT, OTHERS_ROLE, OTHERS_PAIR, OTHERS_STICKY, OTHERS_KINDS, OTHERS_STALE, BODIES_HIDE,
BODIES_REMOVE, BODIES_ONCE, BODIES_RATE, BODIES_MIX, and for the labels OTHERS_LABEL_BODY and OTHERS_LABEL_NAME; and
CONFIG_OTHERS in the unit test.

**The partner and others at once (fake world).** `test_others_partner.py` runs the real mod four times in one
process (`build/fake.so`: fake game memory with a partner body and up to eight other bodies, a fake label list, and
datagrams handed over by the test): A and B are partners over the two-player link, and the test is the relay between
A and the other players C and D (`mod_test_others_roster`, `mod_test_others_deliver`, `mod_test_others_drain`, test
exports of the fake build only). It shows: the partner connects and gets his body and name first; C and D come near
and each gets a session, a body and a label over that body; the partner link refuses nothing and loses nothing while
they are there, leave and come back; a game event hides every label; A's partner socket saw exactly the datagrams B
sent and A's sessions exactly the ones the relay carried; one of C's datagrams given to the partner's socket is
refused as another session, and one of B's given to the others as C's, D's or a stranger's is refused or dropped;
and with `others` set but no `relay` there is no session, no body, no datagram and no log line (the gear and cases
are the `dressed` property, above). Three mutant builds must fail it (`test_others_partner_mutants.py`):
`fake-others-to-partner.so` (a datagram of another player also handed to the partner's core),
`fake-others-no-relay.so` (sessions started without a relay) and `fake-others-undressed.so` (a body removed while it
still wears gear copies). `mod_status` now has
`"others": {on, near, connected, bodies, shown, sent, received, accepted, refused, unknown, malformed}` and
`"labels_shown"` in every build.

## Fights with visitors (`host_fights`, `join_fights`; live once, 2026-11-17)

The owner's decision: each player plays his own save and story, and in a boss fight **one game owns the fight** while
other players join it as **visitors**. This is the first step, for the catcher fight of a BT area, over the relay's
broadcast mode: the fight channel (`core/CORE.md`, "Fight channel"; `world/README.md`, "The fight channel") and
`fights.h`. Config (both off by default; they need `relay_mode` "broadcast", the game provider and `game_thread`):
`"host_fights": true` offers a catcher fight that starts in this game to the players near; `"join_fights": true` joins
a fight this game is offered (always, for now; a prompt later). The two-player sharing below is not changed by either.

### What carries over from the two-player sharing

The two-player BT sharing (`share_enemies`, `bts.h`; "Shared MULEs, BTs, orders and roads" above) is symmetric: both
games run their own fight and either one's news applies in the other's. With visitors only the owner's game decides.

| Two players (`bts.h`) | Owner and visitors (`fights.h`) |
|---|---|
| A fight one is pulled into starts in the partner's game when he is within 300 m (`FightOn`, repeated while it runs) | The owner's fight is announced (kind, place, 300 m) and kept every second; the server offers it to the players of the same build within 300 m; a visitor who joins starts a fight in his own game with the catcher command (0) to show it: the shown copy |
| One health bar: each game sends the percent its catcher lost; the other takes it off (`CatcherHurt`, never below the floor) | The owner sends each catcher's life (absolute, four times a second); a visitor takes his copy down to it, never up and never below the floor. A visitor sends the damage his own hits did, counted from the start of his visit; the owner takes off what is new, never below the floor. Nothing is counted twice: what the mod writes on a copy is never the visitor's damage, and a lost report is repaired by the next (every second) |
| Either game's cleared area ends the other's fight with the cleared command (1) | Only the owner's cleared area ends the copies (command 1). A visitor whose hit killed his copy says so; the owner ends its fight with the area cleared only when its own catcher is at the floor too, else the claim is refused |
| A gazer destroyed in either world is destroyed in the other (its record of dead gazers is saved) | A gazer a visitor destroys in his world is destroyed in the owner's. The owner's are never destroyed in a visitor's world: that record is saved and would change his world |
| A fight lost here keeps the partner's fight news away for two minutes (`FightSync::resting`) | A visitor joins only from an offer, shows one copy a visit, and does not join a fight he left for 10 s; back in the same fight later, his counts go on from where they were, so the owner takes only what is new |
| MULEs down, camp alarms, voidout craters, MULE positions (`enemies.h`, `voidout.h`, `enemy_poses.h`) | Not carried over: they are the world's, not the fight's |
| Shared rain (`share_environment`, `weather.h`) makes the BT area active in both | Not carried over: the copy runs in the visitor's own weather (research below) |

### Who decides, what a visitor shows and sends

- **The owner** is authoritative. Its game runs the fight; only its own catcher manager says whether a fight runs,
  how much life each catcher has and how it ended. It posts that state (format 1: phase running, cleared or lost, and
  each catcher's life in hundredths of a percent); a visitor's report is a request it checks against its own fight.
  When its fight ends it posts the end for a second and closes the fight.
- **A visitor's game** shows the owner's fight with its own catcher fight (the shown copy): started by the catcher
  command when the owner's state says the fight runs, its catchers' life taken down to the owner's, ended with the
  cleared command only when the owner's state says cleared. Positions are not shared: where the visitor's catchers
  and gazers are, and what his BTs do, is his game's (as in the two-player sharing).
- **A visitor sends** (format 1, all counted since he joined): the life his own hits took off each catcher of his copy,
  whether his hit killed it, how often he was caught (`vitals.h`; logged by the owner, nothing else yet), and the
  gazers destroyed in his world (at most 16).
- **A visitor never** announces a fight (not the one he shows, and not that copy after his visit), never applies
  another visitor's report (the server sends him none, his client takes none), never destroys a gazer, and writes
  nothing in his world but the shown copy's start, its catchers' life (down only, never below the floor) and the
  cleared end of a fight the owner cleared.
- While this game visits a fight or still runs the copy it showed, its catcher news are not sent to its two-player
  partner and the partner's catcher news are not applied (gazers still are): the copy is not this world's fight.

### How a visit ends

| End | The visitor's game |
|---|---|
| The owner's fight is cleared (state "cleared") | The copy ends with the cleared command, as in the two-player sharing (a cleared sphere is left in his world: research below); the visit ends |
| The owner's fight is lost (state "lost": he got away, died or voided out) | Nothing is written: the copy is his game's fight now, which he leaves as in the game (out of the tar). The visit ends |
| The owner closes the fight, leaves the server, goes silent for 3 s or his game restarts (STATUS Closed or Gone) | The same: nothing more is written; his game's own state is what it has |
| The visitor goes more than 50 m beyond the fight's radius (Released), asks to leave, or his relay goes offline | The same |
| His copy ends by his own hit (killed) or he gets away from it | He stays a visitor until the owner's fight ends; no second copy is started in that visit |

Nothing of a visit is written into the visitor's save that changes his story: no gazer, MULE, order or story state,
and his own fights are never touched (next part). What the copy itself leaves (a catcher record while it runs, a
cleared sphere after a cleared end) is the game's own BT-area state; whether it is saved is research below.

### When a visitor's own story has the same boss

Not decided (the owner's call). Today's rules keep both choices open: a game that runs a fight of its own takes no
offer (its own comes first; counted as `own_first`); a fight of the visitor's own that starts while he visits, before
the copy, ends his visit before anything is written to it; the server lets a player be the owner of one fight or the
visitor of one, never both (a visitor whose game opens a fight leaves the one he visits). So the visitor's own boss
state is untouched and he sees the owner's fight only while visiting. Shared progress (the owner's clear counting in
the visitor's story, or a visitor's own boss joined as one fight) would be a later setting on top of this: the channel
carries kind and place, so a story boss could be recognised as "the same" once its kind is known.

### Why the channel carries states and cumulative reports

The two-player sharing uses the core's reliable ordered event stream (CARGO type 13). The broadcast mode has no core
session per pair, and building one per owner and visitor would bring back the pairs mode's cost. The fight channel
instead carries versioned latest-wins data, like the shown state: the owner's posts are whole states (a lost one is
replaced 250 ms later, a visitor who joins late gets the latest at once), and a visitor's reports are cumulative counts
(applied as differences, so a lost, repeated or late report changes nothing, and the next one repairs a loss). The
server and both clients take each source's versions only forward.

Log lines: `FIGHT ready`, `FIGHT a catcher fight runs here: offered ...`, `FIGHT N visitors`, `FIGHT visitor X took
N % off catcher ...`, `FIGHT visitor X landed the last hit ...` (or `... not taken`), `FIGHT joining the fight of
player X`, `FIGHT visiting ...`, `FIGHT the owner's fight runs: a catcher fight is started here to show it`, `FIGHT the
visit ends (why): nothing more is written here`, `FIGHT a fight is offered, but this game runs one of its own: not
joined`, `FIGHT the server takes no fights`. `mod_status` has `"fights"` (`role`, `owner`, `fight`, `joined`,
`shown`, `visitors`, and counts: `hosted`, `visits`, `released`, `states`, `reports`, `states_taken`,
`reports_taken`, `damage_applied`, `gazers_applied`, `kills_taken`, `kills_refused`, `own_first`, `commands`,
`writes`); the fake build also `"fake_bts"` (its catcher manager, gazers, and every command and write the mod made).

Tests: `fights_test.cpp` (game-free: every player a `Fights` over a real broadcast client and a `Sandbox`, a game
without the game, through the real server in memory: one owner and nine visitors with a tenth refused for the cap, a
player without `join_fights` and one beyond the fight untouched; one health bar both ways; three visitors hitting at
once; a visitor whose reports are lost for two seconds (no heal, his damage arrives with the next); a gazer; a kill
claim refused, then the last hit taken; the owner leaving; a visitor that also hosts; a game with its own fight; the
floor; a visitor released out of reach who joins the same fight again (nothing counted twice); the formats) with 9 mutants (FIGHT_VISITOR_HOSTS, FIGHT_OWN, FIGHT_HEAL, FIGHT_KILL, FIGHT_ECHO, FIGHT_DOUBLE,
FIGHT_KILL_CLAIM, FIGHT_RELEASE, FIGHT_GONE_CLEARS); `test_fights.py` (five copies of the real mod in the fake world,
each with a UDP socket to the real `coop-world --fight-visitors 2`: the fight offered and joined by two, each showing
it, a visitor that also hosts never announcing it, one health bar, a gazer, the cap leaving a third out, a game with a
fight of its own and one without the settings untouched, the owner's game going away and both visitors released with
nothing more written) and its mutant builds `fake-fight-visitor-hosts.so`, `fake-fight-gone-clears.so`,
`fake-fight-settings.so`; the config in `unit.cpp` (CONFIG_FIGHTS). The server's rules have their own tests and mutants
(`world/fight_test.cpp`), and the client's (`core/tests/broadcast.cpp`).

### What a live run must check (research for the lead)

Nothing of this has run with the game. What could not be determined without it:

- whether the catcher command (0) starts a fight in a visitor's game where his own world has no active BT area (no
  rain there, a cleared sphere, another episode's map state); live it started one "with no fight running and outside
  every cleared sphere" (`game_offsets.md`, "BTs: what destroys a gazer, and the catcher command live"), always
  where the area was active through shared rain. Live 2026-11-17 (second run): with rain at the owner's game only and
  no active area at the visitor's, the visitor's copy started, took hits both ways and ended cleared like the first
  run; his cleared spheres stayed 0 (the owner's went to 1). Other episodes and a visitor inside a cleared sphere are
  still not looked at;
- which catcher the visitor's game brings (its type, `+0x28`) and whether it matches the owner's (the life is shared by
  ordinal, in the manager's record order; a fight with several catchers, or of different types, is not checked);
- the place: the owner's fight is placed where its porter stood when it started, and offered within 300 m of that
  (in the relay's axes); whether a fight's tar area moves away from that point is unknown;
- whether a cleared sphere left by the cleared command in a visitor's world (saved, a countdown from 300) matters to
  his own story, and whether a catcher record started by command 0 and left running when the owner goes is saved;
- a visitor's own boss fight starting while he visits, at the same moment as the copy (the two cannot be told apart
  today: a fight that rises after the copy's command is taken for the copy);
- story bosses (scripted fights, not BT-area catchers): not recognised; the kind field has only the catcher;
- the visitor's hit converted to percent on his difficulty and taken off the owner's catcher on the owner's: the
  percent compares across difficulties (as in the two-player sharing), but a hit's percent differs between two
  difficulties;
- the delay: a visitor's hit reaches the owner and comes back as the owner's life within about 0.3 to 0.5 s on a LAN
  (one report interval, one state interval, two trips); how that looks with ten players is unknown;
- gazers destroyed by visitors in the owner's world, reported at most 16 per visit;
- `vitals.h` caught and dead read in a visitor's game while his copy runs, and what being caught does to a visit (his
  game may start a catcher fight of its own: the copy is then that fight);
- the cost: five looks a second at the catcher manager (and the gazers while owning or visiting), on the game thread.

## The partner from the start (`partner`)

`"partner": "own"` (or `"peer"`) in `mod.json` shows the partner from the start, as the `puppet.double` command `own` /
`peer` would, with no command: `own` draws a Sam the mod makes (the resident double until it exists), `peer` borrows the
game's resident double. It requires `puppet_probe` and an explicit role; the default is `"off"`. The log says
`DOUBLE start mode own (config)`. The launcher writes `partner:"own"` and `partner_gear:true` with the game-thread hook
for every co-op session (`launcher/README.md`). Live (2026-11-08): two copies started that way, with no command, each
made its body, drew the other porter at the other's place and copied the gear; no fault in the crash recorder. A return to
the title screen and Continue in that setup made a new body with its gear in the new world (the old pieces were
forgotten, not touched).

`"partner_marker": true` (requires `puppet_probe`) arms the map markers from the start, as `puppet.marker` `on` would,
but binds no ping key (whether V is free in the game's default keys is unverified): the partner's pin follows them on
the map and the partner's pings arrive. The launcher writes it too. Live, two copies: `MARKER on (config), no ping key`
and each placed the other's pin at the other porter's position.

## The partner's gear (`partner_gear`, off by default)

With `"partner_gear": true` in `mod.json` (requires the game provider), or the command `puppet.gear` `on <stamp>`, the
body the mod makes for the partner in `own` mode wears the porter's visible gear: in the Episode 1 test save the
backpack (with its pouches, rain cover and cylinder) and the wrist cuff. The mod reads the porter's direct children,
copies each visible plain gear piece and the backpack from its own game resource, and attaches the copy to the body at
the same helper with the game's attach call (`gear.h`; the evidence is in `game_offsets.md`, "Gear on the partner
body"). A copied backpack gets an empty cargo carrier the mod owns, without which the game's backpack update faults.
The porter's visible gear is read every second while the body shows; the copies are made once the same set has been
seen three times in a row, and a different set seen three times in a row replaces them (right after a load the game
briefly shows gear it then hides: 15 pieces, then 2). The copies are hidden whenever the body is,
removed with `off <stamp>`, when `own` mode ends or when the body changes,
and forgotten (never touched) when a new world loads. Each side also sends what its porter shows (CARGO events of type
7: each piece as its helper's hash and its place among the porter's pieces with that helper), and the partner's body
wears that set, copied from the matching pieces of the local porter, shown or hidden; until a set arrives, the local
porter's visible gear is used. Live, two copies: `GEAR the partner shows 2 pieces` on both. Carried cargo is not drawn.

The outfit goes with it (`outfit.h`): each side sends its porter's enabled art parts (hood, caps, suit pieces: one CARGO
event of type 8, a 52-bit mask in this build), and the partner's body switches the parts that differ with the game's own
part worker, at most four a second, while the body is shown; part 20 (a cable part that drew stretched polygons on a
made body) is never switched. The body is dressed from the resident double, whose parts are not the porter's: live, 3
of 52 differed, `OUTFIT switched 3 of 3 parts on the body`, and afterwards each body's mask equalled the other porter's.
Both senders wait until a load has been over for 8 s, because right after one the porter shows gear he then hides. The resident double (`peer` mode) is never given gear.
Log lines start with `GEAR`. Tests: `gear_test.cpp` (the porter's tree in fake memory, the plan, the mirror in a fake
world, the empty carrier and its slot lookup; six mutants in `test_game_thread_mutants.py`) and `make
gear-windows-test` (the carrier's critical sections on Windows, with an uninitialised mutant).

## Live test commands (per process, stamp-guarded)

Each command is a file in the mod folder. The mod consumes it and accepts it only for this process
(`<run stamp>` = `<pid>-<process creation time, 16 hex digits>`, the stem of the mod log).

- `puppet.double`: `echo <stamp>` (the double replays the local porter 1 s late, 2 m to the right),
  `lead <stamp>` (3 m ahead and 1.5 m right of the porter, in view of the camera),
  `peer <stamp>` (it shows the received partner), `off <stamp>` (hand it back to the game), `still <stamp>` (keep the
  double's own private-room animation network on the next acquire instead of the porter's). Requires `puppet_probe`.
  `own <stamp>` drives a Sam the mod makes (dress mask 8); `own<hex> <stamp>` (one or two lowercase hex digits, e.g.
  `own10 <stamp>`) sets a research dress mask (`double.h`): 10 the engine's model swap, 20 the swap then the apply if
  needed, 40 a read-only report (`BODY report ...` rows), 80 the game's own in-place art-parts replacement with the
  double's art parts, the call that dresses the resident double (runs alone; `ownc0` adds the report). The body is made once per world, so the mask of the first
  `own` command of a process is the one used. The log confirms it with `DOUBLE own dress mask 0x..`.
- `pad.script`: a virtual gamepad, keyboard and mouse script (`pad.h`), requires `virtual_pad` and `COOP_VIRTUAL_PAD=1`.
  The game never polls the gamepad; the keyboard and mouse steps (`key`, `tap`, `mouse`, `click`) drive it without
  the window focus. The mod's own keys (like, ping) follow the script's keys too.
  `waitui STATE MS` waits until the game shows STATE and goes on at once; when it never does, the script stops and
  the log says `PAD script FAILED: waitui STATE not there after MS ms (UI state: ...)`.
- `puppet.ui` (which screen the game shows, `ui.h`, read-only): with `virtual_pad` on the mod logs `UI state: ...` on
  every change (looked at four times a second): a kind (`gameplay paused menu cutscene loading busy frontend unknown`),
  the game's own state flags by name, the open menu page (`slot:name`, `none`, `unknown`) and `item=unknown` (the
  highlighted entry is not found yet). `state <stamp>` writes the line now, `on <stamp>` / `off <stamp>` switch the
  change log without `virtual_pad`, `dump <stamp>` writes the menu objects for research (game_offsets.md, "Which
  screen is up", with the live checklist; nothing there is confirmed in a running game yet).
- `puppet.marker` (map markers, `marker.h`, OFF until armed; needs `puppet_probe` and a partner mode for the partner
  marker): `on <stamp>` shows the partner as a map pin (group 4) and arms pings on V; `off <stamp>` removes the mod's
  markers; `ping <stamp>` pings at the porter's position (group 3, here and, when connected, in the partner's world as a
  CARGO event of type 5); `key <virtual-key code> <stamp>` changes the ping key (0 = none). Log lines start with `MARKER`.
- `puppet.gear`: `on <stamp>` copies the porter's visible gear onto the own body (`own` mode), `off <stamp>` removes
  it (`gear.h`, the `partner_gear` section above).
- `puppet.body`: `create <stamp>` asks the engine for a second Sam from the double's resource, 3 m to the porter's side,
  and logs what the request produces (`BODY ...` lines); `remove <stamp>` removes it (`body.h`, an experiment).
- `puppet.ride` (lab tests of the partner's vehicle, `ride.h`): `on <stamp>` / `off <stamp>` (off removes it);
  `lead K <stamp>` draws vehicle kind K (1 motorbike, 2 truck) 8 m ahead of this porter as if a partner rode it;
  `park <stamp>` stops the lead vehicle where it is; `rider <stamp>` toggles hiding the partner's body while his vehicle is drawn;
  `goto X Y Z <stamp>` moves this porter with the game's player teleport (game axes; it lands on the topmost surface,
  so indoor targets end up on the roof). `partner_vehicle: true` (requires `puppet_probe`) turns the partner's vehicle
  on from the start. `coop-partner --vehicle KIND ON OFF` makes the stand-in ride from ON to OFF seconds after connecting.
- `puppet.vehicle`: `read <stamp>` logs one read-only vehicle observation; `watch <stamp>` logs at most four a second
  for 60 seconds (240 observations); `off <stamp>` stops it. Requires the ordinary `provider:"game"` and `logging:true`,
  with the pinned `discovery_sha256`. It works with either `self_tick` or the guarded `game_thread` callback and needs
  no vehicle config key, environment variable, `puppet_probe`, or partner. It starts disabled and never creates or moves
  anything. `discovery:true` uses the separate discovery worker and does not process this command.

Vehicle observations are `VEHICLE {json}` rows in the process's mod log. The riding/seat interpretation is provisional
live for a truck (2026-11-08): the porter is a child entity of the vehicle; the static ride-component path was refuted. Expected on foot: `available:true`,
`riding:false`, `kind:"none"`, `seat:"none"`, with null vehicle transform and speed. Seated on a bike: `riding:true`,
`kind:"motorbike"`, `seat:"driver"`, `seat_index:0`, and the **bike's** position/rotation in game axes (z up). Trucks
report `kind:"truck"`; raw seat indices 1..4 select passenger helpers, without an established left/right/front/back
mapping. Unsupported seat values stay `seat:"unknown"`, `seat_index:-1`.

`speed` is null on the first sample, then the magnitude of the vehicle's world-position delta per second
(`speed_source:"position_delta"`), in game world units consistent with metres. A stationary mounted vehicle should
report approximately zero; riding should change its transform and produce positive speed. Pauses over 500 ms,
intervals below 10 ms, clock reversal, an unavailable read, mount/seat/vehicle changes, or a speed above 200 units/s
discard the estimate. Missing/type-mismatched/changing pointers or an invalid transform yield `available:false`,
`riding:null` and a reason, with no stale transform. Unknown is never logged as a successful on-foot observation.
Every row has `provisional:true`; repeated reads cannot establish object lifetime or make a snapshot atomic.

Read-only live check for a verifier: one disposable copy, networking enforced, normal loaded gameplay. With
`logging:true`, write `watch <stamp>` plus a newline to `puppet.vehicle` beside `mod.json` using the current process's
log stamp. Stand for five seconds, mount a bike, wait seated for five seconds, ride/turn/brake for ten seconds,
then dismount and stand for five seconds. Record action times independently and compare the reported vehicle address
and transform with query/read process observations; the porter's transform alone is insufficient. The expected
transitions above, a positive ride speed agreeing with an independent position delta, and a return to on-foot state
are the evidence to collect. A few changing-pointer refusals during mounting are inconclusive; sustained refusal,
on-foot while seated, a wrong kind/seat, or a transform that follows the porter instead of the bike refutes the path.
Send `off <stamp>` to finish early. Repeat for a truck and passenger seat when available. About 30 seconds per vehicle,
60 seconds maximum per watch; no second game copy or stand-in is needed. No live vehicle check has run for this provider.

For example, from native Windows Python, with the current log's run stamp substituted (write LF exactly):

```sh
python -c "from pathlib import Path; p=Path(r'<copy>/redirect-data/mod/puppet.vehicle'); t=p.with_name(p.name+'.tmp'); t.write_bytes(b'watch <stamp>\n'); t.replace(p)"
```

Replace `watch` with `read` or `off` for the other commands. The JSON `t_us` is the monotonic sample time; use it
for independent position-delta comparisons rather than assuming the requested sampling rate was achieved.

`auto_continue: true` in `mod.json` (requires `game_thread`) gets a test copy from the title screen into the last save
without menu input or window focus: on the game thread, every 2 s from 5 s after start, the mod calls the game's
`Application_sExportedContinueLastSave`, which the game refuses whenever a continue is not allowed. It stops for good once
the porter is in the world (`AUTO in game after N s` in the log), so it never loads a save during play. Turning off
"Login Upon Launch" in the copy's game settings (`"AutoLogin" "0"` in `game_settings.cfg`) avoids the online login
attempt, and with it the offline notices of an isolated copy.

The mod log records `POSE available|unavailable reason=...` on every change, so an outside driver can wait for gameplay.

## Session progress and join-at-host (experimental; the move is opt-in)

`progress_mismatch` accepts `"warn"` (default) or `"refuse"`. The handshake carries
the exact wire build and the local progress observation. A known difference
warns once per connection and sets presence flag `ProgressMismatch`; presence
still works. If either side selects refuse, a known mismatch refuses pairing
with `HANDSHAKE refused: story progress mismatch`. Unknown values are explicitly
reported and allow pairing under either policy. This does not synchronize saves.

The read-only provider currently discovers an **episode resource-list index**;
its correspondence to the displayed episode number is unverified. The main-order
progress ID remains unknown. A failed or ambiguous discovery yields unknown,
never a resource default. Game pairing waits for the first available local pose,
then the provider refreshes once a second. Connection logs start with
`HANDSHAKE progress=equal|mismatch|unknown`, name both observed fields, and include
`wire_build`. Metadata is frozen for that connection and reevaluated on reconnect.

`join_at_host` defaults to false and requires `game_thread:true`. The current
gameplay/event/private-room observations also require `puppet_probe:true`; without
that observation backend, the decision stays gated. `role` accepts
`"host"`, `"guest"`, or `"none"` (default); `"client"` remains a guest alias for
existing configs. Without an explicit role, presence uses slot ordering to choose
opposite wire roles; joining stays disabled. An opted-in guest considers the
first connection only, waits for a readable local porter and fresh host presence
in normal gameplay, rejects event/private-room/vehicle states, then consumes the
opportunity. Over 300 m in adapter space, the target is 2 m right of the host at
the host's height. A nearer first observation consumes the opportunity too;
walking away or reconnecting cannot trigger a later join.

With `join_move:"log"` (the default) nothing moves: the verified game callback logs
`JOIN would move local porter to ...; native teleport unavailable; no move performed`
once, without writing Sam's transform or controller.

`join_move:"native"` (needs `join_at_host:true`, and `puppet_probe:true` for the gameplay gates) moves the porter
once with the game's own player teleport: the call the world map's developer "fast travel to crosshair" makes
(`game_offsets.md`, "Moving the porter"). At start the mod checks the code (three pinned prefixes, the call between
them and both class vtables) and logs `JOIN native move armed: code identity verified`, or refuses to start. On
the plan it logs `JOIN moving local porter to ...`, then rechecks right before the call: normal gameplay (no game
event, cutscene, loading or private room), on foot (the vehicle reader; unknown refuses), a readable porter whose
objects are a PlayerGame and a DSPlayerEntity, and a target inside a sanity box (|x|, |z| <= 16 km, height
-500..3000 m in adapter axes; the real world bounds are not known). Any refusal logs
`JOIN native move refused: <reason>; no move performed` and uses up the session's one move. After the call it reads
the porter back: `JOIN moved local porter: readback D m from the target` within 5 m, else
`JOIN readback failed: ...; stopping`. Three seconds later `JOIN settle check: porter holding, ...` or
`JOIN settle check failed: porter dropped D m after the move (ground not streamed in?)`. It never retries and never
moves the porter back. Host tests and mutants cover decisions, the move's gates and readback, the code checks and
the guarded progress reader (`make -C mod join-test`).

For a one-copy live experiment by an authorized operator, retain the normal pinned
config, enforced networking, instance save isolation and fresh proxy probes.
Rebuild/deploy the same identities to the proxy, mod and stand-in. Add:

```json
{"role":"guest","game_thread":true,"self_tick":false,"required":true,
 "puppet_probe":true,"join_at_host":true,"progress_mismatch":"warn"}
```

Keep the other required keys. Start the disposable copy with the reviewed thread
trace environment. The existing per-process command from **Live test commands**
arms the normal partner backend (use the actual log stem for `<run stamp>`):

```text
puppet.double: peer <run stamp>
```

Run the stand-in for that copy's reciprocal slots and shared session (adapter
coordinates are x, Y-up, z):

```sh
coop-partner --slot 1 --peer-slot 0 --role host --session <session> --follow --offset 600 0 0 --seconds 90
```

`--offset X Y Z` offsets the follow target in metres (finite, at most 10 km per
axis); replay mode uses its existing placement options instead. Expect
`HANDSHAKE progress=unknown` or a discovered partial value on both peers, then
one `JOIN would move ... distance ...` over 300 m once the partner is fresh.
Read the local porter position independently before/after: with `join_move:"log"` it must remain at its
original location. The host must stay roughly 600 m away.

Native move (one copy and the stand-in; about 2 minutes of gameplay): add `"join_move":"native"` to the config
above, rebuild and deploy the proxy (it embeds the config parser) and the mod, and run the same stand-in command
(`--offset 600 0 0`, now with `--seconds 120`). Expect, in order: `JOIN native move armed: code identity verified`
at start, `HANDSHAKE ...`, `JOIN moving local porter to ... distance 600.xx m ...`, `JOIN native move called: ...`,
`JOIN moved local porter: readback D m` (D under 5) and `JOIN settle check: porter holding`. Confirm from the
process, not the log: the local pose (presence log or a memory read of the porter entity +c8) jumps by about
600 m and lies within 5 m of the target printed in `JOIN native move called` (game axes), and stays near it. The
stand-in follows with the offset, so it lands 600 m away again: the porter must not jump a second time. The game must keep running (presence log rows keep
coming, no `STALL` line) for 90 s after the move. A `STALL ... in 'join move'` line means the native call faulted:
stop the copy and report it; do not retry. A `readback failed` or `settle check failed` line is a result, not a
fault: record it with the positions.
Expect no JOIN with join disabled, host/none role, private-room/event flags, stale
presence or a near partner, and no second JOIN after reconnect. Allow about 90 s
for loaded gameplay plus 2 s for pairing; title/loading time is additional.

For warning/refusal coverage, restart both peers with fresh nonces and use
`--episode 99 --progress-id unknown` on the stand-in, only after confirming the
copy discovers a different episode index. Warning should log `progress=mismatch`
and keep presence; refusal (`progress_mismatch:"refuse"` or stand-in
`--progress-mismatch refuse`) should log refusal and never pair. When the copy's
progress is unknown, refusal is not expected: use the game-free fixtures to test
known mismatch instead. `--episode N|unknown` and `--progress-id N|unknown` are
explicit stand-in fixtures, not statements about a loaded save. No two-copy run
is needed for these experiments. No live result is claimed here.

## Shared time, rain and environment (opt-in; rain live verified 2026-11-09)

With `share_environment:"host"` (or `"rain"`, the same without ever applying the host's hour) the host's time sample also carries its rain condition (`ds::Environment::rain`), and
a guest places the game's own local rain or clear influence around the host when its world disagrees there
(`weather.h`; `WEATHER ...` log lines). `coop-partner --environment-time H --weather V` plays such a host (V: the map
condition, lower is wetter; -1.8 heavy rain, 1.5 clear).

### Time and environment research

`share_environment` accepts `"off"` (default) or `"host"`. Host mode requires
`provider:"game"`, `game_thread:true`, `self_tick:false`, `required:true`,
`puppet_probe:true` and an explicit `role:"host"` or `role:"guest"` (the existing
`client` alias also works). It uses the existing verified game-thread callback,
whose gate requires enforced networking. Normal loaded gameplay, readable local
state, no event/cutscene and no private-room double use are required. The explicit
role determines authority; slot ordering is insufficient for this feature.

The host reads time and sends an optional environment sample at most every 30 s.
An opted-in guest accepts only a fresh authenticated host sample, independently
reads its own time, checks both complete setter instruction spans and calls the
native `Game_SetTimeOfDay` on that callback at most every 30 s. Reconnects cannot
shorten either rate limit. Changes within 36 s of circular time drift are skipped.
No direct memory writes are used. The native day counter can advance when the
clock is corrected backwards; quest/save effects remain unknown. This feature
is research-only and **does not yet synchronize rain/timefall**, day count or
cycle enablement/rate. Weather determinism and a safe complete setter contract
remain unknown; see `game_offsets.md`, "Time of day and weather".

A read-only stamped command works with either ordinary game ticking mode and
with sharing off. As in **Live test commands**, write exactly this line to the
per-copy `puppet.environment` file beside `mod.json`:

```text
read <run stamp>
```

A stale or malformed command logs `ENVIRONMENT ignored command ...` and does not
request a sample. For example, using native Windows Python and the current
log stem as the stamp (write LF exactly):

```sh
python -c "from pathlib import Path; p=Path(r'<copy>/redirect-data/mod/puppet.environment'); t=p.with_name(p.name+'.tmp'); t.write_bytes(b'read <run stamp>\n'); t.replace(p)"
```

The `ENVIRONMENT {json}` row exposes `available`, `hour`, `cycle`,
`weather_available`, `forecast_time`, `rain_from_map` and refusal reasons.
Unavailable values are null. `rain_from_map` is a signed native map sample, not
a rain percentage; its relation to visible rain/timefall is unverified.
Time and weather are independent guarded observations and are not atomic.
Sharing mode also logs one read per second during eligible gameplay.

One-copy live experiment for an authorized operator (not run for this change):

1. Rebuild/deploy matching proxy, mod and partner binaries; config parser changes
   require a fresh proxy and the normal fresh game-free proxy probes. Retain
   instance save isolation, executable pin, enforced networking and the reviewed
   thread trace setup. Hash original saves before/after and after the settle
   period. Configure the game copy with role guest and `share_environment:"host"`
   plus the required keys above. This callback needs `puppet_probe` observations;
   no double-borrow or body-creation command is needed for time synchronization.
2. In ordinary outdoor gameplay, send `read <stamp>` to `puppet.environment` and
   independently query application/time pointers and `float T+0x20` with a
   read-only process handle. Capture the current sky from the copy. Select a
   fixed hour clearly different from the observed value (21.25 in the example).
3. Run the stand-in on the opposite slots, with the same session as the game:

   ```sh
   coop-partner.exe --slot 1 --peer-slot 0 --role host --session <session> --follow --environment-time 21.25 --seconds 90
   ```

   Adjust slots to the copy's actual config. Expected: stand-in
   `ENVIRONMENT sent fixed hour=21.250000 weather=unknown`; mod
   `ENVIRONMENT host sync enabled: time only; weather setter unavailable`, then
   `ENVIRONMENT applied hour=21.250000 previous=... weather=unknown (time only)`.
   Allow up to 32 s after connection: a first sample can arrive before clock
   estimation and be refused. Independently re-read the time field, compare the
   next environment row and capture the sky at the same location and camera.
4. Continue for 90 s. Assert that the callback continues, input moves the porter
   (independently read positions), and native applies are at least 30 s apart.
   Repeat with a contrasting fixed hour, and repeat once with sharing off: off
   must produce no apply. Logs/screenshots alone do not prove responsiveness or
   state transfer. A signature/local-state refusal, a field that does not change,
   a persistent lighting override, a crash/hang, an apply while gated, or a change
   with sharing off fails or leaves the visual result inconclusive. Rain need not
   change and is not a success criterion for this time-only experiment.

Two real copies are needed later to verify changing host time at the same place;
the fixed stand-in tests the guest setter, not host sampling or matching weather.
Game-free checks: `make -C mod environment-test` (fake memory, guards/mutants,
fixed-time parser/CLI refusals), `make -C core environment-test` (protocol fuzz/mutants).
`make -C mod environment-udp-test` is an additional 33 s two-stand-in wire smoke;
it requires permission to create/bind local UDP sockets and fails if that is denied.

## A private online server (`online_server`, off by default)

`"online_server": "http://ADDRESS:PORT/path"` (game provider and `game_thread`) connects the game's own online layer to
a server the players run (`world/README.md`). The address is numeric and loopback, the partner's (`peer`) or the
listed `server` ("Group play" above); anything else is refused when the config is read, and the proxy's network policy would refuse the connection anyway.
The mod then does three things in the game (`online.h`; addresses in `game_offsets.md`, "Sign-in"):

- points the game's sign-in address string at the server;
- replaces the two functions every gateway body passes through with a pass-through, so bodies are plain JSON instead
  of being encrypted under a key of the publisher's service;
- asks the game's login task to sign in while it is idle (at most five times, 30 s apart): the title menu's login
  message, which makes that call in a normal game, never arrives in a modded copy.

The log says `ONLINE server …`, `ONLINE sign-in asked` and each change of the login task, with `(signed in)` at the
end. Game-free tests: the address rules and the asking logic, five mutants (`online_test.cpp`).

### Received structures built (`build_received`, off by default; built without a game, not run live in the mod)

The game makes another player's structure from the server's answer as a pending Net record and then holds it back:
a bridge, safe house or jump stand is built only while the porter is at least 1 km from it, and every received
structure must pass the game's placement check, which (read statically) holds other players' structures to a wider
spacing than the porter's own. Live (2026-11-17, `game_offsets.md`, "Received structures built"): a served 30 m bridge stayed unbuilt
with the porter 9 m and 1378 m from it, and served rain shelters were taken down within 1 to 3 s.

With `"build_received": true` (needs `online_server`), while the game is signed in to the server the config names,
the mod looks at the construction records on the game thread at every tick (at most 20 times a second) and, for each
record the game has just made from that server's answer, writes one byte: object `+0xaa` gets 0x10 (skip the range
test) and loses 0x20 (skip the placement check). The game then builds it itself. Live, the same byte written by a
reader outside the game made a served bridge stand at once, with the flags of the received bridges the save already
had.

**The trade-off.** The game's own placement check is skipped for received structures, so one may overlap a structure
in your world: one of yours, one of the game's, or one another player built that your save already has. The server
already spreads what it hands a game: two objects of one kind in one answer are at least `spread` metres apart (20
by default; `../world/README.md`, "Which objects a game is handed"). It cannot know what stands in your world.

What is changed: only records owned by another player (Net), in state 0, with `+0xaa` 0x20 set and 0x10 clear, not
marked for removal (`+0xaa` 0x40, `+0xab` bit 0) and with an online id. Left alone: the porter's own (Player) and the
game's (Stage) records, the records of the save (they carry 0x10 already), what the game has refused or built, and
the Net copies the mod makes of a partner's structures (no online id). Each record once, by record id and online id.
The two flag bytes are read again just before the write, and nothing is written if they changed. Nothing without
the option, without `online_server` or before the game is signed in.

Log: `RECEIVED on` (and `RECEIVED off` when the sign-in goes), one `RECEIVED building <kind> <online id> (range and
placement checks skipped): record N, type T, +0xaa 20 -> 10` per record, and `RECEIVED first scan: X ms` once.
`mod_status` has `"received_structures": {on, built, skipped}` (skipped: chosen, then not written because the bytes had changed
or the write failed).

Cost: each tick copies the construction table once (640 slots of 0x710 bytes, about 1.1 MB, the copy the structure
sync makes every 500 ms), reads 0x18 bytes of each record in state 0 and the online id of each candidate. Not
measured in a game; the tick profiler logs the stage `received` when it takes over 1 ms.

Not known yet: whether the tick sees a served rain shelter before the walker refuses it (the tick runs at the message
pump between the game's frames, so it never runs between the answer that makes a record and a walk in that same
frame); whether a structure built this way stays after a load; what the game does with one that overlaps.

Tests: `received_test.cpp` over a fake process memory (the scan, what is changed and left alone, the one byte, once
per record, a slot reused for another object, the bytes changed between the scan and the write, a failed write),
mutants OPTION, SERVER, BUILT, PLAYER, SAVED, REMOVAL, ONLINE, ONCE, RECHECK (`test_game_thread_mutants.py
received`); the config in `unit.cpp` (mutant CONFIG_RECEIVED).

### The player's own strand (`own_strand.h`; a reader, not wired into the mod)

A read-only reader for what in a game's memory is the player's own (the launcher's "My strand" and a live probe;
`game_offsets.md`, "The player's own strand"): construction records whose owner byte says Player variant (built in
this save, or handed back by a server as his), and online objects whose creator is the signed-in player (the
game's user id 1, filed in an area's second list), joined by online id into one list with the server's letter, the
area, the position and the level. Likes are not read from the game (where it keeps them is unknown); a server's
counts are merged in by online id. Static research only: nothing of it has been read from a running game yet. Its
structure reader also gives each object's flag bytes (`+0xa8..+0xab`) and can skip records early; `build_received`
scans with it (above). Game-free: `own_strand_test.cpp` over a fake process memory, eleven mutants (family `own` in
`test_game_thread_mutants.py`).

## The partner is down or caught (`vitals.h`, with `notices`)

Every tenth of a second the mod reads, read-only, whether its porter is dead (the entity's dead bit or the animation
control `Is_Dead`) or held by BTs (`Is_Drag`, `Int_Is_GazerCatch`), with the game's own predicate for a control byte
(`game_offsets.md`, "Death, BTs and voidouts for the partner"). A state that lasts 0.4 s sets the presence flag Down
or Caught (dead wins); it is dropped a second after the state ends. A porter that cannot be read is unknown, never
dead. With `notices` the partner's info log tells it once when it starts and once when it is over ("is down", "is
back on his feet", "has been caught by BTs", "got free"). The log line `VITALS known= dead= caught= crouch=` appears
on every change. While the partner is down his body is laid on its back where he fell (`puppet.h` `laidDown`: the body's transform, lifted 18 cm; the game's own death controls do not move the body) and stands again when he is up; the log says `BODY laid down` / `BODY upright again`. The stand-in partner reports the states with `--down ON OFF` and `--caught ON OFF`.
A call-out (the shout key) is read the same way from two upper-body animation values and sent as the presence flag
Call, kept for 1.5 s so a short call still arrives; with `notices` the partner reads "is calling out" (once per call,
at most every 8 s). The log says `CALL the porter calls out` / `CALL over`.
Game-free: `vitals_test.cpp` (five mutants), the notices in `notice_test.cpp` (three more mutants), the flags in the
core's tests.

