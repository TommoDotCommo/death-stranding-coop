# Diagnostics exporter

`diag.ps1` and `diag-rules.json` are embedded as launcher resources. About → **Send diagnostics** and
`deathstranding-coop.exe --diagnostics-zip` run that matching copy with Windows PowerShell hidden, save
`Desktop\DeathStrandingCoop-diagnostics-<UTC>.zip`, and select it in Explorer. Nothing is uploaded. PowerShell was
chosen to reuse the MDK2/Witness design and Windows' built-in hardware, WER, Defender and firewall queries without
adding a runtime dependency. `--diagnostics` still writes the existing bounded plain-text report.

The zip includes that report, masked launcher/mod configs, new `logs\` runs and legacy `redirect-data\` mod, proxy,
presence and crash files, `system.txt`, `network.txt`, and a hashed listing of mod files in the game folder. It copies
no game files, saves, player keys or world database. Missing or denied queries are recorded as unavailable or in
`manifest.json`. Collection reads active files with sharing enabled and skips reparse-point log files.

The default `-MaxLogMB 60` limits **input log and dump bytes together**, newest modification time first. A file that
does not fit is omitted, with a manifest note; even a crash dump cannot override this cap. UTF-16 legacy logs become
UTF-8. Names, profile paths, every IPv4 address, MACs, Steam IDs (`steam_<id>` too), emails, invite codes and secrets
are masked before zipping. IPs use stable `ip-N` tokens within a bundle; the raw token map is never exported.
Known config secrets are masked wherever they recur. Dump substitutions preserve length and offsets, including
ASCII and UTF-16LE strings. Dumps may still hold other memory.

`manifest.json` lists size and SHA-256 for every other file, including README.txt (self-hashing is impossible).
FNV-1a/32 chain checks describe the original logs before redaction, using the agreed
`<n> <utc ms> <chain hex8> <text>` format; sequence numbers start at zero. A final incomplete line is allowed. Legacy
files without chains are marked `not-present`. The manifest is integrity evidence, not a signature.

By hand (place the shared JSON beside the script):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File diag.ps1 -GameDir "<game folder>" -ReportFile "<text report>"
```

Optional `-DataDir`, `-OutDir`, `-ResultFile`, `-MaxLogMB`, `-NoOpen`. The launcher's `-ResultFile` protocol is
UTF-8 `OK <zip path>` / `ERR <reason>`. `-Fixture` skips hardware and network queries for game-free Windows tests;
it keeps all privacy rules enabled. The launcher never passes it. If export exceeds two minutes, the launcher
reports that it is still working; inspect the Desktop for its eventual output. Temporary exporter resources under
the data folder are retained on that timeout so the running exporter can finish.

Adapted from the MIT-compatible MDK2 and Witness co-op diagnostics designs.
