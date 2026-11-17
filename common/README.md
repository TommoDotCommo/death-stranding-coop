# Shared diagnostics

`diag_log.h` is the header-only UTF-8 logger used by the mod, proxy, launcher
and world server. Windows runs write to
`%LOCALAPPDATA%\DeathStrandingCoop\logs\`; Linux servers use
`$XDG_STATE_HOME/DeathStrandingCoop/logs`, or `~/.local/state/DeathStrandingCoop/logs`.

Names are `<UTC yyyymmdd-hhmmss>_<component>_p<pid>.log`. Lines are
`<sequence> <UTC milliseconds> <chain hex8> <text>`. The chain starts at
2166136261 and continues FNV-1a/32 over `<sequence> <UTC milliseconds> <text>`
without a newline. This detects accidental edits, not forgery. Text stays raw;
only embedded line breaks become spaces. Redaction belongs to diagnostics export.

`max_log_mb` defaults to 200 and accepts 5–2000. Startup prunes the oldest
recognized files first. Runs with `.crash` markers and active log handles are
kept, so retained crashes may exceed the folder budget. A running log stops
accepting ordinary lines at its own cap, reserving space for the clean exit.
The mod and player proxy read the key from `mod.json`; the launcher reads
`[launcher] max_log_mb` in its settings; the server accepts `--max-log-mb`.

F2 writes `BUGMARK <number>` and `<stem>.flight-<number>.txt` from the previous
60 seconds of presence, link and state observations. The memory ring has an
8 MiB safety bound, and each flight file is capped too. A held F2 fires once.
Reliable event type 24 carries marks to a connected partner; receivers do not
echo them. Both players need the same build identity. Group broadcast mode
does not carry the partner event stream.

Player crashes create `<stem>.crash` and a small `MiniDumpNormal` `.dmp`.
Only unhandled exceptions create crash markers; previous filters still run.
The dump thread has a two-second wait bound. Fail-fast and forced termination
can bypass unhandled filters. Linux has logging but no Windows crash recorder.

The proxy hosts mod diagnostic IO through fixed-purpose callbacks so save
redirection cannot move the logs or widen the game's file permissions.

Legacy mod logs, presence JSONL and proxy audit/crash files are still written
at their original locations for existing tools. The player proxy additionally
writes UTF-8 here; its legacy audit remains UTF-16LE.

Run `make -C common test` (also part of `make test`) for portable logging,
flight, pruning and cap checks and parallel protection mutants.
