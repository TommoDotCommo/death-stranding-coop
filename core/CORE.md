# Design A presence core (T0, protocol 1)

Each legitimate, offline client owns its world. The core sends only the local
player's presentation state and samples a remote puppet. It does not replicate
orders, inventory, cargo entities, combat, AI, vehicles or save data. No game
headers, addresses, assets, sockets or Windows APIs are in this core. No live
compatibility is established by these tests. The lab research gate stays closed.

## Build and reproduce

```sh
make -C games/death-stranding/core test mingw
ASAN_OPTIONS=detect_leaks=0 make -C games/death-stranding/core sanitize
# For another hub checkout:
make -C games/death-stranding/core HUB=/path/to/coop-anything test
```

C++17 and Python 3 are required; cross-compilation requires the existing
`x86_64-w64-mingw32-g++`. `mingw` produces an AMD64 COFF object, not a runnable
Windows test. No installation or Windows execution is involved. In the traced worker environment, LeakSanitizer
fails its process inspection; the sanitizer command above disables leak checking
only. AddressSanitizer and UndefinedBehaviorSanitizer remain enabled. The hub's
`tools/fuzz/src/net.cpp`, `net.h` and `rng.h` supply the shared link model and RNG
through `HUB`; game-specific Witness/MDK2 code is not copied. Witness's injected
world/transport/clock and named bug-twin oracles, and MDK2's nonce/generation and
pose interpolation design informed the boundary.

`build_id.py` pins the first 16 bytes of SHA-256 over `core.h`, then `core.cpp`,
each prefixed by UTF-8 filename, NUL and an eight-byte LE content length. The
committed generated `build_id.h` is checked on every normal test/cross build.
After reviewing a production source change, run `python3 build_id.py --write`
from this directory. Before an in-process build, extend this source manifest to
cover the adapter, puppet implementation, configuration schema and relevant build
options too; this T0 hash identifies this core, not an as-yet nonexistent DLL.
The compiler and dependencies are not attested. The hash is a compatibility pin,
not authentication or proof of a trusted binary.

## Wire format

All eleven packet types have the same 128-byte header and are one UDP datagram each; all but ANIMATION and ENEMIES are **exactly 128 bytes**. Integers
are explicitly little endian; signed values use two's complement. No packed
structs, float bit patterns, pointer casts or native padding are on the wire.
Reserved bytes must be zero. Fields unused by a packet type are ignored after
common validation; the encoder fills them with default values (including health
and stamina 255). Receivers do not require ignored fields to be zero.

| Offset | Bytes | Field and allowed range |
|---|---:|---|
| 0 | 4 | Magic ASCII `DSP1` (`0x31505344`) |
| 4 | 2 | Protocol version, exactly 1 |
| 6 | 1 | Kind: HELLO=1, HELLO_ACK=2, PRESENCE=3, PING=4, PONG=5, BYE=6, CARGO=7, CARGO_ACK=8, ENVIRONMENT=9 (optional), ANIMATION=10 (optional, longer), ENEMIES=11 (optional, longer) |
| 7 | 1 | Sender role: host=1, client=2; peer must have opposite role |
| 8 | 2 | Datagram length, exactly 128 (ANIMATION and ENEMIES: 128 plus the payload, at most 1252); actual received length must match; the checksum covers the whole datagram |
| 10 | 2 | Reserved zero |
| 12 | 4 | Steam game build, exactly **13300582** |
| 16 | 16 | Exact `ModBuild` source hash in `build_id.h` |
| 32 | 8 | Nonzero shared session ID; must equal configured session |
| 40 | 8 | Nonzero sender connection nonce |
| 48 | 8 | Recipient nonce echo; zero on HELLO, exact recipient nonce otherwise |
| 56 | 4 | PRESENCE sequence, modulo 2^32; first value arbitrary, subsequent delta 1..2^31-1 |
| 60 | 2 | State flags, bits 0..5 only (below) |
| 62 | 1 | Adapter action ID 0..31; **not** a game memory ID |
| 63 | 1 | Normalised animation phase, 0..255 represents 0..1 inclusive |
| 64 | 8 | Sender monotonic timestamp in milliseconds, 0..2^53 |
| 72 | 12 | x,y,z: three signed i32 centimetres, each -10,000,000..10,000,000 |
| 84 | 6 | vx,vy,vz: three signed i16 centimetres/second, each -10,000..10,000; vector speed <=100.02 m/s after quantisation |
| 90 | 2 | Signed i16 yaw, -32767..32767 maps to -pi..pi; -32768 rejected |
| 92 | 1 | Normalised health, 0..255 |
| 93 | 1 | Normalised stamina, 0..255 |
| 94 | 1 | BYE reason 0=normal, 1=local shutdown, 2=incompatible, 3=adapter unavailable |
| 95 | 1 | Reserved zero |
| 96 | 8 | PING/PONG t0, initiator's send time, ms, 0..2^53 |
| 104 | 8 | PONG t1, responder's receive time, ms, 0..2^53 |
| 112 | 8 | PONG t2, responder's send time, ms, 0..2^53 |
| 120 | 4 | FNV-1a 32-bit checksum over all 128 bytes, treating 120..123 as zero |
| 124 | 4 | Reserved zero |

Position envelope is **+/-100 km on each axis, 1 cm resolution**. This is a
conservative protocol envelope for kilometre-scale regions, not a measured
Decima coordinate limit. Adapter convention is metres, Y up, yaw radians around
Y. Actual game axes, units, origins and cross-region compatibility are UNKNOWN.
Convert/validate before supplying state; do not reinterpret raw engine floats.
Velocity envelope allows vehicles up to 100 m/s; tune only with live evidence
and a compatible protocol/build change. Local finite position/velocity, phase,
flags and action inputs are clamped; velocity magnitude is capped at 100 m/s;
yaw wraps. Any NaN/infinity drops the entire local sample. Incoming values outside
the envelope are dropped, never silently clamped into a plausible remote pose.
Health/stamina are inherently bounded bytes; mapping from game maxima is adapter work.

Flags: bit 0 sprinting, 1 crouching, 2 carrying cargo, 3 in vehicle, 4 in private
room, 5 explicit teleport. Higher bits are invalid. Teleport authorises a position
discontinuity; first teleport is accepted, then at most one every **5 seconds**
by receiver time. An ordinary position delta cannot exceed `100 m/s * min(sender
elapsed, 2 s) + 0.5 m`. The initial sample establishes a baseline. Teleport flags
must describe actual discontinuities, not remain set on every tick. Lost teleport
announcements are not made reliable: the adapter must reannounce a failed
relocation after the cooldown or explicitly reconnect with a fresh nonce; remote
motion may freeze until recovery. This is preferable to an unflagged world jump.

