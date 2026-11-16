# coop-world: the players' own shared-world server

The game's online layer (other players' structures, signs, likes, lost cargo, roads) talks to a server. `coop-world` is
a small server of our own for that layer, so a co-op group shares one world of its own. It never talks to the
publisher's service and holds none of its keys; modded copies stay refused there (network policy `enforce`).

Status (2026-11-11): the game signs in and stays signed in, two copies are two players, and built objects are stored
and handed to the other players of an area. Live: cairns one player built on the server were activated in another
player's game and stayed, and a ladder the game built with its upload flag was uploaded, kept and found by the
other player on the server. Between two copies of the game, a bridge one built stood as a net bridge record in the
other (see "Not done" for what is missing).

## Run

```
coop-world --port 8089 [--bind 127.0.0.1] [--data world.json] [--prefix SECRET] [--log events.log] [--trace calls.log] [--idle-exit SECONDS]
           [--owner KEY] [--name TEXT]
```

(The relay's options are under "The relay", the ones for other servers under "Linked servers".)

- `--bind`: the address to listen on (default loopback). For a partner on another PC, the address he reaches you at.
- `--data`: the world file, written after every change (temporary file, then rename). Without it nothing is kept.
- `--prefix`: a secret first path element. Requests without it get 404 and change nothing. Use one whenever the
  server listens beyond loopback: the Steam ticket is not checked, so the prefix is what makes someone a player.
- `--idle-exit SECONDS`: leave when no game has asked anything for that long (the launcher starts it with six hours).
- `--trace`: every call with its body, for research. Bodies name other accounts (the game looks up the Steam
  friends list); keep the file private. Neither the Steam ticket nor a launcher's sign-in ticket (`/t/`) is written.
- `--owner KEY`: the owner's public key (64 hex digits; the host's launcher gives its own). Saved with the world; a
  start with another key replaces it (whoever starts the server with the world file is its owner). The owner's signed
  calls are the owner's actions on the running server ("Identity").
- `--name TEXT`: this world's display name, the home its builders are shown with on linked servers ("Builder names"):
  1 to 12 characters, at most 16 bytes of UTF-8, no control characters, no space at either end. Saved with the world;
  `coop-world policy --data world.json --name TEXT` gives it to a stopped server.

The launcher does all of this for a session when "Shared world" is on in its Settings (`launcher/README.md`). For a
group of more than two it starts the server with the relay as well ("Group server" in Settings): one port number for
TCP and UDP (49100 + slot), one key for the path and the relay, `--bind 0.0.0.0`; every game then gets `server`,
`relay`, `relay_key`, `player_id` and `online_server` (`mod/README.md`, "Group play"). Not run live. By
hand, each copy of the game gets the address in its `mod.json` (`mod/README.md`, "A private online server"):

```
"online_server": "http://127.0.0.1:8089/SECRET/auth/ds"             one Steam account per player
"online_server": "http://127.0.0.1:8089/SECRET/k/NAME/auth/ds"      the account is NAME (two copies, one Steam account)
"online_server": "http://127.0.0.1:8089/SECRET/t/TICKET/auth/ds"    identity keys: the account of the key the ticket was taken for
```

## What it speaks

