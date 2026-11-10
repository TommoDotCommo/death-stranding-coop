# Death Stranding co-op (Director's Cut, PC)

Work in progress: real-time co-op for DEATH STRANDING DIRECTOR'S CUT on PC (Steam, build 13300582), built from zero.
**Not playable yet; there is no release.** What works today, on one PC with two copies of the game: each copy is
isolated and kept offline, each player sees the other as a second Sam walking, running, crouching and jumping
beside them, wearing the other's gear and outfit, seated on a bike or truck when the other rides one, and pinned
on the map. Cargo one player puts down or picks up is put down or taken away in the other's world, and a structure
one player builds or dismantles (tested with ladders) is built or removed in the other's. Timefall falls where the host stands (opt-in). A two-PC session tried
for real and everything about a shared story are still to do. See [STATUS.md](STATUS.md).

This repository contains only original mod code. It contains no game files, assets, saves or anything copied from
the game, and it never will. You need your own copy of the game.

## Layout

| Folder | What it is |
|---|---|
| [`core/`](core/CORE.md) | Game-free presence core: versioned handshake, quantised player snapshots, clock and RTT estimates, interpolation, reconnect, and a reliable exactly-once channel for cargo events. Tested in a simulator with a fuzzer and mutants. |
| [`mod/`](mod/README.md) | The in-process mod: Winsock transport, the game adapter (reads the local player's pose through the game's own player path), presence log, the partner's body (the mod's own Sam, or the game's resident Sam double) and vehicle, shared cargo, a stand-in partner (`coop-partner`) for testing with one copy, and analysis tools. [`mod/game_offsets.md`](mod/game_offsets.md) holds the reverse-engineering notes for the pinned build. |
| [`launcher/`](launcher/README.md) | Native Windows Play / Saves / Settings / About launcher: LAN/Tailscale lobbies, standard invites, Steam launch with isolated co-op saves, and backups. A development foundation; see its remaining standard work. |
| [`install/`](install/README.md) | Game-free package/install/uninstall core for the drop-in `avrt.dll` release layout. |
| [`redirect/`](redirect/README.md) | The loader proxy that isolates each test copy: saves, loaded modules, the single-instance guard and network access. |

## Build and test

Needs `g++`, the MinGW-w64 cross-compiler, Python 3 and a checkout of
[coop-anything](https://github.com/cranebeaker/coop-anything) for its fuzzer (`HUB=<path-to-playbook-checkout>`); no game:

```sh
make test     # every game-free test and mutant
make all      # Windows binaries (proxy, mod, probes)
```

## Two PCs (built, not yet tried live)

Each player's `mod.json` names the other PC as `peer` (a numeric IPv4 address: a LAN address, or a VPN address
such as Tailscale's 100.x.y.z; host names are refused), and the proxy's network policy must list the same address
(`COOP_NETWORK_PEERS=a.b.c.d`), or the copy may not reach it. The mod binds only the local address that routes to
the peer (or `bind`, if set), never all interfaces, and accepts datagrams only from the peer's address and port.
Ports: UDP `49000 + slot`, so give the two players different slots (host 0, client 1) and allow that UDP port in the
firewall. Windows asks once whether `ds.exe` may use the network.

When a session goes wrong, the launcher's About tab has **Save diagnostics for a bug report** (also
`deathstranding-coop.exe --diagnostics`): one text file with the versions, the config and the ends of the logs,
without the session code, invites or your Windows user name ([launcher/README.md](launcher/README.md)).

## Rules this project keeps

- Your own legitimately owned copy only; nothing that bypasses Steam ownership or any protection.
- Modded copies never talk to the game's online service (see `redirect/network_policy.h`).
- Test copies only: never install the proxy into your real game folder, never touch your real saves.
- No game files in this repository. Reverse-engineering notes describe addresses and layouts, not game content.

Made with the [coop-anything](https://github.com/cranebeaker/coop-anything) playbook.

## License

[MIT](LICENSE). Death Stranding is a trademark of its owners; this project is not affiliated with or endorsed by
them.
