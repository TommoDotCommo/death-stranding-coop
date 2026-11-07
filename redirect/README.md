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
| `COOP_VIRTUAL_PAD` | absent/`0`: off; `1`: wrap the game's by-name `XInputGetState` lookup in `xinput1_4.dll` so a mod pad script drives player 1 (requires enforced networking, the mod, and the mod's `virtual_pad`; a mismatch refuses startup) |
| `COOP_BACKGROUND_FPS` | absent/`0`: off; `1`–`240`: for two copies on one PC, the copy whose window is not in the foreground holds its game thread in the frame's empty `PeekMessage` until 1/n s has passed since its last frame (requires `COOP_THREAD_TRACE=1`). The copy in front is never delayed and the mod's network worker is never held. Two copies on one GPU halve each other's frame rate or worse even with the GPU half idle; the copy you look at gets its full rate back only when the other one nearly stops (measured: about 2× when the background copy is held to under 1 frame per second, about 1.2× at a quarter of its speed) |
| `COOP_NETWORK_PEERS` | `a.b.c.d|...` co-op partners (numeric IPv4, at most 16) |
| `COOP_CRASH_RECORD` | absent/`0`: off; `1`: crash and hang recorder (below); `2`: as `1`, and the minidump is also written at the first recorded fault. Other values refuse startup |
| `COOP_CRASH_HANG_SECONDS` | absent: 15; `0`: no hang note; `1`–`600`: seconds without the main thread's message pump before the hang note (needs `COOP_THREAD_TRACE=1`, which provides the heartbeat) |

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

## The `avrt.dll` build (installs without replacing a game file)

`make -C redirect build/avrt.dll` builds the same proxy under the name `avrt.dll`. `d3dcompiler_47.dll` stays the
default build: the game ships its own `d3dcompiler_47.dll`, so that proxy has to rename it, and Steam's file verify
or an update puts the original back. `ds.exe` also imports `AVRT.dll`, which the game does not ship, so the proxy
beside `ds.exe` is the DLL the loader binds. Nothing in the game folder is renamed or replaced.

Why this name (checked against `ds.exe` build 13300582 and Windows 11 System32):

| Candidate | Result |
|---|---|
| `GDI32`, `KERNEL32`, `OLEAUT32`, `SETUPAPI`, `SHELL32`, `USER32`, `WS2_32` | KnownDLLs: always loaded from System32, cannot be proxied from the game folder |
| `WINHTTP`, `bcrypt` | not shipped, but the proxy itself imports both (the network hooks and entropy), so a proxy with that name would import itself; `WINHTTP` is also what the network policy hooks (15 imports) |
| `USP10` | not shipped; 44 exports, several forwarded to GDI32; used by the UI text shaper, which runs late and is also imported by system DLLs |
| `AVRT` | not shipped; 20 named exports, no forwarders, no data; `ds.exe` uses 2 of them (`AvSetMmThreadCharacteristicsA`, `AvRevertMmThreadCharacteristics`) from one function that looks like an audio render thread (no direct callers found, so when it first runs is unknown); no self-import; not touched by the network policy; no common mod loader uses this name (not exhaustively searched) |

Load order: the proxy's `DllMain` installs the redirect while the loader initialises `ds.exe`'s static imports,
before any game code, as the `d3dcompiler_47` build does. The mod starts, after the loader has returned, from the first
call of any forwarded export (the d3dcompiler build starts it from `D3DCreateBlob` or `D3DReflect`). Until then the
mod is simply not running; the redirect does not depend on it.

Export surface: `avrt.def` and `avrt_stubs.inc` are generated by `gen_proxy_stubs.py <System32 avrt.dll> avrt.def
avrt_stubs.inc` from the real DLL's export table. A `.def` forwarder can only name a module, not a full path, so
each export is a small jump stub; the first call loads the real DLL with its full System32 path, checks that the loader
did not hand back the proxy, patches its imports like any other module, stores the target and jumps with the
caller's arguments untouched. `test_gen_proxy_stubs.py` fails if the committed files no longer match the
live DLL's exports.

`make -C redirect avrt-proxy-test` (Windows interop, no game): a fixture exe that imports `avrt.dll` statically runs
from a temp folder holding the proxy. It must show that the proxy was bound first, that results and error codes match
direct calls to the real DLL (including a call with a fifth, stack-passed argument), that the known-folder environment
and a file created under the "real" folder land in the instance, and that the mod bootstrap ran. The same fixture
must fail with no proxy in the folder and with `mutant-avrt-forward-broken.dll`.

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

## Crash and hang recorder (opt-in)

