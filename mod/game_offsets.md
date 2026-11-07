# Game structure discovery: static evidence for build 13300582

Only the private copy of the Director's Cut executable was inspected, read-only.
Its size is 86,479,872 bytes and its SHA-256 is
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
No game or install program was run, no live memory was read, and no game file was
modified. Everything below is static evidence, not live compatibility. All object
semantics remain **hypotheses** until the stated live observation passes.

RVA means relative virtual address from image base `0x140000000`. RVAs identify
this pinned build's evidence; they are not a patch-independent runtime API. A
future adapter must resolve reviewed signatures, validate every pointer and object
lifetime on a proven game thread, and fail closed on a changed hash, absent or
ambiguous signature, invalid target or implausible value.

## PE and name-reference method

Relevant PE fields are:

| Section | RVA | Virtual size | Raw file offset | Raw size |
|---|---:|---:|---:|---:|
| `.text` | `0x1000` | `0x3b389ac` | `0x400` | `0x3b38a00` |
| `.rdata` | `0x3b3a000` | `0xa252b0` | `0x3b38e00` | `0xa25400` |
| `.data` | `0x4560000` | `0x393d5e0` | `0x455e200` | `0xa05000` |
| `.pdata` | `0x7e9e000` | `0x1c88b8` | `0x4f63200` | `0x1c8a00` |

The exception directory contains 155,834 12-byte function entries. They were used
to attribute code references to function bounds, not to assume that every function
has a symbol. A separate string scan found 191 Microsoft RTTI descriptors, mostly
runtime/Havok types; the useful game names below are Decima reflection/export
strings in `.rdata`, not Microsoft C++ RTTI proof.

For the inventory, a direct cross-reference is an `objdump`-decoded `mov` or `lea`
instruction with a RIP-relative disp32 operand resolving exactly to the selected
standalone NUL-terminated string. Metadata-indirect uses are not included. Thus
zero means **no direct code reference found**, not that the type is unused. These
counts are not total call counts or proof of object layout.

### Relevant Decima names

Every entry below is in `.rdata`; notation is `name @ string RVA / direct refs`.

- Local player/human: `Player @ 0x3bb9c60 / 2`; `PlayerCharacter @ 0x3bbb700 / 1`;
  `PlayerGame @ 0x3d5bb58 / 1`; `DSPlayerEntity @ 0x3d760b0 / 1`;
  `DSPlayerSystem @ 0x3d6ca10 / 1`; `ControlledEntity @ 0x3ee8f58 / 1`;
  `HumanoidController @ 0x3eeb3a0 / 1`; `HumanoidMover @ 0x3f13438 / 1`;
  `NetPlayerManager @ 0x3bd05b8 / 1`.
- Character/skeleton/animation: `AnimationManager @ 0x3d08608 / 1`;
  `AnimationData @ 0x3baca30 / 1`; `SkeletonAnimationResource @ 0x3c116f8 / 1`;
  `RagdollComponent @ 0x3cff950 / 1`; `HumanoidInAirState @ 0x3eedf40 / 0`;
  `HumanoidMountState @ 0x3eef248 / 0`; `HumanoidSwimState @ 0x3eee920 / 0`.
- Transform/position/orientation/velocity: `Entity @ 0x3d033e8 / 49`;
  `WorldNode @ 0x3c23400 / 1`; `WorldPosition @ 0x3ba0d30 / 1`;
  `WorldTransform @ 0x3b8ef30 / 2`; `Position @ 0x3b8ee28 / 10`;
  `Velocity @ 0x3bb1f78 / 1`.
- World/region/streaming: `WorldData @ 0x3b98950 / 1`; `Scene @ 0x3d0787c / 2`;
  `SceneComponent @ 0x3d15bc8 / 1`; `SceneInstance @ 0x3d15280 / 1`;
  `StreamingTileResource @ 0x3c1ce90 / 1`;
  `StreamingTileStateResource @ 0x3c1dd68 / 1`.
- Cutscene/loading/menu: `Cutscene @ 0x3dfe4c0 / 0`;
  `InGameCutscene @ 0x3dfe558 / 0`; `HeavyCutscene @ 0x3dfe870 / 0`;
  `LoadingScreenMenu @ 0x3e62060 / 1`; `MovieMenu @ 0x3e614a8 / 1`;
  `MenuSaveDataDialog @ 0x3f192f8 / 1`; `PrivateRoom @ 0x3d67848 / 0`;
  `GameModule @ 0x3d5c730 / 1`.
- Cargo/vehicle/BB: `Vehicle @ 0x3bdd910 / 1`; `VehicleEntity @ 0x3e22e60 / 1`;
  `DSCargoStrandComponent @ 0x3dd3170 / 0`;
  `CargoStrandEntityResource @ 0x3dd74f0 / 0`; `BBPod @ 0x3d669d4 / 0`;
  `PlayerBBConfig @ 0x3d74958 / 0`; `DSDBBAliveHumanComponent @ 0x3de5cc8 / 0`;
  `DSBaggage2Manager @ 0x3dd2720 / 2`.

## Patch-oriented signature tool

Run from the repository root; this is the one-command verifier for the pinned build:

```sh
python3 -B mod/find_signatures.py "<game dir>/ds.exe"
```

The tool uses only the Python standard library. It reads and hashes the file,
refuses any other SHA-256, parses PE32+ sections with checked arithmetic, scans
only executable sections, and requires exactly one match per candidate. Patterns
are at most 32 bytes. Every relocatable branch/RIP displacement used by a pattern
is wildcarded. Each declared RIP reference must have ModRM mod=00, r/m=101 and a
fully masked disp32; its resolved target must lie in a non-executable PE section.
Zero matches, multiple matches, bad masks, invalid references and bad targets all
produce a nonzero exit and an explicit error. JSON reports the match count, match
RVA and resolved targets. No historical absolute address is a fallback.

Current results, all with `match_count: 1`:

| Candidate | Match RVA | Resolved RIP targets |
|---|---:|---|
| `DSPlayerEntity_sExportedGetLocalDSPlayerEntity` | `0x2757a30` | manager `0x7bc7568` (`.data`), type metadata `0x4b1acb8` (`.data`) |
| `Player_sExportedGetLocalPlayer_impl` | `0x23ca2fc` | manager `0x7bc7568` (`.data`), selection state `0x4b1acbc` (`.data`) |
| `WorldNode_orientation_matrix_body` | `0x1e04e80` | matrix constant `0x3b87f94` (`.rdata`) |
| `MenuSaveDataDialog_sExportedIsSaveDataLoadingOrRequested` | `0x3634fe0` | manager `0x7be6318` (`.data`) |
| `DSPlayerEntity_sExportedHasBaggage_body` | `0x26e8652` | related global `0x7be79b0` (`.data`) |
| `DSSoundNodeGraphBindings_sGetCutsceneSoundPlaying` | `0x2dd37e0` | sound global `0x7ae8700` (`.data`) |

The independent local-player paths resolving the same manager is useful static
corroboration, but not proof that either path is safe to call from a mod.

Ambiguity was measured, not post-filtered by a desired name. The first entity
position lock/copy window had 31 matches; a tighter window containing both
position loads still had 12. The shared WorldNode default-transform block had 3,
and the vehicle type-check sequence had 37. Those patterns were rejected. The
common reflection-registration prologue is also shared and is not used. Therefore
there is deliberately no accepted entity-position or vehicle-driving signature
in this milestone; their pinned RVAs below are analysis leads only.

## Object-layout hypotheses and exact live validation

“High” confidence describes the stated machine-level loads/copies in this build.
It does not imply high confidence in the semantic interpretation. No direct game
call or dereference is authorized by this table. Live work must first establish
the isolated runtime, safe game-thread hook, type/lifetime checks and read-only
observation.