The message shapes are the game's own, read from the executable's reflection data (`mod/game_offsets.md`, "Online
protocol"). With `online_server` the mod makes the bodies plain JSON, so nothing is encrypted on the wire: use it on
loopback, a LAN or a VPN.

| Call | Answer |
|---|---|
| `GET …/auth/ds?provider=steam&display_name=&code=TICKET` | the player (stable id per account) and a session: token and gateway address; under identity keys only at `…/t/TICKET/auth/ds` with a launcher's ticket ("Identity") |
| `…/gate/e/getVersion` | major 0, minor 12 (the game refuses anything else) |
| `createObject`, `updateObject`, `deleteObject` | stored; the new id is the type letter and 12 characters (the game reads the kind from that letter); only the builder changes or removes an object; an upload the server policy does not let be shared is answered the same way and held for its builder ("Server policy"); over 4000 objects of one player, 403 |
| `findQpidObjects` | the other players' objects in that area that every player is handed ("Server policy"), at most the asked number per type, never the asker's own, chosen as "Which objects a game is handed" says |
| `getQpidObjects`, `reverseLookup` | from the stored objects and players |
| `lookup` | a builder's name: this server's players by name only, a builder from a linked server as "Name · Home" ("Builder names") |
| `sendLike`, `getLikeHistory` | a like is added to the object and to its builder's history (the last 2000 likes are kept); a player reads only his own history, after the time he names |
| `getRankingSchedules`, `getWastedBaggages`, `findMissions`, `getOrderedMissions`, … | empty lists |
| anything else | `{}` and an `unhandled NAME` line in the log |
| `POST …/link/pull` with `Authorization: Link KEY` | not the game's: one page of objects for a linked server ("Linked servers") |
| `GET`/`POST …/coop/NAME` | not the game's: the launcher's API ("The launcher's API"), and the owner's calls ("Identity") |

Limits: 16 players, 4000 objects built here per player, 20000 objects held from each other server, 4 MiB per body,
32 connections.

## The relay (`--relay-port`)

The real-time layer (players seeing and touching each other) is the presence core in `core/`. For more than two
players its datagrams go through this server instead of over one direct link: every game says where its player is,
the server works out who is near whom, tells both, and passes the core's datagrams on between players who are near
each other and nobody else. It is off unless `--relay-port` is given. The game mod uses it for group play (`relay`, `relay_mode` in
`../mod/README.md`), and so does the stand-in swarm below.

```
coop-world --port 8089 --relay-port 8090 [--relay-key TEXT] [--relay-enter 150] [--relay-leave 200]
           [--relay-timeout 10] [--relay-players 64] [--relay-rate 200]
```

- `--relay-port`: the UDP port, on the same `--bind` address as the rest (loopback by default). With `--bind 0.0.0.0`
  both listen on every address of the PC, which the launcher's group server does, always with a key.
- `--relay-key`: the group's secret (at most 32 letters, digits, `-` and `_`); every HELLO must carry it. Default: the
  `--prefix`. Beyond loopback the server does not start a relay without one. Under identity keys the group's key is
  not taken: a HELLO carries the player's relay token, in the same field ("Identity").
- `--relay-enter`, `--relay-leave`: two players become near at `enter` metres or closer and stay near until they are
  farther apart than `leave` (straight-line distance; same area; both with a position).
- `--relay-timeout`: seconds without a HELLO after which a player is gone.
- With `--idle-exit`, players on the relay count as activity: the server stays while anyone is there.
- `--relay-players`: the most players at once (at most 1024). `--relay-rate`: datagrams a second a player may send
  for each player near him (a second's worth can be saved up); more is dropped.

The envelope, byte for byte, and the player's side are in `core/relay.h` and `core/CORE.md` ("Relay envelope"). What
the server holds to (`relay.h`):

| Rule | Why |
|---|---|
| A player's address is learned only from a HELLO with the key (under identity keys: with a relay token of that player id) | someone who merely reaches the port is not a player and is never answered |
| While a player is known, a HELLO for his id from another address is refused (REFUSED, id in use) | a second sender cannot take over a player; a game that restarts on a new port waits out the timeout or says BYE first |
| DATA from an address that has not said HELLO is dropped | the source id in what is passed on is the server's own record, never a field the sender wrote |
| DATA goes only to a player who is near the sender | a player cannot reach, or load, someone across the map |
| No HELLO for the timeout: gone, LEAVE for everyone near; DATA does not keep a player | the roster follows the players who are really there |
| ENTER and LEAVE are numbered and repeated until acknowledged | a lost ENTER would leave two players near each other unseen |
| Caps: players, datagram size (1268 bytes), rate per player | one datagram in makes at most one DATA datagram out, and nothing to a sender without the key |

Not done: the key travels in the clear (use loopback, a LAN or a VPN, as for
the HTTP side); there is no cap on how many players may be near one player, so a crowd in one place means every pair
exchanges datagrams; under identity none the relay and the HTTP side do not know each other's players (the player id
in HELLO is whatever the game sends; under identity keys it is bound to the player's key, "Identity"); IPv4 only.

### Broadcast mode (`--broadcast-*`)

A game whose HELLO says envelope version 2 (`core/broadcast.h`, `core/CORE.md` "Broadcast envelope") is a broadcast
player: instead of a core session with every player near, it publishes one update a tick (its presence and its
animation sample), and the relay copies it to the players near, several to one datagram (FRAME), stamped on the
server's clock. The relay also keeps what each player only shows of himself (name, gear, carried cases and their
places), the latest version of each kind, and hands it out. The same port speaks both versions, and players of the two
are never near each other. The mod does not speak it yet.

```
coop-world --port 8089 --relay-port 8090 [--broadcast-full 30] [--broadcast-interval 200] [--broadcast-animation 50]
           [--broadcast-budget 1200] [--broadcast-flush 25] [--broadcast-rate 100] [--broadcast-build GAME:HEX]
```

- `--broadcast-full` (metres, 0..65535): every update of a player this near goes to the receiver.
- `--broadcast-interval` (ms, 0..5000): farther, but still near (`--relay-enter`, `--relay-leave`), at most one update
  in this many milliseconds; a teleport always goes. 0 sends every update to everyone near.
- `--broadcast-animation` (metres, 0..65535): animation samples only this near. The one-frame pulses of the samples
  the interval leaves out are merged into the next one sent, so a receiver between the two distances misses none.
- `--broadcast-budget` (bytes, 256..1200): the most bytes in one FRAME. An entry larger than the budget (a long
  animation sample) goes in a FRAME of its own, never past 1200 bytes.
- `--broadcast-flush` (ms, 0..200): the longest an update waits for others to share its FRAME. 0 sends every entry at
  once, alone.
- `--broadcast-rate` (10..100000): datagrams a second a broadcast player may send (a second's worth can be saved up);
  more is dropped. A player sends about 22: 20 PUBLISH and 2 HELLO.
- `--broadcast-build GAME:HEX`: the game build (decimal) and the 32 hex digits of the mod's 16-byte wire identity.
  Broadcast players of another build are refused (REFUSED, reason 3). Without it, players of different builds are
  only kept apart, and each WELCOME says how many players in reach that keeps away.

What the server holds to in broadcast mode, besides the rules above (`relay.h`):

| Rule | Why |
|---|---|
| The source of every entry and SHARE is the server's record of the sender's address; a PUBLISH, STATE or FETCH that names another player is dropped (counted as spoofed), and one from an address that never said HELLO is nothing | a player cannot speak for another |
| An update is taken in order: a later sequence and a later stamp than the last one taken | a repeat or a late datagram is not passed on |
| Its stamp is at most 2 s behind and 0.5 s ahead of the server's clock | the receivers interpolate on that clock; a stamp from elsewhere would be shown at the wrong moment |
| Within the full distance every update, beyond it one in the interval and a teleport always; animation only within the animation distance, the pulses of the samples left out carried by the next | the cost of a crowd falls with distance, and a teleport or a pulse is never lost to it |
| A receiver's entries go in FRAMEs of at most the budget, each within the flush time | the datagram fits the path, and packing adds at most the flush time of delay |
| State: the latest version of each kind, an older or repeated one refused; handed out on ENTER (each way, before anyone asks), to everyone near when it changes, and on FETCH from a player near (every kind, the empty ones too, at most one answer in 250 ms per pair) | a player who comes near shows the other's gear at once; a lost SHARE is repaired; FETCH reaches no one far away and cannot multiply traffic |
| Pairs players and broadcast players are never near each other, nor broadcast players of different game or mod builds | a receiver trusts the server for the build of the presence it decodes |
| A broadcast player whose HELLO says stream 0 is a game that started again: what it showed is dropped and the state of everyone near is handed to it again | a restarted game shows the others' gear without asking, and the others do not keep his old one |

Costs. A broadcast player sends the same however many players are near (and no PUBLISH while nobody is). For each PUBLISH the server does the interest check for each
player near and appends at most one entry to each; each receiver gets at most one FRAME per flush time and one more
whenever the budget fills. A state change costs one SHARE (at most 1144 bytes) to each player near, a player coming
near one SHARE for each kind held each way, an answered FETCH four. The server keeps at most four times 1100 bytes of
state per player, in memory only. Measured in the tests: 24 players walking in one place, each sending presence and
a small animation sample 20 times a second (`broadcast_test.cpp`), receive about 60 datagrams and 50 kB a second each
(in pairs mode the server would pass each of them 460 presence datagrams a second and up to as many animation
datagrams); over loopback (`test_broadcast.py`) a player in a group of 16 with 8 more beyond 30 m receives about 67
datagrams and 32 kB a second with a 600-byte budget. The relay stays one thread: it wakes for a datagram, for the next
FRAME due, and four times a second.

The mod speaks it with `"relay_mode": "broadcast"` (`../mod/README.md`, "The broadcast mode") and `coop-swarm` with
`--mode broadcast`; live once on 2026-11-16 with two real games and 12 stand-ins (`../STATUS.md`, "Many players").

Not done (broadcast): nothing caps how many players are near one player,
so in a crowd within the full distance every player still receives every other's 20 updates a second (in fewer,
larger datagrams); the shown state is not saved.

### A swarm of stand-in players (`coop-swarm`)

`mod/swarm.cpp` (built by `make -C mod build/coop-swarm`, or `build/coop-swarm.exe` with the Windows binaries) is K
players in one process, each with its own socket: they say HELLO, walk a circle, and run the real protocol core with
whoever the relay says is near, with the mod's wire identity like `coop-partner`. It is the tool for putting many
Sams around one game copy once the mod speaks to the relay, and it loads a server without any game today.

```
coop-swarm --relay 127.0.0.1:8090 --count 20 --centre X Y Z --radius 15 [--mode pairs|broadcast] [--key TEXT]
           [--session N] [--speed 1.6] [--area N] [--first-id 1001] [--only ID] [--role auto|host|client]
           [--seconds N] [--stop FILE] [--require-connected] [--replay FILE [--with-animation]]
```

`--mode broadcast` makes every stand-in a player of the relay's broadcast mode (the mod's `relay_mode`): one broadcast
client each (`core/broadcast_client.h`) instead of a core session per player near, with its name posted as the mod
posts it; `--session`, `--only` and `--role` are then refused. Its lines say `live` where pairs say `connected` (the
players near whose presence is being taken) and have no sessions; the last is
`SWARM done mode=broadcast online=8/8 near=56 live=56 accepted=4368`. In both modes every line also gives the last
second's traffic per stand-in on average, `in=24.6 in_bytes=11718 out=22.2 out_bytes=1986 heard=56` (datagrams and
bytes from and to the relay; heard: the stand-in and player pairs a presence sample arrived for), and a
`SWARM traffic` line before the last gives the same over the whole run, so the two modes can be compared under the
same crowd (those numbers: 8 stand-ins on a 10 m circle in broadcast; in pairs they received and sent about 160
datagrams, 23 kB, a second each).

`--centre` is in the core's axes (metres; x, y up, z). `--session` is the core session number every pair shares
(default 1); of two players the lower id is the host unless `--role` says otherwise. `--only ID` makes the stand-ins
talk to that one player only (a game copy), which keeps a large crowd cheap: without it K players in one place run
K*(K-1) sessions. It prints one line a second, for example
`SWARM t=3 online=8/8 near=56 sessions=56 connected=56 accepted=3008`, and with `--require-connected` exits 1 unless
every player was answered and every session was connected at the end. All options: the comment at the top of
`mod/swarm.cpp`.

`--replay FILE` takes the presence log of a real game (`presence-*.jsonl`, the file `coop-partner --replay` reads).
The players keep to their circle and walk it at the recorded player's speed of the moment: they stand when he stood
and sprint when he sprinted (`--speed` is not used). Player i starts i/K of the way into the recording, and it loops.
With `--with-animation` each also sends the animation samples recorded in the file (mod lab option
`record_animation`) at their moment of the recording, to every connected session, the controls only, as the mod sends
other players; without it the stand-ins send presence only and a game shows them gliding in the walking pose it picks
from their speed. The stand-ins read the animation frames they are sent (a real game's, or each other's): once there
are any, the lines end `animation_out=N animation_in=N animation_in_bytes=N` (samples sent, frames a session's core
took, and their sizes together), and with `--replay` the last line says how far the first player walked
(`walked=8.0`).

## Linked servers

Two groups, each with a server of its own, can hand each other what their players built: once, through a file, or
continuously while both servers run. It is off unless asked for, and each direction has a filter. Built and tested
without a game (2026-11-16): no game has been handed an imported object yet.

### What travels, and the filters

