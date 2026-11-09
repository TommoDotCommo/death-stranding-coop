# Death Stranding co-op launcher

Native Win32/C++17 application. Build with `make -C launcher all` (MinGW-w64); run
`launcher/build/deathstranding-coop.exe` on Windows. No GUI framework or extra runtime
installation is needed. The game is still experimental; this launcher does not make
shared story progression or the partner body release-ready.

The [co-op standard](https://github.com/TommoDotCommo/coop-mod-kit/blob/main/STANDARD.md)
and its prototype define the structure: Play / Saves / Settings / About (A1),
Host / Join / Solo and one primary action (A2), Lobby as a state of Play (A3),
network discovery and invites (B1/B2/B5–B9, C3–C5), ready/start (D3), settings saved
on change (E1), separate co-op saves and backups (F1), per-user invite links (G3).
The application uses ordinary Windows controls with keyboard focus and resizes to
fit a 1280×720 display. Enter confirms the focused action, Esc returns to Play
(with confirmation before leaving a lobby), and F1 opens the quick guide.

## Play

Choose your name (1–32 printable ASCII characters, as required by the mod's current
config parser), then:

- **Host a game** → **Open lobby**. Copy the invite. Wait for your friend to join and
  choose Ready; then **Start game** starts both copies. A lobby has two players.
- **Join a friend**: the list scans every up IPv4 adapter's directed broadcast on
  the configured default port and next three, online Tailscale peers (including
  shared-in nodes), and recent hosts. It refreshes every five seconds while shown.
  Paste a `DS-...` invite, a fragment link, `deathstranding-coop://join/DS-...`,
  numeric IPv4 address, address:port, or host/Tailscale name. Recognition is shown
  before joining. Choose a row or use the pasted routes, then **Ready**.
- **Play solo** starts an isolated co-op save session without a partner. Ordinary
  Steam starts outside the launcher keep their ordinary saves and co-op stays off.

Invite routes use the kit's CRC-checked Crockford base32 v1 payload, with prefix
`DS` and probe game id `DSDC`. Every invite route is probed in parallel; the kit's
300 ms preference chooses LAN before Tailscale before internet. The scan completes
within its 1.2 s reply window, after any Tailscale status lookup. Mismatched versions
remain visible and cannot be joined. Invite-only replies hide the host's name and
require the lobby secret to join. The launcher answers CPQ2/CPS2 (and CPQ1) while
its lobby owns the game port; replies are rate-limited. A separate 16-byte `DSL1`
launcher exchange establishes the shared session, observed peer address, readiness
and acknowledged Start. It is not a game-protocol packet and is never sent to the
public game's service.

The mod's existing ports are `49000 + slot`. The launcher's host port is therefore
49000–49014; client uses the next port, with opposite host/client slot assignments.
Both configs use the same nonzero 32-bit invite secret as the mod's session id.
The proxy's peer allowlist is the same selected numeric address. The generated
config uses the pinned game hash, `required:true`, and the play setup: the guarded
game-thread callback (`game_thread`, `puppet_probe`), the partner drawn as the mod's
own Sam (`partner:"own"`, the game's resident double until it exists) wearing the
porter's visible gear (`partner_gear`), the partner's map pin (`partner_marker`, no
ping key), his vehicle while he rides (`partner_vehicle`), the cargo he carries as cases on his back
(`carry_visual`), his name over his head (`partner_tag`), the host's rain and timefall in the guest's world
(`share_environment:"rain"`: rain only, never the host's hour), and with them shared cargo and structures.
Automatic Continue and the virtual pad stay off. Existing environment-driven test
tools still work independently of this app.

## Steam and data

Settings selects the game folder registered by Steam for app **1850570**. The
launcher verifies that registration and the pinned executable SHA-256, writes
`%LOCALAPPDATA%\DeathStrandingCoop\redirect-data\mod\mod.json`, then asks Steam to
run the app. It never changes Steam launch options or ownership checks. A copy
already running is refused. Install the release `avrt.dll` and the matching mod
first; see [install](../install/README.md).

Steam may already be running, so it cannot reliably inherit a launcher's process
environment. The launcher publishes a single-use `launcher-session.ini` in the
co-op data folder, valid for 120 seconds and bound to the selected executable path.
The release proxy reads and consumes it **before** installing hooks. It supplies
all isolation roots, allowlisted module policy and enforced networking. A missing,
expired, malformed or wrong-copy ticket leaves `avrt.dll` as a pure System32
pass-through: no hooks, no mod, no save redirection and no runtime file writes.
One debugger/stderr line says `co-op: off ... system DLL pass-through`.
Validated release tickets also allow the co-op data tree inside LocalAppData, with
the instance excluded before mapping saves, attributes or enumeration. Lab environments
retain their nested-root refusal. A release module allowlist separately admits the
actual executable directory, keeping adjacent game folders and writes there refused.
Any explicit isolation environment selects the old fail-closed lab path instead;
`d3dcompiler_47.dll` always retains that path.

Saves displays only the co-op data location. Back up copies only
`redirect-data/{local,roaming,documents,savedgames,profile}` to a new folder under
`backups/`. The app requires the game and lobby closed. It rejects symbolic links,
Windows reparse points and hard-linked save files. No source points at the player's
ordinary saves. `launcher.ini` and `recents.tsv` also live in the co-op data folder.
The current proxy requires ASCII-only game/data paths.

Settings offers per-user registration of `deathstranding-coop://`; alternatively:

```powershell
.\deathstranding-coop.exe --register-protocol
```

The registry command quotes the launcher and URI separately. Registration writes
HKCU only, when explicitly requested. The copied web invite points to
`https://tommodotcommo.github.io/death-stranding-coop/join/#DS-...`.
[`docs/join`](../docs/join/index.html) supplies that page, derived from the kit.
**It must be hosted during integration; this change does not publish it.** Pasting
the same link/code into the launcher works without a hosted page.

## Game-free checks

```sh
make -C launcher test   # production config round trip, DS codec/URIs, discovery,
                       # lobby packets, startup tickets, backup confinement;
                       # reference fuzzers and 16 mutants
make all               # mod, both proxies, launcher and Windows fixtures
```

The kit's portable suites are vendored and reused (20,000 invite and 10,000
discovery fuzz rounds). Mutants cover config role, lobby secret, backup links and
running-game guard, output links, missing-config exit, ticket target/expiry/network, nested release/lab save roots,
invite CRC/prefix, discovery nonce/game/subnet broadcast. Each must reach an
assertion in the unchanged oracle, not fail to compile or crash.

Additional game-free checks on native Windows (never start the game):

```powershell
.\launcher\build\net-fixture.exe
.\launcher\build\discover-win-test.exe
python .\redirect\test_passthrough_windows.py --work-dir "<disposable fixture folder>" --fixture .\redirect\build\proxy_fixture.exe --proxy .\redirect\build\avrt.dll --mutant .\redirect\build\mutant-missing-config-closes.dll
```

The first creates two loopback launchers, checks wrong-secret refusal, Ready/Start
and matching configs. The second is the kit's Windows adapter/socket suite. The
third clears COOP_* and uses fake LOCALAPPDATA: release forwarding must survive
without creating files; the missing-config-closes mutant must exit 125.

## LIVE CHECK (operator only)

No game is started by a build or test target. Use only a verified disposable copy,
its isolated environment and save-hash verifier. Deploy rebuilt DLLs and run fresh
isolation/module/network probes before a live launch. Keep networking enforced.

For the simplest one-copy check, use the partner stand-in (no second game needed):

```powershell
.\mod\build\coop-partner.exe --slot 1 --peer-slot 0 --role client --session 123 --follow
.\launcher\build\deathstranding-coop.exe --dev-direct --game-dir "<test copy>" --data-dir "<instance>" --port 49000 --dev-start --dev-peer 127.0.0.1 --dev-session 123
```

`--dev-direct` is the only path that calls CreateProcess directly. It still hashes
the executable and requires the matching mod. It uses the provided test environment
for real roots, Steam root and module/redirect instance; if absent, it derives
known folders and isolates into the explicitly selected data folder. It forces
network policy `enforce`, updates the exact peer allowlist and role/ports/session,
and leaves other test switches inherited. Direct/lab data must be outside the real
user folders, preserving the lab nested-root refusal. It performs no Steam emulation.
`--dev-start` skips the launcher lobby for a stand-in; `--dev-client` chooses client.
Add `--prepare-only` to write/validate configuration and exit without a game.
Custom `--data-dir` is accepted only with `--dev-direct`.

Look for proxy `READY` and `MOD_STARTED` (network only), then mod `POSE available`
after continuing a co-op save, and `handshake:true` / `connected:true` in the
partner/mod state. Inspect `<instance>/redirect-data/mod/mod.json`: role host,
slots 0/1, peer 127.0.0.1, session 123, game provider, game_thread, puppet_probe,
partner own, partner_gear, partner_marker, partner_vehicle, carry_visual, partner_tag, share_environment rain, required. Repeat
with `--dev-client` and a host stand-in to check opposite slots. This launcher
setup shows the partner without any command; no `puppet.double`, `pad.script` or
`puppet.body` command is armed. If a later test sends them independently, use
only the PID/creation-time stamp from the current log as documented in
[Live test commands](../mod/README.md#live-test-commands-per-process-stamp-guarded).
Expect seconds for launcher/proxy setup and about a minute for a cold game load;
allow 90 seconds for presence. Failure is an error dialog, exit 125 on the explicit
lab path, missing READY/MOD_STARTED, unavailable pose after loading, refused
handshake, a peer/config mismatch, or any save-hash change.

Two launchers plus **two** disposable game copies are needed to verify the real
Host → invite/discovery → Join → Ready → Start flow, selected peer IPs and both
port assignments. Use `--dev-direct` on each to avoid Steam's installed-copy
selection. Verify the release's ordinary Steam path separately: without a valid
ticket the pass-through fixture above must pass first; with a new ticket, Steam
must start exactly the selected registered executable, consume the ticket, and
produce READY/MOD_STARTED only in the co-op data folder. Hash ordinary saves before
and after every live run using the existing verifier. These live checks are not
established by the portable tests or cross-build.

## Remaining standard work

This is a launcher foundation, not full A–K compliance. Steam friend/lobby SDK
integration, UPnP/NAT-PMP, a checked firewall status/fix, chat/voice, controller and
Deck navigation, update/diagnostic UI, save metadata/slot management, recent-host
rename/pin/forget, automatic lobby return after game exit, and in-game discovery
replies remain unimplemented. The launcher releases its lobby port before the mod
binds it, so hosts are discoverable in the pre-game lobby only. Internet play uses
LAN/VPN routes; there is no NAT traversal. Named hosts use Windows IPv4 DNS.
