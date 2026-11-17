# 🚚 Death Stranding co-op (Director's Cut, PC)

Work in progress: co-op for DEATH STRANDING DIRECTOR'S CUT on PC (Steam, build 13300582), built from zero. Each
player keeps their own save and story; players who meet see each other and play together, and a strand server the
players run themselves shares what everyone builds. **Not playable yet; there is no release.** Everything marked
as working below ran in the real game as two copies on one PC; two PCs have not been tried. The details are in
[STATUS.md](STATUS.md).

This repository contains only original mod code. It contains no game files, assets, saves or anything copied from
the game, and it never will. You need your own copy of the game.

## ✨ When you meet another player

|  | Feature | Status |
|---|---|:---:|
| 🧍 | **Another Sam** moving exactly as the other player's does: walking, running, resting, dozing, balancing, falling, picking things up | ✅ |
| 🎒 | **Their gear, outfit and carried cargo**, drawn as the game's own cases on back, hips and arms | ✅ |
| 🏷️ | **Their name** over their head; across linked servers with their home server ("Host Sam · north") | ✅ |
| 🏍️ | **Their bike or truck** in their paint, wheels and steering turning; **ride along** on it (key J) | ✅ |
| 🗺️ | **On the map**, with pings | ✅ |
| 💀 | **Down, caught by BTs or resting**: shown in the game's own poses | ✅ |
| 👥 | **Many players at once** through the server's relay (one game showed 16 others) | ✅ |
| ⚔️ | **Boss fights with visitors**: one game owns a BT catcher fight, the others join it, everyone's hits count, and it ends cleared for all | ✅ |
| 🎭 | **Story bosses** (scripted fights) with visitors | ❌ |
| 🎬 | **Cutscenes** together | ❌ |

## 🌐 Your strand on a private server

|  | Feature | Status |
|---|---|:---:|
| 🖥️ | **A strand server you run yourselves** (`coop-world`): the game signs in to it instead of the publisher's service | ✅ |
| 🪜 | **Ladders and cairns** one player builds appear in the others' worlds | ✅ |
| 🌉 | **Other players' bridges** built as soon as they arrive (option `build_received`) | ✅ |
| ⛺ | **Other players' rain shelters**: the receiving game takes them down; a fix (skip its area check for them, as for the save's) is built, not yet tried in the game | 🧪 |
| 📤 | **Offering what you built before** (option `offer_strand`): the structures already in your save go to the server, one at a time, when you and the server say so | ✅ |
| 🙋 | **The builder's name and likes** over a structure, as the server names them | ✅ |
| 👥 | **Players' own choices** per server: share new builds or not, offer older ones or not, show everyone, friends or nobody | 🧪 |
| 💬 | **Told in the game** when your build isn't shared there ("Your ladder is not shared on North: …") | ✅ |
| 🎯 | **Fair picks** when an area is full: newest or most liked first, a cap per builder, spread out, the same picks every time | 🧪 |
| 📜 | **Server rules**: friends, solo and public presets, limits, review, withdrawal (stay or go), the owner removing a player's builds | 🧪 |
| 🔑 | **Player keys** made by the launcher, no accounts; signed calls | 🧪 |
| 🔗 | **Linked servers**: import and export with filters by kind, builder, area and age | 🧪 |
| 👋 | **Seeing players on a linked server** when you meet | ✅ |
| 🛣️ | **Roads, signs and lost cargo** through the server | ❌ |

## 🤝 Two players on one link (opt-in, no longer developed)

The first version shared one playthrough between two players. It stays as settings, but new work goes to the
server above.

|  | Feature | Status |
|---|---|:---:|
| 📦 | **Cargo** put down or picked up by one is gone or there for the other | ✅ |
| 🏗️ | **Structures** built, upgraded or dismantled by one, the same in the other's world | ✅ |
| 🌧️ | **Rain and timefall** where the host stands | ✅ |
| 👻 | **MULEs and BTs**: who is down, BT kills, the catcher fight, cleared areas and voidout craters | ✅ |
| 🛤️ | **Roads** paved together | ✅ |
| 📋 | **Orders**: one load per order, units handed in, connection levels rising together | ✅ |
| 📖 | **Story catch-up**: the guest loads the host's checkpoint | ✅ |
| 📣 | **Shout, scan and vitals**, and the game's message feed when the partner joins or leaves | ✅ |

✅ works in the real game (two copies on one PC) · 🧪 built and tested without the game · ⚠️ works with issues · ❌ not yet

## 🐞 Known issues