What a server stores is what can travel: the built objects (every kind the game uploads: the kind is the one letter
in `t`), each with its builder's player id and name, its area (`q`), its last change and its like count. Players,
sessions, account keys and the like history (who liked what) never travel. Roads, missions and cargo are not stored
by the server yet ("Not done"), so they cannot travel either. Only what every player here is handed travels: an
object kept for its builder or waiting for an answer does not ("Server policy"); a remnant travels with the name of
the player who built it.

A filter is `all`, `none`, or terms separated by `;`. A term that is left out does not narrow.

| Term | Passes |
|---|---|
| `types=l,rope,e` | these kinds: the letter, or a name for the letters that are known: `post` p, `charger` c, `safehouse` s, `watchtower` w, `bridge` b, `shelter` n, `rope` r, `ladder` l, `bike` v, `cairn` e, `road` o, `mushroom` x, `chiralbridge` g, `jumpstand` j |
| `owners=Ann,zygo_…` | objects of these builders, by display name or by player id (a name with `,` or `;` in it: use the id) |
| `areas=226,227` | objects in these areas (the game's area number `q`) |
| `max-age=30d` | objects changed within this time (`s`, `m`, `h`, `d`), by the taking side's clock against the builder's server's time |
| `likes=no` | the objects without their like counts (default `yes`) |
| `imported=yes` | also objects that are themselves imports (default `no`, see "No loops") |

### Where an object is from

Every world has a **server id** (16 random characters, made once, kept in the world file as `server_id`). An object
that travels carries the id of the server it was **first built on** and its id there. That pair is its identity
everywhere:

- An imported object is stored with `origin`: `server` and `id` (that pair), `via` (the server it was received
  from; the same as `server` unless it travelled onward), `name` (its builder's name over there) and `likes` (the
  count it arrived with). An object without `origin` was built here.
- **Its id here is a new one**, in the shape the game requires of every id (the kind's letter and 12 letters and
  digits; the game reads the kind from the first letter). It is computed from the origin pair, so the same object
  gets the same id on every import and on every server that takes it, also after it was forgotten and taken
  again; if that id is taken, by anything, the next one in the sequence is used. The original id is not reused
  because two servers do make the same ids: an id is computed from the builder's player id and a counter.
- Taking the same object again changes nothing; taking a newer state replaces the data, time and builder name and
  keeps the id. Likes given here stay: the count is what the origin reports plus what was given here (likes given
  here do not travel back to the origin).
- **The builder's player id is kept as it is.** A player id is computed from the account, so the same Steam account
  is the same porter on every server, and, as on one server, a player is never handed objects he built himself (his
  game has them in its save). Accounts named in the address (`/k/NAME`) are only told apart by that name: two
  different people called `/k/ann` on two linked servers count as one porter and do not see each other's objects.
- Nobody on the taking server can change or remove an imported object through the game (`updateObject` and
  `deleteObject` answer 403, also for the same account): it changes when its own server changes it.
- The builder's name is given to the game's `lookup` call with his server's display name, "Name · Home" ("Builder
  names"); his account key is not known here.

### Builder names

A game shows a builder's name from the server's `lookup` answer (`displayName`). This server's own players get their
name only. The builder of an object from a linked server gets **"Name · Home"**: his name as his server gave it, a
middle dot, and the display name of the server the object was first built on (also when it came by way of another).
Home is, in this order:

1. the name that server gives itself (`--name`), carried by every object it exports (`home`) and passed on unchanged
   by a server that gives the object onward;
2. else the name of the live link to that server here (`--link NAME`): a server without a name of its own is called
   what the group that takes from it calls it;
3. else the first eight characters of that server's id.

The whole label is at most 24 characters and 32 bytes of UTF-8, so the game's name label does not cut it. When it is
longer, the name is cut (whole characters, never half of one) and the server's name is not; a home longer than 12
characters is cut to that first. The owner's list (`players`, `owner/players`) names a builder known only from linked
servers the same way.

The travelling form: an object exported by a server with a display name has an eighth field, `home`; one without a
name exports the seven fields of before, and a document from before (no `home`) is imported as it was, its builders
named by rules 2 and 3. A server that renames itself changes the name with the next import, and nothing else. A
`coop-world` from before refuses an object with `home` (an unknown field), so a server with a name needs this
version on the taking side. The world file keeps the name (`name`) and every import's `home` (in `origin`); `origins`
prints the home of each server.

Not done: no game has shown a builder from a linked server yet, so whether the game's label shows the middle dot and
a 32-byte name in full is unknown (the limits come from the label's size, not from a live check).

### Through a file

Run these while no server has the world file open (a running server writes the whole file after every change and
would overwrite what a command did).

```
coop-world export  --data world.json --out objects.json [--filter FILTER]     default: all
coop-world import  --data world.json --in objects.json [--filter FILTER] [--prune]
coop-world origins --data world.json
coop-world forget  --data world.json --server ID
```

- `export` writes the objects built on this server that pass the filter (with `imported=yes` also its imports,
  each under its first server). A world file from before linked servers gets its server id on the first command.
- `import` adds what passes the filter and updates what it already has. The same file twice changes nothing.
  With `--prune` the file is taken as everything that server shares now: objects held from it that are not in the
  file, or no longer pass the filter, are removed. The file is checked whole first; a file that is refused (not an
  export, an object in the wrong form or over 64 KiB, an object listed twice, more than 20000 objects from one
  server, the server's own export) changes nothing.
- `origins` prints this server's id and how many objects it holds from which server.
- `forget` removes everything that was first built on, or received from, that server, and nothing else.

### A live link

```
coop-world --port 8089 ... --link NAME --link-key KEY [--link-url http://ADDRESS:PORT[/PREFIX]]
                           [--link-export FILTER] [--link-import FILTER] [--link-every SECONDS]
```

`--link NAME` starts the description of one other server (up to 16; the name is for the log) and the options after
it belong to it:

- `--link-key`: required, 8 to 64 letters, digits, `-` and `_`, the same on both servers and different for every
  link. A server does not start with a link that has no key.
- `--link-export`: what that server may take from here. Default `none`.
- `--link-import`: what is taken from it. Default `none`; needs `--link-url`.
- `--link-url`: where that server listens, with its `--prefix` if it has one; a numeric IPv4 address.
- `--link-every`: seconds between two rounds (default 5).

Objects only move because the taking side asks. A server with `--link-url` and `--link-import` asks the other one
every few seconds (`POST /link/pull`, the key in the `Authorization` header, on a connection of its own); the other
one finds the link by the key and answers with the objects that pass its `--link-export` for that link, at most 500
objects or about 1 MiB a page. The asking side keeps what passes its `--link-import` and makes what it holds from
that server equal to it: new objects appear, changed ones change, and objects that were removed there, or no longer
pass either filter, are removed here. Each direction is configured on its own; a server that only gives needs no
address of the other.

Two servers, on a LAN or VPN, each run by one group:

```
coop-world --port 8089 --bind 10.0.0.2 --data north.json --prefix NORTHSECRET \
    --link south --link-key one-key-for-this-pair --link-url http://10.0.0.3:8089/SOUTHSECRET \
    --link-export "types=ladder,rope,bridge;max-age=30d" --link-import all

coop-world --port 8089 --bind 10.0.0.3 --data south.json --prefix SOUTHSECRET \
    --link north --link-key one-key-for-this-pair --link-url http://10.0.0.2:8089/NORTHSECRET \
    --link-export "owners=Bea" --link-import "areas=226,227"
```

The southern players then find the northern ladders, ropes and bridges of the last 30 days that stand in areas 226
and 227; the northern players find everything Bea built in the south. Nothing else crosses.

What it holds to:

| Rule | Why |
|---|---|
| No link without a key; `/link/pull` does not exist (404) on a server without links, for a key that is no link's, and for a game's session token | being able to reach the port, or being a player, does not make someone a linked server |
| Both filters default to `none` | a link that is only named moves nothing |
| An answer is taken whole or not at all: every page well formed, from the same server and the same state of its world | a server that changes between two pages, or sends garbage, leaves the other as it was; the next round starts again |
| A request or an object in the wrong form is refused (unknown fields too); requests are at most 4 KiB, objects 64 KiB, pages 500 objects, 20000 objects from one server | bounded work and memory for whatever arrives |
| After a restart of either side the first round takes everything again; otherwise a round costs one small request while nothing changed, and every ten minutes everything is compared again | catch-up needs no saved cursor, and an object that grew too old for a filter goes without anything having happened to it |
| A linked server asking does not count as activity for `--idle-exit` | two servers started for a session would keep each other alive |

### No loops

- An object is never imported by the server it was first built on, by any route.
- A server never gives an object back to the server it was built on or received from.
- Imported objects are not given on at all unless the giving side's filter says `imported=yes`, and not taken
  unless the taking side's filter says `imported=yes` too. With both, an object from a third server arrives under
  its first server's identity, so the same object by two routes is still one object (the copy that came first
  stays, a newer state replaces it). An object passed on this way goes when the server it was received from no
  longer gives it; if it still arrives by another route it is back in the next round.

### What is not protected

The link key, like the prefix and the players' session tokens, travels in the clear over plain HTTP, and so do the
objects: use loopback, a LAN or a VPN. There is no TLS and no signature: a linked server is trusted to say what was
built on it and by whom (it can invent objects and builders, up to the caps; it cannot touch objects built here or
taken from another server). Player ids travel with the objects; a player id is a plain hash of the account key, so
someone who has an id can test guesses of the Steam id behind it. Whoever has the key can read everything the
export filter passes. The server connects out only to the addresses given with `--link-url`.

### Co-op across linked servers (designed, not built)

The real-time layer across two servers ("players on linked servers who stand near each other see each other") is
not built. The relay today knows only the players who say HELLO to it. The design:

- **Player ids.** A player's relay id (`player_id`, 49 random bits made once by the launcher) is the same number on
  every server and is not remapped: both games compute a pair's core session from the two ids
  (`ds::relay::pairSession`), so both must know each other under the same number. Two different players with one
  id is as unlikely across servers as on one; if it happens, each server keeps its own player and does not
  announce the other's, and a HELLO for an id a remote player holds is refused as "id in use".
- **One session number.** `pairSession` also takes the group's `session` number from each game's config. Version 1
  of the envelope has no place to tell a game another number for one player, so two linked groups must run with
  the same `session` (and the same build of the mod: the core refuses another wire identity). Lifting that needs a
  per-player session in ROSTER, which is a new envelope version.
- **Positions.** The two relays talk UDP to each other on their relay ports, the link key in a server HELLO
  (address learned only from it, as for players). Twice a second each sends the other its local players that have
  a position: id, area, position, nickname (about 60 bytes a player, 20 to a datagram). A server keeps the other's
  players as remote entries that time out like players.
- **Who is near.** Each local-remote pair is judged by one server only, the one with the lower server id, with the
  same enter and leave distances as for local pairs; it tells the other server (a numbered, repeated event stream
  like ROSTER), and both send their own player ENTER or LEAVE. Two judges would disagree at the edge. Two remote
  players are never a pair here.
- **DATA.** A player's DATA for a remote player is passed to the other server in a server envelope that carries
  both ids (8 bytes more than RELAYED, 1276 bytes at most), and delivered there as RELAYED only when that server
  also holds the pair as near and the source id belongs to that link. The source id is the sending server's own
  record, as today.
- **Cost.** One more hop for every datagram of a cross-server pair, and all of that traffic on the one path between
  the servers: per pair the same rate as today (the relay's cap is 200 datagrams a second per pair and direction),
  so a link needs a cap of its own. Positions cost 2 x players x 60 bytes a second each way, whether or not anybody
  is near. A crowd split over two servers still runs one core session per pair.
- **Not solved by this:** more than two servers in one place (a pair of remote players of two different servers
  would need those two servers linked as well), and NAT (both relays must reach each other).

It was left out because it changes the relay's state machine (remote players, a second event stream, a second
envelope) rather than sitting behind it, and nothing in a game uses the object side yet.

### Not done (linked servers)

- No game has seen an imported object. Unknown until one has: whether the game accepts the remapped ids in every
  list (roads have 23-character ids in a save, the server makes 13 for every kind), which ones a game takes when
  there are more than it asks for (imports take part in the server's choice like objects built here, "Which objects
  a game is handed"), and what a game does when an object it activated is removed on the server.
- A linked server can only be asked (pull). A server behind NAT that cannot be reached can take but not give.
- No exclusion terms (`everything but ...`), no filter by position inside an area, no per-builder caps.
- A removal is seen as "no longer listed", so `import` without `--prune` never removes, and an object that leaves
  and re-enters a filter keeps its id.
- The world file is written whole after every change; with tens of thousands of imported objects that is megabytes
  per change.

## Server policy

Which of the uploads the other players are handed, and what becomes of a player's objects when he takes them back.
The rule behind it: a player's save is his own. A server cannot stop anyone building in his own world; it only
decides what it shares. Built and tested without a game (2026-11-16), except one live check: a real game (an Episode 3
save) on a server with `build=nobody`, when uploads were still refused, built a ladder, was answered 403 for its
upload, did not ask again within 15 seconds, stayed up and had no fault. Whether it then took the ladder out of the
player's own world is unknown, and a malformed answer is already known to make the game delete its upload, so an
upload is no longer refused for the policy: it is answered as any other and held (below).

For a player an object on the server is one of three: his own; another player's of this server; another player's
from a linked server (an import: nobody here changes it, this server can only drop it). Every object records its
builder: his player id and the name he had when he built it (`name` in the world file; imports keep their first
server, their id there and their builder's name, "Where an object is from").

### Presets and settings

```
coop-world --port 8089 --data world.json [--policy friends|solo|public] [--policy-set KEY=VALUE]...
coop-world policy --data world.json [--preset friends|solo|public] [--set KEY=VALUE]...
```

- `--policy` is the preset of a new world (default `friends`). A world that exists keeps its own preset; the policy
  line the server prints at the start says so when `--policy` names another.
- `--policy-set` changes one setting, on every start, and the change is saved with the world.
- `policy` prints the policy, every setting with the preset's value where it differs. With `--preset` (the whole
  preset again) or `--set` it changes it; with `--preset` it also makes a world file that does not exist yet.
- A world file from before the policy is a friends server, and every object in it is shared.

| Setting | Values | `friends` | `solo` | `public` |
|---|---|---|---|---|
| `join` | `invite`, `open` | invite | invite | open |
| `build` | `everyone`, `members`, `nobody` | everyone | everyone | members |
| `accept_offers` | `auto`, `review`, `never` | auto | auto | review |
| `limit` | objects per player, 0 for none (up to 4000) | 0 | 0 | 200 |
| `withdrawal` | `stay`, `go` | go | go | stay |
| `players` | 1 to 16 | 16 | 1 | 16 |
| `offer_builds` | `yes`, `no`, `ask` | yes | yes | yes |
| `offer_old` | `yes`, `no` | no | no | no |
| `order` | `newest`, `liked` | newest | newest | liked |
| `per_builder` | objects of one builder per kind in one answer, 0 for none (up to 1000) | 0 | 0 | 3 |
| `spread` | metres, 0 for off (up to 1000) | 8 | 8 | 8 |
| `identity` | `none`, `keys` | none | none | keys |

- `join`: recorded and shown, nothing more yet (there are no invitations).
- `build`: whose uploads are shared. `members` are the players the owner marked (`player-set ... member=yes`); anyone
  else's upload, and with `nobody` everyone's, is **held**: answered as any other, stored for its builder and
  handed to nobody else (the log names the reason).
- `accept_offers`: what becomes of an upload its builder shares: `auto` every player is handed it, `review` it waits
  for the owner, `never` it is kept for its builder.
- `limit`: a player's uploads beyond it are held. It counts what he shares and what waits; what is kept or held for
  him, and remnants, do not count. The hard cap of 4000 objects of every kind a player stores here stays a refusal
  (403): it protects the server's memory and disk.
- `withdrawal`: what a player's withdrawal does to what he shared (below).
- `players`: accounts beyond it are refused at sign-in; the ones the world has still sign in. Solo is a friends
  server for one player: a personal archive.
- `offer_builds`, `offer_old`: what a player who has chosen nothing has chosen (below).
- `order`, `per_builder`, `spread`: which of an area's objects a game is handed when there are more than it asks for
  ("Which objects a game is handed").
- `identity`: `none`, whoever has the group's key can sign in and act as any player (friends); `keys`, players are known
  by the key their launcher made, and nobody signs in, builds or withdraws in another's name ("Identity"). A world
  file from before this setting has `none`, whatever its preset.

### The players' choices

Each player has two choices on each server, kept with the world:

```
coop-world player-set --data world.json --player ID [offer_builds=yes|no|ask|default] [offer_old=yes|no|default] [member=yes|no]
coop-world offer      --data world.json --player ID (--object ID | --all) [--no]
```

- `offer_builds`: is what he builds while connected shared with the others. `yes`: shared (then as
  `accept_offers` says). `no`: stored for him and handed to nobody else, like a personal archive. `ask`: it waits
  for his answer; `offer` is his answer (yes, or `--no`: kept for him).
- `offer_old`: whether the structures of his old strand may be taken over to this server, a standing yes or no
  (default no). Recorded only: the server cannot yet tell an old structure from a new one.
- `default` goes back to the server's value. A change applies to uploads from then on.

What is kept or held for its builder, or waits, is handed to him only (the game asks for his own objects by id; it
never lists them): not to anyone else, by list or by id, no likes from others, and never to a linked server.

A **held** object is not his choice but the policy's (`build` or `limit`; `review` and `players` list how many and
why). It is shared only once the policy allows it: when he is made a member, the limit is raised, or a withdrawal
makes room. Then his `offer` shares it (as `accept_offers` says), or the owner's `review --accept` does (unless he
chose `ask` and has not answered). While the reason holds, both are refused. `offer --no` makes it his own kept one.

### Review

```
coop-world review --data world.json [--accept ID | --reject ID | --accept-player ID]
```

Without an option it lists what waits, and for whom. `--accept` hands an object that waits for the owner to every
player; `--reject` keeps it for its builder (it is not removed); `--accept-player` accepts everything of one player
that waits for the owner, and every held one the policy now allows. An object that waits for its builder's answer
is not the owner's to answer. Under `ask` and `review` both, the builder's yes puts it before the owner.

### Withdrawal

```
coop-world withdraw --data world.json --player ID (--object ID | --all)
```

A player takes back what he built here (only his own, and never an import):

- `withdrawal=stay` (the public preset): what was shared stays as a **remnant**. It keeps the name its builder had
  when he built it, every other player is still handed it, and it belongs to no account: nobody changes or removes
  it through the game (403, its builder too), likes for it are counted on it and reach nobody's history, and it is
  given to linked servers with its builder's name. It is not handed back to its builder (his game has it in its
  save) and does not count for his limit.
