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

## What is in it

- Setup: finds the game in every Steam library, checks the build, one button (Install / Update / Repair / Up to date /
  Needs attention), Uninstall, a desktop shortcut, invite links and an opt-in Windows Firewall rule. One file goes into
  the game folder (`avrt.dll`); the rest, and co-op's own saves, into `%LOCALAPPDATA%\DeathStrandingCoop`.
- The co-op launcher (Play, Saves, Settings, About), the group server `coop-world`, the proxy and the mod at the state
  of [STATUS.md](../STATUS.md) on the release commit.
- Bug reports: F2 in the game and the launcher's diagnostics (see "Not verified").

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
  (`launcher/install_test.cpp`, 16 mutants), Setup's payload check and container (`dist/test_dist.py`, 4 mutants),
  `tools/release.sh` end to end on stand-in binaries with six tampered releases refused (`tools/test_release.py`).
  The PowerShell scripts parse in Windows PowerShell 5.1.

## Not verified

- **Setup on Windows:** the window, the cards, the button, the firewall step with its administrator prompt, the
  shortcut and invite links, the verbs through `DeathStrandingCoop-Setup.exe`, SmartScreen's wording, an install into
  and an uninstall from a real Steam library. None of this has run on Windows before this alpha.
- **Two PCs** and anything across the internet, routers and firewalls (the README says not yet tried; cite the run
  here if one has happened by release time).
- **The diagnostics zip and F2 marker** described in the README are being built alongside this release; check that
  `About > Save diagnostics for a bug report` writes the zip before the notes go out.
- Structures from other players skip the game's spacing checks (`build_received`); they may overlap.
- Roads paved through the server: the game asks and is answered but takes nothing yet.

## Known issues

- Only Steam build 13300582. Other builds: co-op stays off.
- Another mod's `avrt.dll` in the game folder blocks the install (no chain-loading).
- Traffic is not encrypted: play on a LAN, a VPN, or a server you trust.
- Updating is manual: run the new Setup.

## Reporting

F2 in the game, then About > Save diagnostics for a bug report, then the
[bug form](https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose) with the zip attached.
