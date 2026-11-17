# Diagnostics tools

```sh
python3 tools/diag_report.py host.zip guest.zip --timeline merged.txt --align
python3 tools/diag_report.py player.zip --map matching.map --dll matching.dll --module avrt.dll
python3 tools/diag_report.py logs --raw --json
python3 -B tools/test_diag.py
```

The reader opens archives in memory without extracting them, verifies every manifest entry, reports pre-redaction
chain results, combines timestamped logs from both players, and summarises crashes and flight files. Legacy logs
without UTC timestamps are listed and retained for inspection; no timestamps are invented for them. Repeated or
missing BUGMARK numbers are never silently aligned. With `--align`, unique shared numbers estimate clock offset
against the first input; the estimate includes network delay or the delay between the players pressing F2. Timelines
keep complete bundle and component names. Input basenames must be distinct.

Crash reports include exception code and module offset, minidump module/stack pointers when present, and optional
GNU linker-map symbols for the specified module. A supplied DLL with a different timestamp blocks symbol mapping;
use map and DLL from the exact crashed build. Game addresses remain module offsets unless you provide matching
symbols. `map_symbols.py` and `minidump.py` are adapted from the MIT-compatible Witness/MDK2 tools.

Exit codes: 0 read successfully, 1 manifest/chain/collection problems, 2 unreadable or malformed input. The default
reader cap is 1 GiB uncompressed across all entries in one zip. Raw directories are for trusted local logs.

`diag_rules.py` consumes the same redaction table as `dist/diag.ps1`. Game-free tests cover text and same-length
binary masking, protection mutants in parallel through the existing mutant helper, manifest tampering, byte
budgets, chain damage, crashes, mapping, and marker alignment. They run in `make test`.

On Windows, additionally run the real exporter against synthetic files (no game, hardware queries or network):

```powershell
python tools/test_diag.py --powershell
```

This checks both log locations, UTF-16 conversion, newest-first byte limits including oversized dumps, dump size
preservation, shared redaction rules, original chain results and every manifest hash. `--powershell pwsh` selects
PowerShell 7 when installed. Windows system/network queries and About/Explorer behaviour need separate validation
on Windows; the portable tests cannot establish them.