- `withdrawal=go` (friends and solo): it is removed for everyone.
- Either way, what was never shared (kept or held for him, or waiting) is removed.

### The owner removes a player's objects

```
coop-world players       --data world.json
coop-world remove-player --data world.json --player ID
```

`players` lists every player (id, name, how many of his objects are shared, waiting, kept, remnants, from linked
servers; member; his choices, `(server)` where he chose nothing), then every builder known here only from imports
("Name · Home", "Builder names").
`remove-player` removes everything one builder has here: built here in any state, remnants, and what came from him
through linked servers (only his: the other builders of the same server stay), and the likes history for those
objects. The account stays: he can sign in and build again. An imported builder's objects come back with the next
import from a server that still gives them, unless its filter leaves them out.

Like `export` and `import`, the policy commands work on a world file while no server has it open. While the server
runs, a player's own choices, offer and withdrawal go through the launcher's API (below).

What it holds to:

| Rule | Why |
|---|---|
| What is kept, held or waits is handed to its builder only: not in a list, not by id, no likes, never to a linked server | a player's no, the policy and the owner's review are not undone by asking another way |
| An upload the policy does not let be shared (`build`, `limit`) is answered as any other and held, never refused; only the hard cap of 4000 objects refuses (403) | a player's save is his own: the server decides what it shares, not what he builds, and a game that is refused may take its structure away |
| A held object is shared only when the policy allows it at that moment; `offer` and `review` check `build` and `limit` again | neither the builder nor the owner gets round the policy by answering |
| A new account beyond `players` is refused at sign-in (403) | a solo archive stays one player's |
| An object that waits for its builder is answered by `offer` for him, one that waits for the owner by `review`; `offer` and `withdraw` take only that player's own objects built here | nobody answers for someone else, and an import changes only on its own server |
| A remnant belongs to no account and its likes reach no one | a withdrawal is final for the builder too |
| `remove-player` removes that builder's objects only | the other builders of the same linked server are not touched |
| The world file keeps the policy, the choices and every object's state; it is format 2, which a `coop-world` from before refuses | an older server would hand out what is kept and let remnants be changed |
| A `coop-world` from before `order`, `per_builder` and `spread` refuses a world file that has them (unknown setting) | it would not choose as the owner set |

