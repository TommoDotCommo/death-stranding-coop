# Glossary

The shared words for the owner, testers and developers of Death Stranding Co-op: what each part of the mod is
called, what the game's own terms mean when the mod uses them, and what the testing words mean. When two words
seem to fit, the table below says which one to use.

Entries are alphabetical within each section. *Where* names the file or folder a term is defined in when it is
code. Terms marked **(lab only)** belong to the developers' test setup and never reach a player. Terms marked
**(planned)** are agreed for the first alpha but not in this checkout yet.

- [Which word do I use?](#which-word-do-i-use)
- [Parts of the mod](#parts-of-the-mod)
- [Game terms](#game-terms)
- [Testing words](#testing-words)

## Which word do I use?

| If you mean... | Say | Not |
|---|---|---|
| The one other player in a two-player session | [partner](#partner) | double, stand-in |
| The Sam the mod draws for the partner or another player | [body](#body) (the partner's body) | double, puppet |
| The game's spare Sam that the mod can borrow | [resident double](#resident-double) | partner |
| A program that pretends to be the partner, for tests | [stand-in](#stand-in) (`coop-partner`); many of them: [swarm](#swarm) | partner, double |
| Other players in group play | [other players](#other-players) (in a boss fight: [visitors](#visitor)) | partners |
| The DLL in a player's real game folder | [player proxy](#player-proxy) (`avrt.dll`) | redirect, lab proxy |
| The DLL in a developer's disposable test copy | [lab proxy](#lab-proxy) (`d3dcompiler_47.dll`) | player proxy |
| The program `coop-world.exe` as a whole | [world server](#world-server) | relay, strand server |
| Its part that carries built objects, likes and roads (HTTP) | [strand server](#strand-server) | relay |
| Its part that carries where players are, in real time (UDP) | [relay](#relay) | strand server |
| The game company's own online servers | [the publisher's service](#publishers-service) | server, strand server |
| The two players in a lobby | [host](#host) and [guest](#guest) | host and client |
| The `role` field in `mod.json`, or the two ends of one core session | host and client ([role](#role)) | host and guest |
| In group play, who runs the server, and everyone else | [host](#host) (of the group server) and [joiners](#joiner) | host and guest |
| One file the game wrote to resume from | [checkpoint](#checkpoint) | save |
| A player's whole set of save files and settings | [saves](#saves) (co-op saves or ordinary saves) | checkpoint |
| What the network is like between two PCs | [link](#link) (its [delay](#delay), [round trip](#round-trip)) | connection, lag |
| The mod's source fingerprint two games compare | [wire identity](#wire-identity) | version |
| The release name a player sees | [version](#version) | build id |
| The pinned Steam build of the game | [supported build](#supported-build) (13300582) | version |

## Parts of the mod

<a id="allowlist"></a>**Allowlist** (aka module allowlist): the [module policy](#module-policy) setting that lets
only code from the game's own folder, System32, the Steam client and Windows Defender's scanner load into the game,
plus anything a player lists in `extra-modules.txt`. *Where:* `redirect/module_policy.h`. See also
[player proxy](#player-proxy).

<a id="audit-log"></a>**Audit log** (aka proxy log, redirect log): the [proxy](#player-proxy)'s own log of what it
redirected, refused or allowed (`redirect-<pid>-<start>.log`, UTF-16, one `KIND<TAB>from<TAB>to` record per line).
*Where:* `redirect/redirect.cpp`; the file is in `redirect-data`. See also [logs](#logs).

<a id="backups"></a>**Backups**: the `backups\` folder in the [co-op data folder](#co-op-data-folder). Saves →
Back up copies the co-op saves there, and the launcher moves co-op saves there instead of deleting them before it
replaces them (joining with the host's save, copying in the ordinary saves). Player keys are not backed up. *Where:*
`launcher/saves.cpp`.

<a id="body"></a>**Body** (aka own body, partner body; older: puppet): the Sam the mod makes in your game to show the
[partner](#partner) or an [other player](#other-players), moved every [tick](#tick) to their pose and dressed in
their [gear](#gear). It is the mod's own entity, made and removed by the mod.
In `peer` mode the mod borrows the [resident double](#resident-double) instead. *Where:* `mod/body.h`,
`mod/double.h`, `mod/puppet.h`.

<a id="broadcast"></a>**Broadcast mode** (aka `relay_mode: "broadcast"`): the [relay](#relay)'s mode for many players:
each game sends one update a tick to the server, and the server copies it to every player near. The launcher uses it
for group play. Compare [pairs mode](#pairs). *Where:* `core/broadcast.h`, `world/relay.h`, `mod/crowd.h`.

<a id="bugmark"></a>**BUGMARK** (aka F2 marker): pressing F2 in the game writes a numbered `BUGMARK
<n>` line into the game's [log](#logs) and keeps the last 60 seconds in a flight file beside it. The partner's game
(two players) or the games of the players near (group play: `BUGMARK <n> from <name>`, and a line in the server's log)
write the same number. When both players
press F2 at the moment something goes wrong, the [diagnostics reader](#diagnostics) lines up their two timelines on
that number. The reader already understands them: `tools/diag_report.py --align`.

<a id="build-id"></a>**Build id**: see [wire identity](#wire-identity).

<a id="build-received"></a>**Build received** (`build_received`): a group-play option, on in the launcher unless the
player turns it off, that makes the game build other players' structures as soon as the [strand server](#strand-server)
hands them over, skipping the game's own range, placement and area checks. *Where:* `mod/received.h`.

<a id="co-op-data-folder"></a>**Co-op data folder** (aka data folder): `%LOCALAPPDATA%\DeathStrandingCoop\` on a
player's PC. It holds the [co-op saves](#saves) under `redirect-data\`, the mod under `redirect-data\mod\`,
`launcher.ini`, [`player.key`](#player-key), the [launcher ticket](#launcher-ticket), [backups](#backups) and the
group's world file. Uninstalling the mod keeps it. See also [redirect](#redirect).

<a id="core"></a>**Core** (aka presence core, protocol 1): the game-free networking heart of the real-time layer:
handshake, player snapshots, clock and [round-trip](#round-trip) estimates, smoothing and a reliable channel for
events such as cargo. One core runs per pair of players. *Where:* `core/`, described in `core/CORE.md`.

<a id="crash-record"></a>**Crash record** (aka crash recorder, hang note): an opt-in part of the proxy
(`COOP_CRASH_RECORD=1` or `2`) that writes every fault address as module plus offset, the stack, a hang note when the
game's message loop stops, and a minidump. Today it writes `crash-<pid>-<start>.log/.dmp` in `redirect-data` and only
the lab can turn it on; the alpha will write `<stem>.crash` and `<stem>.dmp` into the new [logs](#logs) folder
**(planned)**. *Where:* `redirect/crash_record.h`.

<a id="crowd"></a>**Crowd**: the mod's side of [broadcast mode](#broadcast): one connection to the relay that sends
this player's pose and receives everyone near. Also the lab command `crowd N` that makes N test [bodies](#body).
*Where:* `mod/crowd.h`.

<a id="delay"></a>**Delay** (aka presentation delay, interpolation delay): how far in the past the mod shows the
other player, about 120 ms by default, so their movement can be smoothed between snapshots; on a poor
[link](#link) it grows. Also the one-way delay a link profile adds. *Where:* `core/`, `mod/timing.h`;
`mod/README.md`, "Why 120 ms delay".

<a id="diagnostics"></a>**Diagnostics** (aka diagnostics zip, bug report): About → **Send diagnostics** in the
launcher (or `deathstranding-coop.exe --diagnostics-zip`) saves a redacted zip on the Desktop with the text report,
recent [logs](#logs), masked settings, `system.txt`, `network.txt` and a `manifest.json`. Nothing is uploaded;
names, addresses, Steam ids and secrets are masked. `--diagnostics` alone writes only the text report. Developers
read zips with `tools/diag_report.py`. *Where:* `dist/` (the exporter), `tools/` (the reader), `launcher/`.

<a id="discovery"></a>**Discovery** (aka signature scan): the mod's read-only search of the running game for the
code and data it needs, by byte patterns, allowed only when `ds.exe`'s hash is the [supported build](#supported-build).
Every game function the mod calls is checked this way first. *Where:* `mod/discovery.h`,
`mod/discovery_signatures.h`; the research behind it is in `mod/game_offsets.md`.

<a id="double"></a>**Double**: two meanings, so say which. The [resident double](#resident-double) is the game's
spare Sam. `Double` in the code (`mod/double.h`) is the mod's driver that moves a [body](#body) to a pose. Neither is
the [partner](#partner).

<a id="enforce"></a>**Enforce**: the default and only shippable setting of the [network policy](#network-policy):
the game reaches loopback and the listed [peers](#peers) and nothing else. Its opposite, `log-only`, records without
refusing and is for research only.

<a id="fight-channel"></a>**Fight channel** (aka fights with visitors): opt-in group play for a BT
[catcher](#catcher) fight: one game owns the fight and (by default) up to nine others join it as [visitors](#visitor) through
the [relay](#relay); everyone's hits count and the fight ends cleared for all. Config `host_fights`,
`join_fights`. *Where:* `mod/fights.h`, `world/relay.h`; `mod/README.md`, "Fights with visitors".

<a id="game-thread"></a>**Game-thread dispatch** (aka game-thread hook, `game_thread`): the way the mod gets to run
its work on the game's own main thread, between frames, at most 20 times a second, which is required before it may
call any game function. The [proxy](#player-proxy)'s [observer](#observer) watches the game's message loop and calls
the mod at a safe moment. *Where:* `mod/game_thread.h`, `redirect/README.md`.

<a id="gear"></a>**Gear**: what a porter visibly wears (backpack, cuff, boots, outfit parts). The mod copies the
other player's gear onto the [body](#body) it shows. Config `partner_gear`, `others_gear`. *Where:* `mod/gear.h`,
`mod/outfit.h`.

<a id="group"></a>**Group** (aka group play, group server): play for any number of players on one [world
server](#world-server): no partner and no lobby; each player plays their own save, and players who are near each
other see each other. A `mod.json` without `peer` is a group config. Compare [lobby](#lobby). *Where:*
`mod/README.md`, "Group play"; `launcher/README.md`, "A group on one server".

<a id="group-invite"></a>**Group invite** (aka group key, `DSG-...`): the code the group's host sends to everyone
else. It holds the group's key (a random number, `group_secret` in `launcher.ini`), the server's port and the host's
addresses. Whoever has it can join; it travels unencrypted, so share it only with people you play with. Compare the
lobby [invite](#invite) (`DS-...`). *Where:* `launcher/README.md`.

<a id="guest"></a>**Guest**: in a two-player [lobby](#lobby), the player who joins. With "Start from the host's
save" the guest's game starts from the host's newest [checkpoint](#checkpoint). In `mod.json` the guest's
[role](#role) is `client`.

<a id="host"></a>**Host**: in a two-player [lobby](#lobby), the player who opens it; in [group](#group) play, the
player whose launcher runs the [world server](#world-server). In two-player play some things follow the host only,
such as rain and story catch-up.

<a id="identity-keys"></a>**Identity keys** (aka player keys, `identity keys`): a server rule (the Public preset)
under which each player is known by their [player key](#player-key) instead of a name: the launcher's calls are
signed, and the game signs in with a short-lived ticket the launcher took for it. Under `identity none` (Friends,
Solo) anyone with the group's key can act as any player. *Where:* `world/identity.h`; `world/README.md`, "Identity".

<a id="invite"></a>**Invite** (aka lobby invite, `DS-...`): the code a two-player host copies to their friend; also
as a `deathstranding-coop://` link or a join page link. It carries the lobby's secret and the host's addresses.
Compare [group invite](#group-invite). *Where:* `launcher/vendor/coop_invite.h`, `docs/join/`.

<a id="joiner"></a>**Joiner**: in [group](#group) play, any player who joins with the [group invite](#group-invite)
rather than running the server. There is no Ready or Start: the game starts and meets the others on the server.

<a id="lab-proxy"></a>**Lab proxy** (`d3dcompiler_47.dll`): the same proxy code as the [player
proxy](#player-proxy), built under the name of a DLL the game ships, for developers' disposable test
[copies](#copy). It is configured only by `COOP_*` environment variables and refuses to start the game without them
(exit 125). It is never given to players. *Where:* `redirect/`.

<a id="launcher"></a>**Launcher** (`deathstranding-coop.exe`): the player's program, with the tabs Play, Saves,
Settings and About. It checks the game build, installs the mod ([Setup](#setup)), writes `mod.json` and the
[launcher ticket](#launcher-ticket), starts the game through Steam, runs lobbies and group servers, and saves
[diagnostics](#diagnostics). *Where:* `launcher/`.

<a id="launcher-ticket"></a>**Launcher ticket** (`launcher-session.ini`): a single-use file the launcher writes into
the [co-op data folder](#co-op-data-folder) just before it asks Steam to start the game. It is valid for 120 seconds
and only for that `ds.exe`. The [player proxy](#player-proxy) reads and deletes it before doing anything; without a
valid ticket it stays a plain pass-through and co-op is off. *Where:* `launcher/launch_win.h`,
`redirect/release_startup_win.h`.

<a id="link"></a>**Link**: the network path between two players' PCs, or between a player and the server. In tests a
link profile (`link`: `none`, `internet`, `poor`, or `COOP_LINK`) makes the mod lose and delay packets on purpose, so
two copies on one PC behave like two PCs far apart. *Where:* `mod/link.h`. See [delay](#delay),
[round trip](#round-trip).

<a id="linked-servers"></a>**Linked servers**: two [world servers](#world-server) connected with a shared key: they
hand each other built objects (each direction with filters by kind, builder, area and age) and, with
`--link-presence yes`, show each other's players, named with their home server ("Bea · north"). Objects never
travel back to where they came from. *Where:* `world/link.h`; `world/README.md`, "Linked servers".

<a id="lobby"></a>**Lobby**: the two-player meeting place in Play: the host opens it, the guest joins with the
[invite](#invite), both choose Ready, and Start starts both games. Group play has no lobby.

<a id="logs"></a>**Logs**: what the mod, proxy, launcher and server write while they run. Today they are in
`redirect-data\` of the [co-op data folder](#co-op-data-folder): `mod\mod-<pid>-<start>.log`, the
[audit log](#audit-log), the [presence log](#presence-log) and [crash records](#crash-record). For the alpha
**(planned)**, every run writes one UTF-8 file to `%LOCALAPPDATA%\DeathStrandingCoop\logs\` named
`<UTC time>_<component>_p<pid>.log` (component `game`, `proxy`, `launcher` or `world`), each line numbered and
chained so damage can be seen, ending with `session end: clean exit`; the oldest are pruned above 200 MB, crashed runs
kept. [Diagnostics](#diagnostics) collect both layouts.

<a id="mod"></a>**Mod** (`mod.dll`): the part that runs inside the game: it reads the player's pose and state, talks
to the partner, the relay and the server, and draws the other players. The proxy loads it from
`redirect-data\mod\mod.dll` once the game has started. *Where:* `mod/`.

<a id="mod-json"></a>**mod.json** (aka the mod config): the mod's settings, beside `mod.dll`. The launcher writes it
for every start; a developer may write it by hand. Unknown keys, duplicates or anything malformed refuse the start.
With `"required": true` the game does not start at all when the mod cannot. *Where:* `mod/config.h`;
`mod/README.md`, "Config, paths and identity". Not to be confused with `coop-mod.json`, the package manifest in the
game folder (`install/README.md`).

<a id="module-policy"></a>**Module policy**: the proxy's rule for which DLLs may load into the game: `strict`,
[`allowlist`](#allowlist) (players) or `log-only` (research). A refused module is answered as a failed load and the
game carries on without it. *Where:* `redirect/module_policy.h`.

<a id="network-policy"></a>**Network policy**: the proxy's rule for where the game may connect. On
[`enforce`](#enforce) it reaches only loopback and the [peers](#peers); every other address and host lookup fails as
if the PC were offline, so the game never talks to [the publisher's service](#publishers-service). *Where:*
`redirect/network_policy.h`.

<a id="notices"></a>**Notices** (aka session notices): lines the mod adds to the game's own [info log](#info-log):
the partner joined or left, is down, is calling out, nobody has arrived yet. Config `notices`. *Where:*
`mod/notice.h`. Compare [strand notices](#strand-notices).

<a id="observer"></a>**Observer** (aka message-thread observer, thread trace, `COOP_THREAD_TRACE`): the proxy's watch
on the game's message loop. It records nothing about game objects; it is the heartbeat for the
[crash record](#crash-record)'s hang note and the seam for [game-thread dispatch](#game-thread). A valid
[launcher ticket](#launcher-ticket) turns it on. *Where:* `redirect/thread_trace.h`.

<a id="offer-strand"></a>**Offer strand** (`offer_strand`, the player's choice `offer_old`): uploading the structures
a player's save already holds to a [strand server](#strand-server), one at a time, through the game's own upload,
when both the player and the server say so. *Where:* `mod/offer_strand.h`.

<a id="other-players"></a>**Other players** (aka others, `others`): in group play, the players near you that the
relay shows you, each with a [body](#body) and a name. They are shown only: no cargo, structures made in the session,
vehicles or orders are shared with them. *Where:* `mod/others.h`, `mod/crowd.h`.

<a id="pad-script"></a>**Pad script** (`pad.script`, aka virtual pad): a file of keys, mouse moves, clicks and
gamepad steps the mod feeds to the game itself, so tests can drive a game copy without using the desktop or the
window focus. It needs `virtual_pad` in `mod.json` and `COOP_VIRTUAL_PAD=1`; it is off for players. *Where:*
`mod/pad.h`, `redirect/virtual_pad.h`, `redirect/virtual_kbm.h`.

<a id="pairs"></a>**Pairs mode** (`relay_mode: "pairs"`): the [relay](#relay)'s original mode, where every two
players near each other run a [core](#core) session of their own through the server. It costs more with many players
than [broadcast mode](#broadcast).

<a id="partner"></a>**Partner**: the one other player in a two-player session (a `mod.json` with `peer`). Most
`partner_*` settings act on the partner only: their [body](#body), name, map pin, vehicle, cargo, likes. The
two-player sharing (cargo, structures, rain, MULEs, orders, story) is opt-in and no longer developed. Compare
[other players](#other-players), [stand-in](#stand-in). *Where:* `mod/partner.h`, `mod/partner_logic.cpp`.

<a id="peers"></a>**Peers** (`COOP_NETWORK_PEERS`): the numeric IPv4 addresses the [network policy](#network-policy)
lets the game reach besides loopback: the partner's PC, or the group's server. A `mod.json` naming a server that is
not in this list refuses the start. Also `peer` in `mod.json`: the partner's address.

<a id="player-id"></a>**Player id** (`player_id`): a player's number on the [relay](#relay), made once by the launcher
from random bits and kept in `launcher.ini`; never the Steam id. Under [identity keys](#identity-keys) it is made from
the [player key](#player-key) instead.

<a id="player-key"></a>**Player key** (`player.key`): an Ed25519 key pair the launcher makes once and keeps in the
[co-op data folder](#co-op-data-folder). The secret half never leaves the PC; the public half is who the player is
on servers with [identity keys](#identity-keys). Deleting it makes a new player there. *Where:* `world/identity.h`,
`launcher/strand.h`, `vendor/monocypher/`.

<a id="player-proxy"></a>**Player proxy** (`avrt.dll`, aka release proxy): the DLL a player's game folder gets
beside `ds.exe`. The game asks Windows for `avrt.dll` but does not ship one, so adding it replaces no game file. Without
a valid [launcher ticket](#launcher-ticket) it only passes calls through to Windows' own `avrt.dll`; with one it
[redirects](#redirect) the saves, enforces the [network](#network-policy) and [module](#module-policy) policies and
loads the [mod](#mod). Compare [lab proxy](#lab-proxy). *Where:* `redirect/` (`make -C redirect build/avrt.dll`).

<a id="presence"></a>**Presence**: what each game tells the others about its player many times a second: position,
rotation, speed, state flags (riding, down, caught, calling out) and animation. It is shown only and changes nothing
in anyone's world. *Where:* `core/`.

<a id="presence-log"></a>**Presence log** (`presence-<pid>.jsonl`): the mod's record of the local and the remote
pose, one JSON line per sample, capped in size. Used to measure how closely the shown player follows the real one,
and as a recorded walk that [stand-ins](#stand-in) can replay. *Where:* `mod/analyse_presence.py` reads it.

<a id="redirect"></a>**Redirect** (aka the redirect, `redirect-data`): what the proxy does to keep co-op apart from
everything else: every save, settings and profile path the game uses is moved into `redirect-data\` of the
[co-op data folder](#co-op-data-folder) (or of a lab [copy](#copy)), so the player's ordinary saves are never read
or written. Also the name of the folder holding the proxy source. *Where:* `redirect/path_map.cpp`.

<a id="relay"></a>**Relay**: the UDP part of the [world server](#world-server) that carries the real-time layer for
group play: every game says where its player is, the relay works out who is near whom (by default 150 m to become
near, 200 m to stop) and passes on their updates, to nobody else. It runs in [pairs](#pairs) or [broadcast](#broadcast) mode.
*Where:* `world/relay.h`, `core/relay.h`.

<a id="resident-double"></a>**Resident double** (the game's `DSPlayerAKukanEntity`): a second Sam the game itself keeps,
parked and hidden, probably for the private room. In `partner: "peer"` mode the mod borrows it to show the partner;
the launcher's setup uses `own` (a [body](#body) the mod makes) and falls back to the double only until that body
exists. *Where:* `mod/double.h`; `mod/game_offsets.md`, "The resident Sam double".

<a id="review"></a>**Review**: under a policy that asks for it, a shared build waits until the server owner accepts
it (launcher window Server owner).

<a id="role"></a>**Role** (`role` in `mod.json`): `host` or `client` in a two-player session; the core requires the two
ends to differ. Between other players in group play the lower player id is the host of the pair. It says nothing
about who runs a server. See the table above.

<a id="round-trip"></a>**Round trip** (aka RTT, ping): the time a packet takes to the other side and back, measured by
the [core](#core) with its own pings and used to line up the two games' clocks. It is part of what a slow
[link](#link) costs, alongside the [delay](#delay).

<a id="server-owner"></a>**Server owner**: the player whose [player key](#player-key) started the
[world server](#world-server), normally the group's host. Through the launcher window Server owner they
[review](#review) builds, accept or reject a player, remove a player's objects and change the rules on the running
server. *Where:* `launcher/owner.h`, `world/api.h`.

<a id="server-policy"></a>**Server policy** (aka rules, presets Friends, Solo, Public): a world server's rules: whose
builds are shared, whether they wait for the owner's [review](#review), limits per player, whether a withdrawn object
stays as a remnant, and [identity](#identity-keys). The host picks a preset for a new world; each player's own
choices are kept per server. *Where:* `world/world.h`; `world/README.md`, "Server policy".

<a id="session-id"></a>**Session** (`session` in `mod.json`): the shared number that ties two games (or a group)
together; in a lobby it is the invite's secret, in a group the group's key. A run of one game process is a "run",
not a session.

<a id="setup"></a>**Setup**: putting the co-op files into the game. Today the launcher does it when started from the
extracted package: `avrt.dll` into the Steam game folder, `mod.dll` into the [co-op data folder](#co-op-data-folder);
About → Remove co-op from the game takes it out, keeping saves. `install/coop_install.py` does the same from a
script. A separate `DeathStrandingCoop-Setup.exe` with `status`, `install`, `uninstall` is **(planned)**. *Where:*
`launcher/core.h` (`setupInstall`), `install/`.

<a id="shared-world"></a>**Shared world** (setting): the two-player launcher setting that starts a
[world server](#world-server) for a lobby session, so both games sign in to the players' own
[strand server](#strand-server). For more players the setting is "Group server" ([group](#group)).

<a id="solo"></a>**Solo** (Play solo): starting the game through the launcher from the co-op saves with nobody else.
The proxy still redirects saves and enforces the network policy. Not the same as the server preset Solo (a world for
one player's archive, [server policy](#server-policy)).

<a id="stand-in"></a>**Stand-in** (`coop-partner.exe`): a test program that plays the partner's side of the protocol
without a game: it follows the porter, replays a recorded walk, drops or carries cargo. It exists because two game
copies on one GPU slow each other down. Never a real player. *Where:* `mod/partner.h`, built as `coop-partner`.

<a id="strand-notices"></a>**Strand notices** (`strand_notices`): lines in the game's info log that tell a player
what a [strand server](#strand-server) did with something they built ("Your ladder is not shared on North: ...").
The mod reads them with a notice token the launcher took. *Where:* `mod/strand_notices.h`.

<a id="strand-server"></a>**Strand server** (aka private server, shared world, `online_server`): the HTTP part of the
[world server](#world-server). The game signs in to it instead of [the publisher's service](#publishers-service) and
gets other players' structures, names, likes and road materials from it, under the [server
policy](#server-policy). Compare [relay](#relay). *Where:* `world/api.h`, `world/world.h`, `mod/online.h`.

<a id="swarm"></a>**Swarm** (`coop-swarm`): many [stand-ins](#stand-in) in one process that join a [relay](#relay)
and walk in circles, to test crowds without many games. *Where:* `mod/swarm.cpp`.

<a id="tick"></a>**Tick**: one round of the mod's work, on the [game thread](#game-thread) (at most 20 a second) or on
the network worker. In [broadcast mode](#broadcast) a game sends one update per tick.

<a id="tickets-and-tokens"></a>**Tickets and tokens**: short-lived secrets the launcher takes from a
[world server](#world-server) for one game start: the *sign-in ticket* in `online_server` (`/t/TICKET/`), with which
the game signs in under [identity keys](#identity-keys); the *relay token* in `relay_key`; and the *notice token*
(`strand_token`) for [strand notices](#strand-notices). Diagnostics mask them. Not the
[launcher ticket](#launcher-ticket), which is between the launcher and the proxy on one PC.

<a id="version"></a>**Version**: the release name a player sees, such as `0.1.0-alpha.1`. For the alpha it is taken
from the Git [tag](#tag) and written into the launcher and `coop-mod.json` **(planned)**; this checkout still says
`0.1.0-dev`. Not the [wire identity](#wire-identity) and not the game's [build](#supported-build).

<a id="visitor"></a>**Visitor**: in the [fight channel](#fight-channel), a player whose game joins a catcher fight
that another game owns. The visitor's game shows its own copy of the fight; their hits and destroyed gazers go to the
owner, whose game decides. A visitor's mod never announces a fight or destroys a gazer itself; when the owner's fight
is cleared, the visitor's copy ends the same way.

<a id="wire-identity"></a>**Wire identity** (aka build id, `WireBuild`, `ModBuild`): a 128-bit fingerprint of the
mod's and core's source files. Two games only talk when their fingerprints match, so both players need the same
build; the server's broadcast mode keeps different builds apart too. It changes whenever one of the listed sources
changes and is updated deliberately with `python3 mod/build_id.py --write`. *Where:* `mod/identity.h`,
`mod/build_id.py`, `core/build_id.h`.

<a id="world-file"></a>**World file** (`world.json`): where a [world server](#world-server) keeps everything:
objects, players and their choices, likes, road materials, the policy. The launcher keeps it in `world\` in the
[co-op data folder](#co-op-data-folder) between sessions; diagnostics never collect it.

<a id="world-server"></a>**World server** (`coop-world.exe`): the players' own server, one program with two parts:
the [strand server](#strand-server) (TCP/HTTP) and the [relay](#relay) (UDP), on one port number (49100 by
default). The group's host runs it through the launcher, which starts it to stop by itself six hours after the last
player; it never talks to the publisher's service. *Where:* `world/`.

## Game terms

How the mod uses the game's own words. The game is DEATH STRANDING DIRECTOR'S CUT on PC; the mod's research notes
are in `mod/game_offsets.md`.

<a id="area"></a>**Area**: the game's division of the map for online objects (a number such as 226). A game loading
an area asks the server for so many objects of each kind for it, and takes only as many as the area has room for.
The server's calls name it with a "qpid" (`findQpidObjects`).

<a id="auto-paver"></a>**Auto-paver** (aka paver): the machine that builds a stretch of road once enough materials are
delivered to it. The mod can share paver deliveries between two players (`share_roads`) and the strand server adds up
every player's deliveries ("Roads paved together", not yet taken by a game).

<a id="bt"></a>**BT** (Beached Thing): the invisible enemies of timefall areas. A BT area is active while it rains
there. The mod shares BTs destroyed, being caught and the catcher fight between two players (`share_enemies`), and
lets visitors join one game's catcher fight in group play ([fight channel](#fight-channel)). See [gazer](#gazer),
[catcher](#catcher), [voidout](#voidout).

<a id="camp"></a>**Camp**: a [MULE](#mule) base. With `share_enemies` a camp alerted in one game is alerted in the
partner's.

<a id="cargo"></a>**Cargo** (aka baggage, the game's code word; case): what porters carry and deliver. A *case* is the
container the game draws; the mod draws the partner's or other players' carried cargo as the game's own cases on their
body (`carry_visual`, `others_carry`) without making any real cargo. Two-player cargo sharing moves real items put down
or picked up. *Where:* `mod/cargo.h`, `mod/carry_visual.h`.

<a id="catcher"></a>**Catcher**: the large BT that drags a caught porter into a boss fight in a tar pool. Its fight is
the one the [fight channel](#fight-channel) shares.

<a id="checkpoint"></a>**Checkpoint**: one save point the game writes and loads from (an autosave or a manual save).
The launcher can hand the host's newest checkpoint to the guest, and story catch-up (`story_resync`) loads the host's.
Compare [saves](#saves).

<a id="chiral-network"></a>**Chiral Network**: the game's network that the player extends by connecting facilities.
Building structures and seeing other players' online objects need a connected region. The mod does not change it: each player connects their own world in their own story.

<a id="director-s-cut"></a>**Director's Cut**: the edition the mod supports. See [supported build](#supported-build).

<a id="episode"></a>**Episode**: a chapter of the story. The mod reads the episode from the game and tells two
players when they are in different ones. Test saves are named by episode (Episode 1, Episode 3).

<a id="facility"></a>**Facility** (knots, preppers, distribution centres): places with a terminal where orders are
taken and delivered. Each has a connection level (stars) that rises with deliveries; `share_rewards` shares it
between two players. *Where:* `mod/rewards.h`.

<a id="gazer"></a>**Gazer**: the floating BTs that search a BT area and catch a porter they find. Destroying gazers is
shared in two-player play; in a visited fight a visitor's destroyed gazers are reported to the owner.

<a id="info-log"></a>**Info log**: the game's message feed in the corner of the screen. The mod's [notices](#notices)
and [strand notices](#strand-notices) appear there as the game's own lines.

<a id="knot"></a>**Knot** (Knot City): the large cities of the story, each a facility. Capital Knot City is in the east
where the story starts; Lake Knot City is the central region's, where much of the mod's live testing took place (its
garage for vehicles). The game's limits per area are often quoted for Lake Knot (5 cairns, no ladders).

<a id="like"></a>**Like**: the game's thanks between players, counted and saved, part of a porter's rating. Likes for
the partner (`partner_likes`, key L) are real saved likes, so they are off by default. The strand server stores likes
given to objects and names a builder's likes over a structure.

<a id="mule"></a>**MULE**: the human enemies who steal cargo, in [camps](#camp). With `share_enemies` a MULE put down in
one game is put down in the partner's; where MULEs walk is not shared (an experiment, `share_enemy_positions`, is
parked).

<a id="odradek"></a>**Odradek**: Sam's scanner on his shoulder. A scan can be shown to the partner (`partner_scan`).

<a id="order"></a>**Order** (the game's code: mission): a delivery job, standard or story (main). Two-player sharing
can split an order's [units](#unit) between both players (`share_orders`, `share_order_units`); story and order
progress are otherwise each player's own.

<a id="porter"></a>**Porter**: the game's word for a deliverer. In the mod's code and notes "the porter" is always
the local player's own Sam, as opposed to the [body](#body) drawn for someone else.

<a id="private-room"></a>**Private room**: Sam's resting room. While a player is in it (or in a cutscene) their body
is hidden in the others' games.

<a id="publishers-service"></a>**The publisher's service** (aka the original online service): the game company's
servers for the Social Strand System. Modded games never contact it: the [network policy](#network-policy) refuses
it and the mod replaces it with the [strand server](#strand-server).

<a id="sam"></a>**Sam** (Sam Porter Bridges): the character every player plays. Each player has their own Sam in their
own save; the mod draws other players' Sams as [bodies](#body).

<a id="saves"></a>**Saves**: a player's set of [checkpoints](#checkpoint), settings and profile. *Co-op saves* are the
ones in the [co-op data folder](#co-op-data-folder), which co-op plays from; *ordinary saves* are the game's usual
ones, which co-op never writes. The launcher's Saves tab can copy the ordinary saves into co-op (read only) and back
the co-op ones up. Test saves for testers are a separate download, not in this repository.

<a id="social-strand-system"></a>**Social Strand System**: the game's asynchronous online play: structures, signs,
lost cargo, likes and roads from players you never meet appear in your world. The mod's [strand
server](#strand-server) does this job for a group, sharing everything rather than a selection.

<a id="strand"></a>**Strand**: in the game, the connections between players; in the mod, a player's *strand* is what
they built and shared through a server ("My strand on this server" in the launcher). Compare
[Social Strand System](#social-strand-system).

<a id="structure"></a>**Structure**: something a player builds into the world: ladder, climbing rope, bridge, rain
(timefall) shelter, postbox, generator (charger), watchtower, safe house, zip-line, sign, and the cairns (stone piles; the game's `PileStone`) that the server hands out
too. Two-player play shares structures and their levels; the strand server hands structures of other players to
every game, and [build received](#build-received) builds them at once. *Where:* `mod/structures.h`, `mod/received.h`.

<a id="supported-build"></a>**Supported build**: Steam app 1850570, build 13300582, identified by the SHA-256 of
`ds.exe` in `STATUS.md`. The launcher and the mod refuse any other build rather than risk a crash.

<a id="terminal"></a>**Terminal**: the delivery terminal at a [facility](#facility), where orders are taken and cargo
handed in, and its menus. Paver deliveries the server counts come from a paver's terminal.

<a id="timefall"></a>**Timefall**: the game's rain, which ages what it touches and wakes BT areas. Two-player play can
share the host's rain with the guest (`share_environment: "rain"`). *Where:* `mod/environment.h`, `mod/weather.h`.

<a id="unit"></a>**Unit**: one cargo item of an order; an order is complete when all its units are delivered.
*Where:* `mod/orders.h`.

<a id="vehicle"></a>**Vehicle**: the bike (reverse trike) and the truck. The mod draws the partner's vehicle under them
in its paint (`partner_vehicle`) and lets the other player ride along (key J). Floating carriers are the hovering
carts (`partner_cart`). *Where:* `mod/vehicle.h`, `mod/ride.h`, `mod/carts.h`.

<a id="voidout"></a>**Voidout**: the explosion when a BT takes a porter, leaving a crater. Sharing the crater between
two players exists and is parked (opt-in).

## Testing words

<a id="baseline"></a>**Baseline** **(lab only)**: the test save a game [copy](#copy) is reset to before a run (named by
episode), and the hash of the saves taken before a run so they can be compared after it.

<a id="copy"></a>**Copy** (aka test copy, slot) **(lab only)**: a disposable copy of the game folder with its own saves,
run with the [lab proxy](#lab-proxy); two copies run at once on one PC as host and client. Its slot number (0, 1, ...)
also chooses the mod's UDP port, `49000 + slot`, which is the `slot` field of `mod.json` for players too.

<a id="evidence"></a>**Evidence**: what a check produced that someone else can look at: values read from the game
process, the mod's log lines, the server's records, saves compared by hash. `STATUS.md` records each feature with its
date and evidence. What has not been shown is written as unknown, not guessed.

<a id="fake-memory"></a>**Fake memory, fake world**: game-free tests in which the mod runs against a pretend game:
fake memory lays out game objects as the real game does (`mod/fake_memory.h`), and the fake world runs the real mod
against a real built `coop-world`.

<a id="game-free"></a>**Game-free** (aka T0): any check that runs without the game: builds, unit tests, simulators,
fuzzers, [mutants](#mutant). `make test` runs them all.

<a id="live-run"></a>**Live run** (aka live test): a test in the real game. Most live runs so far used two lab
[copies](#copy) on one PC; a real two-PC run and runs with the [player proxy](#player-proxy) in a real install are
newer. "Live once" in `STATUS.md` means one such run.

<a id="mutant"></a>**Mutant**: a deliberately broken build of one protection (a compile flag such as
`MUTANT_LINK_NO_LOSS`) that its test must catch. A test that passes on its mutant proves nothing. Every protection has
one; `make test` runs them side by side. *Where:* `mod/mutants.py` and each folder's `test_mutants.py`.

<a id="prepush"></a>**Prepush** **(lab only)**: the owner's last check before verified work goes to `main`: live
regressions and the repository checks must pass.

<a id="probe"></a>**Probe**: a small program or run that checks one thing. The proxy's Windows probes
(`redirect_probe.exe`, `net_probe.exe`) load it without the game; `mod/test_probe.py` runs the mod on a test
transport; `puppet_probe` in `mod.json` lets the mod validate and call the game's entity functions; the launcher's
LAN probe finds lobbies.

<a id="regression"></a>**Regression**: a repeatable test of something that worked before, run to show it still
does; a *live regression* runs it in the game unattended (for example a stand-in riding a bike beside the porter).

<a id="smoke"></a>**Smoke test**: a short run that shows the pieces start and talk to each other at all, without
checking details (for example the two-stand-in wire smoke in `mod/README.md`).

<a id="tag"></a>**Tag**: a Git tag that names a release (such as `v0.1.0-alpha.1`); the alpha's [version](#version)
string will be made from it. Not the name tag over a body (`partner_tag`).

<a id="test-command"></a>**Test command**: a file a test drops into the mod folder (such as `puppet.double` or
[`pad.script`](#pad-script)), accepted only by the game process whose run stamp (`<pid>-<start time>`) it names, so
a command can never reach the wrong game.