| ID | Static observation and semantic hypothesis | Confidence | Exact live observation that confirms or refutes it |
|---|---|---|---|
| H1 | Both local-player implementations use manager global `0x7bc7568`; `DSPlayerEntity` performs a type lookup. **Hypothesis:** this is the local-player manager path. | High for the shared path; medium for lifetime/local semantics. | On the proven game thread, resolve both signatures independently and compare the manager in menu, gameplay, death and reload. Confirm only if both agree, gameplay yields a valid typed local object, and stale objects are rejected after transitions. Refute if they diverge, gameplay returns null/stale data, or the selected object follows a remote/NPC. |
| H2 | `Player_ExportedGetEntity` at `0x2361b80` returns pointer `Player+0x70`, or null for null Player; the DS wrapper also returns object `+0x70`. **Hypothesis:** this is the entity used for presence. | High for offset; medium for entity meaning. | Follow the validated local Player and compare `+0x70` with the DS getter result. While using only local input, evaluate the position lead on that entity. Confirm if both pointers agree and only the controlled character moves; refute on disagreement, unrelated motion or stale reuse after reload. |
| H3 | `Player_ExportedIsLocalHuman` requires byte `Player+0x78 != 0` and `+0x79 == 0`; the manager filter reads the same bytes. **Hypothesis:** these are local/human selection flags. | High for predicate; medium for flag meanings. | Record both bytes for the controlled human, another human/NPC and a remote player when available, including vehicle mounting. Confirm if the predicate selects exactly the local human in every case; refute if local becomes false or any non-local passes. Do not infer bit meanings beyond the observed predicate. |
| H4 | `Entity_ExportedGetPosition` at `0x2362af0` locks through entity `+0x288`, then copies 16 bytes from `+0xc8` and 8 from `+0xd8` (24 total). **Hypothesis:** position storage; scalar layout, units and axes are unknown. | High for widths/offsets; low for numeric interpretation. | With a validated entity, sample all 24 bytes while stationary and after a measured 10 m walk, a jump and a teleport. Confirm a position interpretation only if values stay finite, are stable while stationary, change with the controlled entity, and one consistent scalar decoding gives the measured displacement/scale. Test float64 and packed alternatives rather than assuming three doubles. Refute on NaN/Inf during valid play, unrelated motion or inconsistent scale. |
| H5 | `Entity_ExportedGetWorldTransform` wrapper `0x2362c30` reaches helper `0x234c210`, which returns 60 bytes from `+0xc8..+0xdf`, `+0xe0..+0xff`, and dword `+0x100`. **Hypothesis:** position plus orientation and a final region/flags word. | High for returned layout; low for final-word meaning. | Compare all 60 bytes with H4 and H6 during walking, rotation, teleport and a region boundary. Confirm transform semantics if position agrees, the middle values form a consistent orientation, and the final word changes only with a reproducible state/region transition. Otherwise reject the region/flags interpretation. |
| H6 | WorldNode position getter `0x1e0d260` copies 24 bytes at `+0x20`; transform getter `0x1e0d2c0` copies `+0x20..+0x5b`. Orientation helper `0x1e04e30` reads nine float32 values at `+0x38,+0x3c,+0x40,+0x44,+0x48,+0x4c,+0x50,+0x54,+0x58` and expands them into a 64-byte 4x4 matrix. **Hypothesis:** 3x3 orientation plus position. | High for widths/offsets; medium for matrix orientation. | On a validated WorldNode, yaw exactly 90 degrees, then pitch/roll and translate without rotating. Confirm if the nine values remain finite and orthonormal within measured tolerance, rotation changes only the 3x3 portion, and translation agrees with H4/H5. Determine row/column order and axes from the 90-degree cases; refute orthonormal rotation if those invariants fail. |
| H7 | Entity velocity implementation `0x234c180` returns 16 bytes through object `+0xb0`, virtual slot `+0xc8`; fallback uses `+0xb8` and virtual slot `+0x130`. Angular wrapper `0x23628b0` uses `+0xb0`, slot `+0xd8`, also returning 16 bytes. **Hypothesis:** linear/angular velocity vectors, possibly with padding. | High for call paths; medium for vector semantics. | After H4 establishes units and axes, sample position and both 16-byte results at a fixed game-thread cadence while stationary, walking and driving. Confirm linear velocity only if it is near zero at rest and matches finite differences of position within measured timing error; confirm angular velocity by a controlled 90-degree turn. Identify any padding by invariance. Refute on inconsistent scale, axes or unrelated changes. |
| H8 | `HumanoidController_ExportedGetMove` at `0x350fd60` reads float32 `+0x14c` and `+0x150`. **Hypothesis:** two local move/input components, not world velocity. | High for float loads; low-medium for normalization/meaning. | Record both values with neutral input, full deflection in four directions, walking and sprinting. Confirm a normalized move vector if neutral is zero, full deflection stays within [-1,1], directions are repeatable and magnitude responds to input rather than world speed. Any sustained out-of-range or speed-proportional result refutes normalization; H7, not this field, must establish world velocity. |
| H9 | `AnimationManager_ExportedGetFloatAnimationVariable` at `0x2396fe0` uses manager pointer `+0x58`, record stride 24, and float32 record `+0x20`. No bounds check is visible for caller index EDX. **Hypothesis:** animation-variable records. | High for addressing; low for safe indices/lifetime. | First recover the valid manager and index through the export's own caller/metadata. Then read a known variable through the normal path during idle, walk and ragdoll. Confirm only if the value stays finite and follows that named variable; refute if it is unrelated or index lifetime changes. Never probe arbitrary indices: a crash or out-of-bounds read is not validation. |
| H10 | `VehicleEntity_sExportedIsDriving` implementation `0x2fa7450` uses dword `+0x4cc`, dword `+0x63c`, pointer `+0x528`, type checks and a mode argument. **Hypothesis:** driving is a composite state, not one boolean. | High for composite predicate; medium for field meanings. | Capture all three fields and both mode results on foot, entering, seated stationary, moving and exiting. Confirm the export if its output matches actual driving in every state and mode; use the export rather than guessing a flag. Any mismatch refutes the field interpretation. A single field changing is insufficient confirmation. |
| H11 | Baggage body `0x26e8652` reads global `0x7be79b0`, gates on global-object dword `+0x14 <= 2`, then follows player(?) `+0x54d0`, pointer `+0x50`; later records compare dword `+0xc8` with a requested ID. **Hypothesis:** cargo/baggage lookup state. | High for accessed offsets; low-medium for owner and ID semantics. | With validated objects, observe adding, removing and rearranging one known cargo item. Confirm only if the relevant record appears/disappears with that item and `+0xc8` consistently equals its independently observed ID; refute on unrelated records or unstable IDs. Establish all pointer lifetimes before dereferencing. |
| H12 | BB affection path reaches `0x27a2f10`: return dword `+0x7844`, except byte `+0x7851 != 0` forces zero. **Hypothesis:** BB affection value plus suppression/reset state. | High for returned predicate; medium for affection semantics. | During validated BB gameplay, record both fields through soothing, crying and any affection gain/loss. Confirm if the dword tracks the independently displayed affection and the byte explains forced-zero intervals; refute if displayed affection moves without this value or suppression disagrees. Range and maximum remain unknown until measured. |
| H13 | Save-dialog getter `0x3634fe0` reads manager `+0x328` count and `+0x330` pointer array; an entry with dword `+0xc` in 6..15 yields true. **Hypothesis:** save-data loading/request state only. | High for predicate and narrow scope. | Trigger menu save, save completion, load and an unrelated level/streaming transition. Confirm the narrow predicate if it becomes true only for matching save-dialog operations and entry states match 6..15. If ordinary world loading never changes it, reject it as a general loading gate rather than treating that as failure of the narrow predicate. |
| H14 | Cutscene-sound getter `0x2dd37e0` reads global `0x7ae8700` and calls another function when non-null. **Hypothesis:** cutscene audio state, not visual cutscene state. | High for audio-specific call path; low for visual gating. | Compare it during gameplay, an audible cutscene, a muted cutscene and cutscene exit. Confirm only that it tracks the observed audio condition. If muted/visual states diverge, refute visual-cutscene use. Presence suppression needs a separate visual/loading observation. |

Rejected or insufficient leads:

- `GameModule_sExportedIsGameLoading` resolves to `0x1920dc0`, which is just
  `xor al, al; ret` in this build. It provides no working loading-state evidence.
- `LoadingScreenMenu`, `MovieMenu`, `PrivateRoom`, streaming resources and BB/cargo
  type names are inventory leads only; no safe state field was established.
- Entity position and vehicle driving need new, distinctive signatures before
  runtime use. Their layout observations above are not accepted scanner results.

## Live evidence (2026-11-01, launch 9, read-only discovery, one walk and one quarter turn)

The derived H1 fast path (`player = *(manager + 0x48 + 8*index)`, selection record at the `Player_sExportedGetLocalPlayer_impl`
selection target minus 4: index, mode 1, kind 2, null pointer) yielded a coherent player for 657 samples after the owner
pressed Continue; the manager global is null at the main menu. With the entity at `player + 0x70`:

| Field | Decoding | Evidence |
|---|---|---|
| H4 position, `entity + 0xc8` | 3 x float64 (x, y, z), z up | stationary to the millimetre for 30 s; a straight walk of about 38 units at about 4 units/s; stationary again. The float32 alternative is nonsense. Units: consistent with metres (Decima convention, walking pace); not yet measured against a known distance. |
| H5 rotation, `entity + 0xe0 .. + 0x103` | 3 x 3 float32, row-major; the 60-byte transform's final word is m22 (1.0 on flat ground) | unchanged while walking straight (about 172 degrees), a smooth rotation of about 81 degrees during the owner's quarter turn with the position unchanged; rows stay unit length and orthogonal to 0.001. |
| Forward vector | row 2: (m10, m11, m12) | walking heading from the position track is -97.3 degrees; row 2 points at -97.5 degrees. Row 1 is the right vector. |
| H3 local-human flags, `player + 0x78/+0x79` | `01 00` for the whole run | matches the game's own predicate for the local human. |

Still unconfirmed: lifetime across reload and death, behaviour in vehicles, the slow selection path, and a world/region id
(the transform has no extra word for it). H6 (WorldNode) is not needed for presence while H4/H5 hold.

## Unknowns and required next evidence

- Player/entity/manager type identity, ownership, generation, destruction and
  reload behavior; a safe game-thread callback and supported calling convention.
- Position scalar types, metres/other units, axes, origin, region/cell meaning,
  cross-region conversion and explicit teleport detection.
- Concrete velocity virtual targets, vector padding, angular representation and
  timing relative to the sampled transform.
- Skeleton/pose representation, safe animation IDs/index bounds, action mapping,
  ragdoll handling and remote actor/puppet creation. No animation write is safe yet.
- A general loading/streaming/menu/private-room/visual-cutscene gate. Save loading
  and cutscene sound are not substitutes.
- Vehicle/cargo/BB field meanings and lifetimes, and any presence flag mapping.
- Signature survival on another patch. Uniqueness and RIP validation are proven
  only for the pinned bytes; no other build may be scanned as if compatible.
- All live behavior. `GameProvider` must remain unavailable until these gates pass.

## Commands and timings