## Pairing, clocks, transport and lifecycle

Create `Core(Config, StateProvider, Transport)` with opposite roles, the same
nonzero random out-of-band session ID, and different random nonzero initial
nonces. Never reuse a session/nonce tuple across processes. Inject monotonic
milliseconds into **all** calls, from a single serialised owner thread. Different
clock origins are supported; clock discontinuities/drift are not. Restrict clocks
to 2^53 milliseconds. The adapter must queue networking onto the game-safe thread,
validate buffer ownership, and filter the configured source endpoint before
`receive`; no network discovery or untrusted peer admission is implemented.

HELLO retries every 250 ms. A valid opposite-role HELLO receives HELLO_ACK echoing
the sender's nonce. Only an ACK to a previously sent HELLO with the current
recipient echo establishes pairing. Duplicate HELLO/ACK does not clear snapshots
or extend liveness. A different nonce cannot replace a connected peer; timeout or
explicit disconnect is required. Build/version/session rejection happens before
pairing. A connected session with nothing accepted for 3 s times out and starts
again with a new nonce.

**Times out of order.** `tick`, `receive` and `frame` take the caller's monotonic
milliseconds, and the caller should give every call of one pass the same reading.
The core does not depend on it: a `tick` time earlier than the last receive's is
no silence (the session stays; the 3 s still count from that receive), and a
teleport received at a time earlier than the last teleport's is inside its 5 s.
Before this the differences were unsigned, so one datagram stamped a millisecond
after the tick's reading ended the session at once (seen live from an adapter that
read the clock twice in a pass; mutant `clockOrder`). The other time comparisons
(render age, delay slew, PONG, environment and enemy rates) were already ordered.
Nothing on the wire changed.

`receive` returns a typed error and `reason(error)` supplies clear text,
including `game/mod build mismatch`, `protocol version mismatch` and `wrong
session`. Surface these in the adapter UI/log, rather than reporting only timeout.
No rejection amplification packets are sent for malformed or mismatched data.

PING once per second carries t0. PONG echoes it with t1/t2. Match a single
outstanding ping; reject replays, negative processing duration or RTT, or RTT
above 2 s. RTT = `(t3-t0)-(t2-t1)`; remote-minus-local offset =
`((t1-t0)+(t2-t3))/2`. First matched sample anchors the remote clock; later samples
update RTT but keep the timeline stable. Before that, presence uses arrival time.
The timestamp must increase with accepted presence; after anchoring it must map
within +/-2 seconds of arrival and later than the previous buffered sample.
Asymmetric delay biases offset; it is an estimate, not synchronised wall time.
If a clock jumps, explicitly reconnect. Reconnect resets offset, sequence, buffer,
teleport history and local send state, and increments the local nonce (overflow
fails closed). Persistent reordering cannot grow memory: the buffer holds 64
accepted snapshots. Duplicate/older sequences never refresh state/liveness.

Connected sends are at most **20 Hz**, on quantised-significant change, with a
500 ms unchanged-state refresh. Read failures and cutscene/loading suppress
PRESENCE, while handshake and ping traffic continue. No stale local transform is
sent. A silent peer times out after 3 seconds without accepted PRESENCE or matched
PONG; PING replays cannot extend the session. Timeout retries pairing with a fresh
nonce. BYE immediately clears state and rotates nonce; explicit `disconnect`
sends it once. Repeated BYE/old-session traffic cannot affect the new connection.
No reliable delivery or close acknowledgement is promised for BYE.

UDP port is `portForSlot(slot) = 49000 + slot`, slot **0..15** (invalid slots
throw), range **49000..49015**. Each instance binds its assigned slot's port and
uses the partner slot's derived port. This is distinct from Witness 47000+slot
and MDK2 27900+16*slot. No fixed peer port or lab allocation/runtime adapter is
enabled here.

## Frame sampling and safety boundary

`sample(buffer, now, delayMs, extrapolateMs)` is pure; `Core::render` uses it with
a configurable 0..1000 ms delay (default 120) and a 150 ms extrapolation cap.
Between eligible snapshots interpolate position and velocity, shortest-path yaw,
and cyclic phase for unchanged action. Discrete flags/actions come from the earlier
sample. Height blends on slopes and steps (at most 1 m per interval and 8 m/s
vertical); a larger or faster height change (ledge, fall, lift), a private-room
boundary, a >8 m gap or a teleport **holds the old pose then snaps at the new
sample time**, setting `snapped`. (Until 2026-11-02 any height change over 5 mm
snapped: 25-30 % of live samples on outdoor terrain.) There is no vertical
extrapolation. Horizontal dead reckoning is bounded at
150 ms then freezes at that endpoint; no velocity integration accumulates across
frames. Inputs are validated snapshots from the core, not arbitrary game buffers.
After 500 ms without a pose mark stale; after 1500 ms hide the puppet. Thus a
partner entering a cutscene/loading freezes then disappears despite live PONGs.
A private-room flag hides immediately. Local loading/cutscene also hides the
received puppet in `frame`. After resume, require a valid transform and an
explicit teleport when changing location/region.

The driver must apply sampled positions directly; do not add a second smoothing
filter that sweeps through a floor or teleport. This prevents the **sampler** from
blending between different heights. It does not know collision geometry, world
streaming, bridges, support surfaces or region identity. Before showing a puppet,
the adapter must verify a compatible loaded region and valid support/placement;
hide on uncertainty. T0 cannot prove collision-safe spawn/placement in Decima.
The puppet must be visual-only: no gameplay collision, damage, inventory access,
AI authority or save effects.

Malformed packets cannot invoke game functions, address arbitrary memory, select
arbitrary animation IDs, allocate unbounded snapshots, carry game files or mutate
world/save state through this API. Checks bound state values, ordering and time;
FNV catches accidental corruption. **This is not an authenticated protocol**:
an attacker able to send from the accepted endpoint and learn/spoof the session
and nonces can forge checksum/build pins, impersonate a peer, move its visual
puppet within the envelope, consume CPU, send BYE or deny service. A malicious
valid teleport can place it anywhere within the envelope once per cooldown.
Do not expose this transport on the internet. The adapter needs per-frame receive
budgets and authenticated transport for hostile networks. T0 does not prove the
adapter safe against incorrect memory pointers or unsafe animation/spawn calls.

## Tests, mutants and next live steps