### The launcher's API

Beside the game's gateway, under the same secret prefix, for the launcher and never the game (`api.h`). The launcher
shows the policy before a player's game connects, sends his choices, and lists, offers and withdraws his objects
(`launcher/README.md`, "A group on one server"). Built and tested without a game and without a screen (2026-11-16).

| Call | Answer |
|---|---|
| `GET …/coop/policy` | `format` 1, the world's `server` id, its display `name` (`""` for none), `preset`, `players` (`count`, `limit`) and `settings`: every setting as `key`, `value`, the preset's value (`preset`) and one line in words (`text`) |
| `GET …/coop/me?player=KEY` | that player's id, `known` (has he signed in or chosen here), `name`, `member`, `choices` (his own, `""` for none), what `applies`, and counts of his objects built here: `shared`, `waiting_you`, `waiting_owner`, `held` by reason (`build`, `member`, `limit`), `kept`, `remnants`, and `likes` (on what is still his: a remnant's are nobody's) |
| `GET …/coop/mine?player=KEY` | his objects built here: `id`, `type` and `kind` (ladder, cairn, …), `area`, `state` (`shared`, `waiting`, `held`, `kept`, `remnant`), `waiting_for` (`you` or `owner`), `held` and `held_text` (why), `likes`, `updated`. Objects that came from linked servers are not his here, whatever builder id the other server names, and are not listed |
| `POST …/coop/choices` `{"player":KEY,"offer_builds":"yes\|no\|ask\|default","offer_old":"yes\|no\|default"}` | sets his choices (one or both) and answers as `me`. A key the world does not have yet becomes a player, as a sign-in would (within `players`), so the choices are in place before his game first signs in |
| `POST …/coop/offer` `{"player":KEY,"object":ID}` or `{"player":KEY,"all":true}`, `"answer":"no"` to keep it his | as `coop-world offer` (`--no`): `offered` (or `kept`) and `me` |
| `POST …/coop/withdraw` `{"player":KEY,"object":ID}` or `{"player":KEY,"all":true}` | as `coop-world withdraw`: `remnants`, `removed` and `me` |
| `POST …/coop/ticket` `{"player":KEY}`, signed | identity keys: a sign-in ticket for his game, `ticket` and `expires` ("Identity") |
| `POST …/coop/relay-token` `{"player":KEY}`, signed | identity keys: a relay token, `token`, `relay_id` (made from his key) and `expires` |

The policy also carries `time` (the server's clock, so a launcher whose clock is off signs with the server's time) and
`owner` (whether the world has an owner key). `KEY` is the account key the player's game signs in with: the `NAME` of
a `/k/NAME` sign-in address (two games on one PC), else the 64-bit Steam id from the ticket, and under identity keys
`ed25519-` and his public key in hex. Answers: 200; 400 for a request in the wrong form (an unknown field, a
value that is no choice, a key that is none, a body that names neither one object nor all, or both); 403 with the
reason when the server refuses it (another player's object, an import, a remnant, an object the policy still holds,
a full world); 404 for any other path or method.

**Under identity none there is no identity.** Whoever has the group's prefix can call this API for any player: read his
choices and his objects, change his choices, offer what waits for his answer, withdraw what he built. That is exactly
what he can do already by signing in to the gateway under another player's key (`/k/NAME`, or a Steam ticket the
server does not check). Use it on friends servers only. Under identity keys every call but the policy is signed, and
a signed call acts for its signer only ("Identity").

What it holds to:

| Rule | Why |
|---|---|
| Only under the prefix, never under `/k/NAME`, and only the six calls | the game's paths and the API do not mix, and nothing answers without the group's key |
| A change is a `POST`; a `GET` (or any other method) of a change is 404 and changes nothing | a link that is opened or fetched ahead never offers or withdraws |
| A body is one JSON object with only that call's fields, all checked before anything changes | a request in the wrong form changes nothing |
| `offer` and `withdraw` name one object or `all: true`, never neither | a body that forgot the object does not take back everything |
| `member` and the policy are not in its player calls | they are the owner's: `player-set` and `policy` on the stopped server, or the owner's signed calls ("Identity") |
| A new key by `choices` counts against `players` | a solo archive stays one player's |
| `mine` and `me` count only his objects built here; `offer` and `withdraw` take only those (the world's own checks) | nobody acts on another's objects or on an import |

Owner actions (review, remove-player, members, policy) are commands on the world file while the server is stopped, and
signed calls of the owner's key while it runs ("Identity"), which the host's launcher makes from its Server owner window
(`../launcher/README.md`, "Server owner").

### Not built (server policy)

- Identity under `identity none` (friends and solo): anyone who has the group's prefix can sign in under any account
  key (`/k/NAME`, or a Steam ticket the server does not check), so someone else can act as a player: build under his
  name, be handed what is kept for him, and through the launcher's API change his choices or withdraw his objects.
  There these rules hold against honest mistakes, not against a player who wants to break them. `identity keys`
  (public) closes that, within what "Identity" says it does not protect.
- Screens: the launcher shows the policy and sets a player's choices, offers and withdrawals through the launcher's
  API, and the host's launcher has a Server owner window for the owner's signed calls (`../launcher/README.md`,
  "Server owner"); both built game-free and not seen on a screen. The game itself cannot ask (it does not know of them).
- `join` and `offer_old` are recorded only. There is no ban: `remove-player` does not stop anyone building again.
- What a game does when its upload is refused (403) after it built the structure is not known in full: live, once,
  it did not ask again and stayed up; whether it took the structure out of the player's world was not checked. The
  policy no longer refuses uploads for that reason; the hard cap still does. Unknown too: what a game does when an
  object it had is no longer handed to it (withdrawn with `go`, removed by the owner).
- A setting that changes applies to uploads from then on: nothing stored changes its state.

## Identity

A server whose policy says `identity keys` (the public preset) knows its players by a key of their own instead of by
the group's key, so nobody can sign in, build or withdraw in someone else's name. There are no accounts and no
passwords: every launcher makes an Ed25519 key pair (RFC 8032) once (`player.key` beside `launcher.ini`), its public
key is the player, and the secret half never leaves his PC. The Steam id is not used: the game's Steam ticket is not
read, and no player's Steam id is stored or shown (`identity.h`; the launcher's side in `../launcher/README.md`).
Built and tested without a game (2026-11-17).

- **The account** of a player with a key is `ed25519-` and his public key in 64 hex digits. Such an account is made
  only by a call signed with that key: by name (`/k/NAME`) it is refused under every policy, and an unsigned API call
  never acts for one. A new key becomes a player on its first signed call, within the `players` setting.
- **Signed calls.** Under keys every call of the launcher's API but `GET …/coop/policy` is signed. The signature
  (headers `X-Coop-Key`, `X-Coop-Time`, `X-Coop-Nonce`, `X-Coop-Signature`) covers the method, the address as sent
  (prefix and query included), the world's server id, the time, a random nonce and the SHA-512 of the body. The server
  takes a call within 300 s of its own clock (the launcher signs with the `time` of the policy it read, so its own
  clock may be off), once: the nonce is remembered until the call's time leaves the window, at most 1024 per key and
  65536 in all; when full, signed calls are refused until room comes back, never by forgetting a nonce early. A
  signed call acts for its signer only: its `player` (or `?player=`) must be the signer's account.
- **The game's sign-in.** The game makes its own HTTP calls, which cannot carry a signature. Just before the game
  starts, the launcher takes a sign-in ticket by a signed `POST …/coop/ticket` and writes
  `online_server` as `http://ADDRESS:PORT/PREFIX/t/TICKET/auth/ds` (instead of a `/k/NAME` anyone could write). The
  ticket signs in its player only, and gives the game a session as any sign-in does. It is good until its first use,
  at most 30 minutes, then for 2 minutes more (a sign-in the game asks again); a new ticket ends the player's older
  one. Every other sign-in (`/k/NAME`, a Steam ticket) is refused (403), and a gateway session made before the world
  took keys is no session (401).
