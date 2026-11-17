# install: what goes where, and the one install logic

Setup (`dist/`) and the launcher's own window install the mod the same way, because both run the same code:
[`launcher/install.h`](../launcher/install.h) / [`install.cpp`](../launcher/install.cpp). It is portable C++17 on
`std::filesystem` (no Windows calls), so it builds and is tested on Linux. It follows the co-op mod standard
(rules G1, G2, J1 to J10).

**The choice (2026-11-17): Setup calls the launcher's install.** Setup's window (`dist/installer-gui.ps1`) and its
verbs (`dist/deathstranding-coop-setup.ps1`) run `deathstranding-coop.exe --setup ...` from the package and draw its
answer; they decide nothing about files. The launcher's earlier `setupInstall` became this core, and the Python
installer (`coop_install.py`, `make_package.py`) is retired: one install logic, one test, one set of mutants.
PowerShell keeps only what is Windows-only and outside the files: the desktop shortcut, the firewall rule and the
invite-link registration (which is the launcher's own `--register-protocol`).

## What goes where

| File | Where | Why |
|---|---|---|
| `avrt.dll` (the proxy) | the game folder, beside `ds.exe` | `ds.exe` imports `AVRT.dll` and the game does not ship one, so adding it replaces nothing. Steam's verify and updates leave it alone. Without the launcher's start ticket it only forwards to the system's `avrt.dll` (co-op off). |
| `mod.dll` | `%LOCALAPPDATA%\DeathStrandingCoop\redirect-data\mod\` | where the proxy loads the mod from |
| the launcher and its package | `%LOCALAPPDATA%\DeathStrandingCoop\app\` | `deathstranding-coop.exe`, `coop-world.exe`, a copy of `avrt.dll` and `redirect-data\mod\mod.dll` (the package the installed launcher compares the game against), `coop-mod.json`, README, licences. Setup's install only; the launcher's own window leaves its folder where the player extracted it. |
| `<file>.coop-receipt.json` | beside every installed file | `{"schema":1,"id","version","installed","pending"}`: the SHA-256 Setup put there and, while a replacement is under way, the one it is putting there |

`coop-mod.json` ([`tools/mod_manifest.py`](../tools/mod_manifest.py), standard J5) lists every file with its root
(`game` or `data`), path, SHA-256 and size; a `data` path starting `app/` is the launcher's folder and comes from the
package file without `app/`. The supported `ds.exe` build is the pinned hash in `mod/discovery_options.h`, the same
one the launcher and the mod check: one source of truth. Co-op saves, settings (`mod.json`, `launcher.ini`), player
keys and logs live in the same data folder and are never part of an install or an uninstall.

## States and the one button

| State (`status --json`) | Button | When |
|---|---|---|
| `not-a-game-folder` | Needs attention | no `ds.exe` |
| `unsupported-build` | Needs attention | `ds.exe` is not build 13300582 (exit 3) |
| `not-installed` | Install | none of the files is there |
| `update-available` | Update | a file is what an earlier Setup installed (its receipt says so), an older co-op proxy without a receipt (it carries the proxy's marker text), or a receipt names another version |
| `needs-repair` | Repair | a file is missing, or the right files are there without receipts (copied by hand: Repair writes the receipts) |
| `installed-by-mod-manager` | Up to date | the right files without receipts, hard-linked (a mod manager's deployment): Setup leaves them alone |
| `conflict` | Needs attention | another program's file with our name (another mod's `avrt.dll`), or a file changed since Setup installed it: never replaced, never removed |
| `up-to-date` | Up to date (then Play) | every file is the package's, with a receipt of this version |

Install is all or nothing: the co-op data first, the proxy last; before each file its receipt is written with the
`pending` hash, the file is staged, checked and moved into place, then the receipt is completed. A failure part-way
puts every file and receipt back. A file an earlier version installed that this one no longer ships is removed when
unchanged. Install refuses a running game (exit 4), another build (exit 3) and a damaged package.

Uninstall removes every file whose receipt still matches it (whatever version installed it) and our files without a
receipt; it keeps a file changed since install (listed, exit 1) and another program's file (listed); it removes the
launcher's folder when empty and nothing else of the data folder. Setup's uninstall also removes the shortcut, the
invite links and the firewall rules.

**Chain-loading another `avrt.dll` (J3) is not supported:** `redirect/` has no chain loader (no `avrt.chain.dll`), so
`coop-mod.json` says `"chain_file": null` and Setup refuses with "The game folder already has an avrt.dll that is not
from the co-op mod (another mod's file?). The co-op proxy cannot load another avrt.dll after itself, so Setup changes
nothing. Remove or rename that file (or uninstall the other mod), then run Setup again."

## Finding the game

`findGames` reads every Steam root the registry names (HKCU `SteamPath`, HKLM `InstallPath` in both views), each
root's `steamapps/libraryfolders.vdf` (current and old format; `config/libraryfolders.vdf` too), and in every library
`steamapps/appmanifest_1850570.acf`, whose `installdir` names the folder under `steamapps/common`; Steam's uninstall
entry for app 1850570 is added. Only folders with `ds.exe` count, each once.

## Verbs (J9)

```
deathstranding-coop.exe --setup status [--json] [<game folder>]     (no folder: every install found)
deathstranding-coop.exe --setup install <game folder> --yes
deathstranding-coop.exe --setup uninstall <game folder> --yes
```

`DeathStrandingCoop-Setup.exe status|install|uninstall ...` passes the same words on (through
`deathstranding-coop-setup.ps1`, which adds the options' removal to uninstall). Exit codes: 0 done, 1 failed, 2 usage,
3 unsupported game build, 4 the game is running. `--data-dir <folder>` replaces the data folder, for tests only.

## Tests

`make -C launcher test` runs [`install_test.cpp`](../launcher/install_test.cpp) in fake game, package and data
folders (states and the button, receipts beside every file, install leaving the game byte-for-byte as it was apart from
the proxy, update with a dropped file, repair, an interrupted update, rollback after a failure part-way, a damaged
package, a linked folder, a manifest path that escapes, uninstall keeping changed and foreign files and the co-op data,
by-hand and mod-manager installs, a file that cannot be deleted, the launcher's own layout, `libraryfolders.vdf` in both formats with escapes,
`appmanifest` and duplicate games) and 17 mutants that must fail it (`launcher/test_mutants.py`, `MUTANT_INSTALL_*`).
`make -C dist test` covers Setup's own core and the release tools ([`dist/test_dist.py`](../dist/test_dist.py),
[`tools/test_release.py`](../tools/test_release.py)).

## Release

`tools/release.sh <version>` builds everything (`make all` with the version in every VERSIONINFO and the launcher's
About), stages the package (`tools/package.py`), writes `coop-mod.json`, builds the zip's payload-less Setup and the
single-file Setup with the package embedded (`tools/payload.py`; the exe checks the payload's SHA-256 before it
unpacks it), the `.sha256` files, `NEXUS-<version>.txt` and `PUBLISH-<version>.txt` (the `gh release create
--prerelease` command, never run), and finishes with `tools/check_release_package.py`. Everything goes to
`local/release/` (ignored by git). `SIGN_CMD` signs every exe and dll before it is hashed and embedded.

On Windows, [`tools/test_setup_windows.ps1`](../tools/test_setup_windows.ps1) (optional, game-free, cleans up after
itself) checks a built release: the single-file Setup's `/extract`, the launcher's verbs on fake folders and the same
verbs through both Setup forms (the refusals: no `ds.exe`, another build, no `--yes`, an unknown verb). It found the
verb script dropping the verb (fixed). Not verified yet: the Setup window, the elevated firewall step, the shortcut and
invite links, and an install into (and uninstall from) a real Steam library with build 13300582.