`tests/test.cpp` has a fake provider, parser/clamp/boundary tests, lifecycle/clock
and sampler cases, and 500 fixed seeds (1..500). It reuses the hub's lossy link:
20..150 ms uniform one-way delay (therefore reordering), 10% datagram loss, 5%
duplication, and a 3.5 s cable outage from t=6..9.5 s. Clock origins differ by
10,007..13,500 ms. Both players follow bounded smooth horizontal paths for 12 s,
then stop; every session lasts 16 s in 10 ms steps. Metrics exclude handshake and
outage/recovery warmup: t=2.5..5.89 and 11.5..11.99 s. Compare both rendered poses
against the analytic trajectory at **now-120 ms**, not against current position.
The deliberate presentation delay is additional current-time lag (up to about
0.9 m on these paths). Assert every sampled error <2 m, p99 <0.8 m, final error
<sqrt(.001) m, pairing restored, and clock/RTT estimates in bounds. Another
150,000 random datagrams and 150,000 checksum-correct structured byte mutations
exercise parsing; sanitizer builds run the same workload.

`tests/run.py` runs clean, then each mutant independently and requires exit 1
**and its named oracle**, so a crash or unrelated failure is insufficient. Mutants
cover version, range validation, teleport cooldown, duplicate rejection,
interpolation delay, session, build, magic, checksum, reserved fields, nonce,
timestamp, motion speed, floor snapping, extrapolation cap, staleness, provider
gating, timeout, buffer bound, send rate, lengths, kinds, roles, handshake and
PONG replay, local finite clamps and NaN/infinity rejection, and times out of
order (`clockOrder`). Mutants are injected only through `DS_TESTING` APIs; production core
has no public protection-disabling API.

Measured on this Linux worker: **500/500 sessions, 27/27 mutants killed**;
390,000 pose samples: p50 **0.092159 m**, p95 **0.276197 m**, p99 **0.373698 m**,
max **1.058198 m**, final stopped-pose maximum **0.006430 m**. Clean simulation
runtime 0.499 s; full normal suite including mutants 0.534 s; ASan/UBSan suite
3.666 s (all exclude compilation). Loss model counted 19,465 lost and 8,705
duplicated datagrams. MinGW produced an AMD64 COFF object. The existing safety
fixture suite also passed 56 tests in 67.081 s. Initial iterations caught a yaw
boundary acceptance and an unconfigured handshake mutant; both were corrected
before these final runs. Default LeakSanitizer failed under tracing; ASan/UBSan
passed with `ASAN_OPTIONS=detect_leaks=0`. No leak result or live result is claimed.

Exact in-process next steps (all remain UNKNOWN until authorised inspection):

1. Recheck pinned build/executable identity and all existing copy/save/Cloud gates;
   do not infer legitimate two-client Steam startup from this object compilation.
2. Locate the **real local player**, not camera or remote puppet. Establish safe
   lifecycle/generation checks, game thread, transform axes/origin/units, yaw,
   velocity, health/stamina maxima, and cutscene/loading/private-room signals.
   Map at most 32 safe local action states to this adapter's IDs and recover a
   normalised animation phase. No addresses or signatures are supplied here.
3. Implement `StateProvider::readLocal` and gating. Check every read and lifetime;
   convert units, return false while unavailable. Establish region/support checks
   and actual teleport/loading transitions; decide reliable relocation recovery.
4. Discover a legitimate visual actor creation/destruction API and ownership.
   Start with a marker if secondary skeletal actor spawning is unsafe. Implement
   `applyRemote` only for the owned cosmetic actor: transform/yaw, safe animation
   mapping, phase, flags and visibility. Never write the real player's state,
   shared inventory, cargo, mission progress or saves. Vehicle/cargo flags are
   presentation hints, not replicated entities. Prove cleanup across loads.
5. Implement a bounded nonblocking datagram adapter with slot-derived ports,
   configured peer endpoint, fresh entropy, visible error reasons and serialised
   core calls. Extend the mod hash manifest to the complete DLL before pairing.
6. Only after runtime authorisation: T1 marker/lifecycle tests, then isolated T2
   two-client local movement and measured puppet error, cutscene/private-room,
   vertical transition, teleport, loss, disconnect/rejoin and cleanup tests.
   Validate memory state through bridge dumps; no screenshots-only assertions.

Live-only questions: actual Decima coordinate range/region identity; safe actor
spawning and main-thread callback; preventing physics/AI/save effects; reliable
animation/phase reads and writes; floor/support/streaming detection; Steam same-user
concurrency and real game compatibility with save redirection; two-instance frame
rate and memory/VRAM limits. No network research claims or copied game addresses
are treated as an implementation contract.

## Cargo events (CARGO=7, CARGO_ACK=8)

Shared-world cargo travels as events on the same 128-byte layout, after the handshake, with the usual session,
nonce and build checks:

| Field | CARGO | CARGO_ACK |
|---|---|---|
| 56 sequence | event number, 1, 2, 3... per connection | the event number acknowledged |
| 62 action | event type 1..6 (1 = cargo placed in the world, 2 = shared cargo taken: the partner removes its copy, 3 = structure built, 4 = structure dismantled, 5 = ping: a map marker at `position`, `t2` = the ping's id, `t0` = 0) | ignored |
| 72 position, 90 yaw | where the cargo is, adapter axes | ignored |
| 96 t0 | the game's cargo item hash, 0..2^32-1 | ignored |
| 104 t1 | nonzero event id chosen by the sender, stable across reconnections, below 2^53 | ignored |
| 112 t2 | the shared cargo's id (the same in both worlds, chosen by whoever placed it), below 2^53; 0 = none | ignored |

Delivery is reliable, ordered and at most once per connection. The sender repeats the oldest unacknowledged event
every 200 ms. The receiver takes only the next sequence and acknowledges it, and acknowledges any repeat of an older
one again (its earlier acknowledgement may have been lost). A gap is neither delivered nor acknowledged. On a new
connection both sides number from 1 again, and undelivered events are kept and renumbered. The receiver delivers
each event id only once for the lifetime of the connection object (it remembers the last 4096 ids), so an event
whose acknowledgement was lost just before a reconnection is not delivered twice (mutant `cargoOnce`). Mutants `cargoOrder` and
`cargoAck` remove ordering and acknowledgement matching. The lossy-link fuzz (500 seeds, 10% loss, 5% duplicates,
an outage and reconnect) checks that every event is delivered exactly once and in order.

Like every t-field, t1 and t2 must stay below 2^53 or the receiver refuses the frame as an invalid time. The core's own
event ids are 33 bits mixed from the nonce above a 20-bit counter; the first version shifted the whole 64-bit nonce,
which overflowed for any real (random) nonce, so live cargo events were refused (test `cargo_id_fits_random_nonce`).

## Story-progress handshake extension

HELLO and HELLO_ACK overlay the otherwise unused clock payload at offsets 96..119:
96..99 is a little-endian u32 episode resource index, 100..103 a u32 progress ID,
104 is the sender's `progress_mismatch` policy (0 warn, 1 refuse), and 105..119
must be zero. `0xffffffff` means unknown independently for either field. These
are descriptive observations; they never replicate or modify story state. The
existing game/build identity at 12..31 remains mandatory. Changed peers require
the regenerated, identical adapter identity even though the fixed 128-byte frame
and protocol version stay unchanged.

A known difference in either comparable field is a mismatch. Fully known equal
fields are equal; all other cases are unknown. Unknown is never inferred equal
and never refused. If either peer requests refusal, a known mismatch rejects
both HELLO and ACK before pairing. Otherwise presence, clocks and reliable cargo
remain available. `Core::progressStatus`, `localProgress`, and `peerProgress`
expose the connection's handshake snapshot; presence bit 6 (`ProgressMismatch`)
and rendered state mark a warning. Allowed state bits are now 0..6. The receiver
computes its rendered warning from the handshake rather than trusting a peer's
flag. Duplicate ACKs neither change metadata nor create another connection.
Reconnect clears metadata, reevaluates progress and increments `connectionSerial`.

Providers may defer initial pairing with `readyForSession`. The game adapter waits
for its first readable local pose and bounded progress observation, so a handshake
from the title screen cannot accidentally stand in for the loaded save. A failed
progress discovery is an explicit unknown observation and permits pairing. The
adapter refreshes observations once a second; the accepted handshake snapshot
stays fixed until reconnect. No cross-region compatibility is implied.

The core suite adds progress parser/roundtrip, bilateral policy, partial/unknown,
warning-flag, readiness, duplicate-ACK and reset checks. The 500 seeded lossy-link
sessions include equal, mismatched and unknown progress through timeout/reconnect;
named mutants remove readiness, refusal, peer-policy enforcement, unknown handling
and warning flags. This is game-free protocol evidence, not validation of a save.

## Optional environment samples (kind 9)

`ENVIRONMENT=9` is a separate 128-byte message. HELLO/ACK and their metadata are
unchanged. The production source identities change as usual; deploy the same
adapter identity to both peers and the stand-in. This is an optional latest-value
sample, with no acknowledgement, retransmit queue or cargo sequence dependency.

The usual game/mod build, session, source endpoint and handshake/nonce checks
apply. Only the host sends; only the guest receives. `sequence` at bytes 56..59
has its own strictly advancing nonzero serial, `stamp` at 64..71 is the sender's
monotonic milliseconds, and `t0` at 96..103 is integer milliseconds since midnight
in `[0,86400000)`. `t1`/`t2` and presence/cargo fields are reserved/default
(position/velocity/yaw/flags/action/phase/reason zero; health/stamina 255).
Weather is unknown and has no payload. The host rate is at most one sample per
30 seconds, including reconnects; a lost sample waits for the next period.

The guest requires an established ping clock mapping, a timestamp within 2 s of
its mapped local time, a fresh sequence and a strictly advancing remote stamp.
`latestEnvironment` expires after 2 s and resets on disconnect. Optional samples
never refresh connection liveness and cannot hold a dead presence session open.
The adapter independently controls native-call eligibility and retains its
30-second apply limit across reconnects. No extrapolation of time or weather is
performed. A first sample can precede clock estimation and be refused; allow the
next period before diagnosing a failure.

`make -C core environment-test` runs roundtrip/reserved/range/authority/nonce,
duplicate/reorder, age, disconnect and rate checks, five named mutants, and 200
lossy simulated sessions with independent clocks, outage/reconnect and a final
quiet period. Structured environment payloads exercise the new decoder branch.
This is game-free wire evidence, not proof of a visible sky change.

## Optional enemy poses (kind 11)

`ENEMIES=11` says where the host's game has its enemies, so the guest's adapter can put its own in the same place.
After the header: a count (at most 12) and per enemy 22 bytes: a 56-bit identity (the adapter's, nonzero, below 2^52,
strictly increasing within a sample), the position as three int32 in centimetres (the game's own axes, at most
100 km), the heading as int16 (pi / 32767) and a byte of adapter flags (the low four bits). Header bytes 96..119 are
zero. The datagram's length is exactly 129 + 22 x count.

It is unreliable on purpose: nothing is repeated and nothing acknowledged. Only a connected host sends
(`sendEnemies`, at most every 100 ms; a sample the wire cannot carry is refused). Only a guest accepts, and only
from the host; a sample that is not newer than the last one (sequence and stamp) is refused, as is one whose stamp
does not map to within 2 s of the receiver's clock. `latestEnemies` gives the newest sample with its age and nothing
once it is over a second old or after a reconnection, so a lost or late datagram never moves anything to an old
place. It does not count as presence traffic. Guards and their mutants: `enemiesRange`, `enemiesRole`,
`enemiesOrder`, `enemiesFresh`, `enemiesStale`, `enemiesRate` (`tests/enemies.cpp`, with a 200-seed lossy-link fuzz:
loss, duplicates, corruption, reordering, an outage, independent clocks).

## Optional animation samples (kind 10)

`ANIMATION=10` carries the porter's animation controls (the adapter's numbering;
in this mod `mod/controls.h`) on the presence timeline. It is the one packet longer
than 128 bytes: the usual header (bytes 96..119 reserved zero), then
`[u8 n][held: n bytes][rose: n bytes][u8 m][m x (u8 index, f32 little endian)][u8 k][k x (u32 item, u8 tag,
3 x i16 millimetres, u48 rotation)][u8 j][u16 skeleton, when j > 0][j x (u16 index, 4 x i16 rotation, 3 x i16
millimetres)][u8 c][c x (3 x i32 millimetres, u48 rotation, u8 flags)]`; `k <= 2` cases held in the hands, relative
to the porter's root, item nonzero and each coordinate within 3 m (`tag` and the rotation packing belong to the
adapter); `j <= 48` joints of the vehicle he drives, indices strictly increasing and below `skeleton`; `c <= 2`
floating carriers he tows, as offsets from his root in game axes, each coordinate within 100 m.
`n <= 32` bytes of bool bits; `held` is each bool when sampled, `rose` marks a bool
that went from off to on at any frame since the previous sample (a one-frame
pulse shows only there). Floats are sparse: zero is left out, indices strictly
increase and stay below 192, values are finite with `|v| <= 1e6` and nonzero. The
length must match exactly; at most 1252 bytes, under a 1280-byte path MTU.

Either side sends while connected and not gated: at most every 50 ms, an unchanged
sample at most every 500 ms (`sendAnimation` returns whether it went, so the
caller starts gathering `rose` again). The stamp is the local pose sample time.
Receivers require the ping clock mapping, a stamp within 2 s of the mapped local
time, a fresh sequence and an advancing stamp, and keep 64 samples.
`takeAnimation` returns the sample due at the presence delay, with `rose` gathered
over every sample passed since the previous take (each pulse is given once), and
nothing when none is due or the due one is over a second old. When the sample after it has arrived it comes too
(`between`, `next`, `fraction` 0..1 towards it), for the adapter to blend continuous values. Samples never
refresh connection liveness; disconnect clears them.

