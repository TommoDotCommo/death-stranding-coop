# 🚚 Death Stranding co-op (Director's Cut, PC)

Work in progress: co-op for DEATH STRANDING DIRECTOR'S CUT on PC (Steam, build 13300582), built from zero. Each
player keeps their own save and story; players who meet see each other and play together, and a strand server the
players run themselves shares what everyone builds. **Closed alpha:
[`v0.1.0-alpha.1`](https://github.com/TommoDotCommo/death-stranding-coop/releases/tag/v0.1.0-alpha.1) is out for
testers.** Most features marked as working below ran as two copies of the game on one PC. The main paths also ran on
two PCs with their own Steam accounts, through a rented server over the internet; players in different homes have
not been tried together. The details are in [STATUS.md](STATUS.md).

This repository contains only original mod code. It contains no game files, assets, saves or anything copied from
the game, and it never will. You need your own copy of the game.

## ✨ When you meet another player

|  | Feature | Status |
|---|---|:---:|
| 🧍 | **Another Sam** moving exactly as the other player's does: walking, running, resting, dozing, balancing, falling, picking things up | ✅ |
| 🎒 | **Their gear, outfit and carried cargo**, drawn as the game's own cases on back, hips and arms (in group play on a server: from the release after `v0.1.0-alpha.1`) | ✅ |
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
| ⛺ | **Other players' rain shelters** built and kept (option `build_received`) | ✅ |
| ⚡ | **Others' builds appear at once** while you play (any kind, generators too), and go when taken down | ✅ |
| 🏗️ | **Free build** (rule `free_build`): structures may stand anywhere, on each other and inside walls | 🧪 |
| 📦 | **Cargo set down for the others** through the server, until someone takes it (rule `cargo`) | ✅ |
| 🏝️ | **Only the server's world** (rule `world`, on under public rules): the structures in your save stay hidden while you play there and are all back without it; your own builds there come back from the server; nothing of the server's goes into your save | ✅ |
| 📤 | **Offering what you built before** (option `offer_strand`): the structures already in your save go to the server, one at a time, when you and the server say so | ✅ |
| 🙋 | **The builder's name and likes** over a structure, as the server names them | ✅ |
| 🧳 | **Collections**: select what you built, share it as one line of text (`DSK1-...`), and import someone's here or at its original place ([docs/COLLECTIONS.md](docs/COLLECTIONS.md)) | 🧪 |
| 👥 | **Players' own choices** per server: share new builds or not, offer older ones or not, show everyone, friends or nobody | 🧪 |
| 💬 | **Told in the game** when your build isn't shared there ("Your ladder is not shared on North: …") | ✅ |
| 🎯 | **Fair picks** when an area is full: newest or most liked first, a cap per builder, spread out, the same picks every time | 🧪 |
| 📜 | **Server rules**: friends, solo and public presets, limits, review, withdrawal (stay or go), the owner removing a player's builds | 🧪 |
| 🔑 | **Player keys** made by the launcher, no accounts; signed calls | 🧪 |
| 🔗 | **Linked servers**: import and export with filters by kind, builder, area and age | 🧪 |
| 👋 | **Seeing players on a linked server** when you meet | ✅ |
| 🧭 | **Route challenges**: record a delivery route, set a corridor (say 5 m wide over a mountain) and rules, share it as a string or on the server; others run it, the mod checks they stay inside, the server keeps the best times ([docs/ROUTES.md](docs/ROUTES.md)) | 🧪 |
| 🛣️ | **Roads paved together through the server**: what each player puts into the auto-pavers adds up for the others; the game asks and gets the answer, but has not taken materials from it yet | 🧪 |
| 🪧 | **Signs and lost cargo** through the server | ❌ |

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

## 📥 Install (alpha)

The first alpha for testers, `v0.1.0-alpha.1`, is on the
[Releases page](https://github.com/TommoDotCommo/death-stranding-coop/releases). You need DEATH STRANDING DIRECTOR'S
CUT on Steam, **build 13300582** (betas: None), 64-bit Windows 10 or 11, and Steam running.

1. Download **`DeathStrandingCoop-Setup.exe`**. To check it, compare
   `Get-FileHash .\DeathStrandingCoop-Setup.exe` (PowerShell) with `DeathStrandingCoop-Setup.exe.sha256`.
2. Run it. The alpha is not code-signed, so Windows SmartScreen may say "Windows protected your PC": choose
   **More info**, then **Run anyway**. Setup asks for no administrator rights (only the optional firewall rule does).
3. Setup lists every copy of the game in your Steam libraries with a build check (Browse for another folder). One
   button says what it will do: **Install**, **Update**, **Repair**, **Up to date** or **Needs attention**. Options:
   a desktop shortcut and invite links (`deathstranding-coop://`) ticked, the Windows Firewall rule for hosting not
   ticked (it asks for administrator permission once).
4. **Play** opens the co-op launcher.

Setup adds one file to the game folder, `avrt.dll` (the game loads it but does not ship one, so no game file is
replaced), and puts the launcher, the mod, co-op saves, settings and logs in `%LOCALAPPDATA%\DeathStrandingCoop`.
Every file it installs has a `.coop-receipt.json` beside it. It never replaces another mod's `avrt.dll`: the proxy
cannot load another one after itself yet, so Setup stops and says so. Starting the game from Steam without the
launcher plays it as before, co-op off. Details: [install/README.md](install/README.md).

**Without Setup:** extract `deathstranding-coop-<version>.zip` anywhere and start `deathstranding-coop.exe` from that
folder; it offers the same install (without the shortcut, firewall and link options). For scripts:
`DeathStrandingCoop-Setup.exe status [--json] [<game folder>] | install <game folder> --yes | uninstall <game folder> --yes`
(exit codes 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running).

**Uninstall:** run Setup and choose **Uninstall**: the co-op files, the shortcut, the invite links and the firewall
rules go; your co-op saves and settings stay in `%LOCALAPPDATA%\DeathStrandingCoop` (delete it by hand to remove
them). A file changed after installing, or another program's file, is left where it is and listed. The launcher's
About, **Remove co-op from the game**, takes only the mod out of the game.

**Updating** is manual for now: run the new `DeathStrandingCoop-Setup.exe`; the button says **Update**. Everyone in a
group needs the same version.

## 🧪 Test saves

To test a part of the game you have not reached, start from a test save: ready-made co-op saves at different points
of the story. They come as a separate download beside each release,
`deathstranding-coop-test-saves-<version>.zip`, never in this repository.

1. In the launcher, Saves, **Get test saves...** opens the release page. Download the test saves zip and put it
   beside `deathstranding-coop.exe`. There is no need to extract it.
2. Close the game. Choose a test save in the list and **Use this test save**.
3. Start the game from the launcher (Play solo, Host or Join). **Continue** loads the test save.

Your co-op saves from before move to `backups\before-test-save-<time>` in the co-op data folder
(`%LOCALAPPDATA%\DeathStrandingCoop`); nothing is deleted. Each save is signed for your own Steam account. Your
ordinary Death Stranding saves (outside co-op) are never touched. Details: [launcher/README.md](launcher/README.md)
("Test saves").

What is left before a public release, and the features planned: [docs/ROADMAP.md](docs/ROADMAP.md).

## 🐞 Known issues

- 🎒 In `v0.1.0-alpha.1`, **group play on a server shows other players without their gear or carried cargo**, and **an Odradek arm can float in the air** near you when another player arrives or goes out of sight. Both are fixed for the next release.
- 🖥️ Most features have only run as **two copies on one PC**. Two PCs in one home have met through a rented server; players in different homes, behind other routers, have not been tried together.
- 🧱 With `build_received`, a received structure **skips the game's own spacing and area checks**, so it may overlap something in your world or stand where the game keeps other players' structures out. The server keeps the objects it hands out 20 m apart.
- 🗑️ A structure **removed from the server** stays in a running game until its area loads again.
- 🔢 The game takes **only so many objects of each kind per area** (5 cairns at Lake Knot, no ladders there); the server chooses which.
- 🧟 A dead partner is laid down stiffly: there is no death animation for him yet.
- 🔓 Keys and objects travel **unencrypted**: play on a LAN or a VPN such as Tailscale.
- 🔄 **Only build 13300582** is supported. Other builds are refused rather than risk a crash.
- 🛡️ **Alpha:** the Setup and the binaries are not code-signed, so SmartScreen and some antivirus programs warn.
- 🧩 Another mod's `avrt.dll` in the game folder blocks the install (no chain-loading yet).

## 🐛 Reporting a bug

1. In the game, press **F2** when something goes wrong: it marks the moment in the co-op log.
2. In the launcher, About, **Send diagnostics**: it writes
   `DeathStrandingCoop-diagnostics-<time>.zip` to your Desktop, with Windows user and computer names, addresses and
   Steam ids taken out. Look inside before you send it.
3. Open an issue with the [bug form](https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose) (or the
   playtest report after a session) and attach the zip.

## 🔒 Privacy

The mod, the launcher and Setup have no telemetry and send nothing to the author. The game talks only to the co-op
server you choose and to the players in your session; the publisher's online service is blocked while co-op runs.
Setup makes no network connection. Diagnostics leave your PC only when you send them.

## 🗺️ Next steps

- [ ] See your offered strand arrive in a friend's game
- [x] A real two-PC session
- [x] A one-click installer and a guide for testers ([docs/TESTERS.md](docs/TESTERS.md))
- [ ] Three or more real players, from different homes, on one server
- [ ] See the launcher's windows on a screen: group server, join screen, My strand, server owner, who I see
- [ ] More than one visitor in a fight, and story bosses
- [ ] Roads paved together through the server, in a game; then signs and lost cargo
- [ ] Encryption, before any public server

The full, dated record of what ran and how is in [STATUS.md](STATUS.md).
The words used for the mod's parts, the game and testing are in the [glossary](docs/GLOSSARY.md).

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
| [`launcher/`](launcher/README.md) | Native Windows Play / Saves / Challenges / Settings / About launcher: LAN/Tailscale lobbies, standard invites, Steam launch with isolated co-op saves, and backups. A development foundation; see its remaining standard work. |
| [`install/`](install/README.md) | What goes where, and the one install logic (`launcher/install.*`) that Setup and the launcher share. |
| [`dist/`](dist/README.txt) | Setup: `DeathStrandingCoop-Setup.exe` (payload with SHA-256 check), its PowerShell window and verbs, the player README. |
| [`tools/`](install/README.md#release) | `release.sh` (zip, single-file Setup, checksums, `coop-mod.json`, VERSIONINFO, notes) and its checks. |
| [`redirect/`](redirect/README.md) | The loader proxy that isolates each test copy: saves, loaded modules, the single-instance guard and network access. |
| [`vendor/`](vendor/monocypher/README.md) | Monocypher (CC0 or BSD-2-Clause), unchanged: Ed25519 and SHA-512 for the players' keys (`world/identity.h`). |

## 🛠️ Build and test

Needs `g++`, the MinGW-w64 cross-compiler, Python 3 and a checkout of
[coop-anything](https://github.com/cranebeaker/coop-anything) for its fuzzer (`HUB=<path-to-playbook-checkout>`); no game:

```sh
make test     # every game-free test and mutant
make all      # Windows binaries (proxy, mod, probes)
tools/release.sh v0.1.0-alpha.1   # the release into local/release/ (builds everything; publishes nothing)
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

When a session goes wrong, the launcher's About tab has **Send diagnostics** (also
`deathstranding-coop.exe --diagnostics-zip`): a redacted zip on the Desktop containing recent logs, settings, system
and network checks, and a file manifest. Nothing is uploaded. `--diagnostics` still produces the bounded text report
([launcher/README.md](launcher/README.md), [diagnostics reader](tools/README.md)).

## 📜 Rules this project keeps

- Your own legitimately owned copy only; nothing that bypasses Steam ownership or any protection.
- Modded copies never talk to the publisher's online service (see `redirect/network_policy.h`). A private server
  that the players run themselves, reached over loopback or a listed partner address, is allowed (since 2026-11-11):
  `world/` is that server.
- Development and tests use test copies, never your real saves. Players install the release with Setup into their
  own game folder (one added file, removed by Uninstall); co-op saves stay separate from the ordinary ones.
- No game files in this repository. Reverse-engineering notes describe addresses and layouts, not game content.

Made with the [coop-anything](https://github.com/cranebeaker/coop-anything) playbook.

## ⚖️ License

[MIT](LICENSE). Death Stranding is a trademark of its owners; this project is not affiliated with or endorsed by
them.
