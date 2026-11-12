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
the config. Tests: `notice_test.cpp` (mutants BOUND, LEFT, ALONE_ONCE, NAME_WAIT, WORLD, GAP, AWAY_SETTLE, BACK_PAIRED, LIKE_ICON, JOIN_HOLD). The launcher's play
setup turns it on.

## Shared MULEs, BTs, orders and roads (`share_enemies`, `share_orders`, `share_order_units`, `share_roads`; off by default)

Each game keeps simulating its own world; these settings share the outcome of what a player does in it. They need
`game_thread` and a partner; the launcher's play setup turns the first two on.

- `"share_enemies": true` (`enemies.h`): a MULE one player puts down (knocked out, fainted, asleep, dead) is put
  down in the partner's game with the game's own defeat function, and a camp that comes to know someone is there is
  alarmed in the partner's game. MULEs are matched by the UUID of their locator, which is the same in every game.
  Where MULEs are and who they chase is not shared.
  The same setting shares BTs (`bts.h`): a BT area is active for both players through the shared rain
  (`share_environment`); a BT one player destroys is gone in the partner's game; when one player is pulled into a
  boss fight, it starts for a partner within 300 m (and for one who comes near while it runs); and when either
  player's fight ends with the area cleared, the partner's ends the same way. The two games' bosses share one health
  bar: the life a player takes off its own boss is taken off the partner's (in percent of full life, so different
  difficulties compare). The mod never takes a boss to zero itself: the game only lets a boss die on a real hit, so
  the player who lands the last hit sees the death and the partner's fight ends with the cleared area. Each game
  still runs its own BTs: where they drift, whom they notice and what the boss does is not shared.
- `"share_roads": true` (`roads.h`; on in the launcher's setup): what a player delivers to an auto-paver is counted
  at the partner's paver as delivered by others, through the game's own routine for online contributions, and a road
  one player completes is completed for the partner, so both see it built. Pavers are matched by the game's own
  construction id. Only what happens while the two are connected is shared: roads one player's save already has are
  not copied to the other. The partner's cargo is not touched and the partner gets no likes for it.
- `"share_orders": true` (`orders.h`): a standard order (the repeatable deliveries and collections) one player takes
  on, completes or gives up does the same in the partner's game, through the game's own mission requests. Main and
  sub orders are never shared. Each game has its own cargo for the order; whoever hands it in completes it for both.
- `"share_order_units": true` (with `share_orders`; also on in the launcher's setup): a cargo unit one player hands
  in is delivered in the partner's game too and the partner's copy of that cargo is removed, so two players can
  each carry part of an order.

Log lines: `ENEMY ...`, `ORDER ...`. Test commands in `puppet.cargo` (lab only): `mule <kind>`, `order <id>`,
`unit <id*10+n>`, `unitlose <id*10+n>`; in `puppet.ride`: `place X Y Z HEADING`.

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

## Likes for the partner (`partner_likes`, off by default)

With `"partner_likes": true` (requires `puppet_probe`) pressing **L** (free in the game's default keys) while the partner
is connected and no game event runs sends the partner one like, at most one per 1.5 s (`likes.h`, a CARGO event of
type 9). The partner's mod gives its porter that like the game's own way, `DSLikeCommand_sReceiveLike` with reason
Other and no sender, which shows the game's like counter popup; it gives at most one a second and ten a minute whatever
arrives. These are the game's real likes: they are saved and count toward the porter's evaluation and the like trophies,
which is why the feature is off unless asked for. Log lines start with `LIKE` (`sent to the partner`, `received from the
partner`, `dropped: ...`). Tests: `likes_test.cpp` (mutants GAP and CAP); live, two copies: one L press raised the
other porter's likes by one with the counter popup.

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
a server the players run (`world/README.md`). The address is numeric and either loopback or the partner's (`peer`);
anything else is refused when the config is read, and the proxy's network policy would refuse the connection anyway.
The mod then does three things in the game (`online.h`; addresses in `game_offsets.md`, "Sign-in"):

- points the game's sign-in address string at the server;
- replaces the two functions every gateway body passes through with a pass-through, so bodies are plain JSON instead
  of being encrypted under a key of the publisher's service;
- asks the game's login task to sign in while it is idle (at most five times, 30 s apart): the title menu's login
  message, which makes that call in a normal game, never arrives in a modded copy.

The log says `ONLINE server …`, `ONLINE sign-in asked` and each change of the login task, with `(signed in)` at the
end. Game-free tests: the address rules and the asking logic, five mutants (`online_test.cpp`).

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

