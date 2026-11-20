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
- **Vehicles together**: other players' trucks and bikes are drawn under them; J next to one being driven rides along
  on it. Their floating carriers are drawn too, and J rides on one. **Towing**: J while driving hooks your nearest
  floating carrier (within 8 m) to the vehicle, J again lets it go.
- **The world's enemies** (rule `enemies`): a MULE one player takes down is down for the others, camps alarmed by one
  are alarmed for all, gazers destroyed are gone for everyone; **timefall** where one player has it falls for the
  others near.
- **Building together**: upgrading another player's structure raises it for everyone; **roads**: what the others put
  into a paver counts in full (the game alone took only a share).
- **Orders together**: players within 30 m who take the same standard order at one terminal deliver it as a party.
- **Pings and likes**: B pings where you stand for the players near (on their HUD for 30 s); L gives a like to the
  nearest player within 25 m.
- **Far players on the HUD**: players too far away to be drawn are markers on the HUD.
- **Many players**: Settings, "other players drawn" (0 to 32, 15 by default); bodies beyond 30 m drop their shadow and
  cloth simulation, and bodies no longer slow the game the way they did (32 bodies within 12 m: 59 to 78 fps where it
  was 43, on the test PC).
- A server keeps up to 65536 players' accounts and takes 1024 at once.

Fixed:

- **Graphics settings** were lost at every start (the game refused its own settings file over a value it wrote itself).
- **Discord's overlay and clips** (and other software that loads itself into games) closed the game at start; it is now
  recorded and the game goes on.
- The Odradek arm that floated near you when another player arrived.

## Verified

What has run, and how; the details and dates are in [STATUS.md](../STATUS.md). Live results are from two copies of the
game on one PC unless a line says otherwise.

- Game-free: the mod's, server's and launcher's tests with their mutants (every mutant must fail).
- Live, two games on one server (2026-11-19 and 2026-11-20): others' builds at once, only the server's world, cargo
  set down, free build, the world's enemies (MULEs, camps), timefall, structure levels, roads, vehicles shown and
  riding along, floating carriers shown and ridden, towing, orders together, pings and likes, far players on the HUD,
  chat, the map, the server page.
- Live on the release code (2026-11-20): pings and likes (the like arrives, the ping is on the HUD), both games
  started from the configuration the launcher writes for a group (host and joiner, each shows the other), 32 bodies
  near with the new defaults.

## Not verified

- Two PCs over the internet with this build (the two games ran on one PC).
- More than two real games at once (more players were stand-ins).
- Gazers destroyed and orders together with the launcher's own join (each ran with a test configuration).

## Known issues

- Two players picking up the same crate within the same three seconds each keep one.
- On a server whose world is its own, cargo you set down there goes into your save (structures do not).
- What another player builds skips the game's spacing checks; structures may overlap.
- Materials put into another player's structure short of a level are not shared (a level reached is).
- Riding along: a porter more than 20 m from the vehicle cannot get on; the place on a moving vehicle can lag.
- Boss Battle replays together are not in this release (built, never run with a game).
- Only Steam build 13300582. Another mod's `avrt.dll` in the game folder blocks the install.
- Traffic is not encrypted: play on a LAN, a VPN, or a server you trust.
- Updating is manual: run the new Setup.

## Reporting

F2 in the game, then About > Send diagnostics, then the
[bug form](https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose) with the zip attached.
The zip hides your Windows and computer names, addresses, Steam ids and account numbers, and every secret; open it
and look before you send it if you like.
