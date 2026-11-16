# Death Stranding co-op (Director's Cut, PC)

Work in progress: real-time co-op for DEATH STRANDING DIRECTOR'S CUT on PC (Steam, build 13300582), built from zero.
**Not playable yet; there is no release.** What works today, on one PC with two copies of the game: each copy is
isolated and kept offline, each player sees the other as a second Sam moving exactly as the other's Sam does (his
own animation controls are mirrored: walking, running, resting, dozing, balancing, falling, picking things up),
wearing the other's gear and outfit, carrying the other's cargo as the game's own cases, with his name over his
head, seated on his bike or truck (in his paint, its wheels and steering turning as his do) when he rides one,
and pinned on the map. Cargo one player puts down or picks up is put down or taken away in the other's world, and
a structure one player builds or dismantles (with the game's own construction device and structure menu, or
ladders) is built or removed in the other's. Downed, caught by BTs or resting, the partner is shown in the game's
own poses. Shared enemies, BT kills and voidout craters, roads, structure upgrades and order progress are synced. Timefall falls where the host stands, and the
game's own message feed says when the partner joins, leaves or steps into a cutscene or private room. A guest
starts from the host's save and can catch up with the host's story during a session (the host's game writes a
checkpoint, the guest's game loads it). Opt-in and newer: one load per shared order, connection points and levels
that rise together, MULEs standing where the host has them, and the partner's shout, scan and vitals. A two-PC
session tried for real, a story that moves on for both by itself, and cutscenes are still to do. See
[STATUS.md](STATUS.md).

This repository contains only original mod code. It contains no game files, assets, saves or anything copied from
the game, and it never will. You need your own copy of the game.

## Where this is going

The target changed on 2026-11-16. It is no longer "two players share one playthrough":

- **Your own save, your own story.** Each player plays their own game and completes their own orders. Story and
  order progress are not shared. How large story events such as boss fights work with visitors is undecided; the
  wish is that players can take part in each other's games without sharing progression.
- **A private strand server.** At least one player runs the server (`world/`), a stand-in for the game's online
  service that is run by the players themselves. Everyone connected to it sees everything the game's social strand
  system shares (structures, ladders, ropes, signs, lost cargo, roads), all of it rather than a selection.
- **Co-op when you meet.** When players are near each other the real-time layer switches on: they see each other
  and interact. For many players, not two.
- **Linked servers.** Private servers can be connected: export and import strand objects with a choice of what goes
  each way, and, while both are online, real-time co-op across them.

The dream is Sams everywhere as you play, and ten of them helping in a boss fight. Today's code is a two-player
real-time layer and a shared-world server that two copies sign in to; the story, order and reward sharing listed
below stays as opt-in settings but is no longer where the work goes. See [STATUS.md](STATUS.md).

## Layout

| Folder | What it is |
|---|---|
| [`core/`](core/CORE.md) | Game-free presence core: versioned handshake, quantised player snapshots, clock and RTT estimates, interpolation, reconnect, and a reliable exactly-once channel for cargo events. Tested in a simulator with a fuzzer and mutants. |
| [`mod/`](mod/README.md) | The in-process mod: Winsock transport, the game adapter (reads the local player's pose through the game's own player path), presence log, the partner's body (the mod's own Sam, or the game's resident Sam double) and vehicle, shared cargo, a stand-in partner (`coop-partner`) for testing with one copy, and analysis tools. [`mod/game_offsets.md`](mod/game_offsets.md) holds the reverse-engineering notes for the pinned build. |
| [`launcher/`](launcher/README.md) | Native Windows Play / Saves / Settings / About launcher: LAN/Tailscale lobbies, standard invites, Steam launch with isolated co-op saves, and backups. A development foundation; see its remaining standard work. |
| [`install/`](install/README.md) | Game-free package/install/uninstall core for the drop-in `avrt.dll` release layout. |
| [`redirect/`](redirect/README.md) | The loader proxy that isolates each test copy: saves, loaded modules, the single-instance guard and network access. |
| [`vendor/`](vendor/monocypher/README.md) | Monocypher (CC0 or BSD-2-Clause), unchanged: Ed25519 and SHA-512 for the players' keys (`world/identity.h`). |

## Build and test

Needs `g++`, the MinGW-w64 cross-compiler, Python 3 and a checkout of
[coop-anything](https://github.com/cranebeaker/coop-anything) for its fuzzer (`HUB=<path-to-playbook-checkout>`); no game:

```sh
make test     # every game-free test and mutant
make all      # Windows binaries (proxy, mod, probes)
```

The supported build host is Linux with the cross-compiler. Building natively on Windows with MSYS2
(`mingw-w64-x86_64-gcc`, `make`, `python`) works for `make all`: the Makefiles pick the compiler and `windres` names
MSYS2 has, and link statically. `make test` in full needs Linux (the core's host tests use `sys/syscall.h`); on
Windows run the suites one directory at a time (`make -C redirect portable-test` needs `rg`, ripgrep, on `PATH`). A
compiler or resource compiler with another name can be given as `MINGW=...` or `WINDRES=...`.

## Two PCs (built, not yet tried live)

Each player's `mod.json` names the other PC as `peer` (a numeric IPv4 address: a LAN address, or a VPN address
such as Tailscale's 100.x.y.z; host names are refused), and the proxy's network policy must list the same address
(`COOP_NETWORK_PEERS=a.b.c.d`), or the copy may not reach it. The mod binds only the local address that routes to
the peer (or `bind`, if set), never all interfaces, and accepts datagrams only from the peer's address and port.
Ports: UDP `49000 + slot`, so give the two players different slots (host 0, client 1) and allow that UDP port in the
firewall. Windows asks once whether `ds.exe` may use the network; the launcher's lobby says whether Windows
Firewall lets the game receive your partner's data.

### A group on one server (built, not yet tried live)

For more than two players nobody names a partner. One player hosts the group's server (`coop-world`: the shared
world over HTTP and the relay for players who are near each other over UDP, [world/README.md](world/README.md)), and
every player's game talks to that server and to nothing else. Everyone plays their own save.

- **Host:** in the launcher's Settings turn on "Group server", then Play, Host a game, **Start group server and
  play**. Send the group invite (`DSG-...`) shown there. Windows Firewall has to let `coop-world.exe` receive on the
  server's port, TCP and UDP (49100 with the default settings).
- **Everyone else:** Join a friend, paste the invite, **Join group**. There is no lobby: the game starts and meets
  the others on the server. More players join the same way at any time.
- By hand: a `mod.json` without `peer`, with `"server": "a.b.c.d"`, `relay`, `relay_key`, `player_id`, `others` and
  `online_server` ([mod/README.md](mod/README.md), "Group play"), and the same address in the proxy's
  `COOP_NETWORK_PEERS`. The copy can reach loopback and that address only, as before.

Other players are shown as presence only (position, pose, name); cargo, structures made in the session, vehicles
and the rest of the two-player layer are not shared with them. The key travels in the clear: use a LAN or a VPN.
The launcher steps are in [launcher/README.md](launcher/README.md).

When a session goes wrong, the launcher's About tab has **Save diagnostics for a bug report** (also
`deathstranding-coop.exe --diagnostics`): one text file with the versions, the config and the ends of the logs,
without the session code, invites or your Windows user name ([launcher/README.md](launcher/README.md)).

## Rules this project keeps

- Your own legitimately owned copy only; nothing that bypasses Steam ownership or any protection.
- Modded copies never talk to the publisher's online service (see `redirect/network_policy.h`). A private server
  that the players run themselves, reached over loopback or a listed partner address, is allowed (since 2026-11-11);
  none is integrated yet, see `mod/game_offsets.md`.
- Test copies only: never install the proxy into your real game folder, never touch your real saves.
- No game files in this repository. Reverse-engineering notes describe addresses and layouts, not game content.

Made with the [coop-anything](https://github.com/cranebeaker/coop-anything) playbook.

## License

[MIT](LICENSE). Death Stranding is a trademark of its owners; this project is not affiliated with or endorsed by
them.