Commands were run from the repository root unless noted. Working disassembly and
JSON dumps are confined to ignored `mod/build/`; they are not repository evidence
files. Focused disassembly used `objdump -d -M intel --start-address=<VA>
--stop-address=<VA> "<game dir>/ds.exe"` over the functions
above (0.72-0.95 s per small range).

| Command | Result | Wall time |
|---|---|---:|
| `sha256sum "<game dir>/ds.exe"` | matched profile hash | not measured |
| `objdump -h "<game dir>/ds.exe"` | section table above | 0.07 s |
| `python3 mod/build/analyze_rtti.py "<game dir>/ds.exe" mod/build/rtti` | 191 Microsoft RTTI descriptors | 11.17 s |
| `python3 mod/build/analyze_decima.py "<game dir>/ds.exe" mod/build/rtti` | keyword/name-pointer leads; later exact pass superseded noisy counts | 9.30 s |
| `python3 mod/build/exact_name_refs.py "<game dir>/ds.exe" mod/build/rtti/exact_name_refs.json` | exact standalone-name/direct-RIP inventory | 14.34 s |
| `python3 -B mod/find_signatures.py "<game dir>/ds.exe"` | six unique candidates | 13.62 s first pass; 13.34 s final pass |
| `python3 -B -m unittest ./tests/test_find_signatures.py -v` | 9 synthetic host tests passed | 0.06 s |
| `python3 -B -m unittest discover -s ./tests -v` | 71 game-free tests passed | 104.80 s |
| `make -C ./mod test all` | adapter tests, safety mutants and Windows cross-build passed | 30.90 s |
| `make -C ./core test` | core fuzz and all 28 mutants passed; includes mod tests | 23.80 s |

The scanner's rejected runs failed loudly as intended: 31 and then 12 entity-position
matches, 3 transform-default matches and 37 vehicle type-check matches. They were
analysis evidence, not successful verifier runs.

## Puppet leads: script-binding exports resolved statically (2026-11-02, nothing called)

Method: each exported name has one RIP-relative `lea r8,[name]` in a registration function; 0x17 bytes earlier
`lea rax,[impl]` loads the implementation, and `lea rdx,[desc]` 0xa bytes later loads the short name. Calibrated on two
addresses known from earlier analysis (`Entity_ExportedGetPosition` 0x2362af0, `Player_ExportedGetEntity` 0x2361b80):
both match. Tool: a 40-line scanner over the pinned exe (kept in session scratch, not committed yet). RVAs are for build
13300582 only and are leads, not accepted signatures (`find_signatures.py` has not been extended).

| Export | Impl RVA | Body (first instructions) | Reading |
|---|---:|---|---|
| `SpawnSetup_ExportedCreateEntityFromSpawnSetup` | 0x18f5a20 | `xor eax,eax; ret` | **stub in retail**: always null. Dead end. |
| `BuddyManager_sExportedRequestSpawnBuddy` | 0x18e9b10 | `ret` | stub. |
| `EntityResource_ExportedCreateEntity` | 0x23806a0 | 4 register args + byte + 2 pointers + pointer to a 16-byte tagged value, forwards to 0x2374400 | **real**. Puppet candidate: create an entity from a character resource. Argument meanings UNKNOWN. |
| `EntityResource_ExportedCreateEntityWithParent` | 0x2380630 | real body | candidate. |
| `EntityResource_ExportedCreateDamageEntity` | 0x23805b0 | real body | not for puppets. |
| `Entity_ExportedSetWorldTransform` | 0x2361ea0 | null check on rcx; calls 0x22dff80 (rcx=entity, rdx passed through) then 0x235bf60(entity, 0) | drive the puppet from the received pose. Transform layout presumably the 60-byte H5 block; UNCONFIRMED. |
| `Entity_ExportedPlaceOnWorldTransform` | 0x2361c00 | real | alternative placement. |
| `Entity_ExportedSetVisible` | 0x2361e90 | null check, tail jump 0x235f640 | hide while stale. |
| `Entity_ExportedRemove` | 0x2361c20 | `jmp 0x234caa0` | despawn on disconnect. |
| `Entity_ExportedSetSleeping` / `SetInvulnerable` | 0x2361e20 / 0x2361d90 | real | keep the puppet out of physics/damage. |
| `DsNpcGeneralCommand_sExportedTeleportNpc` | 0x2bf7930 | real | NPC teleport. |
| `Spawnpoint_ExportedSpawn` / `MultiSpawnpoint_ExportedSpawn` | 0x23da6c0 / 0x23da650 | real, need a placed spawnpoint object | possible but needs a spawnpoint instance. |

