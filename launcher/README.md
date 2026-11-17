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
- **Start from the host's save** (guest, on by default): the lobby shows the title of the host's newest co-op save.
  Choosing Ready first fetches that save over the lobby's own connection, re-signs it for the guest's Steam account
  and makes it the guest's only co-op save, so both games continue in the same world and episode. The co-op saves the
  guest had move to `backups\before-join-<time>` (never deleted); the same save offered again changes nothing. Untick
  the box to keep playing from your own co-op save. A host without a co-op save offers nothing.
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
ping key), his vehicle while he rides (`partner_vehicle`), the floating carriers he tows with their cases
(`partner_cart`), the cargo he carries as cases on his back
(`carry_visual`), his name over his head (`partner_tag`), notices in the game's info log when he joins or leaves or
nobody has arrived yet (`notices`), the host's rain and timefall in the guest's world
(`share_environment:"rain"`: rain only, never the host's hour), and with them shared cargo and structures.
It also shares what each player does to the world (`share_enemies`, `share_orders`, `share_order_units`,
`share_roads`, `share_saved_structures`): MULEs put down and camps alerted, BTs destroyed, the boss fight with one
health bar, standard orders and the cargo units handed in, roads and paver deliveries, structures with their levels
and materials (on joining, each world also gets what the other's save already has).
Keys in the game: **J** next to the partner's truck rides along on it and gets off again (`passenger_key`); **L**
sends a like only with `partner_likes`, which this setup leaves off.
Automatic Continue and the virtual pad stay off. Existing environment-driven test
tools still work independently of this app.

## Shared world (experimental, off by default)

Settings has "Shared world". With it on, the game's own online layer runs on a server of the group's own
(`../world/README.md`): the host's launcher starts `coop-world.exe` (beside the launcher) when the session starts, and
each game gets the server's address as `online_server`. The host's setting decides for the session: it travels in
the lobby packets and replaces the guest's own. Both launchers must be this version (an older one refuses the packets).

- The server listens on TCP port 49100 + the session's slot (49100 for UDP port 49000), on loopback when both games
  are on one PC and on all addresses otherwise. The first path element is `w` and the session secret, so only the
  session's players reach it. The host's Windows Firewall has to let `coop-world.exe` receive on that port.
- The host's game signs in at `127.0.0.1`, the guest's at the host's address. On one PC both games are one Steam
  account, so the address also names the player (`/k/host`, `/k/guest`).
- The world file is `world\world.json` in the co-op data folder; it is kept between sessions. The server leaves by
  itself six hours after the last request.
- Not in a solo session. `--shared-world` turns it on for one start from the command line.

What it gives today is small: ladders and cairns each player builds are stored and handed to the other game, which
takes only the kinds its area still has room for (`../STATUS.md`).

## A group on one server (experimental, off by default; built game-free, not run live)

For more than two players there is no partner and no lobby: one player hosts the group's server, and every game
talks to that server only (`../README.md`, "A group on one server"; `../mod/README.md`, "Group play"). Everyone plays
their own co-op save. Near each other, players see each other (presence only, with names); the server also carries
the game's own online layer as under "Shared world".

**Host.** Settings, "Group server", and "Rules of a new group world": Friends (everything shared), Solo archive (one
player) or Public (members' builds only, the owner's review, 200 objects per player, withdrawn objects stay as
remnants); `group_policy` in `launcher.ini`, `../world/README.md` "Server policy" for what each means. Then Play, Host
a game shows the group invite and **Start group server and play** instead of a lobby:

1. The launcher makes the group's key once (a random nonzero 32-bit number, `group_secret` in `launcher.ini`), so
   the invite stays good between sessions. Delete that line for a new key.
2. It starts `coop-world.exe` (beside the launcher) with `--port P --bind 0.0.0.0 --prefix wKEY --data
   world\world.json --idle-exit 21600 --policy RULES --relay-port P --relay-key wKEY --owner HOSTKEY`: the shared world
   on TCP port P and the relay on UDP port P, P = 49100 + the slot of the port in Settings (49100 by default), and the
   host's player key as the owner's (below, "Player keys"). `--policy` gives a new
   world its rules; a world that exists keeps its own (the owner changes them with `coop-world policy` while the
   server is stopped). A server already running keeps serving; the second start leaves. It stops by itself six hours
   after the last player.
3. It writes the host's `mod.json`: no `peer`, `server` `127.0.0.1`, `relay` `127.0.0.1:P`, `relay_key` `wKEY`,
   `session` KEY, `player_id`, `relay_mode` `broadcast` (since 2026-11-17: each game publishes once and the server
   copies to everyone near; `../mod/README.md`, "The broadcast mode"), `others` 15, `partner_tag`, `online_server` `http://127.0.0.1:P/wKEY/auth/ds`,
   `build_received` (below, "Other players' structures"), and the play setup's `game_thread`, `puppet_probe`,
   `required`; with a notice token also `strand_notices`, `offer_strand` and `strand_token` (below, "What the game is
   told"). The proxy's peer list is `127.0.0.1`. On a server that
   takes keys (Public) it first waits for the server to answer (at most ten seconds) and takes the game's sign-in
   ticket and relay token: `online_server` is then `http://127.0.0.1:P/wKEY/t/TICKET/auth/ds`, `relay_key` the token
   and `player_id` the relay id made from the key. The server now starts before the config is written (it used to
   start after it).
4. It starts the game as for any session. Nobody has to be there: friends join when they like.

The host's Windows Firewall has to let `coop-world.exe` receive on port P, TCP and UDP (Windows asks when the
server first listens). The launcher does not check or change that.

**Joiner.** Join a friend, paste the group invite (the code, the join link or the `deathstranding-coop://` link; a
copied one is offered from the clipboard), **Join group**:

1. The launcher tries a TCP connection to the server port on every address in the invite, all at once for at most
   1.5 s, and takes the first address in the invite's order (LAN before Tailscale) that answered. None: it says so
   and starts nothing.
2. It shows the server's rules and asks for the joiner's choices there (below, "Before you join"). Cancel: nothing is
   started.
3. It writes the joiner's `mod.json`: the same keys as the host's with that address as `server`, in `relay` and in
   `online_server`, and the joiner's own `player_id` (on a server that takes keys: his ticket, his relay token and
   the relay id of his key, as for the host). The proxy's peer list is that one address.
4. It starts the game. There is no Ready or Start and the host's launcher need not be open, only the server.

A joiner on the host's own PC (two copies, one Steam account) reaches the server on loopback and signs in to the
shared world under a name made from its player id (`/k/p<id>`).

**The group invite** is `DSG-` followed by the same CRC-checked v1 payload as a lobby invite (`DS-`): the secret is
the group's key, the port is the server's port P (49100 to 49114, TCP and UDP), the routes are the host's LAN and
Tailscale addresses. A lobby invite still opens a two-player lobby as before. A launcher from before this change
does not know the `DSG` prefix and does not take a group invite for a lobby (it reads the text as a host name and
finds no host). Whoever has the invite can join; it is the only secret, and it travels in the clear (LAN or VPN).

**The player number** (`player_id`, `launcher.ini`) is made once from 49 random bits of the system's generator (1 to
562949953421312, inside the 15 digits the mod's config takes) and kept. It is not the Steam id and says nothing about
the player; two installs have two numbers. On a server that takes keys the game uses the number made from the
player's key instead (the same range), which nobody else can claim there.

**Player keys** (`../world/README.md`, "Identity"). The launcher makes an Ed25519 key pair once and keeps it in
`player.key` in the co-op data folder, beside `launcher.ini` (Monocypher, `../vendor/monocypher`). The secret half
never leaves the PC: it signs the launcher's calls to a group's server and is never sent; the public half is the
player on servers whose rules say `identity keys` (the Public rules). Settings shows the key short (its first 16 hex
digits in fours). A `player.key` that cannot be read is an error and is never replaced: a new key is a new player on
those servers. Keep the file private and keep a copy of it if you play on public servers (Back up does not copy it);
delete it to become a new player. The Steam id is not used there. On such a server:

- every call the launcher makes but reading the rules is signed, and acts for the key's account only;
- just before the game starts the launcher takes a sign-in ticket and a relay token for it (signed calls); the game's
  `online_server` carries the ticket instead of a name, and its `relay_key` the token instead of the group's key. A
  ticket is good for 30 minutes until the game signs in, so start the game from the launcher each time;
- the host's key is the owner's: his launcher gives it to the server (`--owner`, whatever the rules, not only Public),
  and the owner's actions on the running server (review, members, removing a player, the rules) are calls signed with
  it, made from the Server owner window (below).

On a server without keys (Friends, Solo archive, or a world from before) nothing of this changes anything.

**What the game is told about the player's builds** (`../mod/README.md`, "Strand notices"; `../world/README.md`, "What
the game is told"). After the choices (and, on a server that takes keys, the passes), the host's and a joiner's
launcher take a notice token for the player's game (`POST …/coop/notice-token`, signed where calls are signed) and
write it into the group `mod.json` as `strand_token`, with `strand_notices` on. With it the game's info log says once
when something the player built is not shared there and why, and when it becomes shared. The token reads that
player's own objects' states and nothing else; a new join ends the older one. A server that gives none (one from
before this call) only means no such lines: the game starts all the same. The two-player shared world takes no
token (its server has no join step), and no partner notice is turned on by this. Chosen this way: on in the
launcher, because the player has just seen the rules and should not be surprised later by what a build of his became;
off in a `mod.json` written by hand unless it says `strand_notices`, as every feature of the mod.

With the token the launcher also writes `offer_strand` on (since 2026-11-17; `../mod/README.md`, "Offering your
existing strand"): the game reads the server's word on the player's older strand with the same token every 5 s and
has the structures his save already holds uploaded to that server, one at a time, only while that word is yes. The
word is his own choice for that server (`offer_old`, "Let this server take my older strand" below; default no), so
the option on changes nothing until he says yes there, and stops when he says no again. Without a token (an older
server) it is not written, as the mod needs the token for it.

**Other players' structures** (since 2026-11-17). Settings has "Build other players' structures when they arrive
(group servers)", on by default (`build_received` in `launcher.ini`). The host's and every joiner's group config
write it as `build_received` (`../mod/README.md`, "Received structures built"): the bridges and rain shelters the
server hands the game are built at once, where the game would otherwise hold another player's bridge back until the
porter is 1 km away and take received rain shelters down. The trade-off, said beside the box: they skip the game's
spacing and area checks, so one may overlap something in the player's world or stand where the game keeps other
players' structures out (the server keeps the objects of one kind in one answer 20 m apart, nothing more). Turned off, the config
says `"build_received": false` and the game keeps its own rules. It is each player's own setting, not the host's;
it does not apply to the two-player shared world.

**Before you join: the server's rules.** The rule behind it: before a game connects, its player sees what the server
will take from him and what it will show him; the server's owner sets the server's rules, the player sets the rules
of his own contributions. So before the joiner's game starts, the launcher asks the server (its launcher API,
`../world/README.md` "The launcher's API") and shows one screen:

- The rules in words, one line per setting (whose builds are shared, whether the owner reviews them first, the limit
  per player, what a withdrawal does, how many players, and the rest), with what the owner changed from the preset
  and how many players the world has. Then: "Anyone who has this group's invite can act for any player here: there
  are no player accounts yet. Join friends' servers only." (On a server that takes keys instead: "This server knows
  players by their keys: your launcher's key is you here, and nobody else can sign in, build or withdraw in your name.
  The traffic is not encrypted: use a LAN or VPN.") Then what the joiner already has there (shared, waiting
  for his answer or the owner's, held by the rules, kept, remnants, likes), or that he has not played there yet.
- His two choices for this server: **Share what I build here**: Yes (the other players can be handed it, as the rules
  say), No (kept for him alone, a personal archive on that server) or Ask me (it waits until he offers it, below);
  **Let this server take my older strand** (the structures already in his save, built before he joined there):
  **Yes** ("my game uploads them to this server one by one while I play"; then as the rules say for any upload) or
  **No** ("only what I build while connected goes to this server"). The server keeps it and tells his
  game's mod, and the launcher's group config has `offer_strand` on (above), so Yes has his game upload them, one at a
  time, while he plays (`../mod/README.md`, "Offering your existing strand"; live once,
  not yet started from the launcher). Ticked: what the
  server has for him if he chose there before, else what this launcher remembers for this server, else the server's
  own values for a player who chose nothing (its `offer_builds` and `offer_old`).
- **Join with these choices**: they are remembered in `launcher.ini` under `[server-ID]` (the world's id, so a server
  at another address is the same server) and sent to the server for the joiner's account, then the game starts.

The account is the one the joiner's game signs in as: on another PC his Steam account (the 64-bit id of the account
signed in to Steam; Steam must be running), on the host's PC the name in his sign-in address (`p<id>`), never the
host's account; on a server that takes keys, his key's account (Steam need not run). When it goes wrong, nothing is started and the launcher says why: the server does not answer, it does
not know the group's key, its answer cannot be read, or it did not take the choices (a full world, for example). A
server from before this change (it answers the game but has no launcher API) is said so ("it shares what every
player builds with every player, and has no choices for you") and joined with its ways.

The host gets no such screen: he chose the rules. His own choices are under "My choices for this server" (below).

**My strand on this server.** After hosting or joining a group server, Play has **My strand on this server** (for the
server this launcher last hosted or joined since it started). It opens a window with the server's rules, what the
player has there and his choices, and the list of his objects built there: kind, area, state (shared, waits for
your answer, waits for the owner, held by the rules and why, kept for you, remnant), likes and id. Select rows (Shift
and Ctrl for several), then:

- **Offer selected** / **Offer all**: what waits for his answer goes on (shared, or to the owner's review); what the
  rules hold is shared only once they allow it, else the server says why and nothing changes.
- **Withdraw selected** / **Withdraw all**: asks first, with what this server does with it (stays for the others as a
  remnant nobody owns, or removed for everyone); the server does not touch his save.
- **My choices for this server**: the same screen as before joining; saved, remembered and sent.
- **Who I see**: who of the players near him in the game the server's relay shows him (`../world/README.md`, "How
  many players one player sees"): everyone near (at most the server's number, his friends first, then the nearest),
  only his friends, nobody (the others still see him), or his game's own setting (`show_others`); and his friend list
  on this server, a friend added by the short form of his key (`a1b2 c3d4 e5f6 a7b8`, shown in the same window for
  him to give his friends) and removed by selecting him. Each change is one call (`GET`/`POST …/coop/friends`), signed
  on a server with player keys; on one without them friends are not recognised on the relay, and the window says so.
- **Refresh**.

Objects that came to the server from linked servers are not listed (their builder is what the other server says).
The owner's actions are in the Server owner window (below).

**Server owner.** After hosting a group server, Play also has **Server owner** beside "My strand on this server". It is
there only for the host who holds the owner key: the server this launcher last hosted, started with this install's key
as the owner's (`--owner`, "Player keys"); a joiner, a two-player session or another key never sees it. Every call it
makes is signed with that key on the server's clock (`../world/README.md`, "Identity": `/coop/owner/...`), whatever the
rules: on a Friends server too, where players' own calls are not signed. The window shows:

- The server: its display name (`--name` of `coop-world`, "Builder names" in `../world/README.md`), its id, its preset
  and how many players it has.
- **Players**: name, key short (the first 16 hex digits in fours; `no key` for an account that is none, whose Steam id
  is not shown), member or not, and his counts: shared, waiting, held, kept, remnants, from linked servers, and the likes
  on what is still his. A builder known only from linked servers is listed as the game names him ("Name · Home").
  Select one, then **Make member**, **Not a member**, **Accept all of player** (everything of his that waits for the
  owner, and what the rules held that they now allow), **Reject all of player** (everything of his that waits for the
  owner is kept for him alone, not removed; asks first) or **Remove all of player...**. Removal asks first with what it
  removes (his counts), what this server's withdrawal rule means (`stay`: a player who takes back what he shared leaves
  remnants under his name; `go`: it is removed for everyone) and that removal leaves nothing, his remnants included,
  and that it is not a ban: his account stays and he can sign in and build again.
- **Waiting for you, or held by the rules**: kind, area, builder and why (waits for you, held and why, or waits for its
  builder's answer first). Select rows, then **Accept selected** (every player can be handed it) or **Reject selected**
  (kept for its builder). What waits for its builder's answer is skipped: it is not the owner's to answer.
- **Rules**: one row per setting with its value, the preset's value where it differs, and the server's own words
  (`/coop/policy`). Select one, type the new value, **Set selected rule**; or choose a preset and **Use this preset**
  (asks first: every setting becomes the preset's). A value the server does not take is refused in its words and
  changes nothing. A change applies to uploads from then on.
- What the last action did, in the server's words when it refused; **Refresh**, **Close**.

Without the window, for tests (`--dev-direct`, no `--dev-start`; the server this launcher hosts on this PC, on the port
of `--port`, with the key of `--dev-session N` or the kept `group_secret`): one of `--owner-list`, `--owner-member
PLAYER yes|no`, `--owner-remove PLAYER`, `--owner-accept OBJECT`, `--owner-reject OBJECT`, `--owner-accept-player
PLAYER`, `--owner-reject-player PLAYER`, or `--owner-preset NAME` and/or `--owner-set KEY=VALUE` (repeated), with
`--owner-report FILE` (or `-` for standard output). It makes the call and writes one line of JSON: `ok`, `action`,
`message` (what happened, or why not), `server`, `name`, and after the action the server's `policy`, `players` and
`waiting` as the server sent them. Exit 0, or 3 when anything was refused (no server, not the owner, a refusal, two
actions, no report file): then there is no dialog. `PLAYER` is the id the report lists (`zygo_...`).

Without the window, for tests (`--dev-direct --dev-start`, with `--prepare-only` to write the configuration only):
`--group-host` hosts (key: `--dev-session N`, else the kept one; `--group-policy friends|solo|public` for a new world's
rules), `--group-join DSG-...` joins by invite, and `--group-join A.B.C.D[:P]` joins a server by address with
`--dev-session N` as the key, without the connection test. A join reads the rules first and sends the remembered
choices (also with `--prepare-only`); `--strand-builds yes|no|ask` and `--strand-old yes|no` choose instead, and
`--strand-policy FILE` (or `-` for standard output) writes one line of JSON: `fetched` (`ok`, `old`, `wrong-key`,
`unreachable`, `bad`), `start`, `message`, `account`, `keyed` (the server takes keys), `choices` and the server's
`policy` as it sent it. A join that is refused shows no dialog and exits 3. On a server that takes keys a join (also
with `--prepare-only`) takes the game's ticket and relay token and writes them into the config; `--group-host` with
`--prepare-only` does so only when its server is already running (it starts none).

**Not seen on a screen.** The join screen, the "My strand on this server" window, its Who I see window, the Server owner window,
the Settings list of rules and the Settings box for other players' structures were built and cross-compiled, and have not been looked at or clicked. The launcher's side of the API (`strand.cpp`) is
tested on Linux sockets (below); on Windows it ran once without a window, against `coop-world.exe` on loopback: a
server that did not answer (report `unreachable`, exit 3) and a full solo world (the rules read, the choices refused,
exit 3, the choices remembered in `launcher.ini`). A join that goes on to start a game, and an older server, were not
run on Windows. The owner's flags ran once on Windows without a window (2026-11-17), against `coop-world.exe` on
loopback with public rules and the launcher's key as the owner's: no server (exit 3), the list, a setting changed
(`--owner-set limit=7`, report in a file), a member call for a player that does not exist (refused in the server's
words, exit 3), and two refused flag combinations (exit 3, no dialog).

Limits today: 15 other players shown per game and 16 players in the shared world (`../world/README.md`); the
two-player features (cargo, structures made in the session, vehicles, enemies, orders, rain) are not part of group
play; no list of who is in the group, no way to leave except closing the game, no check of the host's firewall; the
owner's window does not refresh by itself (Refresh), and there is no ban.

## Diagnostics for a bug report

About → **Send diagnostics** (or `deathstranding-coop.exe --diagnostics-zip`) saves a redacted diagnostics zip on
the Desktop and selects it in Explorer. Nothing is uploaded. It includes both log layouts, capped at 60 MiB of input
logs/dumps by default, masked settings, the text report, hardware/build/WER/Defender/overlay information, network
checks, a hashed mod-file listing, and `manifest.json`. The exporter and its privacy rules are embedded in the
launcher and run hidden using Windows PowerShell; see [dist/README.md](../dist/README.md). The developer-side
[reader](../tools/README.md) merges both players' timelines, aligns BUGMARKs and maps crashes.

`deathstranding-coop.exe --diagnostics` (which shows no window) still
writes `diagnostics-<time>.txt` into the co-op data folder: the launcher version, the game folder and whether its
executable is the supported build, which parts are installed, the mod config, the files in the data folder and the
last lines of the newest mod, proxy and crash logs. The session code, invites, a group's key (in the invite, the
server address and the relay key), a sign-in ticket and a game's notice token (`strand_token`) are hidden, addresses other than
this PC's loopback keep only their first two numbers, and the Windows user name in paths becomes `<user>`. The
report is at most 256 KiB and reads nothing outside the co-op data folder (never a save).

**Windows Firewall, checked.** The launcher reads Windows Firewall's rules (read-only, no administrator rights) when it
starts and when a lobby opens, and the lobby says in one sentence whether the game can receive the partner's data on
its two UDP ports: allowed, blocked (with where to change it), no rule yet (Windows will ask when the session starts:
choose Allow), firewall off, or not readable. Only rules that name the game's `ds.exe` count; a block rule wins. The
same sentence is in the diagnostics file, and `deathstranding-coop.exe --firewall-status` prints it without a window
(exit code: 0 unknown, 1 off, 2 allowed, 3 blocked, 4 no rule). The launcher does not change the firewall.

## Setup

Started from an extracted package (the launcher with `avrt.dll` and `redirect-data\\mod\\mod.dll` beside it), the
launcher asks once per start whether to install or update: it adds `avrt.dll` to the Steam game folder and the mod to
the co-op data folder. A different `avrt.dll` already there is named in the question before it is replaced. About,
**Remove co-op from the game** takes the proxy out again (only while it is this package's); saves stay.

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

**Copy my Steam saves into co-op** (Saves) copies the player's ordinary checkpoints into the co-op data, so co-op does
not have to start a new story; settings and key bindings come along when the co-op side has none. The ordinary saves
are only read. Co-op saves already there move to `backups\before-import-<time>`. It needs the game and lobby closed.

The save transfer (`saves.h`) is a pull on the lobby socket: `DSS1` query, offer (size, CRC-32, title), get(index),
chunk (1 KiB). Every datagram carries the lobby's secret and nonce and only the lobby's two endpoints are served;
the host keeps no state per guest. A file is handed over only when its checksum matches and it parses as a
checkpoint. The account a save belongs to is `steam_<64-bit id>` inside its encrypted block; the id comes from
the account signed in to Steam. Container and cipher after mi5hmash/DeathStrandingSaveDataResigner (MIT).

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
                       # saves: format against independent vectors, install,
                       # copy-in, transfer over a lossy link; fuzzers and mutants
make all               # mod, both proxies, launcher and Windows fixtures
```

`test_group.py` (part of `make -C launcher test`) starts the built `coop-world` with exactly the command line the
launcher makes for a group, signs in at the addresses the launcher writes for the host and for a joiner on the same
PC (two players), and lets three stand-in players (`coop-swarm`) holding the key and session of the launcher's mod
config meet through the relay; another key is not answered. Group mutants: `GROUP_PEER` (a two-player config
written), `GROUP_PEERS` (the proxy lists the partner instead of the server), `GROUP_NO_RELAY`, `GROUP_KEY` (the
games' key is not the server's), `GROUP_ID` (a player number the config refuses), `GROUP_ROUTE` (a route that did not
answer), `GROUP_INVITE_KIND` (a group invite taken for a lobby invite), `GROUP_SAME_PC`, `DIAG_GROUP_KEY`, `GROUP_POLICY`
(the host's rules not given to a new world).

The server's rules (`strand.h`): `core_test.cpp` checks the account a game signs in as, the policy read (and eleven
documents refused), the rules in words with what the owner changed, choices kept per server beside the launcher's
own settings, and the join against answers given in the test: the rules read and the choices sent, remembered choices
with given ones over them, a full server, no account, an older server (joined), a wrong key, no answer, an unreadable
answer and a server error (not joined); the strand view's rows, offer, withdraw and a refusal in the server's words;
the friend list and the choice of whom he sees read and changed (the bodies sent, a refusal in the server's words, a
choice that is none not read, an older server) and short keys as typed.
`test_group.py` does it on real sockets: the group server started with the host's rules (public), the joiner's
choices read, remembered and sent for the account his game signs in as and found on the server, his object listed as
held by the rules, his offer refused in the server's words, the host's account refused his object, his withdrawal;
then a wrong key, nobody listening and a full solo world (not joined), and a small HTTP server standing in for a
coop-world from before the API (joined, said so). Mutants: `STRAND_ACCOUNT` (a joiner on the host's PC acts as the
host's account), `STRAND_PARSE`, `STRAND_WRONG_KEY` (a server without the key taken for an older one),
`STRAND_OLD_SERVER` (an older server refused), `STRAND_SEND_FAIL` (the game started though the choices were refused),
`STRAND_FRIENDS_READ` (a choice of whom he sees that is none taken from the server's answer), `STRAND_DEFAULTS` (fixed choices instead of the server's), `STRAND_PER_SERVER` (one server's choices for another),
`STRAND_SETTINGS_KEEP` (saving the settings drops the choices); `GROUP_KEY`, `GROUP_NO_RELAY`, `GROUP_SAME_PC`,
`GROUP_POLICY`, `STRAND_ACCOUNT`, `STRAND_WRONG_KEY`, `STRAND_OLD_SERVER` and `STRAND_SEND_FAIL` must also fail the
socket test. `test_group.py` runs the public rules with `identity=none`.

Player keys: `core_test.cpp` checks the key made once and kept, a damaged key file refused and left as it was; the
config with a ticket and a relay token (host, joiner, a joiner on the host's PC: the ticket, never a name), the host's
key as the owner's, the ticket hidden in diagnostics; and the join against a stand-in server that checks every
signature as the server does: a server that takes keys joined as the key's account, every call signed on the server's
clock, the ticket, token and relay id taken and written into the game's config, the secret half in no request; no
ticket (not started), a server that names another relay id, no key (nothing sent), a server without keys (unsigned,
no passes, as before), and the host's own passes. Mutants: `KEYS_KEEP` (a new key every start), `STRAND_SIGN`
(unsigned calls), `STRAND_NO_PASSES` (the game started without a ticket), `GROUP_TICKET`, `GROUP_RELAY_TOKEN` (the
config keeps the name and the group's key), `DIAG_TICKET`.

Notice tokens: `core_test.cpp` checks the token taken after the choices for the same account (signed on a server that
takes keys, and for the host's own game with and without keys), written into the group config with `strand_notices`
and nothing without one, a join that goes on when the server gives none or one that is not a token, no new token for
the choices alone, and the token hidden in diagnostics. Mutants: `GROUP_NOTICE_TOKEN` (the config leaves it out),
`STRAND_NOTICE_FATAL` (no token stops the game), `DIAG_NOTICE_TOKEN`. On the built `coop-world`, `test_group.py`
(identity none) and `test_identity.py` (keys) check that each game's token reads its own builds there, unsigned, and
that a made-up one reads nothing. `test_identity.py` (part of `make -C launcher test`) runs
the launcher's side as its `--dev-direct --dev-start --group-host`/`--group-join` paths do, against the built
`coop-world` on loopback with the command line made for public rules: the host's key given as the owner's; the host's
and a joiner's game given tickets, tokens and the relay ids of their keys, and signed in with them as two players;
`/k/NAME`, a key's name, a Steam ticket and a made-up ticket refused; unsigned calls, a call sent twice, changed after
signing, or signed by another key refused, his own taken; the owner's calls with the host's key only, the rules
changed on the running server; the joiner's friend list as the Who I see window changes it (the host added by the
short form of his key, with `friends`), read back, another key's own list empty and his refused to another key; and the
relay answering the joiner's token for his id only, never the group's key.
`GROUP_TICKET`, `GROUP_RELAY_TOKEN`, `STRAND_SIGN` and `KEYS_KEEP` must also fail it.

The strand options in group play: `core_test.cpp` checks that the host's and a joiner's config have `build_received`
on, and off when the player turned the setting off; and `offer_strand` on with the notice token (the host's game and a
joiner's), never without one. `test_identity.py` checks both in the host's and the joiner's config made with the
tokens of the built `coop-world`. Mutants: `GROUP_BUILD_RECEIVED` (the config leaves it out), `GROUP_BUILD_SETTING`
(on whatever the setting says), `GROUP_OFFER_STRAND` (the config leaves it out); `GROUP_BUILD_RECEIVED` and
`GROUP_OFFER_STRAND` must also fail `test_identity.py`.

Server owner (`owner.h`): `core_test.cpp` checks who sees the window (the host with the key he gave the server; not
another key, a joiner, a two-player session or a host without an owner key), and the owner's calls against a stand-in
server that checks every signature as the server does, with its clock 1000 s ahead and no player keys (Friends): every
call signed by the key on the server's clock; the players' and the queue's rows; rejecting all of one player (his that
wait for the owner, not what waits for his answer, nobody else's); member, removal, accept one, accept all of a player,
the rules (a preset and two settings); five actions that are none (nothing sent); the report; a refusal in the
server's words and nothing refused reported as done; no key; an older server; seven lists that are refused; the
removal question under `stay` and `go` (what each means, nothing stays, not a ban); a rule's row. Mutants:
`OWNER_UNSIGNED` (owner calls unsigned where players' are), `OWNER_CLOCK` (signed on the launcher's clock),
`OWNER_REJECT_ALL` (another player's rejected too), `OWNER_REFUSAL` (a refusal taken for done), `OWNER_REMOVE_TEXT` (the
question without what withdrawal means and that it is no ban), `OWNER_HOST_ONLY` (the window for anyone),
`OWNER_PARSE`. `test_owner.py` (part of `make -C launcher test`) runs the same calls as the window and the flags
(`owner::run`) against the built `coop-world` on loopback, started with the command line made for public rules and the
host's key as the owner's: two joiners sign in with their tickets and build (held: members only); the list with keys
short, counts and what waits with builder, kind and area; a joiner's key refused as the owner; a member made, all of
his accepted, all of the other's rejected and not his, one object accepted, a like counted; a rule changed on the
running server with the server's words and the preset's value, a value that is none refused; all of a player's
objects removed with his account kept; an owner call sent twice (the same nonce) refused, and one signed by a joiner.
`OWNER_UNSIGNED`, `OWNER_REJECT_ALL` and `OWNER_REFUSAL` must also fail it.

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

The first creates two loopback launchers, checks wrong-secret refusal, the save
handoff (offer, fetch on Ready, Ready only after it is placed), Ready/Start and matching configs, and for group play
that a joiner takes the invite route whose server answers (a TCP listener stands in for the server; this part has
not been run since it was added). The second is the kit's Windows adapter/socket suite. The
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
`--dev-take-save FILE` (with `--dev-direct`) installs FILE the way the lobby installs the host's save.
Add `--prepare-only` to write/validate configuration and exit without a game.
Custom `--data-dir` is accepted only with `--dev-direct`.

Look for proxy `READY` and `MOD_STARTED` (network only), then mod `POSE available`
after continuing a co-op save, and `handshake:true` / `connected:true` in the
partner/mod state. Inspect `<instance>/redirect-data/mod/mod.json`: role host,
slots 0/1, peer 127.0.0.1, session 123, game provider, game_thread, puppet_probe,
partner own, partner_gear, partner_marker, partner_vehicle, carry_visual, partner_tag, notices, share_environment rain, required. Repeat
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

Group play (above) is outside the standard's two-player lobby flow: it has no lobby, roster, ready state or
discovery row, and its window parts (the Settings box and list of rules, the invite on the Host page, Join group, the
join screen with the server's rules, My strand on this server, Server owner) were built without being seen on a screen. This is a launcher foundation, not full A–K compliance. Steam friend/lobby SDK
integration, UPnP/NAT-PMP, a firewall fix (the status is checked), chat/voice, controller and
Deck navigation, update UI, save metadata/slot management, recent-host
rename/pin/forget, automatic lobby return after game exit, and in-game discovery
replies remain unimplemented. The launcher releases its lobby port before the mod
binds it, so hosts are discoverable in the pre-game lobby only. Internet play uses
LAN/VPN routes; there is no NAT traversal. Named hosts use Windows IPv4 DNS.
