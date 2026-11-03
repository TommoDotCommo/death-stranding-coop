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

All eight packet types are **exactly 128 bytes**, one UDP datagram each. Integers
are explicitly little endian; signed values use two's complement. No packed
structs, float bit patterns, pointer casts or native padding are on the wire.
Reserved bytes must be zero. Fields unused by a packet type are ignored after
common validation; the encoder fills them with default values (including health
and stamina 255). Receivers do not require ignored fields to be zero.

| Offset | Bytes | Field and allowed range |
|---|---:|---|
| 0 | 4 | Magic ASCII `DSP1` (`0x31505344`) |
| 4 | 2 | Protocol version, exactly 1 |
| 6 | 1 | Kind: HELLO=1, HELLO_ACK=2, PRESENCE=3, PING=4, PONG=5, BYE=6, CARGO=7, CARGO_ACK=8 |
| 7 | 1 | Sender role: host=1, client=2; peer must have opposite role |
| 8 | 2 | Datagram length, exactly 128; actual received length must match |
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
pairing. `receive` returns a typed error and `reason(error)` supplies clear text,
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
PONG replay, local finite clamps and NaN/infinity rejection. Mutants are injected only through `DS_TESTING` APIs; production core
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
| 62 action | event type 1..3 (1 = cargo placed in the world) | ignored |
| 72 position, 90 yaw | where the cargo is, adapter axes | ignored |
| 96 t0 | the game's cargo item hash, 0..2^32-1 | ignored |
| 104 t1 | nonzero event id chosen by the sender, stable across reconnections | ignored |

Delivery is reliable, ordered and at most once per connection. The sender repeats the oldest unacknowledged event
every 200 ms. The receiver takes only the next sequence and acknowledges it, and acknowledges any repeat of an older
one again (its earlier acknowledgement may have been lost). A gap is neither delivered nor acknowledged. On a new
connection both sides number from 1 again, and undelivered events are kept and renumbered. The receiver delivers
each event id only once for the lifetime of the connection object (it remembers the last 4096 ids), so an event
whose acknowledgement was lost just before a reconnection is not delivered twice (mutant `cargoOnce`). Mutants `cargoOrder` and
`cargoAck` remove ordering and acknowledgement matching. The lossy-link fuzz (500 seeds, 10% loss, 5% duplicates,
an outage and reconnect) checks that every event is delivered exactly once and in order.
