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
           [--max-connections 32] [--ip-connections 8] [--ip-rate 20] [--ip-burst 200] [--ip-strikes 30] [--ip-block 900]
           [--request-seconds 30] [--limit-loopback | --no-ip-limits]
```

(The relay's options are under "The relay", the ones for other servers under "Linked servers", the limits under
"Limits per address". A server on a rented machine: `../deploy/vps/README.md`.)

- `--bind`: the address to listen on (default loopback). For a partner on another PC, the address he reaches you at.
- `--data`: the world file, written after every change (temporary file, then rename). Without it nothing is kept.
- `--prefix`: a secret first path element. Requests without it get 404 and change nothing. Use one whenever the
  server listens beyond loopback: the Steam ticket is not checked, so the prefix is what makes someone a player.
- `--idle-exit SECONDS`: leave when no game has asked anything for that long (the launcher starts it with six hours).
- `--trace`: every call with its body, for research. Bodies name other accounts (the game looks up the Steam
  friends list); keep the file private. Neither the Steam ticket nor a launcher's sign-in ticket (`/t/`) is written,
  nor the answers that hand out tickets and tokens (`ticket`, `relay-token`, `notice-token`); a notice token travels
  in a header, which is not written.
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
| `createObject`, `updateObject`, `deleteObject` | stored; the new id is the type letter and 12 characters (the game reads the kind from that letter), or the id the upload brings ("An object's own id"); only the builder changes or removes an object; an upload the server policy does not let be shared is answered the same way and held for its builder ("Server policy"); over 4000 objects of one player, 403; the account names in an upload's materials are kept for this server's players only ("An object's own id") |
| `findQpidObjects` | the other players' objects in that area that every player is handed ("Server policy"), at most the asked number per type, never the asker's own, chosen as "Which objects a game is handed" says |
| `getQpidObjects`, `reverseLookup` | from the stored objects and players |
| `lookup` | a builder's name: this server's players by name only, a builder from a linked server as "Name · Home" ("Builder names"), a player's collection builder as "Name · collection" ("Collections") |
| `sendLike`, `getLikeHistory` | a like is added to the object and to its builder's history (the last 2000 likes are kept); a player reads only his own history, after the time he names |
| `devoteHighwayResources`, `getHighwayResources`, `sendHighwayLikes` | what each player put into the game's auto-pavers, added up; each player is answered with what the others put in that the policy shares, and his own ("Roads paved together") |
| `getRankingSchedules`, `getWastedBaggages`, `findMissions`, `getOrderedMissions`, … | empty lists |
| anything else | `{}` and an `unhandled NAME` line in the log |
| `POST …/link/pull` with `Authorization: Link KEY` | not the game's: one page of objects for a linked server ("Linked servers") |
| `GET`/`POST …/coop/NAME` | not the game's: the launcher's API ("The launcher's API"), and the owner's calls ("Identity") |
| `GET …/coop/notices` with `X-Coop-Token` | not the game's: the mod's read of its player's own objects ("What the game is told") |

Limits: 1024 players, 4000 objects built here per player, 20000 objects held from each other server, 4 MiB per body,
32 connections (`--max-connections`), and the limits per address below.

### Limits per address

A server beyond loopback is reached by anyone who finds its port: the prefix keeps strangers out of the world, not off
the port (`limits.h`). Per IPv4 address:

| Option | Default | What |
|---|---|---|
| `--ip-connections N` | 8 | connections at once; one more is closed unanswered (0: no cap) |
| `--ip-rate N`, `--ip-burst N` | 20, 200 | requests a second, and how many can be saved up; one more is answered 429 and its connection closed (rate 0: no cap) |
| `--ip-strikes N`, `--ip-block SECONDS` | 30, 900 | that many answers 404 (a path or prefix that is no one's) within a minute block the address that long: its connections are closed unanswered; the log says so once (0: never) |
| `--request-seconds N` | 30 | a request must arrive whole within this time from its first byte (every connection, not per address) |

Loopback (127.0.0.0/8) is exempt unless `--limit-loopback` (the host's own games, the launcher, and an administrator's
`call` over SSH); `--no-ip-limits` turns the limits per address off. At most 4096 addresses are remembered: an idle one
is forgotten for a new one (never a blocked one), and when every one is busy a new address is turned away. The relay's
UDP port is not limited per address (it answers nothing without a key or token, "The relay").

| Rule | Why |
|---|---|
| One address holds at most `--ip-connections` of the `--max-connections` | one address cannot take every connection from the players |
| Over the rate: 429 and the connection ends, the request is not handled | a flood costs one answer per connection |
| Unknown paths in a minute block the address | the prefix (one of 2^32 for a launcher's group) cannot be guessed from one address |
| A request whose bytes drip in is cut off | a slow sender keeps no connection |
| The table of addresses is bounded and never forgets a block early | memory stays bounded and a block holds |

Tests: `limits_test.cpp` (five mutants: `GATE_BLOCK`, `GATE_CONNECTIONS`, `GATE_RATE`, `GATE_STRIKES`,
`GATE_UNBOUNDED`) and `test_limits.py` over sockets (the cap, 429, the block and its end, the drip, and `keygen` and
`call`; the mutant `GATE_DRIP` must fail it).

### An object's own id

A structure that is already in a player's save may carry an id from the original online service. The game sends it
again with that id when asked to (its own "re-create": the upload's body and `"id"`), and keeps its own id whatever the
answer says. The player's mod asks it to for the save's older strand (`../mod/README.md`, "Offering your existing
strand"). So `createObject` with `"id"`:

- keeps the id when it has the game's shape: the type letter (the same as `t`, lower case) and 12 letters or digits,
  and no object here has it; the upload then goes through the policy as any other (shared, held, kept, waiting);
- answers the stored object, unchanged, when the same player already has an object here under that id (the same
  structure sent twice): no second object;
- refuses (403) an id another player's object has here, a remnant's, or an import's: the game would keep calling the
  structure by an id that names someone else's object;
- refuses (400) an id of any other shape (a capital first letter, which some ids of the original service have,
  included).

The id field itself is not stored in the body. What the game sends with an older structure also carries what the
original service knew of it:

- `mt` and `rmt` (materials delivered and repairs) name each contributor by account (`c`). An account that is not a
  player of this world (the original service's accounts, another server's players) is replaced by `""`, the form the
  game uses for the builder's own entry; the amounts stay. This is done when an upload is stored, when an object is
  handed to a game and when it is given to a linked server, so objects stored before are handed without them too.
- `tags` (`ps4`, `coop`: the game's own marks of the structure) are kept as they are.
- `lp`, the like count, is kept as the object's starting likes, as for any upload.

Built and tested without a game (2026-11-17). Not tried live: no game has sent a re-create to this server yet.

## Roads paved together

The game's auto-pavers take materials from the other players through the server: what a player delivers to a paver
is sent as his contribution, and every few minutes his game asks what the others put into its pavers and adds what
grew since the last answer (static research: `../mod/game_offsets.md`, "Roads, signs and lost cargo through the
server"). A paver is named by its construction id, the same in every game (52 pavers, ids 252 and 2031 to 2511); the
resources are the game's six material indices, of which roads take 0 (chiral crystals), 2 (metals) and 3 (ceramics).

| Call | Body | Answer |
|---|---|---|
| `devoteHighwayResources` | `{"ph":[{"ci":PAVER,"ri":RESOURCE,"n":AMOUNT}]}`, what he delivered since his last call | `{"e":0}`; each entry is added to what he put into that paver |
| `getHighwayResources` | `{"cds":[{"cid":PAVER,"lgd":TIME}],"rids":[0,2,3]}`, at most ten pavers at a time | `pr`: per asked paver and resource with anything in it, `n` what the other players put in, `un` his own, `id` `"PAVER_RESOURCE"`; `cs`: per asked paver the other players who put something in, the most first, each `u` (player id) and `l` (likes for their contributions); `ul`: his own such likes |
| `sendHighwayLikes` | `{"hls":[{"u":PLAYER,"l":N}]}` | `{}`; likes are added to a player whose contributions the others get; to anyone else (himself, nobody, a player whose contributions are not shared or who put nothing in) they are dropped |

- **`n` is a total that grows.** The game keeps the last `n` it saw per paver and resource and takes the difference; a
  smaller `n` takes nothing back. His own contributions are never in `n`: his game holds them already as his own, and
  would count them twice. The game takes at most a part of each requirement from others (a factor from its data, not
  read yet), and only for pavers in areas that are connected.
- **The policy.** A player's contributions reach the others exactly while an upload of his would be shared at once
  ("Server policy"): his choice (or the server's) is `offer_builds` yes, the server takes offers without review
  (`accept_offers` auto), and he may build here (`build`: everyone, or members and he is one). Nobody can be asked
  about a delivery and there is no review of one, so `ask`, `review` and `never` keep them his. `limit` counts objects
  only. The rule is applied when the others ask: a change applies to what he put in before too (his no hides it, his
  yes brings it back). His own `un` is answered whatever the policy.
- **Taking back.** A withdrawal of everything (`withdraw --all`, or the API's `all`) takes his contributions and their
  likes back, under `stay` as under `go`: a remnant of them would be counted as someone else's in his own game. What the
  others' games already took stays there (the game never takes materials back). The owner's `remove-player` takes them
  too.
- **Bounds.** A call holds at most 64 entries, each amount 1 to 100000, paver 1 to 999999, resource 0 to 5; a body in
  the wrong form is refused whole (400) and changes nothing. A player keeps at most 1024 pairs of paver and resource
  (403 beyond), each total at most 10^9.
- **Kept** in the world file (`highway`: player, paver, resource, amount; `highway_likes`: player, likes). A world
  file without them loads; one with an entry for an unknown player, out of range or twice is refused. A `coop-world`
  from before ignores them and drops them at its next save.
- **Not carried by linked servers** (the contributions are per server), and the `lgd` the game sends is not used.

Built and tested without a game (2026-11-17). Not run with a game: whether the game's ask and answer work as read,
the share it takes from others, and whether its last `n` per paver is kept in its save (if not, a restarted game takes
the whole `n` again, within its share). With the mod's `share_roads` on as well, two players' games in one session
also exchange paver materials directly; whether that counts the partner's materials twice is a live check.

## The relay (`--relay-port`)

The real-time layer (players seeing and touching each other) is the presence core in `core/`. For more than two
players its datagrams go through this server instead of over one direct link: every game says where its player is,
the server works out who is near whom, tells both, and passes the core's datagrams on between players who are near
each other and nobody else. It is off unless `--relay-port` is given. The game mod uses it for group play (`relay`, `relay_mode` in
`../mod/README.md`), and so does the stand-in swarm below.

```
coop-world --port 8089 --relay-port 8090 [--relay-key TEXT] [--relay-enter 150] [--relay-leave 200]
           [--relay-timeout 10] [--relay-players 1024] [--relay-rate 200]
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
the HTTP side); in this mode (pairs) there is no cap on how many players may be near one player, so a crowd in one place
means every pair exchanges datagrams (the broadcast mode has one: "How many players one player sees"); under identity none the relay and the HTTP side do not know each other's players (the player id
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
           [--broadcast-near N] [--broadcast-near-margin 10]
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
- `--broadcast-near` (0..1024): the most players near one player he is shown; 0 shows everyone near. Without it the
  world's policy setting `near_cap` is the cap (friends 32, public 16; "Server policy"), read again at each of his
  HELLOs, so the owner's change applies on the running server. Next section.
