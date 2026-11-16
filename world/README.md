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
```

- `--bind`: the address to listen on (default loopback). For a partner on another PC, the address he reaches you at.
- `--data`: the world file, written after every change (temporary file, then rename). Without it nothing is kept.
- `--prefix`: a secret first path element. Requests without it get 404 and change nothing. Use one whenever the
  server listens beyond loopback: the Steam ticket is not checked, so the prefix is what makes someone a player.
- `--idle-exit SECONDS`: leave when no game has asked anything for that long (the launcher starts it with six hours).
- `--trace`: every call with its body, for research. Bodies name other accounts (the game looks up the Steam
  friends list); keep the file private. The sign-in ticket is never written.

The launcher does all of this for a session when "Shared world" is on in its Settings (`launcher/README.md`). By
hand, each copy of the game gets the address in its `mod.json` (`mod/README.md`, "A private online server"):

```
"online_server": "http://127.0.0.1:8089/SECRET/auth/ds"             one Steam account per player
"online_server": "http://127.0.0.1:8089/SECRET/k/NAME/auth/ds"      the account is NAME (two copies, one Steam account)
```

## What it speaks

The message shapes are the game's own, read from the executable's reflection data (`mod/game_offsets.md`, "Online
protocol"). With `online_server` the mod makes the bodies plain JSON, so nothing is encrypted on the wire: use it on
loopback, a LAN or a VPN.

| Call | Answer |
|---|---|
| `GET …/auth/ds?provider=steam&display_name=&code=TICKET` | the player (stable id per account) and a session: token and gateway address |
| `…/gate/e/getVersion` | major 0, minor 12 (the game refuses anything else) |
| `createObject`, `updateObject`, `deleteObject` | stored; the new id is the type letter and 12 characters (the game reads the kind from that letter); only the builder changes or removes an object |
| `findQpidObjects` | the other players' objects in that area, at most the asked number per type, never the asker's own |
| `getQpidObjects`, `lookup`, `reverseLookup` | from the stored objects and players |
| `sendLike`, `getLikeHistory` | a like is added to the object and to its builder's history (the last 2000 likes are kept); a player reads only his own history, after the time he names |
| `getRankingSchedules`, `getWastedBaggages`, `findMissions`, `getOrderedMissions`, … | empty lists |
| anything else | `{}` and an `unhandled NAME` line in the log |

Limits: 16 players, 4000 objects per player, 4 MiB per body, 32 connections.

## The relay (`--relay-port`)

The real-time layer (players seeing and touching each other) is the presence core in `core/`. For more than two
players its datagrams go through this server instead of over one direct link: every game says where its player is,
the server works out who is near whom, tells both, and passes the core's datagrams on between players who are near
each other and nobody else. It is off unless `--relay-port` is given. The game mod does not use it yet; the stand-in
swarm below does.

```
coop-world --port 8089 --relay-port 8090 [--relay-key TEXT] [--relay-enter 150] [--relay-leave 200]
           [--relay-timeout 10] [--relay-players 64] [--relay-rate 200]
```

- `--relay-port`: the UDP port, on the same `--bind` address as the rest (loopback by default; never all interfaces).
- `--relay-key`: the group's secret (at most 32 letters, digits, `-` and `_`); every HELLO must carry it. Default: the
  `--prefix`. Beyond loopback the server does not start a relay without one.
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
| A player's address is learned only from a HELLO with the key | someone who merely reaches the port is not a player and is never answered |
| While a player is known, a HELLO for his id from another address is refused (REFUSED, id in use) | a second sender cannot take over a player; a game that restarts on a new port waits out the timeout or says BYE first |
| DATA from an address that has not said HELLO is dropped | the source id in what is passed on is the server's own record, never a field the sender wrote |
| DATA goes only to a player who is near the sender | a player cannot reach, or load, someone across the map |
| No HELLO for the timeout: gone, LEAVE for everyone near; DATA does not keep a player | the roster follows the players who are really there |
| ENTER and LEAVE are numbered and repeated until acknowledged | a lost ENTER would leave two players near each other unseen |
| Caps: players, datagram size (1268 bytes), rate per player | one datagram in makes at most one DATA datagram out, and nothing to a sender without the key |

Not done: the mod does not speak to the relay yet; the key travels in the clear (use loopback, a LAN or a VPN, as for
the HTTP side); there is no cap on how many players may be near one player, so a crowd in one place means every pair
exchanges datagrams; the relay and the HTTP side do not know each other's players (the player id in HELLO is whatever
the game sends); IPv4 only.

### A swarm of stand-in players (`coop-swarm`)

`mod/swarm.cpp` (built by `make -C mod build/coop-swarm`, or `build/coop-swarm.exe` with the Windows binaries) is K
players in one process, each with its own socket: they say HELLO, walk a circle, and run the real protocol core with
whoever the relay says is near, with the mod's wire identity like `coop-partner`. It is the tool for putting many
Sams around one game copy once the mod speaks to the relay, and it loads a server without any game today.

```
coop-swarm --relay 127.0.0.1:8090 --count 20 --centre X Y Z --radius 15 [--key TEXT]
           [--session N] [--speed 1.6] [--area N] [--first-id 1001] [--only ID] [--role auto|host|client]
           [--seconds N] [--stop FILE] [--require-connected] [--replay FILE [--with-animation]]
```

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

## Tests

`make test`: the core (`world_test.cpp`), the HTTP surface without sockets (`front_test.cpp`), the built server over a
real socket (`test_server.py`), and a mutant for every protection (`test_mutants.py`): a call without a session, a
change by someone who is not the builder, an object handed back to its builder, both caps, the like range, the secret
prefix and the bearer token.

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

## Not done

- A build through the game's own menus has not been driven yet (the upload was made with `CreateConstruction` and
  its upload flag). A game only takes the kinds its area still has room for: two copies of a crowded save exchange
  no ladders. Rain shelters are made by the receiving game and taken down again.
- Positions are `px, py, pz` divided by 10 to the power `exp` (2 when missing: centimetres), in the game's x, y, z.
  The server stores and returns what the builder sent.
- Roads, highway resources, missions (lost cargo and shared lockers), wasted cargo, rankings, profiles, relationships.