`make -C core animation-test` runs roundtrip, full and empty payloads, length,
oversize, checksum, reserved, range (NaN, infinity, magnitude, zero, index,
order), sessions (delay, pulse once, rates, ordering, freshness, nonce, reset)
joints, carts and 100 lossy simulated sessions, with three named mutants (`animationRange`,
`animationOrder`, `animationRose`).

## Several peers (one core per pair)

The core stays what it is: one configured peer, opposite roles, one shared session number. More than two players are
handled outside it, by running one `Core` per other player (`mod/others.h`). Nothing in `core.h`, `core.cpp` or the
wire format changed for this, so a pair of the two-player layer and a pair of others speak the same protocol.

What the adapter chooses per pair:

- **Role.** Every player has a 64-bit id on whatever carries the datagrams. In a pair the lower id is `Host`, the
  higher `Client` (`pairRole`). The role only satisfies the core's rule that the two ends differ; the host-only
  packets (ENVIRONMENT, ENEMIES) are never sent between others and are refused before the core if they arrive.
- **Session.** `pairSession(session, a, b)`: the group's configured session number mixed with both ids (order does
  not matter), never zero. Both ends compute the same value, and every pair has its own, so a datagram of one pair
  handed to another pair's core is refused as `wrong session`. Like the checksum and the build pin this is a
  consistency check, not authentication.
- **Nonce.** Fresh from the OS for every session object, as before.
- **Story progress.** Each player has an own save, so the handshake carries `unknown` progress and the `warn` policy:
  the core never warns and never refuses between others.
- **Build identity.** As in the two-player adapter: the adapter's wider identity travels in bytes 16..31, is checked
  on arrival, and the core is given its own pin.
- **Reliable events (CARGO).** The name (type 10) is always sent and taken. With the mod's opt-in settings for what
  is only shown, three more types are sent and taken, the same events the two-player layer uses: with `others_gear`
  type 7 (the gear a player wears), with `others_carry` also types 6 (what a player carries) and 16 (where each
  carried case sits). Every other type that arrives, and those three without their setting, is acknowledged by the
  core, so the sender's ordered stream does not stop, and then dropped by the adapter: cargo put down or picked up,
  structures, enemies, orders, vehicles, story and everything else that would change this world is never taken from
  another player.
- **Teleport flag.** The flag must reach every peer once. Each session keeps it until a PRESENCE carrying it has left
  for that peer; a flag left set would run into the receiver's five-second teleport limit.

Routing is the carrier's work, not the core's: it hands the adapter each datagram with the sender's id, and the
adapter gives it to that id's core and to no other. A datagram from an id that is not present is dropped.

Costs to know: every pair has its own handshake, pings, 20 Hz presence and animation samples, so a player among N
others sends N times what one partner gets (128 bytes at up to 20 Hz each, plus animation samples of up to about
1 kB at up to 20 Hz each). The core has no fan-out packet: sending one presence to many needs a packet without the
per-peer nonce echo. The relay's broadcast mode ("Broadcast envelope" below) is that packet, outside the core.

Tests: `mod/others_test.cpp` runs several players over a link in memory (independent clocks, delay, loss), with a
plain `Core` as one peer to show that an unchanged core on the other end pairs with it.

## Relay envelope (`relay.h`, envelope version 1)

For many players the core's datagrams no longer travel over one direct link: each game sends them to the players' own
server (`world/`, `coop-world --relay-port`), which passes them on to the players who are near. `relay.h` is the
envelope around a core datagram and the player's side of that conversation. It is header-only and game-free, shared
by the server, the mod and the test tools; it is not yet used by the mod and is not part of `build_id.py`'s manifest.
The core itself is unchanged: a `Core` is still one session with one peer, and a game that sees three players near
runs three of them.

Every datagram starts with the same 8 bytes. Integers are little endian, reserved bytes are zero.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | Magic ASCII `DSR1` |
| 4 | 2 | Envelope version, exactly 1 |
| 6 | 1 | Type, 1..7 (below) |
| 7 | 1 | Reserved zero |

| Type | Direction | Size | After the 8 bytes |
|---|---|---:|---|
| HELLO 1 | player to server | 112 | u64 player id (not 0); token[32]; u32 hello sequence; u32 roster stream; u32 roster ack; u32 area; u8 flags (1 = a position is given); reserved[3]; i32 x3 position in centimetres; nickname[32] |
| WELCOME 2 | server to player | 32 | u64 player id; u32 the HELLO's sequence; u32 roster stream; u16 enter metres; u16 leave metres; u16 timeout seconds; u16 players near now |
| REFUSED 3 | server to player | 24 | u64 player id; u32 the HELLO's sequence; u8 reason (1 server full, 2 id in use at another address); reserved[3] |
| ROSTER 4 | server to player | 20 + events | u32 roster stream; u32 sequence of the first event; u8 count 1..24; reserved[3]; then count times: u8 event (1 enter, 2 leave), u64 player id, u8 n (0..32), nickname[n] |
| DATA 5 | player to server | 16 + payload | u64 destination player id; payload, 1..1252 bytes (one core datagram, not looked at) |
| RELAYED 6 | server to player | 16 + payload | u64 source player id; payload |
| BYE 7 | player to server | 48 | u64 player id; token[32] |

- **Token**: the group's secret, at most 32 letters, digits, `-` and `_`, zero padded. It travels in the clear, like the
  server's path prefix: it keeps strangers who reach the address from joining, not someone who can read the traffic.
- **Position**: the axes of the core's `State` (metres, Y up), as centimetres within 100 km on each axis, like PRESENCE.
  It is integers on the wire for the same reason as there (no float bit patterns). Without the flag the three
  fields are zero and the player is near nobody (loading, a cutscene).
- **Area**: a number for the map the player is in; players in different areas are never near each other. The core
  has no such identifier today, so the adapter defines it; 0 when it has none.
- **Nickname**: at most 32 bytes, no control bytes, zero padded in HELLO. The server only passes it on in ENTER.
- **Player id**: 64 bits chosen by the player, stable for a session. Which id each game uses is the adapter's decision.

Refused whole (`parse` returns the reason, nothing is read from the datagram): shorter than 8 or longer than 1268
bytes, another magic, version or type, a nonzero reserved byte, any fixed-size type that is not exactly its size,
player id 0, a position outside the envelope or given without its flag, text with a forbidden byte or with bytes
after its padding began, an empty or oversized payload, a ROSTER whose events do not fill the datagram exactly.
The encoders throw for the same things.