- `--broadcast-near-margin` (metres, 0..1000): a player shown stays until one who is not is this much nearer.

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
| A broadcast player is told of, sent the updates and state of, and may FETCH only the players near he is shown (next section), at most his cap | a crowd beyond the cap costs him nothing |

Costs. A broadcast player sends the same however many players are near (and no PUBLISH while nobody is). For each PUBLISH the server does the interest check for each
player near who is shown him and appends at most one entry to each; each receiver gets at most one FRAME per flush time and one more
whenever the budget fills. A state change costs one SHARE (at most 1144 bytes) to each player near, a player coming
near one SHARE for each kind held each way, an answered FETCH four. The server keeps at most four times 1100 bytes of
state per player, in memory only. Measured in the tests: 24 players walking in one place, each sending presence and
a small animation sample 20 times a second (`broadcast_test.cpp`), receive about 60 datagrams and 50 kB a second each
(in pairs mode the server would pass each of them 460 presence datagrams a second and up to as many animation
datagrams); over loopback (`test_broadcast.py`) a player in a group of 16 with 8 more beyond 30 m receives about 67
datagrams and 32 kB a second with a 600-byte budget. With a cap ("How many players one player sees") what a player
receives no longer grows with the crowd: 100 players walking in one place, each sending presence 20 times a second,
in memory (`broadcast_test.cpp`) 42 datagrams and 27 kB a second each shown 16 of them, 162 datagrams and 162 kB a
second each shown all 99; over loopback with 100 `coop-swarm` stand-ins (`test_broadcast.py`) 40 datagrams and 25 kB
a second shown 16, 140 datagrams and 157 kB a second shown 99. The relay stays one thread: it wakes for a datagram,
for the next FRAME due, and four times a second.

The mod speaks it with `"relay_mode": "broadcast"` (`../mod/README.md`, "The broadcast mode") and `coop-swarm` with
`--mode broadcast`; live once on 2026-11-16 with two real games and 12 stand-ins (`../STATUS.md`, "Many players").

Not done (broadcast): fights with visitors have not run with a game (the fight channel, next section but one); the shown state is not saved; the server's cap and the players' choices are not run with a
game yet (stand-ins and players written by the tests only).

### How many players one player sees

Of the players near a broadcast player (the enter and leave distances above), the relay shows him at most a cap of
them: it tells him of exactly those (ENTER, LEAVE), sends him their updates and their state, and nothing of the others
near, not even the FRAME entries or SHAREs he would drop. A crowd beyond the cap therefore costs him nothing but the
server's own check. Who they are, chosen again at each of his HELLOs (twice a second) and whenever the players near him
change:

1. **His choice.** `everyone` (the default), `friends` (only the players on his friend list) or `nobody`. The server's
   record of his choice comes first: the launcher sets it (`POST …/coop/friends`, "The launcher's API"), under identity
   keys only. Without one, the choice his game sends in its HELLO (mod config `show_others`; `core/broadcast.h`, HELLO
   byte 65) applies. The choice is who he sees; it does not change who sees him.
2. **His friends first**, then the nearest (straight-line distance), at most the cap: `--broadcast-near`, else the
   policy's `near_cap`.
3. **No flapping.** A player shown counts `--broadcast-near-margin` metres (10) nearer than he is, so one who is not
   shown takes his place only when he is that much nearer. Two players about as far away do not swap with every step.
4. A player who starts being shown is handed his state with his ENTER, as when he comes near; the updates already
   queued for a player go out before a LEAVE, so no entry arrives from someone his roster no longer has. When a shown
   player leaves or goes away, the next one takes the slot at once.
5. A linked server's player ("Presence across linked servers") counts like any other, and is a friend by his id on his
   own server (under identity keys there, the id made from his key, as here).

Friends are known on the relay by relay id, and a relay id is someone's only under identity keys (made from his key,
"Identity"). Under identity none the relay id is whatever the game sends, so the server holds no choice and no friends
for anyone there: `friends` shows nobody, `everyone` and `nobody` work, and the cap applies.

What it holds to (`relay.cpp`, `select`):

| Rule | Why |
|---|---|
| At most the cap of the players near are shown to one player | what a player receives is bounded, whatever the crowd |
| Friends first, then the nearest | in a crowd he sees his friends and the players beside him |
| One shown stays until one not shown is the margin nearer | the roster does not change with every step |
| Nothing of a player near who is not shown: no ROSTER event, entry, SHARE or FETCH answer | a crowd beyond the cap costs him nothing |
| `friends` shows only his friends, `nobody` nobody; the server's record of his choice before his HELLO's | a player decides whom he sees, and the launcher's choice is not undone by an older config |
| A linked server's player counts for the cap | the cap holds across servers |

Not done: who sees a player (the choice is only whom he sees); a cap for a pairs player (one core session per player
near: use broadcast); friends under identity none.

### The fight channel (`--fight-visitors`)

Fights with visitors (`../mod/README.md`, "Fights with visitors"): one broadcast player's game owns a fight and other
players join it as visitors. The envelope is version 2's types 11 to 16 (`../core/CORE.md`, "Fight channel"): FIGHT
(the owner opens, keeps or closes a fight: kind, place, radius, how many visitors it takes), OFFER, JOIN, STATUS,
POST and DELIVER.

```
coop-world --port 8089 --relay-port 8090 [--fight-visitors 9]
```

- `--fight-visitors` (0..32, default 9: ten Sams): the most visitors one fight may have (the owner may ask for fewer).
  0: this server takes no fights; an owner's FIGHT is answered with STATUS Off.

| Rule | Why |
|---|---|
| A fight belongs to the player whose address sent its FIGHT (a FIGHT, JOIN or POST naming another player is dropped as spoofed) | nobody opens, joins or posts for somebody else |
| It is offered (OFFER, at most once in 900 ms, so with each keep of its owner) to every broadcast player of the owner's build in the same area within its radius of its place, by the server's own record of where each player is, who owns no fight, is not its visitor yet and whose choice of whom he is shown lets the owner through (not "nobody"; "friends" only a friend) | a fight reaches the players in reach and nobody else, and a game of another build is never sent a type it does not know |
| A JOIN is taken only for the owner's current fight, from a player of the same build in reach who owns no fight and visits none, while the fight has fewer visitors than its cap (the owner's number and `--fight-visitors`, whichever is less); the answer is STATUS Joined, or Full, Far, Busy or NoFight; a repeated JOIN is answered Joined again | the cap holds, and a lost answer costs nothing |
| The owner's POST (its own fight, a later version) goes to its joined visitors, each as a DELIVER with the owner as its source, and the latest is kept and handed to whoever joins later | only the owner's state reaches visitors, and a late visitor sees the fight at once |
| A joined visitor's POST (for the fight he visits, a later version than his last) goes to the owner and to nobody else, with the visitor as its source; any other POST (from a player who has not joined, for another fight, older) is dropped and counted (`fightRefused`) | only joined visitors' reports reach the owner, and a visitor cannot pass his report off as the owner's state |
| One role at a time: an owner joins nothing (Busy), and a visitor who sends a FIGHT of his own leaves the fight he visits | a player's game is either running a fight or showing one |
| The visitors are released with STATUS Closed when the owner closes the fight or opens another, Gone when the owner leaves, times out, restarts (HELLO stream 0) or sends no FIGHT for 3 s, and a visitor with Released when he is more than 50 m beyond the radius; a visitor who asks to leave gets Left; the owner gets STATUS Open with the number of visitors after every change | nobody keeps visiting a fight that is over, and the owner knows who is there |
| Linked servers' players neither own nor visit fights | co-op across servers is not built |