- **The relay.** The launcher takes a relay token by a signed `POST …/coop/relay-token`; the mod sends it in HELLO and
  BYE where the group's key went (`relay_key`: the same 32-byte field, so the envelope is unchanged), with the relay id
  made from the key (SHA-512 of the key, 49 bits, plus 1; `player_id`), so nobody can take another's id. The relay
  takes a HELLO only with a token of that id: good until its first HELLO, at most 30 minutes, then while HELLOs keep
  coming and 5 minutes after the last; a new token ends the older one. The group's key is not taken under keys: a
  mod that sends it (an older launcher's config) is not answered, as for any wrong key.
- **The owner.** `--owner KEY` (the host's launcher gives its own key) makes that key the owner's; it is saved with the
  world. The owner's calls, signed with that key and with no other, under every policy:

| Call | Answer |
|---|---|
| `GET …/coop/owner/players` | every player as `coop-world players` lists them: `id`, `name`, `key` (a player's key short, `""` for an account that is no key), counts, `likes` (on what is still his), `member`, his choices |
| `GET …/coop/owner/review` | what waits or is held, as `coop-world review` lists it, with `waiting_for` (`builder` or `owner`) |
| `POST …/coop/owner/review` `{"accept":ID}`, `{"reject":ID}` or `{"accept_player":PLAYER}` | as `coop-world review --accept`, `--reject`, `--accept-player` |
| `POST …/coop/owner/remove-player` `{"player":PLAYER}` | as `coop-world remove-player` |
| `POST …/coop/owner/member` `{"player":PLAYER,"member":"yes\|no"}` | as `coop-world player-set ... member=` |
| `POST …/coop/owner/policy` `{"preset":NAME,"set":["KEY=VALUE",…]}` (one or both) | the preset and settings changed on the running server (a value that is none: 400, nothing changed); answers the policy |

An unsigned owner call is 401, one signed with another key 403, and on a world without an owner key every one is 403.

What it holds to:

| Rule | Why |
|---|---|
| Under keys only a launcher's ticket signs in; `/k/NAME` and Steam tickets are refused, and older sessions too | an address or a Steam ticket the server does not check is anyone's to write |
| An account of the key form is made and acted for only with that key's signature, under every policy | nobody takes a key's account by naming it |
| A signed call is checked whole before the API sees it: signature over method, address, server id, time, nonce and body hash | a call changed on the way, sent to another server or to another address is refused |
| Within 300 s of the server's clock, each nonce once, the nonces bounded and never forgotten early | a call cannot be replayed, and memory stays bounded |
| A signed call acts for its signer only; under keys every API call but the policy is signed | nobody changes another's choices or withdraws his objects |
| A ticket signs in its own player only, is short-lived, and a new one ends the old | a ticket is not a standing password |
| A relay token admits only the relay id made from its player's key; under keys the group's key is no token | nobody says HELLO for another player |
| The owner's calls need the owner's key | the owner's actions are his only |
| The world file keeps the owner, the tickets and the tokens; a world file from before has `identity none` | a restart keeps the players in, and an older world does not lock its players out |

**What it does not protect.** HTTP and the relay's UDP are not encrypted. Whoever can see the traffic between a
player and the server (on the same network, or on the path) can read a sign-in ticket, a game's session token or a
relay token and use it within its lifetime: sign in once with the ticket in its first 30 minutes or within 2 minutes
of its use, use the session token as long as the session lasts (sessions do not expire), or take over the relay id
with the token after its player stopped saying HELLO (while he plays, his id is in use). He also reads everything the
calls carry. Signed calls cannot be replayed or changed, but their contents are not secret. So use a LAN or a VPN, as
for the group's key; encryption is a later step. Not protected either: a player's key file on his PC (whoever copies
it is him), a flood of signed calls from many new keys (it fills the nonce memory and the world's players; there is no
rate limit per key), and the owner trusting a linked server ("Linked servers").

Not done: tickets are taken for a game that may sign in again later in a session; whether the game ever does is
unknown (the mod asks it to sign in at most five times until it is signed in, 30 s apart), and one that does after
the ticket's two minutes is refused until it is started again from the launcher. No game has signed in with a ticket
yet. The owner's calls have a launcher window (`../launcher/README.md`, "Server owner"), not seen on a screen.

## Which objects a game is handed

When a game loads an area it asks `findQpidObjects` for it with a number per kind (`o.ct`), and only for some kinds.
Live (2026-11-16, one real game, an Episode 3 save, area 226): `t` 33, `x` 3, `b` 2, `v` 4, `n` 1, `h` 5, `e` 5, and
no ladders (`l`) at all. With 40 cairns on the server in that area, on a 4 m grid, the server handed the first 5 by
id; the game activated 1 and set the other 4 to Fail (it places an object by rules of its own, and these were close
together). An object the game had activated and the server then removed stayed in the game for at least 90 s: the
game asks again only when the area loads (`mod/game_offsets.md`, "What the game does with a served object").

So the server chooses. For each kind, at most the asked number (a kind not asked gets none; a request without `ct`
gets every kind with no number), from the objects of that area that every player is handed: built here by other
players, remnants and imported ones alike. Never the asker's own (his game has them in its save), and nothing kept,
held or waiting ("Server policy"). In this order:

1. **What his game was handed there before**, while it is still on the server and handed to everyone, in the order it
   was handed. A structure that was there last time is there again, so structures do not change between sessions.
   Newer or more liked ones do not push it out. It counts for the cap per builder below and is not checked for spread.
2. **The rest, best first.** `order=newest` (friends and solo): the last changed first. `order=liked` (public): a
   friend's objects first, then the most liked, then the newest. There is no friend list yet, so nobody is a friend
   (the server has the place for one). The same time, likes and friendship: by id.
3. Skipped on the way: an object of a builder who already has `per_builder` of that kind in this answer (0: no cap;
   public 3), so one player does not fill an area while others have built there; and an object closer than `spread`
   metres (straight line, at the position `l / 10^exp` the game reads) to one of its kind already in this answer, so
   the game is handed objects it can place rather than ones it fails (0: off). An object without a position is never
   too close. A skipped object is not handed in that answer, even when nothing else is left.

The right `spread` is not measured yet. The default, 8 m, comes from the one live result above (4 m apart: one of five
placed); the game's own rule (per kind or not, the distance to the save's own structures, the ground) is unknown.

What a player was handed is remembered per player and area: what the answer held, then what was remembered before and
is still there (a kind not asked this time, or the part of a kind beyond a smaller number, comes first again when it
is asked for). It is saved with the world (`picks`), at most 1000 objects per area and 128 areas per player (the areas
asked for longest ago are forgotten first). A world file without `picks` loads with nothing remembered. An object that
is removed, withdrawn with `go`, or no longer handed to everyone leaves every answer from then on; a game that has it
already is not told.

What it holds to (`curate` in `world.cpp`, a function of the asker, the area, the asked numbers, the area's objects,
what was handed before and the policy):

| Rule | Why |
|---|---|
| At most the asked number of each kind, none of a kind not asked | the game fails what it did not ask for |
| Never the asker's own, never what is kept, held or waits | his game has his own; a player's no and the policy are not undone here |
| What a game was handed in an area comes first while it is there and handed to everyone | structures do not change between sessions |
| At most `per_builder` of one builder per kind in one answer | one player does not fill an area |
| Nothing closer than `spread` to one of its kind in the same answer | the game fails objects that stand close together |
| The memory is bounded per player and area, and saved with the world | a long-running server does not grow without end, and a restart changes no answer |

Not built: a friend list; choosing by distance from the player (the game does not say where he is when it asks).
Unknown: whether a game counts the objects it already has from the server against the number it asks for, and what
it does when one it was handed is no longer in the answer. Nothing of this has been run with a game yet.

## Tests

`make test`: the core (`world_test.cpp`), the HTTP surface without sockets (`front_test.cpp`), the built server over a
real socket (`test_server.py`), and a mutant for every protection (`test_mutants.py`): a call without a session, a
change by someone who is not the builder, an object handed back to its builder, both caps, the like range, the secret
prefix and the bearer token.

Server policy: `policy_test.cpp` (the three presets and twenty-one settings that are refused; `build` nobody and
members and the limit: uploads answered in the game's shape and held, handed to nobody else and never exported,
refused to `offer` and `review` while the reason holds and shared when it goes (a member, a higher limit, a
withdrawal), a held one whose builder is to be asked first, waiting objects that count for the limit and kept ones
that do not; one player; the builder's id and name on every object;
`offer_builds` no, ask with its answer, and the server's default; kept objects that do not use up the number a
player asks for; review, accept, reject, a whole player, never; withdrawal with stay and with go, a remnant nobody
changes and whose likes reach nobody, keeping its name after its builder renamed himself; the owner removing a
player and one of two builders from the same linked server; exports and link pages with nothing kept or waiting;
the saved world, a world file from before the policy, and eleven files that are refused). Eighteen mutants must fail
it (`POLICY_PLAYERS`, `POLICY_BUILD_NOBODY`, `POLICY_BUILD_MEMBERS`, `POLICY_LIMIT`, `POLICY_ASK`, `POLICY_REVIEW`,
`POLICY_PENDING_SERVED`, `POLICY_OFFER_NO_SERVED`, `POLICY_REMNANT_CHANGE`, `POLICY_REMNANT_LIKES`, `POLICY_PLAYER_ANY`,
`POLICY_WITHDRAW_STAY_REMOVES`, `POLICY_WITHDRAW_GO_KEEPS`, `POLICY_REMOVE_PLAYER_OTHERS`, `POLICY_EXPORT_PENDING`,
`POLICY_SAVE_LOST`, `POLICY_HELD_SERVED`, `POLICY_HELD_RELEASE`). `test_server.py` starts the built server as a
public server on loopback: an upload waits, the other player is handed nothing, by list or by id, until
`review --accept`, after a restart that keeps the preset; then `withdraw` leaves a remnant and `remove-player` takes
it; three start options are refused, and eight command lines that are refused leave the file as it was. Then a
server with `build=nobody`: the upload is answered 200 with an id of the game's shape, the other player is handed
nothing, `offer` is refused while nobody builds, and after `policy --set build=everyone` and `offer --all` the other
player finds it.

Which objects a game is handed: `curate_test.cpp` (the asked number of each kind for none, one, three and more than
there are, a kind not asked, a negative number, no numbers at all; the asker's own, a kept one and another area left
out; newest first, likes then newest, a friend first, and no friend first with `newest`; a builder capped at three
while another builder fills the rest, and no cap; 40 cairns on a 4 m grid of which five are handed at least 8 m apart
and twelve fit, `spread=0`, exactly 8 m, another kind beside one, objects without a position; the same objects again
after newer and more liked ones came, one removed and one no longer shared replaced, fewer asked and a kind not asked
still remembered, an id twice, 1050 objects with 1000 remembered; then through the server's calls: the same answer
again, an object too close to a chosen one, a removed one replaced, the picks saved and the same answer from a
restored world after a newer object came, a world file without picks, five that are refused, 148 areas of which the
last 128 are kept). Nine mutants must fail it (`CURATE_OWN`, `CURATE_UNSHARED`, `CURATE_ORDER`, `CURATE_COUNTS`,
`CURATE_UNSTABLE`, `CURATE_BUILDER_CAP`, `CURATE_SPREAD`, `CURATE_SAVE_LOST`, `CURATE_UNBOUNDED`). `test_server.py`
builds 16 cairns on a 4 m grid and, a second later, two far away on a server on loopback: asked for four, the other
player gets the two newer ones and two of the grid at least 8 m apart; two newer ones still do not change his answer,
nor does a restart (the picks are in the world file); after one of the four is removed the other three stay and the
newest takes its place.
The launcher's API: `api_test.cpp` through the HTTP surface (the policy in words for the three presets and a changed
setting; a key that has not signed in, made a player by its choices before its sign-in and the same player after it;
fourteen bodies and six keys that are refused and leave the world as it was; a full world; a player's objects in every
state with `waiting_for` and the reason a held one is held, his offer (yes and no), the owner's review, likes, a
withdrawal with stay and with go, another player's object, an import with his builder id, and every change refused as
a `GET`). Eight mutants must fail it (`API_METHOD`, `API_KEY`, `API_STRICT`, `API_CHOICES_MEMBER`, `API_CHOICES_ROOM`,
`API_ALL_IMPLIED`, `API_MINE_OTHERS`, `API_MINE_IMPORTS`), and `FRONT_PREFIX` must fail it too. `test_server.py` calls it
on the built server on loopback: the policy of a public server, a player's choices before his game signs in, his
upload waiting for his answer, his offer, the other player handed nothing, a `GET` of a withdrawal refused; after the
owner's `review --accept` and a restart, his withdrawal leaves a remnant, and the world file keeps every change.

Identity: `identity_test.cpp` (Ed25519 against RFC 8032's tests 1, 2, 3 and SHA(abc), every one with a bit changed
in the message, the signature or the key; the account and key forms; the relay id; a signed call taken once, changed
on the way in each part, signed by another key or for another key, outside the window, and the nonces bounded per key
and in all), seven mutants (`SIGN_NO_VERIFY`, `SIGN_REPLAY`, `SIGN_WINDOW`, `SIGN_BODY`, `SIGN_TARGET`, `SIGN_SERVER`,
`SIGN_NONCE_CAP`). `keys_test.cpp` through the HTTP surface and the relay with real players' sides: under keys no
sign-in by name, Steam ticket or a key's name; a ticket for its own player, again within the grace and not after, an
unused one that expired, a new one ending the old, another player's, a full world; a session from before keys;
unsigned calls refused and nothing changed; a player made by his first signed call; a call for another player, sent
twice, changed, for another server, out of the window, to another address; no ticket or token under none; relay
tokens for their id only, not the group's key, expiring unused and after the last HELLO, ending the older one, and
real players' sides answered or not; the owner's calls with his key, another key and none (members, review, policy
with refused values, remove-player); the saved world with owner, tickets and tokens, a world file from before keys,
and two that are refused; identity none as before with a key's account refused by name. Thirteen mutants must fail
it (`IDENTITY_NAME_SIGN_IN`, `IDENTITY_KEY_NAME`, `IDENTITY_OLD_SESSION`, `TICKET_EXPIRY`, `TICKET_REUSE`,
`TICKET_REPLACE`, `RELAY_TOKEN_ID`, `RELAY_TOKEN_GROUP_KEY`, `RELAY_TOKEN_EXPIRY`, `RELAY_ADMIT`, `API_UNSIGNED`,
`API_SIGNER_PLAYER`, `API_OWNER`). The loopback test with the built server and the launcher's side is
`../launcher/test_identity.py`. The other tests run their public servers with `identity=none`.

Builder names: `names_test.cpp` (display names that are and are not one; cutting in whole characters; the label for
short and long names in letters of one, two and four bytes and for a home longer than twelve; this server's players by
name only, with and without a name; an export with `home` and one of seven fields from a server without a name; an
import named by its server, a document from before named by the link and else by the id, the server's own name over
the link's, an object passed on with its first server's name, a renamed server, a long name cut with the home kept,
the owner's list, the saved world and three world files that are refused, five records whose `home` is none and one
with another eighth field). Three mutants must fail it (`LOOKUP_HOME_MISSING`, `LOOKUP_LOCAL_SUFFIX`, `LOOKUP_CUT`).
`test_names.py` runs two built servers on loopback that take from each other, one with `--name North` and one without:
each game is told its own players by name only, a builder from the other as "Ann · North" and "Bea · south" (the
link's name), a long name cut with the home kept; the name in the policy and the world file, every import's home,
`origins`, `policy --name` on the stopped server, and five names refused at the start and one on the stopped server. `keys_test.cpp` checks the owner's
list (a key short, likes).

Linked servers: `link_test.cpp` (every filter term on the giving and on the taking side; ids of the game's shape
that differ from the original and are the same in every world; the same export twice; an id that collides with a
local one; nobody changing an import; likes kept; changed and removed objects with and without prune; forgetting
one server; the triangle of three servers with and without `imported=yes`; some thirty documents that are refused
whole, the object over 64 KiB and the 20001st object among them; the saved world; then one server asking another
through the two calls a socket would carry: both filters, new, changed and removed objects, the other side away, a
restart of either side, an object that grows too old, 1200 objects in three pages, a world that changes between
two pages, ten answers and twelve requests in the wrong form, pages bounded in bytes, and two servers that take
everything from each other and settle). `front_test.cpp` has the key: the right one, eight wrong ones, a session
token, and a server without links. Eighteen mutants must fail them (`LINK_KEEP_ID`, `LINK_DUPLICATE`,
`LINK_PASS_IMPORTED`, `LINK_ECHO`, `LINK_IMPORT_OWN`, `LINK_IMPORT_OWNER`, `LINK_FILTER_TYPE`, `LINK_FILTER_OWNER`,
`LINK_FILTER_AREA`, `LINK_FILTER_AGE`, `LINK_NO_PRUNE`, `LINK_FORGET_ANY`, `LINK_CAP`, `LINK_SIZE`, `LINK_STRICT`,
`LINK_LIKES`, `LINK_VERSION`, `FRONT_LINK_KEY`). `test_link.py` runs the built server: links that do not start (no
key, a short key, a name instead of an address, an import without an address), the four file commands on two world
files (the second import leaves the file byte for byte, six files that are refused leave it too), and three
servers on loopback: two that take from each other with filters, where objects appear, go when they are removed,
never come back to their builder's server, and are caught up after either server was away, and one with the wrong
key that gets nothing.

The relay: `relay_test.cpp` runs it with a clock of its own and a network in memory, against real player sides
(`ds::relay::Client`) and raw datagrams from addresses that are not players: enter at 150 m and leave past 200 m,
height counted, areas, no position; nothing passed on to a far player; a source that never said HELLO, the wrong key,
a second address for a known id; ten seconds of silence, BYE; the rate cap and the player cap; a HELLO that arrives
late; a restarted server and a restarted game; and 100 players on a link that loses three datagrams in ten, in a
line, all in one place and all apart, with every roster and every delivery checked. Fourteen mutants must fail it
(`RELAY_NO_TOKEN`, `RELAY_REBIND`, `RELAY_ANY_SOURCE`, `RELAY_FORWARD_FAR`, `RELAY_SOURCE_ID`, `RELAY_NO_HYSTERESIS`,
`RELAY_AREA`, `RELAY_NO_POSITION`, `RELAY_NO_TIMEOUT`, `RELAY_NO_LEAVE`, `RELAY_NO_RATE`, `RELAY_PLAYER_CAP`,
`RELAY_HELLO_STALE`, `RELAY_ROSTER_ONCE`). `test_relay.py` runs the built server on loopback UDP: a HELLO written
byte by byte in Python is answered and anything without the key is not, then swarms of stand-in players find each
other through it (8 players, 56 core sessions, all connected; two groups apart; `--only`; the wrong key), and
stand-ins replaying a small recording written by the test: three of them and two plain ones connect all 20 sessions,
the plain ones take over 60 animation frames of 140 bytes and send none, the first replaying one walks the 8 m the
recording's speed gives in six seconds, and `--with-animation` is refused without a recording that has samples.

Broadcast mode: `broadcast_test.cpp` runs the relay with real broadcast players (`ds::broadcast::Client`, each on a
clock of its own) and raw datagrams: a walking, animating player seen by one within 30 m (every update, with its
animation), one at 40 m (one in 200 ms, with animation, and every pulse shown), one at 100 m (one in 200 ms, presence
only) and one far away (nothing); a teleport inside the interval; the shown state handed out on ENTER with no FETCH,
six changes while walking with no FETCH, an older version refused, FETCH answered and limited, FETCH for a player who
is not near, a game started again on either side; a PUBLISH, STATE and FETCH naming another player, and from an
address that never said HELLO; the order and the stamp window, and the sequence wrapping; pairs players, another mod
build and another game build in one place, and a server that pins the build; FRAMEs within a 400-byte budget and the
flush time; 24 players in one place. Thirteen mutants must fail it (`RELAY_BCAST_MODE`, `RELAY_BCAST_BUILD`,
`RELAY_BCAST_SPOOF`, `RELAY_BCAST_ORDER`, `RELAY_BCAST_STAMP`, `RELAY_BCAST_ANIMATION_FAR`, `RELAY_BCAST_TELEPORT`,
`RELAY_BCAST_INTEREST`, `RELAY_BCAST_ROSE`, `RELAY_BCAST_BUDGET`, `RELAY_BCAST_ENTER_STATE`, `RELAY_BCAST_STATE_OLD`,
`RELAY_BCAST_FETCH_FAR`). `test_broadcast.py` runs the built server on loopback UDP with 26 players written byte by
byte in Python: options out of range refused; 16 in a group, 8 on a ring 80 m out and 2 far away, each publishing
20 times a second; every FRAME within a 600-byte budget and from a player near, more than three entries a FRAME,
every update with its animation within 30 m, one in 200 ms and no animation beyond, a STATE handed to the 23 players
near and nobody else, an update naming another player sent nowhere, a pairs player in the group near nobody, and a
server that pins the build refusing another.

## Not done

- A build through the game's own menus has not been driven yet (the upload was made with `CreateConstruction` and
  its upload flag). A game only takes the kinds its area still has room for: two copies of a crowded save exchange
  no ladders. Rain shelters are made by the receiving game and taken down again.
- Positions are `px, py, pz` divided by 10 to the power `exp` (2 when missing: centimetres), in the game's x, y, z.
  The server stores and returns what the builder sent.
- Roads, highway resources, missions (lost cargo and shared lockers), wasted cargo, rankings, profiles, relationships.