The overhead on a core datagram is 16 bytes. The longest core datagram (ANIMATION at 1252 bytes, sized for a
1280-byte path MTU with IPv4 and UDP headers) therefore becomes 1268 bytes, 1296 with headers: over that budget by 16
bytes. Nothing was changed in the core for this; `AnimationWireMax` would have to drop by 16 to keep the budget.

### The roster is a reliable stream

A lost ENTER would leave two players near each other with no session, so ENTER and LEAVE are not fire-and-forget.
The server numbers a player's events from 1 within a *stream* and repeats the unacknowledged ones (up to 24 in one
datagram, oldest first) when an event happens and with every WELCOME; the player applies only the next sequence and
puts the last one applied (`ack`) and the stream number into every HELLO. A WELCOME with another stream number means
the server began again from nothing for this player (he timed out there, or the server restarted): the player forgets
everyone (a LEAVE for each) and starts at 0. A HELLO with stream 0 asks the server for that (a game that started
again on the same port). WELCOME and REFUSED carry the sequence of the HELLO they answer; the player takes them in
that order, once, and never one that answers a HELLO it did not send.

### The player's side (`ds::relay::Client`)

No sockets and no clock of its own, like the core: the adapter gives it a send function for the relay's address,
hands it only datagrams that came from that address, and calls `tick(now)`.

- `setPosition(position, area)` / `clearPosition()`: what the next HELLO reports. HELLO goes out every 500 ms and
  at once after a ROSTER (the acknowledgement).
- `takeEvents()`: ENTER and LEAVE in order. The adapter starts a core session on ENTER and ends it on LEAVE.
- `sendTo(player, datagram)`: wraps one core datagram; refused for a player who is not in the roster.
- `receive(...)`: returns true with the source player and the payload for a RELAYED datagram from a player in the
  roster; everything else is handled inside or dropped.
- No WELCOME for 5 s: the relay is gone; everyone near leaves and the next HELLO starts a new stream. A `tick` time
  earlier than the last WELCOME's is no silence (as in the core, never an unsigned difference).
- `bye()`: tells the server once and forgets everyone.

Pairing two players' cores is the adapter's job. The stand-in swarm (`mod/swarm.cpp`) uses one rule that needs no
negotiation: the same session number for everyone, and of two players the one with the lower id takes the host role.

### Tests

`make -C core relay-test` (part of `make test`): `tests/relay.cpp` pins the layout byte for byte, sends every message
through the wire and back, refuses every sample cut short at each length or one byte longer, each bad field, and
40,000 damaged datagrams; then it drives a `Client` with hand-made server messages (order, repeats, gaps, another
stream, a late WELCOME, a new stream, a refusal, five seconds of silence, a tick out of order, BYE). Ten mutants
must fail it: `RELAY_WIRE_VERSION`, `RELAY_WIRE_LENGTH`, `RELAY_WIRE_RANGE`, `RELAY_WIRE_TEXT`, `RELAY_WIRE_PAYLOAD`,
`RELAY_CLIENT_SILENT`, `RELAY_CLIENT_STREAM`, `RELAY_CLIENT_ORDER`, `RELAY_CLIENT_STRANGER`, `RELAY_CLIENT_CLOCK`. The server's side and
both sides together are tested in `world/` (README there, "The relay").

## Broadcast envelope (`broadcast.h`, envelope version 2)

In version 1 every pair of players near each other runs a core session of its own and the server passes each core
datagram on, so a player among N others sends and receives N times what one partner costs. Version 2 is the relay's
broadcast mode: a game publishes its presence and its animation sample once a tick to the players' own server
(`world/relay.h`), which copies them to every player near it, several players' updates to one datagram, stamped on the
server's clock. What a player only shows of himself (his name, gear, what he carries and where each case sits) is
versioned state that the server keeps and hands out: when a player comes near, when it changes, and when a receiver
asks because it saw it had missed a version. `broadcast.h` is header-only and game-free; the player's side is
`broadcast_client.h` and `.cpp` (`ds::broadcast::Client`), which uses the core's encoder, decoder and interpolation
but no `Core`. Neither is used by the mod yet, and neither is in `build_id.py`'s manifest yet.

Version 3 on the same port is spoken only between the relays of two linked servers (`world/presence.h`;
`world/README.md`, "Presence across linked servers"); a game never sends or receives it, and a linked server's player
reaches a game as an ordinary version 2 player.

A player's HELLO says which version it speaks. The server speaks both and puts a player near only players of the same
version, and in broadcast only players of the same game build and mod build (the HELLO carries both once; within one
build nothing in the entries is checked against it again).

Every datagram starts with version 1's 8 bytes, with 2 in the version field. Integers are little endian, reserved
bytes are zero, and every datagram is at most 1200 bytes (a 1280-byte path with IPv4 and UDP headers to spare).

| Type | Direction | Size | After the 8 bytes |
|---|---|---:|---|
| HELLO 1 | player to server | 140 | bytes 8..111 as version 1's HELLO, except byte 65 (reserved there): who the player wants to be shown, 0 everyone, 1 his friends, 2 nobody; u32 game build; mod build[16]; u64 t0, the player's clock (ms). Every half second: keep-alive, position, and the clock's ping |
| WELCOME 2 | server to player | 76 | bytes 8..31 as version 1's WELCOME; u64 t0 echoed; u64 server time (ms); u32 x4 the versions of this player's state the server holds (0: none); u16 full metres; u16 animation metres; u16 interval ms beyond full; u16 players in reach whom another build or mode keeps apart; u16 FRAME budget; reserved[2] |
| REFUSED 3 | server to player | 24 | as version 1; reason 1 server full, 2 id in use, 3 the server takes another build |
| ROSTER 4 | server to player | 20 + events | as version 1 |
| PUBLISH 5 | player to server | 32 + parts | u64 player id; u32 sequence; u8 parts (1 presence, 2 animation); reserved; u16 animation bytes; u64 stamp, the sample's time on the server's clock (ms); presence[52] with part 1; the animation payload (1..1100 bytes) with part 2 |
| FRAME 6 | server to player | 20 + entries | u64 server time; u8 count (1..255); reserved[3]; count entries of: u64 source; u32 sequence; u64 stamp; u8 parts; reserved; u16 animation bytes; u16 state generation; reserved[2]; presence[52] with part 1; the animation payload with part 2 |
| STATE 7 | player to server | 24 + payload | u64 player id; u8 kind (1 name, 2 gear, 3 carried, 4 places); reserved; u16 bytes (1..1100); u32 version (not 0); payload |
| SHARE 8 | server to player | 44 + payload | u64 source; u8 kind; reserved; u16 bytes; u32 version; u16 state generation; reserved[2]; u32 x4 the source's versions of every kind now; payload. Version 0 with no payload: the source holds nothing of that kind (only in the answer to a FETCH) |
| FETCH 9 | player to server | 24 | u64 player id; u64 source (send me that player's state again) |
| BYE 10 | player to server | 48 | as version 1 |

- **presence[52]**: the core's PRESENCE frame bytes 60..63 (flags, action, phase) and 72..119 (position, velocity,
  yaw, health, stamina, the BYE reason and reserved byte, the vehicle block). The receiver rebuilds the 128-byte frame
  around it and runs the core's own decoder over it, so every value is checked as in a session. The server reads the
  teleport flag and the position, nothing else.
