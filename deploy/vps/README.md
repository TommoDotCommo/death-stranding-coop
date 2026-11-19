# coop-world on a rented server

Outside testers play together through one `coop-world` (`../../world/README.md`) on a small rented Linux server, so
nobody's game connects to anybody's home address. This folder builds it, installs it with one command, administers
it over SSH and checks all of that without a server.

Built and checked without a server or a game (2026-11-17, `make -C deploy/vps check`). First real deployment
(2026-11-18): a rented Ubuntu 24.04 server (1 GB), `deploy.sh` with `--fail2ban yes` in one run; two PCs joined with
the printed invite through the launcher, their games met through the relay over the internet (24 ms round trip) and an
F2 mark reached the server's log. Not yet: players from other homes and routers.

## What you need

- A rented server: Debian 12 or Ubuntu 22.04/24.04, x86_64, 1 GB of memory is plenty, a public IPv4 address, SSH
  with a key for `root` or for a user who may `sudo`.
- This PC: Linux or WSL with `g++` (and the static C library, `libc6-dev` on Debian/Ubuntu), `make`, `ssh`, `scp`,
  `python3` for the checks.

## First deployment

```
deploy/vps/deploy.sh root@203.0.113.7
```

(203.0.113.7 stands for your server's address; a name from `~/.ssh/config` works too, with `--ip ADDRESS` when it
does not resolve to the public address.) It:

1. builds `coop-world` for Linux (`make -C world linux`: one static x86_64 executable, nothing needed from the
   server's libraries) and the invite tool (`build/vps-invite`);
2. copies the executable, `install.sh` and `coop-admin.sh` to a fresh temporary folder on the server;
3. runs `install.sh` there as root (below), then removes the temporary folder;
4. prints the **group invite** (`DSG-...`) with the server's address, port 49100 and the group's key.

Send the invite to the testers privately: whoever has it can join. Running `deploy.sh` again updates the executable and
keeps the world, the group's key and the owner's key. Options go to `install.sh`: `--port 49100..49114`, `--name TEXT`
(the world's display name, 12 letters at most, default `Alpha`), `--ssh-port N`, `--fail2ban yes`.

### What install.sh does on the server

Idempotent: every run leaves the same state, and keeps what was made before.

| What | How |
|---|---|
| A system user `coopworld`, no shell, no home | `useradd --system` |
| `/opt/coop-world/coop-world` (root's, 0755), `/opt/coop-world/coop-admin` (root only) | replaced atomically |
| `/var/lib/coop-world/` (0700, `coopworld`'s): the world file | the only place the server writes |
| `/etc/coop-world/owner.key` (root only, 0600): the owner's key, made once **on the server** | `coop-world keygen`; never copied anywhere |
| `/etc/coop-world/coop-world.conf` (root and the service): port, group key (made once, random 1..4294967295), name, the owner's public key | every value checked before it reaches the command line |
| A systemd unit: `Restart=always`, `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp`, `PrivateDevices`, `ReadWritePaths=/var/lib/coop-world` only, no capabilities, IPv4 and Unix sockets only, `@system-service` system calls, `MemoryDenyWriteExecute`, `MemoryMax=512M`, `TasksMax=256`, `LimitNOFILE=1024` | `install.sh --print-unit` shows it |
| Firewall (ufw): incoming denied except SSH (the port sshd listens on) and the server's port, TCP and UDP | SSH is allowed before the firewall is turned on |
| Automatic security updates | `unattended-upgrades` (no automatic reboot) |
| Logs: journald at most 200 MB, kept 14 days | the log holds players' addresses (below) |
| fail2ban for sshd, with `--fail2ban yes` | 5 failures, banned 1 hour |

The server runs with:

```
coop-world --port 49100 --bind 0.0.0.0 --data /var/lib/coop-world/world.json --prefix wKEY --relay-port 49100 --relay-key wKEY
           --policy public --policy-set identity=keys --owner OWNERKEY --name Alpha
           --max-connections 64 --ip-connections 8 --ip-rate 20 --ip-burst 200 --ip-strikes 30 --ip-block 900
```

### Which rules, and why

**Public rules with identity keys** (`../../world/README.md`, "Server policy" and "Identity"), and `identity=keys` is
set again at every start, so it cannot be switched off by a call. It is the strictest mode the launcher joins today:
the launcher's group join reads the rules, takes a sign-in ticket and a relay token with its player key's signed
calls, and writes them into the game's config (`../../launcher/README.md`, "A group on one server"). Under it:

- nobody signs in, builds, withdraws or says HELLO on the relay in another player's name; a sign-in by a name in
  the address, or by a Steam ticket, is refused, and the group's key alone is not taken by the relay;
- no Steam id is used, stored or shown by the server (the player is his launcher's key);
- the owner can kick and ban (below), which means something only when players cannot change who they are.

The Public preset's other settings: only members' builds are shared, and those wait for the owner's review; at most
200 objects per player; a withdrawn build stays as a remnant; any number of players (65536 accounts, 1024 at once); each player is shown at most 16 others near
him. For a test where everything should be shared without review:

```
deploy/vps/admin.sh root@203.0.113.7 policy-set build=everyone accept_offers=auto
```

The port is 49100 (to 49114) because the launcher's group invite only carries those ports. TCP and UDP use the same
number.

## Administration

`admin.sh HOST COMMAND` runs `coop-admin` as root on the server over SSH. The owner's actions are signed calls to the
server on its own loopback (`coop-world call` with `/etc/coop-world/owner.key`): nothing extra listens on the network,
and the owner's key never leaves the server. (The launcher's Server owner window only reaches a server its own PC
hosts; this server's owner is the key on the server.)

| Command | What it does |
|---|---|
| `status` | the service, the firewall, how many players |
| `logs [N]`, `follow` | the last N log lines (default 200), or follow them |
| `restart` | restarts the service (players' games reconnect; their passes are kept) |
| `invite [--ip A.B.C.D]` | prints the group invite again |
| `rotate-key [--ip A.B.C.D]` | a new group key and its invite: the old invite stops working at once. Players keep their keys, builds and choices; everyone needs the new invite |
| `players` | every player: id (`zygo_...`), short key, counts, member, banned |
| `kick ID` | ends his game's session, sign-in ticket, relay token and notice token at once; he is off the relay at the next tick. He can come back by starting again from his launcher |
| `ban ID`, `unban ID` | a kick, and while it holds: no sign-in, no ticket, no relay token, no launcher call but reading the rules (403). Kept in the world file |
| `remove ID` | removes everything he built here (not a ban: combine with `ban`) |
| `member ID yes\|no`, `review`, `accept OBJECT`, `reject OBJECT`, `accept-player ID` | the Public rules' members and review |
| `policy`, `policy-set KEY=VALUE...` | the rules; `identity` cannot be changed here |
| `block-ip A.B.C.D`, `unblock-ip A.B.C.D` | a firewall rule for one address (both TCP and UDP, every port) |
| `backup FILE` | copies the world file to FILE on this PC. It holds the players' keys and passes: keep it private |

A ban follows a key. Someone banned can make a new key (a new player) as long as he has the invite: then
`rotate-key` and send the new invite to everyone else.

## Safety review

The testers are strangers to each other, and one is under 18. What follows is what the server and the mod let
travel, read from the code (`../../world/`, `../../core/relay.h`, `../../launcher/core.cpp`) and checked where the
"Checks" say so.

### What identifies a player, and who sees it

| What | Other players | The server (its owner) | Anyone on the network path |
|---|---|---|---|
| **Nickname** (launcher Settings; default "Porter"; 1–32 characters the player chooses) | over his head and in the relay's roster, to the players near him | yes, and in the log | yes (plain UDP) |
| **Builder name** of his structures | the name his game signs in with: the game sends an empty one (observed), so "Porter" | yes | yes |
| **Player id** (`zygo_...`) and **relay id** (both made from his key) | with his structures and in the relay's datagrams | yes | yes |
| **Public key** (`ed25519-...`, his launcher's key) | the game's `lookup` answer carries it with his structures; friends exchange its short form by hand | yes | yes |
| **Position, area, animation, gear, carried cargo** | the players near him (at most 16 shown each) | yes, in memory | yes |
| **Structures, likes** | every player of the world (Public: after the owner's review) | yes, in the world file | yes |
| **IP address** | **never**: the relay passes on the server's own record of the sender (its id), and its datagrams have no address field; the invite carries the server's address only | yes: the connection, and the log line of each relay join (kept 14 days) | yes |
| **Steam id** | never under identity keys: nothing is stored or answered by Steam id (a `reverseLookup` of Steam ids finds no player) | **received, not kept**: the game's sign-in still sends its Steam session ticket (which holds the Steam id) in the address, and its friends lookups name the Steam ids of the player's Steam friends; the server does not write them (no `--trace`; the smoke checks the world file and the log) | **yes** (see "Gaps") |
| **Windows user name, real name, e-mail, location** | never sent by the mod or the launcher | never | never |

### Relayed, never direct

In group play (`Join group` with a `DSG-` invite) the game's config has no partner address: the mod talks to the
server only, and the launcher sets the proxy's network policy to that one address (`networkPeers`, `enforce`), so the
game cannot open a connection to another player even if told to. Players near each other exchange presence only
through the relay, which stamps every datagram with the server's record of the sender. Nothing in the group path
puts a player's address where another player can read it: no leak was found in that path (code read, 2026-11-17).

**The two-player lobby is direct.** "Host a game" / "Join a friend" with a `DS-` (not `DSG-`) invite connects the two
games to each other: each sees the other's address, and the host's invite holds his LAN and VPN addresses. Testers
must only use the group invite of this server (`../../docs/TESTERS.md` says so). The launcher also answers lobby
probes on its own LAN (invite-only replies hide the host's name); that never reaches the internet.

### What a stranger can and cannot do

Without the invite: reach the two ports. Every HTTP path but the right `w<key>` prefix answers 404; 30 such answers in a
minute block the address for 15 minutes (the key is one of 4294967295, so guessing it from one address takes years;
many addresses together are the residual risk: `rotate-key`). A relay datagram without a good relay token is never
answered.

With the invite (a tester, or whoever a tester passed it to):

- **can** join under a key of his own (the world keeps up to 65536 accounts), choose any nickname, walk up to other players
  and be seen by them, build (shared after the owner's review), like structures;
- **cannot** act or appear as another player, read another player's address, reach a player who is not near him,
  message anyone while the rules keep text chat off (the public preset does; with `policy-set chat=on` everyone on the
  server reads what anyone types, and the log keeps it), or stay once the owner bans his key.

The owner (whoever holds SSH to the server) sees every player's address and nickname in the log. That is said in
`../../docs/TESTERS.md`.

### Limits per address

On top of the server's own caps (65536 accounts, 1024 players at once on the relay, 4000 objects a player, 4 MiB a request, relay rates): per address at most
8 connections at once and 20 requests a second (200 saved up), 64 connections in all, a request must arrive within
30 s (a sender who drips bytes keeps no connection), and the 404 block above. Loopback is exempt (the admin's calls).
(`../../world/README.md`, "Limits per address".)

### Gaps (not fixed)

- **No TLS.** HTTP and the relay's UDP are plain. On the path between a tester and the server (his Wi-Fi, his
  provider, the hosting company) someone can read his nickname, position, key and tokens, and the **Steam session
  ticket** the game sends at sign-in, which holds his Steam id, and use a sign-in ticket or relay token within its
  short lifetime. The mitigation chosen: everything goes through the server (no peer addresses travel at all),
  identity keys (a token read on the path is short-lived and for one player), no Steam id stored or answered.
  Encryption needs the game's traffic wrapped by the mod or the proxy (the game speaks plain HTTP to the address
  the mod gives it): a follow-up, not done.
- **Accounts fill the world.** Each new key becomes one of the world's 65536 accounts on its first signed call, and `remove` and
  `ban` do not free its place. Someone with the invite and many keys can fill the world. Mitigation: `ban` the keys,
  `rotate-key`, send the new invite only to the testers.
- **Nicknames are free text.** Nothing filters them; the owner bans a key that uses an offensive one.
- **Per address, not per key.** A flood of signed calls from many addresses is bounded only by the server's caps.
- **Unauthenticated relay datagrams** cost the relay a parse each; there is no per-address limit on UDP.
- **The server log** holds addresses and nicknames for 14 days on the server.
- **Never run this server keyless.** On a Friends or Solo server (identity none) a player's account is his Steam id,
  and the game's `lookup` answer gives it to every game that looks up a builder (`boundAccount`, `world/world.cpp`):
  other players' games then hold his Steam id. Under identity keys that field is his launcher's public key. The unit
  sets `identity=keys` at every start and `coop-admin policy-set` refuses to change it. (Leaving the field out on
  keyless servers needs a live check that the game takes such an answer: not done.)

## Checks

```
make -C deploy/vps check
```

Builds the static Linux `coop-world`, `coop-swarm` and the invite tool, checks that the four scripts parse (and runs
`shellcheck` on them when it is installed), then `test_vps.py`: the Linux `coop-world` on loopback with exactly the
command line of the unit `install.sh` writes (only the address, the port and the data path changed), and what a
tester's launcher, his game and the owner do through it (the list is at the top of `test_vps.py`). The server-side
protections it relies on have their own tests and mutants in `make -C world test` ("Limits per address", "Kick and
ban").

## Not verified

- Never run on a rented server: `install.sh` (packages, user, ufw, journald, unattended-upgrades, fail2ban, the unit
  under a real systemd), `deploy.sh` and `admin.sh` over a real SSH connection. The unit passes `systemd-analyze
  verify` on this PC; whether every hardening setting holds on the server's systemd is unknown until it runs.
- No launcher (Windows) has joined a server by an invite with an internet route, and no game has played through it.
- `shellcheck` was not installed on the PC the scripts were written on.