`COOP_CRASH_RECORD=1` makes the proxy write `redirect-data\crash-<pid>-<process start>.log` (UTF-16LE, tab-separated
like the audit log; the audit log's `CRASH_RECORD` line names it). Why: the game's own ERROR dialog lists module names
without their bases, so the fault address cannot be placed. The recorder writes every address as `module+0xoffset`:

- `CRASH\tfirst chance 0xc0000005 access violation read 0x... at D3DCOMPILER_47.dll+0x1234\ttid=...`, followed by
  `CRASH_STACK` (up to 32 frames, unwound from the exception's own context with the module's `.pdata`). Recorded
  codes: access violation, in-page error, illegal and privileged instruction, stack overflow, integer divide by zero,
  heap corruption (`0xc0000374`) and fail-fast (`0xc0000409`; Windows usually ends the process before any handler sees
  it). "first chance" means the fault was seen before anyone handled it: the process may well go on. Each fault
  address is recorded once (repeats are counted), at most 32 addresses; the file never passes 1 MiB.
- `CRASH\tunhandled ...` and its stack when nothing handled it (from an unhandled-exception filter).
- `CRASH_MODULE\t<name>\tbase=... size=...`: the module map, once at startup and again at the first record.
- `CRASH_DUMP`: `crash-<pid>-<process start>.dmp`, a small minidump (stacks, threads, module list) written from the
  unhandled filter (level 2: also at the first record). It needs `C:\Windows\System32\dbghelp.dll`, loaded by full
  path on the recorder's own thread through the module policy: `allowlist` and `log-only` admit it (audited like any
  module); `strict` does not, and then `CRASH_DUMP_READY\toff` says why and no dump is written.
- `HANG` and `HANG_STACK`: when the main thread's message pump has not run for `COOP_CRASH_HANG_SECONDS` (default 15),
  the main thread is suspended just long enough to read its stack, once per process; `HANG_END` when it pumps again.
  A `GetMessage` call that waits for input is not a hang. Requires `COOP_THREAD_TRACE=1` (its hooks are the heartbeat).

It never changes what happens to an exception: the vectored handler always continues the search, and the filter the
game installs still runs after the recorder's. The game registers its own first-position vectored handler (it sends
access violations, illegal instructions, stack overflows and a few others straight to its crash reporter) and its own
unhandled filter after the proxy has loaded. So the recorder hooks `AddVectoredExceptionHandler` and
`SetUnhandledExceptionFilter` in the import tables to stay in front. Inside a handler it does not allocate, takes no lock
the faulting thread could hold (no heap, loader, CRT formatting or audit-log lock) and reads foreign memory only through
`ReadProcessMemory` on its own process.

Tests: `make -C redirect crash-record-test` (also part of `test`). The pure rules have a host test and three mutants. A
Windows fixture (`crash_record_probe.exe`, run directly from WSL; skipped without interop) goes through these cases:
- recorder off;
- a handled access violation (recorded as first chance, deduplicated, the process continues);
- 40 distinct faults (capped at 32);
- a level-2 dump;
- an unhandled access violation (record, module map, minidump, and the "game" filter still runs);
- a hang (the main thread's stack shows where it waits).

Four mutants must fail it: recorder not installed, recorder left behind the game's handler, game filter not chained,
and hang note from the wrong thread.

Research only: `make -C redirect build/research-crash-fault.dll` (deploy it as the copy's `d3dcompiler_47.dll`) adds a
stamp-guarded command file `redirect-data\crash.fault`, polled once a second: `divide <pid>-<process start>` raises an
integer-divide fault on a new thread and recovers it (the game's handler does not report that code), so the copy keeps
running. `access <pid>-<process start>` writes to address 0 on a new thread: a real crash. The default proxy has no
such code.

## Virtual gamepad (experimental)

`virtual_pad.h` holds the pure rules, pinned by `pad_test.cpp` and its four mutants. The proxy returns a wrapper only for a
by-name `XInputGetState` lookup on `xinput1_4.dll`, never for an ordinal lookup or another module. The wrapper calls the real
function first. For user 0, while the mod's `mod_pad` export reports an active script, it substitutes the scripted gamepad
and advances the packet number whenever the content changes. Other users and `XInputSetState` are untouched.
`test_pad_windows.py` (native Windows, no game) runs the real proxy and mod in a fixture tree, then checks:
- the presses reach player 1;
- player 2 is untouched;
- the real pad returns when the script ends;
- the proxy refuses a mismatched or invalid option.

Live status: the game resolves the wrapper but never polls it. The game keeps four pad slots, each with its own
backends (XInput, Windows.Gaming.Input, ...). A slot's current type is at device+28 and its index at device+6c; the
XInput backend holds the `XInputGetState` pointer at +16818. On the test PC, slot 0 is type 4 (Windows.Gaming.Input,
the connected controller) and slots 1-3 are unassigned. XInput detection runs only for unassigned or type-4 slots,
and the wrapper answers only user 0, so it is never asked. Keyboard input drives the live tests instead.

Module snapshots retry on `ERROR_BAD_LENGTH`, the documented race with a concurrent module load, which once ended a copy at
startup. Any other failure is still fatal, and the message includes the error code.