- 🖥️ Everything live so far ran as **two copies on one PC**. Two PCs, a router and a firewall between them have not been tried.
- ⛺ **Rain shelters** from other players are taken down by the receiving game, even with the game's range and placement checks skipped. Read from the game's code: a received shelter is told to recheck its floor, and then the game checks that another player's structure is not in an area where it keeps them out. `build_received` now skips that area check too; not yet tried in the game.
- 🧱 With `build_received`, a received structure **skips the game's own spacing and area checks**, so it may overlap something in your world or stand where the game keeps other players' structures out. The server keeps the objects it hands out 20 m apart.
- 🗑️ A structure **removed from the server** stays in a running game until its area loads again.
- 🔢 The game takes **only so many objects of each kind per area** (5 cairns at Lake Knot, no ladders there); the server chooses which.
- 🧟 A dead partner is laid down stiffly: there is no death animation for him yet.
- 🔓 Keys and objects travel **unencrypted**: play on a LAN or a VPN such as Tailscale.
- 🔄 **Only build 13300582** is supported. Other builds are refused rather than risk a crash.

## 🗺️ Next steps

- [ ] Keep other players' rain shelters
- [ ] See your offered strand arrive in a friend's game
- [ ] A real two-PC session
- [ ] See the launcher's windows on a screen: group server, join screen, My strand, server owner, who I see
- [ ] More than one visitor in a fight, and story bosses
- [ ] Roads, signs and lost cargo through the server
- [ ] Encryption, before any public server
- [ ] A one-click installer and a manual for players

The full, dated record of what ran and how is in [STATUS.md](STATUS.md).

## 🧭 Where this is going

The target changed on 2026-11-16. It is no longer "two players share one playthrough":

- **Your own save, your own story.** Each player plays their own game and completes their own orders. Story and
  order progress are not shared. In a boss fight one game owns the fight and the others join it as visitors
  ([mod/README.md](mod/README.md), "Fights with visitors").
- **A private strand server.** At least one player runs the server (`world/`), a stand-in for the game's online
  service that is run by the players themselves. Everyone connected to it sees everything the game's social strand
  system shares (structures, ladders, ropes, signs, lost cargo, roads), all of it rather than a selection.
- **Co-op when you meet.** When players are near each other the real-time layer switches on: they see each other
  and interact. For many players, not two.
- **Linked servers.** Private servers can be connected: export and import strand objects with a choice of what goes
  each way, and, while both are online, seeing each other across them.

The dream is Sams everywhere as you play, and ten of them helping in a boss fight.

## 📁 Layout

| Folder | What it is |
|---|---|
| [`core/`](core/CORE.md) | Game-free presence core: versioned handshake, quantised player snapshots, clock and RTT estimates, interpolation, reconnect, and a reliable exactly-once channel for cargo events. Tested in a simulator with a fuzzer and mutants. |
| [`mod/`](mod/README.md) | The in-process mod: Winsock transport, the game adapter (reads the local player's pose through the game's own player path), presence log, the partner's body (the mod's own Sam, or the game's resident Sam double) and vehicle, shared cargo, a stand-in partner (`coop-partner`) for testing with one copy, and analysis tools. [`mod/game_offsets.md`](mod/game_offsets.md) holds the reverse-engineering notes for the pinned build. |
| [`launcher/`](launcher/README.md) | Native Windows Play / Saves / Settings / About launcher: LAN/Tailscale lobbies, standard invites, Steam launch with isolated co-op saves, and backups. A development foundation; see its remaining standard work. |
| [`install/`](install/README.md) | Game-free package/install/uninstall core for the drop-in `avrt.dll` release layout. |
| [`redirect/`](redirect/README.md) | The loader proxy that isolates each test copy: saves, loaded modules, the single-instance guard and network access. |
| [`vendor/`](vendor/monocypher/README.md) | Monocypher (CC0 or BSD-2-Clause), unchanged: Ed25519 and SHA-512 for the players' keys (`world/identity.h`). |

## 🛠️ Build and test

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

## 🖧 Two PCs (built, not yet tried live)

Each player's `mod.json` names the other PC as `peer` (a numeric IPv4 address: a LAN address, or a VPN address
such as Tailscale's 100.x.y.z; host names are refused), and the proxy's network policy must list the same address
(`COOP_NETWORK_PEERS=a.b.c.d`), or the copy may not reach it. The mod binds only the local address that routes to
the peer (or `bind`, if set), never all interfaces, and accepts datagrams only from the peer's address and port.
Ports: UDP `49000 + slot`, so give the two players different slots (host 0, client 1) and allow that UDP port in the
firewall. Windows asks once whether `ds.exe` may use the network; the launcher's lobby says whether Windows
Firewall lets the game receive your partner's data.

### 👥 A group on one server (live on one PC, not yet across PCs)

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

## 📜 Rules this project keeps

- Your own legitimately owned copy only; nothing that bypasses Steam ownership or any protection.
- Modded copies never talk to the publisher's online service (see `redirect/network_policy.h`). A private server
  that the players run themselves, reached over loopback or a listed partner address, is allowed (since 2026-11-11):
  `world/` is that server.
- Test copies only: never install the proxy into your real game folder, never touch your real saves.
- No game files in this repository. Reverse-engineering notes describe addresses and layouts, not game content.

Made with the [coop-anything](https://github.com/cranebeaker/coop-anything) playbook.

## ⚖️ License

[MIT](LICENSE). Death Stranding is a trademark of its owners; this project is not affiliated with or endorsed by
them.