Open before any call: which EntityResource to use (the local player's entity probably references its own resource;
cloning Sam may bring player-controller components; an NPC porter resource would be safer), the eight CreateEntity
arguments (read the game's own callers of 0x2374400), entity lifetime/ownership, and a game-thread hook to call from.

### The creator behind `EntityResource_ExportedCreateEntity` (0x2374400), read statically

Hypothesis from the body: `CreateEntity(EntityResource* res /*rcx; null -> null*/, const double pos[3] /*rdx*/,
const float rot[3][4] /*r8: 3 rows, 16-byte stride, xyz used*/, name_or_id /*r9: converted, stored at entity+0x50*/,
bool place /*if set: 0x235a010(entity,&transform,1,0)*/, instigator /*optional: 0x235df30(entity,a)*/,
attack_event /*optional: 0x235d5f0(entity,b)*/, tagged_value /*16 bytes by pointer; 0x23c8130(value,entity)*/)`.
It builds a 60-byte transform (3 x f64 position + 3x3 f32 rotation, the live H5 layout), calls `0x2378cf0` on it,
creates through `res->vtable[0x80/8](res,&transform)`, then adds the entity to the world through the global at
0x7bc4e70 (`vtable[3](mgr, entity, 0, 0, 0)`). The game's own code does not call 0x2374400 except through the export.

`Entity_ExportedGetResource` (0x2362ba0): null check, then `0x1dde990(entity+0x58)`: the entity's resource reference
lives at +0x58. Candidate puppet chain, all through the game's own functions: local entity (live, `player+0x70`) ->
GetResource -> CreateEntity(resource, pose) -> SetWorldTransform per received pose -> SetVisible/Remove.
Risks: Sam's resource may carry player-only components (camera, input, BB, cargo); an NPC porter resource avoids that
but must be found. `PlayerGame_ExportedSwitchPlayerCharacter` (0x2642cf0) suggests character resources are swappable.


### Message pump and creator arguments: next static pass (2026-11-02)

`python3 -B mod/find_message_calls.py "<game dir>/ds.exe"` is a reproducible,
read-only inventory for the same pinned SHA-256. It parses the ordinary USER32
imports and AMD64 exception directory, scans executable sections for RIP-relative
indirect call/jump candidates, follows direct rel32 callers of jump stubs, and
reports LEA references to containing function starts. It reports **byte candidates**,
not decoded instructions; register-indirect uses and delay imports are outside its
inventory. Function bounds are exception-directory ranges, not complete semantic
functions. Synthetic tests cover bounds, signed displacements, data decoys,
import thunks, malformed images, missing imports and the hash refusal.

Focused `objdump` disassembly from the exception-directory start independently
confirmed the following instruction boundaries and targets on the pinned file:

| API | IAT RVA | Call RVA | Containing range (end exclusive) |
|---|---:|---:|---|
| PeekMessageA | 0x3b3a7b8 | 0x1a19381 | 0x1a19350–0x1a193df |
| GetMessageA | 0x3b3a7c0 | 0x1a19398 | 0x1a19350–0x1a193df |

The pump peeks without removing (fifth argument 0), then gets, translates and
dispatches at most 100 messages per invocation. A zero GetMessage result returns
false; a negative result is not specially handled by this game function. No
thread identity or world-mutation phase follows from this code alone.

There are no E8/E9 byte candidates targeting the pump start. Instead, LEA at
0x1a16cae in constructor range 0x1a16c20–0x1a17050 resolves to the pump and the
following store puts it at application object +0xc0. The object's vtable is
0x3ba6488. The routine at 0x1a17a90 reads +0xc0 and invokes it through RAX at
0x1a17aea. Its pointer occurs at 0x3ba6660 (vtable offset +0x1d8); a global
counter at 0x577d68c guards reentrancy around this invocation. These are static
links, **not proof of a safe game-thread callback**. That counter also means a
hook must not assume message dispatch is a unique frame boundary. Thread ID,
outer caller stack, cadence in gameplay/loading, and overlap with world jobs
remain unknown. Do not drive an entity creator from either message API yet.

The creator's arguments 6 and 7 now have stronger static names:

| Argument | Evidence | Interpretation |
|---|---|---|
| 6 | Creator passes it to 0x235df30. Registered `Entity_ExportedSetInstigator` resolves to 0x2361d30 and tail-jumps to the same routine after checking existing fields. | Instigator, not an arbitrary attachment pointer. Exact pointee layout/lifetime unknown. |
| 7 | Creator passes it to 0x235d5f0. Registered `Entity_ExportedSetAttackEvent` resolves to 0x2361c70 and tail-jumps there. | Attack event, not a parent pointer. Exact pointee layout/lifetime unknown. |
| 8 | Export copies 16 bytes; helper 0x23c8130 reads signed tag byte +0, switches on tags 1/2/3, and loads a pointer at +8 in tags 1/2. Other tags return without those actions. | A tagged value; **the earlier UUID hypothesis is rejected**. Zero tag performs no branch action in this helper, but a safe complete spawn contract is not proven. |

The helper unconditionally reads the tag through argument 8: null is not an
acceptable default pointer. Its type name and tags' complete semantics remain
unknown. A raw zero-initialized 16-byte value would exercise only the observed
no-action branch; that does not establish a valid resource/lifetime contract.
No creator or transform setter was called in this pass. Whether Sam's resource
creates camera, input, BB or cargo components is still unknown.

Verification for this pass: pinned-file scanner and focused disassembly ran;
10 synthetic scanner tests and the full repository `make test` passed (including
28 core and 33 pose mutants plus the proxy protection oracles). No game launched,
no live thread identity observed, and no actor created.


### Message-thread observation: menu, loading, movement and pause (2026-11-02)

The opt-in proxy observer (`redirect/thread_trace.h`, `COOP_THREAD_TRACE=1`)
was first exercised by a Windows fixture probe: two threads, nested dispatch,
message/flag and LastError preservation, GetMessage positive/zero/negative
results, disabled mode, invalid configuration refusal, enforced-network
requirement and process-creation-time refusal. Four recorder mutants and six
reader tests passed their oracles. The repository's full `make test` and Windows
cross-build passed before the game observation.

A supervised single disposable instance of the pinned build then produced 906
published records (entry/return pairs), zero dropped pairs, and counters of
45,326 PeekMessageA and 9,947 GetMessageA calls. The only observed thread ID was
9316. Independent pose sampling produced 844 samples, 422 with a local player;
position spans were about 42.95/15.40/11.23 metres in adapter axes, peak sampled
speed 5.32 m/s, and the final paused sample had speed zero. The owner identified
the menu and completed the requested load/walk/pause sequence. A valid pose alone
does not establish a pause or visual-cutscene gate.

The common captured return-address chain was:

`0x1a19387` (Peek) or `0x1a1939e` (Get) -> `0x1a17aec` ->
`0x263d2ae` -> `0x263a869` -> `0x263753d` -> `0x18e9e88` ->
`0x18ea224` -> `0x18ea0a6` -> `0x3b0405e`.

Focused disassembly confirms the caller at 0x263d2a8 dispatches application
vtable slot +0x1d8 through the application global at 0x577d3f0. Routine
0x2637510 loops over calls to 0x263a5d0 and 0x263b860. The former calls
0x263cfe0 at 0x263a864 (return 0x263a869); after that return it iterates
subsystems via vtable +0x8 at 0x263a8a8, calls 0x263ec30, then iterates via
vtable +0x10 at 0x263a8f6. Those subsystems' types and their job synchronization
contracts remain unknown. Do not infer a completed world-job barrier here.

Earlier startup/loading samples also had paths through 0x263094f or
0x1a9862e/0x2630985, then 0x262c353/0x18e9e6c. Thus the message APIs are not
exclusive to the steady main-loop phase. Every sampled live depth was 1, while
the synthetic Windows probe demonstrated depth >1: absence in this sample is
not proof that the game cannot reenter dispatch.

**What this establishes:** a concrete, live-observed game-thread candidate and
its common main-loop caller. It does not establish entity-creation safety,
resource ownership, a visual cutscene gate, or a world-mutation phase. No entity
creator or transform setter was called. A future experiment should restrict
itself to the reviewed caller path and game-thread identity, reject nested or
unavailable-world states, and validate the complete creation/lifetime contract.

Additional static caution: the resource helper at 0x1dde990 (used by
`Entity_ExportedGetResource`) follows a descriptor and can execute reference-count
operations (`lock xadd` at 0x1dde9cf) on one branch. Treat it as an engine call
with side effects, not as a substitute for a guarded read from a worker thread.


The run was stopped by recorded process identity. Both copies passed post-run
source/copy verification; original saves were unchanged immediately and after a
30-second settle check. Both copies received matching source-built proxy and mod
DLLs and fresh isolation/module/network probes before the run. The observer is
opt-in and changes no mod wire identity. Windows scope and enforced-network gate
mutants were also rejected by the fixture probe. These checks do not turn the
candidate into a verified entity-mutation API.


### First guarded creator experiment: missing player controller (2026-11-02)

An opt-in dispatcher successfully entered the mod on the observed startup/main
thread, after an empty PeekMessage return, with the independently checked return
chain `1a19387 -> 1a17aec -> 263d2ae -> 263a869 -> 263753d`.
This verifies callback delivery, not a world-job barrier or mutation contract.

One supervised native creator invocation using the loaded local Sam's resource
crashed before returning an actor. No transform setter or removal call ran.
The owner-provided crash stack contains `23806eb -> 237451c -> 23500e4 ->
237c5c3 -> 271fa8e -> 373a94d -> 351b5c5 -> 27e6510 -> 26b8b00`.
The faulting instruction writes through RCX, which was zero.

Static inspection of the pinned executable explains that null: component init
at 27e63c0 looks up metadata 4bb2770 through helper 22b2a60 at 27e6476.
That metadata names DSPlayerController. The returned component is retained in
R14 and at component+418; at 27e650b it is passed as RCX to 26b8b00 without
a null check. That callee starts by writing RCX+5370. This matches the observed
stack and null register. The generic resource path did not supply this required
controller. Whether other player-only parts (camera/input/BB/cargo) can be
safely cloned remains unknown.

Creator helper 2374400 calls activation 234f3e0 before return site 237451c;
its placement-flag check follows that activation. Changing placement or the
optional tagged value does not address this failure. Do not bypass the null
write or skip the initializer: subsequent calls need the same controller.
The native local-Sam backend now refuses resource approval before calling the
creator, emits `resource_rejected`, and has no config override. A missing
approval callback also refuses. A mutant proves that removing this gate lets
an unapproved fake resource reach creation. Next research needs a standalone
NPC resource or the supported player-controller construction context.

Additional ABI verification: Remove at 2361c20 tail-jumps to 234caa0, whose
second argument (DL) selects immediate versus queued removal. Pass false for
the reference-owning queued task; typed host and Windows stubs test this.
The creator's fourth argument is a non-null C string, not a string object.
Its rotation occupies three 16-byte rows; the transform setter reads 60 bytes.

The failed test copy was stopped by process identity. Original saves were
unchanged immediately and after a 30-second settle check; both copies passed
post-run source verification (archive contents were not rehashed). These checks
do not make the failed creator call safe.


### Player context supplies the missing controller before activation

Static follow-up on the same pinned executable (no new engine calls):

| RVA | Observed behavior |
|---|---|
| 2638880 | Player-specific entity assignment. When player+78 is true and +79 false, constructs DSPlayerController then attaches it to the target entity. |
| 26388e2 / 26388e9 | Loads DSPlayerController metadata 4bb2770, calls generic component construction 22b4fc0 with R8=0 and a generated 16-byte identifier in R9. RDX comes from global 4e14178; its exact type is unknown. |
| 26388ee–26388f4 | Passes the returned controller as RDX, target entity as RCX, to attachment thunk 2348210 -> 2348220. |
| 26388ff | Calls base player assignment 23c7e50, which replaces player+70 at 23c7f21. Calling this with the local player would change its controlled entity. |
| 37447e0 | Player/context creation helper: calls 2380920 to allocate/initialize a 0x1f0-byte creation context, writes player pointer at context+f8, related entity pointer at +98, and creation mode at +94. Calls 237dc10, then 2375ba0 to obtain the result. |
| 2373720 | Context-driven creation: resource virtual+80 call at 23737aa, owner callback virtual+80 at 2373877 using context+f8, then component activation 234f3e0 at 23738b8, then world-manager virtual+18 at 2373905. |

This independently explains the ordering absent from generic export 2374400:
the owner callback can attach the required controller **before** activation.
`PlayerGame_ExportedSwitchPlayerCharacter` 2642cf0 jumps to 262a030, which queues
a task carrying callback 26398c0. That callback invokes 37447e0 on its player,
then performs another player assignment. These are player replacement paths,
not a proven independent-puppet API. Do not call them with the live local player.
An isolated player context would need construction/registration, input/camera
exclusion and cleanup contracts; those remain unknown.

The metadata catalog (`find_actor_types.py`) finds these concrete resource leads:

| Type | Metadata RVA | Getter RVA | Vtable candidate RVA | Factory slot +80 RVA |
|---|---:|---:|---:|---:|
| HumanoidResource | 4ec16b0 | 3737880 | 3f3bb98 | 2366570 |
| DSPlayerEntityResource | 4bc92e0 | 2711b90 | 3d7d010 | 2366570 |

DSPlayerEntityResource derives from HumanoidResource, which derives from
ControlledEntityResource. Both factory slots jump to 2373940, which asks virtual
+68 for the entity type, allocates through 1a8eee0 and copies the transform.
Virtual +68 on DSPlayerEntityResource resolves through getter 270fd70 and
pointer slot 4bb59c8 to DSPlayerEntity metadata 4bc6e00. On HumanoidResource,
getter 3737030 reads slot 4ebf408, whose static value is Humanoid metadata
4ec1530. Thus the shared factory can allocate different entity classes.
These are static initialized pointers; the read-only catalog labels the result
`static_entity_type` rather than claiming a runtime class/lifetime observation.
Sharing a factory does not mean sharing a spawn contract. HumanoidResource is
a possible NPC lead, not an approved standalone resource; no live instance has
yet been identified by the new collector. Its bounded read-only heap inventory
is available as `read_actor_resources.py`; no production DLL or build identity
changed in this follow-up.

### Live resource inventory: NPCs are not HumanoidResources (2026-11-02, read-only)

One supervised run of one copy, early Episode 1, with only query/VM_READ handles:
no injection, no engine call, no creation. The whole private address space
(about 10.4 GB) was scanned to exhaustion more than once, including while the
player carried the episode's corpse.

- `read_actor_resources.py` found only the player's resource
  (`ds/entities/player/ds_player_character`, DSPlayerEntityResource). No plain
  HumanoidResource was loaded at any point.
- A wider scan over every vtable whose slot 0 is a reviewed type getter with
  ControlledEntity or ControlledEntityResource ancestry found no Humanoid entity
  other than DSPlayerEntity. The loaded NPC resources are
  **DSControlledEntityResource** (`ds/entities/npc/mule/pe_mule`,
  `.../pe_demens`, catchers, gazers, animals) and DSDeliveryBotEntityResource,
  plus many cinematics resources.
- Resource layout matches the player's: +30 path string, +48
  SphereBasedActivationPolicy (NPC), +98 Decima array (count/capacity, pointer)
  of 40 component resources, +140 DSNpcImpactEffectResource, +148
  DSGroundMoveEntityUpdaterResource. The MULE component list has no
  DSPlayerPadOperator, Camera, DSPlayerInventory or DSPlayerLife component, which
  the player's list has. It does have AI, combat, patrol, weapon and Odradek
  components (DSAIIndividual, AIHelper, DSPatrol, DSNpcCombatSettings,
  DSMuleWeapon, DSMuleOdradek); demens adds DSDemensComponentResource.
- A bounded (4 GiB, incomplete) reverse-pointer scan attributed no live
  DSControlledEntity to the MULE resources: loaded, apparently not instanced.

So the HumanoidResource lead does not reach NPCs in this build. A MULE-family
resource is a loaded standalone NPC candidate without the player-controller
dependency, but it carries hostile AI. Whether its activation needs other
context, and how to keep its AI inert, are unknown. The creation quarantine is
unchanged and nothing here approves a spawn.

### MULE resource creation path, read statically (2026-11-02, nothing called)

All five ControlledEntityResource classes share factory slot +80 (2366570 ->
2373940). They differ in the entity-type slot +68, which reads a static class
pointer:

| Resource | +68 getter | Entity class created | +70 / +78 |
|---|---:|---|---|
| DSControlledEntityResource (MULE, demens, cinematics) | 2da4880 -> 4c5d438 | DSControlledEntity | DSControlledEntityRep / NetControlledEntity |
| HumanoidResource | 3737030 -> 4ebf408 | Humanoid | HumanoidRep / NetHumanoid |
| DSPlayerEntityResource | 270fd70 -> 4bb59c8 | DSPlayerEntity | |

The failed Sam creation faulted in function 27e63c0, which is DSPlayerState
vtable 3d82348 slot +2c8. DSPlayerState (metadata 4bcf7d0) is a
**HumanoidSubState**, built from the player resource's +2e0 array of
DSPlayerStateResource. Its `FindComponentByType` lookup (22b2a60) for
DSPlayerController (a HumanoidController component, metadata 4bb2770) returned
null. The MULE resource object ends before +2e0 and creates a DSControlledEntity,
which is not a Humanoid. So this crash path has no static reason to run for a
MULE resource. This is untested, and other activation requirements are unknown.

Exports for keeping a created NPC quiet, resolved with the method above
(calibrated on GetPosition 2362af0, Player GetEntity 2361b80):

| Export | Impl RVA | Reading |
|---|---:|---|
| `Entity_ExportedFindComponentByType(entity, rtti)` | 2361990 | null checks; `22b2a60(entity+90, rtti)` |
| `Entity_ExportedFindComponentsByType` | 23619b0 | |
| `EntityComponent_ExportedRemove(component)` | 23618c0 | `235b930(component+48 /*owner*/, component)` |
| `Entity_ExportedSetSleeping(entity, bool, byte)` | 2361e20 | sleeping flag is bit 8 of entity+88; vtable +a0 sleep / +a8 wake |
| `Entity_ExportedSetFaction` / `SetDefaultFaction` | 2361d00 / 2361c90 | |
| `Entity_ExportedSetInvulnerable` / `SetVisible` | 2361d90 / 2361e90 | |
| `AIIndividualComponent_ExportedGetAIIndividual` | 241dd20 | returns component+50 |
| `AIIndividual_ExportedIncrementIgnoreOrderCount` | 2562e10 | |
| `DSNpcCombatSettings_ExportedSetCombatSettings` | 2b9e550 | |

Component class metadata for the MULE's AI-related parts: DSAIIndividualComponent
4c289a0, AIHelperComponent 4af5990, DSPatrolComponent 4c1d300, DSMuleComponent
4c116f0, DSMuleWeaponComponent 4c12540, DSNpcCombatSettingsComponent 4c1c6b0,
DSMuleOdradekComponent 4c15b60, DSNoticeAndKnowledgeReceiveComponent 4c23180,
DSGroundMovementComponent 4c60400, DSDemensComponent 4c1abb0.

Candidate plan for a later supervised experiment (not authorized; the creation
quarantine still refuses every native resource): create from a loaded pe_mule
resource far from the player, then immediately make it invulnerable, neutral and
quiet (find and remove the AI, patrol, weapon and Odradek components, or sleep
it), and drive it with SetWorldTransform. Unknowns: whether removing components
after activation is safe, whether sleeping stops animation as well as AI, what
the NPC looks like as a stand-in for the partner, and the entity lifetime under
world streaming.

## The resident Sam double: DSPlayerAKukanEntity (2026-11-02, read-only)

The pinned build ships two Sam doubles that are plain `Entity` subclasses (not
`DSControlledEntity` or `HumanoidEntity`), built by the shared entity factory
2366570:

| Class | Resource vtable | Entity vtable | Notes |
|---|---:|---:|---|
| `DSPlayerAKukanEntity` | 3d7dcf0 | 3d7de48 (type getter 2711a80) | resource `ds/entities/player/akukan/ds_player_akukan_entity`; depends on the Sam model, Sam's morpheme network, facial rig, voice and the Odradek |
| `DSPlayerMotionEntity` | 3d7d490 | 3d7d5e8 (ctor 26f4f00) | used by one mission scene only; not loaded in the runs below |

"AKukan" is read here as 亜空間 (subspace), so the double is probably Sam's
private-room body. That is a guess and unverified. Game code at 2922cc0 treats `DSPlayerEntity` and
`DSPlayerAKukanEntity` alike when fetching `DSBaggage2CarrierComponent`.

Live, read-only, with the porter in open world in Episode 1 (two runs):

- One live `DSPlayerAKukanEntity` exists. It has 15 components: AKukanConfig, AKukanInventory,
  Baggage2Carrier, Voice, CustomSound, MorphemeAnimationManager, DSTalk,
  FacialRigManager, FacialRig, DSCoverAndAnimModel, DSCoverModel, ArtPartsData,
  NodeGraph, SkinnedModel and FixedPositionMover. It has no AI, controller, pad
  input or player state (which is what crashed the Sam clone above).
- It is parked at (0,0,0) with identity rotation, asleep and hidden. Its flags at
  entity+88 read 0x232850551; the porter's read 0x232854453.
- Flags at entity+88: bit 1 = visible (SetVisible 2361e90 → 235f640 sets or clears it),
  bit 8 = sleeping (SetSleeping 2361e20 calls vtable +a0 to sleep and +a8 to wake;
  the third argument is passed through to +a0 and its meaning is unknown).
- Entity tree, as walked by the engine's own transform propagation
  (235bf60): +70 parent, +78 first child, +80 next sibling. A dynamic child array
  also sits at +240 (count, capacity) / +248 (pointer).
- Pointer path, checked by type and by the child's parent slot at every step:
  porter `DSPlayerEntity` → child `CameraEntity` (vtable 3d198e8, getter
  242daf0) → child `DSPlayerAKukanEntity`. The double's parent chain is exactly
  `CameraEntity` → `DSPlayerEntity`. A complete reverse-pointer scan of the
  process found it referenced only by its own components and children, AI notice
  publishers, render effects, a shared gimmick and the position telemetry. No
  global singleton holds it.
- SetWorldTransform (2361ea0 → 22dff80) copies the 60-byte block to entity+c8
  under a lock at entity+288, sets flag bit 0, then 235bf60 sets bit 4 on the entity
  and its subtree. The double sat at the world origin while its parent camera
  moved with the porter, so placement is not simply relative to the parent.
  A woken double does NOT keep a position set this way. Its `FixedPositionMover`
  (vtable 3d0d270, at double+b0, owner at mover+48) stores its own 60-byte
  transform at mover+58 (position) / +70 (rotation). Its constructor 23b1eb0 sets that
  to origin/identity, and its update (vtable slot 38, 23b6dc0) copies it back into the
  entity through 22dff80 every frame unless the entity sleeps. No setter was found, so
  the backend writes mover+58 as well, and restores it on release.

Live results, 2026-11-02, unattended disposable copies with networking enforced:
- one copy, `echo`: the double appeared beside the porter and followed him;
  release restored the resting flags exactly (0x232850551) at the origin.
- two copies from the same save, `peer`: each copy showed the other porter
  through its own double. The double's position in one copy matched the other
  copy's porter exactly. Saves were unchanged and both copies verified after every run.
- the double has no locomotion animation yet. It runs a different animation network from
  the porter (`sam/references/akukan`; its variable ids differ). That network does contain
  `Speed`, `DSMove`, `Move_Float` and `Move_Float_Interp`, and the backend sets them through
  `AnimationManager_Exported{Init,Set}{Bool,Float}AnimationVariable`
  (2397050/23970b0/23971e0/2397220; `Init(manager, name)` returns an index). The ids are valid
  and the manager updates (`Distance_To_Camera` changes), but the double holds a near-bind pose
  while moving. The prefetch index explains why: `sam/references/akukan` is 1.0 MB against
  121 MB for the porter's `sam/sam`, so the private-room network carries almost no clips.
  A walking double needs the `sam/sam` network.
- Walking works (2026-11-03, live). The manager (vtable 3d09910) holds its
  `MorphemeAnimationManagerResource` at +30 and its network instance at +a0. That instance's
  `MorphemeNetworkDefResource` is at instance+38 (the double's def has 0x1d3 entries, the
  porter's 0x2da3). The manager method 23a2fc0(manager) builds a new instance from
  resource+d8 into +a0. It neither frees nor unregisters the old one, so it is not torn down.
  The backend points the double's resource+d8 at the porter's definition only for the call, then
  restores it. It keeps the old instance pointer and puts it back on release (the borrowed
  instance stays allocated). With the porter's network, the same
  `Speed`/`DSMove`/`Move_Float`/`Move_Float_Interp` settings give a normal walk.
  The borrowed instance is kept after release and swapped back in on the next borrow of the
  same manager, verified live over three cycles. A world reload builds new managers, so a new one
  is made.
- Animation variable slots: manager+50 is the u32 count and +58 points to 48-byte slots
  (+0 a pointer to the C-string name, +8 the network control id or -1, +20 the value as an f32
  or a byte). Measured on the porter: walking sets `Speed`, `DSMove`, `Move_Float(_Interp)`,
  `RealMoveSpeedInterp`, `Move_Speed_Config` and `RealSpeedFrame`. Sprinting sets `Sprint` and
  `RunToDash_Rate(_Interp)`. Crouching sets `Is_Crouch`, `Crouch_Rate` = 1 and
  `Stance_Change_Level`. The mod reads `Sprint` and `Is_Crouch` read-only into the presence
  flags (`ds::Sprint`, `ds::Crouch`), and the receiver sets `Sprint`, `Is_Crouch` and
  `Crouch_Rate` on the double. Jumping sets `Is_Jump`, `Action_Move` and `Is_Actions`, and clears
  `Is_BasicAction`. These, with `Is_Fall` and `Is_Landed`, travel as bits in the wire `action` field,
  and the receiver sets the same bools on the double (`Is_BasicAction` = not `Is_Actions`), which
  makes it jump.
- The double's three `DSPlayerAKukanEffFloorEntity` children (vtable 3d7db80) draw the
  private room's yellow floor ring under it once it is shown. Parked, they are hidden and asleep.
  Waking the double wakes them, and an awake floor effect shows itself again every frame, so
  SetVisible alone does not hold. While the double is borrowed, the backend records each one's
  visible and sleeping state and hides and sleeps them on every tick. On release or yield it
  restores the recorded state.
- Hand-back order, the same on release and on yield: the mover's resting target first (the game never sets it, so a
  stale target would pull its own double to the partner's last position every frame), then the animation network,
  then the floor effects.
- Game events: `DSGameState_sExportedIsStoppingGameEvent` (2dd3760) takes no arguments and returns a bool. It
  checks game-state flags (global 7bf6e88, +8/+10 against a mask) or 26303b0. Live, it is true from the title menu
  through loading and the start-up notices, and false in gameplay. During a local event the mod hands the double
  back and marks its presence like the private room, so the partner hides the player instead of showing a porter
  frozen in a cutscene pose.
- Private room signal: the game wakes and uses a porter's own double for the private room. When that double has been
  yielded to the game, or is awake or shown while the mod is not driving it, the mod sets `ds::PrivateRoom` in its
  presence, and the partner's core then hides that porter (the partner's double is released). On two copies there
  were no false signals before, during or after borrowing. A live private-room entry is untested: the test save
  carries a corpse bag, and the game refuses the private room with one ("Cannot enter to the privateroom :
  CorpseBag").
- the exported SetWorldTransform also runs 235bf60, which marks the entity and its subtree
  as transform-changed. Calling it every tick froze the double mid-stride, so driving now
  only updates the mover target and lets the game move the entity.

`mod/double.cpp` borrows this entity as the partner's body. It only takes the
double when the game has it asleep and hidden. It stops calling as soon as the
double's flags or position differ from what it set (the game is using it), and it
puts it back to its saved resting transform, hidden and asleep, when there is
nothing to show. It never creates or removes anything; its only direct write is
the mover transform above. Unknowns: how to drive its locomotion animation, and whether the private room
still works normally after the double has been borrowed and released.

## Cargo (research for shared-world cargo, 2026-11-03, static)

Script exports of `DSBaggage2Manager`, resolved with the `lea r8,[name]` / `lea rax,[impl]` method:

| Export | Thunk | Implementation | Reading |
|---|---:|---:|---|
| `CreateBaggage` | 2cd1450 | 2cd6b20 | ecx = cargo type id, rdx = 60-byte world transform; fills a request (mask 0x1fffff, kind 4) and queues it with 2c2a9c0 on the manager (global at 7bf4968) + 46d9f0. Creation is asynchronous and returns nothing. |
| `CreateAndAddBaggageToPlayer` | 2cd1440 | | |
| `DeleteBaggage` | 2cd1460 | | |
| `ExtractBaggageInfo` | 2cd14d0 | (inline) | |
| `GetAllBaggages` | 2cd15a0 | 2cd74f0 | filter arguments (ecx id/mask 0x1fe000, r8b, r9b); builds a large list on the stack |
| `GetBaggagesInRange` | 2cd15d0 | 2cd7ae0 | |

Live and read-only, early Episode 1: there are 264 `DSBaggage2Entity` objects, and 263 of them are a hidden,
sleeping pool (flags 0x30040540). The one active entity is the cargo the porter carries. A cargo entity has the
standard entity layout: components at +98 (interaction, anti-gravity, stencil, sound...), `PhysicsMover`
at +b0, `SkinnedModel` at +b8, `AIFaction` at +178 and `ArtPartsData` at +180. The cargo data itself (type,
owner, state) is in the single `DSBaggage2Manager`'s `DSBaggage2` records:
- The manager holds 511 records inline at +20, with a stride of 0x240.
- Record fields: +18 slot, +20 a state bitfield (not a type id: the same corpse record read 0x205 and later 0x105,
  perhaps the holder in bits 8-9), +28 a `DSGameBaggageListItem*`, and +c8 the item hash.
- List items (stride 0x70): +20 is the name text resource (a string at its +20, for example "Resins ({0})"),
  +28 the description, +30 the hash and +50 small floats (weight?).
- Early Episode 1 has 35 live records: metals, resins, boots and the corpse the porter carries.
- A cargo entity points to its record at entity+330.
- Record +38 is the world position as 3 x f64; it is zero for equipped items, and distant world cargo is listed too.
- Record +30 holds two floats, probably durability current and max (900/900, 1500/0).
- Record +1c reads 0xfffffe00 for loose or equipped items and 0 for the carried cargo, so it may be the holder. Unconfirmed.

`CreateBaggage` and `CreateAndAddBaggageToPlayer` work live (2026-11-03). Their id is the cargo **item hash**
(`DSGameBaggageListItem`+30, also record +c8; for example resins = 0x0782598d). Called on the game thread,
`CreateBaggage(hash, transform)` put a real "Resins (40)" crate on the road at the requested point: a new world
record (state 0x107) whose +38 position is the spawn point. `CreateAndAddBaggageToPlayer(hash, false)` adds a record
too. Earlier attempts with other numbers were silently ignored, and the "+4c" exit in 2c470e0 belongs to a different
branch, not to this path.

Shared cargo, first version (`mod/cargo.{h,cpp}`, core CARGO events):
- **Read:** the mod reads the record table through the manager global (image 7bf4968, vtable 3dd9338) four times a
  second, read-only.
- **Announce:** a record counts as placed by this porter when it is new within 15 m of him, or when he held it
  (within 2.5 m) and it now stays put while he is at least 3 m away. It must also have settled for 1 s. Each
  placement is announced once, as {item hash, position}.
- **Recreate:** the partner calls `CreateBaggage(hash, transform)` on its game thread. It first registers the
  expected item and point, so the record that appears for it is marked as a mirror and never announced back.
- **Live, two copies:** a crate created on one copy appeared in the other at the same coordinates, with no echo.
- **Not yet (then):** picking cargo up and ownership, a real put-down by the porter, and cargo far from the partner.
  The first two are done and were driven live on 2026-11-07 (below).

### Cargo, live 2026-11-07: where an item is, and deleting one

- Record byte `+21` (bits 8..15 of the `+20` word) is where the item is: **1 in the world, 2 carried by the porter**.
  The carried corpse read `0x205`; when Sam sat down to rest and put it down, the same record read `0x105` and its
  position dropped to the ground. Equipped items read `0x200`.
- Record `+84` is a persistent cargo id for game-placed cargo (`0x2003`, `0x202e`, `0x2061`...; repeated at `+f8`).
  Cargo made by `CreateBaggage` has `0x1fffff` there (no id).
- `DeleteBaggage` (2cd1460 -> 2cd6d40) queues a kind-2 request with its argument at request `+8`. The queue loop
  (2c95540, branch at 2c95b2b, taken when request `+4` is -1) masks the argument with `0x1fff`, uses it as the
  **index of the record in the manager's array** (`manager+20+index*0x240`) and goes on only when record byte `+21`
  is 1, 2 or 8. Live: `delete 7` removed the crate in record index 7 (36 -> 35 records); its `+20` value (263),
  its `+18` slot (65) and a `+84` id (8238) did nothing.

### Cargo, live 2026-11-07 with keyboard input: the porter's own pick-ups and put-downs

- **Carry frame.** While an item is on the porter's back, its record `+38` position is not a world position: it holds
  values near (100, 100, 20) that jitter with his walk. Put down from the cargo menu (Offload, then Confirm), the record
  reads `0x107` at once but keeps the carry-frame position for over a second; then the real place is written (here
  1692.4, 1056.5, 135.7, at his feet). The first build announced the carry-frame place to the partner. An item in his
  hand (grabbed with a mouse button) holds world coordinates. The partner's double was parked at (100, 100, 20) too
  while the game was on the title screen.
- **Kind.** Record `+20` bits 0..7: 7 on every ordinary item seen (crates, materials, copies made by `CreateBaggage`), 5
  on the president's corpse (story cargo; holder `+1c` 0 where ordinary items read `0xfffffe00`), 0 on equipped gear.
  After a reload with Sam resting, the corpse lay on the ground and was announced as a put-down; only kind 7 is shared
  now.
- **Controls (PC defaults):** E carries an item on the back; holding the right or left mouse button grabs it into
  that hand, and a left click puts it down; Tab opens the map, where clicking on Sam opens cargo management (Enter on
  an item opens its commands; Confirm is a button at the bottom left that takes a mouse click). Taps under about
  300 ms are sometimes lost at 20 fps.

## Outfit (2026-11-03, read-only)

The double has no backpack or hood. The porter's backpack, pouches and other gear are about 30 attached child
entities (plain `Entity` and groups of `DSAttachOrPhysicsEntity`), not body art parts, so outfit parity means
attaching gear entities to the double. Body art parts are switched by `ArtPartsData_sExportedEnableModelPartFromHash`
(22b4f80 -> 22b5090: entity, part name, on). The name is hashed with CRC32C (190fe50) and looked up in the 52-entry
table of the entity's `ArtPartsData` (entity+180, +100 count, +108 array). The porter and the double share that
resource. The archive lists 133 Sam art parts (`part_sam_part_hoodotter`, caps, boots, suits, ...).

Outfit route, static and untried: `ArtPartsVariation_sExportedApplyArtPartsVariation` (22bc100) takes an entity and a
variation resource. It adds a component built from that resource (234ae90, refused when entity+88 bit 31 is set),
then applies it (22bae20). Applying the porter's variation to the double would need the added component removed on
release (`EntityComponent_ExportedRemove`, 23618c0) so the private room is unchanged. It would cover the suit and
hood, but not the backpack. Live, though, the porter has no variation component: 68 components, including `ArtPartsData`,
`ArtPartsShaderVariableComponent` and `BoostMeshStreamingForArtPartsDataComponent`. His look is the art-part states
plus attached gear, so this route does not reproduce it.

## The partner's own body: the engine's spawn path, read from RTTI (static, 2026-11-03, nothing called)

The double is the game's own private-room Sam, so the game takes it back for the private room and cutscenes. Co-op mods
elsewhere (Skyrim Together's proxy actors, the Horizon Zero Dawn Remastered entity spawner) give each remote player a
character of its own, created through the engine. Decima keeps a type descriptor for every class in `.data`, and that
descriptor names the constructor and the fields. Compound descriptor: `+0` id (`-1`), `+4` kind (4 = compound),
`+6` base count, `+7` field count, `+14` size, `+18` alignment, `+20` constructor, `+28` destructor, `+40` name,
`+60` bases (16 bytes each: type, offset), `+68` fields (0x38 bytes each: `+0` type (null = category heading),
`+8` u16 offset, `+a` u16 flags, `+10` name, `+18` getter, `+20` setter). Found by the name string's address appearing
at descriptor+0x40 after an `ffffffff 04` head.

| Type | Descriptor | Size | Constructor / destructor | Fields used |
|---|---:|---:|---|---|
| `Spawnpoint` : `SpawnpointBase` : `ParentWorldNode` : `WorldNode` | `0x4b1dbc0` | 0x1b0 | `0x23cd210` / `0x23cd230` | `ObjectUUID` +8 (GGUUID), `Orientation` (WorldTransform, accessor: getter `0x1dfe580`, setter `0x1dfe5c0`), `SpawnSetup` +88, `EntityResource` +90, `FactsArePersistent` +b0, `AutoSpawn` +b1 |
| `Entity` | `0x4afcdb0` | 0x330 | `0x2330720` / `0x2330780` | `Resource` +58 (the entity's EntityResource), `Model` +b8 |
| `DSPlayerAKukanEntity` : `Entity` | `0x4bca020` | 0x7a0 | `0x26eeb20` / `0x26eeb40` | none of its own |
| `EntityResource` | `0x4b02810` | | | |
| `SpawnSetup` | `0x4ed4830` | | | |

The spawner's recipe, read-only reference (Nukem9/hzdr-gameplay-tweaks, `EntitySpawnerWindow.cpp`): build a
`Spawnpoint` through its RTTI constructor, set `ObjectUUID` (fresh), `Orientation`, `SpawnSetup`, `FactsLifetime` 0
and `AutoSpawn` false, call the spawnpoint's Spawn on a job, then GetEntity. In ds.exe these are
`Spawnpoint_ExportedSpawn` (`0x23da6c0`), `Spawnpoint_ExportedGetEntity` and `Spawnpoint_ExportedDespawn`.

Plan for the partner: the double's own resource (`double+58`) is a Sam body that is already loaded and made to be
puppeted (no player controller). A spawnpoint with `EntityResource` set to it (or `EntityResource_ExportedCreateEntity`
`0x23806a0` directly) should give a second Sam that belongs to the mod alone. It would be driven exactly like the double
(mover target, the porter's animation network) and despawned on disconnect. It has not been called yet. Open: whether
the spawnpoint must be registered in a world-node tree, the memory owner for a refcounted `RTTIRefObject` built
outside the engine allocator, and whether the spawned Sam has a FixedPositionMover like the double.

### The engine's common entity creator (static, 2026-11-03, nothing called)

`Spawnpoint_ExportedSpawn` (`0x23da6c0`) only proceeds when spawnpoint `+188` and `+189` are 0 and `+138` is non-zero
(probably "active in a loaded world", set by the world-node tree), then calls vtable slot 28 (`0x23d8580` ->
`0x23d8b10(spawnpoint, false)`). A spawnpoint built by the mod outside a world tree would therefore be refused
silently. The spawn path ends in `0x2380920`, which 33 game sites call (Spawnpoint, EntityResource exports, gimmicks):

`0x2380920(Ref* out /*rcx, zeroed first*/, const WorldTransform* at /*rdx, 0x3c bytes: 3 f64 position + 9 f32
rotation*/, Resource* resource /*r8*/, void* unknown /*r9*/, const GGUUID* id /*stack +0x20*/)`

It allocates a 0x1f0-byte object, initialised by `0x236cfc0` (vtable `0x3d07fa8`, refcount at `+18`, the transform
copied to `+50`, the id to `+38`), and stores it in `*out` with a reference taken. It also asks the global at
`0x7ae90e0` (vtable slot 3, mode byte 5) for the resource. **Reading:** the result is an asynchronous creation
request, not the entity. The entity appears once the resource is ready, which for an already loaded resource (the
double's own, `double+58`) should be within a frame or two. Caller at `0x2373d6b`: transform = object+20, resource =
object+60, unknown = object+80, id derived from object+8. First live step (one copy, owner away): call it once on the
game thread with the double's resource, a transform 3 m beside the porter and a fresh id; log the request's fields
for a few frames; look for a new DSPlayerAKukanEntity in the world. Remove it with `Entity_ExportedRemove`.

### The common entity creator with the double's resource, live (2026-11-07)

One call of `0x2380920` on the game thread with the double's own resource (`double+58`), a transform 3 m beside the
porter and a fresh id never returned to the mod: no log line after the call. The game kept running, but the mod's
runtime lock was still held, so the mod stopped ticking (commands not read, the partner disconnected). A thread
snapshot found no thread inside the creator, and the mod's loop thread was waiting on that lock. Reading: the call
faulted and the game's own structured exception handling resumed the main loop above the mod's frames. As with the
local Sam's resource (2026-11-02), calling the creator directly does not give a second Sam. The remaining engine
route is a registered `Spawnpoint`; until then the partner stays on the resident double.


## Structures (2026-11-07, static and live)

`DSConstructionPointManager_sExportedCreateConstruction` (thunk 2cd1870 -> 2cd6c00) takes `(u8 type, const
WorldTransform*, bool)`. It returns at once when the manager global (image+7bf4960) is null, otherwise it fills a
0x6c8-byte request (type at +0, the transform at +8, the bool at +640, +665 = 1) and queues it on the manager
(+56a278, under the lock at +56a5b8). The type is `EDSConstructionPointType` (enum descriptor in RTTI, 0x31 values),
in Stage / Player / Net triples: 2-4 safe house, 5-7 post, 8-10 watchtower, 14-16 charger, 17-19 rain shelter,
21-23 zipline, 24-26 ladder, 27-29 climbing rope, 30-32 bridge 30 m, 33-35 bridge 45 m; 0 delivery base,
1 preppers' shelter, 20 MULE post, 36 road rebuilder. "Net" is the variant the game uses for structures that came
from other players.

The manager keeps one 0x710-byte record per structure, in a table that ends at manager+0x5699c8 and grows
downwards (a new record took the slot below the lowest one):

| Offset | Field |
|---|---|
| +0, +10 | a pointer to the record class in the image, and the same pointer + 0x10 (+8 is zero) |
| +18 | id (u32): below 10000 the game's own, 10001.. built by this porter, 100001.. Net |
| +1c, +1d | type, category |
| +1e | state: 2 built, 1 the game's existing structure, 0 refused or pending |
| +28 | world position (3 x f64); the game snaps the height of what it builds |
| +40 | rotation (9 x f32, rows: right, forward, up) |

Live, early Episode 1 (Capital Knot City, before the chiral network): `CreateConstruction(26)` (Net ladder) and
`(25)` (Player ladder) built a ladder lying across the road at once (state 2; ids 100001 and 10002). Post, rain
shelter, watchtower, bridge and zipline requests stayed in state 0 with nothing visible during that run, and were
standing, in state 2, after the next load (the game saved them pending). 155 records were in the table, mostly the game's delivery bases and shelters.

Removal: `DSConstructionPointManager_sExportedRemoveConstructionsInArea` (thunk 2cd1bf0 -> 2cd9760) takes `(const
float4* box min, const float4* box max, const WorldTransform* box placement, bool, bool, bool)`. It collects the
structures in the box (2ce0260 on manager+56a258) and marks for removal (`+aa |= 0x40`, `+ab |= 1`) those whose owner
byte `+b2` is selected: the first bool selects owner 1, the second owner 0, the third owner 2. Live: the third bool
removed a Net ladder and the first removed a ladder built by this porter, so **owner 1 = built by this porter,
2 = Net, 0 = the game's own**. A ladder saved from an earlier session came back after loading (its record
reached state 2 only after the mod's first look), so a receiver checks for an existing built copy before building.

### The partner's own body, live (2026-11-07)

`EntityResource_ExportedCreateEntity` (0x23806a0; the existing puppet probe's signature-checked call) **does** create
a second Sam when given the resident double's own resource (`double+58`, read with 0x2362ba0) instead of the
porter's: a new `DSPlayerAKukanEntity` (vtable 3d7de48) at the requested transform (readback exact), which also moved
exactly and was removed on request, with the porter unchanged and the game running. Unlike the parked double it
starts visible and awake (flags `0x232854453`). After parking it (SetVisible false, SetSleeping true) the double
backend took it like the resident double: shown, woken, its mover target set, the porter's animation network
borrowed, driven to the partner (memory readback equal to the partner's position), released and taken again. So
the partner can have a body that belongs to the mod, and the game's own double (private room, cutscenes) can be left
alone. Built only into experiment DLLs so far (`MOD_PUPPET_DOUBLE_RESOURCE`); the default build still borrows the
resident double. Open: one run showed only a small signal icon where the new body stood without being driven
(unexplained), switching bodies while the resident double is borrowed left the resident double standing (it must
be handed back first), and the body's lifetime across loads is untested.

Own-body mode (`puppet.double` command `own`, default build, 2026-11-07): the mod makes the body once per world,
parks it, and drives it with a second Double backend; the resident double is handed back (parked flags, resting
transform) and left to the game. Live: the body was made, taken and held exactly at the partner's position
(memory readback), with the resident double parked. **But only the body's Odradek renders, not Sam.** The made body
has 12 components where the resident double has 15: it lacks `DSFacialRigComponent` (vtable 3cf6050),
`DSCoverAndAnimModelComponent` (3cf5e80) and `DSCoverModelComponent` (3cf55d0), the visible model, and its art-parts
table is empty. On the resident double those three components have no resource pointer (+30 is 0), so the game
builds them in code, probably while it sets the double up for the private room. Finding that step (or adding the
three components another way) is what remains before the partner can use its own body. `peer` (the resident
double) stays the default.

Drawing the own body (2026-11-07, live, one copy, 90 s hang watch per variant):
- The body's `ArtPartsData` (entity+180, vtable 3cf2b18) has get-or-create accessors for the three model components:
  22b2010 (cover, cached at +60), 22b1f80 (cover+animation, +68) and 22b20a0 (facial rig, +70); each builds the
  component with the generic constructor (22b4fc0) and attaches it (2348210). Built this way they are empty (no
  model or resource pointers, unlike the double's). Cover, or cover and cover+animation: not drawn, stable. Any mask
  with the facial rig: drawn, then the game hangs within about 20 s (main thread waiting in ds.exe+1a0873b).
- The real difference: the double's `ArtPartsData` uses the porter's art-parts resource (+30 = the 52-part
  resource); a made body keeps the one-part resource of its entity resource. `ApplyArtPartsVariation` (22bc100) with
  the double's art-parts resource adds a second `ArtPartsData` from it, which builds all three model components
  properly: **the body is drawn and walks, with no hang over 90 s.** It shows stretched-polygon artifacts (long
  lines and a large flat triangle reaching away from the body) at a distance and while walking, with or without
  the borrowed animation network; standing 4 m from the camera it looked clean over 18 s (gear on its back, no
  stretched polygons).
  The per-part state bytes (+2c) equal the double's. Removing the body's original `ArtPartsData` afterwards
  (EntityComponent_ExportedRemove, 23618c0) hangs the game. `own` now uses the apply step.

The stretched polygons (2026-11-07, one run, parts switched one at a time with the research command
`puppet.cargo "part <index*2+on>"`, which calls the toggle worker 22b8fe0 only when part +2c bit 0, the enabled
request, differs): they appear while the body walks (also with no teleport, and not at distance as such) and come
from **part 20 of the porter's 52-part set**, a cable-like part whose far end is anchored to something the made body
lacks. Switching it off once after the body was shown removed the line for that run. Switching it off at
creation did not last (it was on again a few seconds after the body was shown), and enforcing it once a second while
the body was shown made the drawing much worse (very large flat polygons across the view, distant buildings gone).
Correction from two more runs: switched off once, 3 s after the body was shown, part 20 stayed off for 40 s and two
screenshots were clean; but with part 20 confirmed off from memory (+2c 0x0c) a later run still showed the long
line. The line is intermittent, so a three-screenshot search cannot pin it to one part; part 20 is at most part of
it. Not fixed. Next: detect the line from the game's state rather than screenshots (the model's bone or cable
positions), or count dark thin lines in many frames, before blaming a part.
A measured scan (fixed camera, changed pixels against a reference frame outside the porter, HUD and the partner's
path; clean frames score about 500 to 1000, frames with the line 4000 to 16000) failed for a different reason:
after switching parts 0-25 off and 0-19 back on with the toggle worker, the body drew as huge flat polygons across
the view. Switching parts back on this way corrupts the drawing, so part toggling cannot locate the line, and the
earlier one-part conclusions are void. The research command stays for reading states, not for fixes.
The artifact also appears while the body stands still (2026-11-07, n52: a large dark flat polygon from the hand
of the standing body). `Application_sExportedContinueLastSave` called during gameplay does nothing (it works from
the title screen), so a world-change test of the own body needs the pause menu. With `own`, the one-copy regression
passes (the body driven is the made one, 0.032 m from the partner, cargo and structures both ways).

Static lead (2026-11-07): `AddComponentFromResource` (234ae90; callers: ApplyArtPartsVariation 22bc111 and 234b025)
refuses when entity+88 bit 31 is set; with its third argument true it first removes the entity's components of the
resource's component type that were built from the same resource (resource+30 equal), then builds and attaches the
new one (22b5970, 2348220). It never removes a component built from another resource, which is why a made body ends
up with two `ArtPartsData` (its own one-part one and the porter's). The game's double, made from the same entity
resource, has only the porter's, so the game removes the original somewhere else; removing it right after the
apply froze the game, and so did removing it after the body was parked and asleep (before it was shown; live,
hang within 20 s). Next: find that removal (callers of 235b930 near the double's setup: the double's class is
referenced at 26f329e, 26fb1d4 (vtable) and 2711a80, 291160c, 2922d5b, 2cc37b3 (type)), or avoid the second
component altogether (replace the original's resource before the body is activated).
Switching the original one-part `ArtPartsData`'s part off once after the body was shown made no measurable
difference (15 scored frames before and after). Frame scoring with a fixed camera is confounded after about a minute:
an idle porter sits down to rest and the camera moves. Clean measurements need the porter kept standing (input).
The double's class code does not set its model: 26f3270 is its constructor, 26fb1c0 its destructor, and the other
type references (291160c, 2922d5b, 2cc37b3) are is-this-the-double checks followed by component lookups. The
porter's art parts reach the double from outside, most likely from the private-room or outfit system that copies
the porter's look; that system is the next place to look (callers of ApplyArtPartsVariation's registration
22ba011 from scripts, and the outfit/ArtPartsVariation resources it uses).
The double's 15 components by RTTI name: ArtPartsData, DSCoverModelComponent, DSCoverAndAnimModelComponent,
DSFacialRigComponent, DSFacialRigManagerComponent, DSTalkComponent, VoiceComponent, MorphemeAnimationManager,
SkinnedModel, FixedPositionMover, NodeGraphComponent, DSPlayerAKukanConfigComponent,
DSPlayerAKukanInventoryComponent, DSBaggage2CarrierComponent, DSCustomSoundComponent. No outfit component; the
outfit system's exports (`PlayerOutfitComponent_sExportedApplyPlayerOutfit`, `...SetPlayerOutfit`,
`...ApplyPlayerOutfitPart`) and the double's own `NodeGraphComponent` script are the candidates for how its look
is set at load. A made body has the same graph component but is not dressed.
`PlayerOutfitComponent_sExportedApplyPlayerOutfit` (34ef380 -> 34ef670) and `...SetPlayerOutfit` (34ef400 -> 34efaf0)
look up a `PlayerOutfitComponent` (type 4e07de0) on the given entity and do nothing without one; the double and a
made body have none, so these cannot dress them. Remaining candidate: the double's `NodeGraphComponent` script.