- **Animation payload**: the core's (`encodeAnimationPayload`, "Optional animation samples"). The server checks only
  its shape (at most 32 bool bytes, the held and rose bytes present); it may widen the bool bytes to merge pulses.
- **State**: the adapter's bytes of one kind, one version per change. The *generation* is the server's count of the
  changes it took from that player, over all kinds; FRAME entries and SHAREs carry it, so a receiver sees a change it
  missed.
- **Sequence** (32 bits) and generation (16 bits) are compared in serial-number order. Stamps and times are at most
  2^53.
- **Who is shown** (HELLO byte 65): the player's own choice of whom of the players near the server shows him
  (`ClientConfig::show`, the mod's `show_others`); the server's record of his choice comes first, and it shows him at
  most a cap of them, his friends first, then the nearest (`world/README.md`, "How many players one player sees").
  0 is the HELLO of before, so an older game is everyone; a server from before this refuses a HELLO with 1 or 2 (a
  reserved byte), so a game that asks for friends or nobody needs this server.

Refused whole (`parse` returns the reason): shorter than 8 or longer than 1200 bytes, another magic, version or type,
a nonzero reserved byte, a fixed-size type that is not exactly its size, player id 0, a part, kind, count, choice of who
is shown or length out of range, an animation payload of the wrong shape, a SHARE whose version disagrees with its own list, entries that do
not fill a FRAME exactly. Version 1's parts (HELLO's first 112 bytes, WELCOME's first 32, ROSTER, BYE) go through
version 1's parser. The encoders throw for the same things, and `appendEntry` for an entry that would take a FRAME
past 1200 bytes.

### What the server does with it

The rules and the options are in `world/README.md` ("Broadcast mode"). In short: the source of every entry is the
server's record of the sender's address, never the id a PUBLISH, STATE or FETCH names; an update is taken in order
(a later sequence and a later stamp) with a stamp at most 2 s behind and 0.5 s ahead of the server's clock; a player
within the full distance gets every update, beyond it one in the interval (a teleport always), animation only within
the animation distance, and the pulses of the samples left out ride on the next one sent; a receiver's entries wait at
most the flush time for others to share a FRAME of at most the budget; the latest version of each kind of state is
kept and handed out on ENTER, to everyone near on a change, and on FETCH (every kind, at most one answer in 250 ms per
pair). All of that only to the players near who are shown him: at most a cap of them (`near_cap`, `--broadcast-near`),
by his choice, his friends first, then the nearest, kept while about as near.

### The player's side (`ds::broadcast::Client`)

No sockets and no clock of its own, like the core: the adapter gives it a send function for the relay's address,
hands it only datagrams from that address and calls `tick(now)` with its monotonic milliseconds from one thread.

- **Clock.** HELLO carries the sending time, WELCOME echoes it with the server's time. Only the WELCOME of a HELLO this
  side sent, with that HELLO's time, is a sample. The offset (server minus this side) is taken from the quickest of the
  last 16 round trips and moves by at most 1 % of the time passed (`ClockSlew`); `serverNow(now)` is this side's time
  on the server's clock, valid from the first WELCOME.
- **Up.** `setLocal(state, available, gated, sampledAt, jumped)` and `offerAnimation(animation, now)` give the next
  PUBLISH: one a tick at most, presence at most 20 a second on a change that shows on the wire and every 500 ms
  unchanged, an animation sample at most every 50 ms (an unchanged one every 500 ms) whose pulses are merged into the
  next if it is replaced before it went, the teleport flag on the next PUBLISH with presence only. Nothing while
  offline, alone or gated. `setState(kind, bytes)` makes a new version when the bytes change and sends it at once, and
  again after every WELCOME that shows the server holds another version.
- **Down.** The roster as in version 1 (`takeEvents`, `nearby`). FRAME entries from a source not in the roster are
  dropped (`unknown`). Per source, as the core does per session: order (a later sequence and a later stamp), a stamp
  within 2 s of the server's clock, the core's decoder on every presence, impossible motion (more than 100 m/s plus
  0.5 m) refused with a fresh baseline after a second of it, one teleport in five seconds. `render(source, now)`
  interpolates on the server's clock at a delay of that source's own: the 95th percentile of how late its samples
  arrived, plus `DelayMarginMs` or, when larger, its 95th-percentile send interval (at most 250 ms) and 25 ms more;
  never under `delayMs` or over `DelayMaxMs`. `takeAnimation` gives the sample due at the same shown time with every pulse
  since the last one taken. `live` is a presence taken within 3 s.
- **State.** `state(source, kind, &revision)` is the source's latest bytes of a kind (the revision changes when the
  bytes do). An entry with a later generation than this side has complete starts a FETCH after 300 ms (a SHARE may be
  on its way), repeated every 500 ms until a SHARE's list of versions matches what is held. An entry with an earlier
  generation (one the server had queued before the change, whose SHAREs went at once) and a SHARE older than what is
  held change nothing.
- No WELCOME for 5 s: offline, everyone near leaves. A WELCOME with another stream: everyone leaves and the clock is
  taken again (the server may be another process). `bye()` tells the server once.

