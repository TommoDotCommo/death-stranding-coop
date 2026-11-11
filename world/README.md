# coop-world: the players' own shared-world server

The game's online layer (other players' structures, signs, likes, lost cargo, roads) talks to a server. `coop-world` is
a small server of our own for that layer, so a co-op group shares one world of its own. It never talks to the
publisher's service and holds none of its keys; modded copies stay refused there (network policy `enforce`).

Status (2026-11-11): the game signs in and stays signed in, two copies are two players, and built objects are stored
and handed to the other players of an area. What the game does with them is not verified yet (see "Not done").

## Run

```
coop-world --port 8089 [--bind 127.0.0.1] [--data world.json] [--prefix SECRET] [--log events.log] [--trace calls.log]
```

- `--bind`: the address to listen on (default loopback). For a partner on another PC, the address he reaches you at.
- `--data`: the world file, written after every change (temporary file, then rename). Without it nothing is kept.
- `--prefix`: a secret first path element. Requests without it get 404 and change nothing. Use one whenever the
  server listens beyond loopback: the Steam ticket is not checked, so the prefix is what makes someone a player.
- `--trace`: every call with its body, for research. Bodies name other accounts (the game looks up the Steam
  friends list); keep the file private. The sign-in ticket is never written.

Each copy of the game gets the address in its `mod.json` (`mod/README.md`, "A private online server"):

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
| `createObject`, `updateObject`, `deleteObject` | stored; only the builder changes or removes an object |
| `findQpidObjects` | the other players' objects in that area, at most the asked number per type, never the asker's own |
| `getQpidObjects`, `sendLike`, `lookup`, `reverseLookup` | from the stored objects and players |
| `getRankingSchedules`, `getWastedBaggages`, `findMissions`, `getOrderedMissions`, `getLikeHistory`, … | empty lists |
| anything else | `{}` and an `unhandled NAME` line in the log |

Limits: 16 players, 4000 objects per player, 4 MiB per body, 32 connections.

## Tests

`make test`: the core (`world_test.cpp`), the HTTP surface without sockets (`front_test.cpp`), the built server over a
real socket (`test_server.py`), and a mutant for every protection (`test_mutants.py`): a call without a session, a
change by someone who is not the builder, an object handed back to its builder, both caps, the like range, the secret
prefix and the bearer token.

## Not done

- Whether the game shows a served object: the first trial built a ladder through the mod's test command, which the
  game does not upload. A build through the game's own menus is the next test.
- Roads, highway resources, missions (lost cargo and shared lockers), wasted cargo, rankings, profiles, relationships.
- Running inside the launcher, and the launcher writing `online_server` for both players.
