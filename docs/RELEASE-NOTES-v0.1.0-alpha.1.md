# Death Stranding Co-op v0.1.0-alpha.1 (draft, not published)

The first alpha for outside testers. **Draft:** the owner decides when it is tagged and published
(`local/release/PUBLISH-v0.1.0-alpha.1.txt`, written by `tools/release.sh`). Before publishing, every line under
"Not verified" must be re-read, and anything checked in the meantime moved up with its evidence.

You need DEATH STRANDING DIRECTOR'S CUT on Steam, build 13300582, and 64-bit Windows 10 or 11.

## Downloads

| File | What |
|---|---|
| `DeathStrandingCoop-Setup.exe` (+ `.sha256`) | The single-file Setup: start here. Not code-signed: SmartScreen asks; choose More info, Run anyway. |
| `deathstranding-coop-v0.1.0-alpha.1.zip` (+ `.sha256`) | The same files for a manual install (start `deathstranding-coop.exe` from the extracted folder). |
| `deathstranding-coop-test-saves-0.1.0-alpha.1.zip` | Optional test saves (Episode 1 start, Episode 3 at Lake Knot, a 100% set), signed for you when you pick one: put the zip beside the launcher, then Saves, Use this test save. |

## What is in it

- Setup: finds the game in every Steam library, checks the build, one button (Install / Update / Repair / Up to date /
  Needs attention), Uninstall, a desktop shortcut, invite links and an opt-in Windows Firewall rule. One file goes into
  the game folder (`avrt.dll`); the rest, and co-op's own saves, into `%LOCALAPPDATA%\DeathStrandingCoop`.
- The co-op launcher (Play, Saves, Settings, About), the group server `coop-world`, the proxy and the mod at the state
  of [STATUS.md](../STATUS.md) on the release commit.
- Bug reports: F2 in the game and the launcher's diagnostics (see "Not verified"). In group play F2 also marks the
  logs of the players near you and the server's log (tested without the game: the fake world through the real server).

## Verified

What has run, and how. Live results are from two copies of the game on one PC unless a line says otherwise; the
details and dates are in [STATUS.md](../STATUS.md).

- Seeing other players (body, gear, outfit, vehicle, name tag, map pin, pings), riding along, many players through the
  group server's relay (16 others in one game), visitors in a BT catcher fight: live, two copies on one PC.
- The private strand server: other players' ladders, cairns, bridges and rain shelters appearing, offering structures
  already in your save, the builder's name and likes: live, two copies on one PC.
- The game's connections to the publisher's online service refused while co-op runs; a co-op save kept apart from
  the ordinary saves: live, two copies.
- Setup and the release package, game-free (Linux): the install logic shared by Setup and the launcher
  (`launcher/install_test.cpp`, 17 mutants), Setup's payload check and container (`dist/test_dist.py`, 4 mutants),
  `tools/release.sh` end to end on stand-in binaries with six tampered releases refused (`tools/test_release.py`).
  The PowerShell scripts parse in Windows PowerShell 5.1. On Windows (`tools/test_setup_windows.ps1`, fake folders):
  the single-file Setup unpacks its verified payload, and `status`/`install` through both Setup forms answer with the
  right exit codes for no `ds.exe` (1), another build (3), no `--yes` (2) and an unknown verb (2), writing nothing.

### Live on two PCs, real Steam installs (2026-11-17/18, release candidate of this version)

Two PCs on separate Steam accounts, each with its own Steam copy, connected over Tailscale; everything below through
the player path (Setup or the launcher's install, the game started by Steam), not test copies:

- **Setup** on a clean Windows user: found the game in the Steam library, build check, Install (only `avrt.dll` and
  its receipt added to the game folder; the launcher, server and mod in `%LOCALAPPDATA%\DeathStrandingCoop\app` with
  receipts), desktop shortcut, then Play opened the launcher. The `install <folder> --yes` and `status --json` verbs on
  the other PC.
- **Solo**: Play solo started the game through Steam; the proxy took the launcher's ticket and started the mod; the
  publisher's service was refused.
- **Two players (lobby)**: host Open lobby, invite, the guest pasted it and joined, Ready, the host's save sent and
  re-signed for the guest, Start game started both games; link round trip 14-51 ms; each game showed the other's body
  with outfit, gear, carried cases and name tag (body position read from game memory = the partner's own position).
- **Group play through a server**: `coop-world` with this release's rules for testers (Public, player keys, limits
  per address) on a Linux server reached over Tailscale; both launchers joined by the group invite, showed the
  server's rules, took their tickets and relay tokens; both games signed in and saw each other through the relay.
- **Test saves**: `--use-test-save` and the Saves tab's list; Continue loaded the Episode 3 Lake Knot save.
- **Diagnostics zip** (`--diagnostics-zip`) written on a player PC; the Windows exporter fixture passes (redaction,
  manifest, log chain, size limit, old log folders, dumps). Fixed on the way: the Steam account number was not
  redacted, and one redaction rule was quadratic (minutes for a 1 MB log; now seconds).
- **Crash recorder**: the Windows fixture writes a small dump and marker; the four crash mutants fail.

## Not verified

- **Setup**: the administrator prompt of the firewall option for a user who is not already elevated (the option itself
  ran: ticking it added inbound rules for the game, the launcher and the server on Private and Public networks, and
  unticking took them away), the invite-link option, and SmartScreen's wording on a PC that downloaded the file from
  GitHub. Uninstall from the window ran (the game folder back as it was).
- **The internet**: every two-PC run so far went over Tailscale; a rented server on a public address and players behind
  ordinary home routers have not been tried (`deploy/vps` is checked on loopback only).
- **F2** (bug marker) in a real game, and an actual game crash producing a dump.
- Structures from other players skip the game's spacing checks (`build_received`); they may overlap.
- Roads paved through the server: the game asks and is answered but takes nothing yet.

## Known issues

- The first start of the game shows the publisher's telemetry question (Cancel is fine) and two notices, and the first
  load shows a few "New additional content found" notices: press OK through them.
- The game's graphics settings are per co-op profile: a weak PC should lower them once (Options, Graphics Settings).

- Only Steam build 13300582. Other builds: co-op stays off.
- Another mod's `avrt.dll` in the game folder blocks the install (no chain-loading).
- Traffic is not encrypted: play on a LAN, a VPN, or a server you trust.
- Updating is manual: run the new Setup.

## Reporting

F2 in the game, then About > Send diagnostics, then the
[bug form](https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose) with the zip attached.
The zip hides your Windows and computer names, addresses, Steam ids and account numbers, and every secret; open it
and look before you send it if you like.