Measured in the test's link model (a walk on a circle sampled every 50 ms, every FRAME delayed by 20..120 ms so that
they cross, 5 % repeated; worst of 40 runs): at the full rate with 10 % lost the shown position is within 0.007 m of the
true one at the 99th percentile. At one sample in 200 ms (the default beyond 30 m) it is 0.007 m with no loss, 0.20 m
with 3 % lost and 0.89 m with 10 % lost: a lost sample at 5 a second cannot be bridged and the pose stops for a moment
(the core's extrapolation cap). That is the price of the interval for players farther away.

### Costs

A player sends one PUBLISH a tick (presence at most 20 and animation at most 20 a second, usually together) and two
HELLOs a second whoever is near, where in pairs mode he sends a presence and an animation datagram to each player near.
What he receives is bounded by the server's interest: in `world/broadcast_test.cpp` 24 players walking in one place,
each sending presence and a small animation sample 20 times a second, each received about 60 datagrams (FRAMEs of 7.7
entries, at most 1186 bytes) and 50 kB a second; in pairs mode the same crowd delivers each player 460 presence
datagrams of 144 bytes a second and up to as many animation datagrams. Over loopback (`world/test_broadcast.py`) a
player in a group of 16 with 8 more beyond 30 m received about 67 datagrams and 32 kB a second with a 600-byte budget.
With the server's cap what he receives stops growing with the crowd: of 100 `coop-swarm` stand-ins walking in one
place, each received 40 datagrams and 25 kB a second shown 16 of them, and 140 datagrams and 157 kB a second shown all
99.

### Fight channel (types 11..16)

Fights with visitors (`mod/README.md`, "Fights with visitors"): one player's game owns a fight, and up to a cap of
other players join it as visitors. The owner's state goes to its joined visitors only and a visitor's reports to the
owner only; the server (`world/README.md`, "The fight channel") enforces both, and the player's side checks them again.
Same header, same rules (little endian, reserved bytes zero, at most 1200 bytes, refused whole).

| Type | Direction | Size | After the 8 bytes |
|---|---|---:|---|
| FIGHT 11 | player to server | 44 | u64 player id; u32 fight (not 0); u8 op (1 open or keep, 2 close); u8 kind (not 0; 1 a catcher fight); u8 visitors at most (1..32); reserved; u16 radius metres (1..1000); reserved[2]; i32 x3 place (centimetres, the core's axes, within 100 km); u32 area |
| OFFER 12 | server to player | 44 | as FIGHT, with the owner's id first and, in place of op, the number of visitors now (0..32) |
| JOIN 13 | player to server | 32 | u64 player id; u64 owner; u32 fight; u8 op (1 join, 2 leave); reserved[3] |
| STATUS 14 | server to player | 24 | u64 owner; u32 fight; u8 status (1 Open, 2 Joined, 3 Full, 4 NoFight, 5 Far, 6 Busy, 7 Closed, 8 Gone, 9 Released, 10 Left, 11 Off); u8 visitors now; reserved[2] |
| POST 15 | player to server | 36 + payload | u64 player id; u64 owner (the owner himself for his state, the fight's owner for a visitor's report); u32 fight; u32 version (not 0); u16 bytes (1..1100); reserved[2]; payload |
| DELIVER 16 | server to player | 36 + payload | as POST, with the source (the server's record of who posted it) in place of the player id |

The payloads are the mod's (`mod/fights.h`: the owner's state, the visitor's report) and are covered by its wire
identity: the server offers a fight only to players of the owner's build. **Compatibility:** a server from before
this refuses types 11..16 as unknown (counted as malformed): an owner then never hears STATUS Open and nobody is
offered anything, and presence goes on as before; a game from before this is never offered a fight (another build),
and sends none. The types 1..10 are unchanged.

The player's side (`ds::broadcast::Client`): `hostFight(info, now)` sends FIGHT at once and every `FightKeepMs` (1 s)
while online, `closeFight()` once; `joinFight(owner, fight, now)` sends JOIN at once and every `FightJoinEveryMs`
(500 ms) until a STATUS for that fight answers (refused while hosting; another visit is left first); `leaveFight()`;
`postFight(bytes)` posts a new version as the owner (its state) or as a joined visitor (his report) and nothing in any
other role; `fightOffers(now)` gives the offers heard in the last `FightOfferMs` (3 s); `takeFightNews()` gives the
STATUS messages for this player's fight and each DELIVER taken. A DELIVER is taken only for the player's role: a
visitor the state of the fight he joined from that fight's owner, an owner a report on its own fight from somebody
else, and from each source only a later version than the last; anything else is counted (`fightRefused`). A visit
ends (news with the status) on Full, NoFight, Far, Busy, Closed, Gone, Released or Left, and as Gone when the client
goes offline or the server's stream changes.

Why states rather than the core's reliable events: the broadcast mode has no session per pair to carry an ordered
exactly-once stream, and one per owner and visitor would bring back the pairs mode's cost. The owner's posts are whole
states (a lost one is replaced by the next, a late visitor gets the latest from the server at once) and a visitor's
reports are cumulative (applied as differences), so loss, repetition and reordering change nothing.

### Tests

`make -C core broadcast-test` (part of `make test`): `tests/broadcast.cpp` pins the layout byte for byte, sends every
message through the wire and back, refuses every sample cut short at each length, one byte longer, with another
magic, version, type or reserved byte, each bad field (a choice of who is shown above 2 among them), and 40,000 damaged
datagrams; then it drives a `Client` with
hand-made server messages: the clock (symmetric and slow round trips, a WELCOME for a HELLO never sent), the roster, a
new stream, silence, a refusal for the build; entries in order, repeated, older, with a stamp far from the server's
clock, from a stranger, several sources in one FRAME, impossible motion and the fresh baseline, teleports; state
complete in one SHARE, a gap repaired by FETCH, empty kinds, a late older SHARE and an entry of an older generation
(no FETCH), its own state versioned and sent again; what it publishes (rate, stamps, the presence the core's encoder
gives, gated, the teleport flag once, animation pulses merged; the choice of who is shown in every HELLO); and
interpolation over the lossy link above; the fight channel: every type through the wire and back, byte for byte, each
bad field refused (a status above 11 among them), and an owner and a visitor driven by hand-made STATUS and DELIVER
(the keep and the join repeated until answered, posts only in a role, a visitor's report taken by the owner once per
version, another fight's state, another visitor's report and an older version not taken by a visitor, a visit ended by
the owner, by a refusal and by silence). Twenty-one mutants must fail it: `BCAST_WIRE_VERSION`, `BCAST_WIRE_LENGTH`, `BCAST_WIRE_RANGE`, `BCAST_WIRE_FRAME`,
`BCAST_CLIENT_STRANGER`, `BCAST_CLIENT_ORDER`, `BCAST_CLIENT_CLOCK`, `BCAST_CLIENT_GAP`, `BCAST_CLIENT_STALE_STATE`,
`BCAST_CLIENT_TELEPORT`, `BCAST_CLIENT_SPEED`, `BCAST_CLIENT_REBASE`, `BCAST_CLIENT_RESEND`, `BCAST_CLIENT_DELAY` (it
fails the 200 ms case without loss: p95 0.06 m, shown past the newest sample 81 % of the time), `BCAST_CLIENT_ROSE`,
`BCAST_CLIENT_STREAM`, `BCAST_CLIENT_JUMP`, `BCAST_WIRE_SHOW`, `BCAST_WIRE_FIGHT`, `BCAST_CLIENT_FIGHT_SOURCE`,
`BCAST_CLIENT_FIGHT_ORDER`. The server's side and both sides together are tested in `world/` (README
there, "Broadcast mode").