Costs: an owner sends a FIGHT a second and a POST four times a second; each POST costs one DELIVER per visitor (at
most 32, 36 bytes plus the state, about 6 bytes for one catcher). A visitor sends a report when it changes (at most
four a second) and once a second unchanged, one DELIVER to the owner each. The server keeps one state per fight.

Tests: `fight_test.cpp` (next under "Tests").

### Bug marks (F2)

F2 in a broadcast player's game (`../mod/README.md`, "F2 bug marks") sends a MARK (`../core/CORE.md`, "Bug marks",
types 17 and 18) with the mark's number. The server writes one line to its log (the session log under "Session
diagnostics", and `--log` and the console as every relay line):

```
bugmark 1048577 from 0000000000000001 (Ann) at 1794945178124
```

the mark's number, the sender's relay id (16 hex digits, as in the relay's other lines; under player keys a relay id
is the player's, made from his key), his name as his HELLO gave it, and the server's UTC time in milliseconds. The
organiser finds every report of a session in this one file, and each player's own log has the same number.

| Rule | Why |
|---|---|
| A mark is from the player whose address sent it: a MARK from an address that never said HELLO (counted `unknownSource`), or naming another player (`spoofed`), is dropped; under player keys only a relay id the keys admitted has an address | nobody marks in somebody else's name, and a stranger writes nothing in the log |
| It goes, as MARKED with the server's record of the sender as its source, to the players near who are shown him (the players his updates go to), and to nobody else: not a player near who is not shown him (his cap or his choice), not a player far away, not a pairs player, not a linked server | a mark reaches the players who can see the porter who made it |
| At most one mark every 2 s and 30 in an hour from one player (`marksRefused` counts the rest; a player who leaves and comes back starts again) | a player holding the key or a broken game cannot fill the log or the others' logs |

`RelayStats` counts `marks` (taken and logged), `marksRefused` and `marksSent` (MARKED datagrams).

### A swarm of stand-in players (`coop-swarm`)

`mod/swarm.cpp` (built by `make -C mod build/coop-swarm`, or `build/coop-swarm.exe` with the Windows binaries) is K
players in one process, each with its own socket: they say HELLO, walk a circle, and run the real protocol core with
whoever the relay says is near, with the mod's wire identity like `coop-partner`. It is the tool for putting many
Sams around one game copy once the mod speaks to the relay, and it loads a server without any game today.

```
coop-swarm --relay 127.0.0.1:8090 --count 20 --centre X Y Z --radius 15 [--mode pairs|broadcast] [--key TEXT]
           [--session N] [--speed 1.6] [--area N] [--first-id 1001] [--only ID] [--role auto|host|client]
           [--seconds N] [--stop FILE] [--require-connected] [--replay FILE [--with-animation]] [--names]
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
datagrams, 23 kB, a second each). With `--names` (broadcast) the last lines give every player near a stand-in and the
name he shows, read from his state as the mod reads it: `SWARM name id=1203948 "Sam 1001 · north"`. A bug mark
the server passes to a stand-in (broadcast) gives a line at once, `SWARM mark id=1001 from=1 name="Ann" mark=1048577`,
and the last line then ends `marks=N`.

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
without a game (2026-11-16): no game has been handed an imported object yet. Two linked servers that run the relay can
also show each other's players ("Presence across linked servers", 2026-11-17).

### What travels, and the filters

What a server stores is what can travel: the built objects (every kind the game uploads: the kind is the one letter
in `t`), each with its builder's player id and name, its area (`q`), its last change and its like count. Players,
sessions, account keys and the like history (who liked what) never travel. Paver materials ("Roads paved together")
stay on their server; road paths, missions and cargo are not stored by the server yet ("Not done"). Only what every player here is handed travels: an
object kept for its builder or waiting for an answer does not ("Server policy"); a remnant travels with the name of
the player who built it.

A filter is `all`, `none`, or terms separated by `;`. A term that is left out does not narrow.

| Term | Passes |
|---|---|
| `types=l,rope,e` | these kinds: the letter, or a name for the letters that are known: `post` p, `charger` c, `safehouse` s, `watchtower` w, `bridge` b, `shelter` n, `rope` r, `ladder` l, `bike` v, `cairn` e, `road` o, `mushroom` x, `chiralbridge` g, `jumpstand` j, `sign` t, `zipline` z, `catapult` a |
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

### Presence across linked servers (`--link-presence`)

Two linked servers that both run the relay can also show each other's broadcast players: a player of one server who
stands near a player of the other sees him (body, animation, gear, carried cases, and his name with his server's) as
he sees the players of his own server, and the other way round. It is off unless both servers ask for it on that link.
Built and tested without a game (2026-11-17), then live once the same day: two real games, each on its own server's
relay in broadcast mode, saw each other across a link on loopback, each name carrying the other's server, and the
player of a stopped server left the other game within 9 s (`../STATUS.md`, "Linked servers").

```
coop-world --port 8089 --relay-port 8090 ... --link NAME --link-key KEY --link-presence yes
           [--link-relay ADDRESS:PORT] [--link-presence-budget BYTES] [--link-presence-players N]
```

- `--link-presence yes`: exchange broadcast players with that server (default `no`). It needs `--relay-port` here, and
  `--link-presence yes` on that server's link to this one, with the same key. Objects (`--link-export`,
  `--link-import`) are independent of it.
- `--link-relay`: that server's relay (its `--bind` address and `--relay-port`), a numeric IPv4 address. One of the two
  needs the other's; the side without it learns the address from the first HELLO that carries the link's tag, so a
  server behind NAT can be that side. With it, nothing from another address is taken.
- `--link-presence-budget` (bytes a second, 2000..100000000, default 250000): the most this server sends that one,
  everything counted; a second's worth can be saved up.
- `--link-presence-players` (1..1024, default 64): the most players this server sends that one, and takes from it.

The two servers of the example above, with presence as well (the south does not need the north's relay address):

```
coop-world ... --relay-port 8090 --link south --link-key one-key-for-this-pair --link-presence yes --link-relay 10.0.0.3:8090
coop-world ... --relay-port 8090 --link north --link-key one-key-for-this-pair --link-presence yes
```

How it works:

- **Transport: UDP between the two relays, on their relay ports.** The relay already is one thread on one UDP socket
  with a clock and the broadcast mode's packing; an update is worth nothing late (a lost one is replaced 50 ms later),
  where a TCP stream would hold every later update behind a lost one. The datagrams are envelope version 3 on the
  relay's port (`presence.h`): a 32-byte header, a body and an 8-byte tag. Updates, shown state and its repair travel
  as version 2 datagrams (FRAME, SHARE, FETCH) inside, read by the same parser as the players' own.
- **Who is sent (interest, on the sending side).** Twice a second each server tells the other where its broadcast
  players stand: the 8 m cells, per area, and nothing else. A server sends a player of its own while he is within the
  other's leave distance plus 25 m of one of those cells, and goes on until 25 m more, nearest first, at most
  `--link-presence-players`. A player far from all of them, in another area or without a position is not sent at
  all, not even his name. Twice a second, and at once when it changes, it sends the list of the players it sends: id,
  area, place, state generation, game and mod build, nickname.
- **What is sent of them.** Their updates, thinned as the broadcast mode thins them for a receiver at the nearest of
  the other's cells (every update within its full distance plus 25 m, beyond that one in half its interval, animation
  within its animation distance plus 25 m, a teleport always, the pulses of left-out samples carried by the next one),
  packed into datagrams of at most 1160 bytes within the flush time; their shown state: all of it when a player starts
  to be sent, each change, and all of it again when the other side asks (FETCH, when the generation in the list is not
  the one it holds).
- **The budget.** An update of a player beyond the other's full distance is sent only while a quarter of a second's
  budget is left, less the longer that player has waited (nothing reserved once he has waited a second), so the nearest
  players keep their rate and every player sent gets about one update a second. An update that does not fit loses its
  animation first (its pulses go with the next one), then is left out.
- **On the receiving side (interest again).** A linked server's player is a player of this relay like the others:
  near this server's players by this server's own distances (placed by the list until his updates come), told by ENTER
  and LEAVE, sent in FRAMEs with the same thinning, his state handed out on ENTER, on a change and on FETCH, kept apart
  from players of another mode or build. He is never sent anything, never near another linked server's player, and
  never sent on. His stamps are taken onto this server's clock (the offset of the quickest of the last 16 HELLO round
  trips between the relays) and checked as any player's: in order, at most 2 s behind and 0.5 s ahead.
- **His id** is one of this server's: made from his id there and a key of the link's, the next one when that is taken.
  A player of this server who says HELLO with an id a linked player holds takes it; the linked player leaves and comes
  back under another with the next list. Two servers' players with the same id are therefore no problem.
- **His name is his shown state.** The receiving server adds his home: his name (from his name state, kind 1, or his
  nickname when that is no name), " · " and the name this server gave the link (`--link`), so `Bea · north`. It goes
  into his ROSTER nickname and his name state, which the mod shows over his body unchanged. The link's name is cut to
  12 bytes, then his name so that everything fits the label's 24 characters and a nickname's 32 bytes. The suffix is
  this server's: the linked server can neither change it nor leave it out.

What it holds to (`relay.cpp`, "Linked servers' presence"):

| Rule | Why |
|---|---|
| Every datagram ends in a tag (SipHash-2-4, 64 bits) under a key made from the link key; one without the link's tag changes nothing (counted as forged) | only a server with the key speaks for the link; the key never travels on this port |
| A session is taken only from a HELLO naming this side's own session, never one given up before; anything else only in the session held, naming this side's, from the address held, with a sequence not taken before (a window of 64) | a recorded datagram played again changes nothing; a server that starts again is a new session, and the old one's players leave |
| A linked server's player has an id of this server's, never one held here, and a player here takes his id back | a linked player is never shown under a local player's id, and never keeps one out |
| Updates and state from a link change only that link's players, found by their ids there | a linked server cannot touch a local player's state |
| A linked server's player keeps his own build and mode; a server that pins its build does not take another | the build and mode separation holds across servers |
| A server sends only its own players | no loops (A to B to C to A): a player is one hop from his home server |
| A player near none of the other side's cells is not sent; a sent one is near a player here only by this server's distances | interest on both sides |
| The bytes a second sent on a link stay within its budget, the nearest players first | bounded bandwidth per link |
| A player no longer in the list leaves at once; a link silent for 3 s loses all its players (LEAVE to everyone near them) | nobody stands frozen where a player was |
| At most `--link-presence-players` from a link, 4000 of its datagrams a second, every message read by a strict parser | bounded work for whatever a linked server sends |

Costs. One player near one of the other server costs the link about 2.6 kB a second each way with his animation, the
lists and the HELLOs (`presence_test.cpp`); the list costs about 54 bytes per player sent, twice a second. Thirty
walking, animating players near one player of the other server would take about 51 kB a second; on a 35 kB budget the
link sent 36.5 kB a second over 4 s, the ten nearest got 78 or more of their 80 updates each and every one of the
twenty farther ones 7 to 20. Over loopback (`test_presence.py`) thirty stand-ins on a 20000-byte link: 8, 23 and 21 kB
a second in the server's 5 s reports (a full second's worth saved up lets one window reach 1.2 times the budget), and
each of the other server's four stand-ins was shown 32 of the 33 players near him (the friends preset's `near_cap`),
the thirty linked ones counted like his own, all live. The relay stays one thread.

Changed from the design that was here before: there, one server judged each pair across the link with a second event
stream, and every core datagram of a pair crossed with both ids kept (version 1 needs them for each pair's session).
In broadcast mode a player's update crosses once whoever is near, each server judges nearness for its own players with
its own distances (the sender only chooses coarsely, by cells), and ids can be this server's, since nothing is derived
from them. Places cross only for players near the other server, not for everyone twice a second.

What is not protected: the datagrams are clear UDP, like the relay's own port (where the group's key travels in the
clear). Anyone on the path reads the places, names and state of the players sent, and the cells where the other
server's players stand; the tag stops forging and replay, not reading. The link key does travel in the clear on the
HTTP side when objects are pulled (`Authorization: Link KEY`, "What is not protected" above): whoever reads it there
can make tags too; a link for presence only never sends it. A linked server is trusted for what it says of its own
players (where they are, what they show, their builds, within the caps); it cannot reach this server's players' ids,
states or addresses. A server sends link datagrams only to `--link-relay` or to the address of a HELLO with the tag.

Not done (presence): more than two servers in one place need every pair linked (a player is shown only through the
links of his own server: in a line A, B, C the players of A and C do not see each other); pairs-mode players are not
sent (they publish nothing to copy); both servers behind NAT; IPv4 only; the sequence of one session does not wrap
(about four billion datagrams, 50 days at a thousand a second); no game has seen a linked server's player.

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
| `spread` | metres, 0 for off (up to 1000) | 20 | 20 | 20 |
| `identity` | `none`, `keys` | none | none | keys |
| `near_cap` | players near one player he is shown on the relay, 0 for no cap (up to 1024) | 32 | 32 | 16 |
| `import` | `everyone`, `members`, `owner`, `nobody` | everyone | everyone | members |
| `import_cost` | `free`, `materials` | free | free | free |
| `chat` | `on`, `off` | on | off | off |
| `map` | `on`, `off` | on | off | off |
| `test_mode` | `off`, `on` | off | off | off |

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
- `import`, `import_cost`: who may import a [collection](../docs/COLLECTIONS.md) (`owner`: a call signed with the
  owner's key), and whether what its structures carried in materials is recorded on them (`materials`; nothing is
  taken yet). A `coop-world` from before them refuses a world file that has them (unknown setting).
- `near_cap`: how many of the players near one broadcast player the relay shows him ("How many players one player
  sees"), unless the server was started with `--broadcast-near`. A world file from before it has its preset's value; a
  `coop-world` from before it refuses a world file that has it (unknown setting).
- `chat`: `on` lets the players here talk with text chat ("Text chat", below).
- `map`: `on` shows every player here to the others wherever they are ("Players on the map", below).
- `test_mode`: `on` puts every player's game here in test mode (`../mod/README.md`, "Test mode": material crates on U,
  blood and stamina kept full), for alpha testers. The server does nothing else with it: the player's launcher reads it
  with the rules before his game starts and writes the mod's `test_mode` into the game's config. Its line in words
  (`on`): "Test mode: unlimited materials (U), stamina and blood for everyone here. For testing: the game does not play
  as it should." A world file from before it is off; a `coop-world` from before it refuses a world file that has it.
  Set it with `--policy-set test_mode=on` (or `coop-world policy --set test_mode=on`, or the Server owner window).

### The players' choices

Each player has two choices on each server, kept with the world:

```
coop-world player-set --data world.json --player ID [offer_builds=yes|no|ask|default] [offer_old=yes|no|default] [member=yes|no]
coop-world offer      --data world.json --player ID (--object ID | --all) [--no]
```

- `offer_builds`: is what he builds while connected shared with the others. `yes`: shared (then as
  `accept_offers` says). `no`: stored for him and handed to nobody else, like a personal archive. `ask`: it waits
  for his answer; `offer` is his answer (yes, or `--no`: kept for him).
- `offer_old`: whether the structures of his old strand (what his save holds from before) may be offered to this
  server, a standing yes or no (default no). The server tells his game's mod what applies ("What the game is told");
  with its option `offer_strand` (on in the launcher's group play) the mod then has the game upload them
  (`../mod/README.md`, "Offering your existing strand"). The server itself cannot tell an old structure from a new one and does not enforce it: what is offered
  goes through the policy as any upload.
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

### Players on the map

The policy setting `map` (`on` in the friends preset, `off` in solo and public: under public rules strangers could follow
each other across the world). A game asks with its notice token, in the header `X-Coop-Token`:

| Call | Answer |
|---|---|
| `GET …/coop/map` | `format` 1, `on`, `wait_ms` (how long to wait before asking again: 2 s, up to 30 s for a crowd), `players`: at most 128 players the relay placed in the last 5 s (with `?x=&y=` the nearest, with `&area=` only that map), each `id` (the relay id, as text), `name`, `x`, `y`, `z` (game axes, metres, z up), and `yaw` (radians) and `area` (the map the player is in) when known. While `on` is false, none. 403 for a bad token |

The relay reports each player's place about once a second, so a player who has left drops off within seconds; players of
linked servers are not listed. Under identity keys the asker's own entry is left out and his choice of who he is shown
(`everyone`, `friends`, `nobody`, "How many players one player sees") holds; elsewhere a relay id is whatever the game says,
so the game leaves itself out. That choice is about who he sees: nobody can hide from the map but through the `map` setting.
Places are not encrypted and are not kept: only the latest one of each player is held, in memory.

Tests: `map_test.cpp`; mutants `MAP_TOKEN`, `MAP_OFF`, `MAP_STALE`, `MAP_SELF`, `MAP_CHOICE`, `MAP_AREA`, `MAP_NEAREST`.

### Text chat

The policy setting `chat` (`on` in the friends preset, `off` in solo and public until there is more moderation than kick and
ban). A game talks with the notice token its launcher took for the player, in the header `X-Coop-Token`:

| Call | Answer |
|---|---|
| `GET …/coop/chat[?since=N]` | `format` 1, `on`, `next` (what to give as `since` next time) and `lines` (`n`, `name`, `text`), at most 50, from `since` on. Without `since`: no lines, so a game that joins sees no history |
| `POST …/coop/chat` `{"text":...}` | `format` 1 and the line's `n`. 403 `chat-off` or a bad token, 400 `chat-text`, 429 `chat-rate` |

A line is 1 to 160 characters of well-formed UTF-8 without control characters, trimmed of spaces, under the name the player
signed in with ("Porter" without one). A player says at most 5 lines in 10 s. The server keeps the last 200 lines in memory
only, not in the world file, and writes each to its event log (`--log`), so its owner can see what was said. A kicked or
banned player's token ends at once. Lines are not encrypted, and players of linked servers do not see them.

### The launcher's API

Beside the game's gateway, under the same secret prefix, for the launcher and never the game (`api.h`). The launcher
shows the policy before a player's game connects, sends his choices, lists, offers and withdraws his objects, and
sets whom he sees and his friend list (`launcher/README.md`, "A group on one server"). Built and tested without a game and without a screen (2026-11-16).

| Call | Answer |
|---|---|
| `GET …/coop/policy` | `format` 1, the world's `server` id, its display `name` (`""` for none), `preset`, `players` (`count`, `limit`), with a relay `online` (`count` of the players connected now, `names`: at most 64, without those who gave none; linked servers' players not counted) and `settings`: every setting as `key`, `value`, the preset's value (`preset`) and one line in words (`text`) |
| `GET …/coop/me?player=KEY` | that player's id, `known` (has he signed in or chosen here), `name`, `member`, `choices` (his own, `""` for none), what `applies`, and counts of his objects built here: `shared`, `waiting_you`, `waiting_owner`, `held` by reason (`build`, `member`, `limit`), `kept`, `remnants`, and `likes` (on what is still his: a remnant's are nobody's) |
| `GET …/coop/mine?player=KEY` | his objects built here: `id`, `type` and `kind` (ladder, cairn, …), `area`, `state` (`shared`, `waiting`, `held`, `kept`, `remnant`), `waiting_for` (`you` or `owner`), `held` and `held_text` (why), `likes`, `updated`. Objects that came from linked servers are not his here, whatever builder id the other server names, and are not listed |
| `POST …/coop/choices` `{"player":KEY,"offer_builds":"yes\|no\|ask\|default","offer_old":"yes\|no\|default"}` | sets his choices (one or both) and answers as `me`. A key the world does not have yet becomes a player, as a sign-in would (within `players`), so the choices are in place before his game first signs in |
| `POST …/coop/offer` `{"player":KEY,"object":ID}` or `{"player":KEY,"all":true}`, `"answer":"no"` to keep it his | as `coop-world offer` (`--no`): `offered` (or `kept`) and `me` |
| `POST …/coop/withdraw` `{"player":KEY,"object":ID}` or `{"player":KEY,"all":true}` | as `coop-world withdraw`: `remnants`, `removed` and `me` |
| `GET …/coop/friends?player=KEY` | who he sees: `show_others` (his choice here, `""` for none: his game's own applies) and `friends`, each with `key` (his account key), `id`, `short` (a key's short form, `""` for an account that is no key), `known` and `name` |
| `POST …/coop/friends` `{"player":KEY,"add":[…],"remove":[…],"show_others":"everyone\|friends\|nobody\|default"}` (one or more of the last three) | changes them and answers as the `GET`. A friend is an account key, or the short form of a key (`a1b2 c3d4 e5f6 a7b8`), which names the one player of this world whose key begins so (403 when none does); at most 256 friends, never himself. A key the world does not have yet becomes a player, as by `choices` |
| `POST …/coop/ticket` `{"player":KEY}`, signed | identity keys: a sign-in ticket for his game, `ticket` and `expires` ("Identity") |
| `POST …/coop/relay-token` `{"player":KEY}`, signed | identity keys: a relay token, `token`, `relay_id` (made from his key) and `expires` |
| `POST …/coop/notice-token` `{"player":KEY}` | a notice token for his game's mod ("What the game is told"), `token` and `expires`; taken as a change is (signed under keys) |
| `POST …/coop/collection` `{"player":KEY,"objects":[ID,…]}` or `{"player":KEY,"near":METRES}` | a [collection](../docs/COLLECTIONS.md) of his own objects built here: `collection` (the DSK string), `count`, `left_out`, `anchor` ("Collections" below) |
| `POST …/coop/import` `{"player":KEY,"collection":TEXT,"place":"here\|original"}` | the collection's structures made as his uploads: `imported`, `ids`, the counts by state, `left_out`, `anchor`, `materials` |

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
| Only under the prefix, never under `/k/NAME`, and only the seven calls | the game's paths and the API do not mix, and nothing answers without the group's key |
| A change is a `POST`; a `GET` (or any other method) of a change is 404 and changes nothing | a link that is opened or fetched ahead never offers or withdraws |
| A body is one JSON object with only that call's fields, all checked before anything changes | a request in the wrong form changes nothing |
| `offer` and `withdraw` name one object or `all: true`, never neither | a body that forgot the object does not take back everything |
| `member` and the policy are not in its player calls | they are the owner's: `player-set` and `policy` on the stopped server, or the owner's signed calls ("Identity") |
| A new key by `choices` or `friends` counts against `players` | a solo archive stays one player's |
| A friend list and the choice of whom he sees are his own: under keys only his key's signed call reads or sets them; they are kept with the world (`friends`, `show_others` per player) | nobody sets whom another sees or reads his friends; a restart keeps them |
| `mine` and `me` count only his objects built here; `offer` and `withdraw` take only those (the world's own checks) | nobody acts on another's objects or on an import |

Owner actions (review, remove-player, members, policy) are commands on the world file while the server is stopped, and
signed calls of the owner's key while it runs ("Identity"), which the host's launcher makes from its Server owner window
(`../launcher/README.md`, "Server owner").

### What the game is told

The game is answered as always whatever becomes of its upload, so the player would not know that his ladder is held
for him alone, kept, or waits. The mod tells him in the game's info log (`../mod/README.md`, "Strand notices"); it
learns it here, with one call of its own (`api.h`, `front.h`; built and tested without a game, 2026-11-17):

| Call | Answer |
|---|---|
| `GET …/coop/notices[?since=VERSION]` with the header `X-Coop-Token: TOKEN` | `format` 1, the world's display `name`, a `version`, `full`, `objects`: his objects built here, each `id`, `kind` (ladder, cairn, …), `state` (`shared`, `held`, `kept`, `waiting`, `remnant`) and `why` (held: `build`, `member`, `limit`; kept: `choice`, `owner`, `server`; waiting: `you`, `owner`; else `""`), and `offer_old`: what applies to his older strand here (`yes` or `no`: his choice, else the server's) |

- **What is listed.** Every one of his objects (`full` true) on a first read, or when `since` is not a version of
  this process (the server started again, or a version it never gave); else only the objects whose state changed
  after that version (`full` false). A version is the process's epoch and a stamp that counts state changes: an
  upload, a review, an offer, a held object's reason, a withdrawal that leaves a remnant. What is removed is not
  listed again. Objects from linked servers are not his here, whatever builder id they carry, and are never listed.
- **Why kept.** Every kept object records why: his choice (`offer_builds` no, or his no to an offer), the owner's
  review (`review --reject`), or a server that takes no offers (`accept_offers` never). It is saved with the world
  (`kept`); a world file from before has none (the mod then says only "kept for you").
- **The notice token** is the call's only credential, because the mod holds no key. The launcher takes it just
  before the game starts by `POST …/coop/notice-token` `{"player":KEY}` for the account the game signs in as, as any
  change: unsigned under identity none (for any player, as every call there), signed by the player's key under keys.
  It is good until its first read, at most 30 minutes; then while reads keep coming, until 15 minutes after the last;
  a new one ends the player's older one. The time of its last read reaches the world file at most every 5 minutes, so
  a token outlives a short restart of the server. Under every identity it reads, and does nothing else.

What it holds to:

| Rule | Why |
|---|---|
| A token reads its own player's objects built here only: never another player's, never an import | a notice is about what is his |
| The token is no sign-in ticket, no session, no relay token and no API key; the call changes nothing | a token seen on the path gives nothing but those states |
| Only in the header, never in the address; `GET` only | `--trace` writes addresses; a link fetched ahead reads nothing |
| Unused 30 minutes, idle 15 minutes, a new one ends the older; kept in the world file | short-lived, and the game keeps its notices across a server restart |
| Answers go on from a version of this process only; anything else gets everything | the mod compares with what it knows, so nothing is missed or told twice |

Not protected: as for the rest of the API, plain HTTP: whoever sees the traffic reads the token and, while it is
good, the states of that player's objects.

### Not built (server policy)

- Identity under `identity none` (friends and solo): anyone who has the group's prefix can sign in under any account
  key (`/k/NAME`, or a Steam ticket the server does not check), so someone else can act as a player: build under his
  name, be handed what is kept for him, and through the launcher's API change his choices or withdraw his objects.
  There these rules hold against honest mistakes, not against a player who wants to break them. `identity keys`
  (public) closes that, within what "Identity" says it does not protect.
- Screens: the launcher shows the policy and sets a player's choices, offers and withdrawals through the launcher's
  API, and the host's launcher has a Server owner window for the owner's signed calls (`../launcher/README.md`,
  "Server owner"); both built game-free and not seen on a screen. The game itself cannot ask (it does not know of
  them); the mod tells the player in the game what became of his uploads ("What the game is told").
- `join` is recorded only. `offer_old` is acted on by the player's mod, not by the server (above). `remove-player`
  does not stop anyone building again: the owner's ban does ("Kick and ban", under "Identity").
- What a game does when its upload is refused (403) after it built the structure is not known in full: live, once,
  it did not ask again and stayed up; whether it took the structure out of the player's world was not checked. The
  policy no longer refuses uploads for that reason; the hard cap still does. Unknown too: what a game does when an
  object it had is no longer handed to it (withdrawn with `go`, removed by the owner).
- A setting that changes applies to uploads from then on: nothing stored changes its state.

### Route challenges (`/coop/challenge*`)

Players publish route challenges on the server and send their finished runs; each run's track is replayed through the
mod's own checker (`../mod/route.h` `verify`) before it counts, and every challenge has a board of best times
(`challenges.h`, saved in the world file as `"challenges"`). The calls, the policy setting `challenges`
(`everyone`, `members`, `owner`, `nobody`; public: members) and the limits are in
[../docs/ROUTES.md](../docs/ROUTES.md), "On the server". Tests: `challenges_test.cpp` (6 mutants) and
`test_challenges.py` (the real server over HTTP, with formats encoded in Python).
### Collections

A player's own objects built here made into a DSK string, and a DSK string made into objects of the player who imports
it, at the original place or where his game is (`collections.cpp`, `collection.h`; the whole design, the format and its
rules are in [../docs/COLLECTIONS.md](../docs/COLLECTIONS.md)). Built and tested without a game (2026-11-18).

- **Who and what.** A collection holds only the caller's own objects built here (not a remnant, not an import), 1 to
  200, named or within 1 to 2000 m of his spot. An import is refused unless the policy's `import` lets him; the string
  is checked whole first (400); the hard cap of 4000 refuses the whole import (403); the rest goes through his choices
  and the policy as uploads do (shared, held for `build` or `limit`, kept, waiting).
- **Where.** Original place: as the string says. Here: his spot, turned by his heading minus the collection's, in the
  area (`q`), cell and map of the nearest object with a place within 1.5 km (409 when there is none, or no spot from
  the last 10 minutes).
- **What a game is handed.** An imported structure closer than `spread` to one of its kind (one this import made, one
  everyone is handed, or one of his own) is left out (`too-close`). An imported structure is his on the server (his
  limit, withdrawal, `mine`, likes, the owner's review and kick, ban and removal; exported to linked servers under him),
  but every game is told another builder: his **collection builder** (`collectionBuilderId`, an id of the player-id
  form made from his, never a player's), which `lookup` names "Name · collection" (cut like "Name · Home", "Builder
  names"). Live (2026-11-18), his own game set structures handed to it as its own player's to Fail and never built them
  (`mod/game_offsets.md`, "Own objects served back"); told another builder, every game, his too, gets them as another
  player's (`curate` hands nobody what is told as his own). A kept, held or waiting import is handed to its owner only
  (`Candidate::owner`); `per_builder` and the friends-first order count it as his.
- **The spot.** The relay gives the world each player's place, and a broadcast player's facing, about once a second
  (`RelayConfig::spotted`); under identity keys a player's spot is his key's relay id, under identity none the one the
  launcher names (`relay_id`). Not saved.
- **The world file** keeps each import's source (`collection`) and, under `import_cost=materials`, what it carried
  (`cost`). A `coop-world` from before refuses the file anyway (its policy has `import`, a setting it does not know).

Unknown, for the next live run (`collection_smoke` again): whether the importer's game builds them now that it is told
the collection builder, and whether a game places a structure with a neighbour's area and cell.

## Identity

**Names under player keys (2026-11-18).** The launcher's ticket call carries the name the player chose in the launcher (`name`, printable, at most 64 bytes); the server keeps that as the player's name. The game's sign-in with the ticket brings the Steam name as `display_name`: under player keys the server ignores it, so strangers on a public server never see each other's Steam names. A launcher that sends no name leaves the player as `Porter`. Tests: `keys_test.cpp` (mutants `TICKET_NAME`, `TICKET_STEAM_NAME`).

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
- **The mod's read.** The mod reads its player's objects' states with a notice token the launcher took for it by a
  signed call; the read itself is not signed ("What the game is told").
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
rate limit per key, only per address: "Limits per address"), and the owner trusting a linked server ("Linked servers").

### Kick and ban

The owner ends a player's play on the running server (signed calls of the owner's key, as the other owner calls):

| Call | Answer |
|---|---|
| `POST …/coop/owner/kick` `{"player":PLAYER}` | his game's session, sign-in ticket, relay token and notice token end at once (`ended`: how many); under identity keys the relay takes him off at its next tick (a LEAVE for everyone near) |
| `POST …/coop/owner/ban` `{"player":PLAYER,"ban":"yes\|no"}` | yes: a kick, and while it holds he gets no sign-in (403, by ticket or by name), no ticket, relay token or notice token, and no launcher call but reading the rules (403); no: lifted. Answers the players, each with `banned` |

On a stopped server: `coop-world player-set --data world.json --player ID banned=yes|no`. A ban is kept in the world
file (`banned` on the player); a `coop-world` from before ignores it and drops it at its next save. It follows the
account: under identity keys a key (someone banned can make a new key and join again while he has the group's key:
change the key, `../deploy/vps/README.md`); under identity none the account name or Steam id, and the relay, which
takes the group's key from anyone, does not know him there.

Tests: `ban_test.cpp` (only the owner kicks or bans; a kick ends all four and nobody else's, and he can come back; a
ban refuses every pass, sign-in and launcher call but the rules, is saved, and is lifted; identity none by name) and
`keys_test.cpp` (the relay takes a kicked player off at once). Mutants: `BAN_PASS`, `BAN_SIGN_IN`, `BAN_SAVE_LOST`,
`BAN_KEEP_SESSION`, `BAN_KEEP_PASSES`, `BAN_API`, `BAN_RELAY_KEEP`, `RELAY_EVICT`.

### Administration over SSH

A server whose owner is not at a launcher on the same PC (a rented machine) is administered on the machine itself:

```
coop-world keygen --out owner.key                     a key pair; prints its public half (for --owner); never overwrites
coop-world pubkey --key owner.key                     the public half again
coop-world call --key owner.key --port 8089 [--prefix SECRET] GET|POST CALL [BODY]
```

`call` makes one signed call, as a launcher signs it, to the server on 127.0.0.1 only: it reads the rules for the
server's id and clock, signs `CALL` (`owner/players`, `owner/ban`, `ticket`, …) and prints the answer; exit 0 for
200, 3 for anything else. The key file is the secret half in 64 hex digits, written with mode 0600.
`../deploy/vps/coop-admin.sh` wraps it.

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
held or waiting ("Server policy"), except to its owner what he imported from a collection ("Collections"). In this
order:

1. **What his game was handed there before**, while it is still on the server and handed to everyone, in the order it
   was handed. A structure that was there last time is there again, so structures do not change between sessions.
   Newer or more liked ones do not push it out. It counts for the cap per builder below and is not checked for spread.
2. **The rest, best first.** `order=newest` (friends and solo): the last changed first. `order=liked` (public): a
   friend's objects first, then the most liked, then the newest. A friend is a builder on the asker's friend list
   (`…/coop/friends`, "The launcher's API"). The same time, likes and friendship: by id.
3. Skipped on the way: an object of a builder who already has `per_builder` of that kind in this answer (0: no cap;
   public 3), so one player does not fill an area while others have built there; and an object closer than `spread`
   metres (straight line, at the position `l / 10^exp` the game reads) to one of its kind already in this answer, so
   the game is handed objects it can place rather than ones it fails (0: off). An object without a position is never
   too close. A skipped object is not handed in that answer, even when nothing else is left.

Measured (2026-11-17, one real game, an Episode 3 save, area 226, where the game asks for 5 cairns; `coop-world` on
loopback, a second player serving cairns on a grid, the game's own lists read from the process):

| Cairns on the server | Handed | Activated | Fail |
|---|---|---|---|
| 40 on a 4 m grid (the server's `spread` 8) | 5 | 1 | 4 |
| 3 on a 4 m grid | 1 (the server's spread left out the others) | | |
| 5, 20 m apart | 5 | 4 | 1 |
| 5, 40 m apart | | 4 | 1 |

So the game fails cairns closer than somewhere between 8 and 20 m to each other, and `spread` is 20 m in every preset.
One of five failed at 20 m and at 40 m alike; why is unknown (the ground there, or the save's own cairns, are
candidates). Only cairns were measured; whether other kinds keep the same distance is unknown. A world file saved
before keeps the value it was saved with (8): `coop-world policy --set spread=20` changes it.

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

Not built: choosing by distance from the player (the game does not say where he is when it asks).
Unknown: whether a game counts the objects it already has from the server against the number it asks for, and what
it does when one it was handed is no longer in the answer. Nothing of this has been run with a game yet.

## Tests

Collections: `collection_test.cpp` (the DSK string and its fuzzer, under the sanitizers), `collections_test.cpp` (the
two calls, the policy, the transform, the spot), `relay_test.cpp` and `broadcast_test.cpp` (the spot the relay
reports), `test_collections.py` (the built server, stand-in games, the importer's game handed his imports as another
builder's, `lookup` naming "Name · collection"; a `MUTANT_COLLECTION_CREATOR_SELF` build must fail it). Twenty-two
mutants; the list is in `../docs/COLLECTIONS.md`, "Tests".

`make test`: the core (`world_test.cpp`), the HTTP surface without sockets (`front_test.cpp`), the built server over a
real socket (`test_server.py`), and a mutant for every protection (`test_mutants.py`): a call without a session, a
change by someone who is not the builder, an object handed back to its builder, both caps, the like range, the secret
prefix and the bearer token. An object's own id, in `world_test.cpp`: an upload with an id of the game's shape keeps it,
the same id again from the same player answers the same object (no second one), another player's id is refused and
the object stays its builder's, six ids of other shapes are refused, an upload without an id still gets a new one; the
other player is handed it under that id with the original service's accounts taken out of `mt` and `rmt` (amounts,
the builder's own entry, a player of this world and `tags` kept), the world file has neither account nor the id field,
and an object of an older world file is handed without them. Six mutants must fail it (`WORLD_ID_SHAPE`,
`WORLD_ID_AGAIN`, `WORLD_ID_TAKEN`, `WORLD_ID_KEEP`, `WORLD_STRIP_STORE`, `WORLD_STRIP_SERVE`).

Server policy: `policy_test.cpp` (the three presets and twenty-five settings that are refused, `near_cap` among them; `build` nobody and
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
while another builder fills the rest, and no cap; 40 cairns on a 4 m grid of which two are handed, 20 m apart at least
(twelve with `spread=8`), three on a 4 m grid of which one, five 20 m apart all five, `spread=0`, exactly 20 m, another kind beside one, objects without a position; the same objects again
after newer and more liked ones came, one removed and one no longer shared replaced, fewer asked and a kind not asked
still remembered, an id twice, 1050 objects with 1000 remembered; then through the server's calls: the same answer
again, an object too close to a chosen one, a removed one replaced, the picks saved and the same answer from a
restored world after a newer object came, a world file without picks, five that are refused, 148 areas of which the
last 128 are kept). Nine mutants must fail it (`CURATE_OWN`, `CURATE_UNSHARED`, `CURATE_ORDER`, `CURATE_COUNTS`,
`CURATE_UNSTABLE`, `CURATE_BUILDER_CAP`, `CURATE_SPREAD`, `CURATE_SAVE_LOST`, `CURATE_UNBOUNDED`). `test_server.py`
builds 16 cairns on a 4 m grid and, a second later, two far away on a server on loopback: asked for three, the other
player gets the two newer ones and one of the grid, at least 20 m apart; two newer ones still do not change his
answer, nor does a restart (the picks are in the world file); after one of the three is removed the other two stay and
the newest takes its place.
Roads paved together: `highway_test.cpp` (two players' contributions added up per paver and resource for a third,
the most first among the contributors; each player's own apart from `n`; a total that grows; only the asked pavers and
resources; fifteen bodies of `devoteHighwayResources` and seven of `getHighwayResources` refused with nothing changed,
one of them with a good entry before the bad one; a call without a session; 1024 pairs of paver and resource and the
next one refused while an old one still grows, and a total capped at 10^9; under offer_builds no and ask, his yes over
the server's no, accept_offers review and never, build nobody and members (no member, member), and his no and yes
again for what he put in before; likes to two contributors counted, to himself, to a player who put nothing in, to an
unknown id and to a player whose contributions are not shared dropped, and seven like bodies refused; one object
withdrawn leaving his contributions and everything withdrawn taking them and their likes, under go and stay;
remove-player; the saved world, a world file from before, and fourteen that are refused; the kind names `sign`,
`zipline`, `catapult`). Twelve mutants must fail it (`HIGHWAY_OWN`, `HIGHWAY_ASKED`, `HIGHWAY_RANGE`, `HIGHWAY_WHOLE`,
`HIGHWAY_KEYS`, `HIGHWAY_CHOICE`, `HIGHWAY_REVIEW`, `HIGHWAY_BUILD`, `HIGHWAY_LIKES`, `HIGHWAY_WITHDRAW`,
`HIGHWAY_REMOVE`, `HIGHWAY_SAVE_LOST`). `test_server.py` on loopback: Ann's two deliveries (in the game's wrapped body)
reach Bob as one total with Ann as contributor, a bad resource refused, Bob's like reaching Ann's `ul`, the world file
and a restart keeping them, and `remove-player` on the stopped world taking them.
The launcher's API: `api_test.cpp` through the HTTP surface (the policy in words for the three presets and a changed
setting; a key that has not signed in, made a player by its choices before its sign-in and the same player after it;
fourteen bodies and six keys that are refused and leave the world as it was; a full world; a player's objects in every
state with `waiting_for` and the reason a held one is held, his offer (yes and no), the owner's review, likes, a
withdrawal with stay and with go, another player's object, an import with his builder id, and every change refused as
a `GET`; a friend list and the choice of whom he sees set before his game signs in, read back, eleven bodies that are
refused and leave the world as it was, a short key nobody here has, too many friends, the list and the choice in a
restored world and five world files that are refused, and a friend's two older cairns handed before a stranger's two
newer ones under `order=liked`). Ten mutants must fail it (`API_METHOD`, `API_KEY`, `API_STRICT`, `API_CHOICES_MEMBER`,
`API_CHOICES_ROOM`, `API_ALL_IMPLIED`, `API_MINE_OTHERS`, `API_MINE_IMPORTS`, `FRIENDS_SAVE_LOST`,
`CURATE_FRIENDS_NONE`), and `FRONT_PREFIX` must fail it too. `test_server.py` calls it
on the built server on loopback: the policy of a public server, a player's choices before his game signs in, his
upload waiting for his answer, his offer, the other player handed nothing, a `GET` of a withdrawal refused; after the
owner's `review --accept` and a restart, his withdrawal leaves a remnant, and the world file keeps every change.

What the game is told: `notices_test.cpp` through the HTTP surface (a first read with every one of his objects, the
display name and a version; then only what changed: held for a member's server, shared after he is made a member and
the owner accepts, a remnant; nothing when nothing changed, nothing of another player's even after a change of his;
every object again for a version of another process, a later stamp or garbage; the reasons held for the limit and for
nobody, kept for his choice, his no, the owner's review and a server that takes no offers, waiting for him and for the
owner; the reason saved and two world files refused; an import with his builder id never listed; no token, a wrong
one, one in the address, a `POST`, another path: nothing; the token as a sign-in ticket, a session and a relay token:
refused; the token call's bodies in the wrong form and a full world; a new token ending the older; unused and idle
expiry, a token kept good by reads, saved, taken again after a restart within the idle time, and a world file with a
token for an unknown account refused). Ten mutants must fail it (`NOTICES_OTHERS`, `NOTICES_IMPORTS`,
`NOTICES_TOKEN_TICKET`, `NOTICES_EXPIRY`, `NOTICES_STAMP`, `NOTICES_SINCE`, `NOTICES_KEPT_WHY`, `NOTICES_EPOCH`,
`NOTICES_SAVE`, `TICKET_REPLACE`), and `NOTICES_OFFER_OLD` (what applies to his older strand in every answer: the
server's no, his yes, the server's yes with his no, and his choice set back to the server's). `keys_test.cpp`: under keys the token is taken by a signed call for the signer
only (`API_UNSIGNED` and `API_SIGNER_PLAYER` fail it too) and read unsigned. `test_server.py` on loopback: a token,
a first read, a held ladder listed with its reason and nothing of another player's, the token in the address refused
and never in `--trace`; after `player-set` and `review --accept-player` on the stopped world and a start, the ladder
listed as shared in a full answer.

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
and two that are refused; identity none as before with a key's account refused by name; a friend list set and read
only by its player's key (unsigned, another key), a friend added by the short form of his key, the relay ids the
world gives the relay for a key's choice and friends (and nothing under identity none), four broadcast players on a
relay as the server runs it (only the friend shown, then everyone within a `near_cap` of 1), the list in a restored
world. Fourteen mutants must fail it (`IDENTITY_NAME_SIGN_IN`, `IDENTITY_KEY_NAME`, `IDENTITY_OLD_SESSION`,
`TICKET_EXPIRY`, `TICKET_REUSE`, `TICKET_REPLACE`, `RELAY_TOKEN_ID`, `RELAY_TOKEN_GROUP_KEY`, `RELAY_TOKEN_EXPIRY`,
`RELAY_ADMIT`, `API_UNSIGNED`, `API_SIGNER_PLAYER`, `API_OWNER`, `API_FRIENDS_ANY`), and `FRIENDS_SAVE_LOST` too. The loopback test with the built server and the launcher's side is
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
flush time; 24 players in one place. How many one player sees: ten walking, animating players 5 to 50 m from one with
a cap of 4 (the four nearest shown, though they have the highest ids; every update and pulse of those four and no
entry, SHARE or FETCH answer of the six others; a state change of one not shown not sent; the nearest leaving and the
fifth taking his slot with his name and no FETCH; nine shown without a cap; a cap and a margin out of range refused); a
cap of 1 with one player swinging 3 m about as far as the one shown (no change in 6 s), then coming 15 m nearer (he
is shown), the other coming back to 5 m farther (no change); friends only (two friends of five near, within a cap of
3), everyone from the server's record over the HELLO's friends (a friend 100 m away first, then the nearest), nobody
(no ROSTER, entry or SHARE), the HELLO's choice again when the record is dropped, a HELLO asking for nobody; 100
walking players with a cap of 16 and without (each shown 16 or 99, no entry from a player not shown); bug marks: one
F2 of a player with one player near and shown, one near who is shown nobody and one far away (the log line with the
relay id, the name and the UTC time; a MARKED to the first only, with the name his roster gave; none back to the
sender), a second within 2 s refused, one every 2 s until 30 in the hour, one more an hour after the first, a MARK
from an address that never said HELLO and two naming another player (nothing logged, nothing sent), a far player's
mark logged and sent to nobody, and a player the keys refuse whose marks go nowhere. Twenty-five mutants must fail
it (`RELAY_BCAST_MODE`, `RELAY_BCAST_BUILD`, `RELAY_BCAST_SPOOF`, `RELAY_BCAST_ORDER`,
`RELAY_BCAST_STAMP`, `RELAY_BCAST_ANIMATION_FAR`, `RELAY_BCAST_TELEPORT`, `RELAY_BCAST_INTEREST`, `RELAY_BCAST_ROSE`,
`RELAY_BCAST_BUDGET`, `RELAY_BCAST_ENTER_STATE`, `RELAY_BCAST_STATE_OLD`, `RELAY_BCAST_FETCH_FAR`, `RELAY_NEAR_CAP`,
`RELAY_NEAR_NEAREST`, `RELAY_NEAR_HYSTERESIS`, `RELAY_NEAR_HIDDEN`, `RELAY_NEAR_FRIENDS_FIRST`, `RELAY_SHOW_FRIENDS`,
`RELAY_SHOW_NOBODY`, `RELAY_SHOW_HELLO_FIRST`, `RELAY_MARK_FAR`, `RELAY_MARK_HIDDEN`, `RELAY_MARK_RATE`,
`RELAY_MARK_ANY_SOURCE`). `test_broadcast.py` runs the built server on loopback UDP with 26 players written byte by
byte in Python: options out of range refused; 16 in a group, 8 on a ring 80 m out and 2 far away, each publishing
20 times a second; every FRAME within a 600-byte budget and from a player near, more than three entries a FRAME,
every update with its animation within 30 m, one in 200 ms and no animation beyond, a STATE handed to the 23 players
near and nobody else, an update naming another player sent nowhere, a pairs player in the group near nobody, and a
server that pins the build refusing another; then 100 `coop-swarm` stand-ins in one place on a server with
`--broadcast-near 16` and on one with 0: 1600 and 9900 pairs shown and heard, and with the cap at most 26 kB a second
to each and less than a quarter of what each receives without it.

Fights with visitors: `fight_test.cpp` runs the relay with real players' sides (`ds::broadcast::Client`) and raw
datagrams: a fight with a 50 m radius offered to the players in reach once a second, and not to one 100 m away (near
for the relay, beyond the fight), the owner, or one who sees nobody; two joining and a third refused for a cap of 2,
one refused for being out of reach, another fight id refused; a visitor offered no more; the owner's state to both
visitors and nobody else; a visitor's report to the owner only, with the visitor as source; a report repeated, from a
player who has not joined, one naming another sender, and a visitor's POST naming himself the owner: nothing anywhere;
the latest state handed at once to a visitor who joins later; the owner joining another fight refused, a visitor who
opens a fight of his own leaving his visit; a visitor 40 m beyond the radius kept and 60 m beyond it released; the
fight closed, replaced, its owner silent for 3 s while still saying HELLO, its owner's BYE and timeout; a visitor
leaving the server; a server with `fightVisitors` 0 saying Off; an owner asking for one visitor; and ten Sams: nine
visitors each with every one of 20 states and the owner with every one of their 20 reports each. Ten mutants must
fail it (`RELAY_FIGHT_REACH`, `RELAY_FIGHT_OWNER_GONE`, `RELAY_FIGHT_ONE_ROLE`, `RELAY_FIGHT_CAP`,
`RELAY_FIGHT_STATE_ANY`, `RELAY_FIGHT_UNJOINED`, `RELAY_FIGHT_SOURCE`, `RELAY_FIGHT_VISITOR_STATE`,
`RELAY_FIGHT_SILENT`, and `RELAY_BCAST_SPOOF` again). With a game's side (`../mod/fights.h`) the same server runs in
`../mod/fights_test.cpp` (in memory) and `../mod/test_fights.py` (the built server).

Presence across linked servers: `presence_test.cpp` runs two and three relays in memory, each on a clock of its own
(up to 7 s apart), linked to each other, with real players' sides (`ds::broadcast::Client`) on each: the tag's
reference vectors, every byte of a datagram changed, the replay window, a list of 300 players in 22 parts, the name
state as the mod writes it and names that are cut; Bea in the north and Carl in the south seeing each other (ids of
the other server, names with the server's name, every update with its animation, every pulse, shown where they are)
while Fay far away in the north is never sent and Dup, who has Bea's id in the south, stays himself; a player sent
while near and not once he walks away or into another area; 30 players on a 35 kB budget (the ten nearest at their
full rate, every other one now and then); the link cut (its players gone after 3 s, back when it returns), a player's
BYE (gone at once), a server that starts again, a link of which only one side knows the address; a triangle and a
line of three servers; Hal of another build sent and not near, a server that pins its build, a pairs player not sent,
gear changes, half the link's datagrams lost, a game that starts again, Gus taking back the id a linked player had; a
list played again, a forged one, one with the key from another address, a server with the wrong key. Thirteen mutants
must fail it (`LINK_PRESENCE_MAC`, `LINK_PRESENCE_REPLAY`, `LINK_PRESENCE_REMAP`, `LINK_PRESENCE_LOCAL_FIRST`,
`LINK_PRESENCE_STATE_ANY`, `LINK_PRESENCE_BUILD`, `LINK_PRESENCE_NAME`, `LINK_PRESENCE_INTEREST`, `LINK_PRESENCE_BUDGET`,
`LINK_PRESENCE_TIMEOUT`, `LINK_PRESENCE_GONE`, `LINK_PRESENCE_REEXPORT`, `LINK_PRESENCE_CLOCK`), and `RELAY_NEAR_LINKED`
(linked servers' players shown besides the cap): Bea in the north with a cap of 3, Nia of her own server 30 m away and
five southerners 5 to 25 m away, sees the three nearest southerners and nothing of the other three, and with friends
only the one southerner on her list, by his id in the south. `test_presence.py` runs
two built servers on loopback, one knowing the other's relay: eight option lines that are refused; Bea, Fay and Carl
written byte by byte (each sees the other once, under another id, as `Bea · north` and `Carl · south`, about 20
updates a second; Fay never reaches the south); the north stopped and Carl losing Bea 3 s later; a new north and
`coop-swarm --mode broadcast --names` stand-ins with the same ids on both servers, each seeing its own four and the
other's four, whose names read as the mod reads them end with ` · north` (or ` · south`); thirty stand-ins in the north
on a 20000-byte link, within it, and each of the four in the south shown 32 of his 33 (the `near_cap` of a friends
server), all live.

## Not done

- A build through the game's own menus has not been driven yet (the upload was made with `CreateConstruction` and
  its upload flag). A game only takes the kinds its area still has room for: two copies of a crowded save exchange
  no ladders. Rain shelters are made by the receiving game and taken down again.
- Positions are `px, py, pz` divided by 10 to the power `exp` (2 when missing: centimetres), in the game's x, y, z.
  The server stores and returns what the builder sent.
- Road paths (`findQpidObjects` `ro`, `createRoad`, `getRoadData`), missions (lost cargo and shared lockers), wasted
  cargo, rankings, profiles, relationships. The game's shapes for paths and missions, and a plan for each: `../mod/game_offsets.md`,
  "Roads, signs and lost cargo through the server". Signs (`t`) are uploaded and served as objects like any other
  kind (the sign's kind in `st`, its comment answered as `cm`); no game has been handed one yet.

## Session diagnostics

The Windows server writes UTF-8 session logs to
`%LOCALAPPDATA%\DeathStrandingCoop\logs\`; Linux uses
`$XDG_STATE_HOME/DeathStrandingCoop/logs` or `~/.local/state/DeathStrandingCoop/logs`.
`--max-log-mb N` sets the startup pruning budget (5–2000, default 200).
Existing `--log` output is unchanged. Windows unhandled exceptions create a
crash marker and a small minidump. See [shared diagnostics](../common/README.md).
