# install: put the co-op package into a game folder, and take it out again

`coop_install.py` (standard library only) installs, uninstalls and verifies the package in a game folder. It never
runs the game and its tests use temporary fake game folders only. It follows the co-op mod standard (rules J1 to J10).

```sh
python3 coop_install.py status    <game folder> [--json] [--package DIR]
python3 coop_install.py verify    <game folder>
python3 coop_install.py install   <game folder> --yes [--data-dir DIR]
python3 coop_install.py uninstall <game folder> --yes
```

Exit codes: 0 ok, 1 failed, 2 usage, 3 unsupported game build, 4 game running.

## What goes where

| File | Where | Why |
|---|---|---|
| `avrt.dll` (the proxy) | game folder, next to `ds.exe` | `ds.exe` imports `AVRT.dll`, the game does not ship it, so adding it replaces nothing. Steam's file verify and updates leave it alone. |
| `coop-mod.json` (manifest) | game folder | read by Setup, updater and diagnostics |
| `coop-mod.receipt.json` | game folder | what Setup installed, with hashes; written at install time only |
| `redirect-data/mod/mod.dll` | the co-op data folder | where the proxy looks for the mod |

Install only adds files. If `avrt.dll` (any letter case) or another destination already exists and is not ours,
install stops with a conflict and changes nothing. Uninstall removes exactly the files the receipt lists whose
content is unchanged, then the folders install created (only when empty). Afterwards the game folder is
byte-for-byte what it was. A file the player changed is kept and reported (exit 1); the receipt keeps listing it.

States (`status`): `not-a-game-folder`, `unsupported-build`, `not-installed`, `up-to-date`, `update-available`,
`needs-repair` (a listed file is missing or changed), `conflict`, `installed-by-mod-manager` (files match the release
but there is no receipt; hard-linked files read as Vortex). Install of a mod-manager install, or of an up-to-date
one, changes nothing; install in `update-available` and `needs-repair` replaces only files the receipt lists.
An install that fails part-way undoes itself.

## Where the co-op saves go

The proxy redirects every save, settings and profile location of the game into the co-op data folder
(`<data folder>/redirect-data/{local,roaming,documents,savedgames,profile}`), so the player's own saves are neither
read nor written. The data folder is `%LOCALAPPDATA%\DeathStrandingCoop` by default (`user_data` in the manifest).
Uninstall removes the mod file only and keeps `redirect-data` and everything in it.

## Package

`make_package.py --out DIR --version V --proxy redirect/build/avrt.dll --mod mod/build/mod.dll` writes the
drop-in folder with its `coop-mod.json` (schema 1: `id`, `version`, `game` with the supported `ds.exe` build hashes,
`proxy`, `proton.dll_overrides`, `files` with SHA-256 and size, `user_data`). Entries with `"root": "data"` go to the
data folder.

## Tests

`make -C install test`: 17 behaviour tests (install adds only, shipped files byte-identical, uninstall restores the
folder byte-for-byte, conflict, case variants, unsupported build, running game, rollback, update, repair, mod-manager
detection, CLI exit codes) and 8 mutants that must fail them (uninstall leaving a file or folder, install
overwriting, no rollback, status ignoring content, build not checked, running game ignored, uninstall deleting a
changed file).

## What a Setup program still needs (not built)

- The proxy refuses to start (exit 125) unless its environment names the data folder and the real save folders
  (`COOP_REDIRECT_INSTANCE`, `COOP_REAL_*`, `COOP_MODULE_INSTANCE`, `COOP_REDIRECT_STEAM`). Started from Steam, a
  game with the proxy installed would therefore close at launch. The launcher must set them, or a release build of
  the proxy needs a defined behaviour without them (pass through unchanged, co-op off) and defaults derived from the
  known folders. Deliberately not changed here: the lab build must keep failing closed so a missing variable can
  never run against real saves.
- Chain-loading a foreign `avrt.dll` (J3) is not implemented; install reports a conflict instead.
- A native Setup exe: window, game discovery in Steam libraries, desktop shortcut, firewall rule, and the real
  "game running" check (this core asks the OS to open `ds.exe` for update; a Windows test with a running image is not done).
