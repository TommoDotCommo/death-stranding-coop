# Death Stranding co-op (Director's Cut, PC)

Work in progress: real-time co-op for DEATH STRANDING DIRECTOR'S CUT on PC (Steam, build 13300582), built from zero.
**Not playable yet; there is no release.** What works today, on one PC with two copies of the game: each copy is
isolated and kept offline, each player sees the other as a second Sam walking, running, crouching and jumping
beside them, and cargo one player puts down appears in the other's world. Picking cargo up, the partner's gear,
networking between two PCs and everything about a shared story are still to do. See [STATUS.md](STATUS.md).

This repository contains only original mod code. It contains no game files, assets, saves or anything copied from
the game, and it never will. You need your own copy of the game.

## Layout

| Folder | What it is |
|---|---|
| [`core/`](core/CORE.md) | Game-free presence core: versioned handshake, quantised player snapshots, clock and RTT estimates, interpolation, reconnect, and a reliable exactly-once channel for cargo events. Tested in a simulator with a fuzzer and mutants. |
| [`mod/`](mod/README.md) | The in-process mod: Winsock transport, the game adapter (reads the local player's pose through the game's own player path), presence log, the partner's body (the game's resident Sam double), shared cargo, a stand-in partner (`coop-partner`) for testing with one copy, and analysis tools. [`mod/game_offsets.md`](mod/game_offsets.md) holds the reverse-engineering notes for the pinned build. |
| [`redirect/`](redirect/README.md) | The loader proxy that isolates each test copy: saves, loaded modules, the single-instance guard and network access. |

## Build and test

Needs `g++`, the MinGW-w64 cross-compiler, Python 3 and a checkout of
[coop-anything](https://github.com/cranebeaker/coop-anything) for its fuzzer (`HUB=<path-to-playbook-checkout>`); no game:

```sh
make test     # every game-free test and mutant
make all      # Windows binaries (proxy, mod, probes)
```

## Rules this project keeps

- Your own legitimately owned copy only; nothing that bypasses Steam ownership or any protection.
- Modded copies never talk to the game's online service (see `redirect/network_policy.h`).
- Test copies only: never install the proxy into your real game folder, never touch your real saves.
- No game files in this repository. Reverse-engineering notes describe addresses and layouts, not game content.

Made with the [coop-anything](https://github.com/cranebeaker/coop-anything) playbook.

## License

[MIT](LICENSE). Death Stranding is a trademark of its owners; this project is not affiliated with or endorsed by
them.
