# Death Stranding Co-op v0.1.1-alpha.1 (closed alpha)

The second alpha for outside testers: a shared world through the server. Testers join the organiser's test server with
a group invite (`DSG-...`) sent to them privately; how to install, join and report is in [docs/TESTERS.md](TESTERS.md).
Install it over `v0.1.0-alpha.1` with the new Setup (Update).

You need DEATH STRANDING DIRECTOR'S CUT on Steam, build 13300582, and 64-bit Windows 10 or 11.

## Downloads

| File | What |
|---|---|
| `DeathStrandingCoop-Setup.exe` (+ `.sha256`) | The single-file Setup: start here. Not code-signed: SmartScreen asks; choose More info, Run anyway. |
| `deathstranding-coop-v0.1.1-alpha.1.zip` (+ `.sha256`) | The same files for a manual install (start `deathstranding-coop.exe` from the extracted folder). |
| `deathstranding-coop-test-saves-0.1.1-alpha.1.zip` | Optional test saves (Episode 1 start, Episode 3 at Lake Knot, a 100% set), signed for you when you pick one: put the zip beside the launcher, then Saves, Use this test save. |

## What is new

In group play on a server (every line is a server rule the server's owner can change; the launcher shows them):

- **Others' builds appear at once**: what another player builds while you play stands in your world within seconds,
  any kind (generators too), and goes when its builder takes it down. Before, it appeared only at your next load of
  that area, and some kinds never.
- **Only the server's world** (public servers): the structures in your save are hidden while you play there and are
  all back when you play without the server; nothing of the server's goes into your save.
- **Cargo set down for the others** (rule `cargo`): cargo you put down lies in the other players' worlds until someone
  takes it; the first to take it has it.
- **Free build** (rule `free_build`, off unless the server says so): the game's rules on where a structure may stand
  are switched off.
- **Text chat** (Y), **other players on the game's map**, **the server page** (P: the server, who is online, its
  rules), **hold F1** for the co-op keys, **F3** for test mode's switches (blood, stamina and the porter's battery now
  really kept full).
- **Other players' gear and carried cargo** drawn on their bodies.
- A server keeps up to 65536 players' accounts and takes 1024 at once.

Fixed:

- **Graphics settings** were lost at every start (the game refused its own settings file over a value it wrote itself).
- **Discord's overlay and clips** (and other software that loads itself into games) closed the game at start; it is now
  recorded and the game goes on.
- The Odradek arm that floated near you when another player arrived.

## Verified

What has run, and how; the details and dates are in [STATUS.md](../STATUS.md). Live results are from two copies of the
game on one PC unless a line says otherwise.

- (filled in at release)

## Not verified

- (filled in at release)

## Known issues

- Two players picking up the same crate within the same three seconds each keep one.
- On a server whose world is its own, cargo you set down there goes into your save (structures do not).
- What another player builds skips the game's spacing checks; structures may overlap.
- Upgrades and materials put into another player's structure are not shared through the server yet; nor are vehicles,
  roads, BTs, rain or orders (those are two-player co-op only).
- Only Steam build 13300582. Another mod's `avrt.dll` in the game folder blocks the install.
- Traffic is not encrypted: play on a LAN, a VPN, or a server you trust.
- Updating is manual: run the new Setup.

## Reporting

F2 in the game, then About > Send diagnostics, then the
[bug form](https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose) with the zip attached.
The zip hides your Windows and computer names, addresses, Steam ids and account numbers, and every secret; open it
and look before you send it if you like.
