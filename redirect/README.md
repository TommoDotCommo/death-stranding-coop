# redirect: the loader proxy that isolates each game copy

A replacement `d3dcompiler_47.dll` placed beside a **test copy's** `ds.exe`. The game imports this DLL
statically, so the proxy is loaded before any game code runs. It forwards every compiler export to the copy's own
original (renamed `d3dcompiler_47_original.dll`) and, before doing anything else, patches the import tables of every
loaded module so that the copy:

- **keeps its saves and settings to itself.** Known-folder and environment lookups (`LocalAppData`, `Roaming`,
  `Documents`, `Saved Games`, profile), file creation, deletion, moves, attribute queries, enumeration and free-space
  queries under the real save roots are mapped into the copy's own `redirect-data` folder. The real save tree is
  never read or written. Anything that would write outside the copy is refused.
- **loads only expected code** (`.ds-module-policy.json`: `strict`, `allowlist`, `log-only`). Allowlist admits the
  copy's own folder, System32, the Steam client libraries and Windows Defender's in-process scanner, by exact path
  rules.
- **can run next to another copy.** Named kernel objects listed per copy (the game's single-instance mutex) get a
  per-copy suffix (`objects.h`).
- **stays off the online service** (`network_policy.h`, default `enforce`). Only loopback and the co-op partners'
  IPv4 addresses are reachable. Every other destination, host lookup and WinHTTP connection fails the way an offline
  PC fails, and the game switches to its own offline mode. Why: the game's online features carry one player's
  structures into other players' worlds; a modded client must not take part. `log-only` records without refusing,
  for discovery runs only.
- **writes an audit log** `redirect-<pid>-<process start>.log` (UTF-16LE, `KIND<TAB>from<TAB>to`), with repeated
  records deduplicated and counted.

The proxy also starts the optional in-process mod (`../mod`) from `redirect-data\mod\mod.json` after the loader has
returned, never inside `DllMain`.

## Configuration (environment of the game process)

| Variable | Meaning |
|---|---|
| `COOP_REDIRECT_INSTANCE` | the copy's `redirect-data` folder (all redirected writes go here) |
| `COOP_MODULE_INSTANCE` | the copy's folder (parent of `redirect-data`) |
| `COOP_REDIRECT_STEAM` | the Steam install folder (writes refused, client DLLs admitted) |
| `COOP_REAL_LOCAL`, `COOP_REAL_ROAMING`, `COOP_REAL_DOCUMENTS`, `COOP_REAL_SAVEDGAMES` | the real folders to redirect |
| `COOP_MODULE_POLICY` | `strict` (default), `allowlist`, `log-only` |
| `COOP_ISOLATE_OBJECTS` | `name|name` kernel object names to make per-copy |
| `COOP_NETWORK_POLICY` | `enforce` (default) or `log-only` |
| `COOP_THREAD_TRACE` | absent/`0`: off; `1`: observe main-executable message calls (requires enforced networking) |
| `COOP_NETWORK_PEERS` | `a.b.c.d|...` co-op partners (numeric IPv4, at most 16) |

Use the proxy only in a disposable copy of the game, never in your real Steam install. Disable Steam Cloud for the
game while testing: the Steam client syncs from its own process, which no in-game hook can stop.

## Build and test (Linux, MinGW cross-compiler; no game needed)

```sh
make -C redirect all test
```

`test` runs the pure classifiers (paths, modules, objects, network, audit throttle, hook list) with a deliberately
broken build ("mutant") for every protection; each mutant must fail its test. `all` builds the proxy, the Windows
probes (`redirect_probe.exe`, `net_probe.exe`) and mutant DLLs (`hooks-disabled.dll`, `mutant-*-unmapped.dll`,
`mutant-net-unhooked.dll`). On Windows, the probes load a proxy without the game and check real behaviour:
markers land only in the copy, decoy files in a real-looking tree stay invisible, external connections are refused
while loopback works. The mutant DLLs must fail the probes.

## Known gaps

- Direct `ntdll` file calls (`NtCreateFile`, `NtQueryAttributesFile`) bypass the import-table hooks.
- Network: direct NT/AFD calls, extension functions other than `ConnectEx` and `WSASendMsg`, and sockets in other
  processes are not covered. A Windows Firewall outbound block on the copy's `ds.exe` is a good independent second
  layer (a blocked UDP datagram is dropped silently; check with TCP).
- `DllMain`/TLS code of late-loaded modules runs before their imports are patched.
- A same-size write with restored timestamps into a hard-linked game archive would not be detected by metadata checks.

## Live evidence (Steam build 13300582)

- One copy and then two copies at once reached the menu and gameplay under the allowlist policy, each with its own
  saves (profile, settings and gameplay checkpoints written only inside the copy; the real saves were byte-identical
  after every run, checked again after a settle wait).
- With the network policy enforced, the game's calls to its configuration and online servers were refused and it
  showed its own "switching to offline mode" message; the co-op traffic over loopback was unaffected.


## Message-thread observer (experimental)

Set `COOP_THREAD_TRACE=1` in the disposable process's launch environment to observe
its ordinary `PeekMessageA` and `GetMessageA` imports. It requires network policy
`enforce`, both main-executable imports, and the usual isolation/launch gates.
Absent or `0` leaves these imports untouched; other values refuse startup.
This changes those two IAT slots and writes diagnostics into proxy-owned memory.
It never invokes an entity function, reads an MSG payload, creates an actor or
sets a transform. Dynamic lookups and imports in other modules are not observed.
The proxy must remain loaded for process lifetime; there is no observer unload API.

Each sampled call publishes an entry and a separate return record with thread ID,
monotonic milliseconds, API, caller address, nesting depth and the signed return
value. Entry records include up to 16 native stack frames without symbol lookup.
The fixed buffer holds 2,048 records. Each thread/API samples its first four calls,
then at most one per second, with a separate allowance for nested calls. There are
64 thread-tracking slots. Saturation/contention drops pairs and increments a
counter; it never waits for space or overwrites a published record. No log writes,
heap allocations in the recorder, or game-object dereferences occur in the hooks.
Native stack unwinding still has overhead; this is a diagnostic option.

The audit log's `THREAD_TRACE` record identifies the buffer. From **native Windows
Python**, while the process is alive:

```sh
python redirect/read_thread_trace.py --log "<instance>/redirect-data/redirect-<pid>-<start>.log"
```

The reader checks process creation time before reading, requests only
`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`, and validates the buffer
ABI/process identity. It double-reads append-only records and marks unpublished
or changing records incomplete. It neither injects a thread nor suspends or writes
the process. Save JSON output outside the repository. An entry with no return may
mean a blocked message call; it is not proof of a dead thread. Frame addresses
inside the host image also receive RVAs for comparison with `mod/game_offsets.md`.

A recorded thread and caller stack do **not** establish a safe mutation phase.
Message dispatch may be nested, jobs may still be running, and thread IDs can be
reused. Thread identity, world lifecycle and a job synchronization boundary need
separate evidence before calling the entity creator or transform setter.

Game-free checks:

```sh
make -C redirect thread-trace-test
# Native Windows Python; creates new fixture trees, never launches ds.exe:
python redirect/test_thread_trace_windows.py --proxy "<build>/d3dcompiler_47.dll" --probe "<build>/thread_trace_probe.exe" --work-dir "<disposable drive folder>"
```

The native probe uses its own queues and a message-only window: two threads,
nested dispatch, message/flag preservation, GetMessage positive/zero/negative
results, and LastError preservation. It checks disabled tracing, enabled tracing,
invalid option refusal, enforced-network requirement, PID-reuse refusal and native
message imports in a real late-loaded DLL. The WSL cross-build driver
`test_thread_trace_windows_mutants.py --windows-python <native Windows Python>
--work-dir <disposable drive folder>` must kill mutants that broaden observer
scope or remove the enforced-network requirement.
Portable tests cover publication, bounded storage, per-thread depth, sampling,
concurrent producers, table exhaustion, incomplete reads and ABI validation.
Mutants for depth, rate, capacity and publication must fail their oracles.


When `mod.json` explicitly selects `game_thread:true`, the observer hooks also
provide the reviewed dispatch seam described in `../mod/README.md`. This requires
enforced networking and a matching `mod_game_thread_tick` export. `GAME_THREAD_ARMED`
records the binding; defaults remain observation-only. A failed callback disarms
the pointer and fails closed. The proxy retains its mod module for process lifetime.
